#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "test_data.hpp"
#include "gcode_walk.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;
using Catch::Approx;

// Dynamic overhang speeds and fan speeds with graphs, ported from SuperSlicer.

namespace {

// Klipper printer, fan disabled for the first 3 layers, no ramp and no short layer cooling or slow down,
// so that the speeds and fan speeds are applied as set.
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
        { "perimeter_speed",           60 },
        { "external_perimeter_speed",  "50%" },
        { "bridge_speed",              40 },
        { "fill_density",              "15%" },
    });
}

const std::string overhang_perimeter = "Overhang perimeter";

bool is_perimeter(const std::string &type)
{
    return type == "Perimeter" || type == "External perimeter" || type == overhang_perimeter;
}

std::set<int> fans_of_type(const std::vector<Extrusion> &extrusions, const std::string &type, int first_layer)
{
    std::set<int> out;
    for (const Extrusion &e : extrusions)
        if (e.type == type && e.layer >= first_layer)
            out.insert(e.fan);
    return out;
}

// Feed rates (mm/s, rounded) of the perimeters from layer 1 on.
std::set<int> perimeter_speeds(const std::vector<Extrusion> &extrusions)
{
    std::set<int> out;
    for (const Extrusion &e : extrusions)
        if (is_perimeter(e.type) && e.layer >= 1)
            out.insert(int(std::lround(e.F / 60.)));
    return out;
}

} // namespace

TEST_CASE("Graph data", "[DynamicOverhang]") {
    SECTION("SuperSlicer format, read and written back") {
        GraphData graph;
        REQUIRE(graph.deserialize("0:5:1:0x0:25x10:50x40:75x70:100x100"));
        CHECK(graph.type == GraphData::GraphType::LINEAR);
        CHECK(graph.data().size() == 5);
        CHECK(graph.serialize() == "0:5:1:0x0:25x10:50x40:75x70:100x100");
        // A disabled SuperSlicer graph is read as well.
        GraphData disabled;
        REQUIRE(disabled.deserialize("!0:2:1:0x100:100x20"));
        CHECK(disabled.data().size() == 2);
    }
    SECTION("Invalid strings are refused") {
        GraphData graph;
        CHECK(! graph.deserialize("abc"));
        CHECK(! graph.deserialize("0:3:1:0x0:50x10"));      // end index past the points
        CHECK(! graph.deserialize("0:2:1:50x0:10x10"));     // x decreasing
        CHECK(! graph.deserialize("0:2:7:0x0:10x10"));      // unknown type
    }
    SECTION("Linear and square") {
        GraphData graph(0, 3, GraphData::GraphType::LINEAR, { { 0, 0 }, { 50, 40 }, { 100, 100 } });
        CHECK(graph.interpolate(25) == Approx(20));
        CHECK(graph.interpolate(75) == Approx(70));
        CHECK(graph.interpolate(-10) == Approx(0));
        CHECK(graph.interpolate(150) == Approx(100));
        graph.type = GraphData::GraphType::SQUARE;
        CHECK(graph.interpolate(25) == Approx(0));
        CHECK(graph.interpolate(50) == Approx(40));
        CHECK(graph.interpolate(99) == Approx(40));
    }
    SECTION("Spline goes through the points, flat at the ends") {
        const GraphData graph(0, 3, GraphData::GraphType::SPLINE, { { 0, 0 }, { 50, 40 }, { 100, 100 } });
        CHECK(graph.interpolate(0) == Approx(0));
        CHECK(graph.interpolate(50) == Approx(40));
        CHECK(graph.interpolate(100) == Approx(100));
        CHECK(graph.interpolate(1) == Approx(0).margin(0.5));
        const double mid = graph.interpolate(25);
        CHECK(mid > 0.);
        CHECK(mid < 40.);
    }
    SECTION("Without points") {
        CHECK(GraphData().interpolate(30) == 0.);
    }
    SECTION("One graph per extruder") {
        ConfigOptionGraphs graphs;
        REQUIRE(graphs.deserialize("0:2:1:0x100:100x20,0:2:0:0x80:100x30"));
        REQUIRE(graphs.values.size() == 2);
        CHECK(graphs.values[1].type == GraphData::GraphType::SQUARE);
        CHECK(graphs.serialize() == "0:2:1:0x100:100x20,0:2:0:0x80:100x30");
    }
}

