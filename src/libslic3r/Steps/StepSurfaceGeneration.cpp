///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "StepSurfaceGeneration.hpp"

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
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
    // Surface generation owns only LayerRegionIsland fill surfaces. It must
    // leave deprecated LayerRegion fill caches untouched, because island-level
    // surfaces are the data passed to the new infill pipeline.
    for (PrintObject &object : print.objects())
        for (Layer &layer : object.layers())
            for (LayerSliceIsland &island : layer.islands())
                for (LayerRegionIsland &region_island : island.regions_islands())
                    region_island.set_fill_surfaces().clear();
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
    // STEP_SURFACE_GENERATION is exclusive: one active generator converts the
    // perimeter step's island fill areas into the first set of infill surfaces.
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
