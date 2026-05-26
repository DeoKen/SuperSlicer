///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_surface_defaultsurfacetype_hpp_
#define plugins_surface_defaultsurfacetype_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace Slic3r {
class PrintObject;
}

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

    // Internal decomposition of the old PrintObject::prepare_infill() body.
    // These are deliberately private host-side modules, not ABI hooks yet.
    void run_surface_type_pipeline(const plugin_run_context *run_ctx, Slic3r::PrintObject &object) const;
    bool start_prepare_infill(Slic3r::PrintObject &object) const;

    // Input repair and initial classification.
    void restore_untyped_input_if_needed(const plugin_run_context *run_ctx, Slic3r::PrintObject &object) const;
    void classify_top_bottom_surfaces(const plugin_run_context *run_ctx, Slic3r::PrintObject &object) const;
    void prepare_fill_surfaces(const plugin_run_context *run_ctx, Slic3r::PrintObject &object) const;

    // Shell and external-surface refinement.
    void apply_external_expansion_and_bridge_detection(const plugin_run_context *run_ctx,
                                                       Slic3r::PrintObject &object,
                                                       bool old_algorithm) const;
    void ensure_vertical_shells(const plugin_run_context *run_ctx, Slic3r::PrintObject &object) const;
    void ensure_horizontal_shells(const plugin_run_context *run_ctx, Slic3r::PrintObject &object) const;

    // Final cleanup before infill generation consumes the surfaces.
    void clean_surface_collections(const plugin_run_context *run_ctx, Slic3r::PrintObject &object) const;
    void build_bridge_over_infill_data(const plugin_run_context *run_ctx, Slic3r::PrintObject &object) const;
    void combine_infill_surfaces(const plugin_run_context *run_ctx, Slic3r::PrintObject &object) const;
};

void register_default_surface_type_plugin(orchestrator_handle *orch);

}}} // namespace slic3r_api::SurfaceType::DefaultSurfaceTypePlugin

#endif // plugins_surface_defaultsurfacetype_hpp_
