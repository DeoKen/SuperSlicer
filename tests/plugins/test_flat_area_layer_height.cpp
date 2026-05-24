#include <catch2/catch.hpp>

#include "plugin_test_helpers.hpp"
#include "test_data.hpp"

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/Api/host/steps/LayerHeightStep.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace Slic3r;

TriangleMesh make_translated_cube(const double side_mm, const double z_min_mm, const double z_max_mm, const double x_mm)
{
    TriangleMesh cube = make_cube(side_mm, side_mm, z_max_mm - z_min_mm);
    cube.translate(float(x_mm), 0.f, float(z_min_mm));
    return cube;
}

TriangleMesh make_flat_area_test_mesh()
{
    // One object made from several disconnected cubes at different Z ranges.
    //
    // The tiny base cube keeps the object on the bed and is ignored by the
    // plugin because it is below the fixed first layer. The two large cubes
    // have horizontal face areas above 50 mm2; the two small cubes are below
    // that threshold. This lets the test verify both "all flat areas are
    // considered" and "the min flat area setting removes only the small ones".
    TriangleMesh mesh = make_translated_cube(1., 0., 0.1, -30.);
    mesh.merge(make_translated_cube(10., 2.3, 2.6, -15.));
    mesh.merge(make_translated_cube(3., 4.2, 4.5, 0.));
    mesh.merge(make_translated_cube(8., 6.1, 6.4, 15.));
    mesh.merge(make_translated_cube(2., 8.1, 8.4, 30.));
    return mesh;
}

TriangleMesh make_no_internal_flat_area_test_mesh()
{
    // Same total object height as make_flat_area_test_mesh(), but as one
    // continuous cube. Its only horizontal polygons are the bed face and the
    // top face; both are intentionally ignored by the plugin. Therefore none
    // of the intermediate Z anchors from make_flat_area_test_mesh() should be
    // present in the generated layer-height profile.
    return make_cube(10., 10., 8.4);
}

DynamicPrintConfig flat_area_config(const double min_flat_area_mm2)
{
    Slic3r::Test::Plugins::ensure_plugin_test_runtime_initialized();

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"first_layer_height", "0.5"},
        {"layer_height", "2"},
        {"layer_height_min_flat_area", std::to_string(min_flat_area_mm2)},
        {"max_layer_height", "2"},
        {"min_layer_height", "0.05"},
        {"nozzle_diameter", "2"},
        {"perimeters", "1"},
        {"z_step", "0"}
    });
    return config;
}

std::vector<coord_t> run_flat_area_layer_height_profile(const TriangleMesh &mesh, const double min_flat_area_mm2)
{
    Slic3r::Test::Plugins::ensure_plugin_test_runtime_initialized();

    Model model;
    Print print;
    Slic3r::Test::init_print({mesh}, print, model, flat_area_config(min_flat_area_mm2));

    Orchestrator &orchestrator = Orchestrator::instance();
    Plugin *plugin = orchestrator.get_plugin("flat_area_layer_height");
    REQUIRE(plugin != nullptr);

    plugin_host_context host_context = orchestrator.prepare_plugin_host_context(STEP_LAYER_HEIGHT, plugin, &print);
    plugin_run_context run_context = orchestrator.prepare_plugin_run_context(STEP_LAYER_HEIGHT, plugin, &host_context);
    plugin->setup(run_context, 1);

    std::unique_ptr<ApiHost::Steps::LayerHeightRunContext> layer_context =
        ApiHost::Steps::make_layer_height_run_context(print, 0);
    run_context.data = &layer_context->context_step;
    plugin->setup_run(run_context);
    plugin->run(run_context);

    return print.object(0).layer_profile();
}

std::vector<coord_t> profile_change_zs(const std::vector<coord_t> &profile)
{
    std::vector<coord_t> zs;
    zs.reserve(profile.size() / 2);
    for (size_t idx = 0; idx + 1 < profile.size(); idx += 2)
        zs.push_back(profile[idx]);
    return zs;
}

bool contains_z(const std::vector<coord_t> &zs, const double z_mm)
{
    const coord_t expected = scale_i(z_mm);
    const coord_t tolerance = scale_i(0.005);
    for (coord_t z : zs)
        if (std::abs(z - expected) <= tolerance)
            return true;
    return false;
}

} // namespace

TEST_CASE("FlatAreaLayerHeight filters small flat cube surfaces", "[plugins][flat-area-layer-height]")
{
    // With a zero threshold, the plugin should expose layer-height anchors on
    // the horizontal faces of every cube above the first layer. Raising the
    // threshold to 50 mm2 should keep the 10x10 and 8x8 cube faces, while
    // removing the 3x3 and 2x2 cube faces.
    const TriangleMesh mesh = make_flat_area_test_mesh();

    const std::vector<coord_t> all_flat_area_zs =
        profile_change_zs(run_flat_area_layer_height_profile(mesh, 0.));
    REQUIRE(contains_z(all_flat_area_zs, 2.3));
    REQUIRE(contains_z(all_flat_area_zs, 2.6));
    REQUIRE(contains_z(all_flat_area_zs, 4.2));
    REQUIRE(contains_z(all_flat_area_zs, 4.5));
    REQUIRE(contains_z(all_flat_area_zs, 6.1));
    REQUIRE(contains_z(all_flat_area_zs, 6.4));
    REQUIRE(contains_z(all_flat_area_zs, 8.1));

    const std::vector<coord_t> filtered_flat_area_zs =
        profile_change_zs(run_flat_area_layer_height_profile(mesh, 50.));
    REQUIRE(contains_z(filtered_flat_area_zs, 2.3));
    REQUIRE(contains_z(filtered_flat_area_zs, 2.6));
    REQUIRE_FALSE(contains_z(filtered_flat_area_zs, 4.2));
    REQUIRE_FALSE(contains_z(filtered_flat_area_zs, 4.5));
    REQUIRE(contains_z(filtered_flat_area_zs, 6.1));
    REQUIRE(contains_z(filtered_flat_area_zs, 6.4));
    REQUIRE_FALSE(contains_z(filtered_flat_area_zs, 8.1));
}

TEST_CASE("FlatAreaLayerHeight does not add flat-area anchors without internal flat polygons", "[plugins][flat-area-layer-height]")
{
    // This contrasts the multi-cube mesh with a single continuous cube. The
    // regular layer-height generator may still create intermediate layers, but
    // it should not create the exact Z anchors produced by horizontal cube
    // faces when no such internal flat polygons exist.
    const TriangleMesh no_flat_polygons_mesh = make_no_internal_flat_area_test_mesh();
    const std::vector<coord_t> z_without_flat_polygons =
        profile_change_zs(run_flat_area_layer_height_profile(no_flat_polygons_mesh, 0.));

    REQUIRE_FALSE(contains_z(z_without_flat_polygons, 2.3));
    REQUIRE_FALSE(contains_z(z_without_flat_polygons, 2.6));
    REQUIRE_FALSE(contains_z(z_without_flat_polygons, 4.2));
    REQUIRE_FALSE(contains_z(z_without_flat_polygons, 4.5));
    REQUIRE_FALSE(contains_z(z_without_flat_polygons, 6.1));
    REQUIRE_FALSE(contains_z(z_without_flat_polygons, 6.4));
    REQUIRE_FALSE(contains_z(z_without_flat_polygons, 8.1));
}
