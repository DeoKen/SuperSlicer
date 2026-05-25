#include <catch2/catch.hpp>

#include "perimeter_test_helpers.hpp"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionProperty.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
using namespace Slic3r;
using namespace Slic3r::Test::PerimeterPluginTests;

struct WidthCrossing
{
    float width = 0.f;
};

Polygon circle_polygon(const double center_x, const double center_y, const double radius, const size_t point_count)
{
    Points points;
    points.reserve(point_count);
    for (size_t idx = 0; idx < point_count; ++idx) {
        const double angle = 2. * PI * double(idx) / double(point_count);
        points.emplace_back(scale_i(center_x + radius * std::cos(angle)),
                            scale_i(center_y + radius * std::sin(angle)));
    }
    Polygon polygon(std::move(points));
    polygon.make_counter_clockwise();
    return polygon;
}

ExPolygon tapered_thin_surface()
{
    // A long closed island whose middle is always too thin for regular
    // multi-perimeter output. Its thickness still changes from left to right,
    // so Arachne should keep a single center extrusion but vary its width.
    return ExPolygon(Polygon({
        Point(scale_i(-14.), scale_i(-0.30)),
        Point(scale_i(-7.),  scale_i(-0.30)),
        Point(scale_i(7.),   scale_i(-0.42)),
        Point(scale_i(14.),  scale_i(-0.42)),
        Point(scale_i(14.),  scale_i(0.42)),
        Point(scale_i(7.),   scale_i(0.42)),
        Point(scale_i(-7.),  scale_i(0.30)),
        Point(scale_i(-14.), scale_i(0.30))
    }));
}

ExPolygon crescent_surface()
{
    // Difference between two overlapping disks. The result is a half-moon-like
    // island with a wide belly and narrow tips; with enough requested
    // perimeters, Arachne should use variable-width walls to cover the island
    // instead of leaving a large infill island behind.
    const ExPolygon outer(circle_polygon(0., 0., 12., 96));
    const ExPolygon cutter(circle_polygon(3.5, 0., 11., 96));
    ExPolygons crescent = diff_ex(outer, cutter);
    REQUIRE(crescent.size() == 1);
    return crescent.front();
}

bool segment_crosses_vertical_window(const Point &a,
                                     const Point &b,
                                     const coord_t x,
                                     const coord_t y_min,
                                     const coord_t y_max)
{
    if (a.x() == b.x())
        return a.x() == x && std::max(a.y(), b.y()) >= y_min && std::min(a.y(), b.y()) <= y_max;

    if ((a.x() < x && b.x() < x) || (a.x() > x && b.x() > x))
        return false;

    const double t = double(x - a.x()) / double(b.x() - a.x());
    if (t < 0. || t > 1.)
        return false;

    const double y = double(a.y()) + t * double(b.y() - a.y());
    return y >= double(y_min) && y <= double(y_max);
}

bool polyline_crosses_vertical_window(const ArcPolyline &polyline,
                                      const double x_mm,
                                      const double y_min_mm,
                                      const double y_max_mm)
{
    const Polyline simple = polyline.to_polyline();
    if (simple.points.size() < 2)
        return false;

    const coord_t x = scale_i(x_mm);
    const coord_t y_min = scale_i(y_min_mm);
    const coord_t y_max = scale_i(y_max_mm);
    for (size_t idx = 1; idx < simple.points.size(); ++idx)
        if (segment_crosses_vertical_window(simple.points[idx - 1], simple.points[idx], x, y_min, y_max))
            return true;
    return false;
}

void collect_width_crossings(const ExtrusionEntity &entity,
                             const double x_mm,
                             const double y_min_mm,
                             const double y_max_mm,
                             std::vector<WidthCrossing> &out)
{
    if (const ArcPolyline *polyline = entity.polyline_or_null()) {
        const ExtrusionAttributes *attributes = entity.get_property<ExtrusionAttributes>();
        if (attributes != nullptr && polyline_crosses_vertical_window(*polyline, x_mm, y_min_mm, y_max_mm))
            out.push_back({attributes->width});
        return;
    }

    if (!entity.is_leaf())
        for (const ExtrusionEntityUPtr &child : entity.children())
            if (child)
                collect_width_crossings(*child, x_mm, y_min_mm, y_max_mm, out);
}

