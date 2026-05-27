#include <catch2/catch.hpp>

#include "perimeter_test_helpers.hpp"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Line.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {
using namespace Slic3r;
using namespace Slic3r::Test::PerimeterPluginTests;

DynamicPrintConfig fuzzy_config(std::initializer_list<std::pair<std::string, std::string>> overrides)
{
    DynamicPrintConfig config = perimeter_config({
        {"fuzzy_skin", "all"},
        {"fuzzy_skin_thickness", "0.2"},
        {"fuzzy_skin_point_dist", "0.5"}
    });
    for (const std::pair<std::string, std::string> &entry : overrides)
        config.set_deserialize_strict(entry.first, entry.second);
    return config;
}

void collect_leaf_polylines(const ExtrusionEntity &entity, Polylines &out)
{
    if (entity.is_nop())
        return;

    if (entity.is_leaf()) {
        Points points;
        entity.collect_points(points);
        if (points.size() >= 2)
            out.emplace_back(std::move(points));
        return;
    }

    for (size_t child_idx = 0; child_idx < entity.child_count(); ++child_idx)
        collect_leaf_polylines(entity.child(child_idx), out);
}

Polylines leaf_polylines(const ExtrusionEntity &entity)
{
    Polylines out;
    collect_leaf_polylines(entity, out);
    return out;
}

size_t point_count_on_side(const ExtrusionEntity &entity, const bool right_side)
{
    if (entity.is_nop())
        return 0;

    if (entity.is_leaf()) {
        Points points;
        entity.collect_points(points);
        size_t count = 0;
        for (const Point &point : points)
            if (right_side ? point.x() > 0 : point.x() < 0)
                ++count;
        return count;
    }

    size_t count = 0;
    for (size_t child_idx = 0; child_idx < entity.child_count(); ++child_idx)
        count += point_count_on_side(entity.child(child_idx), right_side);
    return count;
}

double orientation_value(const Point &a, const Point &b, const Point &c)
{
    const double ab_x = double(b.x()) - double(a.x());
    const double ab_y = double(b.y()) - double(a.y());
    const double ac_x = double(c.x()) - double(a.x());
    const double ac_y = double(c.y()) - double(a.y());
    return ab_x * ac_y - ab_y * ac_x;
}

bool opposite_strict_signs(const double lhs, const double rhs)
{
    return (lhs < 0. && rhs > 0.) || (lhs > 0. && rhs < 0.);
}

bool proper_segment_crossing(const Line &lhs, const Line &rhs)
{
    // A proper crossing has both segment interiors crossing each other. Touches
    // at shared endpoints are allowed because split fuzzy fragments naturally
    // meet at region boundaries.
    return opposite_strict_signs(orientation_value(lhs.a, lhs.b, rhs.a),
                                 orientation_value(lhs.a, lhs.b, rhs.b)) &&
           opposite_strict_signs(orientation_value(rhs.a, rhs.b, lhs.a),
                                 orientation_value(rhs.a, rhs.b, lhs.b));
}

bool adjacent_segments_of_same_polyline(const size_t lhs_polyline,
                                        const size_t lhs_segment,
                                        const size_t rhs_polyline,
                                        const size_t rhs_segment,
                                        const bool closed,
                                        const size_t segment_count)
{
    if (lhs_polyline != rhs_polyline)
        return false;
    if (lhs_segment + 1 == rhs_segment || rhs_segment + 1 == lhs_segment)
        return true;
    return closed && segment_count > 1 &&
           ((lhs_segment == 0 && rhs_segment + 1 == segment_count) ||
            (rhs_segment == 0 && lhs_segment + 1 == segment_count));
}

