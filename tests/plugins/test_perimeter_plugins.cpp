#include <catch2/catch.hpp>

#include "plugin_test_helpers.hpp"
#include "test_data.hpp"

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/Api/internal/LayerAccess.hpp"
#include "libslic3r/Api/internal/LayerRegionAccess.hpp"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_perimeter.h"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/PrintRegion.hpp"
#include "libslic3r/Steps/StepLayerHeightGeneration.hpp"
#include "libslic3r/Steps/StepPostSlicing.hpp"
#include "libslic3r/Steps/StepSlicing.hpp"
#include "libslic3r/SurfaceCollection.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace Slic3r;

const char *const SIMPLE_PERIMETER_GENERATOR = "perimeter.generator.simple";
const char *const EXTRA_PERIMETER_COUNT = "perimeter.module.extra_perimeter_count";
const char *const EXTRA_PERIMETER_BELOW_AREA = "perimeter.module.extra_perimeter_below_area";
const char *const EXTRA_PERIMETER_ODD_LAYER = "perimeter.module.extra_perimeter_odd_layer";
const char *const ONLY_ONE_PERIMETER_ON_TOP = "perimeter.module.only_one_perimeter_on_top";
const char *const SEPARATE_HOLE_CONTOUR = "perimeter.module.separate_hole_contour";
const char *const REMOVE_GAP_FILL_ON_OVERHANGS = "perimeter.module.remove_gap_fill_on_overhangs";

struct PerimeterRunCapture;
thread_local PerimeterRunCapture *g_perimeter_run_capture = nullptr;

struct Box
{
    double min_x = 0.;
    double min_y = 0.;
    double min_z = 0.;
    double max_x = 0.;
    double max_y = 0.;
    double max_z = 0.;
};

struct PerimeterRunCapture
{
    ExtrusionEntityCollection external_perimeters;
    SurfaceCollection fill_surfaces;
    SurfaceCollection fill_no_overlap_surfaces;
};

struct PreparedPerimeterPrint
{
    Model model;
    Print print;
    std::vector<std::unique_ptr<PrintRegion>> extra_regions;
};

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

TriangleMesh make_box(const Box &box)
{
    std::vector<Vec3f> vertices = {
        {float(box.min_x), float(box.min_y), float(box.min_z)},
        {float(box.max_x), float(box.min_y), float(box.min_z)},
        {float(box.max_x), float(box.max_y), float(box.min_z)},
        {float(box.min_x), float(box.max_y), float(box.min_z)},
        {float(box.min_x), float(box.min_y), float(box.max_z)},
        {float(box.max_x), float(box.min_y), float(box.max_z)},
        {float(box.max_x), float(box.max_y), float(box.max_z)},
        {float(box.min_x), float(box.max_y), float(box.max_z)}
    };
    std::vector<Vec3i32> faces = {
        {0, 2, 1}, {0, 3, 2},
        {4, 5, 6}, {4, 6, 7},
        {0, 1, 5}, {0, 5, 4},
        {1, 2, 6}, {1, 6, 5},
        {2, 3, 7}, {2, 7, 6},
        {3, 0, 4}, {3, 4, 7}
    };
    return TriangleMesh(std::move(vertices), std::move(faces));
}

ExPolygon rectangle_expolygon(const double min_x, const double min_y, const double max_x, const double max_y)
{
    return ExPolygon(Polygon({
        Point(scale_i(min_x), scale_i(min_y)),
        Point(scale_i(max_x), scale_i(min_y)),
        Point(scale_i(max_x), scale_i(max_y)),
        Point(scale_i(min_x), scale_i(max_y))
    }));
}

ExPolygon rectangle_with_hole_expolygon()
{
    ExPolygon out = rectangle_expolygon(-10., -10., 10., 10.);
    out.holes.push_back(Polygon({
        Point(scale_i(-3.), scale_i(-3.)),
        Point(scale_i(-3.), scale_i(3.)),
        Point(scale_i(3.), scale_i(3.)),
        Point(scale_i(3.), scale_i(-3.))
    }));
    return out;
}

