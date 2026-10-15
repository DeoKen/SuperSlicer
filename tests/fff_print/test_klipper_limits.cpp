#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <string>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <regex>

#include "test_data.hpp"

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

TEST_CASE("Klipper limits: a G-code loaded without them keeps the stock estimate", "[KlipperLimits]") {
    // The viewer fills the config of a loaded G-code with the defaults, which have the Klipper limits: a G-code sliced
    // without them (stock PrusaSlicer, other slicers) must not get the Klipper planner.
    const std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, {
        { "gcode_flavor", "klipper" }, { "machine_limits_usage", "time_estimate_only" } });
    // The default Klipper limits: without the fix the G-code without them got these same defaults back.
    REQUIRE(gcode.find("; klipper_max_velocity = ") != std::string::npos);
    auto estimate_file = [](const std::string &text) {
        const boost::filesystem::path path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("klipper_%%%%%%.gcode");
        {
            boost::nowide::ofstream out(path.string(), std::ios::binary);
            out << text;
        }
        GCodeProcessor processor;
        processor.process_file(path.string());
        boost::filesystem::remove(path);
        return processor.get_result().print_statistics.modes[size_t(PrintEstimatedStatistics::ETimeMode::Normal)].time;
    };
    const double with_limits    = estimate_file(gcode);
    const double without_limits = estimate_file(std::regex_replace(gcode, std::regex("; klipper_[a-z_]+ = [^\n]*\n"), ""));
    // Same G-code: only the planner differs (Klipper limits vs. the machine limits).
    CHECK(with_limits > 0.);
    CHECK(without_limits > 0.);
    CHECK(with_limits != Approx(without_limits));
    // Loading a G-code sliced with the limits gives the time estimated while slicing (written in seconds).
    std::smatch m;
    REQUIRE(std::regex_search(gcode, m, std::regex(R"(; estimated printing time \(normal mode\) = ([^\n]*))")));
    double sliced = 0.;
    const std::string text = m[1].str();
    const std::regex units(R"((\d+)([dhms]))");
    for (std::sregex_iterator it(text.begin(), text.end(), units), end; it != end; ++ it)
        sliced += std::stod((*it)[1].str()) * ((*it)[2] == "d" ? 86400. : (*it)[2] == "h" ? 3600. : (*it)[2] == "m" ? 60. : 1.);
    CHECK(std::abs(with_limits - sliced) <= 1.);
}

// Klipper speed mode (filament) selecting the slow / normal / fast limits of the printer (doc section 22).

TEST_CASE("Klipper speed mode: the estimate uses the limits of the selected mode", "[KlipperSpeedMode]") {
    const std::string move = "G1 X300 F60000\n";
    // Normal limits: 300 mm/s, 1000 mm/s^2. Slow / fast default to 75 % / 125 % of them. 300 mm: long enough to cruise in every mode.
    SECTION("off (default): normal limits") {
        CHECK(estimate(klipper_config({}), move) == Approx(rest_to_rest(300., 300., 1000.)).epsilon(1e-4));
    }
    SECTION("normal") {
        CHECK(estimate(klipper_config({ { "klipper_speed_mode", "normal" } }), move) == Approx(rest_to_rest(300., 300., 1000.)).epsilon(1e-4));
    }
    SECTION("slow: 75 %") {
        CHECK(estimate(klipper_config({ { "klipper_speed_mode", "slow" } }), move) == Approx(rest_to_rest(300., 225., 750.)).epsilon(1e-4));
    }
    SECTION("fast: 125 %") {
        CHECK(estimate(klipper_config({ { "klipper_speed_mode", "fast" } }), move) == Approx(rest_to_rest(300., 375., 1250.)).epsilon(1e-4));
    }
    SECTION("slow, absolute values") {
        CHECK(estimate(klipper_config({ { "klipper_speed_mode", "slow" }, { "klipper_max_velocity_slow", "100" }, { "klipper_max_accel_slow", "2000" } }), move) ==
              Approx(rest_to_rest(300., 100., 2000.)).epsilon(1e-4));
    }
    SECTION("only velocity and accel change: square corner velocity and cruise ratio stay") {
        // Two moves with a 90 degree corner: same corner as with the normal limits scaled by hand.
        const std::string corner = "G1 X100 F60000\nG1 Y100\n";
        CHECK(estimate(klipper_config({ { "klipper_speed_mode", "slow" } }), corner) ==
              Approx(estimate(klipper_config({ { "klipper_max_velocity", "225" }, { "klipper_max_accel", "750" } }), corner)).epsilon(1e-6));
    }
    SECTION("SET_VELOCITY_LIMIT in the G-code still overrides") {
        CHECK(estimate(klipper_config({ { "klipper_speed_mode", "slow" } }), "SET_VELOCITY_LIMIT VELOCITY=200 ACCEL=2000\n" + move) ==
              Approx(rest_to_rest(300., 200., 2000.)).epsilon(1e-4));
    }
}

