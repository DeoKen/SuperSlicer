#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <sstream>
#include <string>
#include <vector>
#include <boost/algorithm/string/predicate.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/GCode/SmallAreaFlowCompensation.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;
using Catch::Approx;

// Small area flow compensation (SuperSlicer / OrcaSlicer): flow factor per solid infill segment length.

static const std::string default_model = "0,0\n0.2,0.44\n0.4,0.61\n0.6,0.7\n0.8,0.76\n1.5,0.86\n2,0.89\n3,0.92\n5,0.95\n10,1";

TEST_CASE("Small area flow model: curve", "[SmallAreaFlow]") {
    std::string error;
    auto model = SmallAreaFlowModel::parse(default_model, &error);
    INFO(error);
    REQUIRE(model.has_value());
    REQUIRE(model->points().size() == 10);
    // Through the points.
    for (const SmallAreaFlowModel::Point &pt : model->points())
        CHECK(model->factor(pt.length) == Approx(pt.factor).margin(1e-9));
    // 1 beyond the last point, smooth and never decreasing in between (PCHIP doesn't overshoot).
    CHECK(model->factor(25.) == 1.);
    double last = model->factor(0.);
    for (double l = 0.01; l < 10.; l += 0.01) {
        const double f = model->factor(l);
        CHECK(f >= last - 1e-12);
        CHECK(f <= 1.);
        last = f;
    }
    // Semicolons as in OrcaSlicer's format, spaces, empty lines.
    CHECK(SmallAreaFlowModel::parse("0, 0;\n\n 1 , 0.5 ;\n2,1;").has_value());
    // Text form round trip.
    auto again = SmallAreaFlowModel::parse(SmallAreaFlowModel::to_text(model->points()));
    REQUIRE(again.has_value());
    CHECK(again->factor(0.3) == Approx(model->factor(0.3)).margin(1e-9));
}

TEST_CASE("Small area flow model: invalid models are refused", "[SmallAreaFlow]") {
    for (const char *text : { "", "0,0", "0.1,0\n10,1", "0,0\n5,0.5\n3,0.7\n10,1", "0,0\n2,0.6\n3,0.5\n10,1",
                              "0,0\n10,0.9", "0,0\nabc\n10,1", "0,0\n5\n10,1", "0,-0.1\n10,1" }) {
        INFO(text);
        std::string error;
        CHECK(! SmallAreaFlowModel::parse(text, &error).has_value());
        CHECK(! error.empty());
    }
}

TEST_CASE("Small area flow model: c field", "[SmallAreaFlow]") {
    // The user's profiles (c 1.0) are exactly what the c field writes.
    const std::string c1 = "0,0\n0.2,0.1667\n0.4,0.2857\n0.6,0.3750\n0.8,0.4444\n1,0.5000\n1.5,0.6000\n2,0.6667\n3,0.7500\n"
                           "5,0.8333\n10,0.9091\n20,0.9524\n30,0.9677\n50,1";
    CHECK(SmallAreaFlowModel::text_for_c(1.) == c1);
    // The setting's default is that curve.
    CHECK(DynamicPrintConfig::full_print_config().opt_string("small_area_infill_flow_compensation_model") == c1);
    REQUIRE(SmallAreaFlowModel::c_of_text(c1).has_value());
    CHECK(*SmallAreaFlowModel::c_of_text(c1) == Approx(1.));
    // Round trip over the range, every text a valid model.
    for (double c : { SmallAreaFlowModel::c_min, 0.05, 0.15, 0.25, 0.333, 1.25, 2.5, 7.1234, SmallAreaFlowModel::c_max }) {
        INFO(c);
        const std::string text = SmallAreaFlowModel::text_for_c(c);
        CHECK(SmallAreaFlowModel::parse(text).has_value());
        auto back = SmallAreaFlowModel::c_of_text(text);
        REQUIRE(back.has_value());
        // The same curve back (a large c with 4 decimals may come back rounded: it writes the same text).
        CHECK(SmallAreaFlowModel::text_for_c(*back) == text);
        CHECK(*back == Approx(c).margin(0.001));
    }
    // Other lengths following the formula are recognised; fewer decimals are compared to their own precision.
    CHECK(SmallAreaFlowModel::c_of_text("0,0\n0.5,0.6667\n2,0.8889\n10,1").has_value());
    CHECK(SmallAreaFlowModel::c_of_text("0,0\n1,0.5\n3,0.75\n10,1").has_value());
    // Drift from the formula (one point moved by 0.001 as a drag in the graph does), the stock curve (not the formula
    // to its 2 decimals), invalid text, no point in between, first factor not 0: no c.
    CHECK(! SmallAreaFlowModel::c_of_text("0,0\n0.2,0.1667\n0.4,0.2857\n0.6,0.3760\n0.8,0.4444\n1,0.5000\n50,1").has_value());
    CHECK(! SmallAreaFlowModel::c_of_text(default_model).has_value());
    CHECK(! SmallAreaFlowModel::c_of_text("abc").has_value());
    CHECK(! SmallAreaFlowModel::c_of_text("0,0\n10,1").has_value());
    CHECK(! SmallAreaFlowModel::c_of_text("0,0.1\n1,0.5\n10,1").has_value());
}

