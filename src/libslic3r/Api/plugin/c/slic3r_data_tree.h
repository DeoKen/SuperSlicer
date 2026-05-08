///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef slic3r_data_tree_h_
#define slic3r_data_tree_h_

#include <stddef.h>
#include <stdint.h>

#include "slic3r_def.h"
#include "slic3r_utils.h"
#include "slic3r_geometry.h"
#include "slic3r_extrusions.h"
#include "slic3r_config_option.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================= HANDLES ========================= */

typedef struct print_handle print_handle;
typedef struct print_region_handle print_region_handle;
typedef struct object_handle object_handle;
typedef struct layer_handle layer_handle;
typedef struct layer_region_handle layer_region_handle;
typedef struct layer_island_handle layer_island_handle;
typedef struct layer_region_island_handle layer_region_island_handle;
typedef struct surface_handle surface_handle;
typedef struct surface_collection_handle surface_collection_handle;
typedef struct config_handle config_handle;

/* ========================= SURFACE TYPE ========================= */

/*
Surface type bitmask (mapped from SurfaceType).
Multiple flags can be combined.
*/
typedef uint16_t raw_surface_type;

/* No surface */
#define RAW_SURFACE_TYPE_NONE 0

/* Position flags */

/* Top horizontal surface, visible from the top */
#define RAW_SURFACE_TYPE_POS_TOP (1 << 0)

/* Bottom horizontal surface, visible from the bottom */
#define RAW_SURFACE_TYPE_POS_BOTTOM (1 << 1)

/* Internal sparse infill */
#define RAW_SURFACE_TYPE_POS_INTERNAL (1 << 2)

/* Inner/outer perimeters (mainly for coloring) */
#define RAW_SURFACE_TYPE_POS_PERIMETER (1 << 3)

/* Density flags */

/* Solid infill (100%) */
#define RAW_SURFACE_TYPE_DENS_SOLID (1 << 4)

/* Sparse infill (>0% & <100%) */
#define RAW_SURFACE_TYPE_DENS_SPARSE (1 << 5)

/* Void / combined sparse layers */
#define RAW_SURFACE_TYPE_DENS_VOID (1 << 6)

/* Modifier flags */

/* First bridging layer */
#define RAW_SURFACE_TYPE_MOD_BRIDGE (1 << 7)

/* Second layer over bridge */
#define RAW_SURFACE_TYPE_MOD_OVERBRIDGE (1 << 8)

/* Check if flag is set */
#define RAW_SURFACE_TYPE_HAS(type, flag) (((type) & (flag)) != 0)

/* Add flag */
#define RAW_SURFACE_TYPE_ADD(type, flag) ((type) |= (flag))

/* Remove flag */
#define RAW_SURFACE_TYPE_REMOVE(type, flag) ((type) &= ~(flag))

/* ========================= SURFACE ========================= */

struct c_surface
{
    const expolygon_handle *expolygon;
    raw_surface_type type;
};
/*
Snapshot one Surface into a tiny C value. Prefer the handle API below when the
Surface may grow new fields, or when you need to mutate it.
*/
c_surface surface_c_view(const surface_handle *me);

/* Get expolygon (non-const / const). */
expolygon_handle *surface_get_expolygon_mutable(surface_handle *me);
const expolygon_handle *surface_get_expolygon(const surface_handle *me);

/* Surface type bitmask access. */
raw_surface_type surface_get_type(const surface_handle *me);
void surface_set_type(surface_handle *me, raw_surface_type type);

/* Convenience helpers for one flag inside the surface type bitmask. */
int32_t surface_get_flag(const surface_handle *me, raw_surface_type flag);
void surface_set_flag(surface_handle *me, raw_surface_type flag, int32_t enabled);

/* Surface collection view. The collection owns its Surface elements. */
uint32_t surface_collection_size(const surface_collection_handle *me);
surface_handle *surface_collection_at_mutable(surface_collection_handle *me, uint32_t idx);
const surface_handle *surface_collection_at(const surface_collection_handle *me, uint32_t idx);

/* ========================= LAYER ========================= */

