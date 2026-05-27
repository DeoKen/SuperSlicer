///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "DefaultSurfaceGenerator.hpp"

#include <cassert>
#include <cstdint>
#include <map>
#include <vector>

#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_surface_generation.h"
#include "libslic3r/Api/plugin/cpp/ClipperViews.hpp"
#include "libslic3r/Api/plugin/cpp/DataTreeViews.hpp"

namespace slic3r_api { namespace SurfaceGeneration { namespace DefaultSurfaceGeneratorPlugin {
namespace {

const char *k_default_surface_generator_id = "surface.generator.default";
const char *k_surface_generation_group = "step_surface_generation_plugin";
const char *k_no_dependencies[] = { nullptr };
constexpr raw_surface_type k_sparse_internal_surface = RAW_SURFACE_TYPE_POS_INTERNAL | RAW_SURFACE_TYPE_DENS_SPARSE;

int32_t region_infill_extruder_id(const LayerRegion &region)
{
    // Config extruders are user-facing 1-based values. LayerRegionIsland stores
    // the resolved extruder as 0-based because the rest of the slicing pipeline
    // indexes extruders that way.
    const ConfigOption option = region.print_region().config().get("infill_extruder");
    const int32_t extruder_id = option.get_int() - 1;
    assert(extruder_id >= 0);
    return extruder_id < 0 ? -1 : extruder_id;
}

std::map<int32_t, std::vector<LayerRegion>> infill_regions_by_extruder(const LayerIsland &island)
{
    std::map<int32_t, std::vector<LayerRegion>> out;
    for (uint32_t region_idx = 0; region_idx < island.region_count(); ++region_idx) {
        const LayerRegion region = island.region(region_idx);
        out[region_infill_extruder_id(region)].push_back(region);
    }
    return out;
}

std::vector<const layer_region_handle *> region_handles(const std::vector<LayerRegion> &regions)
{
    std::vector<const layer_region_handle *> out;
    out.reserve(regions.size());
    for (const LayerRegion &region : regions)
        out.push_back(region.handle());
    return out;
}

layer_region_island_handle *get_or_create_region_island(const run_ctx_surface_generation &ctx,
                                                        const LayerIsland &island,
                                                        const std::vector<LayerRegion> &regions)
{
    if (ctx.get_or_create_region_island == nullptr)
        return nullptr;

    std::vector<const layer_region_handle *> handles = region_handles(regions);
    const layer_region_handle *const *raw_handles = handles.empty() ? nullptr : handles.data();
    return ctx.get_or_create_region_island(island.handle(), raw_handles, uint32_t(handles.size()));
}

StoredExPolygonCollection clip_infill_areas_to_regions(storage_handle *storage,
                                                       const LayerIsland &island,
                                                       const std::vector<LayerRegion> &regions)
{
    assert(storage != nullptr);

    // Multiple infill extruders inside one island must not share the same fill
    // surfaces. Region raw slices are already non-overlapping, so the split is
    // simply "island infill areas intersected with the union of this extruder's
    // regions".
    StoredExPolygonCollection region_slices(storage);
    for (const LayerRegion &region : regions)
        region_slices.append_copy_from(region.slices());

    ClipperContext clip(storage);
    ClipperOperand merged_regions = clipper_union(clip(region_slices.readonly()));
    ClipperOperand clipped = clipper_intersection(clip(island.infill_areas()), merged_regions);
    return clipped.to_expolygon_collection();
}

void set_region_island_surfaces(const run_ctx_surface_generation &ctx,
                                storage_handle *storage,
                                layer_region_island_handle *region_island,
                                const ExPolygonCollection &areas)
{
    if (ctx.set_region_island_fill_surfaces == nullptr || region_island == nullptr)
        return;

    // The step callback moves this whole collection into the LayerRegionIsland.
    // Building a complete SurfaceCollection first keeps the general data-tree
    // API read-only and makes the ownership transfer a single explicit action.
    StoredSurfaceCollection surfaces(storage);
    surfaces.append(areas, k_sparse_internal_surface);
    ctx.set_region_island_fill_surfaces(region_island, surfaces.mutable_handle());
}

void build_island_surfaces(const run_ctx_surface_generation &ctx,
                           storage_handle *storage,
                           const LayerIsland &island)
{
    std::map<int32_t, std::vector<LayerRegion>> grouped_regions = infill_regions_by_extruder(island);
    if (grouped_regions.empty())
        return;

    const bool single_group = grouped_regions.size() == 1;
    for (const std::pair<const int32_t, std::vector<LayerRegion>> &entry : grouped_regions) {
        layer_region_island_handle *region_island = get_or_create_region_island(ctx, island, entry.second);
        if (region_island == nullptr)
            continue;

        if (single_group) {
            set_region_island_surfaces(ctx, storage, region_island, island.infill_areas());
            continue;
        }

        StoredExPolygonCollection clipped_areas = clip_infill_areas_to_regions(storage, island, entry.second);
        set_region_island_surfaces(ctx, storage, region_island, clipped_areas.readonly());
    }
}

} // namespace

DefaultSurfaceGenerator &
DefaultSurfaceGenerator::instance(orchestrator_handle *orch)
{
    static DefaultSurfaceGenerator s_instance(orch);
    return s_instance;
}

const char *DefaultSurfaceGenerator::id_impl() const noexcept
{
    return k_default_surface_generator_id;
}

const char *DefaultSurfaceGenerator::exclusive_group_impl() const noexcept
{
    return k_surface_generation_group;
}

const char *DefaultSurfaceGenerator::exclusive_group_label_impl() const noexcept
{
    return "Surface generation plugin";
}

const char *DefaultSurfaceGenerator::exclusive_group_tooltip_impl() const noexcept
{
    return "Choose which active plugin converts perimeter fill areas into infill surfaces.";
}

slicing_step_t DefaultSurfaceGenerator::step_impl() const noexcept
{
    return STEP_SURFACE_GENERATION;
}

const char *const *DefaultSurfaceGenerator::dependencies_impl() const noexcept
{
    return k_no_dependencies;
}

int32_t DefaultSurfaceGenerator::priority_impl() const noexcept
{
    return 0;
}

const char *DefaultSurfaceGenerator::progress_message_format_impl() const noexcept
{
    return "Default surface generator: %u / %u layers";
}

void DefaultSurfaceGenerator::setup_run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_surface_generation *ctx = plugin_ctx_as_surface_generation(run_ctx);
    if (ctx != nullptr && ctx->object != nullptr) {
        const Object object(ctx->object);
        progress().add_max(object.layer_count());
    }
}

void DefaultSurfaceGenerator::run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_surface_generation *ctx = plugin_ctx_as_surface_generation(run_ctx);
    if (ctx == nullptr || ctx->object == nullptr || run_ctx == nullptr || run_ctx->plugin_storage == nullptr)
        return;

    const Object object(ctx->object);
    for (uint32_t layer_idx = 0; layer_idx < object.layer_count(); ++layer_idx) {
        throw_if_cancelled(run_ctx);

        const Layer layer = object.layer(layer_idx);
        for (uint32_t island_idx = 0; island_idx < layer.island_count(); ++island_idx)
            build_island_surfaces(*ctx, run_ctx->plugin_storage, layer.island(island_idx));

        progress().increment();
    }
}

void register_default_surface_generator_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, DefaultSurfaceGenerator::instance(orch).c_instance());
}

}}} // namespace slic3r_api::SurfaceGeneration::DefaultSurfaceGeneratorPlugin

#ifdef DEFAULT_SURFACE_GENERATOR_PLUGIN_DLL
extern "C" void register_plugin(orchestrator_handle *orch)
{
    slic3r_api::SurfaceGeneration::DefaultSurfaceGeneratorPlugin::register_default_surface_generator_plugin(orch);
}
#endif // DEFAULT_SURFACE_GENERATOR_PLUGIN_DLL
