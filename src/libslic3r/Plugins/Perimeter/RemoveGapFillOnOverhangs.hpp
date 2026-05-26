///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_perimeter_removegapfillonoverhangs_hpp_
#define plugins_perimeter_removegapfillonoverhangs_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace Perimeter { namespace RemoveGapFillOnOverhangsPlugin {

// Removes unsafe gap-fill strokes from unsupported overhang areas.
//
// During perimeter generation, the host creates closed perimeter loops and open
// gap-fill lines for each node area. When gap_fill_no_overhang is enabled,
// those open gap-fill lines should not be printed where there is no lower island
// below them: a thin free-hanging line is fragile and often curls or fails to
// support the next layer cleanly.
//
// This module runs after a node has generated its local extrusions. It builds
// the part of the node area where the setting is enabled, subtracts the union of
// lower islands, and clips only open local polylines from that unsupported
// area. Closed perimeter loops, nested collections, and the area/fill-area tree
// are intentionally left unchanged.
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
