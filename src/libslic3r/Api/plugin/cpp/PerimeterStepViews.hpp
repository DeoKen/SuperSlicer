///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Api_plugin_cpp_PerimeterStepViews_hpp_
#define slic3r_Api_plugin_cpp_PerimeterStepViews_hpp_

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "libslic3r/Api/plugin/c/steps/slic3r_step_perimeter.h"
#include "libslic3r/Api/plugin/cpp/ExtrusionViews.hpp"
#include "libslic3r/Api/plugin/cpp/RegionSettingsViews.hpp"

namespace slic3r_api {

/*
Perimeter step helpers
======================

These classes are the C++ convenience layer over the STEP_PERIMETER and
PERIMETER_GENERATION_MODULE payloads.

A perimeter generator receives a whole layer island, groups compatible regions,
then asks the host to run one group through run_region_group(). The host owns
the temporary perimeter-node tree and calls PerimeterGenerationModule plugins
while it walks that tree. Modules may edit counters, geometry and extrusion
handles through the views below, while structural edits go through callbacks
exposed by the host loop.

Typical stateless module shape:

    void after(void *, void *, perimeter_generation_context *raw_ctx, perimeter_node *raw_node)
    {
        PerimeterGenerationContextView ctx(raw_ctx);
        PerimeterNodeView node(raw_node);

        if (!node.is_last_perimeter())
            return;

        RegionSettings settings = ctx.region_settings({{"my_setting"}});
        settings.segregate(ctx.island().slice());
        ...
    }

Typical stateful module shape:

    void *start(void *, perimeter_generation_context *raw_ctx)
    {
        PerimeterGenerationContextView ctx(raw_ctx);
        return new MyTemporaryCache(ctx);
    }

    void after(void *, void *user_ctx, perimeter_generation_context *raw_ctx, perimeter_node *raw_node)
    {
        MyTemporaryCache &cache = *static_cast<MyTemporaryCache *>(user_ctx);
        ...
    }

    void end(void *, void *user_ctx, perimeter_generation_context *)
    {
        delete static_cast<MyTemporaryCache *>(user_ctx);
    }

All handles are borrowed from the host perimeter loop. Do not store these views
after the callback returns.

module_ctx is the long-lived module instance. user_ctx is optional per-tree
state returned by start() and passed back to before(), after() and end(). Use it
for temporary caches that used to require global maps keyed by perimeter nodes
or region-island handles. The host calls end() for every module whose start()
was called before the current perimeter tree is discarded.
*/

class PerimeterNodeView
{
public:
    /*
    PerimeterNodeView is only a borrowed pointer wrapper.

    It does not own the node and it does not pin the perimeter tree. The host
    perimeter loop keeps node addresses stable during module callbacks.
    Rebuilding the parent's children array is fine: this view stores the
    pointed node address, not the address of the child-array slot. Removing or
    destroying the pointed node still invalidates the view.
    */
    PerimeterNodeView() = default;
    explicit PerimeterNodeView(perimeter_node *node) : m_node(node), m_mutable(node != nullptr) {}
    explicit PerimeterNodeView(const perimeter_node *node) : m_node(node), m_mutable(false) {}

    bool valid() const { return m_node != nullptr; }
    explicit operator bool() const { return valid(); }
    bool is_mutable() const { return valid() && m_mutable; }

    const perimeter_node *handle() const {
        assert(m_node != nullptr);
        return m_node;
    }

    perimeter_node *mutable_handle() const {
        assert(is_mutable());
        return const_cast<perimeter_node *>(m_node);
    }

    bool same_handle(const PerimeterNodeView &other) const { return m_node == other.m_node; }

    /*
    Return the parent node, or an invalid view for the root.

    The returned view follows the mutability of this view. It is borrowed from
    the generator and follows the same lifetime rules as any PerimeterNodeView.
    */
    PerimeterNodeView parent() const {
        const perimeter_node *node = handle();
        if (node->parent == nullptr)
            return PerimeterNodeView();
        return m_mutable ? PerimeterNodeView(node->parent) :
                           PerimeterNodeView(static_cast<const perimeter_node *>(node->parent));
    }

    /*
    Read the current direct child count from the host-owned C node.

    This value is a live read. If a structural helper splits/appends/removes
    children, call child_count() again instead of caching it.
    */
    uint32_t child_count() const { return handle()->child_count; }