TEST_CASE("Klipper speed mode: the macro", "[KlipperSpeedMode]") {
    auto slice = [](std::initializer_list<std::pair<std::string, std::string>> values) {
        DynamicPrintConfig config = klipper_config(values);
        config.set_deserialize_strict({ { "start_gcode", "START_MACRO MODE={speed_mode}" } });
        return Test::slice({ Test::TestMesh::cube_20x20x20 }, config);
    };
    const std::pair<std::string, std::string> macro { "klipper_speed_mode_gcode", "SPEED_MODE TOGGLE={speed_mode}" };
    SECTION("off by default: nothing written, {speed_mode} empty") {
        const std::string gcode = slice({ macro });
        // Only in the config block at the end ("; klipper_speed_mode_gcode = ..."), not as a G-code line.
        CHECK(gcode.find("\nSPEED_MODE") == std::string::npos);
        CHECK(gcode.find("START_MACRO MODE=\n") != std::string::npos);
    }
    SECTION("slow: written once, after the start G-code and before the first layer") {
        const std::string gcode = slice({ { "klipper_speed_mode", "slow" }, macro });
        const size_t pos = gcode.find("\nSPEED_MODE TOGGLE=S\n");
        REQUIRE(pos != std::string::npos);
        CHECK(gcode.find("\nSPEED_MODE TOGGLE=", pos + 1) == std::string::npos);
        CHECK(gcode.find("START_MACRO MODE=S\n") < pos);
        CHECK(pos < gcode.find(";LAYER_CHANGE"));
    }
    SECTION("fast and normal letters") {
        CHECK(slice({ { "klipper_speed_mode", "fast" }, macro }).find("\nSPEED_MODE TOGGLE=F\n") != std::string::npos);
        CHECK(slice({ { "klipper_speed_mode", "normal" }, macro }).find("\nSPEED_MODE TOGGLE=N\n") != std::string::npos);
    }
    SECTION("mode selected, empty macro: nothing written") {
        const std::string gcode = slice({ { "klipper_speed_mode", "slow" } });
        CHECK(gcode.find("\nSPEED_MODE") == std::string::npos);
    }
}

TEST_CASE("Klipper speed mode: sliced and reloaded G-code", "[KlipperSpeedMode]") {
    auto estimated = [](const std::string &gcode) {
        std::smatch m;
        REQUIRE(std::regex_search(gcode, m, std::regex(R"(; estimated printing time \(normal mode\) = ([^\n]*))")));
        double t = 0.;
        const std::string text = m[1].str();
        const std::regex units(R"((\d+)([dhms]))");
        for (std::sregex_iterator it(text.begin(), text.end(), units), end; it != end; ++ it)
            t += std::stod((*it)[1].str()) * ((*it)[2] == "d" ? 86400. : (*it)[2] == "h" ? 3600. : (*it)[2] == "m" ? 60. : 1.);
        return t;
    };
    // Low limits so that the speed mode matters.
    DynamicPrintConfig config = klipper_config({ { "klipper_max_velocity", "60" }, { "klipper_max_accel", "500" } });
    const std::string normal = Test::slice({ Test::TestMesh::cube_20x20x20 }, config);
    config.set_deserialize_strict({ { "klipper_speed_mode", "slow" } });
    const std::string slow = Test::slice({ Test::TestMesh::cube_20x20x20 }, config);
    CHECK(estimated(slow) > estimated(normal) + 1.);
    // Reloading the G-code (viewer) gives the same estimate: the speed mode settings are in its config block.
    const boost::filesystem::path path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("speed_mode_%%%%%%.gcode");
    {
        boost::nowide::ofstream out(path.string(), std::ios::binary);
        out << slow;
    }
    GCodeProcessor processor;
    processor.process_file(path.string());
    boost::filesystem::remove(path);
    CHECK(std::abs(processor.get_result().print_statistics.modes[size_t(PrintEstimatedStatistics::ETimeMode::Normal)].time - estimated(slow)) <= 1.);
}
