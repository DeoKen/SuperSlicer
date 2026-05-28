
#include "StepPostInfillGeneration.hpp"

#include <cstddef>

#include "libslic3r/Api/plugin/c/steps/slic3r_step_post_infill.h"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/Steps/StepRunner.hpp"

namespace Slic3r::Steps::StepPostInfillGeneration {

void clean_and_prepare(Print &) {}

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
    // Post-infill plugins run after normal pattern generation. This step is
    // additive, not exclusive: several cleanup or residual-generation passes
    // can inspect and edit the infill buckets in priority order.
    Detail::run_object_step_plugins(orchestrator,
                                    print,
                                    STEP_POST_INFILL,
                                    &print.full_print_config(),
                                    print.objects().size(),
                                    [&print](const size_t object_idx) {
        run_ctx_post_infill_generation context = {};
        context.print = reinterpret_cast<const print_handle *>(&print);
        context.object = reinterpret_cast<const object_handle *>(&print.object(object_idx));
        return context;
    });
}

} // namespace Slic3r::Steps::StepPostInfillGeneration
