///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef slic3r_plugin_h_
#define slic3r_plugin_h_

#include "slic3r_orchestrator.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================= PLUGIN ENTRY ========================= */

/*
Entry point symbol to export from plugin shared library.

This function MUST be implemented by the plugin.
The orchestrator will call it after loading the plugin.

The plugin have to register its plugin_instance(s) using
orchestrator_register_plugin().

After that, the orchestrator will be able to call the run method defined in the plugin_instance passed by the
orchestrator_register_plugin()
*/
void register_plugin(orchestrator_handle *orch);

#ifdef __cplusplus
}
#endif

#endif // slic3r_plugin_h_
