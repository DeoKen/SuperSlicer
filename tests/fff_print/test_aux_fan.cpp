#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/GCode/FanMover.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Auxiliary part cooling fan, as in OrcaSlicer, with the fan G-code set in the printer settings (auxiliary_fan_gcode).

namespace {

DynamicPrintConfig base_config()
{
    return DynamicPrintConfig::full_print_config_with({
        { "gcode_flavor",                 "klipper" },
        { "start_gcode",                  "START_MARKER" },
        { "end_gcode",                    "END_MARKER" },
        { "auxiliary_fan_gcode",          "AUXFAN S{aux_fan_speed}" },
        { "additional_cooling_fan_speed", { 60 } },
        { "disable_fan_first_layers",     { 3 } },
        { "full_fan_speed_layer",         { 0 } },
    });
}

struct AuxFanCommand
{
    std::string line;
    // Index of the layer it is written for: the cooling buffer writes the fan commands of a layer at its start, before its
    // ;LAYER_CHANGE line (as for the part cooling fan), so this is the number of ;LAYER_CHANGE lines before it.
    // -1 before the start G-code marker, 1000 after the end G-code marker.
    int         layer_changes;
};

std::vector<AuxFanCommand> aux_fan_commands(const std::string &gcode, const std::string &prefix = "AUXFAN")
{
    std::vector<AuxFanCommand> out;
    int  layer_changes = 0;
    bool started = false, ended = false;
    size_t pos = 0;
    while (pos < gcode.size()) {
        size_t eol = gcode.find('\n', pos);
        if (eol == std::string::npos)
            eol = gcode.size();
        const std::string line = gcode.substr(pos, eol - pos);
        if (line == "START_MARKER")
            started = true;
        else if (line == "END_MARKER")
            ended = true;
        else if (line.rfind(";LAYER_CHANGE", 0) == 0)
            ++ layer_changes;
        else if (line.rfind(prefix, 0) == 0)
            out.push_back({ line, ! started ? -1 : ended ? 1000 : layer_changes });
        pos = eol + 1;
    }
    return out;
}

} // namespace

TEST_CASE("Auxiliary fan: off for the first layers, then the filament speed", "[AuxFan]") {
    const std::string gcode = Test::slice({ TestMesh::cube_20x20x20 }, base_config());
    const std::vector<AuxFanCommand> cmds = aux_fan_commands(gcode);
    REQUIRE(cmds.size() == 4);
    // Off before the start G-code (the first layers have the fan disabled), off again at the first layer
    // (the cooling buffer always sets it on the first layer, as OrcaSlicer does).
    CHECK(cmds[0].line == "AUXFAN S0");
    CHECK(cmds[0].layer_changes == -1);
    CHECK(cmds[1].line == "AUXFAN S0");
    CHECK(cmds[1].layer_changes == 0);
    // On from the 4th layer (layer index 3, disable_fan_first_layers = 3), only once.
    CHECK(cmds[2].line == "AUXFAN S60");
    CHECK(cmds[2].layer_changes == 3);
    // Off at the end, before the end G-code.
    CHECK(cmds[3].line == "AUXFAN S0");
    CHECK(cmds[3].layer_changes < 1000);
    CHECK(cmds[3].layer_changes > 3);
}

TEST_CASE("Auxiliary fan: no layers with the fan disabled", "[AuxFan]") {
    DynamicPrintConfig config = base_config();
    config.set_deserialize_strict({ { "disable_fan_first_layers", "0" } });
    const std::vector<AuxFanCommand> cmds = aux_fan_commands(Test::slice({ TestMesh::cube_20x20x20 }, config));
    REQUIRE(cmds.size() == 2);
    // No "off" before the start G-code, on from the first layer.
    CHECK(cmds[0].line == "AUXFAN S60");
    CHECK(cmds[0].layer_changes == 0);
    CHECK(cmds[1].line == "AUXFAN S0");
}