DynamicPrintConfig perimeter_config(std::initializer_list<std::pair<std::string, std::string>> overrides)
{
    Slic3r::Test::Plugins::ensure_plugin_test_runtime_initialized();

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"layer_height", "1"},
        {"first_layer_height", "1"},
        {"nozzle_diameter", "0.4"},
        {"perimeters", "1"},
        {"extra_perimeters_count", "0"},
        {"extra_perimeters_below_area", "0"},
        {"extra_perimeters_odd_layers", "0"},
        {"only_one_perimeter_top", "0"},
        {"perimeters_hole", "!0"},
        {"gap_fill_no_overhang", "0"},
        {"gap_fill_enabled", "1"},
        {"fill_density", "15%"}
    });
    for (const std::pair<std::string, std::string> &entry : overrides)
        config.set_deserialize_strict(entry.first, entry.second);
    return config;
}

void run_until_perimeter_input(Orchestrator &orchestrator, Print &print)
{
    Steps::StepLayerHeightGeneration::run_step(orchestrator, print);
    Steps::StepSlicing::run_step(orchestrator, print);
    Steps::StepPostSlicing::run_step(orchestrator, print);
}

void prepare_cube_print(PreparedPerimeterPrint &prepared, const DynamicPrintConfig &config)
{
    const TriangleMesh cube = make_box({-10., -10., 0., 10., 10., 10.});
    Slic3r::Test::init_print({cube}, prepared.print, prepared.model, config);
    run_until_perimeter_input(Orchestrator::instance(), prepared.print);
}

void set_region_surface(LayerRegion &region, const ExPolygon &surface)
{
    ExPolygons surfaces;
    surfaces.push_back(surface);
    ApiInternal::LayerRegionAccess::slices_mutable(region) = surfaces;
    ApiInternal::LayerRegionAccess::surfaces_mutable(region).set(surfaces, stPosInternal | stDensSolid);
}

void replace_layer_island(Layer &layer, const ExPolygon &surface)
{
    ExPolygons islands;
    islands.push_back(surface);
    ApiInternal::LayerAccess::set_islands(layer, std::move(islands));
    set_region_surface(layer.region(0), surface);
    layer.island(0).fill_regions(layer);
}

void add_overlapping_region(PreparedPerimeterPrint &prepared,
                            Layer &layer,
                            const ExPolygon &surface,
                            const std::string &key,
                            const std::string &value)
{
    PrintRegionConfig config = layer.region(0).region().config();
    config.set_deserialize_strict({{key, value}});
    prepared.extra_regions.push_back(std::make_unique<PrintRegion>(config));
    ApiInternal::LayerAccess::add_region(layer, *prepared.extra_regions.back());

    LayerRegion &region = layer.region(layer.region_count() - 1);
    set_region_surface(region, surface);
    layer.island(0).fill_regions(layer);
}

size_t layer_index_for_top(const PrintObject &object)
{
    REQUIRE(object.layer_count() > 0);
    return object.layer_count() - 1;
}

size_t layer_index_for_odd_layer(const PrintObject &object)
{
    REQUIRE(object.layer_count() > 1);
    return 1;
}

ExtrusionRole role_from_raw(const raw_extrusion_role role)
{
    if (role == RAW_EXTRUSION_ROLE_EXTERNAL_PERIMETER)
        return ExtrusionRole::ExternalPerimeter;
    if (role == RAW_EXTRUSION_ROLE_GAP_FILL)
        return ExtrusionRole::GapFill;
    return ExtrusionRole::Mixed;
}

