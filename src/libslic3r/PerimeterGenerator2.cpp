///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "PerimeterGenerator2.hpp"

#include <utility>
#include <vector>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/DataTreeFwd.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/RegionSettings.hpp"

namespace Slic3r::PerimeterGenerator2 {
namespace {

struct PerimeterNode
{
    // root has nullptr parent.
    PerimeterNode *parent = nullptr;
    // area where you can extrude a new periemter or infill.
    ExPolygon surface;
    // bigge surface, that goes over the aprent's extrusions, used to clip when you have a fill surface that anchor
    // into perimeters. It's often just the parent surface, but it can be split / clipped by some algorithms.
    ExPolygon fill_surface;
    // perimeter extrusion extruded inside this surface
    ExtrusionEntityCollection extrusions;
    // childs contains the areas still available after the extrusions
    std::vector<PerimeterNode> children;
    // my index in perimeter count
    size_t perimeter_idx = 0;
    // number of perimeters loops
    int perimeter_needed = 0;

    bool needs_more_perimeters() const
    {
        return perimeter_needed > 0 && perimeter_idx < size_t(perimeter_needed);
    }
};

class PerimeterTree
{
public:
    explicit PerimeterTree(const ExPolygon &root_surface)
    {
        m_root.surface = root_surface;
    }

    PerimeterNode &root() { return m_root; }
    const PerimeterNode &root() const { return m_root; }
    const ExPolygons &final_inner_surfaces() const { return m_final_inner_surfaces; }

    void finish_node(const PerimeterNode &node)
    {
        if (!node.discarded)
            m_final_inner_surfaces.push_back(node.surface);
    }

    std::vector<PerimeterNode *> create_children(PerimeterNode &parent, ExPolygons &&inner_surfaces)
    {
        std::vector<PerimeterNode *> child_nodes;
        if (inner_surfaces.empty())
            return child_nodes;

        child_nodes.reserve(inner_surfaces.size());
        parent.children.reserve(parent.children.size() + inner_surfaces.size());

        for (ExPolygon &inner_surface : inner_surfaces) {
            parent.children.emplace_back();
            PerimeterNode &child = parent.children.back();
            child.parent = &parent;
            child.surface = std::move(inner_surface);
            child.perimeter_idx = parent.perimeter_idx + 1;
            child.perimeter_needed = parent.perimeter_needed;
            child_nodes.push_back(&child);
        }

        return child_nodes;
    }

    static void set_new_child(const PerimeterProcessContext &params,
                   PerimeterNode &node,
                   ExPolygon &&surface,
                   const ExPolygons &fill_clip) {
        node.surface = std::move(surface);
        const coord_t max_peri_width = std::max(params.perimeter_flow().scaled_width(),
                                                params.external_perimeter_flow().scaled_width());
        ExPolygons big_surface = offset_ex(node.surface, double(max_peri_width));
        assert(big_surface.size() == 1);
        if (big_surface.size() != 1) {
            node.fill_surface = node.surface;
            return;
        }
        big_surface = intersection_ex(big_surface, fill_clip);
        assert(big_surface.size() == 1);
        if (big_surface.size() != 1) {
            node.fill_surface = node.surface;
            return;
        }
        node.fill_surface = big_surface[0];
    }

