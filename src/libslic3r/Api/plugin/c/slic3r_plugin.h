///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_plugin_h_
#define slic3r_plugin_h_

#include <stdint.h>

#include "slic3r_orchestrator.h"

#if defined(_WIN32) && defined(SLIC3R_PLUGIN_EXPORTS)
#define SLIC3R_PLUGIN_API __declspec(dllexport)
#else
#define SLIC3R_PLUGIN_API
#endif

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
SLIC3R_PLUGIN_API void register_plugin(orchestrator_handle *orch);

/*
Return the plugin ABI version used to build this shared library.

The host checks this symbol before calling register_plugin(). This makes stale
plugin DLLs fail cleanly instead of registering a plugin_instance whose vtable
layout no longer matches the host.
*/
SLIC3R_PLUGIN_API uint32_t slic3r_plugin_abi_version(void);

#ifdef __cplusplus
#define SLIC3R_PLUGIN_DECLARE_ABI_VERSION() \
    extern "C" SLIC3R_PLUGIN_API uint32_t slic3r_plugin_abi_version(void) { return SLIC3R_PLUGIN_ABI_VERSION; }
#else
#define SLIC3R_PLUGIN_DECLARE_ABI_VERSION() \
    SLIC3R_PLUGIN_API uint32_t slic3r_plugin_abi_version(void) { return SLIC3R_PLUGIN_ABI_VERSION; }
#endif

#ifdef __cplusplus
}
#endif

#endif // slic3r_plugin_h_
