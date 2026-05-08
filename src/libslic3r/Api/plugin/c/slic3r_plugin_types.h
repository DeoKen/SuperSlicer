///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef slic3r_plugin_types_h_
#define slic3r_plugin_types_h_

#include <stddef.h>
#include "slic3r_utils.h"
#include "slic3r_data_tree.h"
#include "slic3r_volume.h"
#include "slic3r_bridge_detector.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================= HANDLES ========================= */

typedef struct orchestrator_handle orchestrator_handle;

/* ========================= SLICING STEP ========================= */

/*
Defines the execution stage of a plugin in the slicing pipeline.
*/
typedef enum slicing_step_t
{
    STEP_LAYER_HEIGHT              = 100,
    STEP_SLICING                   = 200,
    STEP_POST_SLICING              = 300,
    STEP_PRE_PERIMETER             = 500,
    STEP_PERIMETER                 = 600,
    STEP_POST_PERIMETER            = 700,
    STEP_SURFACE_GENERATION        = 750,
    STEP_SURFACE_TYPE              = 800,
    STEP_PRE_INFILL                = 900,
    STEP_INFILL_GROUP              = 950,
    STEP_INFILL                    = 1000,
    STEP_POST_INFILL               = 1100,
    STEP_SUPPORT_SPOT              = 1200,
    STEP_SUPPORT                   = 1300,
    STEP_PRE_GCODE                 = 1400,
    STEP_ORDERING                  = 1500,
    STEP_WIPETOWER                 = 1600,
    STEP_LAYER_EXTRUSION_EDIT      = 1650,
    STEP_LAYER_STICHING            = 1700,
    STEP_EXTRUSION_EDIT            = 1800,
    STEP_EXTRUSION_SIMPLIFICATION  = 1900,
    STEP_GCODE                     = 2000,

    /*
    Service plugin type used to create infill extrusion for a surface
    */
    INFILL_PATTERN                 = 10000,

    /*
    Service plugin used to create bridge detector instances on demand.
    */
    BRIDGE_DETECTOR                = 10100

} slicing_step_t;


/* ========================= RUN CONTEXT ========================= */

typedef struct plugin_host_context plugin_host_context;

/*
Return non-zero if the host requested this plugin execution to stop.
Plugins should check this regularly in long loops and return as soon as
possible when cancellation is requested.
*/
typedef int (*plugin_is_cancelled_fn)(plugin_host_context *host_context);

/*
Report a message to the host.
Warnings are non-blocking. Errors are fatal for the current slicing operation:
the host will request cancellation for the slicing operation.
*/
typedef void (*plugin_report_fn)(plugin_host_context *host_context, const char *message);

/*
Report progress inside the currently running step.
progress is clamped by the host to the [0.0, 1.0] range. message is optional
UTF-8 text and may be NULL to let the host use a default step/plugin message.
*/
typedef void (*plugin_report_progress_fn)(plugin_host_context *host_context, double progress, const char *message);

typedef struct plugin_run_context {
    slicing_step_t step;
    storage_handle *plugin_storage;
    void *data;

    plugin_host_context *host_context;
    plugin_is_cancelled_fn is_cancelled;
    plugin_report_fn report_warning;
    plugin_report_fn report_error;
    plugin_report_progress_fn report_progress;
} plugin_run_context;

/* ========================= STEP-SPECIFIC PAYLOADS ========================= */

/*
Borrow mutable raw slices for a LayerRegion during steps where the host allows
plugins to fill or edit slicing results. The returned handle is borrowed from
the data tree; do not free it through plugin storage.
*/
typedef expolygon_collection_handle *(*layer_region_borrow_mutable_slices_fn)(layer_region_handle *me);

/* ========================= PAYLOAD FOR STEP LAYER-HEIGHT ========================= */
/*
Borrowed view over one Object layer config range entry.

z_min and z_max are unscaled object-local Z coordinates, matching the native
t_layer_height_range convention. config is a borrowed read-only ConfigBase view
over the range's ModelConfig content; it stays valid only during the current
setup_run()/run() call.
*/
typedef struct c_layer_config_range {
    coord_t z_min;
    coord_t z_max;
    const config_handle *config;
} c_layer_config_range;

