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
SLIC3R_HOST_API void orchestrator_register_plugin(
    orchestrator_handle *orch,
    plugin_instance plugin
);

SLIC3R_HOST_API bridge_detector_instance orchestrator_create_bridge_detector(
    orchestrator_handle *orch,
    const bridge_detector_create_input *input
);

/*
Register a UI layout fragment for one target layout file.

target_file is the base UI file the fragment applies to, for example
"print.ui", "filament.ui", or "printer_fff.ui".

fragment_id identifies this contribution inside target_file. If the same
target_file + fragment_id pair is registered twice, the second registration is
ignored. Identical duplicate content is expected when alternative plugins in
the same exclusive group expose the same settings. If the duplicate content is
different, the host keeps the first fragment and logs a warning because those
plugins no longer agree on the UI hidden behind that shared id.

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
SLIC3R_HOST_API int32_t orchestrator_add_ui_fragment(
    orchestrator_handle *orch,
    const char *target_file,
    const char *fragment_id,
    const char *ui_fragment,
    int32_t priority
);

typedef enum raw_gui_rule_condition {
    RAW_GUI_RULE_CONDITION_NONE = 0,
    RAW_GUI_RULE_CONDITION_BOOL_TRUE,
    RAW_GUI_RULE_CONDITION_BOOL_FALSE,
    RAW_GUI_RULE_CONDITION_OPTION_ENABLED,
    RAW_GUI_RULE_CONDITION_OPTION_DISABLED,
    RAW_GUI_RULE_CONDITION_VALUE_NON_ZERO,
    RAW_GUI_RULE_CONDITION_INT_EQUALS,
    RAW_GUI_RULE_CONDITION_INT_NOT_EQUALS
} raw_gui_rule_condition;

typedef enum raw_gui_rule_action {
    RAW_GUI_RULE_ACTION_NONE = 0,
    RAW_GUI_RULE_ACTION_ENABLE,
    RAW_GUI_RULE_ACTION_ENABLE_ANY
} raw_gui_rule_action;

/*
Index values shared by target_index and condition_index.

RAW_GUI_RULE_INDEX_ALL means "no specific item":
- for target_index, apply the rule to the whole GUI field;
- for condition_index, automatically choose the safest condition read:
  use the current extruder item when such an item exists, otherwise enable if
  any vector item satisfies the condition.

RAW_GUI_RULE_INDEX_CURRENT is mainly useful for target_index. It means "apply
this rule to the vector item currently processed by the GUI refresh loop".
Use it only for options whose value is sized like the extruder count.
*/
#define RAW_GUI_RULE_INDEX_ALL     (-1)
#define RAW_GUI_RULE_INDEX_CURRENT (-2)

typedef struct raw_gui_rule {
    raw_gui_rule_action action;
    raw_gui_rule_condition condition;
    const char *target_key;
    const char *condition_key;
    int32_t target_index;
    int32_t condition_index;
    int32_t condition_int_value;
} raw_gui_rule;

static inline raw_gui_rule raw_gui_rule_init()
{
    raw_gui_rule rule = {0};
    rule.target_index = RAW_GUI_RULE_INDEX_ALL;
    rule.condition_index = RAW_GUI_RULE_INDEX_ALL;
    return rule;
}

/*
Register a GUI state rule.

Rules are evaluated by ConfigManipulation when a tab refreshes its enabled
state.

RAW_GUI_RULE_ACTION_ENABLE enables target_key when every ENABLE rule registered
for the same target is true.

RAW_GUI_RULE_ACTION_ENABLE_ANY enables target_key when at least one ENABLE_ANY
rule registered for the same target is true. It is useful for legacy OR
conditions such as "support is enabled when support_material is true or
raft_layers is non-zero".

If both actions are used for the same target, the target is enabled only when
all ENABLE rules are true and at least one ENABLE_ANY rule is true.

condition_index and target_index are used for vector/extruder options.
Use RAW_GUI_RULE_INDEX_ALL for scalar options or when the whole field should be
affected. Use RAW_GUI_RULE_INDEX_CURRENT for a target that must be applied item
by item in the current extruder loop.

condition_int_value is used by RAW_GUI_RULE_CONDITION_INT_EQUALS and
RAW_GUI_RULE_CONDITION_INT_NOT_EQUALS. This is the intended way to express enum
conditions, because enum config options are read as integer values.

Returns 1 when the rule was added, 0 when an identical rule was already
registered, and a negative value on invalid arguments or internal failure.
*/
SLIC3R_HOST_API int32_t orchestrator_add_gui_rule(
    orchestrator_handle *orch,
    const raw_gui_rule *rule
);

/*
Default host callbacks used to populate plugin_run_context.
Plugins normally call these through the function pointers stored in the run
context instead of calling them directly.
*/
SLIC3R_HOST_API int orchestrator_plugin_is_cancelled(plugin_host_context *host_context);
SLIC3R_HOST_API void orchestrator_plugin_report_warning(plugin_host_context *host_context, const char *message);
SLIC3R_HOST_API void orchestrator_plugin_report_error(plugin_host_context *host_context, const char *message);
SLIC3R_HOST_API void orchestrator_plugin_report_progress(plugin_host_context *host_context, double progress, const char *message);

#ifdef __cplusplus
}
#endif

#endif // slic3r_orchestrator_h_
