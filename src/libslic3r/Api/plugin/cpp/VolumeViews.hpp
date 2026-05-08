///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#pragma once

#include "libslic3r/Api/plugin/c/slic3r_volume.h"
#include "libslic3r/Api/plugin/cpp/DataTreeViews.hpp"

#include <cassert>
#include <vector>

namespace slic3r_api {

/*
Read-only helpers over Volume and TriangleMesh.

These classes are intentionally borrowed views: they never own or free the
underlying host objects. They are meant for slicing plugins that need to inspect
the source model volumes through an Object.

Typical use:
    for (uint32_t i = 0; i < object.volume_count(); ++i) {
        Volume volume = object.volume(i);
        if (!volume.is_model_part())
            continue;
        TriangleMesh mesh = volume.mesh();
        ...
    }
*/

class TriangleMesh : public ConstDataTreeHandleView<triangle_mesh_handle>
{
public:
    using ConstDataTreeHandleView<triangle_mesh_handle>::ConstDataTreeHandleView;

    uint32_t vertex_count() const { return triangle_mesh_vertex_count(handle()); }
    uint32_t triangle_count() const { return triangle_mesh_triangle_count(handle()); }
    bool empty() const { return triangle_count() == 0; }

    c_vec3f vertex(uint32_t idx) const
    {
        assert(idx < vertex_count());
        return triangle_mesh_vertex_at(handle(), idx);
    }

    c_triangle_indices triangle(uint32_t idx) const
    {
        assert(idx < triangle_count());
        return triangle_mesh_triangle_at(handle(), idx);
    }

    ///*
    //Fill one already-created StoredPolygonCollection per slice Z. Z values are
    //unscaled float coordinates, matching TriangleMesh vertices. If the source is
    //Layer::slice_z(), convert it with unscaled() before calling this helper.
    //*/
    //void slice_to_polygons(storage_handle *storage,
    //                       c_matrix4d transform,
    //                       const float *slice_zs,
    //                       uint32_t slice_count,
    //                       polygon_collection_handle **out_by_layer) const
    //{
    //    triangle_mesh_slice_to_polygons(storage, handle(), transform, slice_zs, slice_count, out_by_layer);
    //}

    /*
    Convenience overload for plugin code. The returned StoredPolygonCollection
    objects own their storage allocations and may be moved into later steps.
    */
    std::vector<StoredPolygonCollection> slice_to_polygons(storage_handle *storage,
                                                           c_matrix4d transform,
                                                           const std::vector<float> &slice_zs) const
    {
        std::vector<StoredPolygonCollection> out;
        out.reserve(slice_zs.size());
        for (size_t idx = 0; idx < slice_zs.size(); ++idx)
            out.emplace_back(storage);

        std::vector<polygon_collection_handle *> out_handles;
        out_handles.reserve(out.size());
        for (StoredPolygonCollection &polygons : out)
            out_handles.push_back(polygons.mutable_handle());

        assert(slice_zs.size() == out_handles.size());
        triangle_mesh_slice_to_polygons(handle(), transform, slice_zs.data(), out_handles.data(), static_cast<uint32_t>(slice_zs.size()));
        return out;
    }

    /*
    Native-equivalent ExPolygon slicing path. Use this from slicing plugins when
    reproducing PrintObjectSlice.cpp: it applies closing radius, extra offset,
    contour simplification and slicing mode inside the host.
    */
    std::vector<StoredExPolygonCollection> slice_to_expolygons(storage_handle *storage,
                                                               const c_mesh_slicing_params &params,
                                                               const std::vector<float> &slice_zs) const
    {
        std::vector<StoredExPolygonCollection> out;
        out.reserve(slice_zs.size());
        for (size_t idx = 0; idx < slice_zs.size(); ++idx)
            out.emplace_back(storage);

        std::vector<expolygon_collection_handle *> out_handles;
        out_handles.reserve(out.size());
        for (StoredExPolygonCollection &expolygons : out)
            out_handles.push_back(expolygons.mutable_handle());

        assert(slice_zs.size() == out_handles.size());
        triangle_mesh_slice_to_expolygons_with_params(handle(), &params, slice_zs.data(), out_handles.data(),
                                                      static_cast<uint32_t>(slice_zs.size()));
        return out;
    }
};

class Volume : public ConstDataTreeHandleView<volume_handle>
{
public:
    using ConstDataTreeHandleView<volume_handle>::ConstDataTreeHandleView;

    uint64_t id() const { return volume_get_id(handle()); }
    raw_volume_type type() const { return volume_get_type(handle()); }
    Config config() const { return Config(volume_get_config(handle())); }

    int32_t extruder_id() const { return volume_get_extruder_id(handle()); }

    c_matrix4d matrix() const { return volume_get_matrix(handle()); }
    c_matrix4d matrix_no_offset() const { return volume_get_matrix_no_offset(handle()); }

    bool is_model_part() const { return type() == RAW_VOLUME_TYPE_MODEL_PART; }
    bool is_negative() const { return type() == RAW_VOLUME_TYPE_NEGATIVE_VOLUME; }
    bool is_modifier() const { return type() == RAW_VOLUME_TYPE_PARAMETER_MODIFIER; }
    bool is_support_enforcer() const { return type() == RAW_VOLUME_TYPE_SUPPORT_ENFORCER; }
    bool is_support_blocker() const { return type() == RAW_VOLUME_TYPE_SUPPORT_BLOCKER; }
    bool is_support_modifier() const { return is_support_blocker() || is_support_enforcer(); }
    bool is_seam_position() const
    {
        const raw_volume_type value = type();
        return value == RAW_VOLUME_TYPE_SEAM_POSITION_CENTER ||
               value == RAW_VOLUME_TYPE_SEAM_POSITION_CENTER_Z ||
               value == RAW_VOLUME_TYPE_SEAM_POSITION_INSIDE_CENTER ||
               value == RAW_VOLUME_TYPE_SEAM_POSITION_INSIDE;
    }
    bool is_brim() const { return type() == RAW_VOLUME_TYPE_BRIM_PATCH || type() == RAW_VOLUME_TYPE_BRIM_NEGATIVE; }

    bool is_fdm_support_painted() const { return volume_has_fdm_support_painting(handle()) != 0; }
    bool is_seam_painted() const { return volume_has_seam_painting(handle()) != 0; }
    bool is_mm_painted() const { return volume_has_mm_painting(handle()) != 0; }

    TriangleMesh mesh() const { return TriangleMesh(volume_get_mesh(handle())); }
};

inline uint32_t Object::volume_count() const
{
    return object_volume_count(handle());
}

inline Volume Object::volume(uint32_t idx) const
{
    assert(idx < volume_count());
    return Volume(object_volume_at(handle(), idx));
}

} // namespace slic3r_api