    // return the number of node inside the clip area, including the 'to_split'. the node not inside the clip area are
    // at the end of new_nodes.
    // clip need to be wide enoug  to go over the to_clip's parent extrusions, for the fill area to be correctly clipped.
    static int split_node(const PerimeterProcessContext &params,
                            PerimeterNode &to_split,
                            std::vector<PerimeterNode> &new_nodes,
                            const ExPolygons &clip) {
            // split the child
            ExPolygons srf_yes = intersection_ex(to_split.surface, clip);
            if(srf_yes.empty()) {
                // nothing to clip, nothing is inside the clip area
                return 0;
            }
            ExPolygons srf_no = diff_ex(to_split.surface, srf_yes);
            if(srf_no.empty()) {
                // nothing to clip, evrything is inside the clip area
                return 1;
            }
            ExPolygons srf_fill_yes = intersection_ex({to_split.fill_surface}, clip);
            ExPolygons srf_fill_no = diff_ex({to_split.fill_surface}, srf_fill_yes);
            // normal areas
            PerimeterTree::set_new_child(params, to_split, std::move(srf_yes[0]), srf_fill_yes);
            for (size_t i = 1; i < srf_yes.size(); i++) {
                new_nodes.emplace_back(to_split);
                PerimeterTree::set_new_child(params, new_nodes.back(), std::move(srf_yes[i]), srf_fill_yes);
            }
            // top areas
            new_nodes.emplace_back(to_split);
            PerimeterTree::set_new_child(params, new_nodes.back(), std::move(srf_no[0]), srf_fill_no);
            for (size_t i = 1; i < srf_no.size(); i++) {
                new_nodes.emplace_back(to_split);
                PerimeterTree::set_new_child(params, new_nodes.back(), std::move(srf_no[i]), srf_fill_no);
            }
    }

private:
    PerimeterNode m_root;
    ExPolygons m_final_inner_surfaces;
};

struct PerimeterProcessContext
{
    Print &print;
    PrintObject &object;
    Layer &layer;
    LayerSliceIsland &island;
    LayerRegionIsland &region_island;
    RegionSettings region_setting;
    const ExPolygon &root_surface;

    const PrintRegionConfig& region_config() const;    // TODO (get one of the region's region_island's config)
    Flow perimeter_flow() const; //TODO
    Flow external_perimeter_flow() const; //TODO
};

std::vector<LayerRegionSetCPtrs> collect_region_groups(const LayerSliceIsland &island, const Layer &layer)
{
    // TODO: Move the compatibility grouping from Layer::make_perimeters().
    // This is the first split point between "which regions can share perimeter
    // generation" and the actual perimeter algorithm.
    (void) layer;

    LayerRegionSetCPtrs group;
    for (const LayerRegion *region : island.regions())
        group.insert(region);

    std::vector<LayerRegionSetCPtrs> groups;
    if (!group.empty())
        groups.push_back(std::move(group));
    return groups;
}

LayerRegionIsland &prepare_region_island(LayerSliceIsland &island, const LayerRegionSetCPtrs &regions)
{
    // TODO: Keep the current extruder selection rules from Layer::make_perimeters().
    // For now the new orchestrator only asks the LayerSliceIsland for the output
    // node matching this region group.
    return island.get_or_add_region_island(regions);
}

ExPolygons build_surface_inputs(const LayerSliceIsland &island, const LayerRegionIsland &region_island)
{
    // TODO: Reuse the old "whole island if all regions match, otherwise
    // intersect region raw slices with the island slice" logic. This isolates
    // the region split from the actual perimeter generation loop.
    if (region_island.regions() == island.regions())
        return ExPolygons{island.get_slice()};

    ExPolygons region_area;
    for (const LayerRegion *region : region_island.regions())
        append(region_area, region->get_raw_slices());

    region_area = union_safety_offset_ex(region_area);
    return intersection_ex(region_area, ExPolygons{island.get_slice()});
}

void initialize_root_node(const PerimeterProcessContext &context, PerimeterNode &root)
{
    // TODO: Recreate the contour/hole count setup from process_classic().
    // This is where the base perimeter count, hole perimeter count, spiral
    // vase, first-layer special case and simple whole-region overrides belong.
    (void) context;
    (void) root;
}

void apply_perimeter_count_settings(const PerimeterProcessContext &context, PerimeterNode &root)
{
    // TODO: Pull each setting rule into its own function:
    // - only_one_perimeter_top / only_one_perimeter_first_layer
    // - extra_perimeters_count
    // - extra_perimeters_odd_layers
    // - surface-provided extra perimeter counts
    // Each rule should edit the root node counts or attach data to nodes, not
    // touch generated extrusions.
    (void) context;
    (void) root;
}

void apply_geometry_masks(const PerimeterProcessContext &context, PerimeterNode &root)
{
    // TODO: Create explicit masks/constraints for features that alter the
    // perimeter geometry:
    // - no perimeters on bridge / unsupported areas
    // - extra perimeters on overhangs
    // - bridgeable versus unbridgeable unsupported zones
    // These masks are consumed by node generation and by the after-generation
    // modifier hooks below.
    (void) context;
    (void) root;
}

ExPolygons generate_perimeter_for_node(const PerimeterProcessContext &context, PerimeterNode &node)
{
    // TODO: Implement one-ring perimeter generation.
    // The generator receives the current node surface and its counters. It
    // writes the generated perimeter extrusions into node.extrusions and
    // returns the inner surfaces. If several inner ExPolygons are returned,
    // PerimeterTree creates one child node for each of them.
    //
    // Classic can generate a fixed-width ring. Arachne can generate variable
    // width extrusion and temporarily encode the width profile in ArcPolyline
    // per-point extra data currently stored through z-offset.
    (void) context;
    (void) node;
    return {};
}

class PerimeterModifier
{
public:
    virtual ~PerimeterModifier() = default;

