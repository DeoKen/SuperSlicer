#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Per region G-code (region_gcode, ported from SuperSlicer), used by the pressure advance calibration.

namespace {

size_t count(const std::string &gcode, const std::string &what)
{
    size_t n = 0;
    for (size_t pos = gcode.find(what); pos != std::string::npos; pos = gcode.find(what, pos + what.size()))
        ++ n;
    return n;
}

// One object with two 10 mm cubes 20 mm apart, each part with its own settings (set_part(part index, its config)).
std::string slice_two_parts(const DynamicPrintConfig &config_in, const std::function<void(int, ModelConfig&)> &set_part)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({ { "layer_height", "0.2" }, { "first_layer_height", "0.2" }, { "fill_density", "20%" } });
    config.apply(config_in);
    Model        model;
    ModelObject *object = model.add_object();
    object->name = "parts.stl";
    for (int i = 0; i < 2; ++ i) {
        ModelVolume *volume = object->add_volume(mesh(TestMesh::cube_20x20x20));
        volume->scale(Vec3d(0.5, 0.5, 0.5));
        volume->set_offset(Vec3d(i * 20., 0., 0.));
        set_part(i, volume->config);
    }
    object->add_instance();
    model.center_instances_around_point({ 100, 100 });
    object->ensure_on_bed();
    Print print;
    print.apply(model, config);
    print.validate();
    return gcode(print);
}

} // namespace

TEST_CASE("Per region G-code", "[RegionGcode]") {
    SECTION("Empty by default: nothing added") {
        REQUIRE(DynamicPrintConfig::full_print_config().opt_string("region_gcode").empty());
    }
    SECTION("Print setting: before the extrusions of every layer, with the layer placeholders") {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.set_deserialize_strict({ { "layer_height", "0.2" }, { "first_layer_height", "0.2" },
                                        { "region_gcode", "; REGION_MARK layer [layer_num]" } });
        const std::string out = slice({ TestMesh::cube_20x20x20 }, config);
        CHECK(count(out, "; REGION_MARK layer 0\n") >= 1);
        CHECK(count(out, "; REGION_MARK layer 50\n") >= 1);
        // Before the first extrusion of the layer.
        const size_t mark         = out.find("; REGION_MARK layer 1\n");
        REQUIRE(mark != std::string::npos);
        const size_t layer_change = out.rfind(";LAYER_CHANGE", mark);
        REQUIRE(layer_change != std::string::npos);
        CHECK(mark < out.find(";TYPE:", layer_change));
    }
    SECTION("Part setting: each part prints its own code on every layer, also when the parts share an object") {
        const std::string out = slice_two_parts(DynamicPrintConfig(), [](int part, ModelConfig &config) {
            config.set_key_value("region_gcode", new ConfigOptionString(part == 0 ? "; PART_A" : "; PART_B"));
        });
        const size_t a = count(out, "; PART_A\n");
        const size_t b = count(out, "; PART_B\n");
        // 10 mm at 0.2 mm: 50 layers, perimeters and infill of each part on every layer.
        CHECK(a >= 50);
        CHECK(b >= 50);
        CHECK(a == b);
    }
}
