///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_surface_infillregioncompatibilitysplitter_hpp_
#define plugins_surface_infillregioncompatibilitysplitter_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace SurfaceGeneration { namespace InfillRegionCompatibilitySplitterPlugin {

// Final STEP_SURFACE_GENERATION compatibility pass.
//
// Earlier surface plugins try to keep surfaces as large as possible, often
// spanning several PrintRegion objects. This is good for geometry performance,
// but infill generation can only batch regions together when every setting
// relevant to that surface's infill recipe is compatible. This plugin performs
// the final split: it cuts LayerRegionIsland fill surfaces by the few region
// settings that affect infill execution and moves the pieces into compatible
// LayerRegionIsland groups.
class InfillRegionCompatibilitySplitter : public PluginBase
{
public:
    static InfillRegionCompatibilitySplitter &instance(orchestrator_handle *orch);

private:
    explicit InfillRegionCompatibilitySplitter(orchestrator_handle *orch) : PluginBase(orch) {}

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

void register_infill_region_compatibility_splitter_plugin(orchestrator_handle *orch);

}}} // namespace slic3r_api::SurfaceGeneration::InfillRegionCompatibilitySplitterPlugin

#endif // plugins_surface_infillregioncompatibilitysplitter_hpp_