layer_region_island_handle *test_get_or_create_region_island(
    const layer_island_handle *island_handle,
    const layer_region_handle *const *region_handles,
    uint32_t region_count)
{
    LayerSliceIsland &island =
        *const_cast<LayerSliceIsland *>(reinterpret_cast<const LayerSliceIsland *>(island_handle));
    LayerRegionSetCPtrs regions;
    for (uint32_t idx = 0; idx < region_count; ++idx)
        regions.insert(reinterpret_cast<const LayerRegion *>(region_handles[idx]));
    return reinterpret_cast<layer_region_island_handle *>(&island.get_or_add_region_island(regions));
}

int32_t test_set_region_island_extrusion(layer_region_island_handle *region_island_handle,
                                         raw_extrusion_role role,
                                         extrusion_entity_handle *extrusion_handle)
{
    LayerRegionIsland &region_island =
        *reinterpret_cast<LayerRegionIsland *>(region_island_handle);
    ExtrusionEntityCollection &dst = region_island.mutable_extrusion(role_from_raw(role));
    dst.clear();
    if (extrusion_handle != nullptr) {
        ExtrusionEntity &src = *reinterpret_cast<ExtrusionEntity *>(extrusion_handle);
        if (ExtrusionEntityCollection *collection = dynamic_cast<ExtrusionEntityCollection *>(&src))
            dst.append_move_from(*collection);
        else
            dst.append(std::move(src));
    }
    if (g_perimeter_run_capture != nullptr && role == RAW_EXTRUSION_ROLE_EXTERNAL_PERIMETER)
        g_perimeter_run_capture->external_perimeters = dst;
    return 1;
}

int32_t test_set_region_island_fill_surfaces(layer_region_island_handle *,
                                             surface_collection_handle *surfaces_handle)
{
    REQUIRE(g_perimeter_run_capture != nullptr);
    g_perimeter_run_capture->fill_surfaces.clear();
    if (surfaces_handle != nullptr)
        g_perimeter_run_capture->fill_surfaces.set(
            *reinterpret_cast<SurfaceCollection *>(surfaces_handle));
    return 1;
}

int32_t test_set_region_island_fill_no_overlap_surfaces(layer_region_island_handle *,
                                                        surface_collection_handle *surfaces_handle)
{
    REQUIRE(g_perimeter_run_capture != nullptr);
    g_perimeter_run_capture->fill_no_overlap_surfaces.clear();
    if (surfaces_handle != nullptr)
        g_perimeter_run_capture->fill_no_overlap_surfaces.set(
            *reinterpret_cast<SurfaceCollection *>(surfaces_handle));
    return 1;
}

PerimeterRunCapture run_active_perimeter_plugins(Print &print, Layer &layer, LayerSliceIsland &island)
{
    Orchestrator &orchestrator = Orchestrator::instance();
    PerimeterRunCapture capture;
    g_perimeter_run_capture = &capture;
    const std::vector<Plugin *> plugins = orchestrator.get_active_plugins_for_step(STEP_PERIMETER);
    for (Plugin *plugin : plugins) {
        plugin_host_context host_context =
            orchestrator.prepare_plugin_host_context(STEP_PERIMETER, plugin, &print);
        plugin_run_context run_context =
            orchestrator.prepare_plugin_run_context(STEP_PERIMETER, plugin, &host_context);
        run_ctx_generate_perimeter payload = {};
        payload.print = reinterpret_cast<const print_handle *>(&print);
        payload.object = reinterpret_cast<const object_handle *>(&print.object(0));
        payload.layer = reinterpret_cast<const layer_handle *>(&layer);
        payload.island = reinterpret_cast<const layer_island_handle *>(&island);
        payload.get_or_create_region_island = &test_get_or_create_region_island;
        payload.set_region_island_extrusion = &test_set_region_island_extrusion;
        payload.set_region_island_fill_surfaces = &test_set_region_island_fill_surfaces;
        payload.set_region_island_fill_no_overlap_surfaces = &test_set_region_island_fill_no_overlap_surfaces;
        run_context.data = &payload;

        plugin->setup(run_context, 1);
        plugin->setup_run(run_context);
        plugin->run(run_context);
    }
    g_perimeter_run_capture = nullptr;
    return capture;
}

