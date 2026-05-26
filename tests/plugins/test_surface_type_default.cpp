#include <catch2/catch.hpp>

#include "perimeter_test_helpers.hpp"
#include "plugin_test_helpers.hpp"

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/Api/internal/LayerAccess.hpp"
#include "libslic3r/Api/internal/LayerRegionAccess.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/Steps/StepDetectSurfaceType.hpp"
#include "libslic3r/Steps/StepGeneratePerimeter.hpp"
#include "libslic3r/Steps/StepSurfaceGeneration.hpp"
#include "libslic3r/Surface.hpp"

#include <initializer_list>
#include <vector>

namespace {
using namespace Slic3r;
using namespace Slic3r::Test::PerimeterPluginTests;

class ScopedActivePlugins
{
public:
    // Test-local activation guard: the global plugin runtime is shared, while
    // each section needs to select exactly the plugins under test.
    explicit ScopedActivePlugins(std::initializer_list<const char *> plugin_ids) :
        m_orchestrator(Orchestrator::instance())
    {
        m_previous_active_plugins.reserve(m_orchestrator.active_plugins().size());
        for (Plugin *plugin : m_orchestrator.active_plugins())
            m_previous_active_plugins.push_back(plugin);

        m_orchestrator.clear_active_plugins();
        for (const char *plugin_id : plugin_ids) {
            INFO("Activating test plugin " << plugin_id);
            REQUIRE(m_orchestrator.set_plugin_active(plugin_id, true));
        }
    }

    ~ScopedActivePlugins()
    {
        m_orchestrator.clear_active_plugins();
        for (Plugin *plugin : m_previous_active_plugins)
            m_orchestrator.set_plugin_active(plugin, true);
    }

private:
    Orchestrator &m_orchestrator;
    std::vector<Plugin *> m_previous_active_plugins;
};

struct SurfaceTypeCounts
{
    size_t total = 0;
    size_t top = 0;
    size_t bottom = 0;
    size_t internal = 0;
    size_t sparse = 0;
    size_t solid = 0;
};

// Small aggregate used by assertions: the exact number of split surfaces is
// algorithm-dependent, but these family counts prove which classification
// modules have run.
SurfaceTypeCounts count_surface_types(const PrintObject &object)
{
    SurfaceTypeCounts out;
    for (const Layer &layer : object.layers())
        for (const LayerRegion &region : layer.regions())
            for (const Surface &surface : region.fill_surfaces()) {
                ++out.total;
                if (surface.has_pos_top())
                    ++out.top;
                if (surface.has_pos_bottom())
                    ++out.bottom;
                if (surface.has_pos_internal())
                    ++out.internal;
                if (surface.has_fill_sparse())
                    ++out.sparse;
                if (surface.has_fill_solid())
                    ++out.solid;
            }
    return out;
}

// Replace a LayerRegion's raw slice and its pre-plugin SurfaceCollection with
// one known sparse/internal polygon. Tests use this to avoid depending on the
// exact mesh slicer output.
void set_region_area(LayerRegion &region, const ExPolygon &area)
{
    ExPolygons &region_slices = ApiInternal::LayerRegionAccess::slices_mutable(region);
    region_slices = ExPolygons{area};
    ApiInternal::LayerRegionAccess::surfaces_mutable(region).set(region_slices, stPosInternal | stDensSparse);
}

// Build a one-island/one-region layer. The perimeter and surface-generation
// plugins then operate on deterministic geometry instead of the cube's native
// slice topology.
void replace_layer_island(Layer &layer, const ExPolygon &area)
{
    ApiInternal::LayerAccess::set_islands(layer, ExPolygons{area});
    set_region_area(layer.region(0), area);
    layer.island(0).fill_regions(layer);
}

// LayerIsland above/below links are consumed by top/bottom classification.
// Rebuild them after the tests replace layer islands by hand.
void rebuild_island_overlap_graph(PrintObject &object)
{
    for (Layer &layer : object.layers())
        for (LayerSliceIsland &island : layer.islands()) {
            island.overlaps_above.clear();
            island.overlaps_below.clear();
        }

    for (size_t layer_idx = 1; layer_idx < object.layer_count(); ++layer_idx)
        Layer::build_up_down_graph(object.layer(layer_idx - 1), object.layer(layer_idx));
}

// Give every layer the same island footprint. This creates a simple vertical
// stack where the first layer is bottom, the last layer is top, and middle
// layers are internal.
void replace_all_layers_with_test_island(PrintObject &object)
{
    const ExPolygon island_area = rectangle_expolygon(-10., -10., 10., 10.);
    for (Layer &layer : object.layers())
        replace_layer_island(layer, island_area);
    rebuild_island_overlap_graph(object);
}

// Run only the steps that prepare raw fill surfaces. STEP_SURFACE_TYPE is kept
// separate so tests can assert the before/after contract.
void run_perimeter_and_surface_generation(Print &print)
{
    Orchestrator &orchestrator = Orchestrator::instance();
    Steps::StepGeneratePerimeter::clean_and_prepare(print);
    Steps::StepGeneratePerimeter::run_step(orchestrator, print);
    Steps::StepSurfaceGeneration::clean_and_prepare(print);
    Steps::StepSurfaceGeneration::run_step(orchestrator, print);
}

// Execute the selected surface type plugin. In these tests it is either the
// default native-compatible plugin or intentionally absent.
void run_surface_type_detection(Print &print)
{
    Orchestrator &orchestrator = Orchestrator::instance();
    Steps::StepDetectSurfaceType::clean_and_prepare(print);
    Steps::StepDetectSurfaceType::run_step(orchestrator, print);
}

} // namespace

