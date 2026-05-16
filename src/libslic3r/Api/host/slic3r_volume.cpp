///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "libslic3r/Api/host/ApiHostUtils.hpp"
#include "libslic3r/Api/plugin/c/slic3r_volume.h"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

namespace Slic3r {

static const PrintObject *to_object(const object_handle *me)
{
    return reinterpret_cast<const PrintObject *>(me);
}

static const ModelVolume *to_volume(const volume_handle *me)
{
    return reinterpret_cast<const ModelVolume *>(me);
}

static const TriangleMesh *to_triangle_mesh(const triangle_mesh_handle *me)
{
    return reinterpret_cast<const TriangleMesh *>(me);
}

static Polygons *to_polygons(polygon_collection_handle *me)
{
    return reinterpret_cast<Polygons *>(me);
}

static ExPolygons *to_expolygons(expolygon_collection_handle *me)
{
    return reinterpret_cast<ExPolygons *>(me);
}

static c_matrix4d to_c_matrix4d(const Transform3d &matrix)
{
    c_matrix4d out = {};
    const auto &raw = matrix.matrix();
    for (uint32_t row = 0; row < 4; ++row)
        for (uint32_t col = 0; col < 4; ++col)
            out.value[row * 4 + col] = raw(row, col);
    return out;
}

static c_vec3f to_c_vec3f(const Vec3f &point)
{
    c_vec3f out = {};
    out.x = point.x();
    out.y = point.y();
    out.z = point.z();
    return out;
}

static Transform3d to_transform3d(const c_matrix4d &matrix)
{
    Transform3d out = Transform3d::Identity();
    for (uint32_t row = 0; row < 4; ++row)
        for (uint32_t col = 0; col < 4; ++col)
            out.matrix()(row, col) = matrix.value[row * 4 + col];
    return out;
}

static raw_volume_type to_raw_volume_type(ModelVolumeType type)
{
    return static_cast<raw_volume_type>(static_cast<int>(type));
}

static MeshSlicingParams::SlicingMode to_mesh_slicing_mode(raw_mesh_slicing_mode mode)
{
    switch (mode) {
    case RAW_MESH_SLICING_MODE_EVEN_ODD:
        return MeshSlicingParams::SlicingMode::EvenOdd;
    case RAW_MESH_SLICING_MODE_POSITIVE:
        return MeshSlicingParams::SlicingMode::Positive;
    case RAW_MESH_SLICING_MODE_POSITIVE_LARGEST_CONTOUR:
        return MeshSlicingParams::SlicingMode::PositiveLargestContour;
    case RAW_MESH_SLICING_MODE_REGULAR:
    default:
        return MeshSlicingParams::SlicingMode::Regular;
    }
}

static MeshSlicingParamsEx to_mesh_slicing_params(const c_mesh_slicing_params *params)
{
    MeshSlicingParamsEx out;
    if (params == nullptr)
        return out;

    out.mode = to_mesh_slicing_mode(params->mode);
    out.slicing_mode_normal_below_layer = params->slicing_mode_normal_below_layer;
    out.mode_below = to_mesh_slicing_mode(params->mode_below);
    out.trafo = to_transform3d(params->transform);
    out.closing_radius = params->closing_radius;
    out.extra_offset = params->extra_offset;
    out.resolution = params->resolution;
    out.model_resolution = params->model_resolution;
    return out;
}

uint32_t object_volume_count(const object_handle *object)
{
    const PrintObject *object_native = to_object(object);
    return object_native == nullptr || object_native->model_object() == nullptr ?
        0u : uint32_t(object_native->model_object()->volumes.size());
}

const volume_handle *object_volume_at(const object_handle *object, uint32_t idx)
{
    const PrintObject *object_native = to_object(object);
    if (object_native == nullptr || object_native->model_object() == nullptr)
        return nullptr;
    const ModelVolumePtrs &volumes = object_native->model_object()->volumes;
    if (idx >= volumes.size())
        return nullptr;
    return reinterpret_cast<const volume_handle *>(volumes[idx]);
}

raw_volume_type volume_get_type(const volume_handle *volume)
{
    const ModelVolume *native = to_volume(volume);
    return native == nullptr ? RAW_VOLUME_TYPE_INVALID : to_raw_volume_type(native->type());
}

uint64_t volume_get_id(const volume_handle *volume)
{
    const ModelVolume *native = to_volume(volume);
    return native == nullptr ? 0u : uint64_t(native->id().id);
}

const config_handle *volume_get_config(const volume_handle *volume)
{
    const ModelVolume *native = to_volume(volume);
    return native == nullptr ? nullptr : ApiHost::to_config_handle(&native->config.get());
}

int32_t volume_get_extruder_id(const volume_handle *volume)
{
    const ModelVolume *native = to_volume(volume);
    return native == nullptr ? -1 : int32_t(native->extruder_id());
}

c_matrix4d volume_get_matrix(const volume_handle *volume)
{
    const ModelVolume *native = to_volume(volume);
    return native == nullptr ? c_matrix4d{} : to_c_matrix4d(native->get_matrix());
}

c_matrix4d volume_get_matrix_no_offset(const volume_handle *volume)
{
    const ModelVolume *native = to_volume(volume);
    return native == nullptr ? c_matrix4d{} : to_c_matrix4d(native->get_matrix_no_offset());
}

int volume_has_fdm_support_painting(const volume_handle *volume)
{
    const ModelVolume *native = to_volume(volume);
    return native != nullptr && native->is_fdm_support_painted();
}

int volume_has_seam_painting(const volume_handle *volume)
{
    const ModelVolume *native = to_volume(volume);
    return native != nullptr && native->is_seam_painted();
}

int volume_has_mm_painting(const volume_handle *volume)
{
    const ModelVolume *native = to_volume(volume);
    return native != nullptr && native->is_mm_painted();
}

const triangle_mesh_handle *volume_get_mesh(const volume_handle *volume)
{
    const ModelVolume *native = to_volume(volume);
    return native == nullptr ? nullptr : reinterpret_cast<const triangle_mesh_handle *>(&native->mesh());
}

uint32_t triangle_mesh_vertex_count(const triangle_mesh_handle *mesh)
{
    const TriangleMesh *native = to_triangle_mesh(mesh);
    return native == nullptr ? 0u : uint32_t(native->its.vertices.size());
}

uint32_t triangle_mesh_triangle_count(const triangle_mesh_handle *mesh)
{
    const TriangleMesh *native = to_triangle_mesh(mesh);
    return native == nullptr ? 0u : uint32_t(native->its.indices.size());
}

c_vec3f triangle_mesh_vertex_at(const triangle_mesh_handle *mesh, uint32_t idx)
{
    const TriangleMesh *native = to_triangle_mesh(mesh);
    if (native == nullptr || idx >= native->its.vertices.size())
        return {};
    return to_c_vec3f(native->its.vertices[idx]);
}

c_triangle_indices triangle_mesh_triangle_at(const triangle_mesh_handle *mesh, uint32_t idx)
{
    c_triangle_indices out = {};
    const TriangleMesh *native = to_triangle_mesh(mesh);
    if (native == nullptr || idx >= native->its.indices.size())
        return out;
    const stl_triangle_vertex_indices &triangle = native->its.indices[idx];
    out.a = uint32_t(triangle[0]);
    out.b = uint32_t(triangle[1]);
    out.c = uint32_t(triangle[2]);
    return out;
}

} // namespace Slic3r

