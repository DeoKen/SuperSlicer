///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_step_surface_type_h_
#define slic3r_step_surface_type_h_

#include "slic3r_step_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
Payload for STEP_SURFACE_TYPE.

Plugins classify or refine generated surfaces for one object.
*/
typedef struct run_ctx_detect_surface_type {
    const print_handle *print;
    const object_handle *object;
} run_ctx_detect_surface_type;

static inline const run_ctx_detect_surface_type *
plugin_ctx_as_detect_surface_type(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_SURFACE_TYPE)
        return NULL;
    return (const run_ctx_detect_surface_type *)ctx->data;
}

#ifdef __cplusplus
}
#endif

#endif // slic3r_step_surface_type_h_
