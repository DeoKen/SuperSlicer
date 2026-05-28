#include "perimeter_test_helpers.hpp"
#include "plugin_test_helpers.hpp"

#include <catch2/catch.hpp>

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/ExtrusionEntity.hpp"

namespace Slic3r::Test::PerimeterPluginTests {
namespace {

const char *k_hairy_object_plugin = "python.perimeter.post_process.hairy_object";
const char *k_hairy_object_painting_key = "perimeter.post_process.hairy_object.painting";

size_t count_open_two_point_thin_walls(const ExtrusionEntity &entity)
{
    const ExtrusionAttributes *attributes = entity.get_property<ExtrusionAttributes>();
    const ArcPolyline *polyline = entity.polyline_or_null();
    if (attributes != nullptr && polyline != nullptr && polyline->size() == 2 &&
        polyline->front() != polyline->back() && attributes->role == ExtrusionRole::ThinWall)
        return 1;

    size_t count = 0;
    for (size_t child_idx = 0; child_idx < entity.child_count(); ++child_idx)
        count += count_open_two_point_thin_walls(entity.child(child_idx));
    return count;
}

void collect_open_two_point_thin_wall_starts(const ExtrusionEntity &entity, Points &out)
{
    const ExtrusionAttributes *attributes = entity.get_property<ExtrusionAttributes>();
    const ArcPolyline *polyline = entity.polyline_or_null();
    if (attributes != nullptr && polyline != nullptr && polyline->size() == 2 &&
        polyline->front() != polyline->back() && attributes->role == ExtrusionRole::ThinWall) {
        out.push_back(polyline->front());
        return;
    }

    for (size_t child_idx = 0; child_idx < entity.child_count(); ++child_idx)
        collect_open_two_point_thin_wall_starts(entity.child(child_idx), out);
}

} // namespace

TEST_CASE("Python HairyObject creates painted thin-wall hairs", "[plugins][perimeter][python][hairy]")
{
    REQUIRE(Slic3r::Test::Plugins::python_plugin_test_runtime_available());
    REQUIRE(Orchestrator::instance().is_plugin_active(k_hairy_object_plugin));

    // The fixture cube has eight vertical side facets. Painting them all should
    // cover the contour of every middle layer. HairyObject then clips the island
    // contour by that projected paint and appends one open, two-point thin-wall
    // extrusion for each sampled hair. The density is now a wall-area density:
    // the middle fixture layers are about 0.3 mm high, so 2 hair/mm2 selects
    // every third layer and keeps contour spacing near the square-cell spacing.
    const std::vector<int> side_facets = {4, 5, 6, 7, 8, 9, 10, 11};
    const ExPolygon area = rectangle_expolygon(-10., -10., 10., 10.);
    const size_t layer_idx = 3;
    const DynamicPrintConfig config = perimeter_config({
        {"hairy_object_hair_length", "1.5"},
        {"hairy_object_hair_density", "2.0"}
    });

    const PerimeterRunCapture baseline =
        run_perimeter_case(config, {SIMPLE_PERIMETER_GENERATOR}, area, layer_idx);
    const PerimeterRunCapture hairy =
        run_perimeter_and_post_case_with_generic_facet_painting(
            config,
            {SIMPLE_PERIMETER_GENERATOR},
            {k_hairy_object_plugin},
            area,
            layer_idx,
            {{k_hairy_object_painting_key, EnforcerBlockerType::ENFORCER, side_facets}});

    REQUIRE(external_perimeter_count(baseline) == 1);
    REQUIRE(count_open_two_point_thin_walls(hairy.external_perimeters) > 0);
    REQUIRE(external_perimeter_count(hairy) > external_perimeter_count(baseline));
}

TEST_CASE("Python HairyObject spreads low density across layers", "[plugins][perimeter][python][hairy]")
{
    REQUIRE(Slic3r::Test::Plugins::python_plugin_test_runtime_available());
    REQUIRE(Orchestrator::instance().is_plugin_active(k_hairy_object_plugin));

    // At 0.04 hair/mm2, the ideal square-cell spacing is 5 mm. The middle
    // fixture layers are about 0.3 mm high, so the plugin should put hair on
    // roughly every seventeenth layer and use about 5 mm contour spacing on
    // those layers, instead of placing hairs on every layer with extremely
    // sparse contour spacing.
    const std::vector<int> side_facets = {4, 5, 6, 7, 8, 9, 10, 11};
    const ExPolygon area = rectangle_expolygon(-10., -10., 10., 10.);
    const DynamicPrintConfig config = perimeter_config({
        {"hairy_object_hair_length", "1.5"},
        {"hairy_object_hair_density", "0.04"}
    });

    const PerimeterRunCapture skipped_layer =
        run_perimeter_and_post_case_with_generic_facet_painting(
            config,
            {SIMPLE_PERIMETER_GENERATOR},
            {k_hairy_object_plugin},
            area,
            1,
            {{k_hairy_object_painting_key, EnforcerBlockerType::ENFORCER, side_facets}});
    const PerimeterRunCapture sampled_layer =
        run_perimeter_and_post_case_with_generic_facet_painting(
            config,
            {SIMPLE_PERIMETER_GENERATOR},
            {k_hairy_object_plugin},
            area,
            17,
            {{k_hairy_object_painting_key, EnforcerBlockerType::ENFORCER, side_facets}});

    CHECK(count_open_two_point_thin_walls(skipped_layer.external_perimeters) == 0);
    CHECK(count_open_two_point_thin_walls(sampled_layer.external_perimeters) > 0);
}

TEST_CASE("Python HairyObject staggers hair roots between layers", "[plugins][perimeter][python][hairy]")
{
    REQUIRE(Slic3r::Test::Plugins::python_plugin_test_runtime_available());
    REQUIRE(Orchestrator::instance().is_plugin_active(k_hairy_object_plugin));

    // The start position is intentionally offset with a deterministic
    // low-discrepancy sequence. The same density as the basic test samples
    // every third middle layer, so layers 3 and 6 should both contain hairs
    // but start at different points on the contour.
    const std::vector<int> side_facets = {4, 5, 6, 7, 8, 9, 10, 11};
    const ExPolygon area = rectangle_expolygon(-10., -10., 10., 10.);
    const DynamicPrintConfig config = perimeter_config({
        {"hairy_object_hair_length", "1.5"},
        {"hairy_object_hair_density", "2.0"}
    });

    const PerimeterRunCapture first_layer =
        run_perimeter_and_post_case_with_generic_facet_painting(
            config,
            {SIMPLE_PERIMETER_GENERATOR},
            {k_hairy_object_plugin},
            area,
            3,
            {{k_hairy_object_painting_key, EnforcerBlockerType::ENFORCER, side_facets}});
    const PerimeterRunCapture next_layer =
        run_perimeter_and_post_case_with_generic_facet_painting(
            config,
            {SIMPLE_PERIMETER_GENERATOR},
            {k_hairy_object_plugin},
            area,
            6,
            {{k_hairy_object_painting_key, EnforcerBlockerType::ENFORCER, side_facets}});

    Points first_starts;
    Points next_starts;
    collect_open_two_point_thin_wall_starts(first_layer.external_perimeters, first_starts);
    collect_open_two_point_thin_wall_starts(next_layer.external_perimeters, next_starts);

    REQUIRE(!first_starts.empty());
    REQUIRE(!next_starts.empty());
    CHECK(first_starts.front() != next_starts.front());
}

} // namespace Slic3r::Test::PerimeterPluginTests
