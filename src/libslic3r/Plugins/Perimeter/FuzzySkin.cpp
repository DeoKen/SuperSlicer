///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "FuzzySkin.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <utility>
#include <vector>

#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_post_perimeter.h"
#include "libslic3r/Api/plugin/cpp/ClipperViews.hpp"
#include "libslic3r/Api/plugin/cpp/DataTreeViews.hpp"
#include "libslic3r/Api/plugin/cpp/ExtrusionViews.hpp"
#include "libslic3r/Api/plugin/cpp/RegionSettingsViews.hpp"

namespace slic3r_api { namespace Perimeter { namespace FuzzySkinPlugin {

namespace {

const char *k_fuzzy_skin_id = "perimeter.post_process.fuzzy_skin";
const char *k_no_dependencies[] = { nullptr };
const char *k_used_config_keys[] = { "fuzzy_skin", "fuzzy_skin_thickness", "fuzzy_skin_point_dist" };
const char *k_fuzzy_skin_key = "fuzzy_skin";
const char *k_fuzzy_skin_thickness_key = "fuzzy_skin_thickness";
const char *k_fuzzy_skin_point_dist_key = "fuzzy_skin_point_dist";

// These values match the public FFF fuzzy_skin enum option order. Keeping them
// local avoids including host print-config classes from this plugin algorithm.
constexpr int32_t k_fuzzy_none     = 0;
constexpr int32_t k_fuzzy_external = 1;
constexpr int32_t k_fuzzy_shell    = 2;
constexpr int32_t k_fuzzy_all      = 3;

constexpr uint16_t k_loop_role_hole    = 1u << 3;

struct FuzzyParameters
{
    int32_t mode = k_fuzzy_none;
    coordf_t thickness = 0.;
    coordf_t point_distance = 0.;

    bool can_fuzz_perimeters() const
    {
        return mode != k_fuzzy_none && thickness > 0. && point_distance > 0.;
    }

    bool can_fuzz_gap_fill() const
    {
        return mode == k_fuzzy_all && thickness > 0. && point_distance > 0.;
    }
};

struct InheritedExtrusionState
{
    bool has_perimeter = false;
    EPropertyPerimeter perimeter = {};
};

struct FuzzyTarget
{
    MutableExtrusionEntity entity;
    FuzzyParameters params;
};

struct SplitFragment
{
    SplitFragment(storage_handle *storage,
                  const ExtrusionEntity &source,
                  const Polyline &polyline,
                  const FuzzyParameters &params_in,
                  double order_in,
                  bool fuzzify_in) :
        entity(storage, source),
        params(params_in),
        order(order_in),
        fuzzify(fuzzify_in)
    {
        entity.set(polyline);
    }