PerimeterRunCapture run_perimeter_case(const DynamicPrintConfig &config,
                                       std::initializer_list<const char *> active_plugins,
                                       const ExPolygon &surface,
                                       const size_t layer_idx,
                                       std::initializer_list<std::pair<std::string, std::string>> overlap_overrides = {})
{
    PreparedPerimeterPrint prepared;
    prepare_cube_print(prepared, config);
    PrintObject &object = prepared.print.object(0);
    REQUIRE(layer_idx < object.layer_count());
    Layer &layer = object.layer(layer_idx);
    replace_layer_island(layer, surface);

    if (overlap_overrides.size() > 0) {
        const ExPolygon overlap = rectangle_expolygon(-6., -6., 6., 6.);
        for (const std::pair<std::string, std::string> &entry : overlap_overrides)
            add_overlapping_region(prepared, layer, overlap, entry.first, entry.second);
    }

    ScopedActivePlugins active_scope(active_plugins);
    return run_active_perimeter_plugins(prepared.print, layer, layer.island(0));
}

size_t count_leaf_extrusions(const ExtrusionEntity &entity)
{
    if (entity.is_nop())
        return 0;
    if (entity.is_leaf())
        return entity.has_polyline() ? 1 : 0;

    size_t count = 0;
    for (size_t child_idx = 0; child_idx < entity.child_count(); ++child_idx)
        count += count_leaf_extrusions(entity.child(child_idx));
    return count;
}

double extrusion_length(const ExtrusionEntity &entity)
{
    if (entity.is_nop())
        return 0.;
    if (entity.is_leaf())
        return entity.has_polyline() ? entity.length() : 0.;

    double length = 0.;
    for (size_t child_idx = 0; child_idx < entity.child_count(); ++child_idx)
        length += extrusion_length(entity.child(child_idx));
    return length;
}

size_t count_loops_with_role(const ExtrusionEntity &entity, const ExtrusionLoopRole role_mask)
{
    if (const ExtrusionLoop *loop = dynamic_cast<const ExtrusionLoop *>(&entity))
        return (loop->loop_role() & role_mask) != 0 ? 1 : 0;

    size_t count = 0;
    for (size_t child_idx = 0; child_idx < entity.child_count(); ++child_idx)
        count += count_loops_with_role(entity.child(child_idx), role_mask);
    return count;
}

const ExtrusionEntityCollection &external_perimeters(const PerimeterRunCapture &capture)
{
    return capture.external_perimeters;
}

size_t external_perimeter_count(const PerimeterRunCapture &capture)
{
    return count_leaf_extrusions(capture.external_perimeters);
}

struct TestPerimeterNode
{
    TestPerimeterNode *parent = nullptr;
    ExPolygon surface;
    ExPolygon fill_surface;
    ExtrusionEntityCollection extrusions;
    std::vector<std::unique_ptr<TestPerimeterNode>> children;
    std::vector<perimeter_node *> c_children;
    perimeter_node c_node = {};

    void refresh_c_node()
    {
        c_children.clear();
        c_children.reserve(children.size());
        for (std::unique_ptr<TestPerimeterNode> &child : children) {
            child->parent = this;
            child->refresh_c_node();
            c_children.push_back(&child->c_node);
        }

        c_node.parent = parent == nullptr ? nullptr : &parent->c_node;
        c_node.surface = reinterpret_cast<expolygon_handle *>(&surface);
        c_node.fill_surface = reinterpret_cast<expolygon_handle *>(&fill_surface);
        c_node.extrusions = reinterpret_cast<extrusion_entity_handle *>(&extrusions);
        c_node.children = c_children.empty() ? nullptr : c_children.data();
        c_node.child_count = uint32_t(c_children.size());
    }
};