    /*
    Borrow one direct child view by index.

    The child array belongs to the host perimeter loop. Do not keep indexes across
    structural edits: after split/append/remove, the same index may refer to a
    different child or be out of range.
    */
    PerimeterNodeView child(uint32_t idx) const {
        const perimeter_node *node = handle();
        assert(idx < node->child_count);
        assert(node->children != nullptr);
        perimeter_node *child_node = node->children[idx];
        return m_mutable ? PerimeterNodeView(child_node) :
                           PerimeterNodeView(static_cast<const perimeter_node *>(child_node));
    }

    /*
    Copy the current child node pointers into a std::vector of views.

    This is the safe way to iterate when the loop may call a structural helper
    such as split_node(). If the generator reallocates parent->children, the
    snapshot is still usable because each view stores the child node pointer,
    not a pointer into the child array.

    The snapshot does not protect against explicit destruction of a child node.
    A generator callback that removes nodes must document whether snapshots
    remain usable. The current perimeter split contract keeps the original node
    and returns newly created inside nodes separately.
    */
    std::vector<PerimeterNodeView> children_snapshot() const {
        std::vector<PerimeterNodeView> out;
        out.reserve(child_count());
        for (uint32_t idx = 0; idx < child_count(); ++idx)
            out.push_back(child(idx));
        return out;
    }

    uint32_t perimeter_idx() const { return handle()->perimeter_idx; }
    uint32_t perimeter_needed() const { return handle()->perimeter_needed; }

    bool needs_more_perimeters() const {
        return perimeter_needed() > 0 && perimeter_idx() < perimeter_needed();
    }

    bool is_last_perimeter() const {
        return perimeter_needed() == 0 || perimeter_idx() + 1 >= perimeter_needed();
    }

    void set_perimeter_needed(uint32_t value) const {
        mutable_handle()->perimeter_needed = value;
    }

    void add_perimeters(uint32_t count) const {
        if (count == 0)
            return;
        mutable_handle()->perimeter_needed += count;
    }

    /*
    Ask the generator to process this node once more at its current depth.

    This is useful after a split. A child can already have
    perimeter_idx == perimeter_needed, which means the generator considers this
    branch complete. Bumping perimeter_needed to perimeter_idx + 1 makes the
    generator create one additional perimeter for that branch.
    */
    void request_current_perimeter() const {
        perimeter_node *node = mutable_handle();
        node->perimeter_needed = std::max(node->perimeter_needed, node->perimeter_idx + 1);
    }

    /*
    Surface available for this node's next perimeter.

    The returned ExPolygon is a borrowed view over the host-owned node
    payload. It must not be stored after the current callback.
    */
    ExPolygon surface() const {
        assert(handle()->surface != nullptr);
        return ExPolygon(handle()->surface);
    }

    /*
    Fill clipping surface associated with this node.

    It may be larger than surface so fill can anchor into already generated
    perimeter material. Like surface(), this is a borrowed view.
    */
    ExPolygon fill_surface() const {
        assert(handle()->fill_surface != nullptr);
        return ExPolygon(handle()->fill_surface);
    }

    /*
    Mutable extrusion entity for the perimeter already generated on this node.

    Modules called from before() may see an empty extrusion. Modules called
    from after() usually see the freshly generated perimeter extrusion.
    */
    MutableExtrusionEntity extrusions() const {
        assert(handle()->extrusions != nullptr);
        return MutableExtrusionEntity(mutable_handle()->extrusions);
    }

private:
    const perimeter_node *m_node = nullptr;
    bool m_mutable = false;
};

class PerimeterGenerationContextView
{
public:
    /*
    Borrowed view over the active perimeter-generation callback context.

    The context points both to host objects (print/object/layer/island) and to
    host-owned temporary nodes. Use it only during the module callback that
    received it.
    */
    explicit PerimeterGenerationContextView(perimeter_generation_context *context) : m_context(context) {
        assert(m_context != nullptr);
    }

    perimeter_generation_context *mutable_handle() const {
        assert(m_context != nullptr);
        return m_context;
    }

    const perimeter_generation_context *handle() const {
        assert(m_context != nullptr);
        return m_context;
    }

    plugin_run_context *run_context() const { return mutable_handle()->run_ctx; }

