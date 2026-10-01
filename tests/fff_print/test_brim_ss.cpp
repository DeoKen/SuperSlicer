#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>
#include <boost/algorithm/string/predicate.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Brim options ported from SuperSlicer: brim_ears (+ max angle, detection length, pattern) and brim_per_object.

namespace {

DynamicPrintConfig config_with(std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> items)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config_with({
        { "skirts",       0 },
        { "fill_density", "20%" },
        { "brim_type",    "outer_only" },
        { "brim_width",   5 },
    });
    config.set_deserialize_strict(items);
    return config;
}

// One brim extrusion move, in G-code order.
struct BrimMove { Vec2d from; Vec2d to; size_t line; };

struct Parsed {
    std::vector<BrimMove>               moves;
    // Continuous brim extrusions (no travel in between), as indices into moves.
    std::vector<std::vector<size_t>>    runs;
    // Index of the first extrusion above the first layer, in G-code lines.
    size_t                              first_upper_line = size_t(-1);
};

Parsed parse(const std::string &gcode, const double first_layer_height)
{
    Parsed out;
    std::string type;
    bool        in_run = false;
    size_t      line_idx = 0;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        ++ line_idx;
        const std::string &raw = line.raw();
        if (boost::starts_with(raw, ";TYPE:")) {
            type   = raw.substr(6);
            in_run = false;
        } else if (line.cmd_is("G1") && line.dist_XY(self) > 0) {
            if (line.dist_E(self) > 0) {
                if (type == "Skirt/Brim") {
                    if (! in_run)
                        out.runs.emplace_back();
                    in_run = true;
                    out.runs.back().push_back(out.moves.size());
                    out.moves.push_back({ Vec2d(self.x(), self.y()), Vec2d(line.new_X(self), line.new_Y(self)), line_idx });
                } else if (self.z() > first_layer_height + EPSILON && out.first_upper_line == size_t(-1))
                    out.first_upper_line = line_idx;
            } else
                in_run = false;
        }
    });
    return out;
}

double brim_length(const Parsed &parsed)
{
    double l = 0;
    for (const BrimMove &m : parsed.moves)
        l += (m.to - m.from).norm();
    return l;
}

// Bounding boxes of the first layer of every object instance, in bed coordinates.
std::vector<BoundingBoxf> object_boxes(const Print &print)
{
    std::vector<BoundingBoxf> out;
    for (const PrintObject *object : print.objects())
        for (const PrintInstance &instance : object->instances()) {
            BoundingBox bb = get_extents(object->layers().front()->lslices);
            bb.translate(instance.shift.x(), instance.shift.y());
            out.emplace_back(unscaled(bb.min), unscaled(bb.max));
        }
    return out;
}

double distance_to_box(const Vec2d &p, const BoundingBoxf &bb)
{
    const double dx = std::max({ bb.min.x() - p.x(), 0., p.x() - bb.max.x() });
    const double dy = std::max({ bb.min.y() - p.y(), 0., p.y() - bb.max.y() });
    return std::sqrt(dx * dx + dy * dy);
}

double distance_to_corner(const Vec2d &p, const BoundingBoxf &bb)
{
    double d = std::numeric_limits<double>::max();
    for (const Vec2d &c : { bb.min, bb.max, Vec2d(bb.min.x(), bb.max.y()), Vec2d(bb.max.x(), bb.min.y()) })
        d = std::min(d, (p - c).norm());
    return d;
}

struct Sliced {
    std::string               gcode;
    std::vector<BoundingBoxf> boxes;
    Parsed                    parsed;
};

Sliced slice_brim(std::vector<TriangleMesh> meshes, const DynamicPrintConfig &config)
{
    Print print;
    Model model;
    Test::init_print(std::move(meshes), print, model, config);
    Sliced out;
    out.gcode  = Test::gcode(print);
    out.boxes  = object_boxes(print);
    out.parsed = parse(out.gcode, print.objects().front()->layers().front()->print_z);
    return out;
}

TriangleMesh cube() { return Test::mesh(TestMesh::cube_20x20x20); }
// Low enough to be printed sequentially.
TriangleMesh low_cube() { return Test::mesh(TestMesh::cube_20x20x20, Vec3d::Zero(), Vec3d(1., 1., 0.25)); }

} // namespace

