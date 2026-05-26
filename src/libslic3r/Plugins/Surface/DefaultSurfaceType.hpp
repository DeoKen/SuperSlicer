///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_surface_defaultsurfacetype_hpp_
#define plugins_surface_defaultsurfacetype_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace SurfaceType { namespace DefaultSurfaceTypePlugin {

// Baseline STEP_SURFACE_TYPE plugin.
//
// The surface generator step creates raw sparse fill areas. This plugin runs
// the legacy native prepare-infill sequence, which classifies those areas as
// top/bottom/internal/bridge and performs the existing solid-shell refinements.
// Later passes may split this into smaller surface-type modules.
class DefaultSurfaceType : public PluginBase
{
public:
    static DefaultSurfaceType &instance(orchestrator_handle *orch);

private:
    DefaultSurfaceType(orchestrator_handle *orch) : PluginBase(orch) {}

    const char *id_impl() const noexcept override;
    slicing_step_t step_impl() const noexcept override;
    const char *const *dependencies_impl() const noexcept override;
    int32_t priority_impl() const noexcept override;
    const char *progress_message_format_impl() const noexcept override;
    void setup_run_impl(const plugin_run_context *run_ctx) const override;
    void run_impl(const plugin_run_context *run_ctx) const override;
};

void register_default_surface_type_plugin(orchestrator_handle *orch);

}}} // namespace slic3r_api::SurfaceType::DefaultSurfaceTypePlugin

#endif // plugins_surface_defaultsurfacetype_hpp_