TestPerimeterNode *find_test_node(TestPerimeterNode &node, perimeter_node *c_node)
{
    if (&node.c_node == c_node)
        return &node;
    for (std::unique_ptr<TestPerimeterNode> &child : node.children) {
        TestPerimeterNode *found = find_test_node(*child, c_node);
        if (found != nullptr)
            return found;
    }
    return nullptr;
}

void test_split_node(perimeter_generation_context *context,
                     perimeter_node *node,
                     const expolygon_collection_handle *clip_handle,
                     perimeter_node_span *inside_nodes_out)
{
    REQUIRE(context != nullptr);
    REQUIRE(context->generator_context != nullptr);
    REQUIRE(inside_nodes_out != nullptr);
    TestPerimeterNode &root = *reinterpret_cast<TestPerimeterNode *>(context->generator_context);
    TestPerimeterNode *test_node = find_test_node(root, node);
    REQUIRE(test_node != nullptr);

    if (clip_handle == nullptr) {
        root.c_children = { &test_node->c_node };
        inside_nodes_out->items = root.c_children.data();
        inside_nodes_out->count = 1;
        return;
    }

    const ExPolygons &clip = *reinterpret_cast<const ExPolygons *>(clip_handle);
    ExPolygons inside = intersection_ex(ExPolygons{test_node->surface}, clip);
    ExPolygons outside = diff_ex(ExPolygons{test_node->surface}, clip);

    test_node->children.clear();
    root.c_children.clear();
    for (ExPolygon &surface : inside) {
        std::unique_ptr<TestPerimeterNode> child(new TestPerimeterNode);
        child->surface = std::move(surface);
        child->fill_surface = child->surface;
        child->c_node.perimeter_idx = node->perimeter_idx;
        child->c_node.perimeter_needed = node->perimeter_needed;
        test_node->children.push_back(std::move(child));
        root.c_children.push_back(&test_node->children.back()->c_node);
    }
    for (ExPolygon &surface : outside) {
        std::unique_ptr<TestPerimeterNode> child(new TestPerimeterNode);
        child->surface = std::move(surface);
        child->fill_surface = child->surface;
        child->c_node.perimeter_idx = node->perimeter_idx;
        child->c_node.perimeter_needed = node->perimeter_needed;
        test_node->children.push_back(std::move(child));
    }
    root.refresh_c_node();

    inside_nodes_out->items = root.c_children.empty() ? nullptr : root.c_children.data();
    inside_nodes_out->count = uint32_t(root.c_children.size());
}

ExtrusionPath open_gap_fill_path()
{
    ExtrusionPath path(ExtrusionAttributes(ExtrusionRole::GapFill, ExtrusionFlow(0.1, 0.4f, 0.2f)), nullptr, true);
    path.polyline().append(Point(scale_i(-8.), 0));
    path.polyline().append(Point(scale_i(8.), 0));
    return path;
}

perimeter_generation_module_instance create_module_instance(const char *plugin_id,
                                                            Print &print,
                                                            plugin_run_context &run_context,
                                                            plugin_host_context &host_context,
                                                            run_ctx_perimeter_generation_module &payload)
{
    Orchestrator &orchestrator = Orchestrator::instance();
    Plugin *plugin = orchestrator.get_plugin(plugin_id);
    REQUIRE(plugin != nullptr);
    host_context = orchestrator.prepare_plugin_host_context(PERIMETER_GENERATION_MODULE, plugin, &print);
    run_context = orchestrator.prepare_plugin_run_context(PERIMETER_GENERATION_MODULE, plugin, &host_context);
    payload = {};
    run_context.data = &payload;
    plugin->setup(run_context, 1);
    plugin->setup_run(run_context);
    plugin->run(run_context);
    REQUIRE(payload.module.vt != nullptr);
    return payload.module;
}

