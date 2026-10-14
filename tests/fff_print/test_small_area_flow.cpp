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

TEST_CASE("Small area flow compensation in the G-code", "[SmallAreaFlow]") {
    const auto model = SmallAreaFlowModel::parse(default_model);
    REQUIRE(model.has_value());
    const std::vector<Move> off = moves_of(slice_cube(false));
    const std::vector<Move> on  = moves_of(slice_cube(true));
    // Same moves, only the amounts of extrusion differ.
    REQUIRE(off.size() == on.size());
    int compensated = 0;
    for (size_t i = 0; i < off.size(); ++ i) {
        INFO(off[i].line << "  vs  " << on[i].line);
        REQUIRE(off[i].type == on[i].type);
        const bool applies = off[i].layer > 0 && (off[i].type == "Solid infill" || off[i].type == "Top solid infill");
        if (applies) {
            const double expected = off[i].e * model->factor(off[i].length);
            CHECK(on[i].e == Approx(expected).margin(2e-5));
            if (on[i].e < off[i].e - 1e-4)
                ++ compensated;
        } else {
            CHECK(on[i].e == off[i].e);
        }
    }
    // The short connections between the solid infill lines are reduced; the 20 mm lines are not.
    CHECK(compensated > 0);
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
