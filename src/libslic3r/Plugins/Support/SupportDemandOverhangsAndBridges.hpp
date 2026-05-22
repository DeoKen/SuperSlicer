///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_support_supportdemandoverhangsandbridges_hpp_
#define plugins_support_supportdemandoverhangsandbridges_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace Support { namespace SupportDemandOverhangsAndBridgesPlugin {

// Default STEP_SUPPORT_DEMAND plugin for FFF supports.
//
// It creates island-keyed demand polygons by comparing each layer island with
// the slices below it. When requested by the object config, bridgeable areas are
// removed from the demand through the bridge detector service.
class SupportDemandOverhangsAndBridges : public PluginBase
{
public:
    static SupportDemandOverhangsAndBridges &instance(orchestrator_handle *orch);

private:
    SupportDemandOverhangsAndBridges(orchestrator_handle *orch) : PluginBase(orch) {}

    const char *id_impl() const noexcept override;
    slicing_step_t step_impl() const noexcept override;
    const char *const *dependencies_impl() const noexcept override;
    int32_t priority_impl() const noexcept override;
    const char *progress_message_format_impl() const noexcept override;
    void setup_run_impl(const plugin_run_context *run_ctx) const override;
    void run_impl(const plugin_run_context *run_ctx) const override;
};

void register_support_demand_overhangs_and_bridges_plugin(orchestrator_handle *orch);

}}} // namespace slic3r_api::Support::SupportDemandOverhangsAndBridgesPlugin

#endif // plugins_support_supportdemandoverhangsandbridges_hpp_
