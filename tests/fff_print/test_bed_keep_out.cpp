#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/GCode/BedKeepOut.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Bed keep-out zones (bed_keep_out_zones): travels are routed around them, printing inside them is refused.

namespace {

BedKeepOut awd_corners()
{
    // The front corners of a 350 mm AWD Voron.
    BedKeepOut keep_out;
    keep_out.init(BedKeepOut::parse("0,0,40,40;310,0,350,40"), BoundingBoxf(Vec2d(0, 0), Vec2d(350, 350)));
    return keep_out;
}

Polyline polyline_mm(std::initializer_list<Vec2d> points)
{
    Polyline out;
    for (const Vec2d &p : points)
        out.points.emplace_back(Point::new_scale(p.x(), p.y()));
    return out;
}

void add_cube(Model &model, const std::string &name, const Vec3d &offset)
{
    ModelObject *object = model.add_object();
    object->name = name;
    ModelVolume *volume = object->add_volume(Test::mesh(TestMesh::cube_20x20x20));
    volume->translate(offset);
    object->add_instance();
    object->ensure_on_bed();
}

DynamicPrintConfig keep_out_config(const std::string &zones)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config_with({
        { "bed_shape",          "0x0,350x0,350x350,0x350" },
        { "bed_keep_out_zones", zones },
        { "skirts",             0 },
        { "brim_width",         0 },
    });
    return config;
}

std::string slice_model(Model &model, const DynamicPrintConfig &config)
{
    Print print;
    print.apply(model, config);
    const std::string error = print.validate();
    if (! error.empty())
        throw Slic3r::SlicingError(error);
    return Test::gcode(print);
}

// Number of XY travel moves (G0/G1 without extrusion) crossing the zone.
int travels_crossing(const std::string &gcode, const BoundingBoxf &zone)
{
    BedKeepOut test;
    test.init({ BoundingBoxf(zone.min + Vec2d(BedKeepOut::CLEARANCE, BedKeepOut::CLEARANCE),
                             zone.max - Vec2d(BedKeepOut::CLEARANCE, BedKeepOut::CLEARANCE)) },
              BoundingBoxf(Vec2d(-1000, -1000), Vec2d(1000, 1000)));
    int  crossing = 0;
    // The position is unknown until the first XY move (the start G-code may home anywhere).
    bool known    = false;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if ((line.cmd_is("G0") || line.cmd_is("G1")) && (line.has(Slic3r::X) || line.has(Slic3r::Y))) {
            if (known && line.dist_XY(self) > 0 && line.dist_E(self) <= 0 &&
                test.crosses(Vec2d(self.x(), self.y()), Vec2d(line.new_X(self), line.new_Y(self))))
                ++ crossing;
            known = true;
        }
    });
    return crossing;
}

} // namespace

TEST_CASE("Bed keep-out zones: parsing", "[KeepOut]") {
    std::vector<BoundingBoxf> zones;
    std::string error;
    CHECK(BedKeepOut::parse("", zones, &error));
    CHECK(zones.empty());
    CHECK(BedKeepOut::parse(" 0,0,40,40 ; 310, 0, 350, 40 ;", zones, &error));
    REQUIRE(zones.size() == 2);
    CHECK(zones[1].min == Vec2d(310, 0));
    CHECK(zones[1].max == Vec2d(350, 40));
    CHECK(! BedKeepOut::parse("0,0,40", zones, &error));
    CHECK(! error.empty());
    CHECK(! BedKeepOut::parse("40,0,0,40", zones, &error));
    CHECK(! BedKeepOut::parse("a,0,40,40", zones, &error));
}