TEST_CASE("PrusaSlicer dynamic overhang settings are converted to graphs", "[DynamicOverhang]") {
    SECTION("Speeds, enabled") {
        DynamicPrintConfig config;
        config.set_deserialize_strict({
            { "enable_dynamic_overhang_speeds", "1" },
            { "perimeter_speed",                "60" },
            { "external_perimeter_speed",       "50%" },
            { "overhang_speed_0",               "15" },
            { "overhang_speed_1",               "15" },
            { "overhang_speed_2",               "20" },
            { "overhang_speed_3",               "25" },
        });
        PrintConfigDef::handle_legacy_composite(config);
        CHECK(! config.has("overhang_speed_0"));
        CHECK(! config.has("overhang_speed_3"));
        // The slowest speed is the overhangs speed, the others a % of the way to the external perimeter speed (30 mm/s).
        CHECK(config.opt_serialize("overhangs_speed") == "15");
        CHECK(config.opt_serialize("overhangs_dynamic_speed") == "0:5:1:0x0:25x0:50x33:75x67:100x100");
        // Stock spread the values over one line width: the same width past the overhang threshold.
        CHECK(config.opt_serialize("overhangs_graph_range") == "100%");
    }
    SECTION("Speeds, disabled: only removed") {
        DynamicPrintConfig config;
        config.set_deserialize_strict({
            { "enable_dynamic_overhang_speeds", "0" },
            { "overhang_speed_0",               "15" },
        });
        PrintConfigDef::handle_legacy_composite(config);
        CHECK(! config.has("overhang_speed_0"));
        CHECK(! config.has("overhangs_speed"));
        CHECK(! config.has("overhangs_dynamic_speed"));
    }
    SECTION("Fan speeds") {
        DynamicPrintConfig config;
        config.set_deserialize_strict({
            { "enable_dynamic_fan_speeds",    "1,0" },
            { "overhang_fan_speed_0",         "75,10" },
            { "overhang_fan_speed_1",         "60,10" },
            { "overhang_fan_speed_2",         "45,10" },
            { "overhang_fan_speed_3",         "35,10" },
            { "external_perimeter_fan_speed", "-1,-1" },
            { "perimeter_fan_speed",          "-1,-1" },
            { "default_fan_speed",            "40,40" },
        });
        PrintConfigDef::handle_legacy_composite(config);
        CHECK(! config.has("overhang_fan_speed_0"));
        const auto *graphs = config.option<ConfigOptionGraphs>("overhangs_dynamic_fan_speed");
        REQUIRE(graphs != nullptr);
        REQUIRE(graphs->values.size() == 2);
        // At 100% overlap the default fan speed.
        CHECK(graphs->values[0].serialize() == "0:5:1:0x75:25x60:50x45:75x35:100x40");
        // The disabled extruder keeps the default graph.
        CHECK(graphs->values[1] == print_config_def.get("overhangs_dynamic_fan_speed")->get_default_value<ConfigOptionGraphs>()->values.front());
    }
    SECTION("Old overhangs_width_speed: the graph range past the threshold (50%) + half the line width") {
        const auto converted = [](const std::string &value) {
            DynamicPrintConfig config;
            config.set_deserialize_strict({ { "overhangs_width_speed", value } });
            return config.has("overhangs_graph_range") ? config.opt_serialize("overhangs_graph_range") : std::string("default");
        };
        CHECK(converted("155%") == "55%");
        CHECK(converted("30%") == "0%");
        CHECK(converted("0.6") == "0.2");
        // The old default: the new default.
        CHECK(converted("100%") == "default");
    }
}

TEST_CASE("Overhangs speed", "[DynamicOverhang]") {
    DynamicPrintConfig config = base_config();
    SECTION("Default: the bridges speed") {
        const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::overhang }, config));
        REQUIRE(count_type(extrusions, overhang_perimeter) > 0);
        for (const Extrusion &e : extrusions)
            if (e.type == overhang_perimeter)
                CHECK(e.F == Approx(40 * 60));
    }
    SECTION("Percent of the bridges speed") {
        config.set_deserialize_strict({ { "overhangs_speed", "50%" } });
        const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::overhang }, config));
        REQUIRE(count_type(extrusions, overhang_perimeter) > 0);
        for (const Extrusion &e : extrusions)
            if (e.type == overhang_perimeter)
                CHECK(e.F == Approx(20 * 60));
    }
}

