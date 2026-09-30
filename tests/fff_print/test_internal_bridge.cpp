#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>
#include <boost/algorithm/string/predicate.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Internal bridge infill as its own extrusion type with its own speed, acceleration and fan.

namespace {

// State of the printer at one extruding move.
struct Extrusion
{
    int         layer;
    std::string type;
    int         fan;   // percent
    int         accel; // mm/s^2
    double      F;     // mm/min
};

std::vector<Extrusion> walk_gcode(const std::string &gcode)
{
    std::vector<Extrusion> out;
    int         layer = -1;
    std::string type;
    int         fan   = 0;
    int         accel = 0;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        const std::string &raw = line.raw();
        if (boost::starts_with(raw, ";LAYER_CHANGE")) {
            ++ layer;
        } else if (boost::starts_with(raw, ";TYPE:")) {
            type = raw.substr(6);
        } else if (line.cmd_is("M106")) {
            float s = 0;
            line.has_value('S', s);
            fan = int(std::round(s * 100.f / 255.f));
        } else if (line.cmd_is("M107")) {
            fan = 0;
        } else if (line.cmd_is("M204")) {
            float s = 0;
            if (line.has_value('S', s) || line.has_value('P', s))
                accel = int(std::round(s));
        } else if ((line.cmd_is("G1") || line.cmd_is("G2") || line.cmd_is("G3")) && line.dist_E(self) > 0 && line.dist_XY(self) > 0) {
            out.push_back({ layer, type, fan, accel, line.new_F(self) });
        }
    });
    return out;
}

const std::string internal_bridge = "Internal bridge infill";
const std::string bridge          = "Bridge infill";

// Klipper printer with a low base fan and a high bridge fan, like the user's profile.
DynamicPrintConfig base_config()
{
    return DynamicPrintConfig::full_print_config_with({
        { "gcode_flavor",              "klipper" },
        { "cooling",                   { 1 } },
        { "fan_always_on",             { 1 } },
        { "min_fan_speed",             { 30 } },
        { "max_fan_speed",             { 35 } },
        { "bridge_fan_speed",          { 75 } },
        { "disable_fan_first_layers",  { 3 } },
        { "full_fan_speed_layer",      { 0 } },
        { "fan_below_layer_time",      { 0 } },
        { "slowdown_below_layer_time", { 0 } },
        { "default_acceleration",      11000 },
        { "infill_acceleration",       11000 },
        { "solid_infill_acceleration", 10000 },
        { "bridge_acceleration",       5000 },
        { "first_layer_acceleration",  3000 },
        { "bridge_speed",              40 },
        { "fill_density",              "15%" },
    });
}

size_t count_type(const std::vector<Extrusion> &extrusions, const std::string &type)
{
    size_t n = 0;
    for (const Extrusion &e : extrusions)
        if (e.type == type)
            ++ n;
    return n;
}

} // namespace

TEST_CASE("Internal bridges have their own extrusion type", "[InternalBridge]") {
    const std::vector<Extrusion> cube = walk_gcode(Test::slice({ TestMesh::cube_20x20x20 }, base_config()));
    // The top of the cube is supported by internal bridges over the sparse infill.
    REQUIRE(count_type(cube, internal_bridge) > 0);
    CHECK(count_type(cube, bridge) == 0);

    // The bridge model has external bridges printed over air.
    const std::vector<Extrusion> bridge_model = walk_gcode(Test::slice({ TestMesh::bridge }, base_config()));
    CHECK(count_type(bridge_model, bridge) > 0);
}

TEST_CASE("Internal bridges inherit the bridge settings by default", "[InternalBridge]") {
    const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::cube_20x20x20 }, base_config()));
    REQUIRE(count_type(extrusions, internal_bridge) > 0);
    for (const Extrusion &e : extrusions)
        if (e.type == internal_bridge) {
            INFO("layer " << e.layer);
            CHECK(e.fan == 75);
            CHECK(e.accel == 5000);
            CHECK(e.F == 40 * 60);
        }
}

