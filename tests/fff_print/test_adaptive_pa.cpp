// Adaptive pressure advance (doc section 46), ported from OrcaSlicer.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/GCode/AdaptivePA.hpp"
#include "libslic3r/PressureCalibration.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;
using Catch::Matchers::WithinAbs;

namespace {

// Two accelerations, two flows each: 0.04 / 0.03 at 3000, 0.03 / 0.02 at 10000 (flow 4 / 8 mmÃ‚Â³/s).
const std::string model_2x2 = "0.04,4,3000\n0.03,8,3000\n0.03,4,10000\n0.02,8,10000\n";

std::vector<std::string> lines_of(const std::string &gcode)
{
    std::vector<std::string> out;
    std::istringstream       ss(gcode);
    for (std::string line; std::getline(ss, line);)
        out.emplace_back(line);
    return out;
}

std::vector<std::string> pa_commands(const std::string &gcode)
{
    std::vector<std::string> out;
    for (const std::string &line : lines_of(gcode))
        if (line.rfind("SET_PRESSURE_ADVANCE", 0) == 0)
            out.emplace_back(line);
    return out;
}

AdaptivePAProcessor processor(const std::string &model, double static_pa = 0.025, bool comments = false)
{
    AdaptivePAProcessor::Extruder e;
    e.static_pa = static_pa;
    REQUIRE(e.model.parse(model).empty());
    e.adaptive = e.model.valid();
    return AdaptivePAProcessor({ e }, comments);
}

std::string tag(double mm3_per_mm, unsigned accel, bool role_change = true, bool overhang = false)
{
    return AdaptivePAProcessor::tag(0, mm3_per_mm, accel, false, role_change, overhang);
}

// The pressure advance active at each extrusion line inside a ;TYPE: block, after the first layer.
std::map<std::string, std::set<std::string>> pa_per_type(const std::string &gcode)
{
    std::map<std::string, std::set<std::string>> out;
    std::string pa = "none", type;
    int         layers = 0;
    for (const std::string &line : lines_of(gcode)) {
        if (line.rfind(";LAYER_CHANGE", 0) == 0)
            ++ layers;
        else if (line.rfind(";TYPE:", 0) == 0)
            type = line.substr(6);
        else if (line.rfind("SET_PRESSURE_ADVANCE ADVANCE=", 0) == 0)
            pa = line.substr(29);
        else if (layers >= 2 && line.rfind("G1 ", 0) == 0 && line.find('E') != std::string::npos && line.find('X') != std::string::npos)
            out[type].insert(pa);
    }
    return out;
}

DynamicPrintConfig klipper_config()
{
    return DynamicPrintConfig::full_print_config_with({
        { "gcode_flavor",             "klipper" },
        { "use_relative_e_distances", true },
        { "cooling",                  { 0 } },
        { "start_filament_gcode",     { "M117 FILAMENT START" } },
        { "nozzle_diameter",          { 0.4 } },
    });
}

} // namespace

TEST_CASE("Adaptive PA: PCHIP as OrcaSlicer's", "[AdaptivePA]")
{
    // 2 points: a line, the end values outside.
    PchipInterpolator line({ 4., 8. }, { 0.04, 0.03 });
    CHECK_THAT(line(6.), WithinAbs(0.035, 1e-9));
    CHECK_THAT(line(1.), WithinAbs(0.04, 1e-9));
    CHECK_THAT(line(20.), WithinAbs(0.03, 1e-9));
    // 3 points, computed by hand with OrcaSlicer's formulas: h = 2, 4; delta = -0.005, -0.0025; inner slope
    // (w1 + w2) / (w1 / d0 + w2 / d1) = 18 / (10 / -0.005 + 8 / -0.0025) = -0.00346154; at x = 3 (t = 0.5):
    // 0.5 * 0.05 + 0.125 * 2 * -0.005 + 0.5 * 0.04 - 0.125 * 2 * -0.00346154 = 0.04461538.
    PchipInterpolator curve({ 2., 4., 8. }, { 0.05, 0.04, 0.03 });
    CHECK_THAT(curve(3.), WithinAbs(0.0446154, 1e-6));
    CHECK_THAT(curve(4.), WithinAbs(0.04, 1e-9));
    // Monotone: between the points, never past them.
    for (double x = 2.; x <= 8.; x += 0.1) {
        CHECK(curve(x) <= 0.05 + 1e-12);
        CHECK(curve(x) >= 0.03 - 1e-12);
    }
    // Points with the same x are averaged (OrcaSlicer would divide by 0).
    PchipInterpolator dup({ 4., 4., 8. }, { 0.04, 0.05, 0.03 });
    CHECK(dup.size() == 2);
    CHECK_THAT(dup(4.), WithinAbs(0.045, 1e-9));
}

