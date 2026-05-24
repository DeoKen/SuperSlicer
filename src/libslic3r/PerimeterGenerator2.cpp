///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "PerimeterGenerator2.hpp"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
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
#include "libslic3r/PrintRegion.hpp"
#include "libslic3r/RegionSettings.hpp"

namespace Slic3r::PerimeterGenerator2 {
namespace {

struct PerimeterProcessContext;
struct PerimeterNode;
using PerimeterNodePtr = std::unique_ptr<PerimeterNode>;

// do not add algo-specific field inside this generic node struture.
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
    // unique_ptr to have stable pointers
    std::vector<PerimeterNodePtr> children;
    // my index in perimeter loop count
    size_t perimeter_idx = 0;
    // number of perimeters loops to construct in total
    size_t perimeter_needed = 0;

    bool needs_more_perimeters() const { return perimeter_needed > 0 && perimeter_idx < size_t(perimeter_needed); }

    bool is_last_perimeter() { return perimeter_needed <= 0 || perimeter_idx + 1 >= perimeter_needed; }

    void append_new_children(std::vector<PerimeterNodePtr> &&new_nodes) {
        for (PerimeterNodePtr &node : new_nodes)
            node->parent = this;
        this->children.insert(this->children.end(), std::make_move_iterator(new_nodes.begin()),
                               std::make_move_iterator(new_nodes.end()));
    }
};

class PerimeterTree
{
public:
    explicit PerimeterTree(const ExPolygon &root_surface)
    {
        m_root.surface = root_surface;
        m_root.fill_surface = root_surface;
    }

    PerimeterNode &root() { return m_root; }
    const PerimeterNode &root() const { return m_root; }
    const ExPolygons &final_inner_surfaces() const { 
        //TODO: union all leaf surfaces.
        return {};
    }
    const ExPolygons &final_fill_clip_areas() const { 
        //TODO: union all leaf fill surfaces.
        return {};
    }

    std::vector<PerimeterNode *> create_children(PerimeterNode &parent, ExPolygons &&inner_surfaces)
    {
        std::vector<PerimeterNode *> child_nodes;
        if (inner_surfaces.empty())
            return child_nodes;

        child_nodes.reserve(inner_surfaces.size());
        parent.children.reserve(parent.children.size() + inner_surfaces.size());

        for (ExPolygon &inner_surface : inner_surfaces) {
            parent.children.push_back(std::make_unique<PerimeterNode>());
            PerimeterNode &child = *parent.children.back();
            child.parent = &parent;
            child.surface = std::move(inner_surface);
            child.fill_surface = child.surface;
            child.perimeter_idx = parent.perimeter_idx + 1;
            child.perimeter_needed = parent.perimeter_needed;
            child.extra_perimeter_count_applied = parent.extra_perimeter_count_applied;
            child_nodes.push_back(&child);
        }

        return child_nodes;
    }

    static void set_new_child(const PerimeterProcessContext &params,
                   PerimeterNode &node,
                   ExPolygon &&surface,
                   const ExPolygons &fill_clip) {
        node.surface = std::move(surface);
        if (node.fill_surface.empty())
            node.fill_surface = node.surface;
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
                            std::vector<PerimeterNodePtr> &new_nodes,
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
                PerimeterTree::set_new_child(params, *new_nodes.back(), std::move(srf_yes[i]), srf_fill_yes);
            }
            // top areas
            new_nodes.emplace_back(to_split);
            PerimeterTree::set_new_child(params, *new_nodes.back(), std::move(srf_no[0]), srf_fill_no);
            for (size_t i = 1; i < srf_no.size(); i++) {
                new_nodes.emplace_back(to_split);
                PerimeterTree::set_new_child(params, *new_nodes.back(), std::move(srf_no[i]), srf_fill_no);
            }
            return srf_yes.size();
    }

private:
    PerimeterNode m_root;
};

//TODO: makes it immutable and create caches for config & flow
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

const PrintRegionConfig& PerimeterProcessContext::region_config() const
{
    assert(!region_island.regions().empty());
    return (*region_island.regions().begin())->region().config();
}

Flow PerimeterProcessContext::perimeter_flow() const
{
    assert(!region_island.regions().empty());
    return (*region_island.regions().begin())->flow(frPerimeter);
}

