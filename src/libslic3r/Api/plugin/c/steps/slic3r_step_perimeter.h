///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_step_perimeter_h_
#define slic3r_step_perimeter_h_

#include "slic3r_step_common.h"
#include "../slic3r_extrusion_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
Payload for STEP_PERIMETER.

This step generates perimeters for one layer island of one object.

The input island may intersect several layer regions. A perimeter plugin should
decide which regions can be processed together, usually by grouping regions
whose perimeter-relevant configuration is compatible. For each group, the
plugin asks the host for a region-island output node, then writes the perimeter
result into that node.

Normal plugin usage:

1. Cast the generic plugin_run_context with plugin_ctx_as_generate_perimeter().
   If it returns NULL, the current run is not STEP_PERIMETER.

2. Read the object/layer/island inputs from print, object, layer and island.
   These handles are borrowed from the host and are valid only for the current
   callback.

3. Inspect the layer regions attached to the island and build one or more
   region groups. Each group is represented by an array of layer_region_handle
   pointers. The array order has no semantic meaning; the group is treated as a
   set.

4. For each group, call get_or_create_region_island(island, regions, count).
   The returned handle is the output node for that island/region set.

5. Generate perimeter extrusions and fill surfaces for the returned
   region-island. The plugin may use any internal algorithm; the host only sees
   the published output handles.

6. Publish results with set_region_island_extrusion(),
   set_region_island_fill_surfaces() and
   set_region_island_fill_no_overlap_surfaces(). These callbacks move content
   from plugin-owned handles into the layer data tree. The source handles remain
   valid for the plugin, but their content should be considered empty or
   otherwise unspecified after the call.

The print/object/layer/island handles are processing context. Do not store them
for another run. If a plugin needs persistent data, store it in plugin storage
or in data owned by the plugin instance.
*/
typedef layer_region_island_handle *(*perimeter_get_or_create_region_island_fn)(
    layer_island_handle *island,
    const layer_region_handle *const *regions,
    uint32_t region_count);

/*
Move one extrusion entity into a layer region island.

role selects which extrusion bucket is replaced, for example perimeter or gap
fill. Passing extrusion == NULL clears the bucket for that role. Returns
non-zero on success.
*/
typedef int32_t (*perimeter_set_region_island_extrusion_fn)(
    layer_region_island_handle *region_island,
    raw_extrusion_role role,
    extrusion_entity_handle *extrusion);

/*
Move a surface collection into a layer region island.

Passing surfaces == NULL clears the destination collection. The same callback
shape is used for normal fill surfaces and non-encroaching/no-overlap fill
surfaces; the field name in run_ctx_generate_perimeter selects the destination.
Returns non-zero on success.
*/
typedef int32_t (*perimeter_set_region_island_surfaces_fn)(
    layer_region_island_handle *region_island,
    surface_collection_handle *surfaces);

typedef struct run_ctx_generate_perimeter {
    /*
    Borrowed print and object currently being processed.
    */
    print_handle *print;
    object_handle *object;

    /*
    Borrowed layer and island currently being processed.
    */
    layer_handle *layer;
    layer_island_handle *island;

    /*
    Return the region-island output node for island and a set of regions,
    creating it when necessary.
    */
    perimeter_get_or_create_region_island_fn get_or_create_region_island;

    /*
    Publish perimeter/gap-fill/other extrusions into a region island.
    */
    perimeter_set_region_island_extrusion_fn set_region_island_extrusion;

    /*
    Publish fill surfaces and non-encroaching/no-overlap fill surfaces into a
    region island.
    */
    perimeter_set_region_island_surfaces_fn set_region_island_fill_surfaces;
    perimeter_set_region_island_surfaces_fn set_region_island_fill_no_overlap_surfaces;
} run_ctx_generate_perimeter;