size_t run_remove_gap_fill_module(const DynamicPrintConfig &config,
                                  const bool use_overlap_region,
                                  double *length_out = nullptr)
{
    PreparedPerimeterPrint prepared;
    prepare_cube_print(prepared, config);
    PrintObject &object = prepared.print.object(0);
    Layer &layer = object.layer(0);
    const ExPolygon surface = rectangle_expolygon(-10., -10., 10., 10.);
    replace_layer_island(layer, surface);
    if (use_overlap_region)
        add_overlapping_region(prepared, layer, rectangle_expolygon(-1., -10., 10., 10.), "gap_fill_no_overhang", "1");

    TestPerimeterNode root;
    root.surface = surface;
    root.fill_surface = surface;
    root.extrusions.append(open_gap_fill_path());
    root.c_node.perimeter_idx = 0;
    root.c_node.perimeter_needed = 1;
    root.refresh_c_node();

    plugin_host_context host_context = {};
    plugin_run_context run_context = {};
    run_ctx_perimeter_generation_module payload = {};
    perimeter_generation_module_instance module =
        create_module_instance(REMOVE_GAP_FILL_ON_OVERHANGS, prepared.print, run_context, host_context, payload);

    perimeter_generation_context context = {};
    context.run_ctx = &run_context;
    context.print = reinterpret_cast<const print_handle *>(&prepared.print);
    context.object = reinterpret_cast<const object_handle *>(&object);
    context.layer = reinterpret_cast<const layer_handle *>(&layer);
    context.island = reinterpret_cast<const layer_island_handle *>(&layer.island(0));
    context.root = &root.c_node;
    context.generator_context = &root;
    context.split_node = &test_split_node;

    module.vt->after(module.ctx, &context, &root.c_node);
    if (length_out != nullptr)
        *length_out = extrusion_length(root.extrusions);
    return count_leaf_extrusions(root.extrusions);
}

} // namespace

TEST_CASE("SimplePerimeterGenerator publishes perimeter and fill output", "[plugins][perimeter]")
{
    // This is the smoke test for the full STEP_PERIMETER payload. With no
    // STEP_PERIMETER plugin active, nothing should be published. With the
    // simple generator active, it should create at least one external loop and
    // both fill-surface collections for the island.
    const DynamicPrintConfig config = perimeter_config({});
    const ExPolygon surface = rectangle_expolygon(-10., -10., 10., 10.);

    const PerimeterRunCapture inactive =
        run_perimeter_case(config, {}, surface, 0);
    REQUIRE(external_perimeter_count(inactive) == 0);

    const PerimeterRunCapture generated =
        run_perimeter_case(config, {SIMPLE_PERIMETER_GENERATOR}, surface, 0);
    REQUIRE(external_perimeter_count(generated) > 0);
    REQUIRE_FALSE(generated.fill_surfaces.empty());
    REQUIRE_FALSE(generated.fill_no_overlap_surfaces.empty());
}

TEST_CASE("Extra perimeter count module adds requested loops", "[plugins][perimeter]")
{
    // The simple no/yes check uses one uniform region. The overlap check adds a
    // second region covering the center of the island and verifies that the
    // module can request extra perimeters only where that region applies.
    const ExPolygon surface = rectangle_expolygon(-10., -10., 10., 10.);
    const DynamicPrintConfig disabled = perimeter_config({{"extra_perimeters_count", "0"}});
    const DynamicPrintConfig enabled = perimeter_config({{"extra_perimeters_count", "2"}});

    const size_t base_count = external_perimeter_count(
        run_perimeter_case(disabled, {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT}, surface, 0));
    const size_t enabled_count = external_perimeter_count(
        run_perimeter_case(enabled, {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT}, surface, 0));
    REQUIRE(enabled_count > base_count);

    const size_t overlap_count = external_perimeter_count(
        run_perimeter_case(disabled,
                           {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT},
                           surface,
                           0,
                           {{"extra_perimeters_count", "1"}}));
    REQUIRE(overlap_count > base_count);
    REQUIRE(overlap_count < enabled_count);
}

