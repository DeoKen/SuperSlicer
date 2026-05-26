
///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "StepPrepareForPeriemters.hpp"

#include "libslic3r/Api/plugin/c/steps/slic3r_step_pre_perimeter.h"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"

#include "StepRunner.hpp"

namespace Slic3r::Steps::StepPrepareForPeriemters {

// The pre-perimeter step is currently only an extension point. It deliberately
// owns no host-side data yet, so there is nothing to clear before plugins run.
void clean_and_prepare(Print &) {}

// No invariant is defined yet for the empty boundary step. Keep the validators
// in place so future data ownership can be checked without changing StepPipeline.
bool validate_pre(const Print &, std::string &)
{
    return true;
}

bool validate_post(const Print &, std::string &)
{
    return true;
}

void run_step(Orchestrator &orchestrator, Print &print)
{
    // This is a non-exclusive object step: every active STEP_PRE_PERIMETER
    // plugin receives one run per PrintObject. The payload is intentionally
    // small and read-only; plugins can fetch richer data through the handles.
    Detail::run_object_step_plugins(orchestrator,
                                    print,
                                    STEP_PRE_PERIMETER,
                                    print.objects().size(),
                                    [&print](const size_t object_idx) {
        run_ctx_prepare_for_perimeters payload = {};
        payload.print = reinterpret_cast<const print_handle *>(&print);
        payload.object = reinterpret_cast<const object_handle *>(&print.object(object_idx));
        return payload;
    });
}

} // namespace Slic3r::Steps::StepPrepareForPeriemters