void require_no_centerline_crossings(const ExtrusionEntity &entity)
{
    struct SegmentRef
    {
        Line line;
        size_t polyline = 0;
        size_t segment = 0;
        bool closed = false;
        size_t segment_count = 0;
    };

    std::vector<SegmentRef> segments;
    const Polylines polylines = leaf_polylines(entity);
    for (size_t polyline_idx = 0; polyline_idx < polylines.size(); ++polyline_idx) {
        const Lines lines = polylines[polyline_idx].lines();
        for (size_t segment_idx = 0; segment_idx < lines.size(); ++segment_idx)
            segments.push_back({lines[segment_idx], polyline_idx, segment_idx,
                                polylines[polyline_idx].is_closed(), lines.size()});
    }

    for (size_t lhs_idx = 0; lhs_idx < segments.size(); ++lhs_idx)
        for (size_t rhs_idx = lhs_idx + 1; rhs_idx < segments.size(); ++rhs_idx) {
            const SegmentRef &lhs = segments[lhs_idx];
            const SegmentRef &rhs = segments[rhs_idx];
            if (adjacent_segments_of_same_polyline(lhs.polyline, lhs.segment, rhs.polyline, rhs.segment,
                                                   lhs.closed, lhs.segment_count))
                continue;

            INFO("unexpected crossing between fuzzy centerline segments " << lhs_idx << " and " << rhs_idx);
            REQUIRE_FALSE(proper_segment_crossing(lhs.line, rhs.line));
        }
}

void require_length_growth_bounded(const PerimeterRunCapture &baseline,
                                   const PerimeterRunCapture &modified,
                                   const double max_ratio)
{
    const double baseline_length = extrusion_length(baseline.external_perimeters);
    const double modified_length = extrusion_length(modified.external_perimeters);
    REQUIRE(baseline_length > 0.);
    INFO("baseline length " << baseline_length << ", modified length " << modified_length);
    REQUIRE(modified_length <= baseline_length * max_ratio);
}

void require_centerline_inside_envelope(const PerimeterRunCapture &baseline,
                                        const PerimeterRunCapture &modified,
                                        const coordf_t envelope_radius)
{
    const Polylines baseline_polylines = leaf_polylines(baseline.external_perimeters);
    const Polylines modified_polylines = leaf_polylines(modified.external_perimeters);

    // The envelope is the physical corridor around the original centerline. It
    // is deliberately wider than fuzzy_skin_thickness to absorb integer
    // rounding and corner joins, while still catching a fragment that escaped
    // to a wrong region or jumped across the island.
    const Polygons envelope =
        union_(offset(baseline_polylines, envelope_radius, ClipperLib::jtMiter, 3.0, ClipperLib::etClosedLine));
    const Polylines outside = diff_pl(modified_polylines, envelope);
    INFO("outside-envelope scaled length " << total_length(outside));
    REQUIRE(total_length(outside) <= scale_d(0.02));
}

double max_distance_to_reference(const Polylines &subject, const Polylines &reference)
{
    Lines reference_lines;
    for (const Polyline &polyline : reference) {
        const Lines lines = polyline.lines();
        reference_lines.insert(reference_lines.end(), lines.begin(), lines.end());
    }
    if (reference_lines.empty())
        return 0.;

    double max_distance = 0.;
    for (const Polyline &polyline : subject)
        for (const Point &point : polyline.points) {
            double best_distance = std::numeric_limits<double>::max();
            for (const Line &line : reference_lines)
                best_distance = std::min(best_distance, line.distance_to(point));
            max_distance = std::max(max_distance, best_distance);
        }
    return max_distance;
}

} // namespace

TEST_CASE("Fuzzy skin post-process perturbs a full perimeter without breaking geometry", "[plugins][perimeter]")
{
    // Full-area case: fuzzy skin is enabled uniformly on a simple rectangle.
    // The expected behavior is a single perimeter loop with extra randomized
    // points, bounded length growth, no centerline crossing, and no damage to
    // the fill-area partition published by the perimeter step.
    const DynamicPrintConfig config = fuzzy_config({});
    const ExPolygon area = rectangle_expolygon(-10., -10., 10., 10.);
    const size_t layer_idx = 1;

    const PerimeterRunCapture baseline =
        run_perimeter_case(config, {SIMPLE_PERIMETER_GENERATOR}, area, layer_idx);
    const PerimeterRunCapture fuzzy =
        run_perimeter_and_post_case(config, {SIMPLE_PERIMETER_GENERATOR}, {FUZZY_SKIN}, area, layer_idx);

    REQUIRE(external_perimeter_count(baseline) == 1);
    REQUIRE(external_perimeter_count(fuzzy) == 1);
    REQUIRE(total_polyline_points(external_perimeters(fuzzy)) >
            total_polyline_points(external_perimeters(baseline)));
    require_length_growth_bounded(baseline, fuzzy, 1.35);
    require_no_centerline_crossings(fuzzy.external_perimeters);
    require_centerline_inside_envelope(baseline, fuzzy, scale_d(0.25));
    require_leaf_fill_area_consistency(fuzzy);
}

