#include <catch2/catch.hpp>

#include "perimeter_test_helpers.hpp"

namespace {
using namespace Slic3r;
using namespace Slic3r::Test::PerimeterPluginTests;
}

TEST_CASE("Extra perimeter count module adds requested loops", "[plugins][perimeter]")
{
    // The simple no/yes check uses one uniform region. The overlap check adds a
    // second region covering the center of the island and verifies that the
    // module can request extra perimeters only where that region applies.
    const ExPolygon surface = rectangle_expolygon(-10., -10., 10., 10.);
    const DynamicPrintConfig disabled = perimeter_config({{"extra_perimeters_count", "0"}});
    const DynamicPrintConfig enabled = perimeter_config({{"extra_perimeters_count", "2"}});

    const size_t base_count = external_perimeter_count(
        run_perimeter_case(disabled, {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT}, surface, 0));
    const size_t enabled_count = external_perimeter_count(
        run_perimeter_case(enabled, {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT}, surface, 0));
    REQUIRE(enabled_count > base_count);

    const size_t overlap_count = external_perimeter_count(
        run_perimeter_case(disabled,
                           {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT},
                           surface,
                           0,
                           {{"extra_perimeters_count", "1"}}));
    REQUIRE(overlap_count > base_count);
    REQUIRE(overlap_count < enabled_count);
}
