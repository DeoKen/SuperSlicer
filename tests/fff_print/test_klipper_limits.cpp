#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <string>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"

using namespace Slic3r;
using Catch::Approx;

// Klipper limits (klipper_* printer options) for the time estimate: the estimator plans the moves as Klipper's
// look-ahead does (klippy/toolhead.py). Expected times are computed by hand from Klipper's formulas.

namespace {

DynamicPrintConfig klipper_config(std::initializer_list<std::pair<std::string, std::string>> values)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({ { "gcode_flavor", "klipper" }, { "machine_limits_usage", "time_estimate_only" },
                                    { "klipper_max_velocity", "300" }, { "klipper_max_accel", "1000" },
                                    { "klipper_minimum_cruise_ratio", "0" }, { "klipper_square_corner_velocity", "5" },
                                    { "klipper_max_z_velocity", "20" }, { "klipper_max_z_accel", "300" } });
    for (const auto &[key, value] : values)
        config.set_deserialize_strict(key, value);
    return config;
}

// Estimated print time (normal mode) of a G-code snippet, processed as during slicing.
double estimate(const DynamicPrintConfig &config, const std::string &gcode)
{
    PrintConfig print_config;
    print_config.apply(config, true);
    GCodeProcessor processor;
    processor.apply_config(print_config);
    processor.initialize("klipper_limits.gcode");
    processor.initialize_result_moves();
    processor.process_buffer("G90\nM83\n" + gcode);
    processor.finalize(false);
    return processor.get_result().print_statistics.modes[size_t(PrintEstimatedStatistics::ETimeMode::Normal)].time;
}

// Move from rest to rest: accelerate, cruise, decelerate.
double rest_to_rest(double distance, double velocity, double accel)
{
    const double ramp = velocity * velocity / accel; // acceleration + deceleration distance
    if (ramp > distance)
        return 2. * std::sqrt(distance / accel);
    return 2. * velocity / accel + (distance - ramp) / velocity;
}

} // namespace

TEST_CASE("Klipper limits: velocity and acceleration", "[KlipperLimits]") {
    // max_velocity 300 caps F 30000 (500 mm/s).
    CHECK(estimate(klipper_config({}), "G1 X100 F30000\n") == Approx(rest_to_rest(100., 300., 1000.)).epsilon(1e-4));
}

TEST_CASE("Klipper limits: M204 and SET_VELOCITY_LIMIT replace the limits", "[KlipperLimits]") {
    // M204 S is not capped by max_accel with Klipper.
    CHECK(estimate(klipper_config({}), "M204 S4000\nG1 X100 F30000\n") == Approx(rest_to_rest(100., 300., 4000.)).epsilon(1e-4));
    CHECK(estimate(klipper_config({}), "SET_VELOCITY_LIMIT VELOCITY=200 ACCEL=2000\nG1 X100 F30000\n") ==
          Approx(rest_to_rest(100., 200., 2000.)).epsilon(1e-4));
    CHECK(estimate(klipper_config({}), "set_velocity_limit accel=2000 square_corner_velocity=1\nG1 X100 F30000\n") ==
          Approx(rest_to_rest(100., 300., 2000.)).epsilon(1e-4));
}

TEST_CASE("Klipper limits: Z velocity and acceleration", "[KlipperLimits]") {
    CHECK(estimate(klipper_config({}), "G1 Z10 F6000\n") == Approx(rest_to_rest(10., 20., 300.)).epsilon(1e-4));
}

TEST_CASE("Klipper limits: minimum cruise ratio", "[KlipperLimits]") {
    // A 2 mm move from rest to rest at 11000 mm/s^2: the top speed is sqrt(accel * (1 - ratio) * distance)
    // and ratio * distance is traveled at that speed.
    auto expected = [](double ratio) {
        const double accel = 11000., distance = 2.;
        const double v = std::sqrt(accel * (1. - ratio) * distance);
        return 2. * v / accel + ratio * distance / v;
    };
    for (double ratio : { 0., 0.5, 0.8 }) {
        INFO("ratio " << ratio);
        const DynamicPrintConfig config = klipper_config({ { "klipper_max_accel", "11000" }, { "klipper_minimum_cruise_ratio", std::to_string(ratio) } });
        CHECK(estimate(config, "G1 X2 F18000\n") == Approx(expected(ratio)).epsilon(1e-3));
    }
    // 27.0, 28.6 and 36.2 ms.
    CHECK(expected(0.5) / expected(0.) == Approx(1.0607).epsilon(1e-3));
}

TEST_CASE("Klipper limits: square corner velocity", "[KlipperLimits]") {
    // A 90 degree corner is taken at square_corner_velocity.
    const double accel = 1000., v = 300., d = 100., scv = 5.;
    const double one_move = v / accel + (d - v * v / (2. * accel) - (v * v - scv * scv) / (2. * accel)) / v + (v - scv) / accel;
    CHECK(estimate(klipper_config({}), "G1 X100 F30000\nG1 Y100\n") == Approx(2. * one_move).epsilon(1e-4));
}

TEST_CASE("Klipper limits: off when ignored or with another flavor", "[KlipperLimits]") {
    // Two moves: the stock estimator does not time a single move.
    const std::string gcode = "G1 X100 F30000\nG1 Y100\n";
    const double klipper = estimate(klipper_config({}), gcode);
    CHECK(estimate(klipper_config({ { "machine_limits_usage", "ignore" } }), gcode) != Approx(klipper));
    CHECK(estimate(klipper_config({ { "gcode_flavor", "marlin2" } }), gcode) != Approx(klipper));
}
