///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_step_post_infill_h_
#define slic3r_step_post_infill_h_

#include "slic3r_step_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
Payload for STEP_POST_INFILL.

Runs after STEP_INFILL for one object.

Normal usage:
- inspect the LayerRegionIsland extrusion buckets produced by infill patterns;
- add cleanup, gap-fill, ironing preparation, or other residual infill passes;
- leave the object unchanged when no residual work is required.

This payload is intentionally small for now. As post-infill plugins need more
host-owned mutations, add explicit callbacks here instead of letting plugins
write unrelated data-tree state directly.
*/
typedef struct run_ctx_post_infill_generation {
    const print_handle *print;
    const object_handle *object;
} run_ctx_post_infill_generation;

static inline const run_ctx_post_infill_generation *
plugin_ctx_as_post_infill_generation(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_POST_INFILL)
        return NULL;
    return (const run_ctx_post_infill_generation *)ctx->data;
}

#ifdef __cplusplus
}
#endif

#endif // slic3r_step_post_infill_h_
