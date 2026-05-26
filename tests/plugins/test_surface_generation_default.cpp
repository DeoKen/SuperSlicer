#include <catch2/catch.hpp>

#include "perimeter_test_helpers.hpp"
#include "plugin_test_helpers.hpp"

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/Api/internal/LayerAccess.hpp"
#include "libslic3r/Api/internal/LayerRegionAccess.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/PrintRegion.hpp"
#include "libslic3r/Steps/StepGeneratePerimeter.hpp"
#include "libslic3r/Steps/StepSurfaceGeneration.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/SurfaceCollection.hpp"

#include <cmath>
#include <initializer_list>
#include <memory>
#include <string>
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

ExPolygons dumbbell_expolygon()
{
    ExPolygons parts;
    parts.push_back(rectangle_expolygon(-8., -4., -1., 4.));
    parts.push_back(rectangle_expolygon(-1., -0.15, 1., 0.15));
    parts.push_back(rectangle_expolygon(1., -4., 8., 4.));

    ExPolygons merged = union_ex(parts);
    REQUIRE(merged.size() == 1);
    return merged;
}

void require_same_union(const ExPolygons &actual, const ExPolygons &expected)
{
    const ExPolygons actual_union = union_ex(actual);
    const ExPolygons expected_union = union_ex(expected);
    INFO("actual area " << area_sum(actual_union) << ", expected area " << area_sum(expected_union));
    REQUIRE(area_sum(diff_ex(actual_union, expected_union)) <= area_tolerance());
    REQUIRE(area_sum(diff_ex(expected_union, actual_union)) <= area_tolerance());
}

void require_no_positive_overlap(const ExPolygons &areas)
{
    for (size_t first_idx = 0; first_idx < areas.size(); ++first_idx) {
        for (size_t second_idx = first_idx + 1; second_idx < areas.size(); ++second_idx) {
            const ExPolygons overlap = intersection_ex(
                ExPolygons{areas[first_idx]},
                ExPolygons{areas[second_idx]});
            INFO("area pair " << first_idx << " / " << second_idx);
            CHECK(area_sum(overlap) <= area_tolerance());
        }
    }
}

void require_no_positive_overlap(const SurfaceCollection &surfaces)
{
    require_no_positive_overlap(surface_expolygons(surfaces));
}

void require_sparse_internal_surfaces(const SurfaceCollection &surfaces)
{
    for (const Surface &surface : surfaces)
        CHECK(surface.surface_type == (stPosInternal | stDensSparse));
}

const LayerRegionIsland &whole_region_island(const LayerSliceIsland &island)
{
    const LayerRegionIsland *fallback = nullptr;
    for (const LayerRegionIsland &region_island : island.regions_islands()) {
        if (region_island.regions() != island.regions())
            continue;
        if (!region_island.fill_surfaces().empty())
            return region_island;
        if (fallback == nullptr)
            fallback = &region_island;
    }

    REQUIRE(fallback != nullptr);
    return *fallback;
}

LayerRegionSetCPtrs region_set(const LayerRegion &region)
{
    LayerRegionSetCPtrs out;
    out.insert(&region);
    return out;
}

const LayerRegionIsland &region_island_for(const LayerSliceIsland &island,
                                           const LayerRegionSetCPtrs &regions,
                                           const uint16_t extruder_id)
{
    const LayerRegionIsland *found = nullptr;
    for (const LayerRegionIsland &region_island : island.regions_islands())
        if (region_island.regions() == regions && region_island.extruder_id() == extruder_id) {
            found = &region_island;
            break;
        }

    REQUIRE(found != nullptr);
    return *found;
}

void require_surface_contract(const SurfaceCollection &surfaces, const ExPolygons &expected_areas)
{
    // The default generator is deliberately a thin conversion step: perimeter
    // generation owns the island fill areas, and this step creates exactly one
    // sparse internal Surface for each resulting ExPolygon.
    REQUIRE(surfaces.size() == expected_areas.size());
    require_sparse_internal_surfaces(surfaces);
    require_no_positive_overlap(surfaces);
    require_same_union(surface_expolygons(surfaces), expected_areas);
}