coord_t layer_get_height(const layer_handle *me);
coord_t layer_get_print_z(const layer_handle *me);
/* Center Z of the slicing plane. It is exactly print_z - height / 2. */
coord_t layer_get_slice_z(const layer_handle *me);
/* is == -1 if this layer isn't a support layer. */
coord_t layer_get_support_id(const layer_handle *me);

const expolygon_collection_handle *layer_get_slices(const layer_handle *me);

layer_handle *layer_get_upper_layer_mutable(layer_handle *me);
const layer_handle *layer_get_upper_layer(const layer_handle *me);

layer_handle *layer_get_lower_layer_mutable(layer_handle *me);
const layer_handle *layer_get_lower_layer(const layer_handle *me);

// tag that can be used by processed to store some information
void layer_set_tag(layer_handle *me, const char *tag, double value);
double layer_get_tag(const layer_handle *me, const char *tag);

uint32_t layer_count_region(const layer_handle *me);
layer_region_handle *layer_get_region_mutable(layer_handle *me, uint32_t idx);
const layer_region_handle *layer_get_region(const layer_handle *me, uint32_t idx);

uint32_t layer_count_island(const layer_handle *me);
layer_island_handle *layer_get_island_mutable(layer_handle *me, uint32_t idx);
const layer_island_handle *layer_get_island(const layer_handle *me, uint32_t idx);

/* ========================= LAYER REGION ========================= */

/* tag that can be used by processed to store some information */
void layer_region_set_tag(layer_region_handle *me, const char *tag, double value);
double layer_region_get_tag(const layer_region_handle *me, const char *tag);

c_flow layer_region_get_flow(const layer_region_handle *me, raw_extrusion_role flow_role);
const expolygon_collection_handle *layer_region_get_slices(const layer_region_handle *me);
c_bounding_box layer_region_get_bounding_box(const layer_region_handle *me);

/* ---- processed surfaces ---- */
surface_collection_handle *layer_region_get_surfaces_mutable(layer_region_handle *me);
const surface_collection_handle *layer_region_get_surfaces(const layer_region_handle *me);
uint32_t layer_region_count_surface(const layer_region_handle *me);
surface_handle *layer_region_get_surface_mutable(layer_region_handle *me, uint32_t idx);
const surface_handle *layer_region_get_surface(const layer_region_handle *me, uint32_t idx);

/* ---- processed surfaces for infill ---- */
surface_collection_handle *layer_region_get_fill_surfaces_mutable(layer_region_handle *me);
const surface_collection_handle *layer_region_get_fill_surfaces(const layer_region_handle *me);
uint32_t layer_region_count_fill_surface(const layer_region_handle *me);
surface_handle *layer_region_get_fill_surface_mutable(layer_region_handle *me, uint32_t idx);
const surface_handle *layer_region_get_fill_surface(const layer_region_handle *me, uint32_t idx);

const layer_handle *layer_region_get_layer(const layer_region_handle *me);
const print_region_handle *layer_region_get_print_region(const layer_region_handle *me);

/* ========================= LAYER ISLAND ========================= */

expolygon_handle *layer_island_get_slice_mutable(layer_island_handle *me);
const expolygon_handle *layer_island_get_slice(const layer_island_handle *me);
c_bounding_box layer_island_get_bounding_box(const layer_island_handle *me);
/* give an expolygon included inside get_slice()  where the infill has to be extruded. */
const expolygon_handle *layer_island_get_infill_slice(const layer_island_handle *me);
c_bounding_box layer_island_get_infill_bounding_box(const layer_island_handle *me);
/* give an expolygon included inside get_infill_slice() where the infill may be extruded if there was no
 * infill-perimeter encroachment. */
const expolygon_handle *layer_island_get_infill_no_overlap_slice(const layer_island_handle *me);

/* tag that can be used by processed to store some information */
void layer_island_set_tag(layer_island_handle *me, const char *tag, double value);
double layer_island_get_tag(const layer_island_handle *me, const char *tag);

