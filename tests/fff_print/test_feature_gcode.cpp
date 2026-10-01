#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <vector>
#include <boost/algorithm/string/predicate.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/GCode/FanMover.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// feature_gcode: custom G-code at every extrusion type change, as in SuperSlicer.

namespace {

std::vector<std::string> lines_of(const std::string &gcode)
{
    std::vector<std::string> out;
    std::istringstream in(gcode);
    std::string line;
    while (std::getline(in, line))
        out.emplace_back(line);
    return out;
}

DynamicPrintConfig config_with(const std::string &feature_gcode)
{
    return DynamicPrintConfig::full_print_config_with({
        { "feature_gcode", feature_gcode },
        { "skirts",        1 },
    });
}

} // namespace

TEST_CASE("feature_gcode at every extrusion type change", "[FeatureGcode]") {
    const std::string gcode = Test::slice({ TestMesh::cube_20x20x20 }, config_with("; FEATURE {last_extrusion_role} -> {extrusion_role} L{layer_num}"));

    std::string pending;               // type announced by the last feature_gcode, not yet seen in a ;TYPE: line
    std::string last = "Unknown";      // type announced before
    int features = 0, types = 0;
    for (const std::string &line : lines_of(gcode)) {
        if (boost::starts_with(line, "; FEATURE ")) {
            ++ features;
            const size_t arrow = line.find(" -> ");
            const size_t layer = line.rfind(" L");
            REQUIRE(arrow != std::string::npos);
            REQUIRE(layer != std::string::npos);
            CHECK(line.substr(10, arrow - 10) == last);
            pending = line.substr(arrow + 4, layer - arrow - 4);
            last = pending;
        } else if (boost::starts_with(line, ";TYPE:") && line != ";TYPE:Custom") {
            ++ types;
            INFO(line);
            // The extrusion type change is announced right before its ;TYPE: line.
            CHECK(line.substr(6) == pending);
            pending.clear();
        }
    }
    CHECK(features > 5);
    CHECK(features == types);
}

TEST_CASE("feature_gcode conditions on the extrusion type", "[FeatureGcode]") {
    const std::string gcode = Test::slice({ TestMesh::cube_20x20x20 },
        config_with("{if extrusion_role == \"External perimeter\"}M117 external{endif}"));
    int m117 = 0, external = 0;
    for (const std::string &line : lines_of(gcode)) {
        if (line == "M117 external")
            ++ m117;
        else if (line == ";TYPE:External perimeter")
            ++ external;
    }
    CHECK(external > 0);
    CHECK(m117 == external);
}

TEST_CASE("feature_gcode empty: nothing is inserted", "[FeatureGcode]") {
    // Covered byte for byte by the golden G-code test; here just no stray placeholder output.
    const std::string gcode = Test::slice({ TestMesh::cube_20x20x20 }, config_with(""));
    CHECK(gcode.find("extrusion_role") == std::string::npos);
}

TEST_CASE("Fan mover and custom G-code blocks", "[FeatureGcode][FanMover]") {
    const std::string moves = "G1 F600\nG1 X10 E1\nG1 X20 E1\nG1 X30 E1\n";
    SECTION("a block without fan commands is looked through") {
        FanMover mover(gcfKlipper, 1.5f, true, false);
        const std::string out = mover.process_gcode(moves + "; custom gcode: feature_gcode\nM117 hello\n; custom gcode end: feature_gcode\nM106 S255\nG1 X40 E1\n", true);
        // The fan command moved back past the block, into the move before it.
        CHECK(out.find("M106 S255") < out.find("; custom gcode: feature_gcode"));
    }
    SECTION("a move of a custom block is not split") {
        FanMover mover(gcfKlipper, 1.5f, true, false);
        const std::string out = mover.process_gcode(
            "G1 F600\nG1 X10 E1\n; custom gcode: feature_gcode\nG1 X20\n; custom gcode end: feature_gcode\nG1 X30 E1\nM106 S255\nG1 X40 E1\n", true);
        CHECK(out.find("G1 X20\n") != std::string::npos);
        CHECK(out.find("X15") == std::string::npos);
    }
}
