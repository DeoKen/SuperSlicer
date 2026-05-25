///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "SimplePerimeterGenerator.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_perimeter.h"
#include "libslic3r/Api/plugin/cpp/ExtrusionViews.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/SurfaceCollection.hpp"

namespace slic3r_api { namespace Perimeter { namespace SimplePerimeterGeneratorPlugin {

namespace {

const char *k_simple_perimeter_generator_id = "perimeter.generator.simple";
const char *k_no_dependencies[] = { nullptr };

struct SimplePerimeterNode
{
    SimplePerimeterNode *parent = nullptr;
    Slic3r::ExPolygon surface;
    Slic3r::ExPolygon fill_surface;
    Slic3r::ExtrusionEntityCollection extrusions;
    std::vector<std::unique_ptr<SimplePerimeterNode>> children;
    std::vector<perimeter_node *> c_children;
    perimeter_node c_node = {};

    bool needs_more_perimeters() const
    {
        return c_node.perimeter_needed > 0 && c_node.perimeter_idx < c_node.perimeter_needed;
    }

    void refresh_c_node()
    {
        c_children.clear();
        c_children.reserve(children.size());
        for (std::unique_ptr<SimplePerimeterNode> &child : children) {
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

struct SimplePerimeterTree
{
    SimplePerimeterTree(const Slic3r::ExPolygon &root_surface, const Slic3r::Flow &flow) :
        flow(flow)
    {
        root.surface = root_surface;
        root.fill_surface = root_surface;
        root.c_node.perimeter_idx = 0;
        root.c_node.perimeter_needed = 1;
        root.refresh_c_node();
    }

    SimplePerimeterNode root;
    Slic3r::Flow flow;
    std::vector<perimeter_node *> last_span_nodes;

    void refresh_c_nodes() { root.refresh_c_node(); }
};

const Slic3r::LayerSliceIsland *to_layer_island(const layer_island_handle *handle)
{
    return reinterpret_cast<const Slic3r::LayerSliceIsland *>(handle);
}

const Slic3r::ExPolygons *to_expolygons(const expolygon_collection_handle *handle)
{
    return reinterpret_cast<const Slic3r::ExPolygons *>(handle);
}

layer_region_island_handle *get_or_create_single_region_island(const run_ctx_generate_perimeter &ctx,
                                                               const Slic3r::LayerSliceIsland &island,
                                                               std::vector<const layer_region_handle *> &region_handles)
{
    if (ctx.get_or_create_region_island == nullptr)
        return nullptr;

    region_handles.clear();
    region_handles.reserve(island.regions().size());
    for (const Slic3r::LayerRegion *region : island.regions())
        region_handles.push_back(reinterpret_cast<const layer_region_handle *>(region));

    return ctx.get_or_create_region_island(ctx.island,
                                           region_handles.empty() ? nullptr : region_handles.data(),
                                           uint32_t(region_handles.size()));
}

Slic3r::Flow external_perimeter_flow(const Slic3r::LayerSliceIsland &island)
{
    if (!island.regions().empty())
        return (*island.regions().begin())->flow(Slic3r::frExternalPerimeter);
    return Slic3r::Flow();
}

Slic3r::ExPolygons offset_surface(const Slic3r::ExPolygon &surface, double delta)
{
    return Slic3r::ensure_valid(Slic3r::offset_ex(surface, delta));
}

void append_perimeter_loop(Slic3r::ExtrusionEntityCollection &dst,
                           Slic3r::Polygon &&polygon,
                           const Slic3r::ExtrusionAttributes &attributes,
                           Slic3r::ExtrusionLoopRole loop_role)
{
    if (!polygon.is_valid())
        return;

    Slic3r::ExtrusionPath path(attributes, nullptr, true);
    path.get_or_add_property<EPropertyPerimeter>().shell_count(0).perimeter_role(loop_role);
    path.polyline().append(std::move(polygon.points));
    path.polyline().append(path.polyline().front());
    dst.append(Slic3r::ExtrusionLoop(std::move(path), loop_role));
}

Slic3r::ExtrusionEntityCollection make_external_perimeter_extrusion(const Slic3r::ExPolygon &surface,
                                                                    const Slic3r::Flow &flow)
{
    Slic3r::ExtrusionEntityCollection extrusion;
    const double half_width = -0.5 * double(flow.scaled_width());
    const Slic3r::ExtrusionAttributes attributes(Slic3r::ExtrusionRole::ExternalPerimeter, flow);
    Slic3r::ExPolygons loops = offset_surface(surface, half_width);
    for (Slic3r::ExPolygon &loop : loops) {
        append_perimeter_loop(extrusion, std::move(loop.contour), attributes, Slic3r::elrDefault);
        for (Slic3r::Polygon &hole : loop.holes)
            append_perimeter_loop(extrusion, std::move(hole), attributes, Slic3r::elrHole);
    }
    return extrusion;
}

Slic3r::ExPolygons make_inner_surfaces(const Slic3r::ExPolygon &surface, const Slic3r::Flow &flow)
{
    const double spacing = double(flow.scaled_spacing());
    return offset_surface(surface, -spacing);
}

SimplePerimeterNode *find_node(SimplePerimeterNode &node, perimeter_node *c_node)
{
    if (&node.c_node == c_node)
        return &node;

    for (std::unique_ptr<SimplePerimeterNode> &child : node.children) {
        SimplePerimeterNode *found = find_node(*child, c_node);
        if (found != nullptr)
            return found;
    }

    return nullptr;
}

SimplePerimeterNode *find_node(SimplePerimeterTree &tree, perimeter_node *c_node)
{
    return c_node == nullptr ? nullptr : find_node(tree.root, c_node);
}

Slic3r::ExPolygon pick_fill_surface_for_child(const Slic3r::ExPolygon &surface,
                                              const Slic3r::ExPolygons &fill_surfaces)
{
    if (surface.empty() || fill_surfaces.empty())
        return surface;

    const Slic3r::Point &sample = surface.contour.points.front();
    std::vector<size_t> candidate_idxs;
    for (size_t idx = 0; idx < fill_surfaces.size(); ++idx)
        if (fill_surfaces[idx].contains(sample))
            candidate_idxs.push_back(idx);

    if (candidate_idxs.size() == 1)
        return fill_surfaces[candidate_idxs.front()];

    if (candidate_idxs.size() > 1) {
        Slic3r::ExPolygons best_intersection;
        double best_area = 0.;
        for (const size_t idx : candidate_idxs) {
            Slic3r::ExPolygons intersection = Slic3r::intersection_ex(surface, fill_surfaces[idx]);
            double intersection_area = 0.;
            for (const Slic3r::ExPolygon &expoly : intersection)
                intersection_area += expoly.area();

            if (intersection_area > best_area) {
                best_area = intersection_area;
                best_intersection = std::move(intersection);
            }
        }

        if (!best_intersection.empty()) {
            Slic3r::ExPolygons merged = Slic3r::union_ex(best_intersection);
            if (!merged.empty())
                return merged.front();
        }
    }

    return surface;
}

void set_split_node_surface(SimplePerimeterTree &tree,
                            SimplePerimeterNode &node,
                            Slic3r::ExPolygon &&surface,
                            const Slic3r::ExPolygons &fill_clip)
{
    node.surface = std::move(surface);
    node.fill_surface = node.surface;

    if (fill_clip.empty())
        return;

    const coord_t max_perimeter_width = std::max(tree.flow.scaled_width(), tree.flow.scaled_width());
    Slic3r::ExPolygons big_surface = Slic3r::offset_ex(node.surface, double(max_perimeter_width));
    if (big_surface.size() != 1)
        return;

    big_surface = Slic3r::intersection_ex(big_surface, fill_clip);
    if (big_surface.size() == 1)
        node.fill_surface = big_surface.front();
}

std::unique_ptr<SimplePerimeterNode> make_child_node(SimplePerimeterNode &parent,
                                                     Slic3r::ExPolygon &&surface)
{
    std::unique_ptr<SimplePerimeterNode> child = std::make_unique<SimplePerimeterNode>();
    child->parent = &parent;
    child->surface = std::move(surface);
    child->fill_surface = child->surface;
    child->c_node.perimeter_idx = parent.c_node.perimeter_idx + 1;
    child->c_node.perimeter_needed = parent.c_node.perimeter_needed;
    child->refresh_c_node();
    return child;
}

std::unique_ptr<SimplePerimeterNode> make_split_sibling(const SimplePerimeterNode &source)
{
    std::unique_ptr<SimplePerimeterNode> node = std::make_unique<SimplePerimeterNode>();
    node->parent = source.parent;
    node->c_node.perimeter_idx = source.c_node.perimeter_idx;
    node->c_node.perimeter_needed = source.c_node.perimeter_needed;
    node->refresh_c_node();
    return node;
}

void create_children(SimplePerimeterNode &parent, Slic3r::ExPolygons &&inner_surfaces)
{
    parent.children.clear();
    parent.children.reserve(inner_surfaces.size());
    for (Slic3r::ExPolygon &surface : inner_surfaces)
        if (!surface.empty())
            parent.children.push_back(make_child_node(parent, std::move(surface)));
    parent.refresh_c_node();
}

void append_sibling(SimplePerimeterNode &node, std::unique_ptr<SimplePerimeterNode> &&sibling)
{
    assert(node.parent != nullptr);
    if (node.parent == nullptr)
        return;
    sibling->parent = node.parent;
    node.parent->children.push_back(std::move(sibling));
    node.parent->refresh_c_node();
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

    SimplePerimeterTree *tree = reinterpret_cast<SimplePerimeterTree *>(context->generator_context);
    if (tree == nullptr)
        return;

    SimplePerimeterNode *to_split = find_node(*tree, node);
    const Slic3r::ExPolygons *clip_expolygons = to_expolygons(clip);
    if (to_split == nullptr || clip_expolygons == nullptr || clip_expolygons->empty())
        return;

    // Split is intentionally limited to leaf nodes. Splitting a branch with
    // already generated children would also require splitting those children
    // and their extrusion history, which belongs in a richer generator.
    if (!to_split->children.empty() || to_split->parent == nullptr)
        return;

    Slic3r::ExPolygons srf_yes = Slic3r::intersection_ex(to_split->surface, *clip_expolygons);
    if (srf_yes.empty())
        return;

    Slic3r::ExPolygons srf_no = Slic3r::diff_ex(to_split->surface, srf_yes);
    if (srf_no.empty()) {
        tree->last_span_nodes = { &to_split->c_node };
        inside_nodes_out->items = tree->last_span_nodes.data();
        inside_nodes_out->count = uint32_t(tree->last_span_nodes.size());
        return;
    }

    Slic3r::ExPolygons fill_yes = Slic3r::intersection_ex(Slic3r::ExPolygons{ to_split->fill_surface }, *clip_expolygons);
    Slic3r::ExPolygons fill_no = Slic3r::diff_ex(Slic3r::ExPolygons{ to_split->fill_surface }, fill_yes);

    tree->last_span_nodes.clear();
    set_split_node_surface(*tree, *to_split, std::move(srf_yes.front()), fill_yes);
    tree->last_span_nodes.push_back(&to_split->c_node);

    for (size_t idx = 1; idx < srf_yes.size(); ++idx) {
        std::unique_ptr<SimplePerimeterNode> sibling = make_split_sibling(*to_split);
        set_split_node_surface(*tree, *sibling, std::move(srf_yes[idx]), fill_yes);
        tree->last_span_nodes.push_back(&sibling->c_node);
        append_sibling(*to_split, std::move(sibling));
    }

    for (Slic3r::ExPolygon &outside_surface : srf_no) {
        std::unique_ptr<SimplePerimeterNode> sibling = make_split_sibling(*to_split);
        set_split_node_surface(*tree, *sibling, std::move(outside_surface), fill_no);
        append_sibling(*to_split, std::move(sibling));
    }

    tree->refresh_c_nodes();
    inside_nodes_out->items = tree->last_span_nodes.data();
    inside_nodes_out->count = uint32_t(tree->last_span_nodes.size());
}

void rebuild_children_callback(perimeter_generation_context *context,
                              perimeter_node *node,
                              const expolygon_collection_handle *surfaces,
                              const expolygon_collection_handle *fill_surfaces)
{
    if (context == nullptr || node == nullptr || surfaces == nullptr)
        return;

    SimplePerimeterTree *tree = reinterpret_cast<SimplePerimeterTree *>(context->generator_context);
    SimplePerimeterNode *parent = tree == nullptr ? nullptr : find_node(*tree, node);
    const Slic3r::ExPolygons *child_surfaces = to_expolygons(surfaces);
    const Slic3r::ExPolygons *child_fill_surfaces = to_expolygons(fill_surfaces);
    if (parent == nullptr || child_surfaces == nullptr)
        return;

    parent->children.clear();
    parent->children.reserve(child_surfaces->size());
    for (const Slic3r::ExPolygon &surface : *child_surfaces) {
        if (surface.empty())
            continue;
        std::unique_ptr<SimplePerimeterNode> child = make_child_node(*parent, Slic3r::ExPolygon(surface));
        if (child_fill_surfaces != nullptr && !child_fill_surfaces->empty())
            child->fill_surface = pick_fill_surface_for_child(child->surface, *child_fill_surfaces);
        child->refresh_c_node();
        parent->children.push_back(std::move(child));
    }
    tree->refresh_c_nodes();
}

void collect_extrusions(SimplePerimeterNode &node, Slic3r::ExtrusionEntityCollection &out)
{
    if (!node.extrusions.empty())
        out.append_move_from(node.extrusions);
    for (std::unique_ptr<SimplePerimeterNode> &child : node.children)
        collect_extrusions(*child, out);
}

void collect_leaf_surfaces(const SimplePerimeterNode &node,
                           const Slic3r::Flow &flow,
                           Slic3r::ExPolygons &fill_surfaces,
                           Slic3r::ExPolygons &fill_no_overlap_surfaces)
{
    if (!node.children.empty()) {
        for (const std::unique_ptr<SimplePerimeterNode> &child : node.children)
            collect_leaf_surfaces(*child, flow, fill_surfaces, fill_no_overlap_surfaces);
        return;
    }

    if (node.surface.empty())
        return;

    Slic3r::ExPolygons no_overlap = Slic3r::ensure_valid(Slic3r::ExPolygons{ node.surface });
    fill_no_overlap_surfaces.insert(fill_no_overlap_surfaces.end(),
                                    std::make_move_iterator(no_overlap.begin()),
                                    std::make_move_iterator(no_overlap.end()));

    Slic3r::ExPolygons fill = Slic3r::ensure_valid(
        Slic3r::offset_ex(node.fill_surface.empty() ? node.surface : node.fill_surface,
                          0.25 * double(flow.scaled_spacing())));
    fill_surfaces.insert(fill_surfaces.end(),
                         std::make_move_iterator(fill.begin()),
                         std::make_move_iterator(fill.end()));
}

void publish_extrusion(const run_ctx_generate_perimeter &ctx,
                       layer_region_island_handle *region_island,
                       Slic3r::ExtrusionEntityCollection &extrusion)
{
    if (ctx.set_region_island_extrusion == nullptr)
        return;

    ctx.set_region_island_extrusion(region_island,
                                    RAW_EXTRUSION_ROLE_EXTERNAL_PERIMETER,
                                    reinterpret_cast<extrusion_entity_handle *>(&extrusion));
}

void publish_fill_surfaces(perimeter_set_region_island_surfaces_fn setter,
                           layer_region_island_handle *region_island,
                           Slic3r::ExPolygons surfaces_area)
{
    if (setter == nullptr)
        return;

    Slic3r::SurfaceCollection surfaces;
    surfaces.append(std::move(surfaces_area), Slic3r::stPosInternal | Slic3r::stDensSparse);
    setter(region_island, reinterpret_cast<surface_collection_handle *>(&surfaces));
}

Slic3r::Orchestrator *to_orchestrator(orchestrator_handle *orch)
{
    return orch != nullptr ? reinterpret_cast<Slic3r::Orchestrator *>(orch) :
                             &Slic3r::Orchestrator::instance();
}

std::vector<perimeter_generation_module_instance>
create_perimeter_generation_modules(orchestrator_handle *orch, const run_ctx_generate_perimeter &ctx)
{
    std::vector<perimeter_generation_module_instance> modules;
    Slic3r::Orchestrator *orchestrator = to_orchestrator(orch);
    if (orchestrator == nullptr)
        return modules;

    std::vector<Slic3r::Plugin *> plugins =
        orchestrator->get_active_plugins_for_step(PERIMETER_GENERATION_MODULE);
    modules.reserve(plugins.size());

    for (Slic3r::Plugin *plugin : plugins) {
        Slic3r::Print *print = reinterpret_cast<Slic3r::Print *>(const_cast<print_handle *>(ctx.print));
        plugin_host_context host_context =
            orchestrator->prepare_plugin_host_context(PERIMETER_GENERATION_MODULE, plugin, print);
        plugin_run_context run_context =
            orchestrator->prepare_plugin_run_context(PERIMETER_GENERATION_MODULE, plugin, &host_context);
        run_ctx_perimeter_generation_module module_context = {};
        run_context.data = &module_context;

        plugin->setup(run_context, 1);
        plugin->setup_run(run_context);
        plugin->run(run_context);

        if (module_context.module.vt != nullptr)
            modules.push_back(module_context.module);
    }

    return modules;
}

perimeter_generation_context make_perimeter_generation_context(const plugin_run_context *run_ctx,
                                                               const run_ctx_generate_perimeter &ctx,
                                                               layer_region_island_handle *region_island,
                                                               SimplePerimeterTree &tree)
{
    perimeter_generation_context context = {};
    context.run_ctx = const_cast<plugin_run_context *>(run_ctx);
    context.print = ctx.print;
    context.object = ctx.object;
    context.layer = ctx.layer;
    context.island = ctx.island;
    context.region_island = region_island;
    context.root = &tree.root.c_node;
    context.generator_context = &tree;
    context.split_node = &split_node_callback;
    context.rebuild_children = &rebuild_children_callback;
    return context;
}

void call_module_start(const std::vector<perimeter_generation_module_instance> &modules,
                       perimeter_generation_context &context)
{
    for (const perimeter_generation_module_instance &module : modules)
        if (module.vt != nullptr && module.vt->start != nullptr)
            module.vt->start(module.ctx, &context);
}

void call_module_before(const std::vector<perimeter_generation_module_instance> &modules,
                        perimeter_generation_context &context,
                        perimeter_node &node)
{
    for (const perimeter_generation_module_instance &module : modules)
        if (module.vt != nullptr && module.vt->before != nullptr)
            module.vt->before(module.ctx, &context, &node);
}

void call_module_after(const std::vector<perimeter_generation_module_instance> &modules,
                       perimeter_generation_context &context,
                       perimeter_node &node)
{
    for (const perimeter_generation_module_instance &module : modules)
        if (module.vt != nullptr && module.vt->after != nullptr)
            module.vt->after(module.ctx, &context, &node);
}

void call_module_end(const std::vector<perimeter_generation_module_instance> &modules,
                     perimeter_generation_context &context)
{
    for (const perimeter_generation_module_instance &module : modules)
        if (module.vt != nullptr && module.vt->end != nullptr)
            module.vt->end(module.ctx, &context);
}

} // namespace

SimplePerimeterGenerator &
SimplePerimeterGenerator::instance(orchestrator_handle *orch)
{
    static SimplePerimeterGenerator s_instance(orch);
    return s_instance;
}

const char *SimplePerimeterGenerator::id_impl() const noexcept
{
    return k_simple_perimeter_generator_id;
}

slicing_step_t SimplePerimeterGenerator::step_impl() const noexcept
{
    return STEP_PERIMETER;
}

const char *const *SimplePerimeterGenerator::dependencies_impl() const noexcept
{
    return k_no_dependencies;
}

int32_t SimplePerimeterGenerator::priority_impl() const noexcept
{
    return 0;
}

const char *SimplePerimeterGenerator::progress_message_format_impl() const noexcept
{
    return "Simple perimeter generator: %u / %u islands";
}

void SimplePerimeterGenerator::setup_run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_generate_perimeter *ctx = plugin_ctx_as_generate_perimeter(run_ctx);
    if (ctx != nullptr && ctx->island != nullptr)
        progress().add_max(1);
}

void SimplePerimeterGenerator::run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_generate_perimeter *ctx = plugin_ctx_as_generate_perimeter(run_ctx);
    if (ctx == nullptr || ctx->island == nullptr)
        return;

