///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_perimeter_onlyoneperimeterontop_hpp_
#define plugins_perimeter_onlyoneperimeterontop_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace Perimeter { namespace OnlyOnePerimeterOnTopPlugin {

// PerimeterGenerationModule port of only_one_perimeter_top.
//
// The module detects top-facing parts of the current island and asks the active
// perimeter generator to stop the matching branches after one perimeter. The
// actual tree split remains owned by the generator through the perimeter-node
// split callback.
class OnlyOnePerimeterOnTop : public PluginBase
{
public:
    static OnlyOnePerimeterOnTop &instance(orchestrator_handle *orch);

private:
    OnlyOnePerimeterOnTop(orchestrator_handle *orch) : PluginBase(orch) {}

    const char *id_impl() const noexcept override;
    slicing_step_t step_impl() const noexcept override;
    const char *const *dependencies_impl() const noexcept override;
    int32_t priority_impl() const noexcept override;
    int32_t used_config_keys(raw_used_config_key *keys) const noexcept override;
    const char *progress_message_format_impl() const noexcept override;
    void run_impl(const plugin_run_context *run_ctx) const override;
};

void register_only_one_perimeter_on_top_plugin(orchestrator_handle *orch);

}}} // namespace slic3r_api::Perimeter::OnlyOnePerimeterOnTopPlugin

#endif // plugins_perimeter_onlyoneperimeterontop_hpp_
