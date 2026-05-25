#include <catch2/catch.hpp>

#include "perimeter_test_helpers.hpp"

namespace {
using namespace Slic3r;
using namespace Slic3r::Test::PerimeterPluginTests;
}

TEST_CASE("Only one perimeter first layer limits first-layer branches", "[plugins][perimeter]")
{
    // The first-layer rule is applied after the first perimeter is generated:
    // the first perimeter is still emitted, then child branches are stopped if
    // their region has only_one_perimeter_first_layer enabled.
    const ExPolygon surface = rectangle_expolygon(-10., -10., 10., 10.);
    const DynamicPrintConfig multi = perimeter_config({
        {"extra_perimeters_count", "3"},
        {"only_one_perimeter_first_layer", "0"}
    });
    const DynamicPrintConfig limited = perimeter_config({
        {"extra_perimeters_count", "3"},
        {"only_one_perimeter_first_layer", "1"}
    });

    // Baseline: with only_one_perimeter_first_layer disabled, ExtraPerimeterCount
    // asks for three extra rings. external_perimeter_count() counts generated
    // perimeter leaf polylines in the PERIMETERS bucket, not only the single
    // semantic external perimeter of the rectangle.
    const PerimeterRunCapture multi_run =
        run_perimeter_case(multi,
                           {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT, ONLY_ONE_PERIMETER_FIRST_LAYER},
                           surface,
                           0);

    // Global first-layer case: with the setting enabled everywhere, the module
    // stops the tree after the first perimeter. The count and total length must
    // therefore be lower than the multi-ring baseline.
    const PerimeterRunCapture limited_run =
        run_perimeter_case(limited,
                           {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT, ONLY_ONE_PERIMETER_FIRST_LAYER},
                           surface,
                           0);
    const size_t multi_count = external_perimeter_count(multi_run);
    const size_t limited_count = external_perimeter_count(limited_run);
    const double multi_length = extrusion_length(multi_run.external_perimeters);
    const double limited_length = extrusion_length(limited_run.external_perimeters);
    REQUIRE(limited_count < multi_count);
    REQUIRE(limited_count > 0);
    REQUIRE(limited_length < multi_length);

    // Non-first-layer case: the same setting must be ignored on later layers.
    // The number of generated perimeter polylines stays equal to the baseline.
    PreparedPerimeterPrint prepared;
    prepare_cube_print(prepared, limited);
    const size_t non_first_idx = layer_index_for_odd_layer(prepared.print.object(0));
    const PerimeterRunCapture non_first_run =
        run_perimeter_case(limited,
                           {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT, ONLY_ONE_PERIMETER_FIRST_LAYER},
                           surface,
                           non_first_idx);
    const size_t non_first_count = external_perimeter_count(non_first_run);
    REQUIRE(non_first_count == multi_count);

    // Region-specific case: enable the setting only on the left half of the
    // island. After the first full perimeter, the active-left branch stops
    // while the inactive-right branch keeps the three requested extra rings.
    const ExPolygon left_half = rectangle_expolygon(-10., -10., 0., 10.);
    const PerimeterRunCapture overlap_run =
        run_perimeter_case(multi,
                           {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT, ONLY_ONE_PERIMETER_FIRST_LAYER},
                           surface,
                           0,
                           {{"only_one_perimeter_first_layer", "1"}},
                           &left_half);
    const size_t overlap_count = external_perimeter_count(overlap_run);
    const VerticalSplitCounts split_counts = vertical_split_counts(overlap_run.external_perimeters, scale_i(0.));
    REQUIRE(split_counts.crossing > 0);
    REQUIRE(split_counts.left_only == 0);
    REQUIRE(split_counts.right_only >= 3);
    REQUIRE(overlap_count == split_counts.crossing + split_counts.left_only + split_counts.right_only);
}