    throw_if_cancelled(run_ctx);

    const Slic3r::LayerSliceIsland *island = to_layer_island(ctx->island);
    if (island == nullptr)
        return;

    std::vector<const layer_region_handle *> region_handles;
    layer_region_island_handle *region_island = get_or_create_single_region_island(*ctx, *island, region_handles);
    if (region_island == nullptr)
        return;

    const Slic3r::Flow flow = external_perimeter_flow(*island);
    SimplePerimeterTree tree(island->get_slice(), flow);

    std::vector<perimeter_generation_module_instance> modules =
        create_perimeter_generation_modules(m_orchestrator, *ctx);
    perimeter_generation_context generation_context =
        make_perimeter_generation_context(run_ctx, *ctx, region_island, tree);

    call_module_start(modules, generation_context);

    std::vector<SimplePerimeterNode *> pending_nodes;
    pending_nodes.push_back(&tree.root);
    while (!pending_nodes.empty()) {
        SimplePerimeterNode *node = pending_nodes.back();
        pending_nodes.pop_back();
        if (node == nullptr || !node->needs_more_perimeters())
            continue;

        throw_if_cancelled(run_ctx);

        tree.refresh_c_nodes();
        call_module_before(modules, generation_context, node->c_node);

        node->extrusions = make_external_perimeter_extrusion(node->surface, flow);
        create_children(*node, make_inner_surfaces(node->surface, flow));

        tree.refresh_c_nodes();
        call_module_after(modules, generation_context, node->c_node);

        for (std::unique_ptr<SimplePerimeterNode> &child : node->children)
            pending_nodes.push_back(child.get());
    }

