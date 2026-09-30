#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "test_data.hpp"
#include "gcode_walk.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Per feature fan speeds and default_fan_speed, ported from SuperSlicer.

namespace {

// Klipper printer, fan disabled for the first 3 layers, no ramp and no short layer cooling,
// so that the feature fan speeds are applied as set.
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
        { "fill_density",              "15%" },
    });
}

// Supported overhang with ironing: most of the feature types in one print.
DynamicPrintConfig all_features(DynamicPrintConfig config)
{
    config.set_deserialize_strict({
        { "support_material",                 true },
        { "support_material_interface_layers", 2 },
        { "ironing",                          true },
    });
    return config;
}

// Fan speed of every extrusion of a feature type from the given layer on.
std::map<std::string, std::vector<int>> fans_by_type(const std::vector<Extrusion> &extrusions, int first_layer = 3)
{
    std::map<std::string, std::vector<int>> out;
    for (const Extrusion &e : extrusions)
        if (e.layer >= first_layer)
            out[e.type].push_back(e.fan);
    return out;
}

void check_type_fan(const std::map<std::string, std::vector<int>> &fans, const std::string &type, int expected)
{
    INFO(type);
    auto it = fans.find(type);
    REQUIRE(it != fans.end());
    for (int fan : it->second)
        CHECK(fan == expected);
}

const std::string perimeter          = "Perimeter";
const std::string external_perimeter = "External perimeter";
const std::string overhang_perimeter = "Overhang perimeter";
const std::string internal_infill    = "Internal infill";
const std::string solid_infill       = "Solid infill";
const std::string top_solid_infill   = "Top solid infill";
const std::string ironing            = "Ironing";
const std::string bridge_infill      = "Bridge infill";
const std::string support            = "Support material";
const std::string support_interface  = "Support material interface";

} // namespace

TEST_CASE("Feature fan speeds are applied per feature", "[FeatureFan]") {
    DynamicPrintConfig config = all_features(base_config());
    config.set_deserialize_strict({
        { "perimeter_fan_speed",                  { 11 } },
        { "external_perimeter_fan_speed",         { 12 } },
        { "infill_fan_speed",                     { 13 } },
        { "solid_infill_fan_speed",               { 14 } },
        { "top_fan_speed",                        { 16 } },
        { "support_material_fan_speed",           { 18 } },
        { "support_material_interface_fan_speed", { 19 } },
        { "overhangs_fan_speed",                  { 22 } },
    });
    int redundant = 0;
    const auto fans = fans_by_type(walk_gcode(Test::slice({ TestMesh::overhang }, config), &redundant));
    check_type_fan(fans, perimeter,          11);
    check_type_fan(fans, external_perimeter, 12);
    check_type_fan(fans, internal_infill,    13);
    check_type_fan(fans, solid_infill,       14);
    check_type_fan(fans, top_solid_infill,   16);
    check_type_fan(fans, ironing,            16);
    check_type_fan(fans, support,            18);
    check_type_fan(fans, support_interface,  19);
    // Feature fan speeds are lower than the bridge fan speed, bridges keep it.
    check_type_fan(fans, bridge_infill,      75);
    if (fans.count(overhang_perimeter))
        check_type_fan(fans, overhang_perimeter, 22);
    // The fan is not toggled back to the layer fan speed in between two features having their own fan speed.
    CHECK(redundant == 0);
}

TEST_CASE("Feature fan speed fallbacks", "[FeatureFan]") {
    DynamicPrintConfig config = all_features(base_config());
    config.set_deserialize_strict({
        { "perimeter_fan_speed",        { 11 } },
        { "solid_infill_fan_speed",     { 14 } },
        { "support_material_fan_speed", { 18 } },
    });
    const auto fans = fans_by_type(walk_gcode(Test::slice({ TestMesh::overhang }, config)));
    check_type_fan(fans, perimeter,          11);
    check_type_fan(fans, external_perimeter, 11); // -> perimeter
    check_type_fan(fans, solid_infill,       14);
    check_type_fan(fans, top_solid_infill,   14); // -> solid infill
    check_type_fan(fans, support,            18);
    check_type_fan(fans, support_interface,  18); // -> support material
    // No default_fan_speed: the other features run at the layer fan speed (min_fan_speed, fan always on).
    check_type_fan(fans, internal_infill,    30);
    check_type_fan(fans, ironing,            30); // ironing follows top_fan_speed only, not its fallback
}

TEST_CASE("Default fan speed", "[FeatureFan]") {
    DynamicPrintConfig config = all_features(base_config());

    SECTION("replaces min_fan_speed for all features without a fan speed of their own") {
        config.set_deserialize_strict({ { "default_fan_speed", { 40 } }, { "perimeter_fan_speed", { 11 } } });
        const auto fans = fans_by_type(walk_gcode(Test::slice({ TestMesh::overhang }, config)));
        check_type_fan(fans, perimeter,          11);
        check_type_fan(fans, external_perimeter, 11);
        for (const std::string &type : { internal_infill, solid_infill, top_solid_infill, ironing, support, support_interface })
            check_type_fan(fans, type, 40);
        // The bridge fan speed is higher than the default fan speed.
        check_type_fan(fans, bridge_infill, 75);
    }

    SECTION("0 keeps the fan off, bridges still get their fan") {
        config.set_deserialize_strict({ { "default_fan_speed", { 0 } } });
        const auto fans = fans_by_type(walk_gcode(Test::slice({ TestMesh::overhang }, config)));
        for (const std::string &type : { perimeter, external_perimeter, internal_infill, solid_infill, top_solid_infill, support })
            check_type_fan(fans, type, 0);
        check_type_fan(fans, bridge_infill, 75);
    }
}

TEST_CASE("Feature fan speeds and cooling (SuperSlicer rules)", "[FeatureFan]") {
    DynamicPrintConfig config = all_features(base_config());
    config.set_deserialize_strict({
        { "perimeter_fan_speed",                  { 11 } },
        { "top_fan_speed",                        { 16 } },
        { "support_material_fan_speed",           { 18 } },
        { "support_material_interface_fan_speed", { 19 } },
    });

    SECTION("disable_fan_first_layers stops every feature fan") {
        config.set_deserialize_strict({ { "disable_fan_first_layers", { 1000 } } });
        for (const Extrusion &e : walk_gcode(Test::slice({ TestMesh::overhang }, config)))
            CHECK(e.fan == 0);
    }

    SECTION("short layers raise perimeters but not top solid infill or support") {
        config.set_deserialize_strict({ { "slowdown_below_layer_time", { 10000 } }, { "fan_below_layer_time", { 10000 } } });
        const auto fans = fans_by_type(walk_gcode(Test::slice({ TestMesh::overhang }, config)));
        check_type_fan(fans, perimeter,         35);
        check_type_fan(fans, top_solid_infill,  16);
        check_type_fan(fans, support,           18);
        check_type_fan(fans, support_interface, 19);
    }

    SECTION("full_fan_speed_layer ramps perimeters, top solid infill and support, but not support interface") {
        config.set_deserialize_strict({ { "disable_fan_first_layers", { 1 } }, { "full_fan_speed_layer", { 1000 } } });
        const auto fans = fans_by_type(walk_gcode(Test::slice({ TestMesh::overhang }, config)));
        for (const auto &[type, expected] : std::vector<std::pair<std::string, int>>{ { perimeter, 11 }, { top_solid_infill, 16 }, { support, 18 } }) {
            INFO(type);
            REQUIRE(fans.count(type));
            for (int fan : fans.at(type))
                CHECK(fan < expected);
        }
        check_type_fan(fans, support_interface, 19);
    }
}
