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

The host has object layers and raw slices. Plugins may generate or prepare
surfaces for the object using the data-tree API.
*/
typedef struct run_ctx_surface_generation {
    const print_handle *print;
    const object_handle *object;
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