TEST_CASE("Adaptive PA: the model", "[AdaptivePA]")
{
    AdaptivePAInterpolator model;
    REQUIRE(model.parse(model_2x2).empty());
    REQUIRE(model.valid());
    // Over the flow per acceleration, then over the accelerations; rounded to 3 decimals.
    CHECK_THAT(model(6., 3000.), WithinAbs(0.035, 1e-9));
    CHECK_THAT(model(6., 10000.), WithinAbs(0.025, 1e-9));
    CHECK_THAT(model(6., 6500.), WithinAbs(0.03, 1e-9));
    // Outside of the measurements: the nearest ones (no extrapolation).
    CHECK_THAT(model(1., 1000.), WithinAbs(0.04, 1e-9));
    CHECK_THAT(model(50., 50000.), WithinAbs(0.02, 1e-9));

    SECTION("headers, empty lines, spaces and OrcaSlicer's zero placeholder are skipped") {
        AdaptivePAInterpolator m;
        CHECK(m.parse("; nozzle 0.4\n\n 0.04 , 4 , 3000\r\n0,0,0\n0.03,8,3000\n").empty());
        CHECK_THAT(m(6., 3000.), WithinAbs(0.035, 1e-9));
    }
    SECTION("a single acceleration") {
        AdaptivePAInterpolator m;
        CHECK(m.parse("0.04,4,3000\n0.03,8,3000\n").empty());
        CHECK_THAT(m(6., 20000.), WithinAbs(0.035, 1e-9));
    }
    SECTION("an acceleration with one point is ignored (OrcaSlicer)") {
        AdaptivePAInterpolator m;
        CHECK(m.parse("0.04,4,3000\n0.03,8,3000\n0.01,4,10000\n").empty());
        CHECK_THAT(m(6., 10000.), WithinAbs(0.035, 1e-9));
        CHECK(! m.parse("0.04,4,3000\n").empty());
        CHECK(! m.valid());
    }
    SECTION("errors") {
        AdaptivePAInterpolator m;
        CHECK(! m.parse("0.04,4\n").empty());
        CHECK(! m.parse("0.04,4,abc\n").empty());
        CHECK(! m.parse("0.04,0,3000\n0.03,8,3000\n").empty());
        CHECK(m.parse("").empty());
        CHECK(! m.valid());
    }
}

TEST_CASE("Adaptive PA: the model lines of a nozzle", "[AdaptivePA]")
{
    const std::string model = "0.05,4,3000\n0.04,8,3000\n; nozzle 0.4\n0.03,4,3000\n0.02,8,3000\n; nozzle 0.6\n0.01,4,3000\n0.005,8,3000\n";
    CHECK(adaptive_pa_model_for_nozzle(model, 0.4) == "0.03,4,3000\n0.02,8,3000\n");
    CHECK(adaptive_pa_model_for_nozzle(model, 0.6) == "0.01,4,3000\n0.005,8,3000\n");
    // No section: the lines before the first one.
    CHECK(adaptive_pa_model_for_nozzle(model, 0.5) == "0.05,4,3000\n0.04,8,3000\n");
    CHECK(adaptive_pa_model_for_nozzle("; nozzle 0.4\n0.03,4,3000\n", 0.5).empty());
}

