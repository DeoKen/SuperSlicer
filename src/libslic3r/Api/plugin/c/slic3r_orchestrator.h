///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

/// 
#ifndef slic3r_orchestrator_h_
#define slic3r_orchestrator_h_

#include "slic3r_plugin_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================= REGISTRATION ========================= */

/*
Register a plugin instance.
*/
void orchestrator_register_plugin(
    orchestrator_handle *orch,
    plugin_instance plugin
);

bridge_detector_instance orchestrator_create_bridge_detector(
    orchestrator_handle *orch,
    const bridge_detector_create_input *input
);

/*
Default host callbacks used to populate plugin_run_context.
Plugins normally call these through the function pointers stored in the run
context instead of calling them directly.
*/
int orchestrator_plugin_is_cancelled(plugin_host_context *host_context);
void orchestrator_plugin_report_warning(plugin_host_context *host_context, const char *message);
void orchestrator_plugin_report_error(plugin_host_context *host_context, const char *message);
void orchestrator_plugin_report_progress(plugin_host_context *host_context, double progress, const char *message);

#ifdef __cplusplus
}
#endif

#endif // slic3r_orchestrator_h_
