///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_surface_defaultsurfacegenerator_hpp_
#define plugins_surface_defaultsurfacegenerator_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace SurfaceGeneration { namespace DefaultSurfaceGeneratorPlugin {

// Baseline STEP_SURFACE_GENERATION plugin.
//
// Perimeter generation now publishes island-level fill areas. This plugin is
// the boundary that converts those areas into LayerRegion::fill_surfaces(),
// leaving top/bottom/bridge classification to STEP_SURFACE_TYPE.
class DefaultSurfaceGenerator : public PluginBase
{
public:
    static DefaultSurfaceGenerator &instance(orchestrator_handle *orch);

private:
    DefaultSurfaceGenerator(orchestrator_handle *orch) : PluginBase(orch) {}

    const char *id_impl() const noexcept override;
    slicing_step_t step_impl() const noexcept override;
    const char *const *dependencies_impl() const noexcept override;
    int32_t priority_impl() const noexcept override;
    const char *progress_message_format_impl() const noexcept override;
    void setup_run_impl(const plugin_run_context *run_ctx) const override;
    void run_impl(const plugin_run_context *run_ctx) const override;
};

void register_default_surface_generator_plugin(orchestrator_handle *orch);

}}} // namespace slic3r_api::SurfaceGeneration::DefaultSurfaceGeneratorPlugin

#endif // plugins_surface_defaultsurfacegenerator_hpp_
