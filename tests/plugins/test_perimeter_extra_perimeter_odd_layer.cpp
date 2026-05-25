#include <catch2/catch.hpp>

#include "perimeter_test_helpers.hpp"

namespace {
using namespace Slic3r;
using namespace Slic3r::Test::PerimeterPluginTests;
}

TEST_CASE("Extra perimeter odd layer module is layer-parity dependent", "[plugins][perimeter]")
{
    // This module must not fire on even layer ids. On odd layer ids it adds one
    // perimeter. With overlapping region settings, only the region-local child
    // branch receives that extra pass.
    const ExPolygon surface = rectangle_expolygon(-10., -10., 10., 10.);
    const DynamicPrintConfig disabled = perimeter_config({{"extra_perimeters_odd_layers", "0"}});
    const DynamicPrintConfig enabled = perimeter_config({{"extra_perimeters_odd_layers", "1"}});

    const size_t even_count = external_perimeter_count(
        run_perimeter_case(enabled, {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_ODD_LAYER}, surface, 0));

    PreparedPerimeterPrint prepared;
    prepare_cube_print(prepared, enabled);
    const size_t odd_idx = layer_index_for_odd_layer(prepared.print.object(0));
    const size_t odd_count = external_perimeter_count(
        run_perimeter_case(enabled, {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_ODD_LAYER}, surface, odd_idx));
    REQUIRE(odd_count > even_count);

    const size_t overlap_count = external_perimeter_count(
        run_perimeter_case(disabled,
                           {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_ODD_LAYER},
                           surface,
                           odd_idx,
                           {{"extra_perimeters_odd_layers", "1"}}));
    REQUIRE(overlap_count > even_count);
    REQUIRE(overlap_count != odd_count);
}
