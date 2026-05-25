#include <catch2/catch.hpp>

#include "perimeter_test_helpers.hpp"

namespace {
using namespace Slic3r;
using namespace Slic3r::Test::PerimeterPluginTests;
}

TEST_CASE("Extra perimeter count module adds requested loops", "[plugins][perimeter]")
{
    // Uniform region: with one base perimeter and two extra perimeters, the
    // module should produce exactly three nested external loops.
    const ExPolygon area = rectangle_expolygon(-10., -10., 10., 10.);
    const DynamicPrintConfig disabled = perimeter_config({{"extra_perimeters_count", "0"}});
    const DynamicPrintConfig enabled = perimeter_config({{"extra_perimeters_count", "2"}});

    const size_t base_count = external_perimeter_count(
        run_perimeter_case(disabled, {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT}, area, 0));
    const size_t enabled_count = external_perimeter_count(
        run_perimeter_case(enabled, {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT}, area, 0));
    REQUIRE(base_count == 1);
    REQUIRE(enabled_count == 3);

    // Region-local case: the left-side region requests one extra loop.
    // We should keep the full-area base loop crossing x=0, then add exactly one
    // left-only loop. If the module applied the override globally, this would
    // still have two loops, but both would cross the split line.
    const ExPolygon left_override_area = rectangle_expolygon(-9., -9., -2., 9.);
    const PerimeterRunCapture overlap_run =
        run_perimeter_case(disabled,
                           {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT},
                           area,
                           0,
                           {{"extra_perimeters_count", "1"}},
                           &left_override_area);
    REQUIRE(external_perimeter_count(overlap_run) == 2);
    const VerticalSplitCounts overlap_split =
        vertical_split_counts(external_perimeters(overlap_run), 0);
    REQUIRE(overlap_split.crossing == 1);
    REQUIRE(overlap_split.left_only == 1);
    REQUIRE(overlap_split.right_only == 0);

    // Zero base perimeter: extra_perimeters_count still asks for real loops.
    const DynamicPrintConfig no_base =
        perimeter_config({{"perimeters", "0"}, {"extra_perimeters_count", "2"}});
    const size_t no_base_count = external_perimeter_count(
        run_perimeter_case(no_base, {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT}, area, 0));
    REQUIRE(no_base_count == 2);
}