uint32_t layer_island_count_region(const layer_island_handle *me);
layer_region_handle *layer_island_get_region_mutable(layer_island_handle *me, uint32_t idx);
const layer_region_handle *layer_island_get_region(const layer_island_handle *me, uint32_t idx);

uint32_t layer_island_count_region_island(const layer_island_handle *me);
layer_region_island_handle *layer_island_get_region_island_mutable(layer_island_handle *me, uint32_t idx);
const layer_region_island_handle *layer_island_get_region_island(const layer_island_handle *me, uint32_t idx);

const layer_handle *layer_island_get_layer(const layer_island_handle *me);

/* ========================= LAYER REGION ISLAND ========================= */

int32_t layer_region_island_extruder_id(const layer_region_island_handle *me);
int32_t layer_region_island_has_extrusions(const layer_region_island_handle *me);
int32_t layer_region_island_has_extrusion(const layer_region_island_handle *me, raw_extrusion_role role);
extrusion_entity *layer_region_island_get_mutable_extrusion(layer_region_island_handle *me, raw_extrusion_role role);
const extrusion_entity *layer_region_island_get_extrusion(const layer_region_island_handle *me,
                                                          raw_extrusion_role role);

/* tag that can be used by processed to store some information */
void layer_region_island_set_tag(layer_region_island_handle *me, const char *tag, double value);
double layer_region_island_get_tag(const layer_region_island_handle *me, const char *tag);

uint32_t layer_region_island_count_region_island(const layer_region_island_handle *me);
layer_region_island_handle *layer_region_island_get_region_island_mutable(layer_region_island_handle *me, uint32_t idx);
const layer_region_island_handle *layer_region_island_get_region_island(const layer_region_island_handle *me,
                                                                        uint32_t idx);

/* ========================= PRINT REGION ========================= */

config_handle *print_region_get_config_mutable(print_region_handle *me);
const config_handle *print_region_get_config(const print_region_handle *me);

/* ========================= OBJECT ========================= */

config_handle *object_get_config_mutable(object_handle *me);
const config_handle *object_get_config(const object_handle *me);

// Deprecated: if not useful, it will be deleted
coord_t object_get_max_z(const object_handle *me);

/*
Transformation from the source model object into this printable Object.

This is the same transform as native PrintObject::trafo(): rotation / scaling /
mirroring / Z translation are included, but the temporary XY centering offset
used to keep Clipper coordinates small is not. To reproduce trafo_centered(),
apply object_get_center_offset() as a pre-translation:
    centered = translate(-unscaled(center.x), -unscaled(center.y), 0) * transform
*/
c_matrix4d object_get_transform(const object_handle *me);

/*
Scaled XY offset used by the host while slicing to center the mesh before it is
sent to Clipper. This is a slicer-space 2D value, so it uses coord_t through
c_point. Convert with unscaled() before composing it with c_matrix4d.
*/
c_point object_get_center_offset(const object_handle *me);

uint32_t object_count_layer(const object_handle *me);
layer_handle *object_get_layer_mutable(object_handle *me, uint32_t idx);
const layer_handle *object_get_layer(const object_handle *me, uint32_t idx);

uint32_t object_count_region(const object_handle *me);
print_region_handle *object_get_print_region_mutable(object_handle *me, uint32_t idx);
const print_region_handle *object_get_print_region(const object_handle *me, uint32_t idx);

/* ========================= PRINT ========================= */

config_handle *print_get_config_mutable(print_handle *me);
const config_handle *print_get_config(const print_handle *me);

uint32_t print_count_object(const print_handle *me);

object_handle *print_get_object_mutable(print_handle *me, uint32_t idx);
const object_handle *print_get_object(const print_handle *me, uint32_t idx);

/* ========================= CONFIG ========================= */

/*
Returns keys as a borrowed array of strings.
*/
const_strings_t config_keys(const config_handle *me);

/*
Get option by key (string must be null-terminated).
Returns NULL if not found.
*/
const config_option_handle *config_get(const config_handle *me, const char *key);
config_option_handle *config_get_mutable(config_handle *me, const char *key);

#ifdef __cplusplus
}
#endif

#endif // slic3r_data_tree_h_