extern "C" {

uint32_t object_volume_count(const object_handle *object)
{
    return Slic3r::object_volume_count(object);
}

const volume_handle *object_volume_at(const object_handle *object, uint32_t idx)
{
    return Slic3r::object_volume_at(object, idx);
}

raw_volume_type volume_get_type(const volume_handle *volume)
{
    return Slic3r::volume_get_type(volume);
}

uint64_t volume_get_id(const volume_handle *volume)
{
    return Slic3r::volume_get_id(volume);
}

const config_handle *volume_get_config(const volume_handle *volume)
{
    return Slic3r::volume_get_config(volume);
}

int32_t volume_get_extruder_id(const volume_handle *volume)
{
    return Slic3r::volume_get_extruder_id(volume);
}

c_matrix4d volume_get_matrix(const volume_handle *volume)
{
    return Slic3r::volume_get_matrix(volume);
}

c_matrix4d volume_get_matrix_no_offset(const volume_handle *volume)
{
    return Slic3r::volume_get_matrix_no_offset(volume);
}

int volume_has_fdm_support_painting(const volume_handle *volume)
{
    return Slic3r::volume_has_fdm_support_painting(volume);
}

int volume_has_seam_painting(const volume_handle *volume)
{
    return Slic3r::volume_has_seam_painting(volume);
}

int volume_has_mm_painting(const volume_handle *volume)
{
    return Slic3r::volume_has_mm_painting(volume);
}

const triangle_mesh_handle *volume_get_mesh(const volume_handle *volume)
{
    return Slic3r::volume_get_mesh(volume);
}

uint32_t triangle_mesh_vertex_count(const triangle_mesh_handle *mesh)
{
    return Slic3r::triangle_mesh_vertex_count(mesh);
}

uint32_t triangle_mesh_triangle_count(const triangle_mesh_handle *mesh)
{
    return Slic3r::triangle_mesh_triangle_count(mesh);
}

c_vec3f triangle_mesh_vertex_at(const triangle_mesh_handle *mesh, uint32_t idx)
{
    return Slic3r::triangle_mesh_vertex_at(mesh, idx);
}

c_triangle_indices triangle_mesh_triangle_at(const triangle_mesh_handle *mesh, uint32_t idx)
{
    return Slic3r::triangle_mesh_triangle_at(mesh, idx);
}

void triangle_mesh_slice_to_polygons(const triangle_mesh_handle *mesh,
                                     c_matrix4d transform,
                                     const float *z_mm_by_layer,
                                     polygon_collection_handle **slices_by_layer,
                                     uint32_t layer_count)
{
    c_mesh_slicing_params params = {};
    params.mode = RAW_MESH_SLICING_MODE_REGULAR;
    params.mode_below = RAW_MESH_SLICING_MODE_REGULAR;
    params.transform = transform;
    triangle_mesh_slice_to_polygons_with_params(mesh, &params, z_mm_by_layer, slices_by_layer, layer_count);
}

void triangle_mesh_slice_to_polygons_with_params(const triangle_mesh_handle *mesh,
                                                 const c_mesh_slicing_params *params,
                                                 const float *z_mm_by_layer,
                                                 polygon_collection_handle **slices_by_layer,
                                                 uint32_t layer_count)
{
    if (mesh == nullptr || z_mm_by_layer == nullptr || slices_by_layer == nullptr || layer_count == 0)
        return;

    std::vector<float> zs(z_mm_by_layer, z_mm_by_layer + layer_count);

    Slic3r::MeshSlicingParamsEx native_params = Slic3r::to_mesh_slicing_params(params);
    indexed_triangle_set its = Slic3r::to_triangle_mesh(mesh)->its;
    if (native_params.trafo.rotation().determinant() < 0.)
        Slic3r::its_flip_triangles(its);
    std::vector<Slic3r::Polygons> sliced = Slic3r::slice_mesh(its, zs, native_params);

    for (uint32_t idx = 0; idx < layer_count; ++idx) {
        assert(slices_by_layer != nullptr);
        if (slices_by_layer[idx] != nullptr)
            *Slic3r::to_polygons(slices_by_layer[idx]) = idx < sliced.size() ? std::move(sliced[idx]) : Slic3r::Polygons{};
    }
}

polygon_collection_handle *triangle_mesh_slice_to_polygon(storage_handle *storage,
                                                          const triangle_mesh_handle *mesh,
                                                          c_matrix4d transform,
                                                          float layer_z_mm) {
    if (storage == nullptr || mesh == nullptr)
        return nullptr;

    //MeshSlicingParams params;
    //params.trafo = to_transform3d(transform);
    //std::vector<Polygons> sliced = slice_mesh(to_triangle_mesh(mesh)->its, zs, params);


    return nullptr;
}

void triangle_mesh_slice_to_expolygons(const triangle_mesh_handle *mesh,
                                       c_matrix4d transform,
                                       const float *z_mm_by_layer,
                                       expolygon_collection_handle **slices_by_layer,
                                       uint32_t layer_count)
{
    c_mesh_slicing_params params = {};
    params.mode = RAW_MESH_SLICING_MODE_REGULAR;
    params.mode_below = RAW_MESH_SLICING_MODE_REGULAR;
    params.transform = transform;
    triangle_mesh_slice_to_expolygons_with_params(mesh, &params, z_mm_by_layer, slices_by_layer, layer_count);
}

void triangle_mesh_slice_to_expolygons_with_params(const triangle_mesh_handle *mesh,
                                                   const c_mesh_slicing_params *params,
                                                   const float *z_mm_by_layer,
                                                   expolygon_collection_handle **slices_by_layer,
                                                   uint32_t layer_count)
{
    if (mesh == nullptr || z_mm_by_layer == nullptr || slices_by_layer == nullptr || layer_count == 0)
        return;

    std::vector<float> zs(z_mm_by_layer, z_mm_by_layer + layer_count);

    Slic3r::MeshSlicingParamsEx native_params = Slic3r::to_mesh_slicing_params(params);
    indexed_triangle_set its = Slic3r::to_triangle_mesh(mesh)->its;
    if (native_params.trafo.rotation().determinant() < 0.)
        Slic3r::its_flip_triangles(its);
    std::vector<Slic3r::ExPolygons> sliced = Slic3r::slice_mesh_ex(its, zs, native_params);

    for (uint32_t idx = 0; idx < layer_count; ++idx) {
        if (slices_by_layer[idx] == nullptr)
            continue;
        Slic3r::ExPolygons &dst = *Slic3r::to_expolygons(slices_by_layer[idx]);
        dst = idx < sliced.size() ? std::move(sliced[idx]) : Slic3r::ExPolygons{};
        Slic3r::ensure_valid(dst, scale_i(native_params.resolution));
    }
}

} // extern "C"
