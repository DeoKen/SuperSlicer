///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_surface_topsurfaceexpansion_hpp_
#define plugins_surface_topsurfaceexpansion_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace SurfaceGeneration { namespace TopSurfaceExpansionPlugin {

// STEP_SURFACE_GENERATION refinement plugin.
//
// The initial surface builder marks exact top areas. SolidShells projects those
// exact areas into lower layers. TopSurfaceExpansion applies the configured
// external_infill_margin around top surfaces and carries that same margin into
// the lower top-shell layers so solid infill supports the enlarged top skin.
class TopSurfaceExpansion : public PluginBase
{
public:
    static TopSurfaceExpansion &instance(orchestrator_handle *orch);

private:
    TopSurfaceExpansion(orchestrator_handle *orch) : PluginBase(orch) {}

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

void register_top_surface_expansion_plugin(orchestrator_handle *orch);

}}} // namespace slic3r_api::SurfaceGeneration::TopSurfaceExpansionPlugin

#endif // plugins_surface_topsurfaceexpansion_hpp_
