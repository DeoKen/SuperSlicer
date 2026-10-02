#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <string>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Infill directions from OrcaSlicer: solid_infill_direction, rotate_solid_infill_direction, align_infill_direction_to_model.

namespace {

DynamicPrintConfig base_config()
{
    return DynamicPrintConfig::full_print_config_with({
        { "fill_pattern",        "rectilinear" },
        { "fill_density",        "20%" },
        { "top_fill_pattern",    "rectilinear" },
        { "bottom_fill_pattern", "rectilinear" },
        { "perimeters",          1 },
        { "bottom_solid_layers", 4 },
        { "top_solid_layers",    4 },
    });
}

// Slice with the object rotated around Z on the bed.
std::string slice_rotated(TestMesh mesh, const DynamicPrintConfig &config, double z_degrees)
{
    DynamicPrintConfig full = DynamicPrintConfig::full_print_config();
    full.apply(config);
    Print print;
    Model model;
    init_print({ mesh }, print, model, full);
    if (z_degrees != 0.) {
        model.objects.front()->instances.front()->set_rotation(Z, Geometry::deg2rad(z_degrees));
        model.objects.front()->ensure_on_bed();
        print.apply(model, full);
    }
    return Test::gcode(print);
}

// Direction (degrees, 0-180) of the longest total length of the extrusions of a type on a layer.
double dominant_angle(const std::string &gcode, const std::string &type, int layer_idx)
{
    std::map<int, double> length_by_degree;
    int         layer = -1;
    std::string current_type;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        const std::string &raw = line.raw();
        if (raw.rfind(";LAYER_CHANGE", 0) == 0)
            ++ layer;
        else if (raw.rfind(";TYPE:", 0) == 0)
            current_type = raw.substr(6);
        else if (layer == layer_idx && current_type == type && line.cmd_is("G1") && line.dist_E(self) > 0 && line.dist_XY(self) > 0.5) {
            double a = std::atan2(line.dist_Y(self), line.dist_X(self)) * 180. / M_PI;
            a = std::fmod(a + 360., 180.);
            length_by_degree[int(std::lround(a)) % 180] += line.dist_XY(self);
        }
    });
    REQUIRE(! length_by_degree.empty());
    auto best = length_by_degree.begin();
    for (auto it = length_by_degree.begin(); it != length_by_degree.end(); ++ it)
        if (it->second > best->second)
            best = it;
    return double(best->first);
}

// Last layer with extrusions of a type.
int last_layer(const std::string &gcode, const std::string &type)
{
    int layer = -1, last = -1;
    size_t pos = 0;
    const std::string tag = ";TYPE:" + type + "\n";
    while (pos < gcode.size()) {
        if (gcode.compare(pos, 13, ";LAYER_CHANGE") == 0)
            ++ layer;
        else if (gcode.compare(pos, tag.size(), tag) == 0)
            last = layer;
        pos = gcode.find('\n', pos);
        if (pos == std::string::npos)
            break;
        ++ pos;
    }
    return last;
}

// Difference of two directions, 0-90 degrees.
double angle_diff(double a, double b)
{
    double d = std::fmod(std::abs(a - b), 180.);
    return std::min(d, 180. - d);
}

} // namespace

TEST_CASE("Infill direction: defaults are stock", "[InfillDirection]") {
    DynamicPrintConfig config = base_config();
    const std::string stock = Test::slice({ TestMesh::cube_20x20x20 }, config);
    // solid_infill_direction -1 follows fill_angle: the same as setting it to fill_angle.
    config.set_deserialize_strict({ { "solid_infill_direction", "45" } });
    const std::string explicit_angle = Test::slice({ TestMesh::cube_20x20x20 }, config);
    auto body = [](const std::string &gcode) {
        const size_t pos = gcode.find('\n') + 1;
        return gcode.substr(pos, gcode.find("; prusaslicer_config = begin") - pos);
    };
    CHECK(body(stock) == body(explicit_angle));
}

TEST_CASE("Infill direction: solid infill direction", "[InfillDirection]") {
    DynamicPrintConfig config = base_config();
    const std::string stock = Test::slice({ TestMesh::cube_20x20x20 }, config);
    config.set_deserialize_strict({ { "solid_infill_direction", "0" } });
    const std::string turned = Test::slice({ TestMesh::cube_20x20x20 }, config);
    // Solid infill turned by 45 degrees, sparse infill unchanged.
    CHECK(angle_diff(dominant_angle(stock, "Solid infill", 1), dominant_angle(turned, "Solid infill", 1)) == 45.);
    CHECK(angle_diff(dominant_angle(stock, "Internal infill", 10), dominant_angle(turned, "Internal infill", 10)) == 0.);
}

