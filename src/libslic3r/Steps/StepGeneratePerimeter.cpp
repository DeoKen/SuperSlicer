///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "StepGeneratePerimeter.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/Api/internal/LayerIslandAccess.hpp"
#include "libslic3r/Api/internal/LayerRegionAccess.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/PrintRegion.hpp"
#include "libslic3r/Steps/StepPipeline.hpp"
#include "libslic3r/SurfaceCollection.hpp"

namespace Slic3r::Steps::StepGeneratePerimeter {
namespace {

struct PerimeterTreeNode
{
    explicit PerimeterTreeNode(const ExPolygon &area)
        : area(area)
        , fill_area(area)
        , extrusions(true)
    {}

    // Canonical C ABI node. The C++ wrapper only owns the heavier objects
    // referenced by the handles below; scalar traversal state lives here.
    perimeter_node node = {};
    ExPolygon area;
    ExPolygon fill_area;
    ExtrusionEntity extrusions;
    std::vector<std::unique_ptr<PerimeterTreeNode>> children;
    std::vector<perimeter_node *> child_nodes;

    bool needs_more_perimeters() const
    {
        return node.perimeter_needed > 0 && node.perimeter_idx < node.perimeter_needed;
    }

    void sync_c_pointers()
    {
        child_nodes.clear();
        child_nodes.reserve(children.size());
        for (std::unique_ptr<PerimeterTreeNode> &child : children) {
            child->node.parent = &node;
            child->sync_c_pointers();
            child_nodes.push_back(&child->node);
        }

        node.area = reinterpret_cast<expolygon_handle *>(&area);
        node.fill_area = reinterpret_cast<expolygon_handle *>(&fill_area);
        node.extrusions = reinterpret_cast<extrusion_entity_handle *>(&extrusions);
        node.children = child_nodes.empty() ? nullptr : child_nodes.data();
        node.child_count = uint32_t(child_nodes.size());
    }
};

struct PerimeterTree
{
    explicit PerimeterTree(const ExPolygon &root_area, uint32_t perimeter_needed)
        : root(root_area)
    {
        root.node.perimeter_idx = 0;
        root.node.perimeter_needed = perimeter_needed;
        this->sync_c_pointers();
    }

    PerimeterTreeNode root;
    std::vector<perimeter_node *> last_span_nodes;

