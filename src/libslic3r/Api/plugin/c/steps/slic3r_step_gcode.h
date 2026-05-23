///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_step_gcode_h_
#define slic3r_step_gcode_h_

#include "slic3r_step_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
Payload for STEP_GCODE.

Plugins may participate in final G-code generation for one object/print
context.
*/
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

#ifdef __cplusplus
}
#endif

#endif // slic3r_step_gcode_h_
