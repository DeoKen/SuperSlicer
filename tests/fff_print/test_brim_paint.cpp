#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <vector>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Paint-on brim: brim enforcers / blockers painted on the model (like the support painting).

namespace {

DynamicPrintConfig brim_config(const char *brim_type)
{
    return DynamicPrintConfig::full_print_config_with({
        { "skirts",       0 },
        { "fill_density", "20%" },
        { "brim_type",    brim_type },
        { "brim_width",   5 },
    });
}

// Paints the facets of the first volume whose normal is close to `normal` (all of them for a zero normal).
void paint(Model &model, const Vec3f &normal, TriangleStateType state, size_t object_idx = 0)
{
    ModelVolume                &volume = *model.objects[object_idx]->volumes.front();
    const indexed_triangle_set &its    = volume.mesh().its;
    TriangleSelector            selector(volume.mesh());
    selector.deserialize(volume.brim_facets.get_data(), false);
    for (size_t i = 0; i < its.indices.size(); ++ i)
        if (normal == Vec3f::Zero() || its_face_normal(its, its.indices[i]).dot(normal) > 0.99f)
            selector.set_facet(int(i), state);
    volume.brim_facets.set(selector);
}

// The brim extrusions of the first layer.
std::vector<Linef> brim_moves(const std::string &gcode)
{
    std::vector<Linef> out;
    std::string        type;
    GCodeReader        parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (boost::starts_with(line.raw(), ";TYPE:"))
            type = line.raw().substr(6);
        else if (type == "Skirt/Brim" && line.cmd_is("G1") && line.dist_XY(self) > 0 && line.dist_E(self) > 0)
            out.emplace_back(Vec2d(self.x(), self.y()), Vec2d(line.new_X(self), line.new_Y(self)));
    });
    return out;
}

BoundingBoxf first_layer_box(const Print &print)
{
    const PrintObject *object = print.objects().front();
    BoundingBox        bb     = get_extents(object->layers().front()->lslices);
    bb.translate(object->instances().front().shift);
    return { unscaled(bb.min), unscaled(bb.max) };
}

struct Result {
    std::vector<Linef> moves;
    BoundingBoxf       box;
};

Result slice(const DynamicPrintConfig &config, const std::function<void(Model&)> &painter)
{
    Print print;
    Model model;
    Test::init_print({ TestMesh::cube_20x20x20 }, print, model, config);
    painter(model);
    print.apply(model, config);
    Result out;
    out.moves = brim_moves(Test::gcode(print));
    out.box   = first_layer_box(print);
    return out;
}

// Brim on each side of the box: number of move end points outside the box on that side.
struct Sides { int front = 0, back = 0, left = 0, right = 0; };
Sides sides(const Result &r)
{
    Sides s;
    for (const Linef &l : r.moves)
        for (const Vec2d &p : { l.a, l.b }) {
            if (p.y() < r.box.min.y() - EPSILON) ++ s.front;
            if (p.y() > r.box.max.y() + EPSILON) ++ s.back;
            if (p.x() < r.box.min.x() - EPSILON) ++ s.left;
            if (p.x() > r.box.max.x() + EPSILON) ++ s.right;
        }
    return s;
}

const Vec3f Front  = -Vec3f::UnitY();
const Vec3f Bottom = -Vec3f::UnitZ();
const Vec3f Top    =  Vec3f::UnitZ();

} // namespace

TEST_CASE("Paint-on brim: nothing painted, the same brim as stock", "[BrimPaint]") {
    CHECK(slice(brim_config("no_brim"), [](Model&) {}).moves.empty());
    CHECK(! slice(brim_config("outer_only"), [](Model&) {}).moves.empty());
}

TEST_CASE("Paint-on brim: enforcer on one side wall adds a brim along that side only", "[BrimPaint]") {
    const Result r = slice(brim_config("no_brim"), [](Model &m) { paint(m, Front, TriangleStateType::ENFORCER); });
    REQUIRE(! r.moves.empty());
    for (const Linef &l : r.moves)
        for (const Vec2d &p : { l.a, l.b }) {
            // In front of the painted wall, not longer than the wall (butt ended), not wider than the brim.
            CHECK(p.y() < r.box.min.y() + 0.1);
            CHECK(p.y() > r.box.min.y() - 5.1);
            CHECK(p.x() > r.box.min.x() - 0.5);
            CHECK(p.x() < r.box.max.x() + 0.5);
        }
}

