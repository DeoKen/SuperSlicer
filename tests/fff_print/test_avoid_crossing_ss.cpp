#include <catch2/catch_test_macros.hpp>

#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <boost/algorithm/string/predicate.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Avoid crossing perimeters options ported from SuperSlicer: avoid_crossing_not_first_layer, avoid_crossing_top,
// avoid_travel_island (+ weight).

namespace {

DynamicPrintConfig config_with(std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> items)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config_with({
        { "avoid_crossing_perimeters",            true },
        { "avoid_crossing_perimeters_max_detour", 0 },
        { "skirts",                               0 },
        { "fill_density",                         "20%" },
    });
    config.set_deserialize_strict(items);
    return config;
}

// G-code of each layer, keyed by layer index (from ;LAYER_CHANGE).
std::vector<std::string> gcode_layers(const std::string &gcode)
{
    std::vector<std::string> out(1);
    std::istringstream in(gcode);
    std::string line;
    while (std::getline(in, line)) {
        if (boost::starts_with(line, ";LAYER_CHANGE"))
            out.emplace_back();
        out.back() += line + "\n";
    }
    return out;
}

// One object made of two 20 mm cubes placed diagonally with a gap: every layer has two islands.
TriangleMesh two_cubes()
{
    TriangleMesh a = Test::mesh(TestMesh::cube_20x20x20);
    TriangleMesh b = Test::mesh(TestMesh::cube_20x20x20);
    b.translate(30.f, 30.f, 0.f);
    a.merge(b);
    return a;
}

// Length (mm) of the travel moves outside of the given areas (bed coordinates), per G-code.
double travel_outside(const std::string &gcode, const ExPolygons &areas)
{
    double length = 0;
    bool   known  = false;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if ((line.cmd_is("G0") || line.cmd_is("G1")) && (line.has(Slic3r::X) || line.has(Slic3r::Y))) {
            if (known && line.dist_E(self) <= 0 && line.dist_XY(self) > 0) {
                const Polyline pl{ Point::new_scale(self.x(), self.y()), Point::new_scale(line.new_X(self), line.new_Y(self)) };
                for (const Polyline &outside : diff_pl(Polylines{ pl }, areas))
                    length += unscaled(outside.length());
            }
            known = true;
        }
    });
    return length;
}

} // namespace

TEST_CASE("Avoid crossing perimeters: not on the first layer", "[AvoidCrossing]") {
    const std::string with_option = Test::slice({ TestMesh::cube_with_hole }, config_with({ { "avoid_crossing_not_first_layer", true } }));
    const std::string off         = Test::slice({ TestMesh::cube_with_hole }, config_with({ { "avoid_crossing_perimeters", false } }));
    const std::string all_layers  = Test::slice({ TestMesh::cube_with_hole }, config_with({ { "avoid_crossing_not_first_layer", false } }));
    // Layer 1 in the vector is the first printed layer (index 0 holds the start G-code).
    REQUIRE(gcode_layers(with_option).size() > 2);
    CHECK(gcode_layers(with_option)[1] == gcode_layers(off)[1]);
    CHECK(gcode_layers(all_layers)[1] != gcode_layers(off)[1]);
}

TEST_CASE("Avoid crossing perimeters: smallest crossing between islands", "[AvoidCrossing]") {
    auto slice_and_measure = [](bool island) {
        Print print;
        Model model;
        Test::init_print({ two_cubes() }, print, model, config_with({ { "avoid_travel_island", island }, { "avoid_crossing_not_first_layer", false } }));
        const std::string gcode = Test::gcode(print);
        // The islands of a middle layer, in bed coordinates: travels outside them cross the void.
        const PrintObject &object = *print.objects().front();
        ExPolygons islands = object.layers()[object.layers().size() / 2]->lslices;
        for (ExPolygon &expoly : islands)
            expoly.translate(object.instances().front().shift);
        return travel_outside(gcode, islands);
    };
    const double without = slice_and_measure(false);
    const double with    = slice_and_measure(true);
    INFO("travel over the void without: " << without << " mm, with: " << with << " mm");
    CHECK(without > 0);
    CHECK(with < without);
}

TEST_CASE("Avoid crossing perimeters: top surfaces", "[AvoidCrossing]") {
    // Both settings slice; avoiding the top surfaces changes the travels of a model with top surfaces under other parts.
    const std::string on  = Test::slice({ TestMesh::step }, config_with({ { "avoid_crossing_top", true } }));
    const std::string off = Test::slice({ TestMesh::step }, config_with({ { "avoid_crossing_top", false } }));
    CHECK(! on.empty());
    CHECK(on != off);
}