TEST_CASE("Extra perimeter below area module extends small islands", "[plugins][perimeter]")
{
    // extra_perimeters_below_area only acts after the first ring, when child
    // inner surfaces exist. A very large threshold forces the branch to keep
    // generating perimeters until the surface disappears. The overlap case
    // limits that force to a smaller central region.
    const ExPolygon surface = rectangle_expolygon(-10., -10., 10., 10.);
    const DynamicPrintConfig disabled = perimeter_config({{"extra_perimeters_below_area", "0"}});
    const DynamicPrintConfig enabled = perimeter_config({{"extra_perimeters_below_area", "100000"}});

    const size_t base_count = external_perimeter_count(
        run_perimeter_case(disabled, {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_BELOW_AREA}, surface, 0));
    const size_t enabled_count = external_perimeter_count(
        run_perimeter_case(enabled, {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_BELOW_AREA}, surface, 0));
    REQUIRE(enabled_count > base_count);

    const size_t overlap_count = external_perimeter_count(
        run_perimeter_case(disabled,
                           {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_BELOW_AREA},
                           surface,
                           0,
                           {{"extra_perimeters_below_area", "100000"}}));
    REQUIRE(overlap_count > base_count);
    REQUIRE(overlap_count < enabled_count);
}

TEST_CASE("Extra perimeter odd layer module is layer-parity dependent", "[plugins][perimeter]")
{
    // This module must not fire on even layer ids. On odd layer ids it adds one
    // perimeter. With overlapping region settings, only the region-local child
    // branch receives that extra pass.
    const ExPolygon surface = rectangle_expolygon(-10., -10., 10., 10.);
    const DynamicPrintConfig disabled = perimeter_config({{"extra_perimeters_odd_layers", "0"}});
    const DynamicPrintConfig enabled = perimeter_config({{"extra_perimeters_odd_layers", "1"}});

    const size_t even_count = external_perimeter_count(
        run_perimeter_case(enabled, {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_ODD_LAYER}, surface, 0));

    PreparedPerimeterPrint prepared;
    prepare_cube_print(prepared, enabled);
    const size_t odd_idx = layer_index_for_odd_layer(prepared.print.object(0));
    const size_t odd_count = external_perimeter_count(
        run_perimeter_case(enabled, {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_ODD_LAYER}, surface, odd_idx));
    REQUIRE(odd_count > even_count);

    const size_t overlap_count = external_perimeter_count(
        run_perimeter_case(disabled,
                           {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_ODD_LAYER},
                           surface,
                           odd_idx,
                           {{"extra_perimeters_odd_layers", "1"}}));
    REQUIRE(overlap_count > even_count);
    REQUIRE(overlap_count != odd_count);
}

TEST_CASE("Only one perimeter on top limits top branches", "[plugins][perimeter]")
{
    // The simple generator does not read the normal perimeters setting yet, so
    // ExtraPerimeterCount first creates a multi-perimeter baseline. The
    // OnlyOne module then proves that it can stop a top layer at one perimeter,
    // globally and inside an overlapping enabled region.
    const ExPolygon surface = rectangle_expolygon(-10., -10., 10., 10.);
    const DynamicPrintConfig multi = perimeter_config({
        {"extra_perimeters_count", "3"},
        {"only_one_perimeter_top", "0"}
    });
    const DynamicPrintConfig limited = perimeter_config({
        {"extra_perimeters_count", "3"},
        {"only_one_perimeter_top", "1"}
    });

    PreparedPerimeterPrint prepared;
    prepare_cube_print(prepared, multi);
    const size_t top_idx = layer_index_for_top(prepared.print.object(0));

    const size_t multi_count = external_perimeter_count(
        run_perimeter_case(multi,
                           {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT, ONLY_ONE_PERIMETER_ON_TOP},
                           surface,
                           top_idx));
    const size_t limited_count = external_perimeter_count(
        run_perimeter_case(limited,
                           {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT, ONLY_ONE_PERIMETER_ON_TOP},
                           surface,
                           top_idx));
    REQUIRE(limited_count < multi_count);
    REQUIRE(limited_count > 0);

    const size_t overlap_count = external_perimeter_count(
        run_perimeter_case(multi,
                           {SIMPLE_PERIMETER_GENERATOR, EXTRA_PERIMETER_COUNT, ONLY_ONE_PERIMETER_ON_TOP},
                           surface,
                           top_idx,
                           {{"only_one_perimeter_top", "1"}}));
    REQUIRE(overlap_count < multi_count);
    REQUIRE(overlap_count >= limited_count);
}

