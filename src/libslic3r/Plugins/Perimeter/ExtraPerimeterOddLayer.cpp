///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "ExtraPerimeterOddLayer.hpp"

#include <cstdint>
#include <vector>

#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_perimeter.h"
#include "libslic3r/Api/plugin/cpp/PerimeterStepViews.hpp"

namespace slic3r_api { namespace Perimeter { namespace ExtraPerimeterOddLayerPlugin {

namespace {

const char *k_extra_perimeter_odd_layer_id = "perimeter.module.extra_perimeter_odd_layer";
const char *k_no_dependencies[] = { nullptr };
const char *k_used_config_keys[] = { "extra_perimeters_odd_layers" };
const char *k_extra_perimeter_odd_layer_key = "extra_perimeters_odd_layers";

void request_extra_perimeter_for_children(PerimeterGenerationContextView &context,
                                          const PerimeterNodeView &parent,
                                          const RegionSettingsClip &clip)
{
    const std::vector<PerimeterNodeView> children = parent.children_snapshot();
    for (const PerimeterNodeView &child : children) {
        const std::vector<PerimeterNodeView> inside_nodes = context.split_node(child, clip);
        for (const PerimeterNodeView &inside_node : inside_nodes)
            inside_node.request_current_perimeter();
    }
}

void module_start(void *, perimeter_generation_context *context)
{
    if (context == nullptr || context->root == nullptr)
        return;

    PerimeterGenerationContextView context_view(context);
    if (context_view.island().region_count() == 0)
        return;

    const uint32_t layer_id = context_view.layer_id_from_object();
    if (layer_id == uint32_t(-1))
        return;

    // This mirrors the old "solo config" path: when the whole current island
    // uses extra_perimeters_odd_layers, add one perimeter on odd layer ids.
    if (layer_id % 2 == 1) {
        RegionSettings settings = context_view.region_settings({{k_extra_perimeter_odd_layer_key}});
        settings.segregate(context_view.island().slice());
        if (!settings.has_many_config(k_extra_perimeter_odd_layer_key) &&
            settings.get_solo_config(k_extra_perimeter_odd_layer_key).get_bool())
            context_view.root().add_perimeters(1);
    }
}

void module_after(void *, perimeter_generation_context *context, perimeter_node *node)
{
    if (context == nullptr || node == nullptr)
        return;

    PerimeterGenerationContextView context_view(context);
    if (context_view.island().region_count() == 0)
        return;

    const uint32_t layer_id = context_view.layer_id_from_object();
    if (layer_id == uint32_t(-1) || layer_id % 2 == 0)
        return;

    PerimeterNodeView parent(node);
    if (!parent.is_last_perimeter() || parent.child_count() == 0)
        return;

    RegionSettings settings = context_view.region_settings({{k_extra_perimeter_odd_layer_key}});
    settings.segregate(context_view.island().slice());
    if (!settings.has_many_config(k_extra_perimeter_odd_layer_key))
        return;

    const RegionSettings::AreaMap &areas = settings.get_areas(k_extra_perimeter_odd_layer_key);
    for (const std::pair<const RegionSettingsValue, RegionSettingsClip> &entry : areas)
        if (entry.first.get_bool())
            request_extra_perimeter_for_children(context_view, parent, entry.second);
}

const perimeter_generation_module_vtable &module_vtable()
{
    static const perimeter_generation_module_vtable vt = {
        &module_start,
        nullptr,
        &module_after,
        nullptr
    };
    return vt;
}

} // namespace

ExtraPerimeterOddLayer &
ExtraPerimeterOddLayer::instance(orchestrator_handle *orch)
{
    static ExtraPerimeterOddLayer s_instance(orch);
    return s_instance;
}

const char *ExtraPerimeterOddLayer::id_impl() const noexcept
{
    return k_extra_perimeter_odd_layer_id;
}

slicing_step_t ExtraPerimeterOddLayer::step_impl() const noexcept
{
    return PERIMETER_GENERATION_MODULE;
}

const char *const *ExtraPerimeterOddLayer::dependencies_impl() const noexcept
{
    return k_no_dependencies;
}

int32_t ExtraPerimeterOddLayer::priority_impl() const noexcept
{
    return 0;
}

int32_t ExtraPerimeterOddLayer::used_config_keys(const char **keys) const noexcept
{
    if (keys != nullptr)
        keys[0] = k_used_config_keys[0];
    return 1;
}

const char *ExtraPerimeterOddLayer::progress_message_format_impl() const noexcept
{
    return "Extra perimeter odd layer: %u / %u";
}

void ExtraPerimeterOddLayer::run_impl(const plugin_run_context *run_ctx) const
{
    run_ctx_perimeter_generation_module *ctx = plugin_ctx_as_perimeter_generation_module(run_ctx);
    if (ctx == nullptr)
        return;

    ctx->module.ctx = const_cast<ExtraPerimeterOddLayer *>(this);
    ctx->module.vt = &module_vtable();
}

void register_extra_perimeter_odd_layer_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, ExtraPerimeterOddLayer::instance(orch).c_instance());
}

}}} // namespace slic3r_api::Perimeter::ExtraPerimeterOddLayerPlugin

#ifdef EXTRA_PERIMETER_ODD_LAYER_PLUGIN_DLL
extern "C" void register_plugin(orchestrator_handle *orch)
{
    slic3r_api::Perimeter::ExtraPerimeterOddLayerPlugin::register_extra_perimeter_odd_layer_plugin(orch);
}
#endif // EXTRA_PERIMETER_ODD_LAYER_PLUGIN_DLL
