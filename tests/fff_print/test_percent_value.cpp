#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/PercentValue.hpp"
#include "libslic3r/PrintConfig.hpp"

using namespace Slic3r;
using Catch::Approx;

// The absolute value of a percentage shown next to a field taking a value or a percentage (settings tabs).
TEST_CASE("Absolute value of a percentage", "[PercentValue]") {
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({ { "layer_height", "0.2" }, { "first_layer_height", "0.25" }, { "nozzle_diameter", "0.4" },
                                    { "extrusion_width", "225%" }, { "first_layer_extrusion_width", "200%" },
                                    { "perimeter_extrusion_width", "0.45" },
                                    { "perimeter_speed", "60" }, { "external_perimeter_speed", "50%" },
                                    { "infill_speed", "80" }, { "solid_infill_speed", "50%" }, { "top_solid_infill_speed", "50%" },
                                    { "default_acceleration", "5000" }, { "external_perimeter_acceleration", "50%" } });
    // Widths: of the layer height, as Flow computes them (not of the nozzle).
    CHECK(*percent_absolute_value(config, "extrusion_width") == Approx(0.45));
    CHECK(*percent_absolute_value(config, "first_layer_extrusion_width") == Approx(0.5));
    // Chains of percentages: top solid infill 50 % of solid infill 50 % of infill 80 mm/s.
    CHECK(*percent_absolute_value(config, "external_perimeter_speed") == Approx(30.));
    CHECK(*percent_absolute_value(config, "top_solid_infill_speed") == Approx(20.));
    CHECK(*percent_absolute_value(config, "external_perimeter_acceleration") == Approx(2500.));
    // Absolute values: their percentage of the base (0.45 mm = 225 % of 0.2 mm layers; 2500 = 50 % of 5000).
    CHECK(! percent_absolute_value(config, "perimeter_extrusion_width"));
    const auto hint = percent_value_hint(config, "perimeter_extrusion_width");
    REQUIRE(hint);
    CHECK(hint->percent);
    CHECK(hint->value == Approx(225.));
    config.set_deserialize_strict("perimeter_acceleration", "2500");
    CHECK(percent_value_hint(config, "perimeter_acceleration")->value == Approx(50.));
    // A percentage: its absolute value.
    CHECK(! percent_value_hint(config, "extrusion_width")->percent);
    // No hint: 0 (auto / disabled), a setting without a base (perimeter speed is mm/s only), unknown settings.
    config.set_deserialize_strict("infill_acceleration", "0");
    CHECK(! percent_value_hint(config, "infill_acceleration"));
    CHECK(! percent_value_hint(config, "perimeter_speed"));
    CHECK(! percent_value_hint(config, "no_such_setting"));
    // The unit from the side text.
    CHECK(percent_value_unit(*print_config_def.get("external_perimeter_speed")) == "mm/s");
    CHECK(percent_value_unit(*print_config_def.get("extrusion_width")) == "mm");
}
