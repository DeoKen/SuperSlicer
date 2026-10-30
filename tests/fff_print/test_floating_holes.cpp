#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "libslic3r/libslic3r.h"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Format/OBJ.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/SVG.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <boost/filesystem.hpp>
#include <cstdlib>

#include "test_data.hpp"

using namespace Slic3r;

// Fork (doc section 44): floating holes, bridge layers (floating_hole_bridges). Model: tests/data/screw_holes.obj, the
// block of Stuff/overhang_test.py: counterbores M2..M8 (Y 30, 4 mm deep, flat ceiling, the hole through to the top) and a
// row of other holes floating over a 4 mm deep cavity (Y 70): 1.6 mm round, 3 x 7 mm slot, 5 mm square turned 20 degrees,
// 4 mm round off center in a 16 x 8 mm cavity, 6 mm hexagon in a 12 mm hexagon cavity.

namespace {

struct FloatingCase {
    const char *name;
    double      x, y;         // hole center in the model, mm
    double      void_radius;  // reach of the cavity from the hole center, mm
    double      d;            // round holes: diameter (0 = not round)
    int         angle0;       // expected direction of the first bridge layer in degrees, -1 = not checked
    size_t      layers;       // expected number of bridge layers of a hole that isn't round, 0 = not checked
};

const FloatingCase floating_cases[] = {
    { "M2", 6.2, 30., 2.2, 2.4, 0, 0 },   { "M3", 15.65, 30., 3.25, 3.4, 0, 0 }, { "M4", 26.9, 30., 4., 4.5, 0, 0 },
    { "M5", 39.9, 30., 5., 5.5, 0, 0 },   { "M6", 54.4, 30., 5.5, 6.6, 0, 0 },   { "M8", 71.4, 30., 7.5, 9., 0, 0 },
    { "1.6 mm", 6.25, 70., 2.25, 1.6, 0, 0 }, { "slot", 18., 70., 5.5, 0., -1, 0 },
    // The fewest layers first: along the sides of the square (turned 20 degrees) and of the hexagon.
    { "square", 32.5, 70., 5., 0., -1, 2 },
    // 16 x 8 mm cavity: the bridges beside the hole are shortest across the 8 mm, along Y.
    { "off center", 52.5, 70.5, 8., 4., 90, 0 },
    { "hexagon", 67.5, 70., 6.9, 0., 0, 3 },
};

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

struct Sliced {
    Print print;
    Model model;
};

void slice(Sliced &s, std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> items)
{
    TriangleMesh mesh;
    const std::string path = (boost::filesystem::path(TEST_DATA_DIR) / "screw_holes.obj").string();
    REQUIRE(load_obj(path.c_str(), &mesh));
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({ { "layer_height", "0.2" }, { "first_layer_height", "0.2" }, { "nozzle_diameter", "0.4" },
                                    { "perimeters", "4" }, { "fill_density", "15%" } });
    config.set_deserialize_strict(items);
    Test::init_print({ std::move(mesh) }, s.print, s.model, config);
    s.print.process();
}

// Model coordinates to the layers': the first layer is the whole block outline (X 0, Y 20).
Point to_layer(const PrintObject &object, double x, double y)
{
    const BoundingBox block = get_extents(object.layers().front()->lslices);
    return block.min + Point(scaled<coord_t>(x), scaled<coord_t>(y - 20.));
}

// Angle difference of two line directions, modulo PI, in degrees.
double line_angle_diff(double a, double b)
{
    double d = std::fmod(std::abs(a - b), PI);
    return Geometry::rad2deg(std::min(d, PI - d));
}

struct BridgeLayer { const Layer *layer; double angle; const ExPolygons *area; };

// The bridge layers of a hole: layers with a forced bridge direction near it, from the bottom.
std::vector<BridgeLayer> bridge_layers(const PrintObject &object, const FloatingCase &c)
{
    const Point center = to_layer(object, c.x, c.y);
    std::vector<BridgeLayer> out;
    for (const Layer *layer : object.layers())
        for (const Layer::ForcedBridge &forced : layer->forced_bridges)
            if (unscale<double>((get_extents(forced.area).center() - center).cast<double>().norm()) < c.void_radius)
                out.push_back({ layer, forced.angle, &forced.area });
    return out;
}

// The hole of the layer at the center (nullptr if none).
const Polygon* hole_at(const Layer &layer, const Point &center)
{
    for (const ExPolygon &island : layer.lslices)
        for (const Polygon &h : island.holes)
            if (h.contains(center))
                return &h;
    return nullptr;
}

// With FLOATING_HOLES_SVG set to a folder: the layers from 3.8 to 6 mm drawn there (grey slices, extrusions by role:
// bridge infill blue, overhang perimeters navy, perimeters orange, solid infill purple, other green).
void dump_svg(const PrintObject &object, const std::string &tag)
{
    const char *dir = std::getenv("FLOATING_HOLES_SVG");
    if (dir == nullptr)
        return;
    for (const Layer *layer : object.layers()) {
        if (layer->print_z < 3.79 || layer->print_z > 6.01)
            continue;
        char name[64];
        snprintf(name, sizeof(name), "%s_%05.2f.svg", tag.c_str(), layer->print_z);
        SVG svg((boost::filesystem::path(dir) / name).string(), get_extents(object.layers().front()->lslices));
        svg.draw(layer->lslices, "#dddddd");
        for (const LayerRegion *layerm : layer->regions()) {
            std::vector<const ExtrusionPath*> paths;
            collect_paths(&layerm->perimeters(), paths);
            collect_paths(&layerm->fills(), paths);
            collect_paths(&layerm->thin_fills(), paths);
            for (const ExtrusionPath *p : paths) {
                const ExtrusionRole role = p->role();
                const char *color = role == ExtrusionRole::BridgeInfill ? "#3070ff" : role == ExtrusionRole::OverhangPerimeter ? "#000080" :
                                    role.is_perimeter() ? "#ff8c00" : role == ExtrusionRole::SolidInfill ? "#9050c0" : "#30a030";
                svg.draw(p->polyline, color, scale_(p->width()) * 0.8);
            }
        }
        svg.Close();
    }
}

} // namespace