TEST_CASE("Separate hole contour module limits hole loops", "[plugins][perimeter]")
{
    // The input island has one contour and one hole. When perimeters_hole is
    // disabled, the module is inert. When enabled with fewer hole perimeters
    // than contour perimeters, hole loops should stop before contour loops.
    const ExPolygon surface = rectangle_with_hole_expolygon();
    const DynamicPrintConfig disabled = perimeter_config({
        {"perimeters", "3"},
        {"perimeters_hole", "!0"}
    });
    const DynamicPrintConfig enabled = perimeter_config({
        {"perimeters", "3"},
        {"perimeters_hole", "1"}
    });

    const PerimeterRunCapture disabled_run =
        run_perimeter_case(disabled, {SIMPLE_PERIMETER_GENERATOR, SEPARATE_HOLE_CONTOUR}, surface, 0);
    const PerimeterRunCapture enabled_run =
        run_perimeter_case(enabled, {SIMPLE_PERIMETER_GENERATOR, SEPARATE_HOLE_CONTOUR}, surface, 0);

    REQUIRE(external_perimeter_count(enabled_run) > external_perimeter_count(disabled_run));
    REQUIRE(count_loops_with_role(external_perimeters(enabled_run), elrHole) <
            count_loops_with_role(external_perimeters(enabled_run), elrDefault));

    const PerimeterRunCapture overlap_run =
        run_perimeter_case(disabled,
                           {SIMPLE_PERIMETER_GENERATOR, SEPARATE_HOLE_CONTOUR},
                           surface,
                           0,
                           {{"perimeters_hole", "1"}});
    REQUIRE(external_perimeter_count(overlap_run) == external_perimeter_count(disabled_run));
}

TEST_CASE("Remove gap fill on overhangs clips unsupported open paths", "[plugins][perimeter]")
{
    // This module edits open gap-fill paths, but the temporary simple perimeter
    // generator only emits loops. The test calls the module directly with a
    // synthetic node containing one open gap-fill path. With the setting off it
    // stays unchanged. With the setting on and no lower island, the unsupported
    // path is removed. With an overlapping enabled region, only the forbidden
    // side of the path is clipped.
    const DynamicPrintConfig disabled = perimeter_config({{"gap_fill_no_overhang", "0"}});
    const DynamicPrintConfig enabled = perimeter_config({{"gap_fill_no_overhang", "1"}});

    double disabled_length = 0.;
    const size_t disabled_count = run_remove_gap_fill_module(disabled, false, &disabled_length);
    REQUIRE(disabled_count == 1);
    REQUIRE(disabled_length > 0.);

    double enabled_length = 0.;
    const size_t enabled_count = run_remove_gap_fill_module(enabled, false, &enabled_length);
    REQUIRE(enabled_count == 0);
    REQUIRE(enabled_length == 0.);

    double overlap_length = 0.;
    const size_t overlap_count = run_remove_gap_fill_module(disabled, true, &overlap_length);
    REQUIRE(overlap_count > 0);
    REQUIRE(overlap_length > 0.);
    REQUIRE(overlap_length < disabled_length);
}