TEST_CASE("Adaptive PA: the processor", "[AdaptivePA]")
{
    SECTION("per island, at its speed; only changes are written; the tags are removed") {
        AdaptivePAProcessor p = processor(model_2x2);
        const std::string out = p.process_layer(
            "G1 X0 Y0 F12000\n" + tag(0.1, 3000) +
            "G1 F2400\nG1 X10 Y0 E1\n"           // 40 mm/s x 0.1 = flow 4: 0.04
            "G1 X10 Y10 F12000\n" + tag(0.1, 3000) +
            "G1 F4800\nG1 X20 Y10 E1\n"          // flow 8: 0.03
            "G1 X30 Y10 F12000\n" + tag(0.1, 3000, false) +
            "G1 F4800\nG1 X40 Y10 E1\n");        // again 0.03: nothing
        CHECK(pa_commands(out) == std::vector<std::string>{ "SET_PRESSURE_ADVANCE ADVANCE=0.04", "SET_PRESSURE_ADVANCE ADVANCE=0.03" });
        CHECK(out.find("PA_CHANGE") == std::string::npos);
        // Right before the speed of the island.
        CHECK(out.find("G1 X0 Y0 F12000\nSET_PRESSURE_ADVANCE ADVANCE=0.04\nG1 F2400\n") != std::string::npos);
    }
    SECTION("the fastest speed of the island, up to the next travel") {
        AdaptivePAProcessor p = processor(model_2x2);
        const std::string out = p.process_layer(tag(0.1, 3000) + "G1 F1200\nG1 X10 Y0 E1\nG1 F4800\nG1 X20 Y0 E1\nG1 X30 Y10 F12000\nG1 F600\n");
        CHECK(pa_commands(out) == std::vector<std::string>{ "SET_PRESSURE_ADVANCE ADVANCE=0.03" });
    }
    SECTION("an arc is an extrusion") {
        AdaptivePAProcessor p = processor(model_2x2);
        const std::string out = p.process_layer(tag(0.1, 3000) + "G1 F2400\nG2 X10 Y0 I5 J0 E1\nG1 X30 Y10 F12000\nG1 F4800\n");
        CHECK(pa_commands(out) == std::vector<std::string>{ "SET_PRESSURE_ADVANCE ADVANCE=0.04" });
    }
    SECTION("no G1 F before the extrusion (dropped by the cooling buffer): the travel's feedrate") {
        AdaptivePAProcessor p = processor(model_2x2);
        const std::string out = p.process_layer("G1 F4800\nG1 X5 Y5 F2400\n" + tag(0.1, 3000) + "G1 X10 Y0 E1\n");
        CHECK(pa_commands(out) == std::vector<std::string>{ "SET_PRESSURE_ADVANCE ADVANCE=0.04" });
    }
    SECTION("the acceleration") {
        AdaptivePAProcessor p = processor(model_2x2);
        const std::string out = p.process_layer(tag(0.1, 10000) + "G1 F3600\nG1 X10 Y0 E1\n");   // flow 6 at 10000: 0.025
        CHECK(pa_commands(out) == std::vector<std::string>{ "SET_PRESSURE_ADVANCE ADVANCE=0.025" });
    }
    SECTION("SET_PRESSURE_ADVANCE in the G-code (tool change, custom G-code) is known") {
        AdaptivePAProcessor p = processor(model_2x2);
        const std::string out = p.process_layer(tag(0.1, 3000) + "G1 F2400\nG1 X10 Y0 E1\nG1 X0 Y0 F12000\nSET_PRESSURE_ADVANCE ADVANCE=0.03\n" +
                                                tag(0.1, 3000) + "G1 F2400\nG1 X10 Y0 E1\n");
        CHECK(pa_commands(out) == std::vector<std::string>{ "SET_PRESSURE_ADVANCE ADVANCE=0.04", "SET_PRESSURE_ADVANCE ADVANCE=0.03",
                                                            "SET_PRESSURE_ADVANCE ADVANCE=0.04" });
    }
    SECTION("the state is kept between layers") {
        AdaptivePAProcessor p = processor(model_2x2);
        p.process_layer(tag(0.1, 3000) + "G1 F2400\nG1 X10 Y0 E1\n");
        CHECK(pa_commands(p.process_layer("G1 X0 Y0 F12000\n" + tag(0.1, 3000) + "G1 F2400\nG1 X10 Y0 E1\n")).empty());
    }
    SECTION("never 0: the static value instead, nothing if it is 0 too") {
        AdaptivePAProcessor p = processor("0,4,3000\n0.0004,8,3000\n");
        CHECK(pa_commands(p.process_layer(tag(0.1, 3000) + "G1 F2400\nG1 X10 Y0 E1\n")) ==
              std::vector<std::string>{ "SET_PRESSURE_ADVANCE ADVANCE=0.025" });
        AdaptivePAProcessor p0 = processor("0,4,3000\n0.0004,8,3000\n", 0.);
        CHECK(pa_commands(p0.process_layer(tag(0.1, 3000) + "G1 F2400\nG1 X10 Y0 E1\n")).empty());
    }
    SECTION("an overhang perimeter: the lower of the current and the next speed") {
        AdaptivePAProcessor p = processor(model_2x2);
        // Current speed 80 mm/s, next 40 mm/s: 40 (flow 4: 0.04), not the fastest 80.
        const std::string out = p.process_layer("G1 F4800\n" + tag(0.1, 3000, true, true) + "G1 F2400\nG1 X10 Y0 E1\nG1 F4800\nG1 X20 Y0 E1\n");
        CHECK(pa_commands(out) == std::vector<std::string>{ "SET_PRESSURE_ADVANCE ADVANCE=0.04" });
    }
    SECTION("comments") {
        AdaptivePAProcessor p = processor(model_2x2, 0.025, true);
        const std::string out = p.process_layer(tag(0.1, 3000) + "G1 F2400\nG1 X10 Y0 E1\n");
        CHECK(out.find("; PA_CHANGE:T0 MM3MM:0.1 ACCEL:3000") != std::string::npos);
        CHECK(out.find("; APA speed 40 mm/s, flow 4 mm3/s, acceleration 3000, PA 0.04") != std::string::npos);
    }
}