TEST_CASE("Floating holes: off by default, a warning lists the holes wider than 2 mm", "[FloatingHoles]") {
    CHECK(! DynamicPrintConfig::full_print_config().opt_bool("floating_hole_bridges"));
    Sliced s;
    slice(s, {});
    const PrintObject &object = *s.print.objects().front();
    // No bridge layers; the voids are marked on the ceiling layer only (left as sliced by the overhangs cut).
    for (const Layer *layer : object.layers()) {
        CHECK(layer->forced_bridges.empty());
        if (std::abs(layer->print_z - 4.2) > 0.01)
            CHECK(layer->floating_hole_zones.empty());
        else
            CHECK(layer->floating_hole_zones.size() == 11);
    }
    // 6 counterbores + slot, square, off center, hexagon; not the 1.6 mm hole.
    const auto state = object.step_state_with_warnings(posSlice);
    bool found = false;
    for (const auto &w : state.warnings)
        if (w.message.find("floating hole") != std::string::npos) {
            found = true;
            INFO(w.message);
            CHECK(w.message.rfind("10 floating hole(s)", 0) == 0);
        }
    CHECK(found);
}

TEST_CASE("Floating holes: parallel bridges per layer, turned by 180 / N, then the hole on the polygon", "[FloatingHoles]") {
    for (const char *max_slope : { "0", "25%" }) {
        INFO("overhangs_max_slope " << max_slope);
        Sliced s;
        slice(s, { { "floating_hole_bridges", "1" }, { "overhangs_max_slope", max_slope } });
        const PrintObject &object = *s.print.objects().front();
        dump_svg(object, std::string("on_slope_") + (max_slope[0] == '0' ? "0" : "25"));
        // No warning with the option on.
        for (const auto &w : object.step_state_with_warnings(posSlice).warnings)
            CHECK(w.message.find("floating hole") == std::string::npos);
        const double ext_width = object.layers().front()->regions().front()->flow(frExternalPerimeter).width();
        for (const FloatingCase &c : floating_cases) {
            INFO(c.name);
            const Point center = to_layer(object, c.x, c.y);
            const std::vector<BridgeLayer> layers = bridge_layers(object, c);
            REQUIRE(layers.size() >= 2);
            const size_t n = layers.size();
            if (c.layers > 0)
                CHECK(n == c.layers);
            // Consecutive layers from the first one over the cavity (4.0 .. 4.2).
            CHECK(std::abs(layers.front().layer->print_z - 4.2) < 0.01);
            for (size_t k = 1; k < n; ++ k) {
                CHECK(layers[k].layer->id() == layers[0].layer->id() + k);
                CHECK(line_angle_diff(layers[k].angle, layers[0].angle + double(k) * PI / double(n)) < 0.01);
            }
            if (c.angle0 >= 0)
                CHECK(line_angle_diff(layers.front().angle, Geometry::deg2rad(double(c.angle0))) < 1.5);
            if (c.d > 0) {
                // Round: the smallest N whose 2N-gon corners are within half an external perimeter width of the hole.
                const double r = c.d / 2.;
                size_t expected = 2;
                while (expected < 16 && r / std::cos(PI / (2. * double(expected))) - r > 0.5 * ext_width + 0.01)
                    ++ expected;
                CHECK(n == expected);
                // The last opening: around the hole, corners within the limit.
                const Polygon *last = hole_at(*layers.back().layer, center);
                REQUIRE(last != nullptr);
                for (const Point &p : last->points) {
                    const double dist = unscale<double>((p - center).cast<double>().norm());
                    CHECK(dist > r - 0.05);
                    CHECK(dist < r + 0.5 * ext_width + 0.05);
                }
                // Above it the model's hole.
                const Layer *above = layers.back().layer->upper_layer;
                REQUIRE(above != nullptr);
                const Polygon *hole = hole_at(*above, center);
                REQUIRE(hole != nullptr);
                const BoundingBox bb = get_extents(*hole);
                CHECK(std::abs(unscale<double>(std::max(bb.size().x(), bb.size().y())) - c.d) < 0.1);
            }
            // The bridge infill over the cavity runs along the layer's direction (both sides parallel).
            size_t bridge_segments = 0;
            for (const BridgeLayer &bl : layers) {
                const ExPolygons pieces = offset_ex(*bl.area, scaled<float>(0.5));
                for (const LayerRegion *layerm : bl.layer->regions()) {
                    std::vector<const ExtrusionPath*> paths;
                    collect_paths(&layerm->fills(), paths);
                    for (const ExtrusionPath *p : paths)
                        if (p->role() == ExtrusionRole::BridgeInfill)
                            for (size_t i = 1; i < p->polyline.size(); ++ i) {
                                const Point a = p->polyline.points[i - 1], b = p->polyline.points[i];
                                // Lines of this hole's bridge pieces (the next hole's anchors may reach here).
                                if ((b - a).cast<double>().norm() < scale_(1.) || ! expolygons_contain(pieces, (a + b) / 2))
                                    continue;
                                ++ bridge_segments;
                                CHECK(line_angle_diff(std::atan2(double(b.y() - a.y()), double(b.x() - a.x())), bl.angle) < 1.);
                            }
                }
            }
            if (c.void_radius - c.d / 2. > 2.)
                CHECK(bridge_segments > 0);
        }
    }
}

