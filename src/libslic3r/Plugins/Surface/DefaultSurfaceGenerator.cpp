///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "DefaultSurfaceGenerator.hpp"

#include <map>

#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_surface_generation.h"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/PrintRegion.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/SurfaceCollection.hpp"

namespace slic3r_api { namespace SurfaceGeneration { namespace DefaultSurfaceGeneratorPlugin {
namespace {

const char *k_default_surface_generator_id = "surface.generator.default";
const char *k_no_dependencies[] = { nullptr };

Slic3r::PrintObject *to_object(const object_handle *handle)
{
    return const_cast<Slic3r::PrintObject *>(reinterpret_cast<const Slic3r::PrintObject *>(handle));
}

uint16_t surface_region_island_extruder_id(const Slic3r::LayerRegionSetCPtrs &regions)
{
    if (regions.empty())
        return uint16_t(-1);

    const int16_t extruder_id = int16_t((*regions.begin())->region().config().infill_extruder) - 1;
    return extruder_id < 0 ? uint16_t(-1) : uint16_t(extruder_id);
}

std::map<uint16_t, Slic3r::LayerRegionSetCPtrs> infill_regions_by_extruder(const Slic3r::LayerSliceIsland &island)
{
    std::map<uint16_t, Slic3r::LayerRegionSetCPtrs> out;
    for (const Slic3r::LayerRegion *region : island.regions()) {
        if (region == nullptr)
            continue;

        const int16_t extruder_id = int16_t(region->region().config().infill_extruder) - 1;
        // print::apply should have already set the extruder for each region.
        assert(extruder_id >= 0);
        const uint16_t stored_extruder_id = extruder_id < 0 ? uint16_t(-1) : uint16_t(extruder_id);
        out[stored_extruder_id].insert(region);
    }
    return out;
}

Slic3r::ExPolygons group_raw_slices(const Slic3r::LayerRegionSetCPtrs &regions)
{
    Slic3r::ExPolygons out;
    for (const Slic3r::LayerRegion *region : regions)
        if (region != nullptr)
            Slic3r::append(out, region->get_raw_slices());
    return out;
}

Slic3r::ExPolygons infill_areas_for_region_group(const Slic3r::LayerSliceIsland &island,
                                                  const Slic3r::LayerRegionSetCPtrs &regions)
{
    if (regions == island.regions())
        return island.infill_areas();

    // When an island mixes several infill extruders, each region island must
    // receive only the part of the island fill areas owned by its regions. The
    // regions are already non-overlapping at this point, so clipping against
    // their raw slices is enough to partition the infill work by extruder.
    return Slic3r::intersection_ex(island.infill_areas(), group_raw_slices(regions));
}

void build_island_surfaces(Slic3r::LayerSliceIsland &island)
{
    if (island.regions().empty())
        return;

    // Surface generation is intentionally island-wide until an infill setting
    // forces a split. The first split we must honor is the infill extruder:
    // one LayerRegionIsland may be filled by exactly one extruder.
    std::map<uint16_t, Slic3r::LayerRegionSetCPtrs> grouped_regions = infill_regions_by_extruder(island);
    for (const std::pair<const uint16_t, Slic3r::LayerRegionSetCPtrs> &entry : grouped_regions) {
        Slic3r::LayerRegionIsland &region_island =
            island.get_or_add_region_island(entry.second, surface_region_island_extruder_id(entry.second));

        Slic3r::SurfaceCollection &surfaces = region_island.set_fill_surfaces();
        surfaces.clear();
        surfaces.append(infill_areas_for_region_group(island, entry.second), Slic3r::stPosInternal | Slic3r::stDensSparse);
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
    const Slic3r::PrintObject *object = ctx == nullptr ? nullptr : to_object(ctx->object);
    if (object != nullptr)
        progress().add_max(uint32_t(object->layer_count()));
}

void DefaultSurfaceGenerator::run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_surface_generation *ctx = plugin_ctx_as_surface_generation(run_ctx);
    Slic3r::PrintObject *object = ctx == nullptr ? nullptr : to_object(ctx->object);
    if (object == nullptr)
        return;

    for (Slic3r::Layer &layer : object->layers()) {
        throw_if_cancelled(run_ctx);
        for (Slic3r::LayerSliceIsland &island : layer.islands())
            build_island_surfaces(island);
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
