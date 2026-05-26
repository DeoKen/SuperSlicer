#include <catch2/catch.hpp>

#include "perimeter_test_helpers.hpp"
#include "plugin_test_helpers.hpp"

namespace {
using namespace Slic3r;
using namespace Slic3r::Test::PerimeterPluginTests;
}

#ifdef SLIC3R_TEST_PYTHON_PLUGINS

TEST_CASE("Python SimplePerimeterGenerator publishes perimeter and fill output", "[plugins][perimeter][python]")
{
    // Python exercises the same STEP_PERIMETER host loop as the native simple
    // generator. The plugin should generate one external loop and return child
    // fill surfaces through run_region_group().
    REQUIRE(Slic3r::Test::Plugins::python_plugin_test_runtime_available());

    const DynamicPrintConfig config = perimeter_config({});
    const ExPolygon surface = rectangle_expolygon(-10., -10., 10., 10.);

    const PerimeterRunCapture generated =
        run_perimeter_case(config, {PYTHON_SIMPLE_PERIMETER_GENERATOR}, surface, 0);
    REQUIRE(external_perimeter_count(generated) > 0);
    REQUIRE(extrusion_length(generated.external_perimeters) > 0.);
    REQUIRE_FALSE(generated.fill_surfaces.empty());
    REQUIRE_FALSE(generated.fill_no_overlap_surfaces.empty());
    require_simple_generator_first_child_area_partition(generated, surface);
}

#endif
