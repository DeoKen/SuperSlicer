
//#define CATCH_CONFIG_DISABLE
#include <catch2/catch.hpp>

#include "test_data.hpp"
#include <libslic3r/libslic3r.h>
#include <libslic3r/Layer.hpp>
#include <libslic3r/SVG.hpp>
#include <libslic3r/Format/3mf.hpp>
#include <libslic3r/Api/host/Orchestrator.hpp>
#include <libslic3r/Plugins/MaxOverhangThreshold.hpp>
#include <libslic3r/Plugins/Polyholes.hpp>
#include <libslic3r/Plugins/SliceVolume.hpp>
#include <libslic3r/Plugins/StandardLayerHeightGenerator.hpp>
#include <libslic3r/Steps/StepPipeline.hpp>
//#include <libslic3r/config.hpp>
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

bool region_island_contains_region(const LayerRegionIsland &region_island, const LayerRegion *region)
{
    return region_island.regions().find(region) != region_island.regions().end();
}

size_t perimeter_region_island_count(const Layer &layer, const LayerRegion &region)
{
    size_t count = 0;
    for (const LayerSliceIslandPtr &island : layer.islands())
        for (const LayerRegionIslandPtr &region_island : island->regions_islands())
            if (region_island_contains_region(*region_island, &region) &&
                region_island->has_extrusion(LayerRegionIsland::PERIMETERS))
                ++count;
    return count;
}

size_t perimeter_item_count(const Layer &layer, const LayerRegion &region)
{
    size_t count = 0;
    for (const LayerSliceIslandPtr &island : layer.islands())
        for (const LayerRegionIslandPtr &region_island : island->regions_islands())
            if (region_island_contains_region(*region_island, &region) &&
                region_island->has_extrusion(LayerRegionIsland::PERIMETERS))
                count += region_island->extrusion(LayerRegionIsland::PERIMETERS).items_count();
    return count;
}

} // namespace

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
            PrintObject &object = *print.get_object(0);
            //THEN("67 layers exist in the model") 
            { REQUIRE(object.layers().size() == 67); }
            //THEN("Every layer in region 0 has 1 island of perimeters")
            {
                for (Layer *layer : object.layers()) {
                    REQUIRE(perimeter_region_island_count(*layer, *layer->regions()[0]) == 1);
                }
            }
            //THEN("Every layer (but top) in region 0 has 3 paths in its perimeters list.")
            {
                LayerPtrs layers = object.layers();
                for (auto it_layer = layers.begin(); it_layer != layers.end() - 1; ++it_layer) {
                    REQUIRE(perimeter_item_count(**it_layer, *(*it_layer)->regions()[0]) == 3);
                }
            }
            //THEN("Top layer in region 0 has 1 path in its perimeters list (only 1 perimeter on top).")
            {
                REQUIRE(perimeter_item_count(*object.layers().back(), *object.layers().back()->regions()[0]) == 1);
            }
            REQUIRE_FALSE(object.has_raft());
        }
    }
}