TEST_CASE("Fuzzy skin post-process splits and fuzzifies only the enabled region", "[plugins][perimeter]")
{
    // Single-region case: the global setting is disabled and only the right
    // half of the island enables fuzzy skin. The plugin must split the original
    // leaf perimeter at the region boundary, keep the left fragments unchanged
    // enough to remain printable, and fuzz only the enabled side.
    const DynamicPrintConfig config = fuzzy_config({{"fuzzy_skin", "none"}});
    const ExPolygon area = rectangle_expolygon(-10., -10., 10., 10.);
    const ExPolygon right_side = rectangle_expolygon(0., -10., 10., 10.);
    const size_t layer_idx = 1;

    const PerimeterRunCapture baseline =
        run_perimeter_case(config, {SIMPLE_PERIMETER_GENERATOR}, area, layer_idx);
    const PerimeterRunCapture regional =
        run_perimeter_and_post_case(config,
                                    {SIMPLE_PERIMETER_GENERATOR},
                                    {FUZZY_SKIN},
                                    area,
                                    layer_idx,
                                    {{"fuzzy_skin", "all"}},
                                    &right_side);

    REQUIRE(external_perimeter_count(baseline) == 1);
    REQUIRE(external_perimeter_count(regional) > external_perimeter_count(baseline));
    REQUIRE(point_count_on_side(regional.external_perimeters, true) >
            point_count_on_side(baseline.external_perimeters, true));
    REQUIRE(point_count_on_side(regional.external_perimeters, false) <=
            point_count_on_side(baseline.external_perimeters, false) + 4);
    require_length_growth_bounded(baseline, regional, 1.35);
    require_no_centerline_crossings(regional.external_perimeters);
    require_centerline_inside_envelope(baseline, regional, scale_d(0.25));
    require_leaf_fill_area_consistency(regional);
}

TEST_CASE("Fuzzy skin post-process honors different settings in different regions", "[plugins][perimeter]")
{
    // Multi-region case: both halves of the island enable fuzzy skin, but the
    // right side asks for denser points and a thicker displacement. The point
    // count on the right must therefore be higher, proving that RegionSettings
    // did not collapse both regions into one effective fuzzy configuration.
    const DynamicPrintConfig config = fuzzy_config({{"fuzzy_skin", "none"}});
    const ExPolygon area = rectangle_expolygon(-10., -10., 10., 10.);
    const size_t layer_idx = 1;
    const std::vector<PerimeterRegionOverride> regions = {
        { rectangle_expolygon(-10., -10., 0., 10.),
          {{"fuzzy_skin", "all"}, {"fuzzy_skin_thickness", "0.10"}, {"fuzzy_skin_point_dist", "0.90"}} },
        { rectangle_expolygon(0., -10., 10., 10.),
          {{"fuzzy_skin", "all"}, {"fuzzy_skin_thickness", "0.30"}, {"fuzzy_skin_point_dist", "0.25"}} }
    };

    const PerimeterRunCapture baseline =
        run_perimeter_case(config, {SIMPLE_PERIMETER_GENERATOR}, area, layer_idx);
    const PerimeterRunCapture regional =
        run_perimeter_and_post_case_with_regions(config,
                                                 {SIMPLE_PERIMETER_GENERATOR},
                                                 {FUZZY_SKIN},
                                                 area,
                                                 layer_idx,
                                                 regions);

    REQUIRE(external_perimeter_count(regional) > external_perimeter_count(baseline));
    REQUIRE(point_count_on_side(regional.external_perimeters, true) >
            point_count_on_side(regional.external_perimeters, false));
    require_length_growth_bounded(baseline, regional, 1.65);
    require_no_centerline_crossings(regional.external_perimeters);
    require_centerline_inside_envelope(baseline, regional, scale_d(0.40));
    require_leaf_fill_area_consistency(regional);
}