/*
Perimeter generation node.

This is the C view of the working tree used by a perimeter generator while it
creates perimeter rings for one island/region surface.

The root node represents the initial surface. Each generated ring is written to
node->extrusions, and each remaining inner surface becomes one child node. A
module can inspect or edit the current node before/after the generator creates
one ring.

Lifetime rules:
- all pointers are borrowed from the perimeter generator;
- do not free surface, fill_surface, extrusions, children, or child nodes;
- pointers are valid only during the current perimeter-generation callback;
- child array storage may change when helper layers add/remove/split children.

Low-level C code may read and update the scalar counters directly. Structural
edits such as splitting nodes, appending children, or rebuilding child arrays
are intentionally left to C++/Python helper layers so this ABI stays compact.
*/
typedef struct perimeter_node {
    /*
    Parent node, or NULL for the root.
    */
    struct perimeter_node *parent;

    /*
    Area available for the next perimeter ring. The handle is mutable so modules
    can refine the current node surface before the next ring is generated.
    */
    expolygon_handle *surface;

    /*
    Area later used as fill clipping/anchoring support. It is often equal to
    surface, but modifiers may keep it larger so infill can anchor into already
    generated perimeters.
    */
    expolygon_handle *fill_surface;

    /*
    Extrusion entity owned by this node. After a ring is generated this usually
    contains the perimeter extrusion(s) for the node. Modules may edit it, for
    example to tag overhangs, remove gap fill on overhangs, or mark scarf seams.
    */
    extrusion_entity_handle *extrusions;

    /*
    Direct child nodes. The array is borrowed and contains child_count entries.
    Treat it as read-only unless a helper explicitly documents that it owns the
    structural mutation it performs.
    */
    struct perimeter_node **children;
    uint32_t child_count;

    /*
    Index of the perimeter ring represented by this node, starting at zero for
    the root outer ring.
    */
    uint32_t perimeter_idx;

    /*
    Total number of perimeter rings requested for this branch. Modules may
    increase or decrease it to ask the generator to continue or stop a branch.
    */
    uint32_t perimeter_needed;
} perimeter_node;

/*
Context shared by PerimeterGenerationModule callbacks.

A PerimeterGenerationModule is not a full slicing step. It is a service module
created by a STEP_PERIMETER plugin and called by that generator while it walks
its perimeter-node tree. The module receives the same object/layer/island
context as the generator plus the root node of the current surface.

Use run_ctx for cancellation/progress/error callbacks and plugin storage. It is
the same common run context shape used by normal plugins, but it belongs to the
perimeter generator call that is currently invoking the module.
*/
typedef struct perimeter_generation_context {
    plugin_run_context *run_ctx;
    print_handle *print;
    object_handle *object;
    layer_handle *layer;
    layer_island_handle *island;
    layer_region_island_handle *region_island;
    perimeter_node *root;
} perimeter_generation_context;

typedef struct perimeter_generation_module_vtable perimeter_generation_module_vtable;

/*
Created module instance.

The ctx pointer is owned by the module provider. PerimeterGenerationModule
instances are long-lived singletons registered/initialized at startup. The
perimeter generator only borrows them and must not free them.
*/
typedef struct perimeter_generation_module_instance {
    void *ctx;
    const perimeter_generation_module_vtable *vt;
} perimeter_generation_module_instance;

typedef void (*perimeter_generation_module_start_end_fn)(void *module_ctx,
                                                         perimeter_generation_context *context);
typedef void (*perimeter_generation_module_node_fn)(void *module_ctx,
                                                    perimeter_generation_context *context,
                                                    perimeter_node *node);

/*
Function table for a PerimeterGenerationModule.

Call order for one generated surface:

1. start(context)
   Called once after the root node has been initialized and before any node is
   processed. Use it to initialize per-surface caches or edit root counters.

2. before(context, node)
   Called before the perimeter generator creates one ring for node.

3. after(context, node)
   Called after the generator has written node->extrusions and created/updated
   node children for the inner surfaces.

4. end(context)
   Called once after every pending node has been processed, before the generator
   publishes results back into the layer data tree.

before/after may be called many times. start/end are called exactly once for the
surface if generation starts normally. If generation is aborted after start(),
the host should still call end() when it can do so safely.
*/
struct perimeter_generation_module_vtable {
    perimeter_generation_module_start_end_fn start;
    perimeter_generation_module_node_fn before;
    perimeter_generation_module_node_fn after;
    perimeter_generation_module_start_end_fn end;
};

/*
Payload for PERIMETER_GENERATION_MODULE.

This service plugin type creates a PerimeterGenerationModule instance. The
provider fills module.ctx and module.vt during run(). The STEP_PERIMETER plugin
then calls the module callbacks while generating perimeters.
*/
typedef struct run_ctx_perimeter_generation_module {
    perimeter_generation_module_instance module;
} run_ctx_perimeter_generation_module;

static inline const run_ctx_generate_perimeter *
plugin_ctx_as_generate_perimeter(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != STEP_PERIMETER)
        return NULL;
    return (const run_ctx_generate_perimeter *)ctx->data;
}

static inline run_ctx_perimeter_generation_module *
plugin_ctx_as_perimeter_generation_module(const plugin_run_context *ctx)
{
    if (!ctx || ctx->step != PERIMETER_GENERATION_MODULE)
        return NULL;
    return (run_ctx_perimeter_generation_module *)ctx->data;
}

#ifdef __cplusplus
}
#endif

#endif // slic3r_step_perimeter_h_
