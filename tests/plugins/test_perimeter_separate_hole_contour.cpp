#include <catch2/catch.hpp>

#include "perimeter_test_helpers.hpp"

namespace {
using namespace Slic3r;
using namespace Slic3r::Test::PerimeterPluginTests;
}

TEST_CASE("Separate hole contour module limits hole loops", "[plugins][perimeter]")
{
    // The input island has one contour and one hole. When perimeters_hole is
    // disabled, the module is inert. When enabled with fewer hole perimeters
    // than contour perimeters, hole loops should stop before contour loops.
    const ExPolygon surface = rectangle_with_hole_expolygon();
    const DynamicPrintConfig disabled = perimeter_config({
        {"perimeters", "3"},
        {"perimeters_hole", "!0"}
    });
    const DynamicPrintConfig enabled = perimeter_config({
        {"perimeters", "3"},
        {"perimeters_hole", "1"}
    });

    const PerimeterRunCapture disabled_run =
        run_perimeter_case(disabled, {SIMPLE_PERIMETER_GENERATOR, SEPARATE_HOLE_CONTOUR}, surface, 0);
    const PerimeterRunCapture enabled_run =
        run_perimeter_case(enabled, {SIMPLE_PERIMETER_GENERATOR, SEPARATE_HOLE_CONTOUR}, surface, 0);

    REQUIRE(external_perimeter_count(disabled_run) == 6);
    REQUIRE(external_perimeter_count(enabled_run) == 3);
    REQUIRE(count_loops_with_role(external_perimeters(enabled_run), elrHole) <
            count_loops_with_role(external_perimeters(enabled_run), elrDefault));
    require_leaf_fill_area_consistency(disabled_run);
    require_leaf_fill_area_consistency(enabled_run);

    const PerimeterRunCapture overlap_run =
        run_perimeter_case(disabled,
                           {SIMPLE_PERIMETER_GENERATOR, SEPARATE_HOLE_CONTOUR},
                           surface,
                           0,
                           {{"perimeters_hole", "1"}});
    REQUIRE(external_perimeter_count(overlap_run) == external_perimeter_count(disabled_run));
    require_leaf_fill_area_consistency(overlap_run);
}
