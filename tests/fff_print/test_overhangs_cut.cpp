#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "libslic3r/libslic3r.h"
#include "libslic3r/BoundingBox.hpp"
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