TEST_CASE("Dynamic overhang speeds", "[DynamicOverhang]") {
    DynamicPrintConfig config = base_config();
    config.set_deserialize_strict({
        { "enable_dynamic_overhang_speeds", "1" },
        { "overhangs_speed",                "7" },
    });
    SECTION("Graph at 0%: overhangs speed when sticking out, perimeter speed when supported") {
        config.set_deserialize_strict({ { "overhangs_dynamic_speed", "0:2:1:0x0:99x0" } });
        const std::set<int> speeds = perimeter_speeds(walk_gcode(Test::slice({ TestMesh::overhang }, config)));
        CHECK(speeds.count(7) == 1);
        // Supported perimeters keep their speed: 100% overlap is always the perimeter speed.
        CHECK(speeds.count(60) == 1);
        CHECK(speeds.count(30) == 1);
    }
    SECTION("Graph at 100%: the perimeter speeds only") {
        config.set_deserialize_strict({ { "overhangs_dynamic_speed", "0:2:1:0x100:100x100" } });
        const std::set<int> speeds = perimeter_speeds(walk_gcode(Test::slice({ TestMesh::overhang }, config)));
        CHECK(speeds.count(7) == 0);
        for (int speed : speeds)
            CHECK((speed == 60 || speed == 30));
    }
    SECTION("Partial overhangs between the overhangs speed and the perimeter speed") {
        config.set_deserialize_strict({ { "overhangs_dynamic_speed", "0:2:1:0x0:100x100" }, { "overhangs_graph_range", "2" } });
        const std::set<int> speeds = perimeter_speeds(walk_gcode(Test::slice({ TestMesh::sphere_50mm }, config)));
        CHECK(std::any_of(speeds.begin(), speeds.end(), [](int speed) { return speed > 7 && speed < 30; }));
    }
}

TEST_CASE("Dynamic overhang fan speeds", "[DynamicOverhang]") {
    DynamicPrintConfig config = base_config();
    config.set_deserialize_strict({
        { "enable_dynamic_fan_speeds", "1" },
        { "overhangs_fan_speed",       "60" },
    });
    SECTION("Graph value used, overhangs fan speed replaced") {
        config.set_deserialize_strict({ { "overhangs_dynamic_fan_speed", "0:2:1:0x90:100x90" } });
        const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::overhang }, config));
        const std::set<int> fans = fans_of_type(extrusions, overhang_perimeter, 3);
        CHECK(fans.count(90) == 1);
        CHECK(fans.count(60) == 0);
    }
    SECTION("Lower than the layer fan speed") {
        config.set_deserialize_strict({ { "overhangs_dynamic_fan_speed", "0:2:1:0x10:100x10" } });
        const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::overhang }, config));
        CHECK(fans_of_type(extrusions, overhang_perimeter, 3).count(10) == 1);
        // The supported perimeters keep the layer fan speed.
        for (int fan : fans_of_type(extrusions, "Perimeter", 3))
            CHECK((fan == 30 || fan == 10));
        CHECK(fans_of_type(extrusions, "Internal infill", 3) == std::set<int>{ 30 });
    }
    SECTION("Fan disabled on the first layers") {
        config.set_deserialize_strict({ { "overhangs_dynamic_fan_speed", "0:2:1:0x90:100x90" }, { "disable_fan_first_layers", "1000" } });
        for (const Extrusion &e : walk_gcode(Test::slice({ TestMesh::overhang }, config)))
            CHECK(e.fan == 0);
    }
    SECTION("Disabled: the overhangs fan speed") {
        config.set_deserialize_strict({ { "enable_dynamic_fan_speeds", "0" }, { "overhangs_dynamic_fan_speed", "0:2:1:0x90:100x90" } });
        const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::overhang }, config));
        CHECK(fans_of_type(extrusions, overhang_perimeter, 3) == std::set<int>{ 60 });
    }
}

namespace {

// Sphere sliced at 0.2 mm with 0.4 mm perimeters (0.4 mm nozzle).
DynamicPrintConfig sphere_config()
{
    DynamicPrintConfig config = base_config();
    config.set_deserialize_strict({
        { "layer_height",                       "0.2" },
        { "first_layer_height",                 "0.2" },
        { "nozzle_diameter",                    "0.4" },
        { "perimeter_extrusion_width",          "0.4" },
        { "external_perimeter_extrusion_width", "0.4" },
    });
    return config;
}

// Smallest wall angle from vertical (degrees) of the lower half of the sphere among the layers with an extrusion matching pred.
// The sphere sits on the bed, its radius is half the height of the print.
template<typename Pred>
double steepest_wall_angle(const std::vector<Extrusion> &extrusions, Pred pred)
{
    int last_layer = 0;
    for (const Extrusion &e : extrusions)
        last_layer = std::max(last_layer, e.layer);
    const double layer_height = 0.2;
    const double radius       = 0.5 * (last_layer + 1) * layer_height;
    double       angle        = 90.;
    for (const Extrusion &e : extrusions)
        if (e.layer >= 1 && pred(e)) {
            const double z = (e.layer + 1) * layer_height;
            if (z < radius)
                angle = std::min(angle, std::asin(std::clamp((radius - z) / radius, 0., 1.)) * 180. / M_PI);
        }
    return angle;
}

} // namespace

