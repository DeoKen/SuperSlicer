///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_surface_solidshells_hpp_
#define plugins_surface_solidshells_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace SurfaceGeneration { namespace SolidShellsPlugin {

// STEP_SURFACE_GENERATION refinement plugin.
//
// Earlier surface-generation plugins create LayerRegionIsland fill surfaces and
// classify exposed first/last surfaces. SolidShells projects those exposed
// areas through neighboring layers and turns matching internal sparse/void
// surfaces into internal solid surfaces to satisfy the configured top/bottom
// shell layer count and minimum shell thickness.
class SolidShells : public PluginBase
{
public:
    static SolidShells &instance(orchestrator_handle *orch);

private:
    SolidShells(orchestrator_handle *orch) : PluginBase(orch) {}

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

void register_solid_shells_plugin(orchestrator_handle *orch);

}}} // namespace slic3r_api::SurfaceGeneration::SolidShellsPlugin

#endif // plugins_surface_solidshells_hpp_