TEST_CASE("Fuzzy skin settings change point density and displacement", "[plugins][perimeter]")
{
    // Setting-sensitivity case: the plugin uses deterministic random positions
    // for a given centerline, so changing point distance should change point
    // count, and changing thickness should change the maximum displacement from
    // the original perimeter.
    const ExPolygon area = rectangle_expolygon(-10., -10., 10., 10.);
    const size_t layer_idx = 1;
    const DynamicPrintConfig baseline_config = fuzzy_config({{"fuzzy_skin", "none"}});
    const DynamicPrintConfig sparse_config = fuzzy_config({{"fuzzy_skin_point_dist", "1.00"}});
    const DynamicPrintConfig dense_config = fuzzy_config({{"fuzzy_skin_point_dist", "0.25"}});
    const DynamicPrintConfig thin_config = fuzzy_config({{"fuzzy_skin_thickness", "0.05"}});
    const DynamicPrintConfig thick_config = fuzzy_config({{"fuzzy_skin_thickness", "0.35"}});

    const PerimeterRunCapture baseline =
        run_perimeter_case(baseline_config, {SIMPLE_PERIMETER_GENERATOR}, area, layer_idx);
    const PerimeterRunCapture sparse =
        run_perimeter_and_post_case(sparse_config, {SIMPLE_PERIMETER_GENERATOR}, {FUZZY_SKIN}, area, layer_idx);
    const PerimeterRunCapture dense =
        run_perimeter_and_post_case(dense_config, {SIMPLE_PERIMETER_GENERATOR}, {FUZZY_SKIN}, area, layer_idx);
    const PerimeterRunCapture thin =
        run_perimeter_and_post_case(thin_config, {SIMPLE_PERIMETER_GENERATOR}, {FUZZY_SKIN}, area, layer_idx);
    const PerimeterRunCapture thick =
        run_perimeter_and_post_case(thick_config, {SIMPLE_PERIMETER_GENERATOR}, {FUZZY_SKIN}, area, layer_idx);

    REQUIRE(total_polyline_points(dense.external_perimeters) >
            total_polyline_points(sparse.external_perimeters));

    const Polylines baseline_polylines = leaf_polylines(baseline.external_perimeters);
    const double thin_displacement = max_distance_to_reference(leaf_polylines(thin.external_perimeters), baseline_polylines);
    const double thick_displacement = max_distance_to_reference(leaf_polylines(thick.external_perimeters), baseline_polylines);
    INFO("thin displacement " << thin_displacement << ", thick displacement " << thick_displacement);
    REQUIRE(thick_displacement > thin_displacement * 2.);

    require_length_growth_bounded(baseline, dense, 1.75);
    require_length_growth_bounded(baseline, thick, 1.85);
    require_no_centerline_crossings(dense.external_perimeters);
    require_no_centerline_crossings(thick.external_perimeters);
    require_centerline_inside_envelope(baseline, dense, scale_d(0.25));
    require_centerline_inside_envelope(baseline, thick, scale_d(0.45));
}

TEST_CASE("Fuzzy skin post-process is a no-op when no perimeters exist", "[plugins][perimeter]")
{
    // Empty-perimeter case: a configuration with zero perimeters still runs the
    // post-process step. The plugin must tolerate an empty perimeter bucket and
    // leave it empty instead of creating standalone fuzzy geometry.
    const DynamicPrintConfig config = fuzzy_config({{"perimeters", "0"}});
    const ExPolygon area = rectangle_expolygon(-2., -2., 2., 2.);
    const size_t layer_idx = 1;

    const PerimeterRunCapture no_perimeter =
        run_perimeter_and_post_case(config, {SIMPLE_PERIMETER_GENERATOR}, {FUZZY_SKIN}, area, layer_idx);

    REQUIRE(external_perimeter_count(no_perimeter) == 0);
    REQUIRE(total_polyline_points(no_perimeter.external_perimeters) == 0);
}