TEST_CASE("Adaptive PA in sliced G-code", "[AdaptivePA]")
{
    DynamicPrintConfig config = klipper_config();

    SECTION("off (default): no pressure advance") {
        const std::string gcode = Test::slice({ TestMesh::cube_20x20x20 }, config);
        CHECK(gcode.find("SET_PRESSURE_ADVANCE") == std::string::npos);
        CHECK(gcode.find("PA_CHANGE") == std::string::npos);
    }
    SECTION("pressure advance: once, after the filament start G-code") {
        config.set_deserialize_strict({ { "enable_pressure_advance", "1" }, { "pressure_advance", "0.0225" } });
        const std::string gcode = Test::slice({ TestMesh::cube_20x20x20 }, config);
        CHECK(pa_commands(gcode) == std::vector<std::string>{ "SET_PRESSURE_ADVANCE ADVANCE=0.0225" });
        CHECK(gcode.find("M117 FILAMENT START\nSET_PRESSURE_ADVANCE ADVANCE=0.0225\n") != std::string::npos);
        CHECK(gcode.find("PA_CHANGE") == std::string::npos);
    }
    SECTION("not for other firmwares") {
        config.set_deserialize_strict({ { "gcode_flavor", "marlin2" }, { "enable_pressure_advance", "1" }, { "adaptive_pressure_advance", "1" },
                                        { "adaptive_pressure_advance_model", model_2x2 } });
        const std::string gcode = Test::slice({ TestMesh::cube_20x20x20 }, config);
        CHECK(gcode.find("SET_PRESSURE_ADVANCE") == std::string::npos);
        CHECK(gcode.find("PA_CHANGE") == std::string::npos);
    }

    // Slow walls (flow below 4 mmÃ‚Â³/s: 0.04), fast infill (above 8: 0.03).
    config.set_deserialize_strict({
        { "enable_pressure_advance", "1" }, { "pressure_advance", "0.025" }, { "adaptive_pressure_advance", "1" },
        { "perimeter_speed", "20" }, { "external_perimeter_speed", "20" }, { "small_perimeter_speed", "20" },
        { "infill_speed", "200" }, { "solid_infill_speed", "200" }, { "top_solid_infill_speed", "200" },
        { "fill_density", "20%" }, { "default_acceleration", "0" }, { "klipper_max_accel", "3000" },
    });
    // Set directly: deserializing a list of strings splits at ';'.
    auto set_model = [&config](const std::string &model) { config.option<ConfigOptionStrings>("adaptive_pressure_advance_model", true)->values = { model }; };
    set_model("; nozzle 0.4\n0.04,4,3000\n0.03,8,3000\n");

    SECTION("adaptive: per feature, never 0, no tags left") {
        const std::string gcode = Test::slice({ TestMesh::cube_20x20x20 }, config);
        const auto per_type = pa_per_type(gcode);
        CHECK(per_type.at("External perimeter") == std::set<std::string>{ "0.04" });
        CHECK(per_type.at("Perimeter") == std::set<std::string>{ "0.04" });
        CHECK(per_type.at("Internal infill") == std::set<std::string>{ "0.03" });
        CHECK(gcode.find("PA_CHANGE") == std::string::npos);
        CHECK(gcode.find("ADVANCE=0\n") == std::string::npos);
        // The static value first, after the filament start G-code.
        CHECK(pa_commands(gcode).front() == "SET_PRESSURE_ADVANCE ADVANCE=0.025");
    }
    SECTION("after the cooling slowdown: slowed down infill gets the slow value") {
        config.set_deserialize_strict({ { "cooling", "1" }, { "slowdown_below_layer_time", "500" }, { "min_print_speed", "5" } });
        const auto per_type = pa_per_type(Test::slice({ TestMesh::cube_20x20x20 }, config));
        CHECK(per_type.at("Internal infill") == std::set<std::string>{ "0.04" });
    }
    SECTION("the acceleration: the print settings', default acceleration 0: the Klipper max accel of the speed mode") {
        config.set_deserialize_strict({ { "gcode_comments", "1" }, { "klipper_max_accel", "8000" }, { "klipper_max_accel_slow", "50%" },
                                        { "klipper_speed_mode", "Slow" } });
        std::string gcode = Test::slice({ TestMesh::cube_20x20x20 }, config);
        CHECK(gcode.find(" ACCEL:4000 ") != std::string::npos);
        CHECK(gcode.find("M204") == std::string::npos);
        config.set_deserialize_strict({ { "default_acceleration", "2000" }, { "perimeter_acceleration", "1000" } });
        gcode = Test::slice({ TestMesh::cube_20x20x20 }, config);
        CHECK(gcode.find(" ACCEL:1000 ") != std::string::npos);
        CHECK(gcode.find(" ACCEL:2000 ") != std::string::npos);
        CHECK(gcode.find(" ACCEL:4000 ") == std::string::npos);
    }
    SECTION("no measurements for the nozzle: the static value only") {
        set_model("; nozzle 0.6\n0.04,4,3000\n0.03,8,3000\n");
        const std::string gcode = Test::slice({ TestMesh::cube_20x20x20 }, config);
        CHECK(pa_commands(gcode) == std::vector<std::string>{ "SET_PRESSURE_ADVANCE ADVANCE=0.025" });
        CHECK(gcode.find("PA_CHANGE") == std::string::npos);
    }
    SECTION("not in spiral vase mode (OrcaSlicer)") {
        config.set_deserialize_strict({ { "spiral_vase", "1" }, { "perimeters", "1" }, { "top_solid_layers", "0" }, { "fill_density", "0%" } });
        const std::string gcode = Test::slice({ TestMesh::cube_20x20x20 }, config);
        CHECK(pa_commands(gcode) == std::vector<std::string>{ "SET_PRESSURE_ADVANCE ADVANCE=0.025" });
        CHECK(gcode.find("PA_CHANGE") == std::string::npos);
    }
}
