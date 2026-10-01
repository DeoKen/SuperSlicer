#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "test_data.hpp"
#include "gcode_walk.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Small perimeter min / max length, ported from SuperSlicer: a speed ramp between the two lengths.

namespace {

DynamicPrintConfig base_config()
{
    return DynamicPrintConfig::full_print_config_with({
        { "cooling",                  { 0 } },     // no slowdown, the speeds are checked as set
        { "perimeter_speed",          60 },
        { "external_perimeter_speed", 40 },
        { "small_perimeter_speed",    20 },
        { "perimeters",               2 },
        { "enable_dynamic_overhang_speeds", false },
    });
}

// Feed rates (mm/s) of the perimeters of a type from the second layer on.
std::set<double> speeds(const std::vector<Extrusion> &extrusions, const std::string &type)
{
    std::set<double> out;
    for (const Extrusion &e : extrusions)
        if (e.layer >= 1 && e.type == type)
            out.insert(e.F / 60.);
    return out;
}

} // namespace

TEST_CASE("Small perimeters: default threshold", "[SmallPerimeter]") {
    // The perimeters of a 20 mm cube are longer than the default threshold of 40.8 mm.
    const auto extrusions = walk_gcode(Test::slice({ TestMesh::cube_20x20x20 }, base_config()));
    CHECK(speeds(extrusions, "Perimeter") == std::set<double>{ 60 });
    CHECK(speeds(extrusions, "External perimeter") == std::set<double>{ 40 });
}

TEST_CASE("Small perimeters: min length", "[SmallPerimeter]") {
    DynamicPrintConfig config = base_config();
    config.set_deserialize_strict({ { "small_perimeter_min_length", 200 } });
    const auto extrusions = walk_gcode(Test::slice({ TestMesh::cube_20x20x20 }, config));
    CHECK(speeds(extrusions, "Perimeter") == std::set<double>{ 20 });
    CHECK(speeds(extrusions, "External perimeter") == std::set<double>{ 20 });
}

TEST_CASE("Small perimeters: speed ramp between min and max length", "[SmallPerimeter]") {
    DynamicPrintConfig config = base_config();
    config.set_deserialize_strict({ { "small_perimeter_min_length", 10 }, { "small_perimeter_max_length", 200 } });
    const auto extrusions = walk_gcode(Test::slice({ TestMesh::cube_20x20x20 }, config));
    const std::set<double> perimeter = speeds(extrusions, "Perimeter");
    const std::set<double> external  = speeds(extrusions, "External perimeter");
    REQUIRE(! perimeter.empty());
    REQUIRE(! external.empty());
    for (double s : perimeter) {
        CHECK(s > 20);
        CHECK(s < 60);
    }
    for (double s : external) {
        CHECK(s > 20);
        CHECK(s < 40);
    }
    // The inner perimeter is shorter than the external one, so it is relatively slower: compare the factors.
    const double f_perimeter = (*perimeter.begin() - 20) / (60 - 20);
    const double f_external  = (*external.begin() - 20) / (40 - 20);
    CHECK(f_perimeter < f_external);

    SECTION("a percentage of the nozzle diameter") {
        // 0.4 mm nozzle: 2500% = 10 mm, 50000% = 200 mm, the same as above.
        DynamicPrintConfig pct = base_config();
        pct.set_deserialize_strict({ { "small_perimeter_min_length", "2500%" }, { "small_perimeter_max_length", "50000%" } });
        const auto extrusions_pct = walk_gcode(Test::slice({ TestMesh::cube_20x20x20 }, pct));
        CHECK(speeds(extrusions_pct, "Perimeter") == perimeter);
        CHECK(speeds(extrusions_pct, "External perimeter") == external);
    }
}
