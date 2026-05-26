///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "StepSurfaceGeneration.hpp"

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/Api/internal/LayerRegionAccess.hpp"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_surface_generation.h"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/Steps/StepPipeline.hpp"

#include "StepRunner.hpp"

namespace Slic3r::Steps::StepSurfaceGeneration {

void clean_and_prepare(Print &print)
{
    for (PrintObject &object : print.objects())
        for (Layer &layer : object.layers())
            for (LayerRegion &region : layer.regions()) {
                region.set_fill_surfaces().clear();
                ApiInternal::LayerRegionAccess::fill_no_overlap_expolygons_mutable(region).clear();
            }
}

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
                                                        STEP_SURFACE_GENERATION,
                                                        &print.full_print_config());
    if (plugin == nullptr)
        return;

    Detail::run_object_step_plugin(
        orchestrator,
        print,
        STEP_SURFACE_GENERATION,
        *plugin,
        print.objects().size(),
        [&print](const size_t object_idx) {
            run_ctx_surface_generation payload = {};
            payload.print = reinterpret_cast<const print_handle *>(&print);
            payload.object = reinterpret_cast<const object_handle *>(&print.object(object_idx));
            return payload;
        });
}

} // namespace Slic3r::Steps::StepSurfaceGeneration