Flow PerimeterProcessContext::external_perimeter_flow() const
{
    assert(!region_island.regions().empty());
    return (*region_island.regions().begin())->flow(frExternalPerimeter);
}

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

    // init
    virtual void start_generation(const PerimeterProcessContext &context,
                                    PerimeterTree &tree) const
    {
        (void) context;
        (void) tree;
    }

    // before creating extrusion & childs of node
    virtual void before_generation(const PerimeterProcessContext &context,
                                   PerimeterTree &tree,
                                   PerimeterNode &node) const
    {
        (void) context;
        (void) tree;
        (void) node;
    }

    // after creating extrusion & childs of parent
    virtual void after_generation(const PerimeterProcessContext &context,
                                  PerimeterTree &tree,
                                  PerimeterNode &parent) const
    {
        (void) context;
        (void) tree;
        (void) parent;
    }

    //end of perimeter generation for context's region_island
    virtual void finish_generation(const PerimeterProcessContext &context,
                                  PerimeterTree &tree) const
    {
        (void) context;
        (void) tree;
    }
};
template<class DATA_TYPE>
class PerimeterModifierWithNodeData : public PerimeterModifier
{
protected:
    // map of layerslice island to be sure we can be used in parallel.
    mutable std::mutex m_mutex;
    mutable std::map<const LayerRegionIsland *, std::map<const PerimeterNode *, DATA_TYPE>> m_nodes_with_extra_perimeter;

    const DATA_TYPE& get_data(LayerRegionIsland *island, const PerimeterNode *node) const {
        std::lock_guard lock(m_mutex);
        return m_nodes_with_extra_perimeter[island][node];
    }

    void set_data(LayerRegionIsland *island, const PerimeterNode *node, const DATA_TYPE &data) {
        std::lock_guard lock(m_mutex);
        m_nodes_with_extra_perimeter[island][node] = data;
    }

    void finish_generation(const PerimeterProcessContext &context, PerimeterTree &tree) const override {
        std::lock_guard lock(m_mutex);
        m_nodes_with_extra_perimeter.erase(&context.region_island);
    }
};

// inspired by PeriemterGenerator.cpp lines 3551->3609
class ExtraPerimeterCount final : public PerimeterModifierWithNodeData<int>
{
public:
    void start_generation(const PerimeterProcessContext &params,
                            PerimeterTree &tree) const override
    {
        // solo config code path
        const ConfigOption *opt_extra_perimeters_count = params.region_config().option("extra_perimeters_count");
        if (params.region_setting.has_many_config(opt_extra_perimeters_count))
            return;
        const int extra_perimeters_count = params.region_setting.get_solo_config(opt_extra_perimeters_count).get_int();
        if (extra_perimeters_count > 0) {
            tree.root().perimeter_needed += extra_perimeters_count;
        }
    }

    void after_generation(const PerimeterProcessContext &params,
                          PerimeterTree &tree,
                          PerimeterNode &parent) const override {
        (void) tree;
        // many config code path
        if (parent.children.empty()) {
            return;
        }

        const ConfigOption *opt_extra_perimeters_count = params.region_config().option("extra_perimeters_count");
        if(!params.region_setting.has_many_config(opt_extra_perimeters_count)) {
                return;
        }
        std::vector<PerimeterNodePtr> new_nodes;
        const int already_extruded_extra = this->get_data(&params.region_island, &parent);
        // iterate per child
        for (PerimeterNodePtr &child : parent.children) {
            // if no more perimeters, we can add more!
            if (child->needs_more_perimeters())
                return;


            ExPolygons extra_perimeter_clip_area;
            for (auto const &[extra_perimeters_count_settings, clip] :
                 params.region_setting.get_areas(opt_extra_perimeters_count)) {
                const int extra_perimeters_count = extra_perimeters_count_settings.get_int(opt_extra_perimeters_count);
                if (already_extruded_extra >= extra_perimeters_count) {
                    continue;
                }
                append(extra_perimeter_clip_area, clip.expolys);
            }
            extra_perimeter_clip_area = union_ex(extra_perimeter_clip_area);

            const size_t start_idx = new_nodes.size();
            const int nb_inside = PerimeterTree::split_node(params, *child, new_nodes, extra_perimeter_clip_area);

            if (nb_inside <= 0)
                continue;

            child->perimeter_needed++;
            this->set_data(&params.region_island, child.get(), already_extruded_extra + 1);
            assert(start_idx + size_t(nb_inside - 1) <= new_nodes.size());
            for (size_t idx = 0; idx < size_t(nb_inside - 1); ++idx) {
                new_nodes[start_idx + idx]->perimeter_needed++;
                this->set_data(&params.region_island, new_nodes[start_idx + idx].get(), already_extruded_extra + 1);
            }
        }
        if (!new_nodes.empty()) {
            parent.append_new_children(std::move(new_nodes));
        }
    }
};
    
