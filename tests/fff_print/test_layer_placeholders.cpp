#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <sstream>
#include <string>
#include <vector>
#include <boost/algorithm/string/predicate.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;
using Catch::Approx;

// current_layer_height and total_print_height placeholders: the real Z step of each layer and the top of the print.

namespace {

const char *layer_gcode = ";LH {current_layer_height} Z {layer_z} N {layer_num} T {total_layer_count} H {total_print_height}";

struct LayerCheck {
    int    layer_changes  = 0;   // ;LAYER_CHANGE lines
    int    checked        = 0;   // ;LH lines
    double last_z         = 0.;
    double total_height   = -1.;
    int    total_layers   = -1;
    std::vector<double> heights;
};

// Walks the G-code: each ;LH line must report the ;HEIGHT: of its layer change (= Z step), its ;Z:,
// and the same layer number as the viewer (one per ;LAYER_CHANGE).
LayerCheck check_layers(const std::string &gcode)
{
    LayerCheck r;
    std::istringstream in(gcode);
    std::string line;
    double z = 0., height = 0.;
    bool   expect_height = false;
    while (std::getline(in, line)) {
        if (line == ";LAYER_CHANGE") {
            ++ r.layer_changes;
        } else if (boost::starts_with(line, ";Z:")) {
            z = std::stod(line.substr(3));
            expect_height = true;
        } else if (expect_height && boost::starts_with(line, ";HEIGHT:")) {
            // The first ;HEIGHT: after ;Z: is the layer's Z step; later ones are per extrusion.
            height = std::stod(line.substr(8));
            expect_height = false;
        } else if (boost::starts_with(line, ";LH ")) {
            double lh, lz, h;
            int    n, t;
            REQUIRE(sscanf(line.c_str(), ";LH %lf Z %lf N %d T %d H %lf", &lh, &lz, &n, &t, &h) == 5);
            INFO(line);
            CHECK(lh == Approx(height).margin(1e-4));
            CHECK(lz == Approx(z).margin(1e-4));
            CHECK(n + 1 == r.layer_changes);
            r.total_layers = t;
            r.total_height = h;
            r.heights.emplace_back(lh);
            ++ r.checked;
        }
        if (boost::starts_with(line, ";Z:"))
            r.last_z = z;
    }
    return r;
}

std::string slice_with(TestMesh mesh, DynamicPrintConfig config, const std::vector<coordf_t> &layer_height_profile = {})
{
    config.set_deserialize_strict({ { "layer_gcode", layer_gcode } });
    Print print;
    Model model;
    init_print({ mesh }, print, model, config);
    if (! layer_height_profile.empty()) {
        model.objects.front()->layer_height_profile.set(layer_height_profile);
        print.apply(model, config);
    }
    print.process();
    return Test::gcode(print);
}

void check_common(const LayerCheck &r)
{
    // layer_gcode is not run on the first layer.
    CHECK(r.checked == r.layer_changes - 1);
    CHECK(r.total_layers == r.layer_changes);
    CHECK(r.total_height == Approx(r.last_z).margin(1e-4));
}

} // namespace

TEST_CASE("current_layer_height follows variable layer height", "[LayerPlaceholders]") {
    auto config = DynamicPrintConfig::full_print_config_with({
        { "layer_height",       0.2 },
        { "first_layer_height", 0.3 },
    });
    const LayerCheck r = check_layers(slice_with(TestMesh::cube_20x20x20, config, { 0., 0.1, 8., 0.1, 12., 0.3, 20., 0.3 }));
    check_common(r);
    // The variable layer height profile may end the last layer slightly above the object top (20.016 here).
    CHECK(r.total_height == Approx(20.).margin(0.05));
    REQUIRE(! r.heights.empty());
    // Thin layers at the bottom, thick at the top: never the 0.2 print setting all the way.
    CHECK(r.heights.front() < 0.15);
    CHECK(r.heights.back() > 0.25);
}

TEST_CASE("current_layer_height with support layers between object layers", "[LayerPlaceholders]") {
    auto config = DynamicPrintConfig::full_print_config_with({
        { "layer_height",                        0.2 },
        { "first_layer_height",                  0.3 },
        { "support_material",                    1 },
        { "support_material_contact_distance",   0.15 },
        { "support_material_synchronize_layers", 0 },
    });
    const LayerCheck r = check_layers(slice_with(TestMesh::overhang, config));
    check_common(r);
    // Support-only layers sit between object layers, so some Z steps are smaller than layer_height.
    bool smaller = false;
    for (double h : r.heights)
        smaller |= h < 0.2 - 1e-3;
    CHECK(smaller);
}
