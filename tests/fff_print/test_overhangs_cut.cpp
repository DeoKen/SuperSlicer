#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "libslic3r/libslic3r.h"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Format/OBJ.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <boost/filesystem.hpp>

#include "test_data.hpp"

using namespace Slic3r;

// Fork (doc section 43): overhangs cut (SuperSlicer overhangs_max_slope, overhangs_bridge_threshold). Model: a 30 x 10 x 30 mm wall with a 12 mm horizontal hole along Y at Z 15
// (tests/data/wall_round_hole.obj, made by Stuff/hole_trail_test.py); on the hole layers the wall is two islands, the
// gap between them is the hole width of the layer.
// Screw holes: tests/data/screw_holes.obj, the block of Stuff/overhang_test.py: for M2, M3, M4, M5, M6 and M8 a counterbore
// (4 mm deep, flat ceiling) in the front row and a bore with a dome narrowing into the hole in the back row, printed from
// below.

namespace {

TriangleMesh wall_with_hole()
{
    TriangleMesh mesh;
    const std::string path = (boost::filesystem::path(TEST_DATA_DIR) / "wall_round_hole.obj").string();
    REQUIRE(load_obj(path.c_str(), &mesh));
    return mesh;
}

struct HoleLayer { double z; double width; };

// Hole width per layer (gap between the two islands of the wall), from the bottom of the hole up to the layer where it
// closes (width 0), in mm.
std::vector<HoleLayer> hole_widths(std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> items)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({ { "layer_height", "0.2" }, { "first_layer_height", "0.2" }, { "nozzle_diameter", "0.4" } });
    config.set_deserialize_strict(items);
    Print print;
    Model model;
    Test::init_print({ wall_with_hole() }, print, model, config);
    print.process();
    std::vector<HoleLayer> out;
    bool in_hole = false;
    for (const Layer *layer : print.objects().front()->layers()) {
        if (layer->lslices.size() == 2) {
            BoundingBox a = get_extents(layer->lslices[0]), b = get_extents(layer->lslices[1]);
            if (a.min.x() > b.min.x())
                std::swap(a, b);
            out.push_back({ layer->print_z, unscale<double>(b.min.x() - a.max.x()) });
            in_hole = true;
        } else if (in_hole) {
            out.push_back({ layer->print_z, 0. });
            break;
        }
    }
    return out;
}

// Largest narrowing of the hole from one layer to the next one above, above the middle of the hole, the closing layer
// excluded (that one is the bridge).
double max_narrowing(const std::vector<HoleLayer> &layers)
{
    double max = 0;
    for (size_t i = 1; i + 1 < layers.size(); ++ i)
        if (layers[i].z > 15.)
            max = std::max(max, layers[i - 1].width - layers[i].width);
    return max;
}

// Screw sizes: clearance hole d, counterbore / bore D (mm); hole centers X in the model, counterbores at Y 30, domes at
// Y 50; the block is X 0..82.9, Y 20..60, 15 mm high.
struct ScrewHole { const char *name; double d; double D; double x; };
const ScrewHole screw_holes[] = { { "M2", 2.4, 4.4, 6.2 }, { "M3", 3.4, 6.5, 15.65 }, { "M4", 4.5, 8.0, 26.9 },
                                  { "M5", 5.5, 10.0, 39.9 }, { "M6", 6.6, 11.0, 54.4 }, { "M8", 9.0, 15.0, 71.4 } };

void collect_paths(const ExtrusionEntity *entity, std::vector<const ExtrusionPath*> &out)
{
    if (auto *collection = dynamic_cast<const ExtrusionEntityCollection*>(entity)) {
        for (const ExtrusionEntity *e : collection->entities)
            collect_paths(e, out);
    } else if (auto *loop = dynamic_cast<const ExtrusionLoop*>(entity)) {
        for (const ExtrusionPath &path : loop->paths)
            out.push_back(&path);
    } else if (auto *multipath = dynamic_cast<const ExtrusionMultiPath*>(entity)) {
        for (const ExtrusionPath &path : multipath->paths)
            out.push_back(&path);
    } else if (auto *path = dynamic_cast<const ExtrusionPath*>(entity))
        out.push_back(path);
}