TEST_CASE("Internal bridge speed, acceleration and fan", "[InternalBridge]") {
    // Values that no other feature uses, so that it is visible where they leak.
    DynamicPrintConfig config = base_config();
    config.set_deserialize_strict({
        { "internal_bridge_speed",        23 },
        { "internal_bridge_acceleration", 2345 },
        { "internal_bridge_fan_speed",    { 21 } },
    });

    SECTION("applied inside internal bridges and restored afterwards") {
        const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::cube_20x20x20 }, config));
        REQUIRE(count_type(extrusions, internal_bridge) > 0);
        for (const Extrusion &e : extrusions) {
            INFO("layer " << e.layer << " " << e.type);
            if (e.type == internal_bridge) {
                // The fan speed is lower than the base fan speed: the feature fan overrides the cooling logic.
                CHECK(e.fan == 21);
                CHECK(e.accel == 2345);
                CHECK(e.F == 23 * 60);
            } else {
                CHECK(e.fan != 21);
                CHECK(e.accel != 2345);
                CHECK(e.F != 23 * 60);
            }
        }
    }

    SECTION("external bridges keep the bridge settings") {
        const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::bridge }, config));
        REQUIRE(count_type(extrusions, bridge) > 0);
        for (const Extrusion &e : extrusions)
            if (e.type == bridge) {
                INFO("layer " << e.layer);
                CHECK(e.fan == 75);
                CHECK(e.accel == 5000);
                CHECK(e.F == 40 * 60);
            }
    }

    SECTION("speed as a percentage of the bridge speed") {
        config.set_deserialize_strict({ { "internal_bridge_speed", "50%" } });
        const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::cube_20x20x20 }, config));
        REQUIRE(count_type(extrusions, internal_bridge) > 0);
        for (const Extrusion &e : extrusions)
            if (e.type == internal_bridge)
                CHECK(e.F == 20 * 60);
    }
}

TEST_CASE("Internal bridge fan edge cases", "[InternalBridge]") {
    DynamicPrintConfig config = base_config();

    SECTION("fan disabled on the first layers wins") {
        config.set_deserialize_strict({ { "internal_bridge_fan_speed", { 21 } }, { "disable_fan_first_layers", { 1000 } } });
        const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::cube_20x20x20 }, config));
        REQUIRE(count_type(extrusions, internal_bridge) > 0);
        for (const Extrusion &e : extrusions)
            CHECK(e.fan == 0);
    }

    SECTION("short layer auto-cooling is overridden inside internal bridges") {
        // Every layer is shorter than slowdown_below_layer_time, so the cooling logic runs the fan at max_fan_speed.
        config.set_deserialize_strict({ { "internal_bridge_fan_speed", { 21 } }, { "slowdown_below_layer_time", { 10000 } },
                                        { "fan_below_layer_time", { 10000 } } });
        const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::cube_20x20x20 }, config));
        REQUIRE(count_type(extrusions, internal_bridge) > 0);
        for (const Extrusion &e : extrusions)
            if (e.layer >= 3) {
                INFO("layer " << e.layer << " " << e.type);
                CHECK(e.fan == (e.type == internal_bridge ? 21 : 35));
            }
    }

    SECTION("fan already higher than the bridge fan") {
        config.set_deserialize_strict({ { "min_fan_speed", { 90 } }, { "max_fan_speed", { 100 } } });
        SECTION("inheriting the bridge fan never lowers the fan (stock behaviour)") {
            const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::cube_20x20x20 }, config));
            REQUIRE(count_type(extrusions, internal_bridge) > 0);
            for (const Extrusion &e : extrusions)
                if (e.type == internal_bridge)
                    CHECK(e.fan == 90);
        }
        SECTION("an own internal bridge fan lowers it") {
            config.set_deserialize_strict({ { "internal_bridge_fan_speed", { 50 } } });
            const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::cube_20x20x20 }, config));
            REQUIRE(count_type(extrusions, internal_bridge) > 0);
            for (const Extrusion &e : extrusions)
                if (e.type == internal_bridge)
                    CHECK(e.fan == 50);
        }
    }

    SECTION("full_fan_speed_layer ramp") {
        // The whole cube is inside the ramp.
        config.set_deserialize_strict({ { "disable_fan_first_layers", { 1 } }, { "full_fan_speed_layer", { 1000 } } });
        SECTION("an own internal bridge fan is not ramped") {
            config.set_deserialize_strict({ { "internal_bridge_fan_speed", { 21 } } });
            const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::cube_20x20x20 }, config));
            REQUIRE(count_type(extrusions, internal_bridge) > 0);
            for (const Extrusion &e : extrusions)
                if (e.type == internal_bridge)
                    CHECK(e.fan == 21);
        }
        SECTION("inheriting the bridge fan follows the ramp (stock behaviour)") {
            const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::cube_20x20x20 }, config));
            REQUIRE(count_type(extrusions, internal_bridge) > 0);
            for (const Extrusion &e : extrusions)
                if (e.type == internal_bridge) {
                    CHECK(e.fan > 0);
                    CHECK(e.fan < 75);
                }
        }
    }
}