namespace {

struct Move { std::string type; int layer; double length; double e; std::string line; };

// G1 extrusions with their type, layer and length (relative E).
std::vector<Move> moves_of(const std::string &gcode)
{
    std::vector<Move> out;
    std::istringstream in(gcode);
    std::string line, type;
    int layer = -1;
    double x = 0., y = 0.;
    while (std::getline(in, line)) {
        if (line == ";LAYER_CHANGE")
            ++ layer;
        else if (boost::starts_with(line, ";TYPE:"))
            type = line.substr(6);
        else if (boost::starts_with(line, "G1 ") || boost::starts_with(line, "G2 ") || boost::starts_with(line, "G3 ")) {
            double nx = x, ny = y, e = 0.;
            bool has_e = false;
            std::istringstream words(line.substr(3));
            std::string w;
            while (words >> w) {
                if (w[0] == ';') break;
                if (w[0] == 'X') nx = std::stod(w.substr(1));
                else if (w[0] == 'Y') ny = std::stod(w.substr(1));
                else if (w[0] == 'E') { e = std::stod(w.substr(1)); has_e = true; }
            }
            // Extrusions only: no retractions / deretractions (E without XY).
            if (has_e && e > 0. && (nx != x || ny != y))
                out.push_back({ type, layer, std::hypot(nx - x, ny - y), e, line });
            x = nx;
            y = ny;
        }
    }
    return out;
}

std::string slice_cube(bool enabled, const std::string &top_pattern = "monotonic")
{
    auto config = DynamicPrintConfig::full_print_config_with({
        { "gcode_flavor",                                  "klipper" },
        { "use_relative_e_distances",                      1 },
        { "layer_gcode",                                   "G92 E0" },
        { "top_fill_pattern",                              top_pattern },
        { "small_area_infill_flow_compensation",           enabled ? 1 : 0 },
        { "small_area_infill_flow_compensation_model",     default_model },
    });
    return Test::slice({ TestMesh::cube_20x20x20 }, config);
}

} // namespace

