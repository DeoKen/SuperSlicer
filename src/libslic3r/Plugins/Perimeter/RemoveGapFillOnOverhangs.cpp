///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "RemoveGapFillOnOverhangs.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_perimeter.h"
#include "libslic3r/Api/plugin/cpp/ClipperViews.hpp"
#include "libslic3r/Api/plugin/cpp/PerimeterStepViews.hpp"

namespace slic3r_api { namespace Perimeter { namespace RemoveGapFillOnOverhangsPlugin {

namespace {

const char *k_remove_gap_fill_on_overhangs_id = "perimeter.module.remove_gap_fill_on_overhangs";
const char *k_no_dependencies[] = { nullptr };
const char *k_used_config_keys[] = { "gap_fill_no_overhang" };
const char *k_gap_fill_no_overhang_key = "gap_fill_no_overhang";

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

StoredExPolygonCollection node_surface_collection(storage_handle *storage, const PerimeterNodeView &node)
{
    StoredExPolygonCollection surface(storage);
    surface.push_back(node.surface());
    return surface;
}

StoredExPolygonCollection gap_fill_no_overhang_area(const PerimeterGenerationContextView &context,
                                                    const PerimeterNodeView &node,
                                                    const RegionSettings &settings)
{
    storage_handle *storage = context.storage();
    StoredExPolygonCollection forbidden_area(storage);
    StoredExPolygonCollection node_surface = node_surface_collection(storage, node);
    StoredExPolygonCollection lower_slices = lower_slice_coverage(storage, context.island());
    const RegionSettings::AreaMap &areas = settings.get_areas(k_gap_fill_no_overhang_key);

    for (const std::pair<const RegionSettingsValue, RegionSettingsClip> &entry : areas) {
        if (!entry.first.get_bool(k_gap_fill_no_overhang_key))
            continue;

        StoredExPolygonCollection enabled_area = entry.second.is_accept_all() ?
            node_surface.readonly().clone(storage) :
            entry.second.intersections(node_surface);
        if (enabled_area.empty())
            continue;

        StoredExPolygonCollection unsupported_area(storage);
        if (lower_slices.empty())
            unsupported_area.move_from(std::move(enabled_area));
        else {
            ClipperContext clip(storage);
            unsupported_area =
                clipper_diff(clip(enabled_area), clip(lower_slices)).to_expolygon_collection();
        }

        if (!unsupported_area.empty())
            forbidden_area.append_move_from(std::move(unsupported_area));
    }

    if (!forbidden_area.empty()) {
        ClipperContext clip(storage);
        forbidden_area = clipper_union(clip(forbidden_area)).to_expolygon_collection();
        forbidden_area.ensure_valid();
    }
    return forbidden_area;
}

void append_entity_without_overhang_gap_fill(storage_handle *storage,
                                             StoredExtrusionEntity &dst,
                                             const ExtrusionEntity &entity,
                                             const ExPolygonCollection &forbidden_area)
{
    if (!entity.has_polyline() || entity.local_is_closed() || entity.point_count() < 2) {
        dst.add_child(entity);
        return;
    }

    // Gap-fill clipping works on plain open polylines. The cloned entity keeps
    // its properties, then receives the clipped linear fragment. If gap fill
    // later starts carrying arc data, this needs an ArcPolyline-aware clipper.
    StoredPolyline source_polyline(storage);
    const std::vector<c_point> points = entity.points();
    source_polyline.insert_array(0, points.data(), static_cast<uint32_t>(points.size()));
    StoredPolylineCollection fragments =
        clipper_diff_polyline_expolygons(storage, source_polyline, forbidden_area);

    for (const Polyline fragment : fragments) {
        if (fragment.size() < 2)
            continue;

        StoredExtrusionEntity clipped_entity(storage, entity);
        clipped_entity.set(fragment);
        if (!clipped_entity.empty())
            dst.add_child(clipped_entity.mutable_view());
    }
}

void remove_gap_fill_on_overhangs(storage_handle *storage,
                                  MutableExtrusionEntity extrusions,
                                  const ExPolygonCollection &forbidden_area)
{
    if (forbidden_area.empty() || extrusions.empty() || extrusions.child_count() == 0)
        return;

    StoredExtrusionEntity clipped_extrusions(storage, extrusions.readonly());
    clipped_extrusions.clear_content();

    const uint32_t child_count = extrusions.child_count();
    for (uint32_t child_idx = 0; child_idx < child_count; ++child_idx)
        append_entity_without_overhang_gap_fill(storage, clipped_extrusions, extrusions.child(child_idx), forbidden_area);

    const bool moved = extrusion_move_from(extrusions.mutable_handle(), clipped_extrusions.mutable_handle()) != 0;
    assert(moved);
    (void) moved;
}

void module_after(void *, void *, perimeter_generation_context *context, perimeter_node *node)
{
    if (context == nullptr || node == nullptr)
        return;

    PerimeterGenerationContextView context_view(context);
    if (context_view.island().region_count() == 0)
        return;

    PerimeterNodeView parent(node);
    if (parent.extrusions().child_count() == 0)
        return;

    RegionSettings settings = context_view.region_settings({{k_gap_fill_no_overhang_key}});
    settings.segregate(parent.surface());
    if (!settings.has_many_config(k_gap_fill_no_overhang_key) &&
        !settings.get_solo_config(k_gap_fill_no_overhang_key).get_bool(k_gap_fill_no_overhang_key))
        return;

    StoredExPolygonCollection forbidden_area =
        gap_fill_no_overhang_area(context_view, parent, settings);
    remove_gap_fill_on_overhangs(context_view.storage(), parent.extrusions(), forbidden_area);
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

RemoveGapFillOnOverhangs &
RemoveGapFillOnOverhangs::instance(orchestrator_handle *orch)
{
    static RemoveGapFillOnOverhangs s_instance(orch);
    return s_instance;
}

const char *RemoveGapFillOnOverhangs::id_impl() const noexcept
{
    return k_remove_gap_fill_on_overhangs_id;
}

slicing_step_t RemoveGapFillOnOverhangs::step_impl() const noexcept
{
    return PERIMETER_GENERATION_MODULE;
}

const char *const *RemoveGapFillOnOverhangs::dependencies_impl() const noexcept
{
    return k_no_dependencies;
}

int32_t RemoveGapFillOnOverhangs::priority_impl() const noexcept
{
    return 10;
}

int32_t RemoveGapFillOnOverhangs::used_config_keys(const char **keys) const noexcept
{
    if (keys != nullptr)
        keys[0] = k_used_config_keys[0];
    return 1;
}

const char *RemoveGapFillOnOverhangs::progress_message_format_impl() const noexcept
{
    return "Remove gap fill on overhangs: %u / %u";
}

void RemoveGapFillOnOverhangs::run_impl(const plugin_run_context *run_ctx) const
{
    run_ctx_perimeter_generation_module *ctx = plugin_ctx_as_perimeter_generation_module(run_ctx);
    if (ctx == nullptr)
        return;

    ctx->module.ctx = const_cast<RemoveGapFillOnOverhangs *>(this);
    ctx->module.vt = &module_vtable();
}

void register_remove_gap_fill_on_overhangs_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, RemoveGapFillOnOverhangs::instance(orch).c_instance());
}

}}} // namespace slic3r_api::Perimeter::RemoveGapFillOnOverhangsPlugin

#ifdef REMOVE_GAP_FILL_ON_OVERHANGS_PLUGIN_DLL
extern "C" void register_plugin(orchestrator_handle *orch)
{
    slic3r_api::Perimeter::RemoveGapFillOnOverhangsPlugin::register_remove_gap_fill_on_overhangs_plugin(orch);
}
#endif // REMOVE_GAP_FILL_ON_OVERHANGS_PLUGIN_DLL