struct ScrewHoleLayer {
    double z;
    double overhang_length;  // overhang perimeters within D / 2 + 1 mm of the center, mm
    double overhang_reach;   // farthest point of them from the center, mm
    int    holes;            // holes of the layer within D / 2 of the center
    double hole_width;       // larger of the X and Y sizes of the largest of them, mm
};

// The block sliced with the user's kind of settings (0.2 mm layers, 4 perimeters, 25 % slope, overhang threshold 0); per
// hole the layers from 3 mm up.
std::vector<std::vector<ScrewHoleLayer>> slice_screw_holes(bool domes)
{
    TriangleMesh mesh;
    const std::string path = (boost::filesystem::path(TEST_DATA_DIR) / "screw_holes.obj").string();
    REQUIRE(load_obj(path.c_str(), &mesh));
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({ { "layer_height", "0.2" }, { "first_layer_height", "0.2" }, { "nozzle_diameter", "0.4" },
                                    { "perimeters", "4" }, { "overhangs_max_slope", "25%" }, { "overhangs_threshold", "0%" },
                                    { "fill_density", "15%" } });
    Print print;
    Model model;
    Test::init_print({ std::move(mesh) }, print, model, config);
    print.process();
    const PrintObject &object = *print.objects().front();
    // Model coordinates to the layer's: the first layer is the whole block outline.
    const BoundingBox block = get_extents(object.layers().front()->lslices);
    std::vector<std::vector<ScrewHoleLayer>> out;
    for (const ScrewHole &hole : screw_holes) {
        const Point  center = block.min + Point(scaled<coord_t>(hole.x), scaled<coord_t>((domes ? 50. : 30.) - 20.));
        const double around = hole.D / 2.;
        std::vector<ScrewHoleLayer> layers;
        for (const Layer *layer : object.layers()) {
            if (layer->print_z < 3.)
                continue;
            ScrewHoleLayer l{ layer->print_z, 0., 0., 0, 0. };
            for (const LayerRegion *layerm : layer->regions()) {
                std::vector<const ExtrusionPath*> paths;
                collect_paths(&layerm->perimeters(), paths);
                for (const ExtrusionPath *p : paths)
                    if (p->role() == ExtrusionRole::OverhangPerimeter)
                        for (size_t i = 1; i < p->polyline.size(); ++ i) {
                            const double r = unscale<double>((p->polyline.points[i] - center).cast<double>().norm());
                            if (r < around + 1.) {
                                l.overhang_length += unscale<double>((p->polyline.points[i] - p->polyline.points[i - 1]).cast<double>().norm());
                                l.overhang_reach   = std::max(l.overhang_reach, r);
                            }
                        }
            }
            for (const ExPolygon &island : layer->lslices)
                for (const Polygon &h : island.holes)
                    if (unscale<double>((h.centroid() - center).cast<double>().norm()) < around) {
                        const BoundingBox bb = get_extents(h);
                        ++ l.holes;
                        l.hole_width = std::max(l.hole_width, unscale<double>(std::max(bb.size().x(), bb.size().y())));
                    }
            layers.push_back(l);
        }
        out.push_back(std::move(layers));
    }
    return out;
}

} // namespace

TEST_CASE("Overhangs cut: counterbores M2..M8, the ceiling as sliced, then the round hole", "[OverhangsCut]") {
    // The cut makes no bridges under a hole floating over a void (user, 2026-10-30; "Support floating holes" does, doc
    // section 44): the ceiling is one layer bridged as sliced (before, the cut printed bridges beside the hole, a band
    // across it and a square).
    const auto holes = slice_screw_holes(false);
    for (size_t i = 0; i < holes.size(); ++ i) {
        const ScrewHole &hole = screw_holes[i];
        INFO(hole.name);
        bool ceiling = false;
        for (const ScrewHoleLayer &l : holes[i]) {
            INFO("z " << l.z);
            // From the ceiling up, the model's hole on every layer.
            if (l.z > 4.1) {
                CHECK(l.holes == 1);
                CHECK(std::abs(l.hole_width - hole.d) < 0.1);
            }
            if (l.z < 4.1) {
                // The counterbore: no overhang.
                CHECK(l.overhang_length == 0.);
            } else if (l.z < 4.3) {
                // The ceiling: the hole's perimeters over the void are overhang.
                ceiling = true;
                CHECK(l.overhang_length > 0.);
            } else {
                CHECK(l.overhang_length == 0.);
            }
        }
        CHECK(ceiling);
    }
}

