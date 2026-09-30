#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>
#include <boost/algorithm/string/predicate.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "test_data.hpp"
#include "gcode_walk.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Internal bridge infill as its own extrusion type with its own speed, acceleration and fan.

namespace {

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

    SECTION("short layers raise the internal bridge fan up to max_fan_speed (SuperSlicer)") {
        // Every layer is shorter than slowdown_below_layer_time, so the cooling logic runs the fan at max_fan_speed (35).
        config.set_deserialize_strict({ { "slowdown_below_layer_time", { 10000 } }, { "fan_below_layer_time", { 10000 } } });
        SECTION("an internal bridge fan below max_fan_speed is raised") {
            config.set_deserialize_strict({ { "internal_bridge_fan_speed", { 21 } } });
            const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::cube_20x20x20 }, config));
            REQUIRE(count_type(extrusions, internal_bridge) > 0);
            for (const Extrusion &e : extrusions)
                if (e.layer >= 3) {
                    INFO("layer " << e.layer << " " << e.type);
                    CHECK(e.fan == 35);
                }
        }
        SECTION("an internal bridge fan above max_fan_speed is kept") {
            config.set_deserialize_strict({ { "internal_bridge_fan_speed", { 50 } } });
            const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::cube_20x20x20 }, config));
            REQUIRE(count_type(extrusions, internal_bridge) > 0);
            for (const Extrusion &e : extrusions)
                if (e.layer >= 3) {
                    INFO("layer " << e.layer << " " << e.type);
                    CHECK(e.fan == (e.type == internal_bridge ? 50 : 35));
                }
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