void require_surface_contract(const LayerSliceIsland &island)
{
    require_surface_contract(whole_region_island(island).fill_surfaces(), island.infill_areas());
}

void set_region_area(LayerRegion &region, const ExPolygon &area)
{
    ExPolygons &region_slices = ApiInternal::LayerRegionAccess::slices_mutable(region);
    region_slices = ExPolygons{area};
    ApiInternal::LayerRegionAccess::surfaces_mutable(region).set(region_slices, stPosInternal | stDensSparse);
}

void replace_layer_island(Layer &layer, const ExPolygon &area)
{
    ApiInternal::LayerAccess::set_islands(layer, ExPolygons{area});
    set_region_area(layer.region(0), area);
    layer.island(0).fill_regions(layer);
}

void add_region_with_infill_extruder(PreparedPerimeterPrint &prepared,
                                     Layer &layer,
                                     const ExPolygon &area,
                                     const int infill_extruder)
{
    PrintRegionConfig config = layer.region(0).region().config();
    config.set_deserialize_strict("infill_extruder", std::to_string(infill_extruder));
    prepared.extra_regions.push_back(std::make_unique<PrintRegion>(config));
    ApiInternal::LayerAccess::add_region(layer, *prepared.extra_regions.back());
    set_region_area(layer.region(layer.region_count() - 1), area);
}

void replace_layer_island_with_two_infill_extruders(PreparedPerimeterPrint &prepared,
                                                   Layer &layer,
                                                   const ExPolygon &area)
{
    ApiInternal::LayerAccess::set_islands(layer, ExPolygons{area});
    set_region_area(layer.region(0), rectangle_expolygon(-10., -10., 0., 10.));
    add_region_with_infill_extruder(prepared, layer, rectangle_expolygon(0., -10., 10., 10.), 2);
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

} // namespace

