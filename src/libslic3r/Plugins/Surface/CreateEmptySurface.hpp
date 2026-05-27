///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_surface_createemptysurface_hpp_
#define plugins_surface_createemptysurface_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace SurfaceGeneration { namespace CreateEmptySurfacePlugin {

// Baseline STEP_SURFACE_GENERATION plugin.
//
// Perimeter generation publishes island-level fill areas. This plugin is the
// first surface-generation pass: it creates the initial LayerRegionIsland
// surfaces and classifies each area as bottom, internal, or top by comparing it
// with the layer islands immediately below and above.
class CreateEmptySurface : public PluginBase
{
public:
    static CreateEmptySurface &instance(orchestrator_handle *orch);

private:
    CreateEmptySurface(orchestrator_handle *orch) : PluginBase(orch) {}

    const char *id_impl() const noexcept override;
    const char *name_impl() const noexcept override;
    const char *description_impl() const noexcept override;
    const char *exclusive_group_impl() const noexcept override;
    const char *exclusive_group_label_impl() const noexcept override;
    const char *exclusive_group_tooltip_impl() const noexcept override;
    slicing_step_t step_impl() const noexcept override;
    const char *const *dependencies_impl() const noexcept override;
    int32_t priority_impl() const noexcept override;
    const char *progress_message_format_impl() const noexcept override;
    void setup_run_impl(const plugin_run_context *run_ctx) const override;
    void run_impl(const plugin_run_context *run_ctx) const override;
};

void register_create_empty_surface_plugin(orchestrator_handle *orch);

}}} // namespace slic3r_api::SurfaceGeneration::CreateEmptySurfacePlugin

#endif // plugins_surface_createemptysurface_hpp_
