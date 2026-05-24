///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_step_layer_height_h_
#define slic3r_step_layer_height_h_

#include "slic3r_step_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
Payload for STEP_LAYER_HEIGHT.

The plugin receives one object and returns the full layer height profile through
set_layer_height_profile. z values are scaled coordinates.
*/

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
typedef void (*set_layer_height_profile_fn)(const object_handle *object, coord_t* layer_zs, uint32_t layer_zs_size);

typedef struct run_ctx_layer_height_generation {
    const print_handle *print;
    const object_handle *object;
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

#ifdef __cplusplus
}
#endif

#endif // slic3r_step_layer_height_h_