TEST_CASE("Overhangs cut: domes M2..M8 narrow along the slope, no overhang perimeters", "[OverhangsCut]") {
    const auto holes = slice_screw_holes(true);
    for (size_t i = 0; i < holes.size(); ++ i) {
        const ScrewHole &hole = screw_holes[i];
        INFO(hole.name);
        // The dome ends where the sphere of diameter D is as wide as the hole.
        const double top = 4. + std::sqrt(hole.D * hole.D - hole.d * hole.d) / 2.;
        double prev_width = 0.;
        for (const ScrewHoleLayer &l : holes[i]) {
            INFO("z " << l.z);
            // A ring left where the dome closes faster than the slope follows it was a full loop of overhang perimeters.
            CHECK(l.overhang_length == 0.);
            CHECK(l.holes == 1);
            // Narrowing by at most the slope per layer, 0.1 mm a side (a little tolerance for the smoothing).
            if (prev_width > 0.)
                CHECK(prev_width - l.hole_width < 0.2 + 0.05);
            prev_width = l.hole_width;
            if (l.z > top + 2.5)
                CHECK(std::abs(l.hole_width - hole.d) < 0.1);
        }
    }
}

TEST_CASE("Overhangs cut: a round hole narrows by at most the max slope per layer", "[OverhangsCut]") {
    const std::vector<HoleLayer> off = hole_widths({});
    const std::vector<HoleLayer> on  = hole_widths({ { "overhangs_max_slope", "50%" } });   // 0.2 mm of a 0.4 mm nozzle
    REQUIRE(off.size() > 10);
    REQUIRE(on.size() > 10);
    // Without the cut the top of the hole closes faster than 2 x 0.2 mm per layer.
    CHECK(max_narrowing(off) > 0.6);
    // With it, each side moves in by at most 0.2 mm per layer (a little tolerance for the smoothing).
    CHECK(max_narrowing(on) < 0.4 + 0.05);
    // Below the middle of the hole nothing changes.
    for (size_t i = 0; i < std::min(off.size(), on.size()) && off[i].z < 15.; ++ i)
        CHECK(std::abs(off[i].width - on[i].width) < 0.01);
    // The hole is closed by a flat bridge, much wider than the gap the model closes with.
    REQUIRE(on.size() >= 2);
    CHECK(on[on.size() - 2].width > 3.);
}

TEST_CASE("Overhangs cut: no bridges kept with a bridge max length of 0", "[OverhangsCut]") {
    const std::vector<HoleLayer> on = hole_widths({ { "overhangs_max_slope", "50%" }, { "overhangs_bridge_threshold", "0" } });
    REQUIRE(on.size() >= 2);
    // Without bridges the cut goes on to the top: the last open layer is narrow.
    CHECK(on[on.size() - 2].width < 0.4 + 0.05);
    CHECK(max_narrowing(on) < 0.4 + 0.05);
}

TEST_CASE("Overhangs cut: off by default, the slices are unchanged", "[OverhangsCut]") {
    auto defaults = DynamicPrintConfig::full_print_config();
    CHECK(defaults.option<ConfigOptionFloatOrPercent>("overhangs_max_slope")->value == 0.);
    CHECK(defaults.opt_float("overhangs_bridge_threshold") == -1.);
    const std::vector<HoleLayer> a = hole_widths({});
    const std::vector<HoleLayer> b = hole_widths({ { "overhangs_bridge_threshold", "5" } });
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++ i)
        CHECK(a[i].width == b[i].width);
}

