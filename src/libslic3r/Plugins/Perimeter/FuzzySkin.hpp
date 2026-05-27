///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_perimeter_fuzzyskin_hpp_
#define plugins_perimeter_fuzzyskin_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace Perimeter { namespace FuzzySkinPlugin {

// STEP_POST_PERIMETER plugin that turns smooth perimeter polylines into
// randomly perturbed "fuzzy skin" strokes. It runs after perimeter generation
// so perimeter generators do not need to know how fuzzy skin is implemented.
class FuzzySkin : public PluginBase
{
public:
    static FuzzySkin &instance(orchestrator_handle *orch);

private:
    FuzzySkin(orchestrator_handle *orch) : PluginBase(orch) {}

    const char *id_impl() const noexcept override;
    const char *name_impl() const noexcept override;
    const char *description_impl() const noexcept override;
    slicing_step_t step_impl() const noexcept override;
    const char *const *dependencies_impl() const noexcept override;
    int32_t priority_impl() const noexcept override;
    int32_t used_config_keys(const char **keys) const noexcept override;
    const char *progress_message_format_impl() const noexcept override;
    void setup_run_impl(const plugin_run_context *run_ctx) const override;
    void run_impl(const plugin_run_context *run_ctx) const override;
};

void register_fuzzy_skin_plugin(orchestrator_handle *orch);

}}} // namespace slic3r_api::Perimeter::FuzzySkinPlugin

#endif // plugins_perimeter_fuzzyskin_hpp_
