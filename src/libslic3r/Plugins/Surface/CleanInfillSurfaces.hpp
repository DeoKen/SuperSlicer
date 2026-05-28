///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_surface_cleaninfillsurfaces_hpp_
#define plugins_surface_cleaninfillsurfaces_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace SurfaceGeneration { namespace CleanInfillSurfacesPlugin {

// STEP_SURFACE_GENERATION cleanup plugin.
//
// Earlier modules may create many small or fragmented fill surfaces while
// classifying top/bottom/internal areas. CleanInfillSurfaces applies the final
// sparse-to-solid cleanup rules, drops isolated numerical dust, and rebuilds
// same-type surfaces as unions so later infill code sees compact partitions.
//
// The plugin does not create initial surfaces and it does not inspect extrusion
// paths. It expects LayerRegionIsland fill surfaces to already cover the
// island infill areas, then preserves that coverage while changing only the
// SurfaceType and geometry partition.
class CleanInfillSurfaces : public PluginBase
{
public:
    static CleanInfillSurfaces &instance(orchestrator_handle *orch);

private:
    CleanInfillSurfaces(orchestrator_handle *orch) : PluginBase(orch) {}

    const char *id_impl() const noexcept override;
    const char *name_impl() const noexcept override;
    const char *description_impl() const noexcept override;
    slicing_step_t step_impl() const noexcept override;
    const char *const *dependencies_impl() const noexcept override;
    int32_t priority_impl() const noexcept override;
    int32_t used_config_keys(raw_used_config_key *keys) const noexcept override;
    const char *progress_message_format_impl() const noexcept override;
    void setup_run_impl(const plugin_run_context *run_ctx) const override;
    void run_impl(const plugin_run_context *run_ctx) const override;
};

void register_clean_infill_surfaces_plugin(orchestrator_handle *orch);

}}} // namespace slic3r_api::SurfaceGeneration::CleanInfillSurfacesPlugin

#endif // plugins_surface_cleaninfillsurfaces_hpp_