    void sync_c_pointers()
    {
        root.node.parent = nullptr;
        root.sync_c_pointers();
    }
};

struct PerimeterRunContext
{
    Orchestrator *orchestrator = nullptr;
    Plugin *generator_plugin = nullptr;
    plugin_run_context *generator_run_context = nullptr;
    Print *print = nullptr;
    PrintObject *object = nullptr;
    Layer *layer = nullptr;
    LayerSliceIsland *island = nullptr;
    ExPolygons fill_areas;
    ExPolygons fill_no_overlap_areas;
    ExPolygons perimeter_slices;
    std::vector<plugin_host_context> module_host_contexts;
    bool used_region_group = false;
};

struct PerimeterModuleRun
{
    perimeter_generation_module_instance module = {};
    void *user_context = nullptr;
    bool started = false;
};

LayerRegionIsland *to_region_island(layer_region_island_handle *handle)
{
    return reinterpret_cast<LayerRegionIsland *>(handle);
}

const LayerRegion *to_layer_region(const layer_region_handle *handle)
{
    return reinterpret_cast<const LayerRegion *>(handle);
}

LayerSliceIsland *to_layer_island(layer_island_handle *handle)
{
    return reinterpret_cast<LayerSliceIsland *>(handle);
}

ExtrusionEntity *to_extrusion(extrusion_entity_handle *handle)
{
    return reinterpret_cast<ExtrusionEntity *>(handle);
}

const ExPolygon *to_expolygon(const expolygon_handle *handle)
{
    return reinterpret_cast<const ExPolygon *>(handle);
}

SurfaceCollection *to_surface_collection(surface_collection_handle *handle)
{
    return reinterpret_cast<SurfaceCollection *>(handle);
}

ExtrusionRole bucket_role_from_raw(raw_extrusion_role role)
{
    if ((role & RAW_EXTRUSION_ROLE_THIN) != 0 || role == RAW_EXTRUSION_ROLE_GAP_FILL)
        return LayerRegionIsland::GAP_FILLS;
    if ((role & RAW_EXTRUSION_ROLE_INFILL) != 0)
        return LayerRegionIsland::INFILLS;
    if ((role & RAW_EXTRUSION_ROLE_IRONING) != 0)
        return LayerRegionIsland::IRONINGS;
    if ((role & RAW_EXTRUSION_ROLE_MILL) != 0)
        return LayerRegionIsland::MILLS;
    if ((role & RAW_EXTRUSION_ROLE_SUPPORT) != 0)
        return (role & RAW_EXTRUSION_ROLE_EXTERNAL) != 0 ? LayerRegionIsland::SUPPORT_INTERFACE :
                                                           LayerRegionIsland::SUPPORT;
    return LayerRegionIsland::PERIMETERS;
}

LayerRegionSetCPtrs region_set_from_handles(const layer_region_handle *const *region_handles,
                                             uint32_t region_count,
                                             const LayerSliceIsland &island)
{
    LayerRegionSetCPtrs regions;
    for (uint32_t idx = 0; idx < region_count; ++idx) {
        const LayerRegion *region = region_handles == nullptr ? nullptr : to_layer_region(region_handles[idx]);
        if (region != nullptr)
            regions.insert(region);
    }

    if (regions.empty())
        regions = island.regions();
    return regions;
}

uint16_t perimeter_extruder_id(const LayerRegionSetCPtrs &regions)
{
    if (regions.empty())
        return uint16_t(-1);

    const int16_t extruder_id = int16_t((*regions.begin())->region().config().perimeter_extruder) - 1;
    return extruder_id < 0 ? uint16_t(-1) : uint16_t(extruder_id);
}

uint32_t requested_perimeter_count(const LayerRegionSetCPtrs &regions)
{
    if (regions.empty())
        return 0;

    const int count = (*regions.begin())->region().config().perimeters.value;
    return count <= 0 ? 0 : uint32_t(count);
}

void append_extrusion_children(ExtrusionEntityCollection &dst, ExtrusionEntity &src)
{
    if (src.is_nop())
        return;

    if (ExtrusionEntityCollection *collection = dynamic_cast<ExtrusionEntityCollection *>(&src)) {
        dst.append_move_from(*collection);
        return;
    }

    if (src.is_leaf()) {
        dst.append(std::move(src));
        return;
    }

    ExtrusionEntity::Children &children = src.children();
    while (!children.empty()) {
        dst.append(std::move(children.front()));
        children.erase(children.begin());
    }
}

void append_extrusion_children(ExtrusionEntity &dst, ExtrusionEntity &src)
{
    if (src.is_nop())
        return;

    if (src.is_leaf()) {
        dst.append_child(ExtrusionEntityUPtr(src.clone_move()));
        src.clear_content();
        src.clear_properties();
        return;
    }

    ExtrusionEntity::Children &children = src.children();
    while (!children.empty()) {
        dst.append_child(std::move(children.front()));
        children.erase(children.begin());
    }
}

void collect_extrusions(PerimeterTreeNode &node, ExtrusionEntityCollection &out)
{
    // Keep the extrusion publication shaped like the generation tree. Each
    // PerimeterTreeNode becomes one collection node containing the loops/gap
    // fill generated for this level, followed by one child collection per
    // inner area. Later steps can then recover the perimeter depth hierarchy
    // instead of receiving a flat list of unrelated loops.
    std::unique_ptr<ExtrusionEntity> group =
        std::make_unique<ExtrusionEntity>(ExtrusionEntity::Children(), false, true, false);
    append_extrusion_children(*group, node.extrusions);
    for (std::unique_ptr<PerimeterTreeNode> &child : node.children) {
        ExtrusionEntityCollection child_group;
        collect_extrusions(*child, child_group);
        append_extrusion_children(*group, child_group);
    }

    if (!group->is_leaf() && group->child_count() > 0)
        out.append(std::move(group));
}

void collect_leaf_areas(const PerimeterTreeNode &node,
                        ExPolygons &fill_areas,
                        ExPolygons &fill_no_overlap_areas)
{
    if (!node.children.empty()) {
        for (const std::unique_ptr<PerimeterTreeNode> &child : node.children)
            collect_leaf_areas(*child, fill_areas, fill_no_overlap_areas);
        return;
    }

    if (node.area.empty())
        return;

    fill_no_overlap_areas.push_back(node.area);
    fill_areas.push_back(node.fill_area.empty() ? node.area : node.fill_area);
}

PerimeterTreeNode *find_node(PerimeterTreeNode &node, perimeter_node *c_node)
{
    if (&node.node == c_node)
        return &node;

    for (std::unique_ptr<PerimeterTreeNode> &child : node.children) {
        PerimeterTreeNode *found = find_node(*child, c_node);
        if (found != nullptr)
            return found;
    }
    return nullptr;
}

PerimeterTreeNode *find_node(PerimeterTree &tree, perimeter_node *c_node)
{
    return c_node == nullptr ? nullptr : find_node(tree.root, c_node);
}

ExPolygon pick_fill_area_for_child(const ExPolygon &area, const ExPolygons &fill_areas)
{
    if (area.empty() || fill_areas.empty())
        return area;

    const Point sample = area.contour.front();
    std::vector<size_t> candidate_idxs;
    for (size_t idx = 0; idx < fill_areas.size(); ++idx)
        if (fill_areas[idx].contains(sample))
            candidate_idxs.push_back(idx);

    if (candidate_idxs.size() == 1)
        return fill_areas[candidate_idxs.front()];

    if (candidate_idxs.size() > 1) {
        ExPolygons best_intersection;
        double best_area = 0.;
        for (size_t idx : candidate_idxs) {
            ExPolygons intersection = intersection_ex(ExPolygons{area}, ExPolygons{fill_areas[idx]});
            double intersection_area = 0.;
            for (const ExPolygon &expoly : intersection)
                intersection_area += expoly.area();

            if (intersection_area > best_area) {
                best_area = intersection_area;
                best_intersection = std::move(intersection);
            }
        }

        ExPolygons merged = union_ex(best_intersection);
        if (!merged.empty())
            return merged.front();
    }

    return area;
}

void set_split_node_area(PerimeterTreeNode &node, ExPolygon &&area, const ExPolygons &fill_clip)
{
    node.area = std::move(area);
    node.fill_area = node.area;

    if (fill_clip.empty())
        return;

    ExPolygons fill_intersection = intersection_ex(ExPolygons{node.area}, fill_clip);
    ExPolygons merged = union_ex(fill_intersection);
    if (!merged.empty())
        node.fill_area = merged.front();
}

std::unique_ptr<PerimeterTreeNode> make_child_node(PerimeterTreeNode &parent,
                                                   const ExPolygon &area,
                                                   const ExPolygon *fill_area)
{
    std::unique_ptr<PerimeterTreeNode> child = std::make_unique<PerimeterTreeNode>(area);
    child->node.parent = &parent.node;
    if (fill_area != nullptr)
        child->fill_area = *fill_area;
    child->node.perimeter_idx = parent.node.perimeter_idx + 1;
    child->node.perimeter_needed = parent.node.perimeter_needed;
    child->sync_c_pointers();
    return child;
}

std::unique_ptr<PerimeterTreeNode> make_split_sibling(const PerimeterTreeNode &source)
{
    std::unique_ptr<PerimeterTreeNode> node = std::make_unique<PerimeterTreeNode>(source.area);
    node->node.parent = source.node.parent;
    node->node.perimeter_idx = source.node.perimeter_idx;
    node->node.perimeter_needed = source.node.perimeter_needed;
    node->sync_c_pointers();
    return node;
}

void create_children(PerimeterTreeNode &parent, const ExPolygons &inner_areas, const ExPolygons &inner_fill_areas)
{
    parent.children.clear();
    parent.children.reserve(inner_areas.size());
    const bool has_matching_fill_areas = inner_fill_areas.size() == inner_areas.size();
    for (size_t idx = 0; idx < inner_areas.size(); ++idx) {
        if (inner_areas[idx].empty())
            continue;
        const ExPolygon *fill_area = has_matching_fill_areas ? &inner_fill_areas[idx] : nullptr;
        parent.children.push_back(make_child_node(parent, inner_areas[idx], fill_area));
    }
    parent.sync_c_pointers();
}

void append_sibling(PerimeterTree &tree, PerimeterTreeNode &node, std::unique_ptr<PerimeterTreeNode> &&sibling)
{
    PerimeterTreeNode *parent = find_node(tree, node.node.parent);
    assert(parent != nullptr);
    if (parent == nullptr)
        return;
    sibling->node.parent = &parent->node;
    parent->children.push_back(std::move(sibling));
    parent->sync_c_pointers();
}

void split_node_callback(perimeter_generation_context *context,
                         perimeter_node *node,
                         const expolygon_collection_handle *clip,
                         perimeter_node_span *inside_nodes_out)
{
    if (inside_nodes_out != nullptr)
        *inside_nodes_out = {};
    if (context == nullptr || node == nullptr || clip == nullptr || inside_nodes_out == nullptr)
        return;

    PerimeterTree *tree = reinterpret_cast<PerimeterTree *>(context->generator_context);
    PerimeterTreeNode *to_split = tree == nullptr ? nullptr : find_node(*tree, node);
    const ExPolygons *clip_expolygons = reinterpret_cast<const ExPolygons *>(clip);
    if (to_split == nullptr || clip_expolygons == nullptr || clip_expolygons->empty())
        return;

    // Split only leaf nodes. Splitting a node that already has children would
    // need to repartition all generated descendants, which is a different
    // operation from this compact module callback.
    if (!to_split->children.empty() || to_split->node.parent == nullptr)
        return;

    ExPolygons area_yes = intersection_ex(ExPolygons{to_split->area}, *clip_expolygons);
    if (area_yes.empty())
        return;

    ExPolygons area_no = diff_ex(ExPolygons{to_split->area}, area_yes);
    if (area_no.empty()) {
        tree->last_span_nodes = {&to_split->node};
        inside_nodes_out->items = tree->last_span_nodes.data();
        inside_nodes_out->count = uint32_t(tree->last_span_nodes.size());
        return;
    }

    ExPolygons fill_yes = intersection_ex(ExPolygons{to_split->fill_area}, *clip_expolygons);
    ExPolygons fill_no = diff_ex(ExPolygons{to_split->fill_area}, fill_yes);

    tree->last_span_nodes.clear();
    ExPolygon first_inside = std::move(area_yes.front());
    area_yes.erase(area_yes.begin());
    set_split_node_area(*to_split, std::move(first_inside), fill_yes);
    tree->last_span_nodes.push_back(&to_split->node);

    while (!area_yes.empty()) {
        std::unique_ptr<PerimeterTreeNode> sibling = make_split_sibling(*to_split);
        ExPolygon inside_area = std::move(area_yes.front());
        area_yes.erase(area_yes.begin());
        set_split_node_area(*sibling, std::move(inside_area), fill_yes);
        tree->last_span_nodes.push_back(&sibling->node);
        append_sibling(*tree, *to_split, std::move(sibling));
    }

    while (!area_no.empty()) {
        std::unique_ptr<PerimeterTreeNode> sibling = make_split_sibling(*to_split);
        ExPolygon outside_area = std::move(area_no.front());
        area_no.erase(area_no.begin());
        set_split_node_area(*sibling, std::move(outside_area), fill_no);
        append_sibling(*tree, *to_split, std::move(sibling));
    }

    tree->sync_c_pointers();
    inside_nodes_out->items = tree->last_span_nodes.data();
    inside_nodes_out->count = uint32_t(tree->last_span_nodes.size());
}

void rebuild_children_callback(perimeter_generation_context *context,
                               perimeter_node *node,
                               const expolygon_collection_handle *areas,
                               const expolygon_collection_handle *fill_areas)
{
    if (context == nullptr || node == nullptr || areas == nullptr)
        return;

    PerimeterTree *tree = reinterpret_cast<PerimeterTree *>(context->generator_context);
    PerimeterTreeNode *parent = tree == nullptr ? nullptr : find_node(*tree, node);
    const ExPolygons *child_areas = reinterpret_cast<const ExPolygons *>(areas);
    const ExPolygons *child_fill_areas = reinterpret_cast<const ExPolygons *>(fill_areas);
    if (parent == nullptr || child_areas == nullptr)
        return;

    parent->children.clear();
    parent->children.reserve(child_areas->size());
    for (const ExPolygon &area : *child_areas) {
        if (area.empty())
            continue;
        ExPolygon fill_area = child_fill_areas == nullptr || child_fill_areas->empty() ?
                                  area :
                                  pick_fill_area_for_child(area, *child_fill_areas);
        parent->children.push_back(make_child_node(*parent, area, &fill_area));
    }
    tree->sync_c_pointers();
}

perimeter_generation_context make_generation_context(plugin_run_context *run_context,
                                                     const run_ctx_generate_perimeter &ctx,
                                                     layer_region_island_handle *region_island,
                                                     PerimeterTree &tree)
{
    perimeter_generation_context context = {};
    context.run_ctx = run_context;
    context.print = ctx.print;
    context.object = ctx.object;
    context.layer = ctx.layer;
    context.island = ctx.island;
    context.region_island = region_island;
    context.root = &tree.root.node;
    context.generator_context = &tree;
    context.split_node = &split_node_callback;
    context.rebuild_children = &rebuild_children_callback;
    return context;
}

void call_module_start(std::vector<PerimeterModuleRun> &modules,
                       perimeter_generation_context &context)
{
    for (PerimeterModuleRun &module_run : modules) {
        module_run.started = true;
        if (module_run.module.vt != nullptr && module_run.module.vt->start != nullptr)
            module_run.user_context = module_run.module.vt->start(module_run.module.ctx, &context);
    }
}

void call_module_before(const std::vector<PerimeterModuleRun> &modules,
                        perimeter_generation_context &context,
                        perimeter_node &node)
{
    for (const PerimeterModuleRun &module_run : modules)
        if (module_run.started && module_run.module.vt != nullptr && module_run.module.vt->before != nullptr)
            module_run.module.vt->before(module_run.module.ctx, module_run.user_context, &context, &node);
}

void call_module_after(const std::vector<PerimeterModuleRun> &modules,
                       perimeter_generation_context &context,
                       perimeter_node &node)
{
    for (const PerimeterModuleRun &module_run : modules)
        if (module_run.started && module_run.module.vt != nullptr && module_run.module.vt->after != nullptr)
            module_run.module.vt->after(module_run.module.ctx, module_run.user_context, &context, &node);
}

void call_module_end(std::vector<PerimeterModuleRun> &modules,
                     perimeter_generation_context &context)
{
    for (PerimeterModuleRun &module_run : modules) {
        if (module_run.started && module_run.module.vt != nullptr && module_run.module.vt->end != nullptr)
            module_run.module.vt->end(module_run.module.ctx, module_run.user_context, &context);
        module_run.user_context = nullptr;
        module_run.started = false;
    }
}

class PerimeterModuleEndGuard
{
public:
    PerimeterModuleEndGuard(std::vector<PerimeterModuleRun> &modules,
                            perimeter_generation_context &context)
        : m_modules(&modules)
        , m_context(&context)
    {}

