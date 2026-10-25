#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Overhang hysteresis and negative overhang thresholds (fork).

namespace {

// Rings of n points (CCW), joined by side faces and closed by a cap at both ends.
// Each ring: { z, radius, x of the centre }.
TriangleMesh loft(const std::vector<Vec3d> &rings, int n)
{
    indexed_triangle_set its;
    for (const Vec3d &r : rings)
        for (int i = 0; i < n; ++ i) {
            const double a = 2. * PI * i / n;
            its.vertices.emplace_back(float(r.z() + r.y() * std::cos(a)), float(r.y() * std::sin(a)), float(r.x()));
        }
    for (int k = 0; k + 1 < int(rings.size()); ++ k)
        for (int i = 0; i < n; ++ i) {
            const int j = (i + 1) % n, a = k * n, b = (k + 1) * n;
            its.indices.emplace_back(a + i, a + j, b + j);
            its.indices.emplace_back(a + i, b + j, b + i);
        }
    const int bottom = int(its.vertices.size());
    its.vertices.emplace_back(float(rings.front().z()), 0.f, float(rings.front().x()));
    const int top = int(its.vertices.size());
    its.vertices.emplace_back(float(rings.back().z()), 0.f, float(rings.back().x()));
    const int last = (int(rings.size()) - 1) * n;
    for (int i = 0; i < n; ++ i) {
        const int j = (i + 1) % n;
        its.indices.emplace_back(bottom, j, i);
        its.indices.emplace_back(top, last + i, last + j);
    }
    return TriangleMesh(std::move(its));
}

// Pedestal r = 6 mm, then widening at `angle` from vertical, then a 1 mm cap.
TriangleMesh cone_out(double angle, int n)
{
    const double t = std::tan(angle * PI / 180.), h = std::min(6., 8. / t);
    return loft({ { 0., 6., 0. }, { 2., 6., 0. }, { 2. + h, 6. + h * t, 0. }, { 3. + h, 6. + h * t, 0. } }, n);
}

// Cylinder r = 6 mm leaning at `angle` from vertical: an overhang on one side fading out around the loop.
TriangleMesh tilted_cylinder(double angle, int n)
{
    const double t = std::tan(angle * PI / 180.), h = std::min(12., 14. / t);
    return loft({ { 0., 6., 0. }, { h, 6., h * t } }, n);
}

// Box 20 x 15 x 10 mm, its +X face leaning out at `angle`: a straight overhang, no facets along it.
TriangleMesh slab(double angle)
{
    const double t = std::tan(angle * PI / 180.), h = 10.;
    indexed_triangle_set its;
    its.vertices = { { 0, 0, 0 }, { 20, 0, 0 }, { 20, 15, 0 }, { 0, 15, 0 },
                     { 0, 0, float(h) }, { float(20 + h * t), 0, float(h) }, { float(20 + h * t), 15, float(h) }, { 0, 15, float(h) } };
    const auto quad = [&its](int a, int b, int c, int d) { its.indices.emplace_back(a, b, c); its.indices.emplace_back(a, c, d); };
    quad(0, 3, 2, 1); quad(4, 5, 6, 7); quad(0, 1, 5, 4); quad(1, 2, 6, 5); quad(2, 3, 7, 6); quad(3, 0, 4, 7);
    return TriangleMesh(std::move(its));
}

// 0.4 mm nozzle and lines, 0.2 mm layers, 2 perimeters: with a 0% threshold the overhang angle is atan(0.2 / 0.2) = 45°.
DynamicPrintConfig hysteresis_config(const std::string &generator, const std::string &hysteresis)
{
    return DynamicPrintConfig::full_print_config_with({
        { "nozzle_diameter",                    "0.4" },
        { "layer_height",                       0.2 },
        { "first_layer_height",                 0.2 },
        { "extrusion_width",                    "0.4" },
        { "first_layer_extrusion_width",        "0.4" },
        { "perimeter_extrusion_width",          "0.4" },
        { "external_perimeter_extrusion_width", "0.4" },
        { "perimeters",                         2 },
        { "fill_density",                       "10%" },
        { "skirts",                             0 },
        { "perimeter_generator",                generator },
        { "overhangs_threshold",                "0%" },
        { "overhangs_hysteresis",               hysteresis },
    });
}

struct OverhangRuns
{
    int    max_per_layer = 0; // most overhang perimeter runs in one layer
    int    layers        = 0; // layers with an overhang perimeter
    double length        = 0; // mm of overhang perimeters
};

// An overhang run starts at every ;TYPE:Overhang perimeter (the type is only written when it changes).
OverhangRuns overhang_runs(const std::string &gcode)
{
    OverhangRuns out;
    std::istringstream in(gcode);
    std::string        line, type;
    int                runs = 0;
    double             x = 0., y = 0.;
    const auto end_layer = [&]() {
        out.max_per_layer = std::max(out.max_per_layer, runs);
        out.layers += runs > 0;
        runs = 0;
    };
    while (std::getline(in, line)) {
        if (line.rfind(";LAYER_CHANGE", 0) == 0)
            end_layer();
        else if (line.rfind(";TYPE:", 0) == 0) {
            type = line.substr(6);
            runs += type == "Overhang perimeter";
        } else if (line.rfind("G1 ", 0) == 0) {
            double nx = x, ny = y, e = 0.;
            std::istringstream words(line.substr(3));
            for (std::string w; words >> w && w[0] != ';';)
                switch (w[0]) {
                case 'X': nx = std::stod(w.substr(1)); break;
                case 'Y': ny = std::stod(w.substr(1)); break;
                case 'E': e = std::stod(w.substr(1)); break;
                }
            if (e > 0. && type == "Overhang perimeter")
                out.length += std::hypot(nx - x, ny - y);
            x = nx;
            y = ny;
        }
    }
    end_layer();
    return out;
}

} // namespace