TEST_CASE("Auxiliary fan: not changed by the layer time or the fan ramp", "[AuxFan]") {
    DynamicPrintConfig config = base_config();
    config.set_deserialize_strict({
        { "cooling",                   "1" },
        { "fan_below_layer_time",      "1000" },
        { "slowdown_below_layer_time", "500" },
        { "full_fan_speed_layer",      "10" },
    });
    const std::vector<AuxFanCommand> cmds = aux_fan_commands(Test::slice({ TestMesh::cube_20x20x20 }, config));
    REQUIRE(cmds.size() == 4);
    CHECK(cmds[2].line == "AUXFAN S60");
    CHECK(cmds[2].layer_changes == 3);
}

TEST_CASE("Auxiliary fan: Klipper and M106 P2 templates", "[AuxFan]") {
    DynamicPrintConfig config = base_config();
    SECTION("Klipper SET_FAN_SPEED, speed as a fraction") {
        config.set_deserialize_strict({ { "auxiliary_fan_gcode", "SET_FAN_SPEED FAN=aux SPEED={aux_fan_speed/100.0}" } });
        const std::vector<AuxFanCommand> cmds = aux_fan_commands(Test::slice({ TestMesh::cube_20x20x20 }, config), "SET_FAN_SPEED");
        REQUIRE(cmds.size() == 4);
        CHECK(cmds[2].line == "SET_FAN_SPEED FAN=aux SPEED=0.6");
        CHECK(cmds[3].line == "SET_FAN_SPEED FAN=aux SPEED=0");
    }
    SECTION("OrcaSlicer M106 P2, speed 0-255") {
        config.set_deserialize_strict({ { "auxiliary_fan_gcode", "M106 P2 S{int(aux_fan_speed*2.55+0.5)}" } });
        const std::vector<AuxFanCommand> cmds = aux_fan_commands(Test::slice({ TestMesh::cube_20x20x20 }, config), "M106 P2");
        REQUIRE(cmds.size() == 4);
        CHECK(cmds[2].line == "M106 P2 S153");
        CHECK(cmds[3].line == "M106 P2 S0");
    }
}

TEST_CASE("Auxiliary fan: max_additional_fan in the custom G-code", "[AuxFan]") {
    DynamicPrintConfig config = base_config();
    config.set_deserialize_strict({ { "start_gcode", "START_MARKER\nMAXAUX {max_additional_fan}" } });
    const std::string gcode = Test::slice({ TestMesh::cube_20x20x20 }, config);
    CHECK(gcode.find("\nMAXAUX 60\n") != std::string::npos);
}

TEST_CASE("Auxiliary fan: empty G-code is stock", "[AuxFan]") {
    DynamicPrintConfig config = base_config();
    config.set_deserialize_strict({ { "auxiliary_fan_gcode", "" } });
    const std::string with_speed = Test::slice({ TestMesh::cube_20x20x20 }, config);
    CHECK(aux_fan_commands(with_speed).empty());
    // The filament speed alone changes nothing.
    config.set_deserialize_strict({ { "additional_cooling_fan_speed", "0" } });
    const std::string without_speed = Test::slice({ TestMesh::cube_20x20x20 }, config);
    auto strip_config = [](const std::string &gcode) {
        // The config block at the end lists additional_cooling_fan_speed, and the header has a time stamp.
        const size_t pos = gcode.find('\n') + 1;
        const size_t end = gcode.find("; prusaslicer_config = begin");
        return gcode.substr(pos, end - pos);
    };
    CHECK(strip_config(with_speed) == strip_config(without_speed));
}

TEST_CASE("Auxiliary fan: the fan mover keeps it and the custom G-code tags off it", "[AuxFan]") {
    SECTION("M106 P2 is not the part cooling fan for the fan mover") {
        FanMover mover(gcfKlipper, 1.5f, true, false);
        const std::string in = "G1 F600\nG1 X10 E1\nG1 X20 E1\nG1 X30 E1\nM106 P2 S255\nG1 X40 E1\n";
        CHECK(mover.process_gcode(in, true) == in);
    }
    SECTION("not tagged as custom G-code with the fan mover on") {
        DynamicPrintConfig config = base_config();
        config.set_deserialize_strict({ { "fan_speedup_time", "2" } });
        const std::string gcode = Test::slice({ TestMesh::cube_20x20x20 }, config);
        CHECK(gcode.find("; custom gcode: auxiliary_fan_gcode") == std::string::npos);
        CHECK(aux_fan_commands(gcode).size() == 4);
    }
}