class ExtraPerimeterModifier : public PerimeterModifier
{
protected:
    // map of layerslice island to be sure we can be used in parallel.
    mutable std::mutex m_mutex;
    mutable std::map<const LayerRegionIsland *, std::set<const PerimeterNode *>> m_nodes_with_extra_perimeter;
    bool check_and_set_already_seen(LayerRegionIsland *island, const PerimeterNode *search_for) const {
        std::lock_guard lock(m_mutex);
        auto it = m_nodes_with_extra_perimeter.find(island);
        if (it == m_nodes_with_extra_perimeter.end()) {
            return false;
        }
        std::set<const PerimeterNode *> &already_seen = it->second;
        const PerimeterNode *current = search_for;
        while (current) {
            if (already_seen.find(current) != already_seen.end()) {
                return true;
            }
            current = current->parent == current ? nullptr : current->parent;
        }
        already_seen.insert(search_for);
        return false;
    }
    void finish_generation(const PerimeterProcessContext &context, PerimeterTree &tree) const override {
        std::lock_guard lock(m_mutex);
        m_nodes_with_extra_perimeter.erase(&context.region_island);
    }
};

class ExtraPerimeterBelowArea final : public ExtraPerimeterModifier
{
public:

    void after_generation(const PerimeterProcessContext &params,
                          PerimeterTree &tree,
                          PerimeterNode &parent) const override
    {
        std::vector<PerimeterNodePtr> new_nodes;
        for (PerimeterNodePtr &child : parent.children) {
            // add extra when it's the end, after all periemters are already extruded
            if(child->needs_more_perimeters()) {
                continue;
            }
            // only do it one time per branch (more of an optimisation, it will be exluded again anyway)
            if (check_and_set_already_seen(&params.region_island, &parent)) {
                continue;
            }

            const ConfigOption *opt_extra_perimeters_below_area = params.region_config().option("extra_perimeters_below_area");
            if (!params.region_setting.has_many_config(opt_extra_perimeters_below_area) &&
                params.region_setting.get_solo_config(opt_extra_perimeters_below_area).get_float() <= 0) {
                continue;
            }

            for (auto const &[extra_perimeters_below_area, clip] :
                 params.region_setting.get_areas(opt_extra_perimeters_below_area)) {
                // use only areas with useful min value
                if (extra_perimeters_below_area.get_float(opt_extra_perimeters_below_area) <= 0) {
                    continue;
                }

                const double area_mm2 = extra_perimeters_below_area.get_effective_value(sqr(params.perimeter_flow().width()));
                const double area_scaled = scale_d(scale_d(area_mm2));

                // if clip.empty - solo config
                if (clip.is_accept_all()) {
                    if (child->surface.area() < area_scaled) {
                        child->perimeter_needed += 9999;
                    }
                    continue;
                }
                assert(!clip.empty());

                const size_t start_idx = new_nodes.size();
                const int nb_inside = PerimeterTree::split_node(params, *child, new_nodes, clip.expolys);
                if (nb_inside <= 0)
                    continue;

                if (child->surface.area() < area_scaled) {
                    child->perimeter_needed += 9999;
                }
                assert(start_idx + size_t(nb_inside - 1) <= new_nodes.size());
                for (size_t idx = 0; idx < size_t(nb_inside - 1); ++idx) {
                    if (new_nodes[start_idx + idx]->surface.area() < area_scaled) {
                        new_nodes[start_idx + idx]->perimeter_needed += 9999;
                    }
                }
            }
        }
        if (!new_nodes.empty()) {
            parent.append_new_children(std::move(new_nodes));
        }
    }
};

// inspired from PeriemterGenerator.cpp lines 3660->3694
class ExtraPerimeterOddLayer final : public ExtraPerimeterModifier
{
public:
    //map of layerslice island to be sure we can be used in parallel.
    void start_generation(const PerimeterProcessContext &params,
                            PerimeterTree &tree) const override {
        // == solo_config code path ==
        // only on odd layers
        if (params.layer.id() % 2 == 0) {
            return;
        }
        if (params.region_setting.get_solo_config(params.region_config().option("extra_perimeters_odd_layers")).get_bool()) {
            tree.root().perimeter_needed += 1;
        }
    }