TEST_CASE("Default surface type plugin classifies generated fill surfaces",
          "[plugins][surface-type]")
{
    Slic3r::Test::Plugins::ensure_plugin_test_runtime_initialized();

    SECTION("raw sparse areas become top bottom and internal surfaces")
    {
        // The default surface generator deliberately outputs raw fill areas
        // with the source slice type. The type plugin is the step that runs
        // the native prepare-infill sequence and turns those areas into the
        // top/bottom/internal surface types consumed by infill generation.
        PreparedPerimeterPrint prepared;
        prepare_cube_print(prepared, perimeter_config({
            {"bottom_solid_layers", "1"},
            {"top_solid_layers", "1"},
            {"ensure_vertical_shell_thickness", "disabled"}
        }));
        PrintObject &object = prepared.print.object(0);
        replace_all_layers_with_test_island(object);

        ScopedActivePlugins active({SIMPLE_PERIMETER_GENERATOR,
                                    DEFAULT_SURFACE_GENERATOR,
                                    DEFAULT_SURFACE_TYPE});
        run_perimeter_and_surface_generation(prepared.print);

        SurfaceTypeCounts before = count_surface_types(object);
        REQUIRE(before.total > 0);
        REQUIRE(before.top == 0);
        REQUIRE(before.bottom == 0);
        REQUIRE(before.internal > 0);

        run_surface_type_detection(prepared.print);

        SurfaceTypeCounts after = count_surface_types(object);
        CHECK(after.top > 0);
        CHECK(after.bottom > 0);
        CHECK(after.solid > 0);
        CHECK(after.internal > 0);
        CHECK(object.is_step_done(posPrepareInfill));
    }

    SECTION("missing active surface type plugin leaves raw sparse surfaces untouched")
    {
        // STEP_SURFACE_TYPE is exclusive. If no plugin owns it, the step must
        // not silently run the old native code; this keeps plugin activation
        // and tests honest. The generated surfaces remain raw internal areas.
        PreparedPerimeterPrint prepared;
        prepare_cube_print(prepared, perimeter_config({
            {"bottom_solid_layers", "1"},
            {"top_solid_layers", "1"},
            {"ensure_vertical_shell_thickness", "disabled"}
        }));
        PrintObject &object = prepared.print.object(0);
        replace_all_layers_with_test_island(object);

        ScopedActivePlugins active({SIMPLE_PERIMETER_GENERATOR,
                                    DEFAULT_SURFACE_GENERATOR});
        run_perimeter_and_surface_generation(prepared.print);
        run_surface_type_detection(prepared.print);

        SurfaceTypeCounts counts = count_surface_types(object);
        CHECK(counts.total > 0);
        CHECK(counts.top == 0);
        CHECK(counts.bottom == 0);
        CHECK(counts.internal > 0);
        CHECK_FALSE(object.is_step_done(posPrepareInfill));
    }

    SECTION("zero top solid layers disables top surfaces")
    {
        // prepare_fill_surfaces() is now one named internal module. This case
        // proves it still runs after classification: detect_surfaces_type()
        // would create top surfaces on the last layer, then the zero top layer
        // setting must turn them back into non-top fill.
        PreparedPerimeterPrint prepared;
        prepare_cube_print(prepared, perimeter_config({
            {"bottom_solid_layers", "1"},
            {"top_solid_layers", "0"},
            {"ensure_vertical_shell_thickness", "disabled"}
        }));
        PrintObject &object = prepared.print.object(0);
        replace_all_layers_with_test_island(object);

        ScopedActivePlugins active({SIMPLE_PERIMETER_GENERATOR,
                                    DEFAULT_SURFACE_GENERATOR,
                                    DEFAULT_SURFACE_TYPE});
        run_perimeter_and_surface_generation(prepared.print);
        run_surface_type_detection(prepared.print);

        SurfaceTypeCounts counts = count_surface_types(object);
        CHECK(counts.top == 0);
        CHECK(counts.bottom > 0);
        CHECK(counts.internal > 0);
    }

    SECTION("zero bottom solid layers disables bottom surfaces")
    {
        // Same module boundary as above, but for the bottom layer side. This
        // catches accidental extraction that classifies top/bottom correctly
        // but forgets to apply region fill preparation afterward.
        PreparedPerimeterPrint prepared;
        prepare_cube_print(prepared, perimeter_config({
            {"bottom_solid_layers", "0"},
            {"top_solid_layers", "1"},
            {"ensure_vertical_shell_thickness", "disabled"}
        }));
        PrintObject &object = prepared.print.object(0);
        replace_all_layers_with_test_island(object);

        ScopedActivePlugins active({SIMPLE_PERIMETER_GENERATOR,
                                    DEFAULT_SURFACE_GENERATOR,
                                    DEFAULT_SURFACE_TYPE});
        run_perimeter_and_surface_generation(prepared.print);
        run_surface_type_detection(prepared.print);

        SurfaceTypeCounts counts = count_surface_types(object);
        CHECK(counts.top > 0);
        CHECK(counts.bottom == 0);
        CHECK(counts.internal > 0);
    }
}