namespace {

// tests/data/overhang_flat_cases.obj (Stuff/overhang_flat_test.py): every case of flat and sloped undersides side by
// side; boxes in model coordinates (mm), X0 Y0 X1 Y1. The model starts at 0, 0.
struct FlatCase { const char *name; double x0, y0, x1, y1; };
const FlatCase flat_cases[] = {
    { "chamfer", 36, 0, 52, 10 },  { "lip", 58, 0, 70, 10 },        { "tunnel", 76, 0, 92, 10 },
    { "slope5", 98, 0, 115, 10 },  { "slope_flat", 121, 0, 137, 10 }, { "flat_chamfer", 143, 0, 155, 10 },
    { "counterbore", 22, 16, 38, 32 }, { "mushroom", 44, 16, 60, 32 }, { "u_slope", 66, 16, 90, 38 },
    { "groove", 96, 16, 120, 38 },  { "mixed", 126, 16, 150, 36 },
    { "steep60", 0, 44, 13, 54 },   { "chamfer45", 19, 44, 34, 54 },  { "fillet_ceiling", 40, 44, 56, 54 },
    { "staircase", 62, 44, 74, 54 }, { "thin_lip", 80, 44, 88.25, 54 }, { "arch_tunnel", 94, 44, 110, 54 },
    { "pointed_arch", 116, 44, 132, 54 }, { "corner_ledge", 138, 44, 151, 57 }, { "cone_column", 0, 62, 16, 78 },
    { "blind_pocket", 19, 61, 37, 79 }, { "countersink", 40, 61, 58, 79 }, { "rotated_groove", 62, 62, 93.78, 93.05 },
    { "floating_island", 99.78, 62, 111.78, 68 } };
const FlatCase& flat_case(const std::string &name)
{
    for (const FlatCase &c : flat_cases)
        if (name == c.name)
            return c;
    throw std::runtime_error("no case " + name);
}

struct CaseLayer {
    double z;
    double diff;          // area of the cut layer's difference to the model's (as sliced), mm2
    double excess;        // area of the cut layer sticking out past the cut layer below by more than the slope, mm2
    double model_excess;  // the same for the model (an overhang of the model in this layer), mm2
};

// The model's layers with and without the cut (0.2 mm layers, 0.4 mm nozzle, the user's 35 %: 0.14 mm per layer); per case
// (or a part of it: x0 / x1 within the box), the layers.
struct FlatCasesSliced {
    std::vector<std::pair<double, ExPolygons>> model, cut;
    Point origin;
};

const FlatCasesSliced& flat_cases_sliced()
{
    static FlatCasesSliced out = []() {
        FlatCasesSliced r;
        TriangleMesh mesh;
        const std::string path = (boost::filesystem::path(TEST_DATA_DIR) / "overhang_flat_cases.obj").string();
        REQUIRE(load_obj(path.c_str(), &mesh));
        for (const char *slope : { "0", "35%" }) {
            DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
            config.set_deserialize_strict({ { "layer_height", "0.2" }, { "first_layer_height", "0.2" }, { "nozzle_diameter", "0.4" },
                                            { "perimeters", "2" }, { "fill_density", "15%" }, { "overhangs_max_slope", slope } });
            Print print;
            Model model;
            Test::init_print({ mesh }, print, model, config);
            print.process();
            const PrintObject &object = *print.objects().front();
            r.origin = get_extents(object.layers().front()->lslices).min;
            auto &layers = std::string(slope) == "0" ? r.model : r.cut;
            for (const Layer *layer : object.layers())
                layers.emplace_back(layer->print_z, layer->lslices);
        }
        return r;
    }();
    return out;
}

std::vector<CaseLayer> case_layers(const FlatCase &c, double x0 = 0, double x1 = 1e3)
{
    const FlatCasesSliced &s = flat_cases_sliced();
    REQUIRE(s.model.size() == s.cut.size());
    const Polygon box = BoundingBox(s.origin + Point(scaled<coord_t>(std::max(c.x0 - 1., c.x0 + x0)), scaled<coord_t>(c.y0 - 1.)),
                                    s.origin + Point(scaled<coord_t>(std::min(c.x1 + 1., c.x0 + x1)), scaled<coord_t>(c.y1 + 1.))).polygon();
    const float slope = float(scale_(0.14 + 0.03));
    const auto mm2 = [](const auto &polys) { return Slic3r::area(polys) / (scale_(1.) * scale_(1.)); };
    std::vector<CaseLayer> out;
    ExPolygons model_below, cut_below;
    for (size_t i = 0; i < s.model.size(); ++ i) {
        const ExPolygons model = intersection_ex(s.model[i].second, Polygons{ box });
        const ExPolygons cut   = intersection_ex(s.cut[i].second, Polygons{ box });
        CaseLayer l{ s.model[i].first, mm2(xor_ex(model, cut)), 0., 0. };
        if (i > 0) {
            l.excess       = mm2(diff_ex(cut, offset_ex(cut_below, slope)));
            l.model_excess = mm2(diff_ex(model, offset_ex(model_below, slope)));
        }
        out.push_back(l);
        model_below = model;
        cut_below   = cut;
    }
    return out;
}

// Flat undersides: every layer as sliced.
void check_as_sliced(const std::vector<CaseLayer> &layers, double from_z = 0)
{
    for (const CaseLayer &l : layers)
        if (l.z > from_z + 1e-3) {
            INFO("z " << l.z);
            CHECK(l.diff < 0.2);
        }
}

} // namespace

