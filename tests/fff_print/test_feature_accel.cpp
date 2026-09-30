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

// Per feature accelerations (gap fill, support, support interface, ironing, brim and skirt) and the brim / skirt speed.
// All default to 0, which keeps the stock PrusaSlicer behaviour.

namespace {

const std::string gap_fill          = "Gap fill";
const std::string support           = "Support material";
const std::string support_interface = "Support material interface";
const std::string ironing           = "Ironing";
const std::string skirt             = "Skirt/Brim";

// Supported overhang with ironing and a skirt on the first 3 layers, distinct accelerations for the stock features.
DynamicPrintConfig base_config()
{
    return DynamicPrintConfig::full_print_config_with({
        { "gcode_flavor",                      "klipper" },
        // No slowdown by the cooling logic, the speeds are checked as set.
        { "cooling",                           { 0 } },
        { "support_material",                  true },
        { "support_material_interface_layers", 2 },
        { "ironing",                           true },
        { "skirts",                            1 },
        { "skirt_height",                      3 },
        { "default_acceleration",              5000 },
        { "perimeter_acceleration",            3000 },
        { "external_perimeter_acceleration",   2500 },
        { "top_solid_infill_acceleration",     4000 },
        { "solid_infill_acceleration",         3500 },
        { "infill_acceleration",               4500 },
        { "first_layer_acceleration",          0 },
        { "support_material_speed",            60 },
    });
}

// Accelerations (or feed rates) of the extrusions of each feature type, from the second layer on.
std::map<std::string, std::vector<double>> by_type(const std::vector<Extrusion> &extrusions, bool feed_rate = false)
{
    std::map<std::string, std::vector<double>> out;
    for (const Extrusion &e : extrusions)
        if (e.layer >= 1)
            out[e.type].push_back(feed_rate ? e.F : double(e.accel));
    return out;
}

void check_type(const std::map<std::string, std::vector<double>> &values, const std::string &type, double expected, bool required = true)
{
    INFO(type);
    auto it = values.find(type);
    if (! required && it == values.end())
        return;
    REQUIRE(it != values.end());
    for (double v : it->second)
        CHECK(v == expected);
}

} // namespace

TEST_CASE("Feature accelerations at 0 keep the stock accelerations", "[FeatureAccel]") {
    const auto accels = by_type(walk_gcode(Test::slice({ TestMesh::overhang }, base_config())));
    check_type(accels, support,           5000);  // default acceleration
    check_type(accels, support_interface, 5000);
    check_type(accels, ironing,           3500);  // solid infill acceleration
    check_type(accels, skirt,             5000);
    check_type(accels, gap_fill,          5000, false);
}

TEST_CASE("Feature accelerations", "[FeatureAccel]") {
    DynamicPrintConfig config = base_config();

    SECTION("absolute values, support interface follows support") {
        config.set_deserialize_strict({
            { "gap_fill_acceleration",      1111 },
            { "support_material_acceleration", 2222 },
            { "ironing_acceleration",       3333 },
            { "brim_acceleration",          4444 },
        });
        const auto accels = by_type(walk_gcode(Test::slice({ TestMesh::overhang }, config)));
        check_type(accels, support,           2222);
        check_type(accels, support_interface, 2222);
        check_type(accels, ironing,           3333);
        check_type(accels, skirt,             4444);
        check_type(accels, gap_fill,          1111, false);
    }

    SECTION("percentages of the parent feature") {
        config.set_deserialize_strict({
            { "gap_fill_acceleration",                   "50%" },   // of perimeter 3000
            { "support_material_acceleration",           "40%" },   // of default 5000
            { "support_material_interface_acceleration", "50%" },   // of support 2000
            { "ironing_acceleration",                    "25%" },   // of top solid infill 4000
            { "brim_acceleration",                       "150%" },  // of support 2000
        });
        const auto accels = by_type(walk_gcode(Test::slice({ TestMesh::overhang }, config)));
        check_type(accels, support,           2000);
        check_type(accels, support_interface, 1000);
        check_type(accels, ironing,           1000);
        check_type(accels, skirt,             3000);
        check_type(accels, gap_fill,          1500, false);
    }

    SECTION("the first layer acceleration wins on the first layer") {
        config.set_deserialize_strict({ { "first_layer_acceleration", 1234 }, { "brim_acceleration", 4444 } });
        int checked = 0;
        for (const Extrusion &e : walk_gcode(Test::slice({ TestMesh::overhang }, config)))
            if (e.layer == 0 && e.type == skirt) {
                CHECK(e.accel == 1234);
                ++ checked;
            }
        CHECK(checked > 0);
    }
}

TEST_CASE("Brim and skirt speed", "[FeatureAccel]") {
    DynamicPrintConfig config = base_config();

    SECTION("0 = support material speed (stock)") {
        check_type(by_type(walk_gcode(Test::slice({ TestMesh::overhang }, config)), true), skirt, 60 * 60);
    }
    SECTION("absolute") {
        config.set_deserialize_strict({ { "brim_speed", 25 } });
        check_type(by_type(walk_gcode(Test::slice({ TestMesh::overhang }, config)), true), skirt, 25 * 60);
    }
    SECTION("percentage of the support material speed") {
        config.set_deserialize_strict({ { "brim_speed", "50%" } });
        check_type(by_type(walk_gcode(Test::slice({ TestMesh::overhang }, config)), true), skirt, 30 * 60);
    }
}

TEST_CASE("Gap fill acceleration", "[FeatureAccel]") {
    // Two hollow squares with a single classic perimeter leave gaps that are filled with gap fill (as in test_gaps.cpp).
    DynamicPrintConfig config = base_config();
    config.set_deserialize_strict({ { "support_material", false }, { "ironing", false }, { "perimeter_generator", "classic" },
                                    { "perimeters", 1 }, { "perimeter_extrusion_width", 0.35 }, { "first_layer_extrusion_width", 0.35 },
                                    { "gap_fill_acceleration", "50%" } });
    const auto accels = by_type(walk_gcode(Test::slice({ TestMesh::two_hollow_squares }, config)));
    check_type(accels, gap_fill, 1500);   // 50% of the perimeter acceleration
}