    void after_generation(const PerimeterProcessContext &params,
                          PerimeterTree &tree,
                          PerimeterNode &parent) const override
    {
        // == many_config code path ==
        // only on odd layers
        if (params.layer.id() % 2 == 0) {
            return;
        }
        // do it when the last perimeter is extruded
        if(!parent.is_last_perimeter()) {
            return;
        }
        // only do it one time per branch. If one parent is already done, stop here.
        if (check_and_set_already_seen(&params.region_island, &parent)) {
            return;
        }
        // check where we need to do it
        const ConfigOption* opt_extra_perimeters_odd_layers = params.region_config().option("extra_perimeters_odd_layers");
        if (params.region_setting.has_many_config(opt_extra_perimeters_odd_layers)) {
            ExPolygons extra_perimeter_areas;
            for (auto const &[is_extra_perimeters_odd_layers, areas] :
                 params.region_setting.get_areas(opt_extra_perimeters_odd_layers)) {
                if (is_extra_perimeters_odd_layers.get_bool()) {
                    std::vector<PerimeterNodePtr> new_nodes;
                    for (PerimeterNodePtr &child : parent.children) {
                        int start_idx = new_nodes.size();
                        int nb_extra_peri = PerimeterTree::split_node(params, *child, new_nodes, areas.expolys);
                        if(nb_extra_peri > 0) {
                            child->perimeter_needed = 1;
                            assert(start_idx + nb_extra_peri - 1 <= new_nodes.size());
                            for(size_t idx = 0; idx < size_t(nb_extra_peri - 1); idx++) {
                                new_nodes[start_idx + idx]->perimeter_needed += 1;
                            }
                        }
                    }
                    parent.append_new_children(std::move(new_nodes));
                }
            }
        }
    }
};

// inspired from PeriemterGenerator.cpp lines 3409->3462
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
                          PerimeterNode &parent) const override
    {
        // if first perimeter, and has a first perimeter, and areas inside
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
            for (PerimeterNodePtr &child : parent.children) {
                child->perimeter_needed = 1;
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
            std::vector<PerimeterNodePtr> new_nodes;
            for (PerimeterNodePtr &child : parent.children) {
                int start_idx = new_nodes.size();
                int nb_top = PerimeterTree::split_node(params, *child, new_nodes, top_fills);
                if(nb_top > 0) {
                    child->perimeter_needed = 1;
                    assert(start_idx + nb_top - 1 <= new_nodes.size());
                    for(size_t idx = 0; idx < size_t(nb_top - 1); idx++) {
                        new_nodes[start_idx + idx]->perimeter_needed = 1;
                    }
                }
            }
            parent.append_new_children(std::move(new_nodes));
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
    static const ExtraPerimeterCount extra_perimeter_count;
    static const ExtraPerimeterBelowArea extra_perimeter_below_area;
    static const ExtraPerimeterOddLayer extra_perimeter_odd_layer;
    static const OnlyOnePerimeterOnTop only_one_perimeter_on_top;
    static const SeparateHoleContour separate_hole_contour;
    static const std::vector<const PerimeterModifier *> modifiers = {
        &only_one_perimeter_on_top,
        &extra_perimeter_count,
        &extra_perimeter_below_area,
        &extra_perimeter_odd_layer,
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
    RegionSettings region_settings((*region_island.regions().begin())->config(), perimeter_keys);
    segregate_extra_perimeters(region_settings, surface, region_island.regions());
    PerimeterProcessContext context{print, object, layer, island, region_island, region_settings, surface};
    PerimeterTree tree(surface);
    PerimeterNode &root = tree.root();

    initialize_root_node(context, root);
    apply_perimeter_count_settings(context, root);
    apply_geometry_masks(context, root);

    const std::vector<const PerimeterModifier *> &modifiers = perimeter_modifiers();
    for (const PerimeterModifier *modifier : modifiers)
        modifier->start_generation(context, tree);

    std::vector<PerimeterNode *> pending_nodes;
    pending_nodes.push_back(&root);

    while (!pending_nodes.empty()) {
        PerimeterNode *node = pending_nodes.back();
        pending_nodes.pop_back();

        if (!node->needs_more_perimeters()) {
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
            modifier->after_generation(context, tree, *node);

        for (PerimeterNodePtr &child : node->children)
            pending_nodes.push_back(child.get());
    }

    // Gap fill is intentionally left out of this first PerimeterGenerator2
    // sketch. It will either become a post-perimeter step or an infill-side
    // operation once the perimeter/fill boundary contract is stable.
    
    for (const PerimeterModifier *modifier : modifiers)
        modifier->finish_generation(context, tree);

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
