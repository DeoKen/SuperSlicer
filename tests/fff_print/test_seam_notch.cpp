#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <boost/algorithm/string/predicate.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Seam notch ported from SuperSlicer: seam_notch_all, seam_notch_inner, seam_notch_outer, seam_notch_angle.

namespace {

DynamicPrintConfig config_with(std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> items)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config_with({
        { "skirts",       0 },
        { "fill_density", "20%" },
        { "layer_height", 0.2 },
        { "perimeters",   2 },
    });
    config.set_deserialize_strict(items);
    return config;
}

// One external perimeter move.
struct Move { Vec2d from; Vec2d to; double e_per_mm; };

// External perimeter moves of each layer.
std::vector<std::vector<Move>> external_perimeters(const std::string &gcode)
{
    std::vector<std::vector<Move>> layers;
    std::string type;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        const std::string &raw = line.raw();
        if (boost::starts_with(raw, ";LAYER_CHANGE"))
            layers.emplace_back();
        else if (boost::starts_with(raw, ";TYPE:"))
            type = raw.substr(6);
        else if (line.cmd_is("G1") && ! layers.empty() && type == "External perimeter" && line.dist_E(self) > 0 && line.dist_XY(self) > 0)
            layers.back().push_back({ Vec2d(self.x(), self.y()), Vec2d(line.new_X(self), line.new_Y(self)), line.dist_E(self) / line.dist_XY(self) });
    });
    return layers;
}

// Moves with a reduced flow (below 95 % of the most common flow of the layer).
size_t reduced_moves(const std::vector<Move> &moves)
{
    if (moves.empty())
        return 0;
    std::vector<double> flows;
    for (const Move &m : moves)
        flows.push_back(m.e_per_mm);
    std::sort(flows.begin(), flows.end());
    const double median = flows[flows.size() / 2];
    return std::count_if(moves.begin(), moves.end(), [median](const Move &m) { return m.e_per_mm < 0.95 * median; });
}

// Number of layers with reduced flow external perimeter moves.
size_t notched_layers(const std::string &gcode)
{
    size_t n = 0;
    for (const std::vector<Move> &layer : external_perimeters(gcode))
        if (reduced_moves(layer) >= 2)
            ++ n;
    return n;
}

TriangleMesh cylinder() { return make_cylinder(10., 3.); }

// G-code without the header line holding the time stamp and without the config block (it lists the settings).
std::string strip(const std::string &gcode)
{
    const size_t begin = gcode.find('\n') + 1;
    return gcode.substr(begin, gcode.find("; prusaslicer_config = begin") - begin);
}
std::string slice_gcode(std::initializer_list<TriangleMesh> meshes, const DynamicPrintConfig &config)
{
    std::string gcode = Test::slice(meshes, config);
    return strip(gcode);
}
std::string slice_gcode(std::initializer_list<TestMesh> meshes, const DynamicPrintConfig &config)
{
    std::string gcode = Test::slice(meshes, config);
    return strip(gcode);
}

} // namespace

TEST_CASE("Seam notch: off by default", "[SeamNotch]") {
    const std::string stock = slice_gcode({ cylinder() }, config_with({}));
    CHECK(notched_layers(stock) == 0);
    // Inner (round holes) does not apply to the outer perimeter of a cylinder.
    CHECK((slice_gcode({ cylinder() }, config_with({ { "seam_notch_inner", "50%" } })) == stock));
}

TEST_CASE("Seam notch: round perimeter", "[SeamNotch]") {
    const std::string stock = slice_gcode({ cylinder() }, config_with({}));
    const size_t      layers = external_perimeters(stock).size();
    REQUIRE(layers > 10);
    for (const char *key : { "seam_notch_outer", "seam_notch_all" }) {
        SECTION(key) {
            const std::string gcode = slice_gcode({ cylinder() }, config_with({ { key, "50%" } }));
            CHECK((gcode != stock));
            // Every layer is notched.
            CHECK(notched_layers(gcode) == layers);
            // The loop starts and ends inside the cylinder, about half a perimeter width deep (wider on the first layer).
            for (const std::vector<Move> &layer : external_perimeters(gcode)) {
                // Center of the bounding box of the loop.
                Vec2d lo = layer.front().to, hi = lo;
                for (const Move &m : layer) {
                    lo = lo.cwiseMin(m.to);
                    hi = hi.cwiseMax(m.to);
                }
                const Vec2d center = (lo + hi) / 2.;
                double radius = 0;
                for (const Move &m : layer)
                    radius = std::max(radius, (m.to - center).norm());
                const double depth_start = radius - (layer.front().from - center).norm();
                const double depth_end   = radius - (layer.back().to - center).norm();
                INFO("depth at start " << depth_start << ", at end " << depth_end);
                CHECK(depth_start > 0.1);
                CHECK(depth_start < 0.5);
                CHECK(depth_end > 0.1);
                CHECK(depth_end < 0.5);
            }
        }
    }
}

TEST_CASE("Seam notch: maximum angle", "[SeamNotch]") {
    const std::string stock = slice_gcode({ cylinder() }, config_with({}));
    // 180 filters everything, even the (almost straight) seam of a round perimeter.
    CHECK((slice_gcode({ cylinder() }, config_with({ { "seam_notch_outer", "50%" }, { "seam_notch_angle", 180 } })) == stock));
}

TEST_CASE("Seam notch: square corners are not notched", "[SeamNotch]") {
    // The seam of a cube is in a corner: the start and end directions are too different (as in SuperSlicer).
    const std::string stock = slice_gcode({ TestMesh::cube_20x20x20 }, config_with({}));
    CHECK((slice_gcode({ TestMesh::cube_20x20x20 }, config_with({ { "seam_notch_all", "50%" }, { "seam_notch_angle", 360 } })) == stock));
}

TEST_CASE("Seam notch: round hole", "[SeamNotch]") {
    const std::string stock = slice_gcode({ TestMesh::cube_with_hole }, config_with({}));
    const std::string gcode = slice_gcode({ TestMesh::cube_with_hole }, config_with({ { "seam_notch_inner", "50%" } }));
    CHECK(notched_layers(stock) == 0);
    CHECK(notched_layers(gcode) > 0);
}

TEST_CASE("Seam notch: not combined with the scarf seam", "[SeamNotch]") {
    auto validate = [](std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> items) {
        Print print;
        Model model;
        Test::init_print({ TestMesh::cube_20x20x20 }, print, model, config_with(items));
        return print.validate();
    };
    CHECK(validate({ { "seam_notch_all", "50%" } }).empty());
    CHECK(validate({ { "scarf_seam_placement", "everywhere" } }).empty());
    CHECK(! validate({ { "seam_notch_all", "50%" }, { "scarf_seam_placement", "everywhere" } }).empty());
}
