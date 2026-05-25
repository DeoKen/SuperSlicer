///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "SeparateHoleContour.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_perimeter.h"
#include "libslic3r/Api/plugin/cpp/ClipperViews.hpp"
#include "libslic3r/Api/plugin/cpp/PerimeterStepViews.hpp"

namespace slic3r_api { namespace Perimeter { namespace SeparateHoleContourPlugin {

namespace {

const char *k_separate_hole_contour_id = "perimeter.module.separate_hole_contour";
const char *k_no_dependencies[] = { nullptr };
const char *k_used_config_keys[] = { "perimeters_hole", "perimeters" };
const char *k_perimeters_hole_key = "perimeters_hole";
const char *k_perimeters_key = "perimeters";

// Same bit value as Slic3r::ExtrusionLoopRole::elrHole. The perimeter property
// is a C payload, so the module only needs the ABI bit, not the C++ enum type.
const int32_t k_perimeter_loop_role_hole = 1 << 3;

struct HoleContourCount
{
    int32_t max_hole_count = 0;
    int32_t max_contour_count = 0;
    int32_t hole_deleted = 0;
    int32_t contour_deleted = 0;
};

class ModuleState
{
public:
    bool get(const perimeter_node *node, HoleContourCount &out) const
    {
        const std::map<const perimeter_node *, HoleContourCount>::const_iterator it = counts.find(node);
        if (it == counts.end())
            return false;

        out = it->second;
        return true;
    }

