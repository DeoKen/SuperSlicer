///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#pragma once

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace MaxOverhangThresholdPlugin {

// Official post-slicing plugin that reproduces the legacy
// PrintObject::_max_overhang_threshold() pass through the public plugin API.
// It edits raw LayerRegion slices, then asks the host to rebuild the dependent
// layer slice/island caches.
class MaxOverhangThreshold : public PluginBase
{
public:
    static MaxOverhangThreshold &instance();

private:
    const char *id_impl() const noexcept override;
    slicing_step_t step_impl() const noexcept override;
    const char *const *dependencies_impl() const noexcept override;
    int32_t priority_impl() const noexcept override;
    const char *progress_message_format_impl() const noexcept override;
    void setup_run_impl(const plugin_run_context *run_ctx) const override;
    void run_impl(const plugin_run_context *run_ctx) const override;
};

// Register the singleton plugin in an orchestrator or external plugin loader.
void register_max_overhang_threshold_plugin(orchestrator_handle *orch);

}} // namespace slic3r_api::MaxOverhangThresholdPlugin