TEST_CASE("Overhang threshold", "[DynamicOverhang]") {
    DynamicPrintConfig config = sphere_config();
    const auto is_overhang = [](const Extrusion &e) { return e.type == overhang_perimeter; };
    // The middle of the external perimeter past the edge of the layer below by the threshold: its outer edge sticks out by
    // threshold + 0.2 mm on every 0.2 mm layer.
    SECTION("Default 50%: the steepest overhang perimeter at atan(0.4 / 0.2)") {
        REQUIRE(config.opt_serialize("overhangs_threshold") == "50%");
        const double angle = steepest_wall_angle(walk_gcode(Test::slice({ TestMesh::sphere_50mm }, config)), is_overhang);
        CHECK(angle == Approx(63.4).margin(3.));
    }
    SECTION("0%: atan(0.2 / 0.2)") {
        config.set_deserialize_strict({ { "overhangs_threshold", "0%" } });
        const double angle = steepest_wall_angle(walk_gcode(Test::slice({ TestMesh::sphere_50mm }, config)), is_overhang);
        CHECK(angle == Approx(45.).margin(3.));
    }
    SECTION("In mm: 0.1 mm, atan(0.3 / 0.2)") {
        config.set_deserialize_strict({ { "overhangs_threshold", "0.1" } });
        const double angle = steepest_wall_angle(walk_gcode(Test::slice({ TestMesh::sphere_50mm }, config)), is_overhang);
        CHECK(angle == Approx(56.3).margin(3.));
    }
    SECTION("50% and 0.2 mm give the same G-code") {
        const std::string percent = Test::slice({ TestMesh::overhang }, config);
        config.set_deserialize_strict({ { "overhangs_threshold", "0.2" } });
        std::string mm = Test::slice({ TestMesh::overhang }, config);
        // Only the header line (time) and the config block at the end differ.
        const auto body = [](const std::string &gcode) {
            const size_t begin = gcode.find('\n');
            return gcode.substr(begin, gcode.find("; overhangs_threshold") - begin);
        };
        CHECK(body(percent) == body(mm));
    }
}

TEST_CASE("Dynamic overhang graphs start at the overhang threshold", "[DynamicOverhang]") {
    DynamicPrintConfig config = sphere_config();
    // No overhang hysteresis (default 15 %): it moves the switch to overhang by up to 0.06 mm either way, here 45° -> 49.5°.
    config.set_deserialize_strict({
        { "overhangs_threshold",  "0%" },
        { "overhangs_graph_range", "2" },
        { "overhangs_hysteresis", "0" },
    });
    SECTION("Speeds: the perimeters steeper than the threshold keep their speed") {
        config.set_deserialize_strict({
            { "enable_dynamic_overhang_speeds", "1" },
            { "overhangs_speed",                "7" },
            { "overhangs_dynamic_speed",        "0:2:1:0x0:100x100" },
        });
        const double angle = steepest_wall_angle(walk_gcode(Test::slice({ TestMesh::sphere_50mm }, config)), [](const Extrusion &e) {
            const int speed = int(std::lround(e.F / 60.));
            return is_perimeter(e.type) && speed != 60 && speed != 30;
        });
        CHECK(angle == Approx(45.).margin(3.));
    }
    SECTION("Fan speeds: the graph only past the threshold") {
        config.set_deserialize_strict({
            { "enable_dynamic_fan_speeds",   "1" },
            { "overhangs_dynamic_fan_speed", "0:2:1:0x90:100x90" },
        });
        const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::sphere_50mm }, config));
        const double angle = steepest_wall_angle(extrusions, [](const Extrusion &e) { return is_perimeter(e.type) && e.layer >= 3 && e.fan == 90; });
        CHECK(angle == Approx(45.).margin(3.));
    }
}

TEST_CASE("Dynamic overhang graphs end the graph range past the overhang threshold", "[DynamicOverhang]") {
    DynamicPrintConfig config = sphere_config();
    // Graphs from 0 mm (45°) to 0.1 mm (outer edge 0.3 mm per 0.2 mm layer: 56.3°) past the edge of the layer below.
    config.set_deserialize_strict({
        { "overhangs_threshold",            "0%" },
        { "overhangs_graph_range",          "25%" },
        { "enable_dynamic_overhang_speeds", "1" },
        { "overhangs_speed",                "7" },
        { "overhangs_dynamic_speed",        "0:2:1:0x0:100x100" },
    });
    const std::vector<Extrusion> extrusions = walk_gcode(Test::slice({ TestMesh::sphere_50mm }, config));
    const double slowest = steepest_wall_angle(extrusions, [](const Extrusion &e) { return is_perimeter(e.type) && std::lround(e.F / 60.) == 7; });
    CHECK(slowest == Approx(56.3).margin(3.));
}