TEST_CASE("Paint-on brim: the lines do not bend around a part without a brim there", "[BrimPaint]") {
    // Cube A painted all around, cube B 6 mm to its right painted only on its far (right) wall, brim 5 mm: the brim lines
    // of A are the same as with A alone (they used to bend around B in the gap, leaving pieces of lines).
    auto brim_of_a = [](bool with_b) {
        Print print;
        Model model;
        const DynamicPrintConfig config = brim_config("no_brim");
        if (with_b)
            Test::init_print({ TestMesh::cube_20x20x20, TestMesh::cube_20x20x20 }, print, model, config);
        else
            Test::init_print({ TestMesh::cube_20x20x20 }, print, model, config);
        for (size_t i = 0; i < model.objects.size(); ++ i) {
            ModelInstance &instance = *model.objects[i]->instances.front();
            instance.set_offset(Vec3d(100. + 26. * double(i), 100., instance.get_offset().z()));
        }
        paint(model, Bottom, TriangleStateType::ENFORCER, 0);
        if (with_b)
            paint(model, Vec3f::UnitX(), TriangleStateType::ENFORCER, 1);
        // A's brim: left of the middle of the gap.
        const double middle = model.objects.front()->instance_bounding_box(0).max.x() + 3.;
        print.apply(model, config);
        std::vector<std::array<long, 4>> out;
        for (const Linef &l : brim_moves(Test::gcode(print)))
            if (l.a.x() < middle && l.b.x() < middle) {
                std::array<long, 4> s{ std::lround(l.a.x() * 100.), std::lround(l.a.y() * 100.), std::lround(l.b.x() * 100.), std::lround(l.b.y() * 100.) };
                if (std::make_pair(s[0], s[1]) > std::make_pair(s[2], s[3]))
                    s = { s[2], s[3], s[0], s[1] };
                out.push_back(s);
            }
        std::sort(out.begin(), out.end());
        return out;
    };
    const auto alone = brim_of_a(false);
    REQUIRE(! alone.empty());
    CHECK(brim_of_a(true) == alone);
}

TEST_CASE("Paint-on brim: enforcer on the bottom face adds the brim all around", "[BrimPaint]") {
    const Sides s = sides(slice(brim_config("no_brim"), [](Model &m) { paint(m, Bottom, TriangleStateType::ENFORCER); }));
    CHECK(s.front > 0);
    CHECK(s.back > 0);
    CHECK(s.left > 0);
    CHECK(s.right > 0);
}

TEST_CASE("Paint-on brim: painting away from the bed does nothing", "[BrimPaint]") {
    CHECK(slice(brim_config("no_brim"), [](Model &m) { paint(m, Top, TriangleStateType::ENFORCER); }).moves.empty());
}

TEST_CASE("Paint-on brim: blocker removes the brim along that side", "[BrimPaint]") {
    const Result r = slice(brim_config("outer_only"), [](Model &m) { paint(m, Front, TriangleStateType::BLOCKER); });
    const Sides  s = sides(r);
    CHECK(s.back > 0);
    CHECK(s.left > 0);
    CHECK(s.right > 0);
    // In front of the blocked wall only the ends of the side brims may remain (within the brim width of the corners).
    for (const Linef &l : r.moves)
        for (const Vec2d &p : { l.a, l.b })
            if (p.y() < r.box.min.y() - EPSILON)
                CHECK((p.x() < r.box.min.x() + 0.5 || p.x() > r.box.max.x() - 0.5));
}

