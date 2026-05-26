#include <catch2/catch.hpp>

#include "perimeter_test_helpers.hpp"
#include "plugin_test_helpers.hpp"

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/Api/internal/LayerAccess.hpp"
#include "libslic3r/Api/internal/LayerIslandAccess.hpp"
#include "libslic3r/Api/internal/LayerRegionAccess.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/Steps/StepGeneratePerimeter.hpp"
#include "libslic3r/Steps/StepSurfaceGeneration.hpp"

#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace {
using namespace Slic3r;
using namespace Slic3r::Test::PerimeterPluginTests;

class ScopedActivePlugins
{
public:
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

double area_sum(const ExPolygons &areas)
{
    double out = 0.;
    for (const ExPolygon &area : areas)
        out += std::abs(area.area());
    return out;
}

double area_tolerance()
{
    const double side = double(scale_i(0.005));
    return side * side;
}

ExPolygons surface_expolygons(const SurfaceCollection &surfaces)
{
    ExPolygons out;
    out.reserve(surfaces.size());
    for (const Surface &surface : surfaces)
        if (!surface.empty())
            out.push_back(surface.expolygon);
    return out;
}

void require_same_union(const ExPolygons &actual, const ExPolygons &expected)
{
    const ExPolygons actual_union = union_ex(actual);
    const ExPolygons expected_union = union_ex(expected);
    INFO("actual area " << area_sum(actual_union) << ", expected area " << area_sum(expected_union));
    REQUIRE(area_sum(diff_ex(actual_union, expected_union)) <= area_tolerance());
    REQUIRE(area_sum(diff_ex(expected_union, actual_union)) <= area_tolerance());
}

void require_subset(const ExPolygons &child, const ExPolygons &parent)
{
    REQUIRE(area_sum(diff_ex(union_ex(child), union_ex(parent))) <= area_tolerance());
}

void set_region_areas(LayerRegion &region, ExPolygons areas)
{
    ExPolygons &region_slices = ApiInternal::LayerRegionAccess::slices_mutable(region);
    region_slices = std::move(areas);
    ApiInternal::LayerRegionAccess::surfaces_mutable(region).set(region_slices, stPosInternal | stDensSparse);
}

void set_region_area(LayerRegion &region, const ExPolygon &area)
{
    set_region_areas(region, ExPolygons{area});
}

void replace_layer_island(Layer &layer, const ExPolygon &area)
{
    ApiInternal::LayerAccess::set_islands(layer, ExPolygons{area});
    set_region_area(layer.region(0), area);
    layer.island(0).fill_regions(layer);
}

void add_region(PreparedPerimeterPrint &prepared, Layer &layer, const ExPolygon &area)
{
    PrintRegionConfig config = layer.region(0).region().config();
    prepared.extra_regions.push_back(std::make_unique<PrintRegion>(config));
    ApiInternal::LayerAccess::add_region(layer, *prepared.extra_regions.back());
    set_region_area(layer.region(layer.region_count() - 1), area);
}

void replace_layer_with_two_regions(PreparedPerimeterPrint &prepared,
                                    Layer &layer,
                                    const ExPolygon &island_area,
                                    const ExPolygon &first_region,
                                    const ExPolygon &second_region)
{
    ApiInternal::LayerAccess::set_islands(layer, ExPolygons{island_area});
    set_region_area(layer.region(0), first_region);
    add_region(prepared, layer, second_region);
    layer.island(0).fill_regions(layer);
}

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

void run_perimeter_and_surface_steps(Print &print)
{
    Orchestrator &orchestrator = Orchestrator::instance();
    Steps::StepGeneratePerimeter::clean_and_prepare(print);
    Steps::StepGeneratePerimeter::run_step(orchestrator, print);
    Steps::StepSurfaceGeneration::clean_and_prepare(print);
    Steps::StepSurfaceGeneration::run_step(orchestrator, print);
}

ExPolygons expected_fill_for_region(const Layer &layer, const LayerRegion &region)
{
    ExPolygons all_fill_expolygons;
    for (const LayerSliceIsland &island : layer.islands())
        append(all_fill_expolygons, island.fill_expolygons());
    return intersection_ex(region.get_raw_slices(), all_fill_expolygons);
}

ExPolygons expected_no_overlap_for_region(const Layer &layer, const LayerRegion &region)
{
    ExPolygons all_fill_no_overlap_expolygons;
    for (const LayerSliceIsland &island : layer.islands()) {
        if (island.fill_no_overlap_expolygons().empty())
            append(all_fill_no_overlap_expolygons, island.fill_expolygons());
        else
            append(all_fill_no_overlap_expolygons, island.fill_no_overlap_expolygons());
    }

    all_fill_no_overlap_expolygons = union_safety_offset_ex(all_fill_no_overlap_expolygons);
    return intersection_ex(region.get_raw_slices(), all_fill_no_overlap_expolygons);
}

void require_sparse_internal_surfaces(const SurfaceCollection &surfaces)
{
    REQUIRE_FALSE(surfaces.empty());
    for (const Surface &surface : surfaces)
        CHECK(surface.surface_type == (stPosInternal | stDensSparse));
}

} // namespace

