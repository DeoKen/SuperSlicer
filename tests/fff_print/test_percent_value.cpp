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

// Settings without a ratio_over whose percentage the slicer resolves in code.
TEST_CASE("Bases resolved in code", "[PercentValue]") {
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({ { "layer_height", "0.2" }, { "nozzle_diameter", "0.4,0.6" }, { "extrusion_width", "0.45" },
                                    { "external_perimeter_extrusion_width", "0.4" },
                                    { "seam_gap_distance", "15%" }, { "seam_notch_all", "10%" }, { "seam_notch_inner", "0.04" },
                                    { "min_bead_width", "85%" }, { "wall_transition_length", "0.4" },
                                    { "infill_speed", "100" }, { "solid_infill_speed", "80" }, { "over_bridge_speed", "50%" },
                                    { "first_layer_infill_speed", "25%" } });
    for (const char *key : { "seam_gap_distance", "seam_notch_all", "min_bead_width", "over_bridge_speed" })
        CHECK(percent_has_base(key));
    CHECK(! percent_has_base("avoid_crossing_perimeters_max_detour"));  // % of each travel: no fixed base
    CHECK(! percent_has_base("first_layer_speed"));                     // scales every speed
    // Seam gap: of the nozzle diameter (the first extruder).
    CHECK(*percent_absolute_value(config, "seam_gap_distance") == Approx(0.06));
    // Seam notch: of the external perimeter width.
    CHECK(*percent_absolute_value(config, "seam_notch_all") == Approx(0.04));
    CHECK(percent_value_hint(config, "seam_notch_inner")->value == Approx(10.));
    // External perimeter width 0 = extrusion width (as Flow), then auto (1.125 x nozzle).
    config.set_deserialize_strict("external_perimeter_extrusion_width", "0");
    CHECK(*percent_absolute_value(config, "seam_notch_all") == Approx(0.045));
    config.set_deserialize_strict("extrusion_width", "0");
    CHECK(*percent_absolute_value(config, "seam_notch_all") == Approx(0.045));
    // Arachne: of the smallest nozzle.
    CHECK(*percent_absolute_value(config, "min_bead_width") == Approx(0.34));
    CHECK(percent_value_hint(config, "wall_transition_length")->value == Approx(100.));
    // Over bridge / first layer infill: of the solid infill speed.
    CHECK(*percent_absolute_value(config, "over_bridge_speed") == Approx(40.));
    CHECK(*percent_absolute_value(config, "first_layer_infill_speed") == Approx(20.));
}