    /*
    Storage associated with the currently running plugin.

    Temporary geometry built by helper classes such as RegionSettings and
    ClipperContext is allocated here and is released by the plugin storage
    lifetime rules.
    */
    storage_handle *storage() const {
        plugin_run_context *ctx = run_context();
        assert(ctx != nullptr);
        assert(ctx->plugin_storage != nullptr);
        return ctx->plugin_storage;
    }

    Print print() const { return Print(handle()->print); }
    Object object() const { return Object(handle()->object); }
    Layer layer() const { return Layer(handle()->layer); }
    LayerIsland island() const { return LayerIsland(handle()->island); }
    LayerRegionIsland region_island() const { return LayerRegionIsland(handle()->region_island); }

    /*
    Root of the host-owned perimeter tree for the current surface.

    Modules usually edit root counters in start(), and inspect/edit individual
    nodes in before()/after().
    */
    PerimeterNodeView root() const { return PerimeterNodeView(mutable_handle()->root); }

    /*
    Return the layer index inside the current object, or uint32_t(-1) if the
    context layer does not belong to that object.

    The C payload intentionally passes handles, not indexes, because layer
    ordering is owned by the host data tree. This helper performs the lookup for
    module code that still needs parity or layer-number logic.
    */
    uint32_t layer_id_from_object() const {
        const Object object_view = object();
        const Layer current_layer = layer();
        const uint32_t layer_count = object_view.layer_count();
        for (uint32_t layer_idx = 0; layer_idx < layer_count; ++layer_idx)
            if (object_view.layer(layer_idx).same_handle(current_layer))
                return layer_idx;
        return uint32_t(-1);
    }

    RegionSettings region_settings(
        std::initializer_list<std::initializer_list<const char *>> option_groups) const
    {
        return RegionSettings(storage(), island(), option_groups);
    }

    /*
    Flow used as the perimeter reference for modules that need a physical scale.

    The perimeter generator may later expose a more exact per-region flow in
    the C payload. Until then this mirrors the current RegionSettings helper
    convention: use the first region attached to the processed island as the
    default reference, then split by RegionSettings only when a setting value
    actually differs between regions.
    */
    c_flow perimeter_flow() const {
        const LayerIsland island_view = island();
        assert(island_view.region_count() > 0);
        return island_view.region(0).flow(RAW_EXTRUSION_ROLE_INTERNAL_PERIMETER);
    }

    /*
    Split node with clip using the host-owned callback.

    The returned views are the exact inside parts reported by the generator.
    They are copied out of the temporary C span immediately, so callers do not
    need to know whether the generator inserted siblings next to the original
    node or appended them at the end of the parent child array.
    */
    std::vector<PerimeterNodeView> split_node(const PerimeterNodeView &node, const RegionSettingsClip &clip) const {
        std::vector<PerimeterNodeView> inside_nodes;
        if (!node.valid())
            return inside_nodes;
        if (clip.is_accept_all()) {
            inside_nodes.push_back(node);
            return inside_nodes;
        }
        perimeter_generation_context *context = mutable_handle();
        if (context->split_node == nullptr)
            return inside_nodes;

        perimeter_node_span span = {};
        context->split_node(context, node.mutable_handle(), clip.handle_or_null(), &span);
        inside_nodes.reserve(span.count);
        for (uint32_t idx = 0; idx < span.count; ++idx)
            if (span.items != nullptr && span.items[idx] != nullptr)
                inside_nodes.push_back(PerimeterNodeView(span.items[idx]));
        return inside_nodes;
    }

    /*
    Ask the generator to replace a node's children from a new surface list.

    Modules use this after deleting or changing a generated perimeter class:
    the old child surfaces were computed from the previous geometry and may no
    longer represent the space available for the next ring. Generators that do
    not maintain a real node tree may leave rebuild_children null; in that case
    this helper is a no-op and returns false.
    */
    bool rebuild_children(const PerimeterNodeView &node,
                          const ExPolygonCollection &surfaces,
                          const ExPolygonCollection &fill_surfaces) const {
        if (!node.valid())
            return false;

        perimeter_generation_context *context = mutable_handle();
        if (context->rebuild_children == nullptr)
            return false;

        context->rebuild_children(context, node.mutable_handle(), surfaces.handle(), fill_surfaces.handle());
        return true;
    }

private:
    perimeter_generation_context *m_context = nullptr;
};

} // namespace slic3r_api

#endif // slic3r_Api_plugin_cpp_PerimeterStepViews_hpp_
