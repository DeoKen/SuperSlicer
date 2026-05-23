///|/ Copyright (c) SuperSlicer 2026 Durand R?mi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "StepPostSlicing.hpp"

#include <sstream>

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/Api/host/steps/PostSlicingStep.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/SurfaceCollection.hpp"
#include "libslic3r/Thread.hpp"

#include "StepRunner.hpp"

namespace Slic3r::Steps::StepPostSlicing {
namespace {

bool has_non_empty_expolygon(const ExPolygons &expolygons)
{
    for (const ExPolygon &expolygon : expolygons)
        if (!expolygon.empty())
            return true;
    return false;
}

} // namespace

void clean_and_prepare(Print &) {}

bool validate_pre(const Print &print, std::string &out_error)
{
    bool ok = true;

    for (size_t object_idx = 0; object_idx < print.objects().size(); ++object_idx) {
        const PrintObject &object = print.objects()[object_idx];

        for (size_t layer_idx = 0; layer_idx < object.layer_count(); ++layer_idx) {
            const Layer &layer = object.layer(layer_idx);

            std::ostringstream layer_prefix;
            layer_prefix << "object " << object_idx << ", layer " << layer_idx << ": ";

            if (!has_non_empty_expolygon(layer.lslices())) {
                ok = false;
                out_error += layer_prefix.str() + "layer slices do not contain any non-empty ExPolygon";
            }

            if (layer.islands().size() != layer.lslices().size()) {
                ok = false;
                std::ostringstream msg;
                msg << layer_prefix.str() << "island count (" << layer.islands().size()
                    << ") does not match layer slice count (" << layer.lslices().size() << ")";
                out_error += msg.str();
            }

            for (size_t island_idx = 0; island_idx < layer.islands().size(); ++island_idx) {
                const LayerSliceIsland &island = layer.islands()[island_idx];

                if (island.get_slice().empty()) {
                    ok = false;
                    std::ostringstream msg;
                    msg << layer_prefix.str() << "island " << island_idx << " has an empty slice";
                    out_error += msg.str();
                }

                if (!island.regions_islands().empty()) {
                    ok = false;
                    std::ostringstream msg;
                    msg << layer_prefix.str() << "island " << island_idx
                        << " already has LayerRegionIsland data";
                    out_error += msg.str();
                }
            }

            for (size_t region_idx = 0; region_idx < layer.region_count(); ++region_idx) {
                const LayerRegion &region = layer.region(region_idx);

                std::ostringstream region_prefix;
                region_prefix << layer_prefix.str() << "region " << region_idx << ": ";

                if (!has_non_empty_expolygon(region.get_raw_slices())) {
                    ok = false;
                    out_error += region_prefix.str() + "raw slices do not contain any non-empty ExPolygon";
                }

                if (!region.slices().empty()) {
                    ok = false;
                    out_error += region_prefix.str() + "processed surfaces are not empty";
                }

                if (!region.fill_surfaces().empty()) {
                    ok = false;
                    out_error += region_prefix.str() + "fill surfaces are not empty";
                }
            }
        }
    }

    return ok;
}

bool validate_post(const Print &print, std::string &error)
{
    return validate_pre(print, error);
}

void run_step(Orchestrator &orchestrator, Print &print)
{
    Detail::validate_or_report(validate_pre, print, "Post-slicing pre-step validation");

    std::vector<Plugin *> plugins = orchestrator.get_active_plugins_for_step(STEP_POST_SLICING);

    for (Plugin *plugin : plugins) {
        const size_t run_count = print.objects().size();
        plugin_host_context host_context = orchestrator.prepare_plugin_host_context(STEP_POST_SLICING, plugin, &print);
        plugin_run_context run_context = orchestrator.prepare_plugin_run_context(STEP_POST_SLICING,
                                                                                plugin,
                                                                                &host_context);
        plugin->setup(run_context, uint32_t(run_count));

        // Each worker gets its own plugin_run_context copy and step payload. The
        // shared run_context only carries stable host/plugin callbacks.
        parallel_for(size_t(0), run_count, [&print, plugin, &run_context](const size_t idx) {
            plugin_run_context context_copy = run_context;
            if (context_copy.is_cancelled != nullptr && context_copy.is_cancelled(context_copy.host_context))
                return;

            run_ctx_post_slicing context_step = ApiHost::Steps::make_post_slicing_run_context(print, idx);
            context_copy.data = &context_step;
            plugin->setup_run(context_copy);
        });

        parallel_for(size_t(0), run_count, [&print, plugin, &run_context](const size_t idx) {
            plugin_run_context context_copy = run_context;
            if (context_copy.is_cancelled != nullptr && context_copy.is_cancelled(context_copy.host_context))
                return;

            run_ctx_post_slicing context_step = ApiHost::Steps::make_post_slicing_run_context(print, idx);
            context_copy.data = &context_step;
            plugin->run(context_copy);
        });

        Detail::validate_or_report(validate_post, print, "Post-slicing post-plugin validation");
    }

    //old post-clicing, replaced by plugins
//    this->_max_overhang_threshold();
}

} // namespace Slic3r::Steps::StepPostSlicing
