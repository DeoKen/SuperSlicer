///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "ExtraPerimeterCount.hpp"

#include <cstdint>
#include <map>
#include <vector>

#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_perimeter.h"
#include "libslic3r/Api/plugin/cpp/PerimeterStepViews.hpp"

namespace slic3r_api { namespace Perimeter { namespace ExtraPerimeterCountPlugin {

namespace {

const char *k_extra_perimeter_count_id = "perimeter.module.extra_perimeter_count";
const char *k_no_dependencies[] = { nullptr };
const char *k_used_config_keys[] = { "extra_perimeters_count" };
const char *k_extra_perimeter_count_key = "extra_perimeters_count";

class ModuleState
{
public:
    int32_t get(const perimeter_node *node) const
    {
        const std::map<const perimeter_node *, int32_t>::const_iterator it = counts.find(node);
        return it == counts.end() ? 0 : it->second;
    }

    void set(const perimeter_node *node, int32_t count)
    {
        counts[node] = count;
    }

private:
    std::map<const perimeter_node *, int32_t> counts;
};

bool build_extra_clip(const RegionSettings::AreaMap &areas,
                      int32_t already_extruded_extra,
                      RegionSettingsClip &clip_out)
{
    bool has_clip = false;
    for (const std::pair<const RegionSettingsValue, RegionSettingsClip> &entry : areas) {
        const int32_t extra_perimeters_count = entry.first.get_int(k_extra_perimeter_count_key);
        if (already_extruded_extra >= extra_perimeters_count)
            continue;

        if (entry.second.is_accept_all()) {
            clip_out.make_accept_all();
            return true;
        }

        clip_out.append_copy_from(entry.second.expolygons());
        has_clip = true;
    }

    if (has_clip)
        clip_out.union_self();
    return has_clip;
}

void request_extra_for_inside_nodes(const std::vector<PerimeterNodeView> &inside_nodes,
                                    ModuleState &state,
                                    int32_t next_extra_count)
{
    for (const PerimeterNodeView &inside_node : inside_nodes) {
        inside_node.request_current_perimeter();
        state.set(inside_node.handle(), next_extra_count);
    }
}

void *module_start(void *, perimeter_generation_context *context)
{
    ModuleState *state = new ModuleState();
    if (context == nullptr || context->root == nullptr)
        return state;

    PerimeterGenerationContextView context_view(context);
    if (context_view.island().region_count() == 0)
        return state;

    RegionSettings settings = context_view.region_settings({{k_extra_perimeter_count_key}});
    settings.segregate(context_view.island().slice());
    if (settings.has_many_config(k_extra_perimeter_count_key))
        return state;

    const int32_t extra_perimeters_count =
        settings.get_solo_config(k_extra_perimeter_count_key).get_int(k_extra_perimeter_count_key);
    if (extra_perimeters_count > 0)
        context_view.root().add_perimeters(uint32_t(extra_perimeters_count));
    return state;
}

void module_after(void *, void *user_context, perimeter_generation_context *context, perimeter_node *node)
{
    ModuleState *state = static_cast<ModuleState *>(user_context);
    if (context == nullptr || node == nullptr || state == nullptr)
        return;

    PerimeterGenerationContextView context_view(context);
    if (context_view.island().region_count() == 0)
        return;

    PerimeterNodeView parent(node);
    if (parent.child_count() == 0)
        return;

    RegionSettings settings = context_view.region_settings({{k_extra_perimeter_count_key}});
    settings.segregate(context_view.island().slice());
    if (!settings.has_many_config(k_extra_perimeter_count_key))
        return;

    const RegionSettings::AreaMap &areas = settings.get_areas(k_extra_perimeter_count_key);
    const int32_t already_extruded_extra = state->get(node);
    const std::vector<PerimeterNodeView> children = parent.children_snapshot();

    for (const PerimeterNodeView &child : children) {
        if (child.needs_more_perimeters())
            return;

        RegionSettingsClip eligible_clip(context_view.storage());
        if (!build_extra_clip(areas, already_extruded_extra, eligible_clip))
            continue;

        const std::vector<PerimeterNodeView> inside_nodes = context_view.split_node(child, eligible_clip);
        request_extra_for_inside_nodes(inside_nodes, *state, already_extruded_extra + 1);
    }
}

void module_end(void *, void *user_context, perimeter_generation_context *)
{
    delete static_cast<ModuleState *>(user_context);
}

const perimeter_generation_module_vtable &module_vtable()
{
    static const perimeter_generation_module_vtable vt = {
        &module_start,
        nullptr,
        &module_after,
        &module_end
    };
    return vt;
}

} // namespace

ExtraPerimeterCount &
ExtraPerimeterCount::instance(orchestrator_handle *orch)
{
    static ExtraPerimeterCount s_instance(orch);
    return s_instance;
}

const char *ExtraPerimeterCount::id_impl() const noexcept
{
    return k_extra_perimeter_count_id;
}

slicing_step_t ExtraPerimeterCount::step_impl() const noexcept
{
    return PERIMETER_GENERATION_MODULE;
}

const char *const *ExtraPerimeterCount::dependencies_impl() const noexcept
{
    return k_no_dependencies;
}

int32_t ExtraPerimeterCount::priority_impl() const noexcept
{
    return -20;
}

int32_t ExtraPerimeterCount::used_config_keys(const char **keys) const noexcept
{
    if (keys != nullptr)
        keys[0] = k_used_config_keys[0];
    return 1;
}

const char *ExtraPerimeterCount::progress_message_format_impl() const noexcept
{
    return "Extra perimeter count: %u / %u";
}

void ExtraPerimeterCount::run_impl(const plugin_run_context *run_ctx) const
{
    run_ctx_perimeter_generation_module *ctx = plugin_ctx_as_perimeter_generation_module(run_ctx);
    if (ctx == nullptr)
        return;

    ctx->module.ctx = const_cast<ExtraPerimeterCount *>(this);
    ctx->module.vt = &module_vtable();
}

void register_extra_perimeter_count_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, ExtraPerimeterCount::instance(orch).c_instance());
}

}}} // namespace slic3r_api::Perimeter::ExtraPerimeterCountPlugin

#ifdef EXTRA_PERIMETER_COUNT_PLUGIN_DLL
extern "C" void register_plugin(orchestrator_handle *orch)
{
    slic3r_api::Perimeter::ExtraPerimeterCountPlugin::register_extra_perimeter_count_plugin(orch);
}
#endif // EXTRA_PERIMETER_COUNT_PLUGIN_DLL