    void set(const perimeter_node *node, const HoleContourCount &count)
    {
        counts[node] = count;
    }

private:
    std::map<const perimeter_node *, HoleContourCount> counts;
};

uint32_t count_from_config(int32_t value)
{
    return value <= 0 ? 0 : uint32_t(value);
}

bool extrusion_is_hole_perimeter(const ExtrusionEntity &entity)
{
    const EPropertyPerimeter *perimeter = entity.property<EPropertyPerimeter>();
    return perimeter != nullptr && (perimeter->perimeter_role() & k_perimeter_loop_role_hole) != 0;
}

bool extrusion_is_perimeter_loop_candidate(const ExtrusionEntity &entity)
{
    return !entity.empty() && entity.is_closed();
}

size_t erase_perimeter_class(MutableExtrusionEntity extrusions, bool erase_holes)
{
    size_t erased_count = 0;
    for (uint32_t idx = extrusions.child_count(); idx > 0; --idx) {
        const uint32_t child_idx = idx - 1;
        const ExtrusionEntity child = extrusions.child(child_idx);
        if (!extrusion_is_perimeter_loop_candidate(child))
            continue;
        if (extrusion_is_hole_perimeter(child) != erase_holes)
            continue;

        if (extrusions.remove_child(child_idx))
            ++erased_count;
    }
    return erased_count;
}

double erase_cleanup_distance(const PerimeterGenerationContextView &context,
                              const PerimeterNodeView &node)
{
    (void) node;
    const c_flow flow = context.perimeter_flow();
    return std::max<double>(double(SCALED_EPSILON), 0.1 * double(flow.spacing));
}

void append_closed_entity_polygons(storage_handle *storage,
                                   StoredPolygonCollection &polygons,
                                   const ExtrusionEntity &entity)
{
    if (entity.has_polyline() && entity.local_is_closed()) {
        std::vector<c_point> points = entity.points();
        if (points.size() > 1 && points_equal(points.front(), points.back()))
            points.pop_back();

        if (points.size() >= 3) {
            StoredPolygon polygon(storage);
            polygon.insert_array(0, points.data(), uint32_t(points.size()));
            if (polygon.valid_polygon())
                polygons.push_back(polygon.readonly());
        }
    }

    for (uint32_t child_idx = 0; child_idx < entity.child_count(); ++child_idx)
        append_closed_entity_polygons(storage, polygons, entity.child(child_idx));
}

StoredExPolygonCollection collection_from_expolygon(storage_handle *storage,
                                                    const ExPolygon &expolygon)
{
    StoredExPolygonCollection collection(storage);
    collection.push_back(expolygon);
    return collection;
}

StoredExPolygonCollection offset_collection(storage_handle *storage,
                                            const ExPolygonCollection &subject,
                                            double delta)
{
    if (subject.empty())
        return StoredExPolygonCollection(storage);

    ClipperContext clip(storage);
    return clipper_offset(clip(subject), delta).to_expolygon_collection();
}

StoredExPolygonCollection offset2_collection(storage_handle *storage,
                                             const ExPolygonCollection &subject,
                                             double delta1,
                                             double delta2)
{
    if (subject.empty())
        return StoredExPolygonCollection(storage);

    ClipperContext clip(storage);
    return clipper_offset2(clip(subject), delta1, delta2).to_expolygon_collection();
}

StoredExPolygonCollection diff_collection(storage_handle *storage,
                                          const ExPolygonCollection &subject,
                                          const ExPolygonCollection &clip_area)
{
    if (subject.empty())
        return StoredExPolygonCollection(storage);
    if (clip_area.empty())
        return subject.clone(storage);

    ClipperContext clip(storage);
    return clipper_diff(clip(subject), clip(clip_area)).to_expolygon_collection();
}

StoredExPolygonCollection extrusion_coverage_area(storage_handle *storage,
                                                  const ExtrusionEntity &extrusions,
                                                  double cleanup_distance)
{
    StoredPolygonCollection polygons(storage);
    append_closed_entity_polygons(storage, polygons, extrusions);
    if (polygons.empty())
        return StoredExPolygonCollection(storage);

    ClipperContext clip(storage);
    StoredExPolygonCollection area =
        clipper_union(clipper_offset(clip(polygons), cleanup_distance)).to_expolygon_collection();
    if (!area.empty())
        area = offset2_collection(storage, area.readonly(), cleanup_distance, -cleanup_distance);
    return area;
}

StoredExPolygonCollection build_child_areas(storage_handle *storage,
                                            const PerimeterNodeView &parent,
                                            const ExPolygonCollection &kept_extrusion_area,
                                            double cleanup_distance)
{
    StoredExPolygonCollection parent_area = collection_from_expolygon(storage, parent.area());
    StoredExPolygonCollection child_areas = kept_extrusion_area.empty() ?
        parent_area.readonly().clone(storage) :
        diff_collection(storage, parent_area, kept_extrusion_area);

    if (!child_areas.empty())
        child_areas = offset2_collection(storage, child_areas.readonly(), -cleanup_distance, cleanup_distance);
    return child_areas;
}

StoredExPolygonCollection build_fill_areas(storage_handle *storage,
                                           const PerimeterNodeView &parent,
                                           const ExPolygonCollection &kept_extrusion_area,
                                           double cleanup_distance)
{
    StoredExPolygonCollection base_fill_area = collection_from_expolygon(storage, parent.fill_area());
    StoredExPolygonCollection fill_areas = kept_extrusion_area.empty() ?
        base_fill_area.readonly().clone(storage) :
        diff_collection(storage, base_fill_area, kept_extrusion_area);

    if (!fill_areas.empty())
        fill_areas = offset2_collection(storage, fill_areas.readonly(), -cleanup_distance, cleanup_distance);
    return fill_areas;
}

void store_for_children(ModuleState &state,
                        const PerimeterNodeView &parent,
                        const HoleContourCount &data)
{
    const std::vector<PerimeterNodeView> children = parent.children_snapshot();
    for (const PerimeterNodeView &child : children)
        state.set(child.handle(), data);
}

void set_if_child_needs_one_less_perimeter(const PerimeterNodeView &child)
{
    if (child.perimeter_needed() > child.perimeter_idx())
        child.set_perimeter_needed(child.perimeter_needed() - 1);
}

void *module_start(void *, perimeter_generation_context *context)
{
    ModuleState *state = new ModuleState();
    if (context == nullptr || context->root == nullptr)
        return state;

    PerimeterGenerationContextView context_view(context);
    if (context_view.island().region_count() == 0)
        return state;

    RegionSettings settings = context_view.region_settings({{k_perimeters_hole_key, k_perimeters_key}});
    settings.segregate(context_view.island().slice());

    // Region-varying perimeter counts need node splitting by the active areas.
    // The old in-core implementation only supported one value pair per island
    // here, so keep that restriction until the generator tree is complete.
    if (settings.has_many_config(k_perimeters_hole_key))
        return state;

    const RegionSettingsValue &values = settings.get_solo_config(k_perimeters_hole_key);
    if (!values.is_enabled(k_perimeters_hole_key))
        return state;

    HoleContourCount data;
    data.max_hole_count = values.get_int(k_perimeters_hole_key);
    data.max_contour_count = values.get_int(k_perimeters_key);

    const uint32_t requested_perimeter_count =
        std::max(count_from_config(data.max_hole_count), count_from_config(data.max_contour_count));
    if (requested_perimeter_count > context_view.root().perimeter_needed())
        context_view.root().set_perimeter_needed(requested_perimeter_count);

    if (data.max_hole_count != data.max_contour_count)
        state->set(context->root, data);
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

    HoleContourCount data;
    if (!state->get(node, data))
        return;

    PerimeterNodeView parent(node);
    const int32_t perimeter_idx = int32_t(parent.perimeter_idx());
    const int32_t perimeter_needed = int32_t(parent.perimeter_needed());

    bool need_erase_holes = data.max_hole_count == 0;
    bool need_erase_contour = data.max_contour_count == 0;
    const int32_t diff_contour_hole = data.max_contour_count - data.max_hole_count;

    if (!need_erase_contour && diff_contour_hole < 0) {
        const int32_t contour_needed = perimeter_needed + diff_contour_hole;
        if (perimeter_idx >= contour_needed && data.contour_deleted < -diff_contour_hole)
            need_erase_contour = true;
    }

    if (!need_erase_holes && diff_contour_hole > 0) {
        const int32_t holes_needed = perimeter_needed - diff_contour_hole;
        if (perimeter_idx >= holes_needed && data.hole_deleted < diff_contour_hole)
            need_erase_holes = true;
    }

    if (!need_erase_holes && !need_erase_contour) {
        store_for_children(*state, parent, data);
        state->set(node, data);
        return;
    }

    MutableExtrusionEntity extrusions = parent.extrusions();
    if (need_erase_contour && need_erase_holes) {
        const size_t erased_holes = erase_perimeter_class(extrusions, true);
        const size_t erased_contours = erase_perimeter_class(extrusions, false);
        if (erased_contours > 0)
            ++data.contour_deleted;
        if (erased_holes > 0)
            ++data.hole_deleted;

        bool child_needs_more_perimeters = false;
        const std::vector<PerimeterNodeView> children = parent.children_snapshot();
        for (const PerimeterNodeView &child : children)
            child_needs_more_perimeters |= child.needs_more_perimeters();

        if (parent.is_last_perimeter() && !child_needs_more_perimeters) {
            if (parent.perimeter_needed() > 0)
                parent.set_perimeter_needed(parent.perimeter_needed() - 1);
        } else {
            for (const PerimeterNodeView &child : children) {
                set_if_child_needs_one_less_perimeter(child);
                state->set(child.handle(), data);
            }
        }

        state->set(node, data);
        return;
    }

    const size_t erased_count = erase_perimeter_class(extrusions, need_erase_holes);
    if (erased_count == 0) {
        state->set(node, data);
        return;
    }

    if (need_erase_contour)
        ++data.contour_deleted;
    if (need_erase_holes)
        ++data.hole_deleted;

    const double cleanup_distance = erase_cleanup_distance(context_view, parent);
    StoredExPolygonCollection kept_extrusion_area =
        extrusion_coverage_area(context_view.storage(), extrusions.readonly(), cleanup_distance);
    StoredExPolygonCollection child_areas =
        build_child_areas(context_view.storage(), parent, kept_extrusion_area, cleanup_distance);
    StoredExPolygonCollection fill_areas =
        build_fill_areas(context_view.storage(), parent, kept_extrusion_area, cleanup_distance);

    if (context_view.rebuild_children(parent, child_areas, fill_areas))
        store_for_children(*state, parent, data);

    state->set(node, data);
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

SeparateHoleContour &
SeparateHoleContour::instance(orchestrator_handle *orch)
{
    static SeparateHoleContour s_instance(orch);
    return s_instance;
}

const char *SeparateHoleContour::id_impl() const noexcept
{
    return k_separate_hole_contour_id;
}

slicing_step_t SeparateHoleContour::step_impl() const noexcept
{
    return PERIMETER_GENERATION_MODULE;
}

const char *const *SeparateHoleContour::dependencies_impl() const noexcept
{
    return k_no_dependencies;
}

int32_t SeparateHoleContour::priority_impl() const noexcept
{
    return 6;
}

int32_t SeparateHoleContour::used_config_keys(const char **keys) const noexcept
{
    if (keys != nullptr)
        for (uint32_t idx = 0; idx < sizeof(k_used_config_keys) / sizeof(k_used_config_keys[0]); ++idx)
            keys[idx] = k_used_config_keys[idx];
    return int32_t(sizeof(k_used_config_keys) / sizeof(k_used_config_keys[0]));
}

const char *SeparateHoleContour::progress_message_format_impl() const noexcept
{
    return "Separate hole/contour perimeters: %u / %u";
}

void SeparateHoleContour::run_impl(const plugin_run_context *run_ctx) const
{
    run_ctx_perimeter_generation_module *ctx = plugin_ctx_as_perimeter_generation_module(run_ctx);
    if (ctx == nullptr)
        return;

    ctx->module.ctx = const_cast<SeparateHoleContour *>(this);
    ctx->module.vt = &module_vtable();
}

void register_separate_hole_contour_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, SeparateHoleContour::instance(orch).c_instance());
}

}}} // namespace slic3r_api::Perimeter::SeparateHoleContourPlugin

#ifdef SEPARATE_HOLE_CONTOUR_PLUGIN_DLL
extern "C" void register_plugin(orchestrator_handle *orch)
{
    slic3r_api::Perimeter::SeparateHoleContourPlugin::register_separate_hole_contour_plugin(orch);
}
#endif // SEPARATE_HOLE_CONTOUR_PLUGIN_DLL
