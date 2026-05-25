///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_perimeter_removegapfillonoverhangs_hpp_
#define plugins_perimeter_removegapfillonoverhangs_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace Perimeter { namespace RemoveGapFillOnOverhangsPlugin {

// PerimeterGenerationModule port of gap_fill_no_overhang.
//
// The module runs after a node has produced perimeter/gap-fill extrusions. It
// computes the part of the current node area that is enabled for the setting
// and unsupported by lower islands, then clips non-loop local polylines away
// from that forbidden area.
class RemoveGapFillOnOverhangs : public PluginBase
{
public:
    static RemoveGapFillOnOverhangs &instance(orchestrator_handle *orch);

private:
    RemoveGapFillOnOverhangs(orchestrator_handle *orch) : PluginBase(orch) {}

    const char *id_impl() const noexcept override;
    slicing_step_t step_impl() const noexcept override;
    const char *const *dependencies_impl() const noexcept override;
    int32_t priority_impl() const noexcept override;
    int32_t used_config_keys(const char **keys) const noexcept override;
    const char *progress_message_format_impl() const noexcept override;
    void run_impl(const plugin_run_context *run_ctx) const override;
};

void register_remove_gap_fill_on_overhangs_plugin(orchestrator_handle *orch);

}}} // namespace slic3r_api::Perimeter::RemoveGapFillOnOverhangsPlugin

#endif // plugins_perimeter_removegapfillonoverhangs_hpp_