TEST_CASE("Overhang hysteresis: a wall as steep as the threshold does not flip", "[OverhangHysteresis]") {
    for (const std::string generator : { "arachne", "classic" }) {
        SECTION(generator + ": cone of 180 facets 1° past the threshold angle") {
            const OverhangRuns before = overhang_runs(Test::slice({ cone_out(46., 180) }, hysteresis_config(generator, "0")));
            const OverhangRuns after  = overhang_runs(Test::slice({ cone_out(46., 180) }, hysteresis_config(generator, "10%")));
            INFO("before: max " << before.max_per_layer << " runs per layer, after: " << after.max_per_layer);
            // Stock: the facets put pieces of the loops in front of and behind the threshold.
            CHECK(before.max_per_layer > 4);
            // One run per loop at most (2 perimeters), still an overhang.
            CHECK(after.max_per_layer <= 2);
            CHECK(after.layers > 0);
        }
    }
}

TEST_CASE("Overhang hysteresis keeps the real overhangs", "[OverhangHysteresis]") {
    for (const std::string generator : { "arachne", "classic" }) {
        SECTION(generator + ": straight face 1° past the threshold angle, same as stock") {
            const OverhangRuns before = overhang_runs(Test::slice({ slab(46.) }, hysteresis_config(generator, "0")));
            const OverhangRuns after  = overhang_runs(Test::slice({ slab(46.) }, hysteresis_config(generator, "10%")));
            CHECK(before.max_per_layer == 1);
            CHECK(after.max_per_layer == 1);
            CHECK(after.layers == before.layers);
            CHECK(std::abs(after.length - before.length) < 0.01 * before.length);
        }
        SECTION(generator + ": leaning cylinder, the overhang side well past the threshold") {
            const OverhangRuns before = overhang_runs(Test::slice({ tilted_cylinder(60., 90) }, hysteresis_config(generator, "0")));
            const OverhangRuns after  = overhang_runs(Test::slice({ tilted_cylinder(60., 90) }, hysteresis_config(generator, "10%")));
            CHECK(after.max_per_layer <= 2);
            CHECK(after.layers >= before.layers - 1);
            CHECK(std::abs(after.length - before.length) < 0.05 * before.length);
        }
        SECTION(generator + ": walls steeper than the threshold stay walls") {
            CHECK(overhang_runs(Test::slice({ cone_out(40., 180) }, hysteresis_config(generator, "10%"))).layers == 0);
            CHECK(overhang_runs(Test::slice({ slab(40.) }, hysteresis_config(generator, "10%"))).layers == 0);
        }
    }
}

TEST_CASE("Negative overhang threshold", "[OverhangHysteresis]") {
    DynamicPrintConfig config = hysteresis_config("arachne", "0");
    SECTION("-10%: atan(0.16 / 0.2) = 38.7°, a 42° wall is an overhang") {
        CHECK(overhang_runs(Test::slice({ slab(42.) }, config)).layers == 0);
        config.set_deserialize_strict({ { "overhangs_threshold", "-10%" } });
        CHECK(overhang_runs(Test::slice({ slab(42.) }, config)).layers > 0);
        CHECK(overhang_runs(Test::slice({ slab(36.) }, config)).layers == 0);
    }
    SECTION("Down to -40% of the nozzle diameter, in % or in mm") {
        Print print;
        Model model;
        config.set_deserialize_strict({ { "overhangs_threshold", "-40%" } });
        Test::init_print({ slab(42.) }, print, model, config);
        CHECK(print.validate().empty());
        for (const char *value : { "-45%", "-0.2" }) {
            Print print2;
            Model model2;
            config.set_deserialize_strict({ { "overhangs_threshold", value } });
            Test::init_print({ slab(42.) }, print2, model2, config);
            INFO(value);
            CHECK(! print2.validate().empty());
        }
    }
}
