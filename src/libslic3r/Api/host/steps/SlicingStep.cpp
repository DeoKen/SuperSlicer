///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "SlicingStep.hpp"

#include <memory>
#include <vector>

#include "libslic3r/Api/host/ApiHostUtils.hpp"
#include "libslic3r/Api/internal/LayerRegionAccess.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/PrintObjectRegion.hpp"

namespace Slic3r::ApiHost::Steps {
namespace {

using SlicingLayerRange = PrintObjectRegions::LayerRangeRegions;
using SlicingVolumeRegion = PrintObjectRegions::VolumeRegion;

expolygon_collection_handle *layer_region_borrow_mutable_slices(layer_region_handle *me)
{
    return me == nullptr ? nullptr :
                           reinterpret_cast<expolygon_collection_handle *>(
                               &ApiInternal::LayerRegionAccess::slices_mutable(*reinterpret_cast<LayerRegion *>(me)));
}

const SlicingLayerRange *to_layer_range(const slicing_layer_range_handle *me)
{
    return reinterpret_cast<const SlicingLayerRange *>(me);
}

const SlicingVolumeRegion *to_volume_region(const slicing_volume_region_handle *me)
{
    return reinterpret_cast<const SlicingVolumeRegion *>(me);
}

const PrintObject *to_object(const object_handle *me)
{
    return reinterpret_cast<const PrintObject *>(me);
}

c_vec3f to_c_vec3f(const Vec3f &point)
{
    c_vec3f out = {};
    out.x = point.x();
    out.y = point.y();
    out.z = point.z();
    return out;
}

c_bounding_box3f to_c_bounding_box3f(const PrintObjectRegions::BoundingAlignedBox3f &box)
{
    c_bounding_box3f out = {};
    out.min = to_c_vec3f(box.min());
    out.max = to_c_vec3f(box.max());
    return out;
}

uint32_t slicing_layer_range_count(const object_handle *object)
{
    const PrintObject *native = to_object(object);
    return native == nullptr || native->shared_regions() == nullptr ?
        0u : uint32_t(native->shared_regions()->layer_ranges.size());
}

const slicing_layer_range_handle *slicing_layer_range_at(const object_handle *object, uint32_t idx)
{
    const PrintObject *native = to_object(object);
    if (native == nullptr || native->shared_regions() == nullptr)
        return nullptr;
    const std::vector<SlicingLayerRange> &ranges = native->shared_regions()->layer_ranges;
    return idx < ranges.size() ? reinterpret_cast<const slicing_layer_range_handle *>(&ranges[idx]) : nullptr;
}

coord_t slicing_layer_range_z_min(const slicing_layer_range_handle *range)
{
    return range == nullptr ? 0 : to_layer_range(range)->layer_height_range_.first;
}

coord_t slicing_layer_range_z_max(const slicing_layer_range_handle *range)
{
    return range == nullptr ? 0 : to_layer_range(range)->layer_height_range_.second;
}

const config_handle *slicing_layer_range_config(const slicing_layer_range_handle *range)
{
    return range == nullptr ?
        nullptr : ApiHost::to_config_handle(to_layer_range(range)->config);
}

uint32_t slicing_layer_range_volume_region_count(const slicing_layer_range_handle *range)
{
    return range == nullptr ? 0u : uint32_t(to_layer_range(range)->volume_regions.size());
}

const slicing_volume_region_handle *slicing_layer_range_volume_region_at(
    const slicing_layer_range_handle *range,
    uint32_t idx)
{
    if (range == nullptr)
        return nullptr;
    const std::vector<SlicingVolumeRegion> &volume_regions = to_layer_range(range)->volume_regions;
    return idx < volume_regions.size() ?
        reinterpret_cast<const slicing_volume_region_handle *>(&volume_regions[idx]) : nullptr;
}

const volume_handle *slicing_volume_region_volume(const slicing_volume_region_handle *volume_region)
{
    const SlicingVolumeRegion *native = to_volume_region(volume_region);
    return native == nullptr ? nullptr : reinterpret_cast<const volume_handle *>(native->model_volume);
}

int32_t slicing_volume_region_parent(const slicing_volume_region_handle *volume_region)
{
    const SlicingVolumeRegion *native = to_volume_region(volume_region);
    return native == nullptr ? -1 : native->parent;
}

int32_t slicing_volume_region_layer_region_idx(const slicing_volume_region_handle *volume_region)
{
    const SlicingVolumeRegion *native = to_volume_region(volume_region);
    return native == nullptr || native->region == nullptr ? -1 : native->region->print_object_region_id();
}

c_bounding_box3f slicing_volume_region_bbox(const slicing_volume_region_handle *volume_region)
{
    const SlicingVolumeRegion *native = to_volume_region(volume_region);
    return native == nullptr || native->bbox == nullptr ? c_bounding_box3f{} : to_c_bounding_box3f(*native->bbox);
}

} // namespace

std::unique_ptr<SlicingRunContext> make_slicing_run_context(Print &print, size_t object_idx)
{
    std::unique_ptr<SlicingRunContext> out = std::make_unique<SlicingRunContext>();
    out->context_step.print = reinterpret_cast<print_handle *>(&print);
    out->context_step.object = reinterpret_cast<object_handle *>(&print.object(object_idx));
    out->context_step.layer_region_borrow_mutable_slices = layer_region_borrow_mutable_slices;
    out->context_step.layer_range_count = slicing_layer_range_count;
    out->context_step.layer_range_at = slicing_layer_range_at;
    out->context_step.layer_range_z_min = slicing_layer_range_z_min;
    out->context_step.layer_range_z_max = slicing_layer_range_z_max;
    out->context_step.layer_range_config = slicing_layer_range_config;
    out->context_step.layer_range_volume_region_count = slicing_layer_range_volume_region_count;
    out->context_step.layer_range_volume_region_at = slicing_layer_range_volume_region_at;
    out->context_step.volume_region_volume = slicing_volume_region_volume;
    out->context_step.volume_region_parent = slicing_volume_region_parent;
    out->context_step.volume_region_layer_region_idx = slicing_volume_region_layer_region_idx;
    out->context_step.volume_region_bbox = slicing_volume_region_bbox;
    return out;
}

} // namespace Slic3r::ApiHost::Steps
