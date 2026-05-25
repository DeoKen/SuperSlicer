#include <catch2/catch.hpp>

#include "perimeter_test_helpers.hpp"

namespace {
using namespace Slic3r;
using namespace Slic3r::Test::PerimeterPluginTests;
}

TEST_CASE("SimplePerimeterGenerator publishes perimeter and fill output", "[plugins][perimeter]")
{
    // This is the smoke test for the full STEP_PERIMETER payload. With no
    // STEP_PERIMETER plugin active, nothing should be published. With the
    // simple generator active, it should create at least one external loop and
    // both fill-surface collections for the island.
    const DynamicPrintConfig config = perimeter_config({});
    const ExPolygon surface = rectangle_expolygon(-10., -10., 10., 10.);

    const PerimeterRunCapture inactive =
        run_perimeter_case(config, {}, surface, 0);
    REQUIRE(external_perimeter_count(inactive) == 0);

    const PerimeterRunCapture generated =
        run_perimeter_case(config, {SIMPLE_PERIMETER_GENERATOR}, surface, 0);
    REQUIRE(external_perimeter_count(generated) > 0);
    REQUIRE_FALSE(generated.fill_surfaces.empty());
    REQUIRE_FALSE(generated.fill_no_overlap_surfaces.empty());
    require_simple_generator_first_child_area_partition(generated, surface);
}
