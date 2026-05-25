#include <catch2/catch.hpp>

#include "perimeter_test_helpers.hpp"

namespace {
using namespace Slic3r;
using namespace Slic3r::Test::PerimeterPluginTests;
}

TEST_CASE("Remove gap fill on overhangs clips unsupported open paths", "[plugins][perimeter]")
{
    // This module edits open gap-fill paths, but the temporary simple perimeter
    // generator only emits loops. The test calls the module directly with a
    // synthetic node containing one open gap-fill path. With the setting off it
    // stays unchanged. With the setting on and no lower island, the unsupported
    // path is removed. With an overlapping enabled region, only the forbidden
    // side of the path is clipped.
    const DynamicPrintConfig disabled = perimeter_config({{"gap_fill_no_overhang", "0"}});
    const DynamicPrintConfig enabled = perimeter_config({{"gap_fill_no_overhang", "1"}});

    double disabled_length = 0.;
    const size_t disabled_count = run_remove_gap_fill_module(disabled, false, &disabled_length);
    REQUIRE(disabled_count == 1);
    REQUIRE(disabled_length > 0.);

    double enabled_length = 0.;
    const size_t enabled_count = run_remove_gap_fill_module(enabled, false, &enabled_length);
    REQUIRE(enabled_count == 0);
    REQUIRE(enabled_length == 0.);

    double overlap_length = 0.;
    const size_t overlap_count = run_remove_gap_fill_module(disabled, true, &overlap_length);
    REQUIRE(overlap_count > 0);
    REQUIRE(overlap_length > 0.);
    REQUIRE(overlap_length < disabled_length);
}
