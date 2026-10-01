#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;
using Catch::Approx;

// Hole size compensation (hole_size_compensation, hole_size_threshold), ported from SuperSlicer.

namespace {

struct Areas { double holes = 0; double contours = 0; };

// Hole and contour areas (mm²) of a middle layer of the first object.
Areas layer_areas(TestMesh mesh, std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> items)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict(items);
    Print print;
    Test::init_and_process_print({ mesh }, print, config);
    const PrintObject &object = *print.objects().front();
    const Layer &layer = *object.layers()[object.layers().size() / 2];
    Areas out;
    for (const ExPolygon &expoly : layer.lslices) {
        out.contours += unscaled(unscaled(expoly.contour.area()));
        for (const Polygon &hole : expoly.holes)
            out.holes += unscaled(unscaled(std::abs(hole.area())));
    }
    return out;
}

} // namespace

TEST_CASE("Hole size compensation", "[HoleCompensation]") {
    const Areas stock = layer_areas(TestMesh::cube_with_hole, {});
    // 20 mm cube with a 10 x 10 mm hole.
    REQUIRE(stock.holes == Approx(100.).margin(1.));

    SECTION("negative: the convex hole gets bigger") {
        const Areas a = layer_areas(TestMesh::cube_with_hole, { { "hole_size_compensation", -0.5 } });
        CHECK(a.holes == Approx(11. * 11.).margin(1.5));
        CHECK(a.contours == Approx(stock.contours).margin(0.01));
    }
    SECTION("positive: the convex hole gets smaller") {
        const Areas a = layer_areas(TestMesh::cube_with_hole, { { "hole_size_compensation", 0.5 } });
        CHECK(a.holes == Approx(9. * 9.).margin(1.));
        CHECK(a.contours == Approx(stock.contours).margin(0.01));
    }
    SECTION("above four times the threshold the hole is not changed") {
        const Areas a = layer_areas(TestMesh::cube_with_hole, { { "hole_size_compensation", -0.5 }, { "hole_size_threshold", 20 } });
        CHECK(a.holes == Approx(stock.holes).margin(0.01));
    }
    SECTION("between the threshold and four times it the compensation fades out") {
        // Hole area 100 = 2 x threshold: a third of the compensation is lost.
        const Areas a = layer_areas(TestMesh::cube_with_hole, { { "hole_size_compensation", -0.5 }, { "hole_size_threshold", 50 } });
        CHECK(a.holes > stock.holes + 1.);
        CHECK(a.holes < 11. * 11. - 1.);
    }
}

TEST_CASE("Hole size compensation: concave holes are not changed", "[HoleCompensation]") {
    const Areas stock = layer_areas(TestMesh::cube_with_concave_hole, {});
    REQUIRE(stock.holes > 10.);
    const Areas a = layer_areas(TestMesh::cube_with_concave_hole, { { "hole_size_compensation", -0.5 } });
    CHECK(a.holes == Approx(stock.holes).margin(0.01));
}
