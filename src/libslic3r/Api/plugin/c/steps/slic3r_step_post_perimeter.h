///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_step_post_perimeter_h_
#define slic3r_step_post_perimeter_h_

#include "slic3r_step_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
Payload for STEP_POST_PERIMETER.

Runs after perimeter generation for one object. Plugins may inspect or edit the
perimeter data before later surface/infill phases consume it.
*/
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

#ifdef __cplusplus
}
#endif

#endif // slic3r_step_post_perimeter_h_
