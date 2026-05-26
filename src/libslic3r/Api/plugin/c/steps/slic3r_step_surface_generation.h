///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_step_surface_generation_h_
#define slic3r_step_surface_generation_h_

#include "slic3r_step_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
Payload for STEP_SURFACE_GENERATION.

The perimeter step has already published island-level infill areas. A surface
generation plugin converts those areas into LayerRegionIsland fill surfaces.

Normal usage:
- iterate object -> layers -> islands with the data-tree API;
- group island regions that can share the same fill surfaces;
- call get_or_create_region_island() for each group;
- call set_region_island_fill_surfaces() with the ExPolygons to convert into
  sparse/internal Surface entries.

The plugin should not write deprecated LayerRegion fill surface caches here.
The new infill pipeline reads LayerRegionIsland surfaces.
*/
typedef layer_region_island_handle *(*surface_generation_get_or_create_region_island_fn)(
    const layer_island_handle *island,
    const layer_region_handle *const *regions,
    uint32_t region_count);

/*
Replace the fill surfaces of a LayerRegionIsland.

areas is borrowed from the caller. The host copies its ExPolygons into Surface
objects and assigns surface_type to every created surface. Passing areas == NULL
clears the destination collection.
*/
typedef int32_t (*surface_generation_set_region_island_fill_surfaces_fn)(
    layer_region_island_handle *region_island,
    const expolygon_collection_handle *areas,
    raw_surface_type surface_type);

typedef struct run_ctx_surface_generation {
    const print_handle *print;
    const object_handle *object;

    surface_generation_get_or_create_region_island_fn get_or_create_region_island;
    surface_generation_set_region_island_fill_surfaces_fn set_region_island_fill_surfaces;
} run_ctx_surface_generation;

static inline const run_ctx_surface_generation *
plugin_ctx_as_surface_generation(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_SURFACE_GENERATION)
        return NULL;
    return (const run_ctx_surface_generation *)ctx->data;
}

#ifdef __cplusplus
}
#endif

#endif // slic3r_step_surface_generation_h_
