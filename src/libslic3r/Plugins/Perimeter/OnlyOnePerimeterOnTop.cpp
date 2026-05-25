///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "OnlyOnePerimeterOnTop.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_perimeter.h"
#include "libslic3r/Api/plugin/cpp/ClipperViews.hpp"
#include "libslic3r/Api/plugin/cpp/PerimeterStepViews.hpp"

namespace slic3r_api { namespace Perimeter { namespace OnlyOnePerimeterOnTopPlugin {

namespace {

const char *k_only_one_perimeter_on_top_id = "perimeter.module.only_one_perimeter_on_top";
const char *k_no_dependencies[] = { nullptr };
const char *k_used_config_keys[] = {
    "only_one_perimeter_top",
    "min_width_top_surface",
    "only_one_perimeter_top_other_algo",
    "external_infill_margin",
    "bridged_infill_margin",
    "gap_fill_enabled"
};
const char *k_only_one_perimeter_top_key = "only_one_perimeter_top";
const char *k_min_width_top_surface_key = "min_width_top_surface";
const char *k_only_one_perimeter_top_other_algo_key = "only_one_perimeter_top_other_algo";
const char *k_external_infill_margin_key = "external_infill_margin";
const char *k_bridged_infill_margin_key = "bridged_infill_margin";
const char *k_gap_fill_enabled_key = "gap_fill_enabled";

Config region_config(const PerimeterGenerationContextView &context)
{
    return context.island().region(0).print_region().config();
}

c_flow external_perimeter_flow(const PerimeterGenerationContextView &context)
{
    return context.island().region(0).flow(RAW_EXTRUSION_ROLE_EXTERNAL_PERIMETER);
}

StoredExPolygonCollection collection_from_expolygon(storage_handle *storage, const ExPolygon &expolygon)
{
    StoredExPolygonCollection collection(storage);
    collection.push_back(expolygon);
    return collection;
}

StoredExPolygonCollection lower_slice_coverage(storage_handle *storage, const LayerIsland &island)
{
    StoredExPolygonCollection lower_slices(storage);
    const std::vector<LayerIsland> lower_islands = island.lower_islands();
    for (const LayerIsland &lower_island : lower_islands)
        lower_slices.push_back(lower_island.slice());

    if (lower_slices.empty())
        return lower_slices;

    ClipperContext clip(storage);
    return clipper_union(clip(lower_slices)).to_expolygon_collection();
}

StoredExPolygonCollection upper_slice_coverage(storage_handle *storage, const LayerIsland &island)
{
    StoredExPolygonCollection upper_slices(storage);
    const std::vector<LayerIsland> upper_islands = island.upper_islands();
    for (const LayerIsland &upper_island : upper_islands)
        upper_slices.push_back(upper_island.slice());

    if (upper_slices.empty())
        return upper_slices;

    ClipperContext clip(storage);
    return clipper_union(clip(upper_slices)).to_expolygon_collection();
}

StoredExPolygonCollection union_append(storage_handle *storage,
                                       StoredExPolygonCollection &&lhs,
                                       StoredExPolygonCollection &&rhs)
{
    if (!rhs.empty())
        lhs.append_move_from(std::move(rhs));
    if (lhs.empty())
        return std::move(lhs);

    ClipperContext clip(storage);
    return clipper_union(clip(lhs)).to_expolygon_collection();
}

StoredExPolygonCollection offset_collection(storage_handle *storage,
                                            const ExPolygonCollection &subject,
                                            double delta)
{
    ClipperContext clip(storage);
    return clipper_offset(clip(subject), delta).to_expolygon_collection();
}

StoredExPolygonCollection diff_collection(storage_handle *storage,
                                          const ExPolygonCollection &subject,
                                          const ExPolygonCollection &clip_area)
{
    if (subject.empty() || clip_area.empty())
        return subject.clone(storage);

    ClipperContext clip(storage);
    return clipper_diff_with_safety_offset(clip(subject), clip(clip_area)).to_expolygon_collection();
}

StoredExPolygonCollection intersection_collection(storage_handle *storage,
                                                  const ExPolygonCollection &subject,
                                                  const ExPolygonCollection &clip_area)
{
    if (subject.empty() || clip_area.empty())
        return StoredExPolygonCollection(storage);

    ClipperContext clip(storage);
    return clipper_intersection_with_safety_offset(clip(subject), clip(clip_area)).to_expolygon_collection();
}

StoredExPolygonCollection build_upper_slices_for_area(const PerimeterGenerationContextView &context,
                                                      const RegionSettingsClip &enabled_area)
{
    storage_handle *storage = context.storage();
    StoredExPolygonCollection upper_slices = upper_slice_coverage(storage, context.island());
    if (enabled_area.is_accept_all())
        return upper_slices;

    // Outside the setting-enabled area, behave as if an upper layer existed.
    // This prevents the module from forcing one perimeter where the setting is
    // disabled by a region/modifier split.
    StoredExPolygonCollection island_surface = collection_from_expolygon(storage, context.island().slice());
    StoredExPolygonCollection disabled_area = enabled_area.diff(island_surface);
    return union_append(storage, std::move(upper_slices), std::move(disabled_area));
}

StoredExPolygonCollection build_bridge_checker(const PerimeterGenerationContextView &context,
                                               const Config &config,
                                               const ExPolygonCollection &orig_polygons,
                                               const ExPolygonCollection &lower_slices,
                                               uint32_t perimeter_count)
{
    storage_handle *storage = context.storage();
    if (lower_slices.empty())
        return StoredExPolygonCollection(storage);

    const c_flow perimeter_flow = context.perimeter_flow();
    const c_flow ext_flow = external_perimeter_flow(context);
    const double bridge_margin = config.get(k_bridged_infill_margin_key).get_effective_value(unscaled(ext_flow.width));
    double bridge_offset = double(perimeter_flow.spacing) * double(perimeter_count) + scale_d(bridge_margin);
    StoredExPolygonCollection bridge_checker = diff_collection(storage, orig_polygons, lower_slices);

    while (bridge_offset > SCALED_EPSILON && !bridge_checker.empty()) {
        double current_offset = double(perimeter_flow.spacing);
        if (bridge_offset < double(perimeter_flow.spacing) * 1.5)
            current_offset = bridge_offset;
        bridge_offset -= current_offset;

        StoredExPolygonCollection grown = offset_collection(storage, bridge_checker, current_offset);
        StoredExPolygonCollection clipped = intersection_collection(storage, grown, orig_polygons);
        ClipperContext clip(storage);
        bridge_checker = clipper_offset2(clip(clipped), -current_offset, current_offset).to_expolygon_collection();
    }

    return bridge_checker;
}

StoredExPolygonCollection build_top_fills(const PerimeterGenerationContextView &context,
                                          const PerimeterNodeView &parent,
                                          const RegionSettingsValue &values,
                                          const RegionSettingsClip &enabled_area,
                                          const ExPolygonCollection &current_polygons,
                                          StoredExPolygonCollection &non_top_polygons)
{
    storage_handle *storage = context.storage();
    Config config = region_config(context);
    const c_flow perimeter_flow = context.perimeter_flow();
    const c_flow ext_flow = external_perimeter_flow(context);
    const uint32_t inner_perimeter_count = parent.perimeter_needed() > 0 ? parent.perimeter_needed() - 1 : 0;

    const double max_perimeters_width = unscaled(double(ext_flow.width) + double(perimeter_flow.spacing) * double(inner_perimeter_count));
    coord_t offset_top_surface =
        scale_i(config.get(k_external_infill_margin_key).get_effective_value(inner_perimeter_count == 0 ? 0. : max_perimeters_width));
    if (offset_top_surface > 0.9 * (inner_perimeter_count <= 1 ? 0. : double(perimeter_flow.spacing) * double(inner_perimeter_count - 1)))
        offset_top_surface -= coord_t(0.9 * (inner_perimeter_count <= 1 ? 0. : double(perimeter_flow.spacing) * double(inner_perimeter_count - 1)));
    else
        offset_top_surface = 0;

    const double configured_min_width =
        values.get_effective_value(unscaled(perimeter_flow.width), k_min_width_top_surface_key);
    const coordf_t min_width_top_surface =
        std::max(coordf_t(double(ext_flow.spacing) / 2.0 + 10.0), scale_d(configured_min_width));

    StoredExPolygonCollection upper_slices = build_upper_slices_for_area(context, enabled_area);
    ClipperContext clip(storage);
    StoredExPolygonCollection grown_upper_slices(storage);
    if (!values.get_bool(k_only_one_perimeter_top_other_algo_key))
        grown_upper_slices = clipper_offset(clip(upper_slices), min_width_top_surface).to_expolygon_collection();
    else
        grown_upper_slices = clipper_offset2(clip(upper_slices), -double(offset_top_surface),
                                             double(offset_top_surface) + min_width_top_surface).to_expolygon_collection();

    StoredExPolygonCollection fill_clip =
        offset_collection(storage, current_polygons, -double(ext_flow.spacing));

    StoredExPolygonCollection orig_without_bridge = current_polygons.clone(storage);
    StoredExPolygonCollection lower_slices = lower_slice_coverage(storage, context.island());
    StoredExPolygonCollection bridge_checker =
        build_bridge_checker(context, config, current_polygons, lower_slices, inner_perimeter_count);
    if (!bridge_checker.empty())
        orig_without_bridge = diff_collection(storage, current_polygons, bridge_checker);

    StoredExPolygonCollection top_polygons =
        diff_collection(storage, orig_without_bridge, grown_upper_slices);
    StoredExPolygonCollection temp_gap = diff_collection(storage, top_polygons, fill_clip);
    StoredExPolygonCollection top_offset =
        offset_collection(storage, top_polygons,
                          double(offset_top_surface) + min_width_top_surface - double(ext_flow.spacing) / 2.0);
    StoredExPolygonCollection inner_polygons =
        diff_collection(storage, current_polygons, top_offset);

    top_polygons = diff_collection(storage, fill_clip, inner_polygons);

    StoredExPolygonCollection new_non_top_polygons =
        intersection_collection(storage, inner_polygons, current_polygons);
    if (config.get(k_gap_fill_enabled_key).get_bool())
        new_non_top_polygons = union_append(storage, std::move(new_non_top_polygons), std::move(temp_gap));

    non_top_polygons = union_append(storage, std::move(non_top_polygons), std::move(new_non_top_polygons));

    return top_polygons;
}

void set_children_to_one_perimeter(const PerimeterNodeView &parent)
{
    parent.set_perimeter_needed(1);
    const std::vector<PerimeterNodeView> children = parent.children_snapshot();
    for (const PerimeterNodeView &child : children)
        child.set_perimeter_needed(1);
}

std::vector<PerimeterNodeView> split_node_with_expolygons(perimeter_generation_context *context,
                                                          const PerimeterNodeView &node,
                                                          const ExPolygonCollection &clip)
{
    std::vector<PerimeterNodeView> inside_nodes;
    if (context == nullptr || context->split_node == nullptr || node.child_count() > 0)
        return inside_nodes;
    perimeter_node_span span = {};
    context->split_node(context, node.mutable_handle(), clip.handle(), &span);
    inside_nodes.reserve(span.count);
    for (uint32_t idx = 0; idx < span.count; ++idx)
        if (span.items != nullptr && span.items[idx] != nullptr)
            inside_nodes.push_back(PerimeterNodeView(span.items[idx]));
    return inside_nodes;
}

void set_top_children_to_one_perimeter(perimeter_generation_context *context,
                                       const PerimeterNodeView &parent,
                                       const ExPolygonCollection &top_fills)
{
    const std::vector<PerimeterNodeView> children = parent.children_snapshot();
    for (const PerimeterNodeView &child : children) {
        const std::vector<PerimeterNodeView> inside_nodes = split_node_with_expolygons(context, child, top_fills);
        if (inside_nodes.empty())
            continue;
        child.set_perimeter_needed(1);
        for (const PerimeterNodeView &inside_node : inside_nodes)
            inside_node.set_perimeter_needed(1);
    }
}

void set_enabled_children_to_one_perimeter(perimeter_generation_context *context,
                                           const PerimeterNodeView &parent,
                                           const RegionSettingsClip &enabled_area)
{
    if (enabled_area.is_accept_all()) {
        set_children_to_one_perimeter(parent);
        return;
    }

    const std::vector<PerimeterNodeView> children = parent.children_snapshot();
    for (const PerimeterNodeView &child : children) {
        const std::vector<PerimeterNodeView> inside_nodes =
            split_node_with_expolygons(context, child, enabled_area.expolygons());
        for (const PerimeterNodeView &inside_node : inside_nodes)
            inside_node.set_perimeter_needed(1);
    }
}

void *module_start(void *, perimeter_generation_context *context)
{
    if (context == nullptr || context->root == nullptr)
        return nullptr;

    PerimeterGenerationContextView context_view(context);
    if (context_view.island().region_count() == 0 || context_view.island().upper_island_count() > 0)
        return nullptr;

    RegionSettings settings = context_view.region_settings({{k_only_one_perimeter_top_key}});
    settings.segregate(context_view.island().slice());
    if (!settings.has_many_config(k_only_one_perimeter_top_key) &&
        settings.get_solo_config(k_only_one_perimeter_top_key).get_bool(k_only_one_perimeter_top_key))
        context_view.root().set_perimeter_needed(1);
    return nullptr;
}

void module_after(void *, void *, perimeter_generation_context *context, perimeter_node *node)
{
    if (context == nullptr || node == nullptr)
        return;

    PerimeterGenerationContextView context_view(context);
    if (context_view.island().region_count() == 0)
        return;

    PerimeterNodeView parent(node);
    if (parent.perimeter_idx() > 0 || parent.extrusions().empty() ||
        parent.perimeter_needed() == 0 || parent.child_count() == 0)
        return;

    RegionSettings settings = context_view.region_settings(
        {{k_only_one_perimeter_top_key, k_min_width_top_surface_key, k_only_one_perimeter_top_other_algo_key}});
    settings.segregate(context_view.island().slice());
    if (!settings.has_many_config(k_only_one_perimeter_top_key) &&
        !settings.get_solo_config(k_only_one_perimeter_top_key).get_bool(k_only_one_perimeter_top_key))
        return;

    const RegionSettings::AreaMap &areas = settings.get_areas(k_only_one_perimeter_top_key);

    if (context_view.island().upper_island_count() == 0) {
        for (const std::pair<const RegionSettingsValue, RegionSettingsClip> &entry : areas) {
            if (!entry.first.get_bool(k_only_one_perimeter_top_key))
                continue;
            set_enabled_children_to_one_perimeter(context, parent, entry.second);
        }
        return;
    }

    StoredExPolygonCollection top_fills(context_view.storage());
    StoredExPolygonCollection non_top_polygons(context_view.storage());
    for (const std::pair<const RegionSettingsValue, RegionSettingsClip> &entry : areas) {
        if (!entry.first.get_bool(k_only_one_perimeter_top_key))
            continue;

        StoredExPolygonCollection source_polygons =
            non_top_polygons.empty() ?
            collection_from_expolygon(context_view.storage(), parent.surface()) :
            non_top_polygons.readonly().clone(context_view.storage());
        StoredExPolygonCollection perimeter_centerline =
            offset_collection(context_view.storage(), source_polygons, -double(external_perimeter_flow(context_view).width) / 2.0);
        StoredExPolygonCollection current_top_fills =
            build_top_fills(context_view, parent, entry.first, entry.second, perimeter_centerline, non_top_polygons);
        top_fills = union_append(context_view.storage(), std::move(top_fills), std::move(current_top_fills));
    }

    if (!top_fills.empty())
        set_top_children_to_one_perimeter(context, parent, top_fills);
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

OnlyOnePerimeterOnTop &
OnlyOnePerimeterOnTop::instance(orchestrator_handle *orch)
{
    static OnlyOnePerimeterOnTop s_instance(orch);
    return s_instance;
}

const char *OnlyOnePerimeterOnTop::id_impl() const noexcept
{
    return k_only_one_perimeter_on_top_id;
}

slicing_step_t OnlyOnePerimeterOnTop::step_impl() const noexcept
{
    return PERIMETER_GENERATION_MODULE;
}

const char *const *OnlyOnePerimeterOnTop::dependencies_impl() const noexcept
{
    return k_no_dependencies;
}

int32_t OnlyOnePerimeterOnTop::priority_impl() const noexcept
{
    return 5;
}

int32_t OnlyOnePerimeterOnTop::used_config_keys(const char **keys) const noexcept
{
    if (keys != nullptr)
        for (uint32_t idx = 0; idx < sizeof(k_used_config_keys) / sizeof(k_used_config_keys[0]); ++idx)
            keys[idx] = k_used_config_keys[idx];
    return int32_t(sizeof(k_used_config_keys) / sizeof(k_used_config_keys[0]));
}

const char *OnlyOnePerimeterOnTop::progress_message_format_impl() const noexcept
{
    return "Only one perimeter on top: %u / %u";
}

void OnlyOnePerimeterOnTop::run_impl(const plugin_run_context *run_ctx) const
{
    run_ctx_perimeter_generation_module *ctx = plugin_ctx_as_perimeter_generation_module(run_ctx);
    if (ctx == nullptr)
        return;

    ctx->module.ctx = const_cast<OnlyOnePerimeterOnTop *>(this);
    ctx->module.vt = &module_vtable();
}

void register_only_one_perimeter_on_top_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, OnlyOnePerimeterOnTop::instance(orch).c_instance());
}

}}} // namespace slic3r_api::Perimeter::OnlyOnePerimeterOnTopPlugin

#ifdef ONLY_ONE_PERIMETER_ON_TOP_PLUGIN_DLL
extern "C" void register_plugin(orchestrator_handle *orch)
{
    slic3r_api::Perimeter::OnlyOnePerimeterOnTopPlugin::register_only_one_perimeter_on_top_plugin(orch);
}
#endif // ONLY_ONE_PERIMETER_ON_TOP_PLUGIN_DLL
