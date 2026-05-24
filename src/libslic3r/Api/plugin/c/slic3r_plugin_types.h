///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_plugin_types_h_
#define slic3r_plugin_types_h_

#include <stdint.h>

#include "slic3r_plugin_run_context.h"
#include "slic3r_slicing_step.h"
#include "slic3r_utils.h"
#include "steps/slic3r_step_bridge_detector.h"
#include "steps/slic3r_step_extrusion_edit.h"
#include "steps/slic3r_step_extrusion_simplification.h"
#include "steps/slic3r_step_gcode.h"
#include "steps/slic3r_step_infill.h"
#include "steps/slic3r_step_infill_group.h"
#include "steps/slic3r_step_layer_extrusion_edit.h"
#include "steps/slic3r_step_layer_height.h"
#include "steps/slic3r_step_layer_stiching.h"
#include "steps/slic3r_step_ordering.h"
#include "steps/slic3r_step_perimeter.h"
#include "steps/slic3r_step_post_infill.h"
#include "steps/slic3r_step_post_perimeter.h"
#include "steps/slic3r_step_post_slicing.h"
#include "steps/slic3r_step_pre_gcode.h"
#include "steps/slic3r_step_pre_infill.h"
#include "steps/slic3r_step_pre_perimeter.h"
#include "steps/slic3r_step_slicing.h"
#include "steps/slic3r_step_support.h"
#include "steps/slic3r_step_support_demand.h"
#include "steps/slic3r_step_support_spot.h"
#include "steps/slic3r_step_surface_generation.h"
#include "steps/slic3r_step_surface_type.h"
#include "steps/slic3r_step_wipetower.h"

#define SLIC3R_PLUGIN_ABI_VERSION 2u

#ifdef __cplusplus
extern "C" {
#endif

/* ========================= HANDLES ========================= */

typedef struct orchestrator_handle orchestrator_handle;

/* ========================= PLUGIN CALLBACKS ========================= */

typedef void (*plugin_initialize_fn)(void *plugin_ctx, storage_handle *storage);

/* This plugin_run_context is destroyed after each of these function return, so don't keep it, copy the data you want instead. */
typedef void (*plugin_setup_fn)(void *plugin_ctx, const plugin_run_context *run_ctx, uint32_t run_count);
typedef void (*plugin_setup_run_fn)(void *plugin_ctx, const plugin_run_context *run_ctx);
typedef void (*plugin_run_fn)(void *plugin_ctx, const plugin_run_context *run_ctx);

/*
Return the configuration option keys read by this plugin.

This uses the usual C double-call pattern:
- call with keys == NULL to get the number of entries to allocate;
- call again with an array of that size to receive borrowed const char*
  pointers owned by the plugin.

The host uses this list to enable/disable GUI fields when several plugins are
available for an exclusive step and a project selects one of them.
*/
typedef int32_t (*plugin_used_config_keys_fn)(void *plugin_ctx, const char **keys);

/* ========================= PLUGIN VTABLE ========================= */

typedef struct plugin_vtable {

    /*
    ABI version used to build this vtable.

    Keep this as the first field: the host can reject stale plugin instances
    before calling any function pointer whose slot may have moved.
    */
    uint32_t abi_version;

    const char* (*get_id)(void *plugin_ctx);

    slicing_step_t (*get_step)(void *plugin_ctx);

    const_strings_t (*get_dependencies)(void *plugin_ctx);

    int32_t (*get_priority)(void *plugin_ctx);

    plugin_used_config_keys_fn used_config_keys;

    /**
     * Called once at startup, to be able to setup settings, via orchestrator_create_option_def
    */
    plugin_initialize_fn initialize;

    /*
    Called once before any setup_run()/run() call for this step/plugin pair.
    run_count is the number of run contexts that will be prepared for this
    plugin. Use it to reset plugin-side shared state such as progress helpers.
    */
    plugin_setup_fn setup;

    /*
    Called once for each run context before any run() starts. The host may call
    setup_run() in parallel, but it guarantees that all setup_run() calls finish
    before the first run() starts. Use it to estimate per-run work.
    */
    plugin_setup_run_fn setup_run;

    /*
    * Called once per object to do the step this plugin is made for.
    */
    plugin_run_fn run;

} plugin_vtable;

/* ========================= PLUGIN INSTANCE ========================= */

typedef struct plugin_instance {
    void *ctx;
    const plugin_vtable *vt;
} plugin_instance;

#ifdef __cplusplus
}
#endif

#endif // slic3r_plugin_types_h_
