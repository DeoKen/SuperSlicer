
///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "StepDetectSurfaceType.hpp"

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_surface_type.h"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/Steps/StepPipeline.hpp"

#include "StepRunner.hpp"

namespace Slic3r::Steps::StepDetectSurfaceType {

// STEP_SURFACE_TYPE consumes the raw fill_surfaces created by
// STEP_SURFACE_GENERATION. Those inputs are owned by LayerRegion, so this
// step does not clear anything before the selected plugin runs.
void clean_and_prepare(Print &) {}

// Validation is intentionally shallow for now. The default plugin still calls
// the native prepare-infill internals, and the later split will add invariants
// once each submodule owns a smaller, easier-to-check contract.
bool validate_pre(const Print &, std::string *)
{
    return true;
}

bool validate_post(const Print &, std::string *)
{
    return true;
}

void run_step(Orchestrator &orchestrator, Print &print)
{
    // Surface typing is exclusive: multiple algorithms may be active, but only
    // the selected one should classify/finalize fill_surfaces for a given print.
    Plugin *plugin = selected_or_active_plugin_for_step(orchestrator,
                                                        STEP_SURFACE_TYPE,
                                                        &print.full_print_config());
    if (plugin == nullptr)
        return;

    Detail::run_object_step_plugin(
        orchestrator,
        print,
        STEP_SURFACE_TYPE,
        *plugin,
        print.objects().size(),
        [&print](const size_t object_idx) {
            // One payload per object. The handles are const from the C API
            // point of view; the built-in default plugin is a host bridge and
            // uses friend access for the legacy mutation path.
            run_ctx_detect_surface_type payload = {};
            payload.print = reinterpret_cast<const print_handle *>(&print);
            payload.object = reinterpret_cast<const object_handle *>(&print.object(object_idx));
            return payload;
        });
}

} // namespace Slic3r::Steps::StepDetectSurfaceType