TEST_CASE("Overhangs cut: flat undersides are left as sliced", "[OverhangsCut]") {
    // A flat face of the model is not a slope: nothing to make steeper (user, 2026-11-15, the groove of SideSupport_A).
    for (const char *name : { "groove", "lip", "tunnel", "mushroom", "counterbore", "staircase", "blind_pocket", "rotated_groove",
                              "corner_ledge", "floating_island" }) {
        INFO(name);
        const auto layers = case_layers(flat_case(name));
        // The case has a flat overhang ...
        CHECK(std::any_of(layers.begin(), layers.end(), [](const CaseLayer &l) { return l.model_excess > 1.; }));
        // ... left as sliced.
        check_as_sliced(layers);
    }
}

TEST_CASE("Overhangs cut: regular overhangs (not holes) are left as sliced", "[OverhangsCut]") {
    // User, 2026-11-16: the cut is for horizontal holes, arch tunnels and domes only. Chamfers, ledges on a slope, a slot
    // whose walls lean in but stay open, a column flaring out, a countersink, a fillet, a slope and a ledge together.
    for (const char *name : { "chamfer", "slope5", "u_slope", "chamfer45", "cone_column", "countersink", "fillet_ceiling",
                              "slope_flat", "flat_chamfer", "mixed" }) {
        INFO(name);
        const auto layers = case_layers(flat_case(name));
        // The case has an overhang past the slope ...
        CHECK(std::any_of(layers.begin(), layers.end(), [](const CaseLayer &l) { return l.model_excess > 0.3; }));
        // ... left as sliced.
        check_as_sliced(layers);
    }
}

TEST_CASE("Overhangs cut: steep slopes, thin lips, arches", "[OverhangsCut]") {
    SECTION("a 60 degree slope (steeper than the max slope) and a 0.25 mm lip: (almost) nothing cut") {
        check_as_sliced(case_layers(flat_case("steep60")));
        // Not wider than two slope steps: left to the cut, which trims its first layer by about 0.11 mm along its 10 mm;
        // full from the next layer on.
        int trimmed = 0;
        for (const CaseLayer &l : case_layers(flat_case("thin_lip"))) {
            INFO("z " << l.z);
            CHECK(l.diff < 1.5);
            trimmed += l.diff > 0.2;
        }
        CHECK(trimmed <= 1);
    }
    SECTION("round and pointed arch tunnels: cut from bottom to top, closed by a bridge at Z 7.2") {
        // User's screenshot 2026-11-16: only the top of the arches was cut, their lower half still stepped 0.2 mm per layer.
        for (const char *name : { "arch_tunnel", "pointed_arch" }) {
            INFO(name);
            const auto layers = case_layers(flat_case(name));
            CHECK(std::any_of(layers.begin(), layers.end(), [](const CaseLayer &l) { return l.diff > 1.; }));
            for (const CaseLayer &l : layers)
                if (l.z < 7.1) {
                    INFO("z " << l.z);
                    CHECK(l.excess < 0.3);
                }
        }
    }
}

