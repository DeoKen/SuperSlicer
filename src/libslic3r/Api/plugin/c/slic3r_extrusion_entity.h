///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_extrusion_entity_h_
#define slic3r_extrusion_entity_h_

#include <stdint.h>

#include "slic3r_extrusion_polyline.h"
#include "slic3r_extrusion_property.h"
#include "slic3r_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
Extrusion entity API.

An extrusion entity is a tree node. It may contain:
    - no local polyline and no children;
    - one local polyline and no children;
    - children and no local polyline.

Children are ordered. If an entity is marked continuous, the children are
expected to form one continuous path. If an entity is marked sortable, the host
may reorder its children during path planning.

Use the polyline API to edit local points/segments. Use the property API to
describe how an entity or its descendants should be interpreted.
*/

typedef struct extrusion_entity extrusion_entity;

#ifndef EXTRUSION_INDEX_INVALID
#define EXTRUSION_INDEX_INVALID ((uint32_t)UINT32_MAX)
#endif

/*
Entity flags.

REVERSIBLE means the entity may be reversed by algorithms that optimize travel.
SORTABLE means the entity's children may be reordered. It is meaningful only for
non-continuous child collections.
CONTINUOUS means child order forms a single continuous extrusion path.
*/
#define RAW_EXTRUSION_FLAG_REVERSIBLE ((uint32_t)(1u << 0))
#define RAW_EXTRUSION_FLAG_SORTABLE   ((uint32_t)(1u << 1))
#define RAW_EXTRUSION_FLAG_CONTINUOUS ((uint32_t)(1u << 2))

/* Create an empty extrusion entity owned by storage. Release it with storage_free(). */
extrusion_entity *extrusion_create_empty(storage_handle *storage);

/* Create a deep copy of src owned by storage. Release it with storage_free(). */
extrusion_entity *extrusion_clone(storage_handle *storage, const extrusion_entity *src);

/*
Replace dst with a deep copy of src.

dst keeps its handle identity, but its content, properties, flags, children and
stored data are replaced.
*/
int32_t extrusion_copy_from(extrusion_entity *dst, const extrusion_entity *src);

/*
Move src into dst.

dst keeps its handle identity. src remains valid but becomes empty afterwards.
*/
int32_t extrusion_move_from(extrusion_entity *dst, extrusion_entity *src);

/* Remove local polyline and all children. Properties and flags are unchanged. */
int32_t extrusion_clear_content(extrusion_entity *entity);

/* Return the current entity flags bitset. Returns 0 for NULL. */
uint32_t extrusion_flags(const extrusion_entity *entity);

/*
Set mutable entity flags.

The host may reject incoherent combinations, for example SORTABLE on a
continuous entity. Returns non-zero on success.
*/
int32_t extrusion_set_flags(extrusion_entity *entity, uint32_t flags);

/* Return non-zero if the entity has a local polyline. */
int32_t extrusion_has_polyline(const extrusion_entity *entity);

/* Return non-zero if the entity has children. */
int32_t extrusion_has_children(const extrusion_entity *entity);

/* Return the number of direct children. */
uint32_t extrusion_child_count(const extrusion_entity *entity);

/* Return one direct child, or NULL if idx is invalid. */
extrusion_entity *extrusion_child_mutable(extrusion_entity *entity, uint32_t idx);
const extrusion_entity *extrusion_child(const extrusion_entity *entity, uint32_t idx);

/*
Insert a deep copy of child into parent.

Insertion at idx == extrusion_child_count(parent) appends to the end. idx larger
than child_count is invalid and returns EXTRUSION_INDEX_INVALID. If parent has a
local polyline, the operation fails: clear the local polyline first if changing
the entity into a child collection is intended.
*/
uint32_t extrusion_insert_child_copy(extrusion_entity *parent,
                                     uint32_t idx,
                                     const extrusion_entity *child);

/*
Insert child content into parent, then leave child empty.

This moves the content of the child handle, not a node already attached to some
other parent. To move an existing child between parents, use extrusion_move_child().
*/
uint32_t extrusion_insert_child_move(extrusion_entity *parent,
                                     uint32_t idx,
                                     extrusion_entity *child);

/* Remove one direct child. Returns non-zero on success. */
int32_t extrusion_remove_child(extrusion_entity *parent, uint32_t idx);

/*
Move an existing child from one parent to another.

The moved child keeps its content and properties. dst_idx follows normal insert
semantics: dst_idx == extrusion_child_count(dst_parent) appends to the end.
Indices larger than the destination child count are invalid.
*/
uint32_t extrusion_move_child(extrusion_entity *dst_parent,
                              uint32_t dst_idx,
                              extrusion_entity *src_parent,
                              uint32_t src_idx);

#ifdef __cplusplus
}
#endif

#endif /* slic3r_extrusion_entity_h_ */