size_t crossing_branch_count(const ExtrusionEntity &entity,
                             const double x_mm,
                             const double y_min_mm,
                             const double y_max_mm)
{
    if (entity.is_leaf())
        return entity.polyline_or_null() != nullptr &&
               polyline_crosses_vertical_window(entity.as_polyline(), x_mm, y_min_mm, y_max_mm) ? 1 : 0;

    if (!entity.is_collection())
        return polyline_crosses_vertical_window(entity.as_polyline(), x_mm, y_min_mm, y_max_mm) ? 1 : 0;

    size_t count = 0;
    for (const ExtrusionEntityUPtr &child : entity.children())
        if (child != nullptr)
            count += crossing_branch_count(*child, x_mm, y_min_mm, y_max_mm);
    return count;
}

float max_crossing_width(const std::vector<WidthCrossing> &crossings)
{
    REQUIRE_FALSE(crossings.empty());
    float out = 0.f;
    for (const WidthCrossing &crossing : crossings)
        out = std::max(out, crossing.width);
    return out;
}

double covered_area_ratio(const ExPolygon &surface, const ExtrusionEntity &extrusions)
{
    Polygons covered_polygons;
    extrusions.polygons_covered_by_width(covered_polygons, float(scale_d(0.02)));
    ExPolygons covered = union_ex(covered_polygons);
    ExPolygons missed = diff_ex(surface, covered);
    return area(missed) / surface.area();
}
}

TEST_CASE("ArachnePerimeterGenerator publishes variable-width perimeter output", "[plugins][perimeter]") {
    // The Arachne STEP_PERIMETER plugin consume island payload and publish both variable-width perimeter extrusions
    // and fill surfaces for the generated inner contour.
    const DynamicPrintConfig config = perimeter_config({{"perimeters", "2"}});
    const ExPolygon surface = rectangle_with_hole_expolygon();

    const PerimeterRunCapture generated =
        run_perimeter_case(config, {ARACHNE_PERIMETER_GENERATOR}, surface, 0);
    REQUIRE(external_perimeter_count(generated) > 0);
    REQUIRE(extrusion_length(generated.external_perimeters) > 0.);
    REQUIRE_FALSE(generated.fill_surfaces.empty());
    REQUIRE_FALSE(generated.fill_no_overlap_surfaces.empty());
}

TEST_CASE("ArachnePerimeterGenerator emits one wider line through a variable thin area", "[plugins][perimeter]")
{
    // The island is a tapered thin ribbon. One perimeter generation pass cannot
    // fit several regular side-by-side walls in the narrow cross-sections, so
    // Arachne should produce exactly one extrusion crossing each thin section,
    // and the crossing in the wider section should carry a larger width.
    const DynamicPrintConfig config = perimeter_config({{"perimeters", "1"}});
    const PerimeterRunCapture generated =
        run_perimeter_case(config, {ARACHNE_PERIMETER_GENERATOR}, tapered_thin_surface(), 0);

    std::vector<WidthCrossing> narrow_crossings;
    std::vector<WidthCrossing> wider_crossings;
    collect_width_crossings(generated.external_perimeters, -8., -1., 1., narrow_crossings);
    collect_width_crossings(generated.external_perimeters, 0., -1., 1., wider_crossings);

    REQUIRE(crossing_branch_count(generated.external_perimeters, -8., -1., 1.) == 1);
    REQUIRE(crossing_branch_count(generated.external_perimeters, 0., -1., 1.) == 1);
    const float narrow_width = max_crossing_width(narrow_crossings);
    const float wider_width = max_crossing_width(wider_crossings);
    CHECK(narrow_width < wider_width);
    CHECK(wider_width - narrow_width > 0.02f);
}

TEST_CASE("ArachnePerimeterGenerator covers a crescent surface with enough perimeters", "[plugins][perimeter]")
{
    // A half-moon has thin tips and a broader center. With enough requested
    // perimeters, the variable-width perimeter set should cover nearly the
    // whole island by width, leaving only tiny numerical clipping residue.
    const ExPolygon surface = crescent_surface();
    const DynamicPrintConfig config = perimeter_config({{"perimeters", "9"}});
    const PerimeterRunCapture generated =
        run_perimeter_case(config, {ARACHNE_PERIMETER_GENERATOR}, surface, 0);

    REQUIRE(external_perimeter_count(generated) > 0);
    CHECK(covered_area_ratio(surface, generated.external_perimeters) < 0.02);
}
