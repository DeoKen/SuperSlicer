///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_step_ordering_h_
#define slic3r_step_ordering_h_

#include "slic3r_step_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
Payload for STEP_ORDERING.

Plugins can inspect or change extrusion ordering data for one object before
G-code generation consumes it.
*/
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

#ifdef __cplusplus
}
#endif

#endif // slic3r_step_ordering_h_
