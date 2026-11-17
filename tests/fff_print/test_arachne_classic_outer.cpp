#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "libslic3r/libslic3r.h"
#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Arachne "Classic outer wall" (arachne_classic_outer_wall, fork).

namespace {

// Vertical cylinder of n facets, radius r, 6 mm tall.
TriangleMesh cylinder(double r, int n)
{
    indexed_triangle_set its;
    for (float z : { 0.f, 6.f })
        for (int i = 0; i < n; ++ i) {
            const double a = 2. * PI * i / n;
            its.vertices.emplace_back(float(r * std::cos(a)), float(r * std::sin(a)), z);
        }
    for (int i = 0; i < n; ++ i) {
        const int j = (i + 1) % n;
        its.indices.emplace_back(i, j, n + j);
        its.indices.emplace_back(i, n + j, n + i);
    }
    const int bottom = int(its.vertices.size());
    its.vertices.emplace_back(0.f, 0.f, 0.f);
    const int top = int(its.vertices.size());
    its.vertices.emplace_back(0.f, 0.f, 6.f);
    for (int i = 0; i < n; ++ i) {
        const int j = (i + 1) % n;
        its.indices.emplace_back(bottom, j, i);
        its.indices.emplace_back(top, n + i, n + j);
    }
    return TriangleMesh(std::move(its));
}

DynamicPrintConfig config(const std::string &generator, bool classic_outer)
{
    return DynamicPrintConfig::full_print_config_with({
        { "nozzle_diameter",                    "0.4" },
        { "layer_height",                       0.2 },
        { "first_layer_height",                 0.2 },
        { "extrusion_width",                    "0.45" },
        { "first_layer_extrusion_width",        "0.45" },
        { "perimeter_extrusion_width",          "0.45" },
        { "external_perimeter_extrusion_width", "0.4" },
        { "perimeters",                         3 },
        { "fill_density",                       "15%" },
        { "skirts",                             0 },
        { "perimeter_generator",                generator },
        { "arachne_classic_outer_wall",         classic_outer ? "1" : "0" },
    });
}

void collect(const ExtrusionEntity &e, Polylines &out)
{
    if (const auto *coll = dynamic_cast<const ExtrusionEntityCollection*>(&e)) {
        for (const ExtrusionEntity *child : coll->entities)
            collect(*child, out);
    } else if (e.role().is_external_perimeter())
        out.emplace_back(e.as_polyline());
}

// The outer walls of every layer (from the third, above the bottom layers, up to below the top layers).
std::vector<Polylines> outer_walls(const TriangleMesh &mesh, const DynamicPrintConfig &cfg)
{
    Print print;
    init_and_process_print({ mesh }, print, cfg);
    std::vector<Polylines> out;
    const PrintObject &object = *print.objects().front();
    for (size_t i = 3; i + 3 < object.layers().size(); ++ i) {
        Polylines walls;
        for (const LayerRegion *region : object.layers()[i]->regions())
            collect(region->perimeters(), walls);
        out.emplace_back(std::move(walls));
    }
    return out;
}

// Largest distance of the points of `a` to the lines of `b` (mm).
double distance(const Polylines &a, const Polylines &b)
{
    Lines lines;
    for (const Polyline &pl : b)
        append(lines, pl.lines());
    const AABBTreeLines::LinesDistancer<Line> tree(std::move(lines));
    double d = 0.;
    for (const Polyline &pl : a)
        for (const Point &p : pl.points)
            d = std::max(d, tree.distance_from_lines<false>(p));
    return unscaled<double>(d);
}

} // namespace

TEST_CASE("Classic outer wall: the outer wall of a cylinder is the classic one on every layer", "[ArachneClassicOuter]") {
    const TriangleMesh mesh = cylinder(10., 180);
    const std::vector<Polylines> hybrid  = outer_walls(mesh, config("arachne", true));
    const std::vector<Polylines> arachne = outer_walls(mesh, config("arachne", false));
    const std::vector<Polylines> classic = outer_walls(mesh, config("classic", false));
    REQUIRE(hybrid.size() == classic.size());
    REQUIRE(hybrid.size() > 10);
    const size_t mid = hybrid.size() / 2;
    double hybrid_vs_classic = 0., hybrid_drift = 0., classic_drift = 0.;
    for (size_t i = 0; i < hybrid.size(); ++ i) {
        REQUIRE(! hybrid[i].empty());
        hybrid_vs_classic = std::max(hybrid_vs_classic, std::max(distance(hybrid[i], classic[i]), distance(classic[i], hybrid[i])));
        hybrid_drift      = std::max(hybrid_drift, distance(hybrid[i], hybrid[mid]));
        classic_drift     = std::max(classic_drift, distance(classic[i], classic[mid]));
    }
    // Same line as the classic generator on every layer (up to rounding of the coordinates), so as consistent as classic.
    CHECK(hybrid_vs_classic < 0.001);
    CHECK(hybrid_drift <= classic_drift + 0.001);
    // Arachne without the option is a different line: it drops points of the curve (shortcuts up to ~0.025 mm inside).
    double arachne_vs_classic = 0.;
    for (size_t i = 0; i < arachne.size(); ++ i)
        arachne_vs_classic = std::max(arachne_vs_classic, std::max(distance(arachne[i], classic[i]), distance(classic[i], arachne[i])));
    CHECK(arachne_vs_classic > 0.005);
    WARN("hybrid vs classic " << hybrid_vs_classic << " mm, Arachne vs classic " << arachne_vs_classic << " mm, drift: hybrid " << hybrid_drift << " classic " << classic_drift);
}

TEST_CASE("Classic outer wall: thin walls stay as Arachne makes them", "[ArachneClassicOuter]") {
    // 0.7 mm wall: Arachne narrows its lines, so the outer loop is not of the normal width and is kept.
    TriangleMesh mesh = TriangleMesh(its_make_cube(20., 0.7, 6.));
    const std::vector<Polylines> hybrid  = outer_walls(mesh, config("arachne", true));
    const std::vector<Polylines> arachne = outer_walls(mesh, config("arachne", false));
    REQUIRE(hybrid.size() == arachne.size());
    for (size_t i = 0; i < hybrid.size(); ++ i) {
        REQUIRE(hybrid[i].size() == arachne[i].size());
        for (size_t k = 0; k < hybrid[i].size(); ++ k)
            CHECK(hybrid[i][k].points == arachne[i][k].points);
    }
}

TEST_CASE("Classic outer wall: off by default", "[ArachneClassicOuter]") {
    CHECK(! FullPrintConfig::defaults().opt_bool("arachne_classic_outer_wall"));
}
