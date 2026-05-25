#include <catch2/catch.hpp>

#include "perimeter_test_helpers.hpp"

namespace {
using namespace Slic3r;
using namespace Slic3r::Test::PerimeterPluginTests;
}

TEST_CASE("Extra perimeter below area module extends small islands", "[plugins][perimeter]")
{
    // extra_perimeters_below_area only acts after the first ring, when child
    // inner surfaces exist. A very large threshold forces the branch to keep
    // generating perimeters until the surface disappears. The overlap case
    // limits that force to a smaller central region.
    const ExPolygon surface = rectangle_expolygon(-10., -10., 10., 10.);
    const DynamicPrintConfig disabled = perimeter_config({{"extra_perimeters_below_area", "0"}});
    const DynamicPrintConfig enabled = perimeter_config({{"extra_perimeters_below_area", "100000"}});

    const size_t base_count = external_perimeter_count(
        run_perimeter_case(disabled, {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_BELOW_AREA}, surface, 0));
    const size_t enabled_count = external_perimeter_count(
        run_perimeter_case(enabled, {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_BELOW_AREA}, surface, 0));
    REQUIRE(enabled_count > base_count);

    const size_t overlap_count = external_perimeter_count(
        run_perimeter_case(disabled,
                           {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_BELOW_AREA},
                           surface,
                           0,
                           {{"extra_perimeters_below_area", "100000"}}));
    REQUIRE(overlap_count > base_count);
    REQUIRE(overlap_count < enabled_count);
}
