///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "OnlyOnePerimeterFirstLayer.hpp"

#include <cstdint>
#include <utility>
#include <vector>

#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_perimeter.h"
#include "libslic3r/Api/plugin/cpp/PerimeterStepViews.hpp"

namespace slic3r_api { namespace Perimeter { namespace OnlyOnePerimeterFirstLayerPlugin {

namespace {

const char *k_only_one_perimeter_first_layer_id = "perimeter.module.only_one_perimeter_first_layer";
const char *k_no_dependencies[] = { nullptr };
const char *k_used_config_keys[] = { "only_one_perimeter_first_layer" };
const char *k_only_one_perimeter_first_layer_key = "only_one_perimeter_first_layer";

void set_children_to_one_perimeter(const PerimeterNodeView &parent)
{
    const std::vector<PerimeterNodeView> children = parent.children_snapshot();
    for (const PerimeterNodeView &child : children)
        child.set_perimeter_needed(1);
}

void set_enabled_children_to_one_perimeter(PerimeterGenerationContextView &context,
                                           const PerimeterNodeView &parent,
                                           const RegionSettingsClip &enabled_area)
{
    const std::vector<PerimeterNodeView> children = parent.children_snapshot();
    for (const PerimeterNodeView &child : children) {
        const std::vector<PerimeterNodeView> inside_nodes = context.split_node(child, enabled_area);
        for (const PerimeterNodeView &inside_node : inside_nodes)
            inside_node.set_perimeter_needed(1);
    }
}

void module_after(void *, void *, perimeter_generation_context *context, perimeter_node *node)
{
    if (context == nullptr || node == nullptr)
        return;

    PerimeterGenerationContextView context_view(context);
    if (context_view.island().region_count() == 0)
        return;

    const uint32_t layer_id = context_view.layer_id_from_object();
    if (layer_id != 0)
        return;

    PerimeterNodeView parent(node);
    if (parent.perimeter_idx() != 0 || parent.child_count() == 0)
        return;

    RegionSettings settings = context_view.region_settings({{k_only_one_perimeter_first_layer_key}});
    settings.segregate(context_view.island().slice());

    if (!settings.has_many_config(k_only_one_perimeter_first_layer_key)) {
        if (settings.get_solo_config(k_only_one_perimeter_first_layer_key).get_bool(
                k_only_one_perimeter_first_layer_key))
            set_children_to_one_perimeter(parent);
        return;
    }

    const RegionSettings::AreaMap &areas = settings.get_areas(k_only_one_perimeter_first_layer_key);
    for (const std::pair<const RegionSettingsValue, RegionSettingsClip> &entry : areas) {
        if (!entry.first.get_bool(k_only_one_perimeter_first_layer_key))
            continue;
        set_enabled_children_to_one_perimeter(context_view, parent, entry.second);
    }
}

const perimeter_generation_module_vtable &module_vtable()
{
    static const perimeter_generation_module_vtable vt = {
        nullptr,
        nullptr,
        &module_after,
        nullptr
    };
    return vt;
}

} // namespace

OnlyOnePerimeterFirstLayer &
OnlyOnePerimeterFirstLayer::instance(orchestrator_handle *orch)
{
    static OnlyOnePerimeterFirstLayer s_instance(orch);
    return s_instance;
}

const char *OnlyOnePerimeterFirstLayer::id_impl() const noexcept
{
    return k_only_one_perimeter_first_layer_id;
}

slicing_step_t OnlyOnePerimeterFirstLayer::step_impl() const noexcept
{
    return PERIMETER_GENERATION_MODULE;
}

const char *const *OnlyOnePerimeterFirstLayer::dependencies_impl() const noexcept
{
    return k_no_dependencies;
}

int32_t OnlyOnePerimeterFirstLayer::priority_impl() const noexcept
{
    return 7;
}

int32_t OnlyOnePerimeterFirstLayer::used_config_keys(const char **keys) const noexcept
{
    if (keys != nullptr)
        keys[0] = k_used_config_keys[0];
    return 1;
}

const char *OnlyOnePerimeterFirstLayer::progress_message_format_impl() const noexcept
{
    return "Only one perimeter on first layer: %u / %u";
}

void OnlyOnePerimeterFirstLayer::run_impl(const plugin_run_context *run_ctx) const
{
    run_ctx_perimeter_generation_module *ctx = plugin_ctx_as_perimeter_generation_module(run_ctx);
    if (ctx == nullptr)
        return;

    ctx->module.ctx = const_cast<OnlyOnePerimeterFirstLayer *>(this);
    ctx->module.vt = &module_vtable();
}

void register_only_one_perimeter_first_layer_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, OnlyOnePerimeterFirstLayer::instance(orch).c_instance());
}

}}} // namespace slic3r_api::Perimeter::OnlyOnePerimeterFirstLayerPlugin

#ifdef ONLY_ONE_PERIMETER_FIRST_LAYER_PLUGIN_DLL
extern "C" void register_plugin(orchestrator_handle *orch)
{
    slic3r_api::Perimeter::OnlyOnePerimeterFirstLayerPlugin::register_only_one_perimeter_first_layer_plugin(orch);
}
#endif // ONLY_ONE_PERIMETER_FIRST_LAYER_PLUGIN_DLL