// Flow factors by line length (SmallAreaFlowModel::line_factors()).
TEST_CASE("Small area flow model: factor by line, not by segment", "[SmallAreaFlow]") {
    const auto model = SmallAreaFlowModel::parse(default_model);
    REQUIRE(model.has_value());
    auto factors = [&model](const std::vector<Vec2d> &pts, double width = 0.45) {
        std::vector<double> len;
        for (size_t i = 1; i < pts.size(); ++ i)
            len.push_back((pts[i] - pts[i - 1]).norm());
        return model->line_factors(pts, len, width);
    };
    SECTION("a straight line cut into short segments gets the factor of its whole length") {
        std::vector<Vec2d> pts;
        for (int i = 0; i <= 8; ++ i)
            pts.emplace_back(0.25 * i, 0.);   // 2 mm in 0.25 mm pieces
        for (double f : factors(pts))
            CHECK(f == Approx(model->factor(2.)));
    }
    SECTION("a curved line of short segments (gentle turns) is one line") {
        std::vector<Vec2d> pts{ Vec2d(0., 0.) };
        for (int i = 1; i <= 40; ++ i) {
            const double a = 0.1 * i;   // ~5.7 degrees per 0.5 mm segment: 20 mm long
            pts.push_back(pts.back() + 0.5 * Vec2d(std::cos(a), std::sin(a)));
        }
        for (double f : factors(pts))
            CHECK(f == 1.);              // 20 mm >= the last point of the curve (10 mm)
    }
    SECTION("zig-zag: lines get their own length's factor, the links along the wall follow the lines") {
        // 3 mm lines along X joined by 0.4 mm links along Y.
        std::vector<Vec2d> pts{ Vec2d(0., 0.), Vec2d(3., 0.), Vec2d(3., 0.4), Vec2d(0., 0.4), Vec2d(0., 0.8), Vec2d(3., 0.8) };
        const std::vector<double> f = factors(pts);
        REQUIRE(f.size() == 5);
        for (double v : f)
            CHECK(v == Approx(model->factor(3.)));
        CHECK(model->factor(0.4) < model->factor(3.) - 0.2);  // the link would have got much less on its own
    }
    SECTION("a link between lines of different lengths gets the mean of their factors") {
        std::vector<Vec2d> pts{ Vec2d(0., 0.), Vec2d(1., 0.), Vec2d(1., 0.4), Vec2d(-4., 0.4) };
        const std::vector<double> f = factors(pts);
        CHECK(f[0] == Approx(model->factor(1.)));
        CHECK(f[1] == Approx(0.5 * (model->factor(1.) + model->factor(5.))));
        CHECK(f[2] == Approx(model->factor(5.)));
    }
    SECTION("a short piece longer than 3 widths, or not shorter than its neighbours, keeps its own factor") {
        // 1.5 mm piece between 3 mm lines, width 0.45 (3 widths = 1.35 mm): it is a line of its own.
        std::vector<Vec2d> pts{ Vec2d(0., 0.), Vec2d(3., 0.), Vec2d(3., 1.5), Vec2d(0., 1.5) };
        CHECK(factors(pts)[1] == Approx(model->factor(1.5)));
        // 0.3 mm piece between 0.25 mm pieces: not shorter than its neighbours, so a line of its own.
        std::vector<Vec2d> pts2{ Vec2d(0., 0.), Vec2d(0.25, 0.), Vec2d(0.25, 0.3), Vec2d(0., 0.3) };
        CHECK(factors(pts2)[1] == Approx(model->factor(0.3)));
        // A strip 0.5 mm wide: 0.5 mm lines, the 0.4 mm link follows them.
        std::vector<Vec2d> pts3{ Vec2d(0., 0.), Vec2d(0.5, 0.), Vec2d(0.5, 0.4), Vec2d(0., 0.4) };
        for (double f : factors(pts3))
            CHECK(f == Approx(model->factor(0.5)));
    }
}

TEST_CASE("Small area flow compensation in the G-code", "[SmallAreaFlow]") {
    // 20 mm cube, solid lines at 45 degrees: the lines of 10 mm and more are not changed, nor the links between two
    // of them (they used to be reduced as 0.5 mm lines); the short lines in the corners are reduced.
    const std::vector<Move> off = moves_of(slice_cube(false));
    const std::vector<Move> on  = moves_of(slice_cube(true));
    REQUIRE(off.size() == on.size());
    int long_links = 0, reduced = 0;
    for (size_t i = 0; i < off.size(); ++ i) {
        INFO(off[i].line << "  vs  " << on[i].line);
        REQUIRE(off[i].type == on[i].type);
        const bool applies = off[i].layer > 0 && (off[i].type == "Solid infill" || off[i].type == "Top solid infill");
        if (! applies) {
            CHECK(on[i].e == off[i].e);
            continue;
        }
        CHECK(on[i].e <= off[i].e + 2e-5);
        if (on[i].e < off[i].e - 1e-4)
            ++ reduced;
        if (off[i].length >= 10.)
            CHECK(on[i].e == Approx(off[i].e).margin(2e-5));
        else if (off[i].length < 1. && i > 0 && i + 1 < off.size() && off[i - 1].length >= 10. && off[i + 1].length >= 10. &&
                 off[i - 1].type == off[i].type && off[i + 1].type == off[i].type) {
            CHECK(on[i].e == Approx(off[i].e).margin(2e-5));
            ++ long_links;
        }
    }
    CHECK(long_links > 0);
    CHECK(reduced > 0);
}