TEST_CASE("Default surface generator converts island infill areas to region-island surfaces",
          "[plugins][surface-generation]")
{
    Slic3r::Test::Plugins::ensure_plugin_test_runtime_initialized();

    SECTION("normal island")
    {
        // A normal rectangular island still has an inner fill area after one
        // perimeter. Surface generation must copy that island-level area into
        // the matching LayerRegionIsland without splitting or losing coverage.
        PreparedPerimeterPrint prepared;
        prepare_cube_print(prepared, perimeter_config({{"perimeters", "1"}}));
        PrintObject &object = prepared.print.object(0);
        Layer &layer = object.layer(0);
        replace_layer_island(layer, rectangle_expolygon(-10., -10., 10., 10.));
        rebuild_island_overlap_graph(object);

        ScopedActivePlugins active({SIMPLE_PERIMETER_GENERATOR, DEFAULT_SURFACE_GENERATOR});
        run_perimeter_and_surface_steps(prepared.print);

        const LayerSliceIsland &island = layer.island(0);
        REQUIRE_FALSE(island.infill_areas().empty());
        require_surface_contract(island);
    }

    SECTION("perimeters consumed the whole island")
    {
        // This very narrow island can receive a perimeter line, but its inner
        // area disappears after the perimeter offset. The surface step should
        // therefore keep the LayerRegionIsland present but with no fill
        // surfaces, instead of resurrecting the original island as infill.
        PreparedPerimeterPrint prepared;
        prepare_cube_print(prepared, perimeter_config({{"perimeters", "1"}}));
        PrintObject &object = prepared.print.object(0);
        Layer &layer = object.layer(0);
        replace_layer_island(layer, rectangle_expolygon(-0.1, -8., 0.1, 8.));
        rebuild_island_overlap_graph(object);

        ScopedActivePlugins active({SIMPLE_PERIMETER_GENERATOR, DEFAULT_SURFACE_GENERATOR});
        run_perimeter_and_surface_steps(prepared.print);

        const LayerSliceIsland &island = layer.island(0);
        CHECK(island.infill_areas().empty());
        require_surface_contract(island);
    }

    SECTION("one island split into multiple fill expolygons")
    {
        // The dumbbell starts as one island, but the thin neck is too narrow
        // to survive the inner perimeter offset. Perimeter generation publishes
        // multiple fill ExPolygons, and surface generation must preserve that
        // partition exactly: no overlaps and no missing area.
        PreparedPerimeterPrint prepared;
        prepare_cube_print(prepared, perimeter_config({{"perimeters", "1"}}));
        PrintObject &object = prepared.print.object(0);
        Layer &layer = object.layer(0);
        const ExPolygons island_area = dumbbell_expolygon();
        replace_layer_island(layer, island_area.front());
        rebuild_island_overlap_graph(object);

        ScopedActivePlugins active({SIMPLE_PERIMETER_GENERATOR, DEFAULT_SURFACE_GENERATOR});
        run_perimeter_and_surface_steps(prepared.print);

        const LayerSliceIsland &island = layer.island(0);
        REQUIRE(island.infill_areas().size() >= 2);
        require_surface_contract(island);
    }

    SECTION("one island with two infill extruders")
    {
        // The perimeter generator still works on the full island, but infill
        // surfaces are later consumed by an infill extruder. When two regions
        // inside the same island use different infill extruders, the surface
        // step must create one LayerRegionIsland per extruder and clip the
        // island fill areas back to the regions owned by that extruder.
        PreparedPerimeterPrint prepared;
        prepare_cube_print(prepared, perimeter_config({{"perimeters", "1"}, {"nozzle_diameter", "0.4,0.4"}}));
        PrintObject &object = prepared.print.object(0);
        Layer &layer = object.layer(0);
        const ExPolygon island_area = rectangle_expolygon(-10., -10., 10., 10.);
        replace_layer_island_with_two_infill_extruders(prepared, layer, island_area);
        rebuild_island_overlap_graph(object);

        ScopedActivePlugins active({SIMPLE_PERIMETER_GENERATOR, DEFAULT_SURFACE_GENERATOR});
        run_perimeter_and_surface_steps(prepared.print);

        const LayerSliceIsland &island = layer.island(0);
        REQUIRE_FALSE(island.infill_areas().empty());

        const LayerRegionIsland &left_infill =
            region_island_for(island, region_set(layer.region(0)), uint16_t(0));
        const LayerRegionIsland &right_infill =
            region_island_for(island, region_set(layer.region(1)), uint16_t(1));

        const ExPolygons expected_left = intersection_ex(island.infill_areas(), layer.region(0).get_raw_slices());
        const ExPolygons expected_right = intersection_ex(island.infill_areas(), layer.region(1).get_raw_slices());
        require_surface_contract(left_infill.fill_surfaces(), expected_left);
        require_surface_contract(right_infill.fill_surfaces(), expected_right);

        ExPolygons combined = surface_expolygons(left_infill.fill_surfaces());
        append(combined, surface_expolygons(right_infill.fill_surfaces()));
        require_no_positive_overlap(combined);
        require_same_union(combined, island.infill_areas());
    }

    SECTION("zero requested perimeters keeps the whole island as fill")
    {
        // With perimeters=0, perimeter generation does not consume the island.
        // The original island area is still the fill area, and the surface step
        // must convert it like any other perimeter output.
        PreparedPerimeterPrint prepared;
        prepare_cube_print(prepared, perimeter_config({{"perimeters", "0"}}));
        PrintObject &object = prepared.print.object(0);
        Layer &layer = object.layer(0);
        const ExPolygon island_area = rectangle_expolygon(-10., -10., 10., 10.);
        replace_layer_island(layer, island_area);
        rebuild_island_overlap_graph(object);

        ScopedActivePlugins active({SIMPLE_PERIMETER_GENERATOR, DEFAULT_SURFACE_GENERATOR});
        run_perimeter_and_surface_steps(prepared.print);

        const LayerSliceIsland &island = layer.island(0);
        require_surface_contract(island);
        require_same_union(surface_expolygons(whole_region_island(island).fill_surfaces()), ExPolygons{island_area});
    }
}
