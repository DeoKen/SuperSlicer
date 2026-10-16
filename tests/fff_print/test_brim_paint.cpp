#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
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
void paint(Model &model, const Vec3f &normal, TriangleStateType state)
{
    ModelVolume                &volume = *model.objects.front()->volumes.front();
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
