///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_step_infill_h_
#define slic3r_step_infill_h_

#include "slic3r_step_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
Payload for STEP_INFILL.

This step generates infill extrusion for one object.
*/
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

#ifdef __cplusplus
}
#endif

#endif // slic3r_step_infill_h_
