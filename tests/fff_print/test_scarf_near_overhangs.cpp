#include <catch2/catch_test_macros.hpp>

#include <string>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Fork: scarf_seam_not_near_overhangs (doc section 41), a normal seam instead of the scarf joint when the seam is near an overhang
// or the scarf joint goes around a sharp corner.

namespace {

struct ScarfCount
{
    // Scarf joints (external perimeter moves that raise Z while extruding) on layers with / without an overhang perimeter.
    int on_overhang_layers  = 0;
    int on_other_layers     = 0;
    int overhang_layers     = 0;
};

ScarfCount count_scarfs(const std::string &gcode)
{
    ScarfCount count;
    std::string type;
    bool layer_has_overhang = false, in_scarf = false;
    int  layer_scarfs = 0;
    auto end_layer = [&]() {
        (layer_has_overhang ? count.on_overhang_layers : count.on_other_layers) += layer_scarfs;
        count.overhang_layers += layer_has_overhang;
        layer_has_overhang = false;
        layer_scarfs = 0;
    };
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        const std::string &raw = line.raw();
        if (raw.rfind(";LAYER_CHANGE", 0) == 0) {
            end_layer();
        } else if (raw.rfind(";TYPE:", 0) == 0) {
            type = raw.substr(6);
            layer_has_overhang |= type == "Overhang perimeter";
            in_scarf = false;
        } else if (line.cmd_is("G1")) {
            const bool scarf_move = type == "External perimeter" && line.has(Slic3r::Z) && line.has(Slic3r::E) && line.e() > 0;
            if (scarf_move && ! in_scarf)
                ++ layer_scarfs;
            in_scarf = scarf_move;
        }
    });
    end_layer();
    return count;
}

} // namespace

TEST_CASE("No scarf joint near overhangs", "[Seams][Scarf]") {
    auto config = DynamicPrintConfig::full_print_config_with({
        { "scarf_seam_placement", "everywhere" },
        { "seam_position",        "rear" },
        { "perimeters",           2 },
        { "layer_height",         0.2 },
        { "first_layer_height",   0.2 },
    });
    // 20 x 20 x 10 cube with a 10 x 10 hole, turned on its side: a horizontal hole through the rear wall.
    TriangleMesh part = Test::mesh(TestMesh::cube_with_hole);
    part.rotate_x(float(PI / 2));
    part.translate(0, 0, -part.bounding_box().min.z());
    const ScarfCount off = count_scarfs(Test::slice({ part }, config));
    config.set_deserialize_strict({ { "scarf_seam_not_near_overhangs", "1" }, { "scarf_seam_overhang_distance", "6" } });
    const ScarfCount on = count_scarfs(Test::slice({ part }, config));

    // The model has overhang layers with scarf joints when the option is off.
    REQUIRE(off.overhang_layers > 0);
    REQUIRE(off.on_overhang_layers > 0);
    REQUIRE(off.on_other_layers > 0);
    // With the option, fewer scarf joints on the overhang layers and on the layers where the hole splits the rear wall
    // (the scarf joint would go around the edge of the hole).
    CHECK(on.on_overhang_layers < off.on_overhang_layers);
    CHECK(on.on_other_layers < off.on_other_layers);

    SECTION("a scarf joint along one flat face is kept") {
        // 20 mm cube, 10 mm scarf joint: it stays on the rear face, the corner the seam is on doesn't count.
        config.set_deserialize_strict({ { "scarf_seam_length", "10" }, { "scarf_seam_not_near_overhangs", "0" } });
        const ScarfCount cube_off = count_scarfs(Test::slice({ TestMesh::cube_20x20x20 }, config));
        config.set_deserialize_strict({ { "scarf_seam_not_near_overhangs", "1" } });
        const ScarfCount cube_on = count_scarfs(Test::slice({ TestMesh::cube_20x20x20 }, config));
        REQUIRE(cube_off.on_other_layers > 0);
        CHECK(cube_on.on_other_layers == cube_off.on_other_layers);
    }

    SECTION("off by default") {
        auto defaults = DynamicPrintConfig::full_print_config();
        CHECK_FALSE(defaults.opt_bool("scarf_seam_not_near_overhangs"));
    }
}

// Fork: the preview shows a seam marker at both ends of each scarf joint ramp (GCodeProcessor, "Seams" option).
TEST_CASE("Seam markers at both ends of a scarf joint", "[Seams][Scarf]") {
    auto config = DynamicPrintConfig::full_print_config_with({
        { "seam_position",      "rear" },
        { "scarf_seam_length",  "10" },
        { "layer_height",       0.2 },
        { "first_layer_height", 0.2 },
    });
    const auto count_seam_markers = [](const DynamicPrintConfig &config, const std::string &gcode) {
        PrintConfig print_config;
        print_config.apply(config, true);
        GCodeProcessor processor;
        processor.apply_config(print_config);
        processor.initialize("scarf_seam_markers.gcode");
        processor.initialize_result_moves();
        processor.process_buffer(gcode);
        processor.finalize(false);
        int seams = 0;
        for (const GCodeProcessorResult::MoveVertex &move : processor.get_result().moves)
            seams += move.type == EMoveType::Seam;
        return seams;
    };
    const std::string plain = Test::slice({ TestMesh::cube_20x20x20 }, config);
    const int plain_markers = count_seam_markers(config, plain);
    config.set_deserialize_strict({ { "scarf_seam_placement", "everywhere" } });
    const std::string scarf = Test::slice({ TestMesh::cube_20x20x20 }, config);
    const ScarfCount ramps = count_scarfs(scarf);

    REQUIRE(plain_markers > 0);
    REQUIRE(ramps.on_other_layers > 0);
    // The marker at the end of each loop, plus one at the start and one at the end of each ramp.
    CHECK(count_seam_markers(config, scarf) == plain_markers + 2 * ramps.on_other_layers);
}
