
//#define CATCH_CONFIG_DISABLE
#include <catch2/catch.hpp>

#include "test_data.hpp"
#include <libslic3r/libslic3r.h>
#include <libslic3r/Layer.hpp>
#include <libslic3r/LayerRegion.hpp>
#include <libslic3r/PrintObject.hpp>
#include <libslic3r/SVG.hpp>
#include <libslic3r/Format/3mf.hpp>
#include <libslic3r/Api/host/Orchestrator.hpp>
#include <libslic3r/Api/plugin/c/slic3r_orchestrator.h>
#include <libslic3r/Plugins/MaxOverhangThreshold.hpp>
#include <libslic3r/Plugins/Polyholes.hpp>
#include <libslic3r/Plugins/SliceVolume.hpp>
#include <libslic3r/Plugins/StandardLayerHeightGenerator.hpp>
#include <libslic3r/Steps/StepPipeline.hpp>
//#include <libslic3r/config.hpp>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

using namespace Slic3r;
using namespace Slic3r::Test;
using namespace std::literals;

namespace {

void register_step_pipeline_plugins()
{
    static bool registered = []() {
        orchestrator_handle *orchestrator = reinterpret_cast<orchestrator_handle *>(&Orchestrator::instance());
        slic3r_api::StandardLayerHeightGeneratorPlugin::register_standard_layer_height_generator_plugin(orchestrator);
        slic3r_api::SliceVolumePlugin::register_slice_volume_plugin(orchestrator);
        slic3r_api::PolyholesPlugin::register_polyholes_plugin(orchestrator);
        slic3r_api::MaxOverhangThresholdPlugin::register_max_overhang_threshold_plugin(orchestrator);
        return true;
    }();
    (void)registered;
}


std::string read_text_file(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

bool region_island_contains_region(const LayerRegionIsland &region_island, const LayerRegion *region)
{
    return region_island.regions().find(region) != region_island.regions().end();
}

size_t perimeter_region_island_count(const Layer &layer, const LayerRegion &region)
{
    size_t count = 0;
    for (const LayerSliceIsland &island : layer.islands())
        for (const LayerRegionIsland &region_island : island.regions_islands())
            if (region_island_contains_region(region_island, &region) &&
                region_island.has_extrusion(LayerRegionIsland::PERIMETERS))
                ++count;
    return count;
}

size_t perimeter_item_count(const Layer &layer, const LayerRegion &region)
{
    size_t count = 0;
    for (const LayerSliceIsland &island : layer.islands())
        for (const LayerRegionIsland &region_island : island.regions_islands())
            if (region_island_contains_region(region_island, &region) &&
                region_island.has_extrusion(LayerRegionIsland::PERIMETERS))
                count += region_island.extrusion(LayerRegionIsland::PERIMETERS).items_count();
    return count;
}

} // namespace

TEST_CASE("Plugin UI fragment rebuilds the original print layout", "[Api][UiLayout]")
{
    Orchestrator &orchestrator = Orchestrator::instance();
    orchestrator_handle *orch_handle = reinterpret_cast<orchestrator_handle *>(&orchestrator);
    const int32_t polyholes_added = orchestrator_add_ui_fragment(orch_handle,
                                                                 "print.ui",
                                                                 "polyholes",
                                                                 slic3r_api::PolyholesPlugin::Polyholes::print_ui_fragment(),
                                                                 0);
    REQUIRE((polyholes_added == 0 || polyholes_added == 1));
    const int32_t overhang_added = orchestrator_add_ui_fragment(orch_handle,
                                                               "print.ui",
                                                               "max_overhang_threshold",
                                                               slic3r_api::MaxOverhangThresholdPlugin::MaxOverhangThreshold::print_ui_fragment(),
                                                               0);
    REQUIRE((overhang_added == 0 || overhang_added == 1));

    const std::string base = read_text_file(std::string(TEST_DATA_DIR) + "/../../resources/ui_layout/default/print.ui");
    const std::string expected = read_text_file(std::string(TEST_DATA_DIR) + "/ui_layout/print_with_builtin_overhang_threshold.ui");

    const std::string merged = orchestrator.merged_ui_layout("print.ui", base);
    if (merged != expected) {
        const size_t diff_pos = std::mismatch(merged.begin(), merged.end(), expected.begin(), expected.end()).first - merged.begin();
        CAPTURE(diff_pos);
        CAPTURE(merged.substr(diff_pos, 160));
        CAPTURE(expected.substr(diff_pos, 160));
    }
    REQUIRE(merged == expected);
}

SCENARIO("PrintObject: Perimeter generation") {
    GIVEN("20mm cube and default config & 0.3 layer height") {
        DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
        TestMesh mesh = TestMesh::cube_20x20x20;
        Model model{};
        config.set_key_value("fill_density", new ConfigOptionPercent(0));
        config.set_deserialize("nozzle_diameter", "0.4");
        config.set_deserialize("layer_height", "0.3");

        WHEN("make_perimeters() is called") {
            Print print{};
            register_step_pipeline_plugins();
            Slic3r::Test::init_print({mesh}, print, model, config);
#ifdef _DEBUG
            Slic3r::Steps::StepPipeline::debug_run(Orchestrator::instance(), print, STEP_PRE_PERIMETER);
#endif
            print.process();
            PrintObject &object = print.object(0);
            //THEN("67 layers exist in the model") 
            { REQUIRE(object.layer_count() == 67); }
            //THEN("Every layer in region 0 has 1 island of perimeters")
            {
                for (Layer &layer : object.layers()) {
                    REQUIRE(perimeter_region_island_count(layer, layer.region(0)) == 1);
                }
            }
            //THEN("Every layer (but top) in region 0 has 3 paths in its perimeters list.")
            {
                for (size_t layer_idx = 0; layer_idx + 1 < object.layer_count(); ++layer_idx) {
                    Layer &layer = object.layer(layer_idx);
                    REQUIRE(perimeter_item_count(layer, layer.region(0)) == 3);
                }
            }
            //THEN("Top layer in region 0 has 1 path in its perimeters list (only 1 perimeter on top).")
            {
                Layer &top_layer = object.layer(object.layer_count() - 1);
                REQUIRE(perimeter_item_count(top_layer, top_layer.region(0)) == 1);
            }
            REQUIRE_FALSE(object.has_raft());
        }
    }
}