TEST_CASE("Brim ears: only at the sharp corners", "[BrimSS]") {
    const Sliced full = slice_brim({ cube() }, config_with({}));
    for (const char *pattern : { "concentric", "rectilinear" }) {
        SECTION(pattern) {
            const Sliced ears = slice_brim({ cube() }, config_with({ { "brim_ears", true }, { "brim_ears_pattern", pattern } }));
            REQUIRE(! ears.parsed.moves.empty());
            const BoundingBoxf &box = ears.boxes.front();
            // Every brim line is in a disc of brim_width around a corner, every corner has an ear.
            std::set<int> corners;
            for (const BrimMove &m : ears.parsed.moves)
                for (const Vec2d &p : { m.from, m.to }) {
                    INFO("brim point " << p.x() << ", " << p.y());
                    CHECK(distance_to_corner(p, box) < 5.5);
                    corners.insert(int(p.x() > box.center().x()) + 2 * int(p.y() > box.center().y()));
                }
            CHECK(corners.size() == 4);
            CHECK(brim_length(ears.parsed) < 0.5 * brim_length(full.parsed));
        }
    }
}

TEST_CASE("Brim ears: max angle", "[BrimSS]") {
    // A cube has 90° corners: no ear below 90°, ears above.
    CHECK(slice_brim({ cube() }, config_with({ { "brim_ears", true }, { "brim_ears_max_angle", 0 } })).parsed.moves.empty());
    CHECK(slice_brim({ cube() }, config_with({ { "brim_ears", true }, { "brim_ears_max_angle", 80 } })).parsed.moves.empty());
    CHECK(! slice_brim({ cube() }, config_with({ { "brim_ears", true }, { "brim_ears_max_angle", 100 } })).parsed.moves.empty());
}

TEST_CASE("Brim per object: brims not merged", "[BrimSS]") {
    // Two objects closer than twice the brim width: the plate brim merges their brims.
    auto runs_touching_both = [](const Sliced &s) {
        REQUIRE(s.boxes.size() == 2);
        size_t n = 0;
        // A run belongs to one object if all its points are within the brim width of it; a merged brim loop goes
        // around both objects, so it has points farther than the brim width from each of them.
        for (const std::vector<size_t> &run : s.parsed.runs) {
            double max0 = 0, max1 = 0;
            for (size_t idx : run)
                for (const Vec2d &p : { s.parsed.moves[idx].from, s.parsed.moves[idx].to }) {
                    max0 = std::max(max0, distance_to_box(p, s.boxes[0]));
                    max1 = std::max(max1, distance_to_box(p, s.boxes[1]));
                }
            if (max0 > 8.5 && max1 > 8.5)
                ++ n;
        }
        return n;
    };
    const Sliced shared     = slice_brim({ cube(), cube() }, config_with({ { "brim_width", 8 } }));
    const Sliced per_object = slice_brim({ cube(), cube() }, config_with({ { "brim_width", 8 }, { "brim_per_object", true } }));
    INFO("gap between the objects: " << std::max(shared.boxes[1].min.x() - shared.boxes[0].max.x(), shared.boxes[1].min.y() - shared.boxes[0].max.y())
         << ", " << std::max(shared.boxes[0].min.x() - shared.boxes[1].max.x(), shared.boxes[0].min.y() - shared.boxes[1].max.y()));
    CHECK(runs_touching_both(shared) > 0);
    CHECK(runs_touching_both(per_object) == 0);
    // Both objects have a brim, every brim line is within the brim width of its object.
    std::set<int> objects;
    for (const BrimMove &m : per_object.parsed.moves) {
        const double d0 = distance_to_box(m.to, per_object.boxes[0]), d1 = distance_to_box(m.to, per_object.boxes[1]);
        objects.insert(d0 < d1 ? 0 : 1);
        CHECK(std::min(d0, d1) < 8.5);
    }
    CHECK(objects.size() == 2);
}

TEST_CASE("Brim per object: printed with its object when printing sequentially", "[BrimSS]") {
    const Sliced shared     = slice_brim({ low_cube(), low_cube() }, config_with({ { "complete_objects", true } }));
    const Sliced per_object = slice_brim({ low_cube(), low_cube() }, config_with({ { "complete_objects", true }, { "brim_per_object", true } }));
    REQUIRE(! shared.parsed.moves.empty());
    REQUIRE(! per_object.parsed.moves.empty());
    // Plate brim: all of it before the first object goes up. Brim per object: the second brim after it.
    CHECK(shared.parsed.moves.back().line < shared.parsed.first_upper_line);
    CHECK(per_object.parsed.moves.front().line < per_object.parsed.first_upper_line);
    CHECK(per_object.parsed.moves.back().line > per_object.parsed.first_upper_line);
}

TEST_CASE("Brim per object with ears", "[BrimSS]") {
    const Sliced s = slice_brim({ cube(), cube() }, config_with({ { "brim_per_object", true }, { "brim_ears", true } }));
    REQUIRE(! s.parsed.moves.empty());
    for (const BrimMove &m : s.parsed.moves)
        CHECK(std::min(distance_to_corner(m.to, s.boxes[0]), distance_to_corner(m.to, s.boxes[1])) < 5.5);
}