    StoredExtrusionEntity entity;
    FuzzyParameters params;
    double order = 0.;
    bool fuzzify = false;
};

FuzzyParameters fuzzy_parameters_from_value(const RegionSettingsValue &value, double nozzle_diameter)
{
    FuzzyParameters out;
    out.mode = value.get_int(k_fuzzy_skin_key);
    out.thickness = scale_d(value.get_effective_value(nozzle_diameter, k_fuzzy_skin_thickness_key));
    out.point_distance = scale_d(value.get_effective_value(nozzle_diameter, k_fuzzy_skin_point_dist_key));
    return out;
}

double nozzle_diameter_for_fuzzy_skin(const LayerIsland &island)
{
    if (island.region_count() == 0)
        return 0.;
    const c_flow flow = island.region(0).flow(RAW_EXTRUSION_ROLE_EXTERNAL_PERIMETER);
    return unscaled(flow.nozzle_diameter);
}

InheritedExtrusionState state_with_entity_properties(const InheritedExtrusionState &parent_state,
                                                     const ExtrusionEntity &entity)
{
    // Extrusion properties may be stored on an ancestor collection and inherited
    // by leaf paths. Carry the current perimeter metadata down the tree so a
    // split child is still classified as external/shell/hole correctly.
    InheritedExtrusionState state = parent_state;
    const EPropertyPerimeter *perimeter = entity.property<EPropertyPerimeter>();
    if (perimeter != nullptr) {
        state.has_perimeter = true;
        state.perimeter = *perimeter;
    }

    return state;
}

bool perimeter_state_is_hole(const InheritedExtrusionState &state)
{
    return state.has_perimeter && (state.perimeter.perimeter_role() & k_loop_role_hole) != 0;
}

bool perimeter_state_is_shell(const InheritedExtrusionState &state)
{
    return state.has_perimeter && state.perimeter.shell_count() == 0;
}

bool should_fuzzify_perimeter(const FuzzyParameters &params, const InheritedExtrusionState &state)
{
    if (!params.can_fuzz_perimeters())
        return false;
    if (params.mode == k_fuzzy_all)
        return true;
    if (!perimeter_state_is_shell(state))
        return false;
    if (params.mode == k_fuzzy_shell)
        return true;
    if (params.mode == k_fuzzy_external)
        return !perimeter_state_is_hole(state);
    return false;
}

bool should_fuzzify_for_role(raw_extrusion_role role,
                             const FuzzyParameters &params,
                             const InheritedExtrusionState &state)
{
    if (role == RAW_EXTRUSION_ROLE_GAP_FILL)
        return params.can_fuzz_gap_fill();
    return should_fuzzify_perimeter(params, state);
}

bool any_area_can_fuzzify_role(const RegionSettings::AreaMap &areas, raw_extrusion_role role, double nozzle_diameter)
{
    for (const std::pair<const RegionSettingsValue, RegionSettingsClip> &entry : areas) {
        const FuzzyParameters params = fuzzy_parameters_from_value(entry.first, nozzle_diameter);
        if (role == RAW_EXTRUSION_ROLE_GAP_FILL ? params.can_fuzz_gap_fill() : params.can_fuzz_perimeters())
            return true;
    }
    return false;
}

StoredPolyline polyline_from_points(storage_handle *storage, const std::vector<c_point> &points)
{
    StoredPolyline polyline(storage);
    if (!points.empty())
        polyline.insert_array(0, points.data(), static_cast<uint32_t>(points.size()));
    return polyline;
}

double squared_distance_to_projection(c_point point,
                                      c_point segment_start,
                                      c_point segment_end,
                                      double &projection_ratio)
{
    const double vx = double(segment_end.x) - double(segment_start.x);
    const double vy = double(segment_end.y) - double(segment_start.y);
    const double wx = double(point.x) - double(segment_start.x);
    const double wy = double(point.y) - double(segment_start.y);
    const double segment_length_sq = vx * vx + vy * vy;
    if (segment_length_sq <= 0.) {
        projection_ratio = 0.;
        const double dx = double(point.x) - double(segment_start.x);
        const double dy = double(point.y) - double(segment_start.y);
        return dx * dx + dy * dy;
    }

    projection_ratio = (wx * vx + wy * vy) / segment_length_sq;
    if (projection_ratio < 0.)
        projection_ratio = 0.;
    else if (projection_ratio > 1.)
        projection_ratio = 1.;

    const double px = double(segment_start.x) + vx * projection_ratio;
    const double py = double(segment_start.y) + vy * projection_ratio;
    const double dx = double(point.x) - px;
    const double dy = double(point.y) - py;
    return dx * dx + dy * dy;
}

double distance_along_points(const std::vector<c_point> &source, c_point point)
{
    // Clipper returns fragments in geometric order most of the time, but not as
    // a documented guarantee. Sort fragments by their first point projected on
    // the original polyline so replacement children keep the extrusion order.
    double best_distance_sq = std::numeric_limits<double>::max();
    double best_distance = 0.;
    double accumulated = 0.;

    for (size_t idx = 1; idx < source.size(); ++idx) {
        double projection_ratio = 0.;
        const double distance_sq =
            squared_distance_to_projection(point, source[idx - 1], source[idx], projection_ratio);
        const double segment_length = norm(source[idx] - source[idx - 1]);
        if (distance_sq < best_distance_sq) {
            best_distance_sq = distance_sq;
            best_distance = accumulated + projection_ratio * segment_length;
        }
        accumulated += segment_length;
    }

    return best_distance;
}

StoredExPolygonCollection union_explicit_clips(storage_handle *storage, const RegionSettings::AreaMap &areas)
{
    StoredExPolygonCollection accepted_area(storage);
    for (const std::pair<const RegionSettingsValue, RegionSettingsClip> &entry : areas) {
        if (!entry.second.is_accept_all() && !entry.second.has_explicit_empty_geometry())
            accepted_area.append_copy_from(entry.second.expolygons());
    }

    if (!accepted_area.empty()) {
        ClipperContext clip(storage);
        accepted_area = clipper_union(clip(accepted_area)).to_expolygon_collection();
        accepted_area.ensure_valid();
    }
    return accepted_area;
}

void append_intersection_fragments(storage_handle *storage,
                                   const MutableExtrusionEntity &source_entity,
                                   const StoredPolyline &source_polyline,
                                   const std::vector<c_point> &source_points,
                                   const RegionSettingsClip &clip,
                                   const FuzzyParameters &params,
                                   raw_extrusion_role role,
                                   const InheritedExtrusionState &state,
                                   std::vector<SplitFragment> &fragments)
{
    StoredPolylineCollection clipped_polylines =
        clipper_intersection_polyline_expolygons(storage, source_polyline, clip.expolygons());
    for (const Polyline clipped_polyline : clipped_polylines) {
        if (clipped_polyline.size() < 2)
            continue;

        const double order = distance_along_points(source_points, clipped_polyline.front());
        const bool fuzzify = should_fuzzify_for_role(role, params, state);
        fragments.emplace_back(storage, source_entity.readonly(), clipped_polyline, params, order, fuzzify);
    }
}

void append_remainder_fragments(storage_handle *storage,
                                const MutableExtrusionEntity &source_entity,
                                const StoredPolyline &source_polyline,
                                const std::vector<c_point> &source_points,
                                const StoredExPolygonCollection &accepted_area,
                                std::vector<SplitFragment> &fragments)
{
    // RegionSettings should normally cover the whole island. Keeping the
    // remainder as a non-fuzzy fragment makes the splitter robust if a future
    // modifier creates holes in that coverage.
    StoredPolylineCollection remainders = accepted_area.empty() ?
        StoredPolylineCollection(storage) :
        clipper_diff_polyline_expolygons(storage, source_polyline, accepted_area.readonly());

    if (accepted_area.empty()) {
        StoredPolyline full_source = polyline_from_points(storage, source_points);
        remainders.push_back(full_source);
    }

    FuzzyParameters disabled_params;
    for (const Polyline remainder : remainders) {
        if (remainder.size() < 2)
            continue;
        const double order = distance_along_points(source_points, remainder.front());
        fragments.emplace_back(storage, source_entity.readonly(), remainder, disabled_params, order, false);
    }
}

std::vector<SplitFragment> split_leaf_by_region_settings(storage_handle *storage,
                                                         MutableExtrusionEntity entity,
                                                         const RegionSettings::AreaMap &areas,
                                                         double nozzle_diameter,
                                                         raw_extrusion_role role,
                                                         const InheritedExtrusionState &state)
{
    // RegionSettings gives polygon clips for each distinct fuzzy setting tuple.
    // Intersect the source polyline with every clip, keep the outside remainder
    // as printable non-fuzzy fragments, then sort all pieces back in path order.
    std::vector<SplitFragment> fragments;
    const std::vector<c_point> source_points = entity.points();
    if (source_points.size() < 2)
        return fragments;

    StoredPolyline source_polyline = polyline_from_points(storage, source_points);
    bool accept_all = false;
    for (const std::pair<const RegionSettingsValue, RegionSettingsClip> &entry : areas) {
        const FuzzyParameters params = fuzzy_parameters_from_value(entry.first, nozzle_diameter);
        if (entry.second.is_accept_all()) {
            accept_all = true;
            fragments.emplace_back(storage, entity.readonly(), source_polyline, params, 0.,
                                   should_fuzzify_for_role(role, params, state));
            break;
        }
        if (!entry.second.has_explicit_empty_geometry())
            append_intersection_fragments(storage, entity, source_polyline, source_points, entry.second,
                                          params, role, state, fragments);
    }

    if (!accept_all) {
        StoredExPolygonCollection accepted_area = union_explicit_clips(storage, areas);
        append_remainder_fragments(storage, entity, source_polyline, source_points, accepted_area, fragments);
    }

    std::sort(fragments.begin(), fragments.end(),
              [](const SplitFragment &lhs, const SplitFragment &rhs) { return lhs.order < rhs.order; });
    return fragments;
}

bool fragments_need_replacement(const std::vector<SplitFragment> &fragments)
{
    for (const SplitFragment &fragment : fragments)
        if (fragment.fuzzify)
            return true;
    return false;
}

void collect_fuzzy_targets_from_fragments(MutableExtrusionEntity owner,
                                          const std::vector<SplitFragment> &fragments,
                                          std::vector<FuzzyTarget> &targets)
{
    // New child handles are only stable after all children have been inserted.
    // Store the handles after replacement, then fuzz them in a separate pass.
    assert(owner.child_count() == fragments.size());
    for (uint32_t idx = 0; idx < owner.child_count() && idx < fragments.size(); ++idx)
        if (fragments[idx].fuzzify)
            targets.push_back({ owner.child_mutable(idx), fragments[idx].params });
}

uint32_t replace_child_with_fragments(MutableExtrusionEntity parent,
                                      uint32_t child_idx,
                                      MutableExtrusionEntity child,
                                      std::vector<SplitFragment> &fragments,
                                      std::vector<FuzzyTarget> &targets)
{
    if (fragments.empty())
        return child_idx + 1;

    if (fragments.size() == 1) {
        const bool moved = extrusion_move_from(child.mutable_handle(), fragments.front().entity.mutable_handle()) != 0;
        assert(moved);
        (void) moved;
        if (fragments.front().fuzzify)
            targets.push_back({ child, fragments.front().params });
        return child_idx + 1;
    }

    if ((parent.flags() & RAW_EXTRUSION_FLAG_SORTABLE) != 0) {
        // Sortable parents may reorder children. A split fuzzy path must stay
        // in its original sequence, so replace the leaf by a non-sortable
        // collection containing the ordered fragments.
        const bool was_reversible = (child.flags() & RAW_EXTRUSION_FLAG_REVERSIBLE) != 0;
        child.clear_content();
        child.set_flags(was_reversible ? RAW_EXTRUSION_FLAG_REVERSIBLE : 0);
        for (SplitFragment &fragment : fragments)
            child.add_child(fragment.entity.mutable_view());
        collect_fuzzy_targets_from_fragments(child, fragments, targets);
        return child_idx + 1;
    }

    // Non-sortable parents already preserve child order. In that case inserting
    // the fragments as siblings keeps the tree shallower and mirrors how a
    // continuous loop stores ordered path pieces.
    const bool removed = parent.remove_child(child_idx);
    assert(removed);
    (void) removed;
    for (uint32_t offset = 0; offset < fragments.size(); ++offset) {
        const uint32_t inserted_idx = parent.insert_child_move(child_idx + offset, fragments[offset].entity.mutable_view());
        assert(!is_invalid_index(inserted_idx));
        if (!is_invalid_index(inserted_idx) && fragments[offset].fuzzify)
            targets.push_back({ parent.child_mutable(inserted_idx), fragments[offset].params });
    }
    return child_idx + static_cast<uint32_t>(fragments.size());
}

void replace_root_leaf_with_fragments(MutableExtrusionEntity root,
                                      std::vector<SplitFragment> &fragments,
                                      std::vector<FuzzyTarget> &targets)
{
    // A root leaf has no parent where sibling fragments could be inserted. Turn
    // it into a non-sortable collection so the fragment order remains explicit.
    if (fragments.empty())
        return;
    if (fragments.size() == 1) {
        const bool moved = extrusion_move_from(root.mutable_handle(), fragments.front().entity.mutable_handle()) != 0;
        assert(moved);
        (void) moved;
        if (fragments.front().fuzzify)
            targets.push_back({ root, fragments.front().params });
        return;
    }

    const bool was_reversible = (root.flags() & RAW_EXTRUSION_FLAG_REVERSIBLE) != 0;
    root.clear_content();
    root.set_flags(was_reversible ? RAW_EXTRUSION_FLAG_REVERSIBLE : 0);
    for (SplitFragment &fragment : fragments)
        root.add_child(fragment.entity.mutable_view());
    collect_fuzzy_targets_from_fragments(root, fragments, targets);
}

void process_entity_children(storage_handle *storage,
                             MutableExtrusionEntity parent,
                             const RegionSettings::AreaMap *areas,
                             const FuzzyParameters &solo_params,
                             double nozzle_diameter,
                             raw_extrusion_role role,
                             const InheritedExtrusionState &parent_state,
                             std::vector<FuzzyTarget> &targets);

void process_entity(storage_handle *storage,
                    MutableExtrusionEntity entity,
                    const RegionSettings::AreaMap *areas,
                    const FuzzyParameters &solo_params,
                    double nozzle_diameter,
                    raw_extrusion_role role,
                    const InheritedExtrusionState &parent_state,
                    std::vector<FuzzyTarget> &targets)
{
    // Walk the extrusion tree directly instead of flattening it. Keeping the
    // hierarchy lets us replace a single leaf without disturbing unrelated
    // collections, loops, or continuous path groups.
    const InheritedExtrusionState state = state_with_entity_properties(parent_state, entity.readonly());

    if (entity.child_count() > 0) {
        process_entity_children(storage, entity, areas, solo_params, nozzle_diameter, role, state, targets);
        return;
    }

    if (!entity.has_polyline() || entity.point_count() < 2)
        return;

    if (areas == nullptr) {
        if (should_fuzzify_for_role(role, solo_params, state))
            targets.push_back({ entity, solo_params });
        return;
    }

    std::vector<SplitFragment> fragments =
        split_leaf_by_region_settings(storage, entity, *areas, nozzle_diameter, role, state);
    if (fragments_need_replacement(fragments))
        replace_root_leaf_with_fragments(entity, fragments, targets);
}

void process_entity_children(storage_handle *storage,
                             MutableExtrusionEntity parent,
                             const RegionSettings::AreaMap *areas,
                             const FuzzyParameters &solo_params,
                             double nozzle_diameter,
                             raw_extrusion_role role,
                             const InheritedExtrusionState &parent_state,
                             std::vector<FuzzyTarget> &targets)
{
    uint32_t child_idx = 0;
    while (child_idx < parent.child_count()) {
        MutableExtrusionEntity child = parent.child_mutable(child_idx);
        const InheritedExtrusionState child_state = state_with_entity_properties(parent_state, child.readonly());

        if (child.child_count() > 0) {
            process_entity_children(storage, child, areas, solo_params, nozzle_diameter, role, child_state, targets);
            ++child_idx;
            continue;
        }

        if (!child.has_polyline() || child.point_count() < 2) {
            ++child_idx;
            continue;
        }

        if (areas == nullptr) {
            if (should_fuzzify_for_role(role, solo_params, child_state))
                targets.push_back({ child, solo_params });
            ++child_idx;
            continue;
        }

        std::vector<SplitFragment> fragments =
            split_leaf_by_region_settings(storage, child, *areas, nozzle_diameter, role, child_state);
        child_idx = fragments_need_replacement(fragments) ?
            replace_child_with_fragments(parent, child_idx, child, fragments, targets) :
            child_idx + 1;
    }
}

uint32_t seed_from_points(const std::vector<c_point> &points)
{
    // The original host implementation used the global slicer RNG. This plugin
    // keeps the Cura-derived point placement algorithm, but uses a local seed
    // derived from the extrusion geometry so parallel plugin runs do not share
    // mutable random state.
    uint32_t seed = 2166136261u;
    for (const c_point point : points) {
        const uint64_t x = uint64_t(point.x);
        const uint64_t y = uint64_t(point.y);
        seed ^= uint32_t(x);
        seed *= 16777619u;
        seed ^= uint32_t(x >> 32);
        seed *= 16777619u;
        seed ^= uint32_t(y);
        seed *= 16777619u;
        seed ^= uint32_t(y >> 32);
        seed *= 16777619u;
    }
    return seed == 0 ? 1u : seed;
}

double random_between_0_and_1(std::minstd_rand &rng)
{
    return double(rng()) / double(rng.max());
}

void append_point_if_different(std::vector<c_point> &out, c_point point)
{
    if (out.empty() || !points_equal(out.back(), point))
        out.push_back(point);
}

bool points_differ(const std::vector<c_point> &lhs, const std::vector<c_point> &rhs)
{
    if (lhs.size() != rhs.size())
        return true;
    for (size_t idx = 0; idx < lhs.size(); ++idx)
        if (!points_equal(lhs[idx], rhs[idx]))
            return true;
    return false;
}

c_point fuzzy_point_between(c_point p0, c_point p1, double distance, double offset)
{
    const double dx = double(p1.x) - double(p0.x);
    const double dy = double(p1.y) - double(p0.y);
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.)
        return p0;

