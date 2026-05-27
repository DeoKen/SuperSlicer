///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_perimeter_separateholecontour_hpp_
#define plugins_perimeter_separateholecontour_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace Perimeter { namespace SeparateHoleContourPlugin {

// PerimeterGenerationModule port of perimeters_hole.
//
// The module lets contour perimeters and hole perimeters stop at different
// depths. It removes the loop class that is past its configured count and asks
// the generator to continue from areas rebuilt around the remaining loops.
class SeparateHoleContour : public PluginBase
{
public:
    static SeparateHoleContour &instance(orchestrator_handle *orch);

private:
    SeparateHoleContour(orchestrator_handle *orch) : PluginBase(orch) {}

    const char *id_impl() const noexcept override;
    slicing_step_t step_impl() const noexcept override;
    const char *const *dependencies_impl() const noexcept override;
    int32_t priority_impl() const noexcept override;
    int32_t used_config_keys(raw_used_config_key *keys) const noexcept override;
    const char *progress_message_format_impl() const noexcept override;
    void run_impl(const plugin_run_context *run_ctx) const override;
};

void register_separate_hole_contour_plugin(orchestrator_handle *orch);

}}} // namespace slic3r_api::Perimeter::SeparateHoleContourPlugin

#endif // plugins_perimeter_separateholecontour_hpp_