TEST_CASE("Infill direction: rotate solid infill direction", "[InfillDirection]") {
    DynamicPrintConfig config = base_config();
    const std::string stock = Test::slice({ TestMesh::cube_20x20x20 }, config);
    // Stock: the solid infill turns by 90 degrees every layer.
    CHECK(angle_diff(dominant_angle(stock, "Solid infill", 1), dominant_angle(stock, "Solid infill", 2)) == 90.);
    config.set_deserialize_strict({ { "rotate_solid_infill_direction", "0" } });
    const std::string fixed = Test::slice({ TestMesh::cube_20x20x20 }, config);
    CHECK(angle_diff(dominant_angle(fixed, "Solid infill", 1), dominant_angle(fixed, "Solid infill", 2)) == 0.);
    // The sparse infill still alternates.
    CHECK(angle_diff(dominant_angle(fixed, "Internal infill", 10), dominant_angle(fixed, "Internal infill", 11)) == 90.);
}

TEST_CASE("Infill direction: align to model", "[InfillDirection]") {
    DynamicPrintConfig config = base_config();
    const std::string straight = slice_rotated(TestMesh::cube_20x20x20, config, 0.);
    SECTION("off: the infill keeps its direction on the bed") {
        const std::string rotated = slice_rotated(TestMesh::cube_20x20x20, config, 30.);
        CHECK(angle_diff(dominant_angle(straight, "Internal infill", 10), dominant_angle(rotated, "Internal infill", 10)) == 0.);
        CHECK(angle_diff(dominant_angle(straight, "Solid infill", 1), dominant_angle(rotated, "Solid infill", 1)) == 0.);
    }
    SECTION("on: the infill turns with the object") {
        config.set_deserialize_strict({ { "align_infill_direction_to_model", "1" } });
        const std::string rotated = slice_rotated(TestMesh::cube_20x20x20, config, 30.);
        CHECK(angle_diff(dominant_angle(straight, "Internal infill", 10), dominant_angle(rotated, "Internal infill", 10)) == 30.);
        CHECK(angle_diff(dominant_angle(straight, "Solid infill", 1), dominant_angle(rotated, "Solid infill", 1)) == 30.);
        const int top = last_layer(straight, "Top solid infill");
        REQUIRE(top > 0);
        CHECK(angle_diff(dominant_angle(straight, "Top solid infill", top), dominant_angle(rotated, "Top solid infill", top)) == 30.);
    }
}

TEST_CASE("Infill direction: ironing follows the solid infill direction", "[InfillDirection]") {
    DynamicPrintConfig config = base_config();
    config.set_deserialize_strict({ { "ironing", "1" }, { "ironing_type", "top" } });
    const std::string stock = Test::slice({ TestMesh::cube_20x20x20 }, config);
    config.set_deserialize_strict({ { "solid_infill_direction", "0" } });
    const std::string turned = Test::slice({ TestMesh::cube_20x20x20 }, config);
    const int top = last_layer(stock, "Ironing");
    REQUIRE(top > 0);
    CHECK(angle_diff(dominant_angle(stock, "Ironing", top), dominant_angle(turned, "Ironing", top)) == 45.);
}

TEST_CASE("Infill direction: the bridging angle override turns with the object", "[InfillDirection]") {
    DynamicPrintConfig config = base_config();
    config.set_deserialize_strict({ { "bridge_angle", "90" } });
    const std::string straight = slice_rotated(TestMesh::bridge, config, 0.);
    const int bridge_layer = [&straight]() {
        // First layer with bridge infill.
        int layer = -1;
        size_t pos = 0;
        while ((pos = straight.find('\n', pos)) != std::string::npos) {
            ++ pos;
            if (straight.compare(pos, 13, ";LAYER_CHANGE") == 0)
                ++ layer;
            else if (straight.compare(pos, 19, ";TYPE:Bridge infill") == 0)
                return layer;
        }
        return -1;
    }();
    REQUIRE(bridge_layer > 0);
    SECTION("off") {
        const std::string rotated = slice_rotated(TestMesh::bridge, config, 30.);
        CHECK(angle_diff(dominant_angle(straight, "Bridge infill", bridge_layer), dominant_angle(rotated, "Bridge infill", bridge_layer)) == 0.);
    }
    SECTION("on") {
        config.set_deserialize_strict({ { "align_infill_direction_to_model", "1" } });
        const std::string rotated = slice_rotated(TestMesh::bridge, config, 30.);
        CHECK(angle_diff(dominant_angle(straight, "Bridge infill", bridge_layer), dominant_angle(rotated, "Bridge infill", bridge_layer)) == 30.);
    }
}
