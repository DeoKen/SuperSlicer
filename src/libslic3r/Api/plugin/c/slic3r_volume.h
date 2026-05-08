///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef slic3r_volume_h_
#define slic3r_volume_h_

#include <stdint.h>

#include "slic3r_data_tree.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================= HANDLES ========================= */

typedef struct volume_handle volume_handle;
typedef struct triangle_mesh_handle triangle_mesh_handle;

/* ========================= BASIC VALUE TYPES ========================= */

typedef struct c_triangle_indices {
    uint32_t a;
    uint32_t b;
    uint32_t c;
} c_triangle_indices;

/*
Low-level mesh slicing mode, matching MeshSlicingParams::SlicingMode.
Plugins should normally derive this from the Object's "slicing_mode" config:
Regular -> REGULAR, EvenOdd -> EVEN_ODD, CloseHoles -> POSITIVE.
*/
typedef enum raw_mesh_slicing_mode {
    RAW_MESH_SLICING_MODE_REGULAR                  = 0,
    RAW_MESH_SLICING_MODE_EVEN_ODD                 = 1,
    RAW_MESH_SLICING_MODE_POSITIVE                 = 2,
    RAW_MESH_SLICING_MODE_POSITIVE_LARGEST_CONTOUR = 3
} raw_mesh_slicing_mode;

/*
C ABI mirror of MeshSlicingParamsEx.

All distances are unscaled millimeters because TriangleMesh vertices and the
low-level slicer work in float/double model coordinates. The transform is also
unscaled and is applied before slicing.
*/
typedef struct c_mesh_slicing_params {
    raw_mesh_slicing_mode mode;
    uint32_t slicing_mode_normal_below_layer;
    raw_mesh_slicing_mode mode_below;
    c_matrix4d transform;
    float closing_radius;
    float extra_offset;
    double resolution;
    double model_resolution;
} c_mesh_slicing_params;

/* ========================= VOLUME TYPE ========================= */

/*
Stable C mirror of the host volume type enum.
Keep the numeric values aligned with the host enum because plugins may persist
or compare them without including C++ headers.
*/
typedef enum raw_volume_type {
    RAW_VOLUME_TYPE_INVALID                     = -1,
    RAW_VOLUME_TYPE_MODEL_PART                  = 0,
    RAW_VOLUME_TYPE_NEGATIVE_VOLUME             = 1,
    RAW_VOLUME_TYPE_PARAMETER_MODIFIER          = 2,
    RAW_VOLUME_TYPE_SUPPORT_BLOCKER             = 3,
    RAW_VOLUME_TYPE_SUPPORT_ENFORCER            = 4,
    RAW_VOLUME_TYPE_SEAM_POSITION_CENTER        = 5,
    RAW_VOLUME_TYPE_SEAM_POSITION_CENTER_Z      = 6,
    RAW_VOLUME_TYPE_SEAM_POSITION_INSIDE_CENTER = 7,
    RAW_VOLUME_TYPE_SEAM_POSITION_INSIDE        = 8,
    RAW_VOLUME_TYPE_BRIM_PATCH                  = 9,
    RAW_VOLUME_TYPE_BRIM_NEGATIVE               = 10
} raw_volume_type;

/* ========================= PRINTOBJECT -> VOLUMES ========================= */

/* Returns the number of volumes borrowed from the Object's source model data. */
uint32_t object_volume_count(const object_handle *object);

/*
Returns a borrowed read-only Volume handle.
The handle stays valid only while the underlying Object stays valid.
*/
const volume_handle *object_volume_at(const object_handle *object, uint32_t idx);

/* ========================= VOLUME ========================= */

raw_volume_type volume_get_type(const volume_handle *volume);
uint64_t volume_get_id(const volume_handle *volume);
const config_handle *volume_get_config(const volume_handle *volume);

/* Returns -1 if the volume has no FFF extruder assignment. */
int32_t volume_get_extruder_id(const volume_handle *volume);

c_matrix4d volume_get_matrix(const volume_handle *volume);
c_matrix4d volume_get_matrix_no_offset(const volume_handle *volume);

/* Facet-painting flags. Detailed facet extraction will be added as a separate ABI. */
int volume_has_fdm_support_painting(const volume_handle *volume);
int volume_has_seam_painting(const volume_handle *volume);
int volume_has_mm_painting(const volume_handle *volume);

const triangle_mesh_handle *volume_get_mesh(const volume_handle *volume);

/* ========================= TRIANGLE MESH ========================= */

uint32_t triangle_mesh_vertex_count(const triangle_mesh_handle *mesh);
uint32_t triangle_mesh_triangle_count(const triangle_mesh_handle *mesh);
c_vec3f triangle_mesh_vertex_at(const triangle_mesh_handle *mesh, uint32_t idx);
c_triangle_indices triangle_mesh_triangle_at(const triangle_mesh_handle *mesh, uint32_t idx);

/*
Slice a TriangleMesh into raw polygon loops at object-local Z positions.

transform is an unscaled 3D transform matrix. slice_zs are also unscaled float
values because TriangleMesh vertices live in float 3D coordinates. If the Z
comes from a Layer view, convert Layer::slice_z() with unscaled() before calling
this function.

out_by_layer must point to slice_count mutable polygon_collection_handle objects,
usually created in plugin storage by the plugin.
Each destination collection is cleared and replaced by the polygons generated at
the matching Z.
*/
void triangle_mesh_slice_to_polygons(const triangle_mesh_handle *mesh,
                                     c_matrix4d transform,
                                     const float *z_mm_by_layer,
                                     polygon_collection_handle **slices_by_layer,
                                     uint32_t layer_count);

/*
Same as triangle_mesh_slice_to_polygons(), but exposes MeshSlicingParamsEx so a
plugin can reproduce the native PrintObject::slice_volumes() behavior.
*/
void triangle_mesh_slice_to_polygons_with_params(const triangle_mesh_handle *mesh,
                                                 const c_mesh_slicing_params *params,
                                                 const float *z_mm_by_layer,
                                                 polygon_collection_handle **slices_by_layer,
                                                 uint32_t layer_count);

polygon_collection_handle* triangle_mesh_slice_to_polygon(storage_handle *storage,
                                    const triangle_mesh_handle *mesh,
                                    c_matrix4d transform,
                                    float layer_z_mm);

/*
Slice a TriangleMesh into raw expolygon at object-local Z positions.

transform is an unscaled 3D transform matrix. slice_zs are also unscaled float
values because TriangleMesh vertices live in float 3D coordinates. If the Z
comes from a Layer view, convert Layer::slice_z() with unscaled() before calling
this function.

out_by_layer must point to slice_count mutable polygon_collection_handle objects,
usually created in plugin storage by the plugin.
Each destination collection is cleared and replaced by the polygons generated at
the matching Z.
*/
void triangle_mesh_slice_to_expolygons(const triangle_mesh_handle *mesh,
                                      c_matrix4d transform,
                                      const float *z_mm_by_layer,
                                      expolygon_collection_handle **slices_by_layer,
                                    uint32_t layer_count);

/*
Slice directly to ExPolygons using MeshSlicingParamsEx. This is the preferred
entry point for STEP_SLICING plugins because it follows the same low-level path
as PrintObjectSlice.cpp::slice_volumes_inner().
*/
void triangle_mesh_slice_to_expolygons_with_params(const triangle_mesh_handle *mesh,
                                                   const c_mesh_slicing_params *params,
                                                   const float *z_mm_by_layer,
                                                   expolygon_collection_handle **slices_by_layer,
                                                   uint32_t layer_count);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* slic3r_volume_h_ */
