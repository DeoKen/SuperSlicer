///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_perimeter_arachneperimetergenerator_hpp_
#define plugins_perimeter_arachneperimetergenerator_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace Perimeter { namespace ArachnePerimeterGeneratorPlugin {

// STEP_PERIMETER implementation backed by libArachne's variable-width wall
// generator. This plugin is intentionally independent from the temporary
// SimplePerimeterGenerator pipeline: it publishes complete Arachne walls for
// the current layer island and the corresponding fill areas.
class ArachnePerimeterGenerator : public PluginBase
{
public:
    static ArachnePerimeterGenerator &instance(orchestrator_handle *orch);

private:
    ArachnePerimeterGenerator(orchestrator_handle *orch) : PluginBase(orch) {}

    const char *id_impl() const noexcept override;
    slicing_step_t step_impl() const noexcept override;
    const char *const *dependencies_impl() const noexcept override;
    int32_t priority_impl() const noexcept override;
    const char *progress_message_format_impl() const noexcept override;
    void setup_run_impl(const plugin_run_context *run_ctx) const override;
    void run_impl(const plugin_run_context *run_ctx) const override;
};

void register_arachne_perimeter_generator_plugin(orchestrator_handle *orch);

}}} // namespace slic3r_api::Perimeter::ArachnePerimeterGeneratorPlugin

#endif // plugins_perimeter_arachneperimetergenerator_hpp_
