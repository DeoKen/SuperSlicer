///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_step_perimeter_h_
#define slic3r_step_perimeter_h_

#include "slic3r_step_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
Payload for STEP_PERIMETER.

This step owns perimeter generation for one object. Plugins receive the print
and object handles and use the data-tree API to read/write perimeter results.
*/
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

#ifdef __cplusplus
}
#endif

#endif // slic3r_step_perimeter_h_