TEST_CASE("Small area flow compensation in the G-code: narrow part", "[SmallAreaFlow]") {
    const auto model = SmallAreaFlowModel::parse(default_model);
    REQUIRE(model.has_value());
    // 4 x 20 x 5 mm, one perimeter, solid lines along X (about 3 mm) and Y (about 19 mm).
    auto slice_narrow = [](bool enabled) {
        auto config = DynamicPrintConfig::full_print_config_with({
            { "gcode_flavor",                                  "klipper" },
            { "use_relative_e_distances",                      1 },
            { "layer_gcode",                                   "G92 E0" },
            { "perimeters",                                    1 },
            { "fill_angle",                                    0 },
            { "small_area_infill_flow_compensation",           enabled ? 1 : 0 },
            { "small_area_infill_flow_compensation_model",     default_model },
        });
        return Test::slice({ mesh(TestMesh::cube_20x20x20, Vec3d::Zero(), Vec3d(0.2, 1., 0.25)) }, config);
    };
    const std::vector<Move> off = moves_of(slice_narrow(false));
    const std::vector<Move> on  = moves_of(slice_narrow(true));
    REQUIRE(off.size() == on.size());
    int lines = 0, links = 0;
    for (size_t i = 0; i < off.size(); ++ i) {
        INFO(off[i].line << "  vs  " << on[i].line);
        REQUIRE(off[i].type == on[i].type);
        const bool applies = off[i].layer > 0 && (off[i].type == "Solid infill" || off[i].type == "Top solid infill");
        if (! applies) {
            CHECK(on[i].e == off[i].e);
            continue;
        }
        const double ratio = on[i].e / off[i].e;
        CHECK(ratio <= 1. + 1e-3);
        if (off[i].length > 2.) {
            // A whole straight line: the factor of its length.
            CHECK(ratio == Approx(model->factor(off[i].length)).margin(2e-3));
            ++ lines;
        } else if (off[i].length < 0.6 && i > 0 && i + 1 < off.size() && off[i - 1].length > 2. && off[i + 1].length > 2. &&
                   off[i - 1].type == off[i].type && off[i + 1].type == off[i].type) {
            // A link between two lines: their factors (the mean, or a neighbour's when it joins that line at a gentle
            // turn), much more than the factor of its own length.
            const double f1 = model->factor(off[i - 1].length), f2 = model->factor(off[i + 1].length);
            CHECK(ratio >= std::min(f1, f2) - 2e-3);
            CHECK(ratio <= std::max(f1, f2) + 2e-3);
            CHECK(ratio > model->factor(off[i].length) + 0.1);
            ++ links;
        }
    }
    CHECK(lines > 0);
    CHECK(links > 0);
}

TEST_CASE("Small area flow compensation: top surfaces with a curved pattern are not changed", "[SmallAreaFlow]") {
    const std::vector<Move> off = moves_of(slice_cube(false, "concentric"));
    const std::vector<Move> on  = moves_of(slice_cube(true, "concentric"));
    REQUIRE(off.size() == on.size());
    int top = 0;
    for (size_t i = 0; i < off.size(); ++ i)
        if (off[i].type == "Top solid infill") {
            ++ top;
            CHECK(on[i].e == off[i].e);
        }
    CHECK(top > 0);
}
