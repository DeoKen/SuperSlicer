///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "StepGenerateInfill.hpp"

#include <cassert>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_infill.h"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/Steps/StepPipeline.hpp"
#include "libslic3r/Surface.hpp"

namespace Slic3r::Steps::StepGenerateInfill {
namespace {

struct InfillStepHostContext
{
    Orchestrator *orchestrator = nullptr;
    Print *print = nullptr;
    std::map<std::string, Plugin *> pattern_plugins;
    std::map<Plugin *, plugin_host_context> pattern_host_contexts;
};

Plugin *pattern_plugin_for_id(InfillStepHostContext &host, const char *pattern_id)
{
    const std::string requested_id = pattern_id != nullptr ? pattern_id : "";
    const std::map<std::string, Plugin *>::const_iterator selected =
        host.pattern_plugins.find(requested_id);
    if (selected != host.pattern_plugins.end())
        return selected->second;

    // Presets can outlive plugin activation changes. Falling back keeps the
    // slice usable, while the warning tells the user that the selected pattern
    // id is no longer available in the active plugin set.
    if (!host.pattern_plugins.empty()) {
        Plugin *fallback = host.pattern_plugins.begin()->second;
        const std::string message = "Infill pattern '" + requested_id + "' is not active; using '" +
                                    host.pattern_plugins.begin()->first + "' instead.";
        host.orchestrator->add_plugin_message(Orchestrator::PluginMessageLevel::Warning,
                                              fallback,
                                              INFILL_PATTERN,
                                              message.c_str());
        return fallback;
    }

    return nullptr;
}

int32_t generate_pattern_callback(const run_ctx_generate_infill *ctx,
                                  const char *pattern_id,
                                  const layer_handle *layer,
                                  const layer_island_handle *island,
                                  const layer_region_island_handle *region_island,
                                  const layer_region_handle *primary_region,
                                  const surface_handle *surface,
                                  const expolygon_collection_handle *no_overlap_areas,
                                  const raw_infill_pattern_params *params,
                                  extrusion_entity_handle *output)
{
    if (ctx == nullptr || ctx->host_context == nullptr || params == nullptr || output == nullptr)
        return 0;

    InfillStepHostContext &host = *reinterpret_cast<InfillStepHostContext *>(ctx->host_context);
    Plugin *plugin = pattern_plugin_for_id(host, pattern_id);
    if (plugin == nullptr)
        return 0;

    std::map<Plugin *, plugin_host_context>::iterator host_context = host.pattern_host_contexts.find(plugin);
    assert(host_context != host.pattern_host_contexts.end());
    if (host_context == host.pattern_host_contexts.end())
        return 0;

    plugin_run_context run_context =
        host.orchestrator->prepare_plugin_run_context(INFILL_PATTERN, plugin, &host_context->second);

    run_ctx_infill_pattern payload = {};
    payload.print = ctx->print;
    payload.object = ctx->object;
    payload.layer = layer;
    payload.island = island;
    payload.region_island = region_island;
    payload.primary_region = primary_region;
    payload.surface = surface;
    payload.no_overlap_areas = no_overlap_areas;
    payload.params = params;
    payload.output = output;
    run_context.data = &payload;

    plugin->setup_run(run_context);
    plugin->run(run_context);
    return 1;
}

ExtrusionRole bucket_role_from_raw(raw_extrusion_role role)
{
    if ((role & RAW_EXTRUSION_ROLE_THIN) != 0 || role == RAW_EXTRUSION_ROLE_GAP_FILL)
        return LayerRegionIsland::GAP_FILLS;
    if ((role & RAW_EXTRUSION_ROLE_INFILL) != 0)
        return LayerRegionIsland::INFILLS;
    if ((role & RAW_EXTRUSION_ROLE_IRONING) != 0)
        return LayerRegionIsland::IRONINGS;
    if ((role & RAW_EXTRUSION_ROLE_MILL) != 0)
        return LayerRegionIsland::MILLS;
    if ((role & RAW_EXTRUSION_ROLE_SUPPORT) != 0)
        return (role & RAW_EXTRUSION_ROLE_EXTERNAL) != 0 ? LayerRegionIsland::SUPPORT_INTERFACE :
                                                           LayerRegionIsland::SUPPORT;
    return LayerRegionIsland::INFILLS;
}

void append_extrusion_children(ExtrusionEntityCollection &dst, ExtrusionEntity &src)
{
    if (src.is_nop())
        return;

    if (ExtrusionEntityCollection *collection = dynamic_cast<ExtrusionEntityCollection *>(&src)) {
        dst.append_move_from(*collection);
        return;
    }

    if (src.is_leaf()) {
        dst.append(std::move(src));
        return;
    }

    ExtrusionEntity::Children &children = src.children();
    while (!children.empty()) {
        dst.append(std::move(children.front()));
        children.erase(children.begin());
    }
}

int32_t append_region_island_extrusion_callback(layer_region_island_handle *region_island_handle,
                                                raw_extrusion_role role,
                                                extrusion_entity_handle *extrusion_handle)
{
    LayerRegionIsland *region_island = reinterpret_cast<LayerRegionIsland *>(region_island_handle);
    ExtrusionEntity *extrusion = reinterpret_cast<ExtrusionEntity *>(extrusion_handle);
    if (region_island == nullptr || extrusion == nullptr)
        return 0;

    // Publication is append-only for this step. The generator may call the
    // callback several times for one LayerRegionIsland, once per surface, and
    // all generated paths must stay under the same infill bucket.
    ExtrusionEntityCollection &dst = region_island->mutable_extrusion(bucket_role_from_raw(role));
    append_extrusion_children(dst, *extrusion);
    region_island->remove_empty_extrusions();
    return 1;
}

bool surface_needs_infill(const Surface &surface)
{
    return !surface.empty() && !surface.has_fill_void();
}

size_t count_candidate_surfaces(const Print &print)
{
    size_t count = 0;
    for (const PrintObject &object : print.objects())
        for (const Layer &layer : object.layers())
            for (const LayerSliceIsland &island : layer.islands())
                for (const LayerRegionIsland &region_island : island.regions_islands())
                    for (const Surface &surface : region_island.fill_surfaces())
                        if (surface_needs_infill(surface))
                            ++count;
    return count;
}

void prepare_pattern_plugins(InfillStepHostContext &host)
{
    assert(host.orchestrator != nullptr);
    assert(host.print != nullptr);

    const size_t run_count = count_candidate_surfaces(*host.print);
    for (Plugin *plugin : host.orchestrator->get_active_plugins_for_step(INFILL_PATTERN)) {
        if (plugin == nullptr)
            continue;

        host.pattern_plugins.emplace(plugin->get_id(), plugin);
        plugin_host_context plugin_host =
            host.orchestrator->prepare_plugin_host_context(INFILL_PATTERN, plugin, host.print);
        plugin_host.object_count = run_count;
        host.pattern_host_contexts.emplace(plugin, plugin_host);

        plugin_run_context setup_context =
            host.orchestrator->prepare_plugin_run_context(INFILL_PATTERN, plugin, &host.pattern_host_contexts.find(plugin)->second);
        plugin->setup(setup_context, uint32_t(run_count));
    }
}

void clear_infill_outputs(Print &print)
{
    for (PrintObject &object : print.objects()) {
        for (Layer &layer : object.layers()) {
            for (LayerSliceIsland &island : layer.islands()) {
                for (LayerRegionIsland &region_island : island.regions_islands()) {
                    if (region_island.has_extrusion(LayerRegionIsland::INFILLS))
                        region_island.mutable_extrusion(LayerRegionIsland::INFILLS).clear();
                    if (region_island.has_extrusion(LayerRegionIsland::GAP_FILLS))
                        region_island.mutable_extrusion(LayerRegionIsland::GAP_FILLS).clear();
                    if (region_island.has_extrusion(LayerRegionIsland::IRONINGS))
                        region_island.mutable_extrusion(LayerRegionIsland::IRONINGS).clear();
                    region_island.remove_empty_extrusions();
                }
            }
        }
    }
}

void run_generator_for_object(Orchestrator &orchestrator,
                              Plugin &plugin,
                              Print &print,
                              PrintObject &object,
                              plugin_host_context &host_context,
                              InfillStepHostContext &infill_context)
{
    plugin_run_context run_context =
        orchestrator.prepare_plugin_run_context(STEP_INFILL, &plugin, &host_context);

    run_ctx_generate_infill payload = {};
    payload.print = reinterpret_cast<const print_handle *>(&print);
    payload.object = reinterpret_cast<const object_handle *>(&object);
    payload.host_context = &infill_context;
    payload.generate_pattern = &generate_pattern_callback;
    payload.append_region_island_extrusion = &append_region_island_extrusion_callback;
    run_context.data = &payload;

    plugin.setup_run(run_context);
    plugin.run(run_context);
}

} // namespace

void clean_and_prepare(Print &print)
{
    clear_infill_outputs(print);
}

bool validate_pre(const Print &, std::string *)
{
    return true;
}

bool validate_post(const Print &, std::string *)
{
    return true;
}

void run_step(Orchestrator &orchestrator, Print &print)
{
    // STEP_INFILL is an exclusive step. The selected generator owns the
    // high-level traversal and parameter preparation, while this host wrapper
    // provides callbacks for pattern-service plugins and data-tree mutation.
    Plugin *plugin = selected_or_active_plugin_for_step(orchestrator, STEP_INFILL, &print.full_print_config());
    if (plugin == nullptr) {
        orchestrator.add_plugin_message(Orchestrator::PluginMessageLevel::Error,
                                        nullptr,
                                        STEP_INFILL,
                                        "No active infill generator plugin is available.");
        return;
    }

    InfillStepHostContext infill_context;
    infill_context.orchestrator = &orchestrator;
    infill_context.print = &print;
    prepare_pattern_plugins(infill_context);
    if (infill_context.pattern_plugins.empty()) {
        orchestrator.add_plugin_message(Orchestrator::PluginMessageLevel::Error,
                                        plugin,
                                        INFILL_PATTERN,
                                        "No active infill pattern plugin is available.");
        return;
    }

    plugin_host_context host_context = orchestrator.prepare_plugin_host_context(STEP_INFILL, plugin, &print);
    host_context.object_count = print.objects().size();
    plugin_run_context setup_context = orchestrator.prepare_plugin_run_context(STEP_INFILL, plugin, &host_context);
    plugin->setup(setup_context, uint32_t(print.objects().size()));

    for (size_t object_idx = 0; object_idx < print.objects().size(); ++object_idx) {
        host_context.object_idx = object_idx;
        if (setup_context.is_cancelled != nullptr && setup_context.is_cancelled(setup_context.host_context))
            return;
        run_generator_for_object(orchestrator, *plugin, print, print.object(object_idx), host_context, infill_context);
    }
}

} // namespace Slic3r::Steps::StepGenerateInfill