    ~PerimeterModuleEndGuard()
    {
        this->finish();
    }

    void finish()
    {
        if (m_modules != nullptr && m_context != nullptr)
            call_module_end(*m_modules, *m_context);
        m_modules = nullptr;
        m_context = nullptr;
    }

private:
    std::vector<PerimeterModuleRun> *m_modules = nullptr;
    perimeter_generation_context *m_context = nullptr;
};

std::vector<PerimeterModuleRun>
create_perimeter_generation_modules(PerimeterRunContext &run)
{
    std::vector<PerimeterModuleRun> modules;
    if (run.orchestrator == nullptr)
        return modules;

    std::vector<Plugin *> plugins = run.orchestrator->get_active_plugins_for_step(PERIMETER_GENERATION_MODULE);
    modules.reserve(plugins.size());
    run.module_host_contexts.clear();
    run.module_host_contexts.reserve(plugins.size());

    for (Plugin *plugin : plugins) {
        run.module_host_contexts.push_back(
            run.orchestrator->prepare_plugin_host_context(PERIMETER_GENERATION_MODULE, plugin, run.print));
        plugin_host_context &host_context = run.module_host_contexts.back();
        plugin_run_context run_context =
            run.orchestrator->prepare_plugin_run_context(PERIMETER_GENERATION_MODULE, plugin, &host_context);
        run_ctx_perimeter_generation_module module_context = {};
        run_context.data = &module_context;

        plugin->setup(run_context, 1);
        plugin->setup_run(run_context);
        plugin->run(run_context);

        if (module_context.module.vt != nullptr) {
            PerimeterModuleRun module_run;
            module_run.module = module_context.module;
            modules.push_back(module_run);
        }
    }

    return modules;
}

void publish_region_group(PerimeterRunContext &run,
                          LayerRegionIsland &region_island,
                          PerimeterTree &tree)
{
    ExtrusionEntityCollection perimeters;
    perimeters.set_can_sort_reverse(false, false);
    collect_extrusions(tree.root, perimeters);

    ExtrusionEntityCollection &dst = region_island.mutable_extrusion(LayerRegionIsland::PERIMETERS);
    dst.clear();
    dst.append_move_from(perimeters);
    region_island.remove_empty_extrusions();

    collect_leaf_areas(tree.root, run.fill_areas, run.fill_no_overlap_areas);
}

int32_t run_region_group_callback(const run_ctx_generate_perimeter *ctx,
                                  const layer_region_handle *const *region_handles,
                                  uint32_t region_count,
                                  const expolygon_handle *root_area,
                                  void *generator_context,
                                  perimeter_generate_node_fn generate_node)
{
    if (ctx == nullptr || ctx->host_context == nullptr || generate_node == nullptr)
        return 0;

    PerimeterRunContext &run = *reinterpret_cast<PerimeterRunContext *>(ctx->host_context);
    if (run.island == nullptr || run.generator_run_context == nullptr)
        return 0;
    run.used_region_group = true;

    const ExPolygon *root_expolygon = root_area == nullptr ? &run.island->get_slice() : to_expolygon(root_area);
    if (root_expolygon == nullptr || root_expolygon->empty())
        return 1;

    LayerRegionSetCPtrs regions = region_set_from_handles(region_handles, region_count, *run.island);
    if (regions.empty())
        return 0;

    LayerRegionIsland &region_island =
        run.island->get_or_add_region_island(regions, perimeter_extruder_id(regions));
    PerimeterTree tree(*root_expolygon, requested_perimeter_count(regions));
    std::vector<PerimeterModuleRun> modules = create_perimeter_generation_modules(run);
    perimeter_generation_context generation_context =
        make_generation_context(run.generator_run_context,
                                *ctx,
                                reinterpret_cast<layer_region_island_handle *>(&region_island),
                                tree);

    PerimeterModuleEndGuard module_end_guard(modules, generation_context);
    call_module_start(modules, generation_context);

    int32_t result = 1;
    std::vector<PerimeterTreeNode *> pending_nodes;
    pending_nodes.push_back(&tree.root);
    while (!pending_nodes.empty()) {
        PerimeterTreeNode *node = pending_nodes.back();
        pending_nodes.pop_back();
        if (node == nullptr || !node->needs_more_perimeters())
            continue;

        if (run.print != nullptr)
            run.print->throw_if_canceled();

        tree.sync_c_pointers();
        call_module_before(modules, generation_context, node->node);

        node->extrusions.clear_content();
        node->extrusions.clear_properties();
        ExPolygons inner_areas;
        ExPolygons inner_fill_areas;
        const int32_t ok = generate_node(generator_context,
                                         &generation_context,
                                         &node->node,
                                         reinterpret_cast<expolygon_collection_handle *>(&inner_areas),
                                         reinterpret_cast<expolygon_collection_handle *>(&inner_fill_areas));
        if (!ok) {
            result = 0;
            break;
        }

        create_children(*node, inner_areas, inner_fill_areas);

        tree.sync_c_pointers();
        call_module_after(modules, generation_context, node->node);

        for (std::unique_ptr<PerimeterTreeNode> &child : node->children)
            pending_nodes.push_back(child.get());
    }

    module_end_guard.finish();
    if (!result)
        return result;

    publish_region_group(run, region_island, tree);
    return result;
}

layer_region_island_handle *get_or_create_region_island_callback(const layer_island_handle *island_handle,
                                                                 const layer_region_handle *const *region_handles,
                                                                 uint32_t region_count)
{
    LayerSliceIsland *island = to_layer_island(const_cast<layer_island_handle *>(island_handle));
    if (island == nullptr)
        return nullptr;

    LayerRegionSetCPtrs regions = region_set_from_handles(region_handles, region_count, *island);
    if (regions.empty())
        return nullptr;
    LayerRegionIsland &region_island = island->get_or_add_region_island(regions, perimeter_extruder_id(regions));
    return reinterpret_cast<layer_region_island_handle *>(&region_island);
}

int32_t set_region_island_extrusion_callback(layer_region_island_handle *region_island_handle,
                                             raw_extrusion_role role,
                                             extrusion_entity_handle *extrusion_handle)
{
    LayerRegionIsland *region_island = to_region_island(region_island_handle);
    if (region_island == nullptr)
        return 0;

    ExtrusionEntityCollection &dst = region_island->mutable_extrusion(bucket_role_from_raw(role));
    dst.clear();
    if (extrusion_handle != nullptr)
        append_extrusion_children(dst, *to_extrusion(extrusion_handle));
    region_island->remove_empty_extrusions();
    return 1;
}

int32_t set_region_island_fill_surfaces_callback(layer_region_island_handle *region_island_handle,
                                                 surface_collection_handle *surfaces_handle)
{
    LayerRegionIsland *region_island = to_region_island(region_island_handle);
    if (region_island == nullptr)
        return 0;

    SurfaceCollection *surfaces = to_surface_collection(surfaces_handle);
    for (const LayerRegion *region : region_island->regions()) {
        LayerRegion *mutable_region = const_cast<LayerRegion *>(region);
        mutable_region->set_fill_surfaces().clear();
        if (surfaces != nullptr)
            mutable_region->set_fill_surfaces().set(*surfaces);
    }
    return 1;
}

int32_t set_region_island_fill_no_overlap_surfaces_callback(layer_region_island_handle *region_island_handle,
                                                            surface_collection_handle *surfaces_handle)
{
    LayerRegionIsland *region_island = to_region_island(region_island_handle);
    if (region_island == nullptr)
        return 0;

    ExPolygons expolygons;
    SurfaceCollection *surfaces = to_surface_collection(surfaces_handle);
    if (surfaces != nullptr)
        for (const Surface &surface : *surfaces)
            expolygons.push_back(surface.expolygon);

    for (const LayerRegion *region : region_island->regions()) {
        LayerRegion *mutable_region = const_cast<LayerRegion *>(region);
        ApiInternal::LayerRegionAccess::fill_no_overlap_expolygons_mutable(*mutable_region) = expolygons;
    }
    return 1;
}

void clear_island_outputs(LayerSliceIsland &island)
{
    island.mutable_regions_islands().clear();
    ApiInternal::LayerIslandAccess::set_fill_expolygons(island, ExPolygons{});
    ApiInternal::LayerIslandAccess::fill_no_overlap_expolygons_mutable(island).clear();
    ApiInternal::LayerIslandAccess::perimeter_slices_mutable(island).clear();
}

void clear_layer_outputs(Layer &layer)
{
    for (LayerSliceIsland &island : layer.islands())
        clear_island_outputs(island);

    for (LayerRegion &region : layer.regions()) {
        region.set_fill_surfaces().clear();
        ApiInternal::LayerRegionAccess::fill_no_overlap_expolygons_mutable(region).clear();
    }
}

void assign_island_outputs(LayerSliceIsland &island, PerimeterRunContext &run)
{
    run.fill_areas = ensure_valid(std::move(run.fill_areas));
    run.fill_no_overlap_areas = ensure_valid(std::move(run.fill_no_overlap_areas));

    ApiInternal::LayerIslandAccess::set_fill_expolygons(island, std::move(run.fill_areas));
    ApiInternal::LayerIslandAccess::fill_no_overlap_expolygons_mutable(island) =
        std::move(run.fill_no_overlap_areas);
    ApiInternal::LayerIslandAccess::perimeter_slices_mutable(island) =
        union_ex(ExPolygons{island.get_slice()});
}

void build_region_fill_surfaces(Layer &layer)
{
    ExPolygons all_fill_expolygons;
    ExPolygons all_fill_no_overlap_expolygons;
    for (LayerSliceIsland &island : layer.islands()) {
        append(all_fill_expolygons, island.fill_expolygons());
        if (island.fill_no_overlap_expolygons().empty())
            append(all_fill_no_overlap_expolygons, island.fill_expolygons());
        else
            append(all_fill_no_overlap_expolygons, island.fill_no_overlap_expolygons());
    }

    all_fill_no_overlap_expolygons = union_safety_offset_ex(all_fill_no_overlap_expolygons);
    for (LayerRegion &region : layer.regions()) {
        region.set_fill_surfaces().clear();
        for (const Surface &raw_surface : region.slices()) {
            ExPolygons expolygons = intersection_ex(ExPolygons{raw_surface.expolygon}, all_fill_expolygons);
            region.set_fill_surfaces().append(std::move(expolygons), raw_surface);
        }

        ExPolygons &fill_no_overlap =
            ApiInternal::LayerRegionAccess::fill_no_overlap_expolygons_mutable(region);
        fill_no_overlap = intersection_ex(region.get_raw_slices(), all_fill_no_overlap_expolygons);
        if (fill_no_overlap == region.get_raw_slices())
            ensure_valid(fill_no_overlap);
    }
}

size_t count_layer_islands(const Print &print)
{
    size_t count = 0;
    for (const PrintObject &object : print.objects())
        for (const Layer &layer : object.layers())
            count += layer.islands().size();
    return count;
}

bool run_generator_for_island(Orchestrator &orchestrator,
                              Plugin &plugin,
                              Print &print,
                              PrintObject &object,
                              Layer &layer,
                              LayerSliceIsland &island,
                              plugin_host_context &host_context)
{
    plugin_run_context run_context =
        orchestrator.prepare_plugin_run_context(STEP_PERIMETER, &plugin, &host_context);

    PerimeterRunContext perimeter_context;
    perimeter_context.orchestrator = &orchestrator;
    perimeter_context.generator_plugin = &plugin;
    perimeter_context.generator_run_context = &run_context;
    perimeter_context.print = &print;
    perimeter_context.object = &object;
    perimeter_context.layer = &layer;
    perimeter_context.island = &island;

    run_ctx_generate_perimeter payload = {};
    payload.print = reinterpret_cast<const print_handle *>(&print);
    payload.object = reinterpret_cast<const object_handle *>(&object);
    payload.layer = reinterpret_cast<const layer_handle *>(&layer);
    payload.island = reinterpret_cast<const layer_island_handle *>(&island);
    payload.host_context = &perimeter_context;
    payload.run_region_group = &run_region_group_callback;
    payload.get_or_create_region_island = &get_or_create_region_island_callback;
    payload.set_region_island_extrusion = &set_region_island_extrusion_callback;
    payload.set_region_island_fill_surfaces = &set_region_island_fill_surfaces_callback;
    payload.set_region_island_fill_no_overlap_surfaces = &set_region_island_fill_no_overlap_surfaces_callback;
    run_context.data = &payload;

    plugin.setup_run(run_context);
    plugin.run(run_context);

    if (perimeter_context.used_region_group)
        assign_island_outputs(island, perimeter_context);
    return perimeter_context.used_region_group;
}

} // namespace

void clean_and_prepare(Print &print)
{
    for (PrintObject &object : print.objects())
        for (Layer &layer : object.layers())
            clear_layer_outputs(layer);
}

bool validate_pre(const Print &, std::string *)
{
    return true;
}

bool validate_post(const Print &, std::string *)
{
    return true;
}

void run_step(Orchestrator &orchestrator, Print &print)
{
    // STEP_PERIMETER is an exclusive step: many perimeter generator plugins may
    // be active, but exactly one owns the generation for this print. The
    // selected_or_active_plugin_for_step() helper reads the generated
    // step_perimeter_plugin config option when it exists, falling back to the
    // first active generator only when there is no selector to read.
    Plugin *plugin = selected_or_active_plugin_for_step(orchestrator, STEP_PERIMETER, &print.full_print_config());
    if (plugin == nullptr)
        return;

    const size_t run_count = count_layer_islands(print);
    plugin_host_context host_context =
        orchestrator.prepare_plugin_host_context(STEP_PERIMETER, plugin, &print);
    plugin_run_context setup_context =
        orchestrator.prepare_plugin_run_context(STEP_PERIMETER, plugin, &host_context);
    plugin->setup(setup_context, uint32_t(run_count));

    for (PrintObject &object : print.objects()) {
        for (Layer &layer : object.layers()) {
            bool layer_needs_fill_rebuild = false;
            for (LayerSliceIsland &island : layer.islands())
                layer_needs_fill_rebuild |= run_generator_for_island(
                    orchestrator, *plugin, print, object, layer, island, host_context);
            if (layer_needs_fill_rebuild)
                build_region_fill_surfaces(layer);
        }
    }
}

} // namespace Slic3r::Steps::StepGeneratePerimeter