    call_module_end(modules, generation_context);

    Slic3r::ExtrusionEntityCollection extrusion;
    collect_extrusions(tree.root, extrusion);

    Slic3r::ExPolygons fill_surfaces;
    Slic3r::ExPolygons fill_no_overlap_surfaces;
    collect_leaf_surfaces(tree.root, flow, fill_surfaces, fill_no_overlap_surfaces);

    publish_extrusion(*ctx, region_island, extrusion);
    publish_fill_surfaces(ctx->set_region_island_fill_surfaces, region_island, std::move(fill_surfaces));
    publish_fill_surfaces(ctx->set_region_island_fill_no_overlap_surfaces,
                          region_island,
                          std::move(fill_no_overlap_surfaces));

    progress().increment();
}

void register_simple_perimeter_generator_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, SimplePerimeterGenerator::instance(orch).c_instance());
}

}}} // namespace slic3r_api::Perimeter::SimplePerimeterGeneratorPlugin

#ifdef SIMPLE_PERIMETER_GENERATOR_PLUGIN_DLL
extern "C" void register_plugin(orchestrator_handle *orch)
{
    slic3r_api::Perimeter::SimplePerimeterGeneratorPlugin::register_simple_perimeter_generator_plugin(orch);
}
#endif // SIMPLE_PERIMETER_GENERATOR_PLUGIN_DLL
