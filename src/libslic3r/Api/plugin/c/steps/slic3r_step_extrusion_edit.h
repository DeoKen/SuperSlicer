///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_step_extrusion_edit_h_
#define slic3r_step_extrusion_edit_h_

#include "slic3r_step_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
Payload for STEP_EXTRUSION_EDIT.

Plugins may edit the final object extrusion tree before simplification/G-code.
*/
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

#ifdef __cplusplus
}
#endif

#endif // slic3r_step_extrusion_edit_h_
