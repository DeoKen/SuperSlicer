///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_flat_area_layer_height_hpp_
#define plugins_flat_area_layer_height_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace FlatAreaLayerHeightPlugin {

// STEP_LAYER_HEIGHT plugin that tries to place layer boundaries on large
// horizontal mesh surfaces while staying inside the configured min/max layer
// height range.
class FlatAreaLayerHeight : public PluginBase
{
public:
    static FlatAreaLayerHeight &instance(orchestrator_handle *orch);
    static const char *print_ui_fragment() noexcept;

private:
    FlatAreaLayerHeight(orchestrator_handle *orch) : PluginBase(orch) {}

    const char *id_impl() const noexcept override;
    slicing_step_t step_impl() const noexcept override;
    const char *const *dependencies_impl() const noexcept override;
    int32_t priority_impl() const noexcept override;
    int32_t used_config_keys(const char **keys) const noexcept override;
    const char *progress_message_format_impl() const noexcept override;
    void inilialize_impl(storage_handle *storage) const override;
    void setup_run_impl(const plugin_run_context *run_ctx) const override;
    void run_impl(const plugin_run_context *run_ctx) const override;
};

void register_flat_area_layer_height_plugin(orchestrator_handle *orch);

}} // namespace slic3r_api::FlatAreaLayerHeightPlugin

#endif // plugins_flat_area_layer_height_hpp_
