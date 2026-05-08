///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#pragma once

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

#include <string>

#ifdef _DEBUG
namespace Slic3r {
class Print;
class PrintObject;
}
#endif

namespace slic3r_api { namespace StandardLayerHeightGeneratorPlugin {

// Official STEP_LAYER_HEIGHT plugin that computes the per-object layer height
// profile from object config, print config, enforced Z values, and user layer
// ranges. The host consumes the generated profile to recreate object layers in
// the following slicing step.
class StandardLayerHeightGenerator : public PluginBase
{
public:
    static StandardLayerHeightGenerator &instance();

private:
    const char *id_impl() const noexcept override;
    slicing_step_t step_impl() const noexcept override;
    const char *const *dependencies_impl() const noexcept override;
    int32_t priority_impl() const noexcept override;
    void run_impl(const plugin_run_context *run_ctx) const override;
};

// Register the singleton plugin in an orchestrator or external plugin loader.
void register_standard_layer_height_generator_plugin(orchestrator_handle *orch);
#ifdef _DEBUG
// Debug-only host helper used to compare the plugin-side parameter recreation
// against native PrintObject::slicing_parameters().
bool test_layer_height_slicing_parameters(const Slic3r::Print &print,
                                          const Slic3r::PrintObject &object,
                                          std::string &out_error);
#endif

}} // namespace slic3r_api::StandardLayerHeightGeneratorPlugin
