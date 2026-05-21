///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_orchestrator_h_
#define slic3r_orchestrator_h_

///


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
Register a UI layout fragment for one target layout file.

target_file is the base UI file the fragment applies to, for example
"print.ui", "filament.ui", or "printer_fff.ui".

fragment_id identifies this contribution inside target_file. If the same
target_file + fragment_id pair is registered twice, the second registration is
ignored. This lets a plugin safely register the same UI fragment once, and it
also leaves room for future global UI files to reserve the same fragment id.

ui_fragment is a small .ui document using the normal UI layout syntax. During
GUI construction, the host parses the base UI file and this fragment, then
merges pages, groups and lines by name. Missing pages/groups/lines are created.
Use insert$before$NAME or insert$after$NAME on page/group/line commands to
place a missing node next to an existing node of the same kind. A kind may be
specified explicitly, for example insert$aftergroup$Filtering.

priority orders fragments for the same target_file. Lower priority is applied
first. Fragments with the same priority keep registration order.

Returns 1 when the fragment was added, 0 when it was already present, and a
negative value on invalid arguments or internal failure.
*/
int32_t orchestrator_add_ui_fragment(
    orchestrator_handle *orch,
    const char *target_file,
    const char *fragment_id,
    const char *ui_fragment,
    int32_t priority
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
