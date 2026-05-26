///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_perimeter_extraperimetercount_hpp_
#define plugins_perimeter_extraperimetercount_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace Perimeter { namespace ExtraPerimeterCountPlugin {

// PerimeterGenerationModule port of extra_perimeters_count.
//
// In the uniform case it simply increases the root perimeter count. When the
// value differs between regions/modifiers, it asks the generator to split child
// nodes by the active areas and requests one additional perimeter pass for the
// matching children until each branch reaches the configured count.
class ExtraPerimeterCount : public PluginBase
{
public:
    static ExtraPerimeterCount &instance(orchestrator_handle *orch);

private:
    ExtraPerimeterCount(orchestrator_handle *orch) : PluginBase(orch) {}

    const char *id_impl() const noexcept override;
    slicing_step_t step_impl() const noexcept override;
    const char *const *dependencies_impl() const noexcept override;
    int32_t priority_impl() const noexcept override;
    int32_t used_config_keys(const char **keys) const noexcept override;
    const char *progress_message_format_impl() const noexcept override;
    void run_impl(const plugin_run_context *run_ctx) const override;
};

void register_extra_perimeter_count_plugin(orchestrator_handle *orch);

}}} // namespace slic3r_api::Perimeter::ExtraPerimeterCountPlugin

#endif // plugins_perimeter_extraperimetercount_hpp_