TEST_CASE("Bed keep-out zones: routing", "[KeepOut]") {
    const BedKeepOut keep_out = awd_corners();
    Polyline out;

    SECTION("a travel clear of the zones is left alone") {
        const Polyline in = polyline_mm({ { 50, 5 }, { 300, 5 } });
        CHECK(! keep_out.intersects(in));
        REQUIRE(keep_out.reroute(in, out));
        CHECK(out.points == in.points);
    }
    SECTION("a travel across a corner goes around it through its inner corner") {
        const Polyline in = polyline_mm({ { 10, 60 }, { 60, 10 } });
        CHECK(keep_out.intersects(in));
        REQUIRE(keep_out.reroute(in, out));
        REQUIRE(out.size() == 3);
        CHECK(! keep_out.intersects(out));
        CHECK(unscaled<double>(out.points[1].x()) > 40 + BedKeepOut::CLEARANCE);
        CHECK(unscaled<double>(out.points[1].y()) > 40 + BedKeepOut::CLEARANCE);
    }
    SECTION("a travel from one corner area to the other avoids both zones") {
        const Polyline in = polyline_mm({ { 20, 60 }, { 60, 20 }, { 290, 20 }, { 330, 60 } });
        REQUIRE(keep_out.intersects(in));
        REQUIRE(keep_out.reroute(in, out));
        CHECK(! keep_out.intersects(out));
    }
    SECTION("a travel starting inside a zone may leave it") {
        const Polyline in = polyline_mm({ { 20, 20 }, { 100, 100 } });
        REQUIRE(keep_out.reroute(in, out));
        CHECK(out.points == in.points);
    }
    SECTION("a travel ending inside a zone cannot be routed") {
        CHECK(! keep_out.reroute(polyline_mm({ { 100, 100 }, { 20, 20 } }), out));
    }
    SECTION("running along the edge of a zone is allowed") {
        CHECK(! keep_out.intersects(polyline_mm({ { 41, 0 }, { 41, 100 } })));
    }
}

TEST_CASE("Bed keep-out zones: travels in sliced G-code go around a zone", "[KeepOut]") {
    // Two cubes side by side with a zone between them: the travels between the cubes would cross it.
    Model model;
    add_cube(model, "left",  Vec3d(90, 90, 0));
    add_cube(model, "right", Vec3d(190, 90, 0));
    const BoundingBoxf zone(Vec2d(140, 70), Vec2d(160, 130));

    const std::string without = slice_model(model, keep_out_config(""));
    // Sanity check of the test itself: without the zone some travels do cross it.
    REQUIRE(travels_crossing(without, zone) > 0);

    const std::string with = slice_model(model, keep_out_config("140,70,160,130"));
    CHECK(travels_crossing(with, zone) == 0);
}

TEST_CASE("Bed keep-out zones: the first move after the start G-code goes around a zone", "[KeepOut]") {
    // The start G-code ends its purge line just right of the front left zone; the object sits behind that zone,
    // so the straight move to the first perimeter would cut through the zone.
    Model model;
    add_cube(model, "cube", Vec3d(3, 45, 0));
    DynamicPrintConfig config = keep_out_config("0,0,40,40;310,0,350,40");
    config.set_deserialize_strict({ { "start_gcode", "G28\nG1 X45 Y2 Z0.3 F3000\nG1 Z2" } });
    const BoundingBoxf zone(Vec2d(-1, -1), Vec2d(41, 41));
    DynamicPrintConfig config_without = config;
    config_without.set_deserialize_strict({ { "bed_keep_out_zones", "" } });
    // Sanity check of the test itself: without the zones the first move crosses the front left corner.
    REQUIRE(travels_crossing(slice_model(model, config_without), zone) > 0);
    CHECK(travels_crossing(slice_model(model, config), zone) == 0);
}

TEST_CASE("Bed keep-out zones: printing inside a zone is refused", "[KeepOut]") {
    SECTION("an object") {
        Model model;
        add_cube(model, "cube", Vec3d(30, 30, 0));   // 30..50, the zone ends at 40
        CHECK_THROWS_AS(slice_model(model, keep_out_config("0,0,40,40")), Slic3r::SlicingError);
    }
    SECTION("an object next to a zone is fine") {
        Model model;
        add_cube(model, "cube", Vec3d(45, 45, 0));
        CHECK_NOTHROW(slice_model(model, keep_out_config("0,0,40,40")));
    }
    SECTION("a skirt") {
        Model model;
        add_cube(model, "cube", Vec3d(45, 45, 0));
        DynamicPrintConfig config = keep_out_config("0,0,40,40");
        config.set_deserialize_strict({ { "skirts", 1 }, { "skirt_distance", 6 } });
        CHECK_THROWS_AS(slice_model(model, config), Slic3r::SlicingError);
    }
    SECTION("an invalid zone is reported by the validation") {
        Model model;
        add_cube(model, "cube", Vec3d(100, 100, 0));
        Print print;
        print.apply(model, keep_out_config("0,0,40"));
        CHECK(! print.validate().empty());
    }
}
