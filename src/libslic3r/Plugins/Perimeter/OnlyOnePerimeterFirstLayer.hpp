///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_perimeter_onlyoneperimeterfirstlayer_hpp_
#define plugins_perimeter_onlyoneperimeterfirstlayer_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace Perimeter { namespace OnlyOnePerimeterFirstLayerPlugin {

// PerimeterGenerationModule port of only_one_perimeter_first_layer.
//
// The old setting was evaluated once per region before perimeter generation.
// This module applies the same "one perimeter on first layer" rule after the
// first perimeter ring exists, which lets RegionSettings split only the parts
// of an island where a modifier/region actually enables the setting.
class OnlyOnePerimeterFirstLayer : public PluginBase
{
public:
    static OnlyOnePerimeterFirstLayer &instance(orchestrator_handle *orch);

private:
    OnlyOnePerimeterFirstLayer(orchestrator_handle *orch) : PluginBase(orch) {}

    const char *id_impl() const noexcept override;
    slicing_step_t step_impl() const noexcept override;
    const char *const *dependencies_impl() const noexcept override;
    int32_t priority_impl() const noexcept override;
    int32_t used_config_keys(const char **keys) const noexcept override;
    const char *progress_message_format_impl() const noexcept override;
    void run_impl(const plugin_run_context *run_ctx) const override;
};

void register_only_one_perimeter_first_layer_plugin(orchestrator_handle *orch);

}}} // namespace slic3r_api::Perimeter::OnlyOnePerimeterFirstLayerPlugin

#endif // plugins_perimeter_onlyoneperimeterfirstlayer_hpp_
