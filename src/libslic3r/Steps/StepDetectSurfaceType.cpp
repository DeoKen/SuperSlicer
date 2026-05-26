
#include "StepDetectSurfaceType.hpp"

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_surface_type.h"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/Steps/StepPipeline.hpp"

#include "StepRunner.hpp"

namespace Slic3r::Steps::StepDetectSurfaceType {

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
            run_ctx_detect_surface_type payload = {};
            payload.print = reinterpret_cast<const print_handle *>(&print);
            payload.object = reinterpret_cast<const object_handle *>(&print.object(object_idx));
            return payload;
        });
}

} // namespace Slic3r::Steps::StepDetectSurfaceType
