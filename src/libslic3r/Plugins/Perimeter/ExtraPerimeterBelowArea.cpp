///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "ExtraPerimeterBelowArea.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <vector>

#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_perimeter.h"
#include "libslic3r/Api/plugin/cpp/PerimeterStepViews.hpp"

namespace slic3r_api { namespace Perimeter { namespace ExtraPerimeterBelowAreaPlugin {

namespace {

const char *k_extra_perimeter_below_area_id = "perimeter.module.extra_perimeter_below_area";
const char *k_no_dependencies[] = { nullptr };
const char *k_used_config_keys[] = { "extra_perimeters_below_area" };
const char *k_extra_perimeter_below_area_key = "extra_perimeters_below_area";
const uint32_t k_force_many_perimeters = 9999;

double threshold_area_scaled(const PerimeterGenerationContextView &context, const RegionSettingsValue &value)
{
    const c_flow flow = context.perimeter_flow();
    const double perimeter_width_mm = unscaled(flow.width);
    const double reference_area_mm2 = perimeter_width_mm * perimeter_width_mm;
    const double area_mm2 = value.get_effective_value(reference_area_mm2, k_extra_perimeter_below_area_key);
    return scale_d(scale_d(area_mm2));
}

void force_extra_perimeters_if_small(const PerimeterNodeView &node, double area_scaled)
{
    if (node.surface().area() < area_scaled)
        node.add_perimeters(k_force_many_perimeters);
}

class SeenNodes
{
public:
    bool check_and_set(const layer_region_island_handle *region_island, const perimeter_node *node)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::set<const perimeter_node *> &nodes = m_nodes[region_island];
        const perimeter_node *current = node;
        while (current != nullptr) {
            if (nodes.find(current) != nodes.end())
                return true;
            current = current->parent == current ? nullptr : current->parent;
        }
        nodes.insert(node);
        return false;
    }

    void clear(const layer_region_island_handle *region_island)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_nodes.erase(region_island);
    }

private:
    std::mutex m_mutex;
    std::map<const layer_region_island_handle *, std::set<const perimeter_node *>> m_nodes;
};

SeenNodes &seen_nodes()
{
    static SeenNodes s_seen_nodes;
    return s_seen_nodes;
}

void module_after(void *, perimeter_generation_context *context, perimeter_node *node)
{
    if (context == nullptr || node == nullptr)
        return;

    PerimeterGenerationContextView context_view(context);
    if (context_view.island().region_count() == 0)
        return;

    PerimeterNodeView parent(node);
    if (parent.child_count() == 0 || seen_nodes().check_and_set(context->region_island, node))
        return;

    RegionSettings settings = context_view.region_settings({{k_extra_perimeter_below_area_key}});
    settings.segregate(context_view.island().slice());

    if (!settings.has_many_config(k_extra_perimeter_below_area_key) &&
        settings.get_solo_config(k_extra_perimeter_below_area_key).get_float(k_extra_perimeter_below_area_key) <= 0.)
        return;

    const std::vector<PerimeterNodeView> children = parent.children_snapshot();
    for (const PerimeterNodeView &child : children) {
        if (child.needs_more_perimeters())
            continue;

        const RegionSettings::AreaMap &areas = settings.get_areas(k_extra_perimeter_below_area_key);
        for (const std::pair<const RegionSettingsValue, RegionSettingsClip> &entry : areas) {
            if (entry.first.get_float(k_extra_perimeter_below_area_key) <= 0.)
                continue;

            const double area_scaled = threshold_area_scaled(context_view, entry.first);
            if (entry.second.is_accept_all()) {
                force_extra_perimeters_if_small(child, area_scaled);
                continue;
            }

            const std::vector<PerimeterNodeView> inside_nodes = context_view.split_node(child, entry.second);
            for (const PerimeterNodeView &inside_node : inside_nodes)
                force_extra_perimeters_if_small(inside_node, area_scaled);
        }
    }
}

void module_end(void *, perimeter_generation_context *context)
{
    if (context != nullptr)
        seen_nodes().clear(context->region_island);
}

const perimeter_generation_module_vtable &module_vtable()
{
    static const perimeter_generation_module_vtable vt = {
        nullptr,
        nullptr,
        &module_after,
        &module_end
    };
    return vt;
}

} // namespace

ExtraPerimeterBelowArea &
ExtraPerimeterBelowArea::instance(orchestrator_handle *orch)
{
    static ExtraPerimeterBelowArea s_instance(orch);
    return s_instance;
}

const char *ExtraPerimeterBelowArea::id_impl() const noexcept
{
    return k_extra_perimeter_below_area_id;
}

slicing_step_t ExtraPerimeterBelowArea::step_impl() const noexcept
{
    return PERIMETER_GENERATION_MODULE;
}

const char *const *ExtraPerimeterBelowArea::dependencies_impl() const noexcept
{
    return k_no_dependencies;
}

int32_t ExtraPerimeterBelowArea::priority_impl() const noexcept
{
    return -10;
}

int32_t ExtraPerimeterBelowArea::used_config_keys(const char **keys) const noexcept
{
    if (keys != nullptr)
        keys[0] = k_used_config_keys[0];
    return 1;
}

const char *ExtraPerimeterBelowArea::progress_message_format_impl() const noexcept
{
    return "Extra perimeter below area: %u / %u";
}

void ExtraPerimeterBelowArea::run_impl(const plugin_run_context *run_ctx) const
{
    run_ctx_perimeter_generation_module *ctx = plugin_ctx_as_perimeter_generation_module(run_ctx);
    if (ctx == nullptr)
        return;

    ctx->module.ctx = const_cast<ExtraPerimeterBelowArea *>(this);
    ctx->module.vt = &module_vtable();
}

void register_extra_perimeter_below_area_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, ExtraPerimeterBelowArea::instance(orch).c_instance());
}

}}} // namespace slic3r_api::Perimeter::ExtraPerimeterBelowAreaPlugin

#ifdef EXTRA_PERIMETER_BELOW_AREA_PLUGIN_DLL
extern "C" void register_plugin(orchestrator_handle *orch)
{
    slic3r_api::Perimeter::ExtraPerimeterBelowAreaPlugin::register_extra_perimeter_below_area_plugin(orch);
}
#endif // EXTRA_PERIMETER_BELOW_AREA_PLUGIN_DLL
