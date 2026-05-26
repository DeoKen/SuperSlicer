
///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "StepPostPerimeterGeneration.hpp"

#include "libslic3r/Api/plugin/c/steps/slic3r_step_post_perimeter.h"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"

#include "StepRunner.hpp"

namespace Slic3r::Steps::StepPostPerimeterGeneration {

// The post-perimeter step is a plugin boundary after perimeter generation.
// Later plugins may edit perimeter extrusions or derived areas, but the host
// does not own transient data for this step yet.
void clean_and_prepare(Print &) {}

// Keep explicit validators even while the step is empty. They document the
// intended lifecycle and give future modules a natural place for invariants.
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
    // Non-exclusive: all active post-perimeter plugins run in priority order,
    // once per object, after STEP_PERIMETER has published region-island
    // perimeter extrusions and island fill areas.
    Detail::run_object_step_plugins(orchestrator,
                                    print,
                                    STEP_POST_PERIMETER,
                                    print.objects().size(),
                                    [&print](const size_t object_idx) {
        run_ctx_post_perimeter_generation payload = {};
        payload.print = reinterpret_cast<const print_handle *>(&print);
        payload.object = reinterpret_cast<const object_handle *>(&print.object(object_idx));
        return payload;
    });
}

} // namespace Slic3r::Steps::StepPostPerimeterGeneration