TEST_CASE("Floating holes: two slices give the same layers", "[FloatingHoles]") {
    Sliced a, b;
    slice(a, { { "floating_hole_bridges", "1" } });
    slice(b, { { "floating_hole_bridges", "1" } });
    const PrintObject &oa = *a.print.objects().front(), &ob = *b.print.objects().front();
    REQUIRE(oa.layers().size() == ob.layers().size());
    for (size_t i = 0; i < oa.layers().size(); ++ i) {
        const Layer &la = *oa.layers()[i], &lb = *ob.layers()[i];
        CHECK(la.lslices == lb.lslices);
        REQUIRE(la.forced_bridges.size() == lb.forced_bridges.size());
        for (size_t k = 0; k < la.forced_bridges.size(); ++ k)
            CHECK(la.forced_bridges[k].angle == lb.forced_bridges[k].angle);
    }
}

TEST_CASE("Floating holes: a hole the overhangs cut leaves floating gets the bridge layers too", "[FloatingHoles]") {
    // The countersink of the fourth row (Y 90, X 9: 10 mm into a 3.4 mm hole over 1 mm): the cut at 25 % widens its last
    // steps, so the 3.4 mm hole starts over a void of about 9 mm.
    const FloatingCase countersink{ "countersink", 9., 90., 4.6, 3.4, 0, 0 };
    {
        Sliced s;
        slice(s, { { "floating_hole_bridges", "1" }, { "overhangs_max_slope", "25%" } });
        const PrintObject &object = *s.print.objects().front();
        const std::vector<BridgeLayer> layers = bridge_layers(object, countersink);
        REQUIRE(layers.size() >= 2);
        CHECK(std::abs(layers.front().layer->print_z - 5.2) < 0.01);
        for (size_t k = 1; k < layers.size(); ++ k)
            CHECK(line_angle_diff(layers[k].angle, layers[0].angle + double(k) * PI / double(layers.size())) < 0.01);
    }
    {
        Sliced s;
        slice(s, { { "overhangs_max_slope", "25%" } });
        const PrintObject &object = *s.print.objects().front();
        CHECK(bridge_layers(object, countersink).empty());
        bool found = false;
        for (const auto &w : object.step_state_with_warnings(posSlice).warnings)
            if (w.message.find("floating hole") != std::string::npos) {
                found = true;
                INFO(w.message);
                // The 10 of the model + the countersink's hole.
                CHECK(w.message.rfind("11 floating hole(s)", 0) == 0);
            }
        CHECK(found);
    }
}