TEST_CASE("Overhangs cut: a chamfer under a box hanging off a wall is not a hole, as sliced", "[OverhangsCut]") {
    // User, 2026-11-16 (Raiser.stl): a box hanging off a wall, its underside a 45 degree chamfer, a pocket in it from the top
    // whose floor is 1 mm above the chamfer, 2 mm from the box's face. The chamfer was cut ("it's a regular overhang, it
    // should NOT have been cut at all"), and the strip the cut still owed touched the pocket, an "open hole", and was put
    // back in one layer as a bridge with overhang perimeters along the face. Now: no hole, every layer as sliced.
    // Model (mm): wall X 0..30, Y 0..3, Z 0..20; chamfer from Y 3 at Z 5 to Y 13 at Z 15, face Y 13 up to Z 20; pocket
    // X 8..22, Y 4..11 from Z 16 up. Convex pieces, overlapping.
    const auto piece = [](std::initializer_list<Vec3f> pts) { return its_convex_hull(std::vector<Vec3f>(pts)); };
    const auto box   = [&piece](float x0, float y0, float z0, float x1, float y1, float z1) {
        return piece({ { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 }, { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } });
    };
    indexed_triangle_set its = box(0, 0, 0, 30, 3, 20);
    its_merge(its, piece({ { 0, 2.5f, 4.5f }, { 30, 2.5f, 4.5f }, { 0, 13, 15 }, { 30, 13, 15 }, { 0, 13, 16 }, { 30, 13, 16 }, { 0, 2.5f, 16 }, { 30, 2.5f, 16 } }));
    its_merge(its, box(0, 2.5f, 16, 8, 13, 20));
    its_merge(its, box(22, 2.5f, 16, 30, 13, 20));
    its_merge(its, box(7.5f, 2.5f, 16, 22.5f, 4, 20));
    its_merge(its, box(7.5f, 11, 16, 22.5f, 13, 20));
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({ { "layer_height", "0.2" }, { "first_layer_height", "0.2" }, { "nozzle_diameter", "0.4" },
                                    { "perimeters", "2" }, { "fill_density", "15%" }, { "overhangs_max_slope", "35%" } });
    Print print;
    Model model;
    Test::init_print({ TriangleMesh(std::move(its)) }, print, model, config);
    print.process();
    const PrintObject &object = *print.objects().front();
    // Y of the face per layer, from the wall's back (Y 0, the first layer is the wall alone).
    const coord_t back = get_extents(object.layers().front()->lslices).min.y();
    bool pocket = false;
    for (const Layer *layer : object.layers()) {
        const double face = unscale<double>(get_extents(layer->lslices).max.y() - back);
        // The model's face at the middle of the layer: the wall (Y 3), the chamfer (Y = Z - 2), the box (Y 13).
        const double z     = layer->print_z - 0.1;
        const double model_face = std::clamp(z - 2., 3., 13.);
        INFO("z " << layer->print_z << " face " << face << " model " << model_face);
        CHECK(std::abs(face - model_face) < 0.05);
        for (const ExPolygon &island : layer->lslices)
            pocket |= ! island.holes.empty();
    }
    // The model is as described: the pocket is a hole of the layers.
    CHECK(pocket);
}

TEST_CASE("Overhangs cut: an object that slices to nothing", "[OverhangsCut]") {
    // The base plate the adaptive PA calibration builds its objects on (0.3 x 0.3 x 0.025 mm): no layer. slice_volumes() deleted
    // the empty layers and the cut read the first one of the emptied vector (crash, 2026-11-05).
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({ { "layer_height", "0.2" }, { "first_layer_height", "0.2" }, { "overhangs_max_slope", "50%" } });
    // PrusaSlicer's own error for it, not a crash.
    Print print;
    Model model;
    Test::init_print({ TriangleMesh(its_make_cube(0.3, 0.3, 0.025)) }, print, model, config);
    CHECK_THROWS_WITH(print.process(), Catch::Matchers::ContainsSubstring("No layers were detected"));
}