// Set the layers to these heights.
typedef void (*set_layer_height_profile_fn)(object_handle *object, coord_t* layer_zs, uint32_t layer_zs_size);
typedef struct run_ctx_layer_height_generation {
    print_handle *print;
    object_handle *object;
    // input from the gui, containing the layers set by the variable layer height feature. is empty if the feature is not used.
    coord_t* enforce_layer_zs;
    uint32_t enforce_layer_zs_size;
    // Object layer-specific config overrides, borrowed from the host object layer ranges.
    // The array and its config handles are read-only and valid only for this setup_run()/run() call.
    const c_layer_config_range *layer_config_ranges;
    uint32_t layer_config_ranges_size;
    // you have to call that to give back your updated layer heights.
    set_layer_height_profile_fn set_layer_height_profile;
    // Maximum z of the object (from the platter, in the 3D view).
    coord_t max_z;
} run_ctx_layer_height_generation;

static inline const run_ctx_layer_height_generation *
plugin_ctx_as_layer_height_generation(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_LAYER_HEIGHT)
        return NULL;
    return (const run_ctx_layer_height_generation *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP SLICING ========================= */
/*
Opaque borrowed views over the host's PrintObjectRegions::LayerRangeRegions and
VolumeRegion entries. They are only valid during STEP_SLICING setup_run()/run().
Plugins use these views to decide by themselves which volume slices overlap and
which LayerRegion receives the final polygons, without exposing the native C++
containers as part of the stable public API.
*/
typedef struct slicing_layer_range_handle slicing_layer_range_handle;
typedef struct slicing_volume_region_handle slicing_volume_region_handle;

typedef uint32_t (*slicing_layer_range_count_fn)(const object_handle *object);
typedef const slicing_layer_range_handle *(*slicing_layer_range_at_fn)(const object_handle *object, uint32_t idx);
typedef coord_t (*slicing_layer_range_z_min_fn)(const slicing_layer_range_handle *range);
typedef coord_t (*slicing_layer_range_z_max_fn)(const slicing_layer_range_handle *range);
typedef const config_handle *(*slicing_layer_range_config_fn)(const slicing_layer_range_handle *range);

typedef uint32_t (*slicing_layer_range_volume_region_count_fn)(const slicing_layer_range_handle *range);
typedef const slicing_volume_region_handle *(*slicing_layer_range_volume_region_at_fn)(
    const slicing_layer_range_handle *range,
    uint32_t idx);

typedef const volume_handle *(*slicing_volume_region_volume_fn)(const slicing_volume_region_handle *volume_region);
typedef int32_t (*slicing_volume_region_parent_fn)(const slicing_volume_region_handle *volume_region);
/*
Return the LayerRegion index to write into, or -1 for volume regions that do not
produce printable material directly (for example negative volumes).
*/
typedef int32_t (*slicing_volume_region_layer_region_idx_fn)(const slicing_volume_region_handle *volume_region);
typedef c_bounding_box3f (*slicing_volume_region_bbox_fn)(const slicing_volume_region_handle *volume_region);

typedef struct run_ctx_slicing {
    print_handle *print;
    object_handle *object;

    // Mutable access to LayerRegion raw slices created/filled by slicing plugins.
    layer_region_borrow_mutable_slices_fn layer_region_borrow_mutable_slices;

    // Step-local read API over layer ranges and their volume/region entries.
    slicing_layer_range_count_fn layer_range_count;
    slicing_layer_range_at_fn layer_range_at;
    slicing_layer_range_z_min_fn layer_range_z_min;
    slicing_layer_range_z_max_fn layer_range_z_max;
    slicing_layer_range_config_fn layer_range_config;
    slicing_layer_range_volume_region_count_fn layer_range_volume_region_count;
    slicing_layer_range_volume_region_at_fn layer_range_volume_region_at;
    slicing_volume_region_volume_fn volume_region_volume;
    slicing_volume_region_parent_fn volume_region_parent;
    slicing_volume_region_layer_region_idx_fn volume_region_layer_region_idx;
    slicing_volume_region_bbox_fn volume_region_bbox;
} run_ctx_slicing;

static inline const run_ctx_slicing *
plugin_ctx_as_slicing(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_SLICING)
        return NULL;
    return (const run_ctx_slicing *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP POST-SLICING ========================= */
// at this tep, it's possibel to modify the geometry sotred in a printobject's layer : layer slice, island slice, and layerregion slices.
// the layerregion slice are mutable at this step.
// the layer slices and island slice are not and you need these special callback to get them in mutable state.
// Note: layerregion slices has been created at the slice step, and can't be recreated. layer and layer islands can be recreated from it (see below for methods)

// clear previous layer's islands, move expolygon_collection_handle into the layer slices and construct islands from it.
typedef void (*layer_assign_islands_by_moving_contents_fn)(layer_handle *me, expolygon_collection_handle *in_out_islands);
// this layer islans  has been modified, recompute the slices (cache of islands)
typedef void (*layer_recompute_slices_from_islands_fn)(layer_handle *me);
// this layer region has been modified, clear all islands and slices from the layer and recompute them from this layer's layer_region;
typedef void (*layer_recompute_slices_and_islands_from_layer_region_fn)(layer_handle *me);
// if don't want ot reconstruct all islands from scratch, you can also modify the layer slices & islands slice &
// region slices, but be careful to keep them consistent. You can use other methodds to recompute one from the other.
typedef expolygon_collection_handle *(*layer_borrow_mutable_slices_fn)(layer_handle *me);
typedef expolygon_handle *(*layer_island_borrow_mutable_slice_fn)(layer_island_handle *me);
//note: at this step, their is no islandregion in any island.
typedef struct run_ctx_post_slicing {
    // mutable handles
    print_handle *print;
    object_handle *object;

    // Takes ownership / moves contents into Layer islands.
    layer_assign_islands_by_moving_contents_fn layer_assign_islands_by_moving_contents;

    // Rebuilds Layer slices from Layer islands after island mutation.
    layer_recompute_slices_from_islands_fn layer_recompute_slices_from_islands;

    // Rebuilds Layer slices & islands from LayerRegion slices.
    layer_recompute_slices_and_islands_from_layer_region_fn layer_recompute_slices_and_islands_from_layer_region;

    // Mutable post-slicing accessors. Caller must keep Layer / LayerRegion / LayerIsland
    // slices consistent if several caches are edited manually.
    layer_borrow_mutable_slices_fn layer_borrow_mutable_slices;
    layer_region_borrow_mutable_slices_fn layer_region_borrow_mutable_slices;
    layer_island_borrow_mutable_slice_fn layer_island_borrow_mutable_slice;
} run_ctx_post_slicing;

/* ========================= PAYLOAD FOR STEP SURFACE-GENERATION ========================= */
typedef struct run_ctx_surface_generation {
    print_handle *print;
    object_handle *object;
} run_ctx_surface_generation;

static inline const run_ctx_surface_generation *
plugin_ctx_as_surface_generation(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_SURFACE_GENERATION)
        return NULL;
    return (const run_ctx_surface_generation *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP PRE-PERIMETER ========================= */
typedef struct run_ctx_prepare_for_perimeters {
    print_handle *print;
    object_handle *object;
} run_ctx_prepare_for_perimeters;

static inline const run_ctx_prepare_for_perimeters *
plugin_ctx_as_prepare_for_perimeters(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_PRE_PERIMETER)
        return NULL;
    return (const run_ctx_prepare_for_perimeters *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP PERIMETER ========================= */
typedef struct run_ctx_generate_perimeter {
    print_handle *print;
    object_handle *object;
} run_ctx_generate_perimeter;

static inline const run_ctx_generate_perimeter *
plugin_ctx_as_generate_perimeter(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_PERIMETER)
        return NULL;
    return (const run_ctx_generate_perimeter *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP POST-PERIMETER ========================= */
typedef struct run_ctx_post_perimeter_generation {
    print_handle *print;
    object_handle *object;
} run_ctx_post_perimeter_generation;

static inline const run_ctx_post_perimeter_generation *
plugin_ctx_as_post_perimeter_generation(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_POST_PERIMETER)
        return NULL;
    return (const run_ctx_post_perimeter_generation *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP SURFACE-TYPE ========================= */
typedef struct run_ctx_detect_surface_type {
    print_handle *print;
    object_handle *object;
} run_ctx_detect_surface_type;

static inline const run_ctx_detect_surface_type *
plugin_ctx_as_detect_surface_type(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_SURFACE_TYPE)
        return NULL;
    return (const run_ctx_detect_surface_type *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP PRE-INFILL ========================= */
typedef struct run_ctx_prepare_infill {
    print_handle *print;
    object_handle *object;
} run_ctx_prepare_infill;

static inline const run_ctx_prepare_infill *
plugin_ctx_as_prepare_infill(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_PRE_INFILL)
        return NULL;
    return (const run_ctx_prepare_infill *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP INFILL-GROUP ========================= */
typedef struct run_ctx_group_infill_regions {
    print_handle *print;
    object_handle *object;
} run_ctx_group_infill_regions;

static inline const run_ctx_group_infill_regions *
plugin_ctx_as_group_infill_regions(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_INFILL_GROUP)
        return NULL;
    return (const run_ctx_group_infill_regions *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP INFILL ========================= */
typedef struct run_ctx_generate_infill {
    print_handle *print;
    object_handle *object;
} run_ctx_generate_infill;

static inline const run_ctx_generate_infill *
plugin_ctx_as_generate_infill(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_INFILL)
        return NULL;
    return (const run_ctx_generate_infill *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP POST-INFILL ========================= */
typedef struct run_ctx_post_infill_generation {
    print_handle *print;
    object_handle *object;
} run_ctx_post_infill_generation;

static inline const run_ctx_post_infill_generation *
plugin_ctx_as_post_infill_generation(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_POST_INFILL)
        return NULL;
    return (const run_ctx_post_infill_generation *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP SUPPORT-SPOT ========================= */
typedef struct run_ctx_detect_support_spots {
    print_handle *print;
    object_handle *object;
} run_ctx_detect_support_spots;

static inline const run_ctx_detect_support_spots *
plugin_ctx_as_detect_support_spots(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_SUPPORT_SPOT)
        return NULL;
    return (const run_ctx_detect_support_spots *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP SUPPORT ========================= */
typedef struct run_ctx_generate_support {
    print_handle *print;
    object_handle *object;
} run_ctx_generate_support;

static inline const run_ctx_generate_support *
plugin_ctx_as_generate_support(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_SUPPORT)
        return NULL;
    return (const run_ctx_generate_support *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP PRE-GCODE ========================= */
typedef struct run_ctx_prepare_gcode {
    print_handle *print;
    object_handle *object;
} run_ctx_prepare_gcode;

static inline const run_ctx_prepare_gcode *
plugin_ctx_as_prepare_gcode(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_PRE_GCODE)
        return NULL;
    return (const run_ctx_prepare_gcode *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP ORDERING ========================= */
typedef struct run_ctx_extrusion_ordering {
    print_handle *print;
    object_handle *object;
} run_ctx_extrusion_ordering;

static inline const run_ctx_extrusion_ordering *
plugin_ctx_as_extrusion_ordering(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_ORDERING)
        return NULL;
    return (const run_ctx_extrusion_ordering *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP WIPETOWER ========================= */
typedef struct run_ctx_generate_wipe_tower {
    print_handle *print;
    object_handle *object;
} run_ctx_generate_wipe_tower;

static inline const run_ctx_generate_wipe_tower *
plugin_ctx_as_generate_wipe_tower(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_WIPETOWER)
        return NULL;
    return (const run_ctx_generate_wipe_tower *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP LAYER-EXTRUSION-EDIT ========================= */
typedef struct run_ctx_layer_extrusion_edition {
    print_handle *print;
    object_handle *object;
} run_ctx_layer_extrusion_edition;

static inline const run_ctx_layer_extrusion_edition *
plugin_ctx_as_layer_extrusion_edition(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_LAYER_EXTRUSION_EDIT)
        return NULL;
    return (const run_ctx_layer_extrusion_edition *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP LAYER-STICHING ========================= */
typedef struct run_ctx_layer_stiching {
    print_handle *print;
    object_handle *object;
} run_ctx_layer_stiching;

static inline const run_ctx_layer_stiching *
plugin_ctx_as_layer_stiching(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_LAYER_STICHING)
        return NULL;
    return (const run_ctx_layer_stiching *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP EXTRUSION-EDIT ========================= */
typedef struct run_ctx_extrusion_edition {
    print_handle *print;
    object_handle *object;
} run_ctx_extrusion_edition;

static inline const run_ctx_extrusion_edition *
plugin_ctx_as_extrusion_edition(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_EXTRUSION_EDIT)
        return NULL;
    return (const run_ctx_extrusion_edition *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP EXTRUSION-SIMPLIFICATION ========================= */
typedef struct run_ctx_extrusion_simplification {
    print_handle *print;
    object_handle *object;
} run_ctx_extrusion_simplification;

static inline const run_ctx_extrusion_simplification *
plugin_ctx_as_extrusion_simplification(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_EXTRUSION_SIMPLIFICATION)
        return NULL;
    return (const run_ctx_extrusion_simplification *)ctx->data;
}

/* ========================= PAYLOAD FOR STEP GCODE ========================= */
typedef struct run_ctx_generate_gcode {
    print_handle *print;
    object_handle *object;
} run_ctx_generate_gcode;

static inline const run_ctx_generate_gcode *
plugin_ctx_as_generate_gcode(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_GCODE)
        return NULL;
    return (const run_ctx_generate_gcode *)ctx->data;
}

typedef struct run_ctx_bridge_detector {
    bridge_detector_create_input input;
    bridge_detector_instance detector;
} run_ctx_bridge_detector;

/* ========================= CAST HELPERS ========================= */

static inline const run_ctx_post_slicing *
plugin_ctx_as_post_slicing(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_POST_SLICING)
        return NULL;

    return (const run_ctx_post_slicing *)ctx->data;
}

static inline run_ctx_bridge_detector *
plugin_ctx_as_bridge_detector(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != BRIDGE_DETECTOR)
        return NULL;

    return (run_ctx_bridge_detector *)ctx->data;
}

/* ========================= RUN callbacks ========================= */

typedef void (*plugin_run_fn)(
    void *plugin_ctx,
    /* This plugin_run_context is destroyed after this function return, so don't keep it, copy the data you want instead. */
    const plugin_run_context *run_ctx
);

/* ========================= PLUGIN VTABLE ========================= */

typedef void (*plugin_setup_fn)(void *plugin_ctx, const plugin_run_context *run_ctx, uint32_t run_count);
typedef void (*plugin_setup_run_fn)(void *plugin_ctx, const plugin_run_context *run_ctx);

typedef struct plugin_vtable {

    const char* (*get_id)(void *plugin_ctx);

    slicing_step_t (*get_step)(void *plugin_ctx);

    const_strings_t (*get_dependencies)(void *plugin_ctx);

    int32_t (*get_priority)(void *plugin_ctx);

    /*
    Called once before any setup_run()/run() call for this step/plugin pair.
    run_count is the number of run contexts that will be prepared for this
    plugin. Use it to reset plugin-side shared state such as progress helpers.
    */
    plugin_setup_fn setup;

    /*
    Called once for each run context before any run() starts. The host may call
    setup_run() in parallel, but it guarantees that all setup_run() calls finish
    before the first run() starts. Use it to estimate per-run work.
    */
    plugin_setup_run_fn setup_run;

    plugin_run_fn run;

} plugin_vtable;

/* ========================= PLUGIN INSTANCE ========================= */

typedef struct plugin_instance {
    void *ctx;
    const plugin_vtable *vt;
} plugin_instance;

#ifdef __cplusplus
}
#endif

#endif // slic3r_plugin_types_h_