    virtual void after_root_created(const PerimeterProcessContext &context,
                                    PerimeterTree &tree,
                                    PerimeterNode &root) const
    {
        (void) context;
        (void) tree;
        (void) root;
    }

    virtual void before_generation(const PerimeterProcessContext &context,
                                   PerimeterTree &tree,
                                   PerimeterNode &node) const
    {
        (void) context;
        (void) tree;
        (void) node;
    }

    virtual void after_generation(const PerimeterProcessContext &context,
                                  PerimeterTree &tree,
                                  PerimeterNode &parent,
                                  const std::vector<PerimeterNode *> &children) const
    {
        (void) context;
        (void) tree;
        (void) parent;
        (void) children;
    }
};

class ExtraPerimeterOddLayer final : public PerimeterModifier
{
public:
    //map of layerslice island to be sure we can be used in parallel.
    std::mutex mutex;
    std::map<const LayerSliceIsland*, std::set<const PerimeterNode *>> nodes_with_extra_perimeter;
    void after_root_created(const PerimeterProcessContext &params,
                            PerimeterTree &tree,
                            PerimeterNode &root) const override {
        std::lock_guard lock(mutex);
        nodes_with_extra_perimeter[&params.island].clear();
        // only on odd layers
        if (params.layer.id() % 2 == 0) {
            return;
        }
        if (params.region_setting.get_solo_config(params.region_config().option("extra_perimeters_odd_layers")).get_bool()) {
            root.perimeter_needed += 1;
            nodes_with_extra_perimeter[&params.island].insert(&root);
        }
    }

    bool check_and_set_already_seen(LayerSliceIsland * island, const PerimeterNode *search_for) const
    {
        std::lock_guard lock(mutex);
        auto it = nodes_with_extra_perimeter.find(island);
        if (it == nodes_with_extra_perimeter.end()) {
            return false;
        }
        std::set<const PerimeterNode *> &already_seen = it->second;
        const PerimeterNode *current = search_for;
        while(current) {
            if (already_seen.find(current) != already_seen.end()) {
                return true;
            }
            current = current->parent == current ? nullptr : current->parent;
        }
        already_seen.insert(search_for);
        return false;
    }