TEST_CASE("Paint-on brim: blocker keeps the brim of the walls facing it", "[BrimPaint]") {
    // One object of two 10 mm cubes 4 mm apart, brim 5 mm: blocking the wall of the first cube facing the second removes
    // only the half of the gap's brim nearest to it; the brim of the second cube's wall stays.
    indexed_triangle_set its  = its_make_cube(10., 10., 10.);
    indexed_triangle_set its2 = its_make_cube(10., 10., 10.);
    for (Vec3f &v : its2.vertices)
        v.x() += 14.f;
    its_merge(its, std::move(its2));
    Print print;
    Model model;
    const DynamicPrintConfig config = brim_config("outer_only");
    Test::init_print({ TriangleMesh(std::move(its)) }, print, model, config);
    {
        ModelVolume                &volume = *model.objects.front()->volumes.front();
        const indexed_triangle_set &mesh   = volume.mesh().its;
        const BoundingBoxf3         bbox   = bounding_box(mesh);
        const float                 middle = float(0.5 * (bbox.min.x() + bbox.max.x()));
        TriangleSelector            selector(volume.mesh());
        for (size_t i = 0; i < mesh.indices.size(); ++ i)
            if (its_face_normal(mesh, mesh.indices[i]).x() > 0.99f && mesh.vertices[mesh.indices[i][0]].x() < middle)
                selector.set_facet(int(i), TriangleStateType::BLOCKER);
        volume.brim_facets.set(selector);
    }
    print.apply(model, config);
    const std::vector<Linef> moves = brim_moves(Test::gcode(print));
    const BoundingBoxf       box   = first_layer_box(print);
    // Brim lines in the gap (x from box.min.x + 10 to + 14), away from its ends.
    int first_half = 0, second_half = 0;
    for (const Linef &l : moves) {
        const Vec2d p = 0.5 * (l.a + l.b);
        if (p.y() < box.min.y() + 1. || p.y() > box.max.y() - 1.)
            continue;
        if (p.x() > box.min.x() + 10.2 && p.x() < box.min.x() + 11.8)
            ++ first_half;
        if (p.x() > box.min.x() + 12.2 && p.x() < box.min.x() + 13.8)
            ++ second_half;
    }
    CHECK(first_half == 0);
    CHECK(second_half > 0);
}

namespace {

// A 10 mm cube with a plate on top reaching 10 mm past it: the support under the plate stands on the bed next to the cube,
// within the brim width (5 mm).
struct SupportMoves { std::vector<Linef> support_first, support_second, brim; };
SupportMoves slice_support_next_to_cube(const char *brim_type, bool painted, std::initializer_list<ConfigBase::SetDeserializeItem> options = {})
{
    indexed_triangle_set its   = its_make_cube(10., 10., 10.);
    indexed_triangle_set plate = its_make_cube(20., 10., 2.);
    for (Vec3f &v : plate.vertices)
        v.z() += 10.f;
    its_merge(its, std::move(plate));
    DynamicPrintConfig config = brim_config(brim_type);
    config.set_deserialize_strict({ { "support_material", 1 }, { "support_material_auto", 1 }, { "first_layer_height", 0.2 }, { "layer_height", 0.2 } });
    config.set_deserialize_strict(options);
    Print print;
    Model model;
    Test::init_print({ TriangleMesh(std::move(its)) }, print, model, config);
    if (painted) {
        paint(model, Bottom, TriangleStateType::ENFORCER);
        print.apply(model, config);
    }
    SupportMoves                                         out;
    std::string                                          type;
    std::vector<std::pair<double, Linef>>                support_above;
    GCodeReader                                          parser;
    const std::string gcode = Test::gcode(print);
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (boost::starts_with(line.raw(), ";TYPE:"))
            type = line.raw().substr(6);
        else if (line.cmd_is("G1") && line.dist_XY(self) > 0 && line.dist_E(self) > 0) {
            const Linef l(Vec2d(self.x(), self.y()), Vec2d(line.new_X(self), line.new_Y(self)));
            const bool  support = type == "Support material" || type == "Support material interface";
            if (support && std::abs(self.z() - 0.2) < EPSILON)
                out.support_first.emplace_back(l);
            else if (support)
                support_above.emplace_back(self.z(), l);
            else if (type == "Skirt/Brim")
                out.brim.emplace_back(l);
        }
    });
    // The support's lowest layer above the first one.
    double second_z = std::numeric_limits<double>::max();
    for (const auto &[z, l] : support_above)
        second_z = std::min(second_z, z);
    for (const auto &[z, l] : support_above)
        if (std::abs(z - second_z) < EPSILON)
            out.support_second.emplace_back(l);
    return out;
}

} // namespace

namespace {

// The largest distance from a line of the support's second layer to the brim lines.
double support_distance_to_brim(const SupportMoves &m)
{
    double max_dist = 0.;
    for (const Linef &s : m.support_second)
        for (const Vec2d &p : { s.a, s.b, Vec2d(0.5 * (s.a + s.b)) }) {
            double dist = std::numeric_limits<double>::max();
            for (const Linef &b : m.brim)
                dist = std::min(dist, line_alg::distance_to(b, p));
            max_dist = std::max(max_dist, dist);
        }
    return max_dist;
}

} // namespace