    const double ratio = distance / length;
    const double x = double(p0.x) + dx * ratio - dy * offset / length;
    const double y = double(p0.y) + dy * ratio + dx * offset / length;
    return c_point{ coord_t(x), coord_t(y) };
}

double points_length(const std::vector<c_point> &points)
{
    double length = 0.;
    for (size_t idx = 1; idx < points.size(); ++idx)
        length += norm(points[idx] - points[idx - 1]);
    return length;
}

void append_fuzzy_segment_points(c_point p0,
                                 c_point p1,
                                 coordf_t min_dist_between_points,
                                 coordf_t range_random_point_dist,
                                 coordf_t thickness,
                                 double &dist_left_over,
                                 std::minstd_rand &rng,
                                 std::vector<c_point> &out)
{
    const double segment_length = norm(p1 - p0);
    if (segment_length <= 0.)
        return;

    double last_inserted_distance = dist_left_over + segment_length * 2.;
    for (double distance = dist_left_over; distance < segment_length;
         distance += min_dist_between_points + random_between_0_and_1(rng) * range_random_point_dist) {
        const double offset = random_between_0_and_1(rng) * (thickness * 2.) - thickness;
        append_point_if_different(out, fuzzy_point_between(p0, p1, distance, offset));
        last_inserted_distance = distance;
    }
    dist_left_over = segment_length - last_inserted_distance;
}

std::vector<c_point> fuzzy_open_points(const std::vector<c_point> &points, const FuzzyParameters &params)
{
    // Open paths keep their original endpoints. Only intermediate randomized
    // points are inserted, otherwise travel planning may see a changed start or
    // end position for the extrusion.
    if (points.size() < 2)
        return points;

    const coordf_t min_dist_between_points = params.point_distance * 3. / 4.;
    const coordf_t range_random_point_dist = params.point_distance / 2.;
    if (min_dist_between_points <= SCALED_EPSILON || points_length(points) < min_dist_between_points * 3.)
        return points;

    std::minstd_rand rng(seed_from_points(points));
    double dist_left_over = random_between_0_and_1(rng) * (min_dist_between_points / 2.);
    std::vector<c_point> out;
    out.reserve(points.size());
    append_point_if_different(out, points.front());

    for (size_t idx = 1; idx < points.size(); ++idx)
        append_fuzzy_segment_points(points[idx - 1], points[idx], min_dist_between_points,
                                    range_random_point_dist, params.thickness, dist_left_over, rng, out);

    append_point_if_different(out, points.back());
    return out.size() >= 2 ? out : points;
}

std::vector<c_point> fuzzy_closed_points(const std::vector<c_point> &points, const FuzzyParameters &params)
{
    // Closed paths are processed as a ring, then explicitly closed again. This
    // keeps loop semantics intact even when the first randomized point moves.
    if (points.size() < 4)
        return points;

    std::vector<c_point> ring(points.begin(), points.end() - 1);
    const coordf_t min_dist_between_points = params.point_distance * 3. / 4.;
    const coordf_t range_random_point_dist = params.point_distance / 2.;
    if (min_dist_between_points <= SCALED_EPSILON || points_length(points) < min_dist_between_points * 3.)
        return points;

    std::minstd_rand rng(seed_from_points(points));
    double dist_left_over = random_between_0_and_1(rng) * (min_dist_between_points / 2.);
    std::vector<c_point> out;
    out.reserve(points.size());

    c_point previous = ring.back();
    for (const c_point point : ring) {
        append_fuzzy_segment_points(previous, point, min_dist_between_points, range_random_point_dist,
                                    params.thickness, dist_left_over, rng, out);
        previous = point;
    }

    if (out.size() < 3)
        return points;
    append_point_if_different(out, out.front());
    return out;
}

void apply_fuzzy_skin_to_entity(const FuzzyTarget &target)
{
    // This is the plugin-side adaptation of the fuzzy_paths(), fuzzy_polygon()
    // and fuzzy_extrusion_line() algorithms that historically lived in
    // PerimeterGenerator.cpp. The point-placement idea comes from Cura: insert
    // points at randomized distances, offset them along the segment normal,
    // and keep the path endpoints/closure valid.
    MutableExtrusionEntity entity = target.entity;
    const std::vector<c_point> points = entity.points();
    if (points.size() < 2)
        return;

    const std::vector<c_point> fuzzy_points = entity.local_is_closed() ?
        fuzzy_closed_points(points, target.params) :
        fuzzy_open_points(points, target.params);
    if (fuzzy_points.size() >= 2 && points_differ(fuzzy_points, points))
        entity.set_points(fuzzy_points);
}

void process_region_island_role(const run_ctx_post_perimeter_generation &ctx,
                                storage_handle *storage,
                                const LayerRegionIsland &region_island,
                                const RegionSettings &settings,
                                double nozzle_diameter,
                                raw_extrusion_role role,
                                std::vector<FuzzyTarget> &targets)
{
    // Each role bucket has its own extrusion root. Perimeters and gap fill use
    // the same splitter, but fuzzy skin settings only allow gap fill in "all"
    // mode to match the historical behavior.
    extrusion_entity_handle *root_handle = ctx.get_region_island_mutable_extrusion(region_island.handle(), role);
    if (root_handle == nullptr)
        return;

    const RegionSettings::AreaMap &areas = settings.get_areas(k_fuzzy_skin_key);
    if (!any_area_can_fuzzify_role(areas, role, nozzle_diameter))
        return;

    MutableExtrusionEntity root(root_handle);
    const InheritedExtrusionState empty_state;
    if (settings.has_many_config(k_fuzzy_skin_key)) {
        process_entity(storage, root, &areas, FuzzyParameters{}, nozzle_diameter, role, empty_state, targets);
        return;
    }

    const FuzzyParameters solo_params =
        fuzzy_parameters_from_value(settings.get_solo_config(k_fuzzy_skin_key), nozzle_diameter);
    if (role == RAW_EXTRUSION_ROLE_GAP_FILL ? !solo_params.can_fuzz_gap_fill() : !solo_params.can_fuzz_perimeters())
        return;

    process_entity(storage, root, nullptr, solo_params, nozzle_diameter, role, empty_state, targets);
}

void process_island(const run_ctx_post_perimeter_generation &ctx,
                    storage_handle *storage,
                    const LayerIsland &island)
{
    // Build the region-setting map once per island. Every region island inside
    // that layer island can then reuse the same spatial partitioning.
    if (island.region_count() == 0 || island.region_island_count() == 0)
        return;

    RegionSettings settings(storage, island, {{k_fuzzy_skin_key, k_fuzzy_skin_thickness_key, k_fuzzy_skin_point_dist_key}});
    settings.segregate(island.slice());
    const double nozzle_diameter = nozzle_diameter_for_fuzzy_skin(island);
    std::vector<FuzzyTarget> targets;

    for (uint32_t region_island_idx = 0; region_island_idx < island.region_island_count(); ++region_island_idx) {
        const LayerRegionIsland region_island = island.region_island(region_island_idx);
        process_region_island_role(ctx, storage, region_island, settings, nozzle_diameter,
                                   RAW_EXTRUSION_ROLE_PERIMETER, targets);
        process_region_island_role(ctx, storage, region_island, settings, nozzle_diameter,
                                   RAW_EXTRUSION_ROLE_GAP_FILL, targets);
    }

    // Split first, fuzz afterwards. This avoids invalidating tree positions
    // while the splitter is still walking the children of a parent entity.
    for (const FuzzyTarget &target : targets)
        apply_fuzzy_skin_to_entity(target);
}

} // namespace

FuzzySkin &FuzzySkin::instance(orchestrator_handle *orch)
{
    static FuzzySkin s_instance(orch);
    return s_instance;
}

const char *FuzzySkin::id_impl() const noexcept
{
    return k_fuzzy_skin_id;
}

const char *FuzzySkin::name_impl() const noexcept
{
    return "Fuzzy skin";
}

const char *FuzzySkin::description_impl() const noexcept
{
    return "Perturbs perimeter geometry in regions where fuzzy skin is enabled.";
}

slicing_step_t FuzzySkin::step_impl() const noexcept
{
    return STEP_POST_PERIMETER;
}

const char *const *FuzzySkin::dependencies_impl() const noexcept
{
    return k_no_dependencies;
}

int32_t FuzzySkin::priority_impl() const noexcept
{
    return 0;
}

int32_t FuzzySkin::used_config_keys(const char **keys) const noexcept
{
    if (keys != nullptr)
        for (uint32_t idx = 0; idx < sizeof(k_used_config_keys) / sizeof(k_used_config_keys[0]); ++idx)
            keys[idx] = k_used_config_keys[idx];
    return int32_t(sizeof(k_used_config_keys) / sizeof(k_used_config_keys[0]));
}

const char *FuzzySkin::progress_message_format_impl() const noexcept
{
    return "Fuzzy skin: %u / %u layers";
}

void FuzzySkin::setup_run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_post_perimeter_generation *ctx = plugin_ctx_as_post_perimeter_generation(run_ctx);
    if (ctx != nullptr && ctx->object != nullptr)
        progress().add_max(Object(ctx->object).layer_count());
}

void FuzzySkin::run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_post_perimeter_generation *ctx = plugin_ctx_as_post_perimeter_generation(run_ctx);
    if (ctx == nullptr || ctx->object == nullptr || run_ctx == nullptr || run_ctx->plugin_storage == nullptr)
        return;

    throw_if_cancelled(run_ctx);

    const Object object(ctx->object);
    for (uint32_t layer_idx = 0; layer_idx < object.layer_count(); ++layer_idx) {
        const Layer layer = object.layer(layer_idx);
        if (layer_idx > 0) {
            for (uint32_t island_idx = 0; island_idx < layer.island_count(); ++island_idx)
                process_island(*ctx, run_ctx->plugin_storage, layer.island(island_idx));
        }
        progress().increment();
    }
}

void register_fuzzy_skin_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, FuzzySkin::instance(orch).c_instance());
}

}}} // namespace slic3r_api::Perimeter::FuzzySkinPlugin

#ifdef FUZZY_SKIN_PLUGIN_DLL
extern "C" void register_plugin(orchestrator_handle *orch)
{
    slic3r_api::Perimeter::FuzzySkinPlugin::register_fuzzy_skin_plugin(orch);
}
#endif // FUZZY_SKIN_PLUGIN_DLL