    void after_generation(const PerimeterProcessContext &params,
                          PerimeterTree &tree,
                          PerimeterNode &parent,
                          const std::vector<PerimeterNode *> &children) const override
    {
        // only on odd layers
        if (params.layer.id() % 2 == 0) {
            return;
        }
        // only do it one time per branch. If one parent is already done, stop here.
        if (check_and_set_already_seen(&params.island, &parent)) {
            return;
        }
        // do it when the last perimeter is extruded
        if(parent.perimeter_idx < parent.perimeter_needed) {
            return;
        }
        // check where we need to do it
        const ConfigOption* opt_extra_perimeters_odd_layers = params.region_config().option("extra_perimeters_odd_layers");
        if (params.region_setting.has_many_config(opt_extra_perimeters_odd_layers)) {
            ExPolygons extra_perimeter_areas;
            for (auto const &[is_extra_perimeters_odd_layers, areas] :
                 params.region_setting.get_areas(opt_extra_perimeters_odd_layers)) {
                if (is_extra_perimeters_odd_layers.get_bool()) {
                    std::vector<PerimeterNode> new_nodes;
                    for (PerimeterNode &child : parent.children) {
                        int start_idx = new_nodes.size();
                        int nb_extra_peri = PerimeterTree::split_node(params, child, new_nodes, areas.expolys);
                        if(nb_extra_peri > 0) {
                            child.perimeter_needed = 1;
                            assert(start_idx + nb_extra_peri <= new_nodes.size());
                            for(size_t idx = 0; idx < nb_extra_peri; idx++) {
                                new_nodes[start_idx + idx].perimeter_needed += 1;
                            }
                        }
                    }
                }
            }
        }
    }
};

class OnlyOnePerimeterOnTop final : public PerimeterModifier
{
protected:
    //TODO same as the perimetergenerator one
    void split_top_surfaces(const ExPolygons *lower_slices,
                                            const ExPolygons *upper_slices,
                                            const ExPolygons &orig_polygons,
                                            ExPolygons &top_fills,
                                            ExPolygons &non_top_polygons,
                                            ExPolygons &fill_clip,
                                            int peri_count,
                                            coordf_t min_width,
                                            bool use_old_algorithm_for_min_width);
public:

    void after_generation(const PerimeterProcessContext &params,
                          PerimeterTree &tree,
                          PerimeterNode &parent,
                          const std::vector<PerimeterNode *> &children) const override
    {
        // only active on first perimeter
        // 
        // Pseudo-code intent:
        //
        // top_surfaces = get_top_surfaces(parent.surface);
        // if (!top_surfaces.empty()) {
        //     for (top_surface : top_surfaces) {
        //         use parent.extrusions;
        //         append(fill_surfaces,
        //             intersection(top_surface,
        //                 offset(child.surface, ext_perimeter_spacing / 2)));
        //     }
        // }
        //
        // This hook has both sides of the operation: the parent node contains
        // the generated ring extrusions, and children contain the inner surfaces.
        // It can clamp child contour/hole counts or create fill clipping data.

        // if first periemter, and has a first perimeter, and areas inside
        if (parent.perimeter_idx > 0 || parent.extrusions.empty() || parent.perimeter_needed == 0 || parent.children.empty()) {
            return;
        }
        const ConfigOption *opt_only_one_perimeter_top = params.region_config().option("only_one_perimeter_top");
        // ensure we ahve at least a region with the option activated,
        if (!params.region_setting.has_many_config(opt_only_one_perimeter_top) &&
                    !params.region_setting.get_solo_config(opt_only_one_perimeter_top).get_bool()) {
            return;
        }
        // not sure if best here or in nodes.
        ExPolygons top_fills;
        // this one may beuseful, need to test.
        ExPolygons fill_clip;
        // we have top layer?
        if (params.island.overlaps_above.empty()) {
            // nope: stop here!
            parent.perimeter_needed = 1;
            for (auto &child : parent.children) {
                child.perimeter_needed = 1;
            }
        } else {
            //yes, check if we have a top area
            // Check if current layer has surfaces that are not covered by upper layer (i.e., top surfaces)
            ExPolygons non_top_polygons;
            for (auto const &[opt_values, areas] : params.region_setting.get_areas(opt_only_one_perimeter_top)) {
                if (opt_values.get_bool(opt_only_one_perimeter_top)) {
                    ExPolygons upper_slices;
                    for(const auto &upper_island : params.island.overlaps_above) {
                        upper_slices.push_back(upper_island.to->get_slice());
                    }
                    // has multiple or only one?s
                    if (!areas.is_accept_all()) {
                        // compute the area where the only_one_perimeter_top isn't true
                        ExPolygons cliped_upper_slices = diff_ex({params.island.get_slice()}, areas.expolys);
                        //add it to upper_slices
                        if (upper_slices.empty()) {
                            upper_slices = cliped_upper_slices;
                        } else {
                            upper_slices = union_ex(upper_slices, cliped_upper_slices);
                        }
                    }

                    ExPolygons perimeter_centerline;
                    if (non_top_polygons.empty()) {
                        perimeter_centerline = offset_ex(parent.surface, -params.external_perimeter_flow().scaled_width() / 2);
                    }else{
                        perimeter_centerline = offset_ex(non_top_polygons, -params.external_perimeter_flow().scaled_width()/ 2);
                    }
                    
                    ExPolygons lower_slices;
                    for(const auto &lower_island : params.island.overlaps_below) {
                        lower_slices.push_back(lower_island.to->get_slice());
                    }
                    split_top_surfaces(&lower_slices, &upper_slices, perimeter_centerline,
                        top_fills, non_top_polygons, fill_clip,
                        parent.perimeter_needed - 1,
                        scale_d(opt_values.get_effective_value(unscaled(params.perimeter_flow().scaled_width()),
                                                               params.region_config().option("min_width_top_surface"))),
                        opt_values.get_bool(params.region_config().option("only_one_perimeter_top_other_algo")));
                }
            }
        }
        // has to set the outer polygon to the centerline of the external perimeter
        if (top_fills.empty()) {
            // No top surfaces, no special handling needed
        } else {
            // Make sure infill not overlap with wall
            // offset the InnerContour as the result use bounds and not centerline
            ExPolygons inner_areas;
            std::vector<PerimeterNode> new_nodes;
            for (PerimeterNode &child : parent.children) {
                int start_idx = new_nodes.size();
                int nb_top = PerimeterTree::split_node(params, child, new_nodes, top_fills);
                if(nb_top > 0) {
                    child.perimeter_needed = 1;
                    assert(start_idx + nb_top <= new_nodes.size());
                    for(size_t idx = 0; idx < nb_top; idx++) {
                        new_nodes[start_idx + idx].perimeter_needed = 1;
                    }
                }
            }
            parent.children.insert(parent.children.end(), std::make_move_iterator(new_nodes.begin()),
                                    std::make_move_iterator(new_nodes.end()));
        }
    }
};

class SeparateHoleContour final : public PerimeterModifier
{
public:
    void after_generation(const PerimeterProcessContext &context,
                          PerimeterTree &tree,
                          PerimeterNode &parent,
                          const std::vector<PerimeterNode *> &children) const override
    {
        // Pseudo-code intent:
        //
        // if (hole_count != contour_count &&
        //     (hole_count < perimeter_idx || contour_count < perimeter_idx)) {
        //     mask_area = (hole_count < perimeter_idx) ?
        //         grow_contour_only(parent.surface) :
        //         grow_holes_only(parent.surface);
        //
        //     deleted_extrusions = filter(parent.extrusions, mask_area);
        //     repair child surfaces with deleted_extrusions.as_polygon(width);
        // }
        //
        // This stays as a modifier because it needs the generated extrusion and
        // the freshly created children. It can edit both before children are
        // queued for further processing.
        (void) context;
        (void) tree;
        (void) parent;
        (void) children;
    }
};

const std::vector<const PerimeterModifier *> &perimeter_modifiers()
{
    static const ExtraPerimeter extra_perimeter;
    static const OnlyOnePerimeterOnTop only_one_perimeter_on_top;
    static const SeparateHoleContour separate_hole_contour;
    static const std::vector<const PerimeterModifier *> modifiers = {
        &extra_perimeter,
        &only_one_perimeter_on_top,
        &separate_hole_contour
    };
    return modifiers;
}

void build_fill_surfaces(const PerimeterProcessContext &context, const PerimeterTree &tree)
{
    // TODO: Recreate inner_perimeter, fill_surfaces and fill_no_overlap from
    // tree.final_inner_surfaces(). This is intentionally after perimeter
    // generation because these polygons must stay coherent with what the
    // generator actually made.
    (void) context;
    (void) tree;
}

void publish_surface_result(const PerimeterProcessContext &context, const PerimeterTree &tree)
{
    // TODO: Move generated node extrusions to context.region_island, append
    // fill surfaces/fill_no_overlap to context.island, and update the perimeter
    // boundary used by avoid-crossing-perimeters.
    (void) context;
    (void) tree;
}
// for RegionSettings
const std::vector<t_config_option_keys> perimeter_keys({
    {"extra_perimeters_below_area"},
    {"extra_perimeters_count"},
    {"extra_perimeters_odd_layers"},
    {"extra_perimeters_on_overhangs"},
    {"only_one_perimeter_top", "min_width_top_surface", "only_one_perimeter_top_other_algo"},
    {"thin_walls", "thin_walls_min_width", "thin_walls_overlap"},
    {"overhangs_speed_enforce"},
    {"overhangs", "overhangs_speed", "overhangs_width_speed", "overhangs_flow_ratio", "overhangs_width"},
    {"gap_fill_enabled"},
    {"gap_fill_no_overhang"},
    {"seam_slope_type", "external_perimeters_first", "external_perimeters_first_force", "external_perimeters_nothole", "external_perimeters_hole"},
    });
// same as the perimetergenerator one
void segregate_extra_perimeters(RegionSettings &region_settings, const ExPolygon &my_srf, const LayerRegionSetCPtrs &lregions);


void process_surface(Print &print,
                     PrintObject &object,
                     Layer &layer,
                     LayerSliceIsland &island,
                     LayerRegionIsland &region_island,
                     const ExPolygon &surface)
{
    assert(!region_island.regions().empty());
    RegionSettings region_settings(region_island.regions().front()->config(), perimeter_keys);
    segregate_extra_perimeters(region_settings, surface, region_island.regions());
    PerimeterProcessContext context{print, object, layer, island, region_island, region_settings, surface};
    PerimeterTree tree(surface);
    PerimeterNode &root = tree.root();

    initialize_root_node(context, root);
    apply_perimeter_count_settings(context, root);
    apply_geometry_masks(context, root);

    const std::vector<const PerimeterModifier *> &modifiers = perimeter_modifiers();
    for (const PerimeterModifier *modifier : modifiers)
        modifier->after_root_created(context, tree, root);

    std::vector<PerimeterNode *> pending_nodes;
    pending_nodes.push_back(&root);

    while (!pending_nodes.empty()) {
        PerimeterNode *node = pending_nodes.back();
        pending_nodes.pop_back();

        if (!node->needs_more_perimeters()) {
            tree.finish_node(*node);
            continue;
        }

        for (const PerimeterModifier *modifier : modifiers)
            modifier->before_generation(context, tree, *node);

        // Generate one ring for the current node. The generator writes the ring
        // extrusion into the node and returns the inner surfaces.
        ExPolygons inner_surfaces = generate_perimeter_for_node(context, *node);

        // Inner surfaces become child nodes inheriting the parent counters.
        std::vector<PerimeterNode *> children = tree.create_children(*node, std::move(inner_surfaces));

        // Modifiers can now edit the generated parent extrusion, repair/remove
        // children, or change child counters before they are queued.
        for (const PerimeterModifier *modifier : modifiers)
            modifier->after_generation(context, tree, *node, children);

        for (PerimeterNode *child : children)
            if (!child->discarded)
                pending_nodes.push_back(child);
    }

    // Gap fill is intentionally left out of this first PerimeterGenerator2
    // sketch. It will either become a post-perimeter step or an infill-side
    // operation once the perimeter/fill boundary contract is stable.

    build_fill_surfaces(context, tree);
    publish_surface_result(context, tree);
}

} // namespace

void process(Print &print, PrintObject &object, Layer &layer, LayerSliceIsland &island)
{
    // First split the island into groups of compatible regions. This mirrors
    // the old Layer::make_perimeters responsibility, but keeps it outside the
    // actual perimeter generation algorithm.
    std::vector<LayerRegionSetCPtrs> region_groups = collect_region_groups(island, layer);

    for (const LayerRegionSetCPtrs &regions : region_groups) {
        // Create or retrieve the output node for this region group.
        LayerRegionIsland &region_island = prepare_region_island(island, regions);

        // Build the concrete expolygon surfaces that this region group will
        // process inside the current island.
        ExPolygons surfaces = build_surface_inputs(island, region_island);

        for (const ExPolygon &surface : surfaces) {
            // Process one surface independently. All setting-dependent logic is
            // routed through named helper functions inside process_surface().
            process_surface(print, object, layer, island, region_island, surface);
        }
    }
}

} // namespace Slic3r::PerimeterGenerator2