TEST_CASE("Own brim under the support", "[BrimPaint]") {
    // support_material_own_brim: with a brim (painted all around, or the outer brim type) the support touching it has no
    // first layer, its first layer is brim; without a brim it keeps its first layer. Off: stock, the support keeps its
    // first layer where it is not over the brim (5 mm brim, 10 mm overhang).
    const SupportMoves no_brim = slice_support_next_to_cube("no_brim", false, { { "support_material_own_brim", 1 } });
    REQUIRE(! no_brim.support_first.empty());
    CHECK(no_brim.brim.empty());
    for (const auto &[brim_type, painted] : { std::make_pair("no_brim", true), std::make_pair("outer_only", false) }) {
        INFO("brim type " << brim_type << (painted ? ", painted" : ""));
        const SupportMoves m = slice_support_next_to_cube(brim_type, painted, { { "support_material_own_brim", 1 } });
        CHECK(m.support_first.empty());
        REQUIRE(! m.support_second.empty());
        REQUIRE(! m.brim.empty());
        // Every line of the support's second layer lies on the brim: within a line of a brim line.
        CHECK(support_distance_to_brim(m) < 0.6);
    }
    CHECK(! slice_support_next_to_cube("outer_only", false).support_first.empty());
}

TEST_CASE("Paint-on brim: blocker in a hole removes the inner brim there", "[BrimPaint]") {
    Print print;
    Model model;
    DynamicPrintConfig config = brim_config("inner_only");
    Test::init_print({ TestMesh::cube_with_hole }, print, model, config);
    const size_t all = brim_moves(Test::gcode(print)).size();
    REQUIRE(all > 0);
    paint(model, Vec3f::Zero(), TriangleStateType::BLOCKER);
    print.apply(model, config);
    const std::vector<Linef> left = brim_moves(Test::gcode(print));
    const BoundingBoxf box = first_layer_box(print);
    INFO("box " << box.min.x() << "," << box.min.y() << " - " << box.max.x() << "," << box.max.y() << " moves " << left.size()
         << " first " << (left.empty() ? 0. : left.front().a.x()) << "," << (left.empty() ? 0. : left.front().a.y()));
    CHECK(left.empty());
}

TEST_CASE("Paint-on brim: repainting only makes the brim again", "[BrimPaint]") {
    Print print;
    Model model;
    const DynamicPrintConfig config = brim_config("no_brim");
    Test::init_print({ TestMesh::cube_20x20x20 }, print, model, config);
    Test::gcode(print);
    paint(model, Front, TriangleStateType::ENFORCER);
    print.apply(model, config);
    CHECK(print.objects().front()->is_step_done(posInfill));
    CHECK(! print.is_step_done(psSkirtBrim));
    CHECK(! brim_moves(Test::gcode(print)).empty());
}

TEST_CASE("Paint-on brim: saved in the 3mf project", "[BrimPaint]") {
    Print print;
    Model model;
    const DynamicPrintConfig config = brim_config("no_brim");
    Test::init_print({ TestMesh::cube_20x20x20 }, print, model, config);
    paint(model, Front, TriangleStateType::ENFORCER);
    paint(model, Bottom, TriangleStateType::BLOCKER);
    const auto path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("brim-paint-%%%%-%%%%.3mf");
    REQUIRE(store_3mf(path.string().c_str(), &model, &config, false));
    DynamicPrintConfig        loaded_config;
    ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Disable };
    Model                     loaded;
    boost::optional<Semver>   version;
    REQUIRE(load_3mf(path.string().c_str(), loaded_config, ctxt, &loaded, false, version));
    boost::filesystem::remove(path);
    REQUIRE(loaded.objects.size() == 1);
    const ModelVolume &volume = *loaded.objects.front()->volumes.front();
    CHECK(volume.brim_facets.has_facets(volume, TriangleStateType::ENFORCER));
    CHECK(volume.brim_facets.has_facets(volume, TriangleStateType::BLOCKER));
    CHECK(volume.brim_facets.get_data() == model.objects.front()->volumes.front()->brim_facets.get_data());
}
