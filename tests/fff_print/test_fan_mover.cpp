#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/GCode/FanMover.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Fan mover (fan_speedup_time), ported from SuperSlicer: fan speed increases are started earlier.

namespace {

// 10 mm/s: every 10 mm move takes 1 s.
const std::string moves_before =
    "G1 F600\n"
    "G1 X10 E1\n"
    "G1 X20 E1\n"
    "G1 X30 E1\n";

std::string move(const std::string &gcode, float delay, bool only_overhangs = false)
{
    FanMover mover(gcfKlipper, delay, true, only_overhangs);
    return mover.process_gcode(gcode, true);
}

// Sum of the relative extrusion and the last X of the G-code.
std::pair<double, double> extrusion_and_last_x(const std::string &gcode)
{
    double e = 0, x = 0;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.cmd_is("G1")) {
            if (line.has(Slic3r::E))
                e += line.e();
            if (line.has(Slic3r::X))
                x = line.x();
        }
    });
    return { e, x };
}

} // namespace

TEST_CASE("Fan mover: a fan speed increase starts earlier", "[FanMover]") {
    SECTION("1.5 s earlier splits the move in the middle") {
        const std::string out = move(moves_before + "M106 S255\nG1 X40 E1\n", 1.5f);
        CHECK(out == "G1 F600\nG1 X10 E1\nG1 X15 E0.5\nM106 S255\nG1 X20 E0.5\nG1 X30 E1\nG1 X40 E1\n");
    }
    SECTION("a delay longer than the G-code before it moves it to the start") {
        const std::string out = move("G1 F600\nG1 X10 E1\nM106 S255\nG1 X20 E1\n", 5.f);
        CHECK(out == "M106 S255\nG1 F600\nG1 X10 E1\nG1 X20 E1\n");
    }
    SECTION("the extrusion is unchanged") {
        const std::string in  = moves_before + "M106 S255\nG1 X40 E1\n";
        const auto before = extrusion_and_last_x(in);
        const auto after  = extrusion_and_last_x(move(in, 1.3f));
        CHECK(std::abs(before.first - after.first) < 1e-4);
        CHECK(before.second == after.second);
    }
}

TEST_CASE("Fan mover: what is not moved", "[FanMover]") {
    SECTION("slowing the fan down") {
        const std::string in = "M106 S255\n" + moves_before + "M106 S51\nG1 X40 E1\n";
        CHECK(move(in, 1.5f) == in);
    }
    SECTION("disabled") {
        const std::string in = moves_before + "M106 S255\nG1 X40 E1\n";
        CHECK(move(in, 0.f) == in);
    }
    SECTION("only for overhangs: other features") {
        const std::string in = ";TYPE:Solid infill\n" + moves_before + "M106 S255\nG1 X40 E1\n";
        CHECK(move(in, 1.5f, true) == in);
    }
    SECTION("only for overhangs: overhang perimeters are moved") {
        const std::string in = ";TYPE:Overhang perimeter\n" + moves_before + "M106 S255\nG1 X40 E1\n";
        CHECK(move(in, 1.5f, true) != in);
    }
    SECTION("fan commands of custom G-code") {
        const std::string in = moves_before + "; custom gcode: layer_gcode\nM106 S255\n; custom gcode end: layer_gcode\nG1 X40 E1\n";
        CHECK(move(in, 1.5f) == in);
    }
    SECTION("into the previous layer") {
        FanMover mover(gcfKlipper, 1.5f, true, false);
        const std::string layer1 = mover.process_gcode(moves_before, true);
        const std::string layer2 = mover.process_gcode("M106 S255\nG1 X40 E1\n", true);
        CHECK(layer1 == moves_before);
        CHECK(layer2 == "M106 S255\nG1 X40 E1\n");
    }
}

TEST_CASE("Fan mover in sliced G-code", "[FanMover]") {
    auto config = DynamicPrintConfig::full_print_config_with({
        { "gcode_flavor",             "klipper" },
        { "use_relative_e_distances", true },
        { "cooling",                  { 1 } },
        { "fan_always_on",            { 1 } },
        { "min_fan_speed",            { 30 } },
        { "bridge_fan_speed",         { 75 } },
        { "fan_below_layer_time",     { 0 } },
        { "fan_speedup_overhangs",    false },
    });
    const std::string without = Test::slice({ TestMesh::bridge }, config);
    config.set_deserialize_strict({ { "fan_speedup_time", 2 } });
    const std::string with = Test::slice({ TestMesh::bridge }, config);

    // The fan commands moved, the extrusion did not.
    CHECK(with != without);
    const auto a = extrusion_and_last_x(without);
    const auto b = extrusion_and_last_x(with);
    CHECK(std::abs(a.first - b.first) < 1e-2);
    CHECK(a.second == b.second);
    // The custom G-codes are tagged for the fan mover.
    CHECK(with.find("; custom gcode: start_gcode") != std::string::npos);
    CHECK(without.find("; custom gcode:") == std::string::npos);
}
