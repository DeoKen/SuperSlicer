///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "StepSurfaceGeneration.hpp"

#include <cassert>
#include <utility>

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_surface_generation.h"
#include "libslic3r/DataTreeFwd.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/PrintRegion.hpp"
#include "libslic3r/Steps/StepPipeline.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/SurfaceCollection.hpp"

#include "StepRunner.hpp"

namespace Slic3r::Steps::StepSurfaceGeneration {
namespace {

LayerSliceIsland *to_layer_island(const layer_island_handle *handle)
{
    return const_cast<LayerSliceIsland *>(reinterpret_cast<const LayerSliceIsland *>(handle));
}

const LayerRegion *to_layer_region(const layer_region_handle *handle)
{
    return reinterpret_cast<const LayerRegion *>(handle);
}

LayerRegionIsland *to_layer_region_island(layer_region_island_handle *handle)
{
    return reinterpret_cast<LayerRegionIsland *>(handle);
}

SurfaceCollection *to_surface_collection(surface_collection_handle *handle)
{
    return reinterpret_cast<SurfaceCollection *>(handle);
}

LayerRegionSetCPtrs region_set_from_handles(const layer_region_handle *const *region_handles,
                                             uint32_t region_count,
                                             const LayerSliceIsland &island)
{
    LayerRegionSetCPtrs regions;
    if (region_handles == nullptr || region_count == 0)
        return island.regions();

    for (uint32_t idx = 0; idx < region_count; ++idx) {
        const LayerRegion *region = to_layer_region(region_handles[idx]);
        if (region != nullptr)
            regions.insert(region);
    }
    return regions;
}

uint16_t infill_extruder_id(const LayerRegionSetCPtrs &regions)
{
    if (regions.empty())
        return uint16_t(-1);

    const int16_t extruder_id = int16_t((*regions.begin())->region().config().infill_extruder) - 1;
    assert(extruder_id >= 0);
    return extruder_id < 0 ? uint16_t(-1) : uint16_t(extruder_id);
}

layer_region_island_handle *get_or_create_region_island_callback(const layer_island_handle *island_handle,
                                                                 const layer_region_handle *const *region_handles,
                                                                 uint32_t region_count)
{
    LayerSliceIsland *island = to_layer_island(island_handle);
    if (island == nullptr)
        return nullptr;

    LayerRegionSetCPtrs regions = region_set_from_handles(region_handles, region_count, *island);
    if (regions.empty())
        return nullptr;

    LayerRegionIsland &region_island = island->get_or_add_region_island(regions, infill_extruder_id(regions));
    return reinterpret_cast<layer_region_island_handle *>(&region_island);
}

int32_t set_region_island_fill_surfaces_callback(layer_region_island_handle *region_island_handle,
                                                 surface_collection_handle *surfaces_handle)
{
    LayerRegionIsland *region_island = to_layer_region_island(region_island_handle);
    if (region_island == nullptr)
        return 0;

    SurfaceCollection &surfaces = region_island->set_fill_surfaces();
    if (surfaces_handle == nullptr)
        surfaces.clear();
    else
        surfaces.set(std::move(*to_surface_collection(surfaces_handle)));
    return 1;
}

} // namespace

void clean_and_prepare(Print &print)
{
    // Surface generation owns only LayerRegionIsland fill surfaces. It must
    // leave deprecated LayerRegion fill caches untouched, because island-level
    // surfaces are the data passed to the new infill pipeline.
    for (PrintObject &object : print.objects())
        for (Layer &layer : object.layers())
            for (LayerSliceIsland &island : layer.islands())
                for (LayerRegionIsland &region_island : island.regions_islands())
                    region_island.set_fill_surfaces().clear();
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
    // STEP_SURFACE_GENERATION is exclusive: one active generator converts the
    // perimeter step's island fill areas into the first set of infill surfaces.
    Plugin *plugin = selected_or_active_plugin_for_step(orchestrator,
                                                        STEP_SURFACE_GENERATION,
                                                        &print.full_print_config());
    if (plugin == nullptr)
        return;

    Detail::run_object_step_plugin(
        orchestrator,
        print,
        STEP_SURFACE_GENERATION,
        *plugin,
        print.objects().size(),
        [&print](const size_t object_idx) {
            run_ctx_surface_generation payload = {};
            payload.print = reinterpret_cast<const print_handle *>(&print);
            payload.object = reinterpret_cast<const object_handle *>(&print.object(object_idx));
            payload.get_or_create_region_island = &get_or_create_region_island_callback;
            payload.set_region_island_fill_surfaces = &set_region_island_fill_surfaces_callback;
            return payload;
        });
}

} // namespace Slic3r::Steps::StepSurfaceGeneration
