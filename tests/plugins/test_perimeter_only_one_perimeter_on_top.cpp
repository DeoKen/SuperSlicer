#include <catch2/catch.hpp>

#include "perimeter_test_helpers.hpp"

namespace {
using namespace Slic3r;
using namespace Slic3r::Test::PerimeterPluginTests;
}

TEST_CASE("Only one perimeter on top limits top branches", "[plugins][perimeter]")
{
    // The top rule is applied after the first perimeter is generated on areas
    // that have no upper island coverage. ExtraPerimeterCount creates a
    // multi-ring baseline; this module then stops top child branches globally
    // or only inside the region where only_one_perimeter_top is enabled.
    const ExPolygon surface = rectangle_expolygon(-10., -10., 10., 10.);
    const DynamicPrintConfig multi = perimeter_config({
        {"extra_perimeters_count", "3"},
        {"only_one_perimeter_top", "0"}
    });
    const DynamicPrintConfig limited = perimeter_config({
        {"extra_perimeters_count", "3"},
        {"only_one_perimeter_top", "1"}
    });

    PreparedPerimeterPrint prepared;
    prepare_cube_print(prepared, multi);
    const size_t top_idx = layer_index_for_top(prepared.print.object(0));

    // Baseline on the top layer: with only_one_perimeter_top disabled, the
    // extra-perimeter module keeps the tree going after the first ring.
    const PerimeterRunCapture multi_run =
        run_perimeter_case(multi,
                           {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT, ONLY_ONE_PERIMETER_ON_TOP},
                           surface,
                           top_idx);

    // Global top case: with the setting enabled everywhere, the module stops
    // all child branches after the first top perimeter.
    const PerimeterRunCapture limited_run =
        run_perimeter_case(limited,
                           {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT, ONLY_ONE_PERIMETER_ON_TOP},
                           surface,
                           top_idx);
    const size_t multi_count = external_perimeter_count(multi_run);
    const size_t limited_count = external_perimeter_count(limited_run);
    const double multi_length = extrusion_length(multi_run.external_perimeters);
    const double limited_length = extrusion_length(limited_run.external_perimeters);
    REQUIRE(limited_count < multi_count);
    REQUIRE(limited_count > 0);
    REQUIRE(limited_length < multi_length);

    // Region-specific top case: enable the setting only on the left half of
    // the top island. The first full perimeter still crosses the split, the
    // active-left branch stops, and the inactive-right branch keeps generating
    // the requested extra rings.
    const ExPolygon left_half = rectangle_expolygon(-10., -10., 0., 10.);
    const PerimeterRunCapture local_run =
        run_perimeter_case(multi,
                           {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT, ONLY_ONE_PERIMETER_ON_TOP},
                           surface,
                           top_idx,
                           {{"only_one_perimeter_top", "1"}},
                           &left_half);
    const size_t local_count = external_perimeter_count(local_run);
    const VerticalSplitCounts split_counts = vertical_split_counts(local_run.external_perimeters, scale_i(0.));
    REQUIRE(split_counts.crossing > 0);
    REQUIRE(split_counts.left_only == 0);
    REQUIRE(split_counts.right_only >= 3);
    REQUIRE(local_count == split_counts.crossing + split_counts.left_only + split_counts.right_only);
}
