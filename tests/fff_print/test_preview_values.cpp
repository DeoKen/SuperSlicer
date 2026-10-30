#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using Catch::Approx;

// Fork (doc section 42): the points the time estimator inserts inside a move for the Actual speed view (internal_only)
// have the move's own feed rate, width, height, flow, fan and temperature. Stock blended them with the previous move's,
// so the Speed, Height, Width and Volumetric flow views drew a fade over a long move after an overhang perimeter piece.

TEST_CASE("Preview: points inside a move have the move's own values", "[GCodeProcessor][Preview]") {
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({ { "machine_limits_usage", "time_estimate_only" }, { "machine_max_acceleration_extruding", "5000" },
                                    { "machine_max_acceleration_x", "5000" }, { "machine_max_acceleration_y", "5000" } });
    PrintConfig print_config;
    print_config.apply(config, true);
    GCodeProcessor processor;
    processor.apply_config(print_config);
    processor.initialize("preview_values.gcode");
    processor.initialize_result_moves();
    // A short overhang piece, then directly (no travel) a long external perimeter move with other values.
    processor.process_buffer(
        "G90\nM83\nG1 Z0.2 F600\nG1 X10 Y10 F30000\n"
        ";TYPE:Overhang perimeter\n;WIDTH:0.36\n;HEIGHT:0.36\nG1 F15000\nG1 X12 Y10 E0.1\n"
        ";TYPE:External perimeter\n;WIDTH:0.4\n;HEIGHT:0.26\nG1 F18000\nG1 X142 Y10 E5\n"
        "G1 X142 Y20 F30000\n");
    processor.finalize(false);

    int inside = 0;
    for (const GCodeProcessorResult::MoveVertex &move : processor.get_result().moves) {
        if (! move.internal_only || move.type != EMoveType::Extrude || move.position.x() < 12.5f)
            continue;
        ++ inside;
        CHECK(move.width == Approx(0.4));
        CHECK(move.height == Approx(0.26));
        CHECK(move.feedrate == Approx(300.));
    }
    // The long move accelerates, cruises and brakes: two points inside it.
    CHECK(inside == 2);
}

TEST_CASE("Preview: points inside the moves of a sliced part with overhangs", "[GCodeProcessor][Preview]") {
    // 20 x 20 x 10 cube with a 10 x 10 hole, on its side: overhang perimeter pieces at the top of the hole, followed by
    // wall moves with other values.
    TriangleMesh part = Test::mesh(Test::TestMesh::cube_with_hole);
    part.rotate_x(float(PI / 2));
    part.translate(0, 0, -part.bounding_box().min.z());
    auto config = DynamicPrintConfig::full_print_config_with({ { "layer_height", 0.2 }, { "first_layer_height", 0.2 } });
    const std::string gcode = Test::slice({ part }, config);

    PrintConfig print_config;
    print_config.apply(config, true);
    GCodeProcessor processor;
    processor.apply_config(print_config);
    processor.initialize("preview_values_part.gcode");
    processor.initialize_result_moves();
    processor.process_buffer(gcode);
    processor.finalize(false);

    const auto &moves = processor.get_result().moves;
    REQUIRE(gcode.find(";TYPE:Overhang perimeter") != std::string::npos);
    int inside = 0, differing = 0;
    for (size_t i = 0; i + 1 < moves.size(); ++ i) {
        if (! moves[i].internal_only || moves[i].type != EMoveType::Extrude)
            continue;
        // The move this point belongs to ends at the next vertex that is not an inserted one.
        size_t j = i + 1;
        while (j < moves.size() && moves[j].internal_only)
            ++ j;
        REQUIRE(j < moves.size());
        ++ inside;
        differing += moves[i].width != moves[j].width || moves[i].height != moves[j].height ||
                     moves[i].feedrate != moves[j].feedrate || moves[i].mm3_per_mm != moves[j].mm3_per_mm;
    }
    CHECK(inside > 0);
    CHECK(differing == 0);
}