TEST_CASE("Default surface generator creates raw fill surfaces from perimeter fill areas",
          "[plugins][surface-generation]")
{
    Slic3r::Test::Plugins::ensure_plugin_test_runtime_initialized();

    SECTION("one region and one island")
    {
        // A single island with one region is the basic contract: perimeter
        // generation writes island fill areas, then surface generation clips
        // those areas back into the region's raw slice as sparse internal fill.
        PreparedPerimeterPrint prepared;
        prepare_cube_print(prepared, perimeter_config({{"perimeters", "1"}}));
        PrintObject &object = prepared.print.object(0);
        Layer &layer = object.layer(0);
        const ExPolygon island_area = rectangle_expolygon(-10., -10., 10., 10.);
        replace_layer_island(layer, island_area);
        rebuild_island_overlap_graph(object);

        ScopedActivePlugins active({SIMPLE_PERIMETER_GENERATOR, DEFAULT_SURFACE_GENERATOR});
        run_perimeter_and_surface_steps(prepared.print);

        const LayerRegion &region = layer.region(0);
        require_sparse_internal_surfaces(region.fill_surfaces());
        require_same_union(surface_expolygons(region.fill_surfaces()), expected_fill_for_region(layer, region));
        require_same_union(region.fill_no_overlap_expolygons(), expected_no_overlap_for_region(layer, region));
    }

    SECTION("one island split by two non-overlapping regions")
    {
        // The island is generated once, but each LayerRegion must receive only
        // the part of the island fill area that intersects its own raw slice.
        // This catches accidental layer-level assignment that ignores regions.
        PreparedPerimeterPrint prepared;
        prepare_cube_print(prepared, perimeter_config({{"perimeters", "1"}}));
        PrintObject &object = prepared.print.object(0);
        Layer &layer = object.layer(0);
        const ExPolygon island_area = rectangle_expolygon(-10., -10., 10., 10.);
        const ExPolygon left_region = rectangle_expolygon(-10., -10., 0., 10.);
        const ExPolygon right_region = rectangle_expolygon(0., -10., 10., 10.);
        replace_layer_with_two_regions(prepared, layer, island_area, left_region, right_region);
        rebuild_island_overlap_graph(object);

        ScopedActivePlugins active({SIMPLE_PERIMETER_GENERATOR, DEFAULT_SURFACE_GENERATOR});
        run_perimeter_and_surface_steps(prepared.print);

        const LayerRegion &left = layer.region(0);
        const LayerRegion &right = layer.region(1);

        require_sparse_internal_surfaces(left.fill_surfaces());
        require_sparse_internal_surfaces(right.fill_surfaces());
        require_same_union(surface_expolygons(left.fill_surfaces()), expected_fill_for_region(layer, left));
        require_same_union(surface_expolygons(right.fill_surfaces()), expected_fill_for_region(layer, right));
        ExPolygons combined_fill = surface_expolygons(left.fill_surfaces());
        append(combined_fill, surface_expolygons(right.fill_surfaces()));
        require_same_union(combined_fill, layer.island(0).fill_expolygons());
    }

    SECTION("no-overlap area stays inside the larger fill area")
    {
        // The simple perimeter generator publishes two related areas per leaf:
        // a larger fill/anchor area and a stricter no-overlap area. Surface
        // generation must preserve that distinction on the region.
        PreparedPerimeterPrint prepared;
        prepare_cube_print(prepared, perimeter_config({{"perimeters", "1"}}));
        PrintObject &object = prepared.print.object(0);
        Layer &layer = object.layer(0);
        const ExPolygon island_area = rectangle_expolygon(-10., -10., 10., 10.);
        replace_layer_island(layer, island_area);
        rebuild_island_overlap_graph(object);

        ScopedActivePlugins active({SIMPLE_PERIMETER_GENERATOR, DEFAULT_SURFACE_GENERATOR});
        run_perimeter_and_surface_steps(prepared.print);

        const ExPolygons fill = surface_expolygons(layer.region(0).fill_surfaces());
        const ExPolygons no_overlap = layer.region(0).fill_no_overlap_expolygons();
        require_subset(no_overlap, fill);
        CHECK(area_sum(union_ex(no_overlap)) < area_sum(union_ex(fill)));
    }

    SECTION("zero requested perimeters keeps the whole island as fill")
    {
        // With perimeters=0, STEP_PERIMETER still publishes an island fill area
        // equal to the original island. Surface generation should therefore
        // create sparse fill over the full raw region slice.
        PreparedPerimeterPrint prepared;
        prepare_cube_print(prepared, perimeter_config({{"perimeters", "0"}}));
        PrintObject &object = prepared.print.object(0);
        Layer &layer = object.layer(0);
        const ExPolygon island_area = rectangle_expolygon(-10., -10., 10., 10.);
        replace_layer_island(layer, island_area);
        rebuild_island_overlap_graph(object);

        ScopedActivePlugins active({SIMPLE_PERIMETER_GENERATOR, DEFAULT_SURFACE_GENERATOR});
        run_perimeter_and_surface_steps(prepared.print);

        require_sparse_internal_surfaces(layer.region(0).fill_surfaces());
        require_same_union(surface_expolygons(layer.region(0).fill_surfaces()), ExPolygons{island_area});
        require_same_union(layer.region(0).fill_no_overlap_expolygons(), ExPolygons{island_area});
    }

    SECTION("missing perimeter output leaves no fill surfaces")
    {
        // If no perimeter generator ran, there are no island fill areas to
        // convert. The surface step should clear any old fill surfaces and
        // leave the region empty instead of fabricating infill from raw slices.
        PreparedPerimeterPrint prepared;
        prepare_cube_print(prepared, perimeter_config({{"perimeters", "1"}}));
        PrintObject &object = prepared.print.object(0);
        Layer &layer = object.layer(0);
        const ExPolygon island_area = rectangle_expolygon(-10., -10., 10., 10.);
        replace_layer_island(layer, island_area);
        layer.region(0).set_fill_surfaces().set(ExPolygons{island_area}, stPosInternal | stDensSparse);
        rebuild_island_overlap_graph(object);

        ScopedActivePlugins active({DEFAULT_SURFACE_GENERATOR});
        run_perimeter_and_surface_steps(prepared.print);

        CHECK(layer.region(0).fill_surfaces().empty());
        CHECK(layer.region(0).fill_no_overlap_expolygons().empty());
    }
}
