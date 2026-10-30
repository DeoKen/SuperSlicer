///|/ Copyright (c) Prusa Research 2021 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Lukáš Hejl @hejllukas
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <boost/log/trivial.hpp>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <cassert>
#include <cstddef>

#include "ClipperUtils.hpp"
#include "ElephantFootCompensation.hpp"
#include "I18N.hpp"
#include "Layer.hpp"
#include "MultiMaterialSegmentation.hpp"
#include "Print.hpp"
#include "ShortestPath.hpp"
#include "admesh/stl.h"
#include "libslic3r/Feature/Interlocking/InterlockingGenerator.hpp"
#include "libslic3r/Feature/FullSpectrum/VirtualExtruder.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/BridgeDetector.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/format.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ObjectID.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h"


namespace Slic3r {

LayerPtrs new_layers(
    PrintObject                 *print_object,
    // Object layers (pairs of bottom/top Z coordinate), without the raft.
    const std::vector<coordf_t> &object_layers)
{
    LayerPtrs out;
    out.reserve(object_layers.size());
    auto     id   = int(print_object->slicing_parameters().raft_layers());
    coordf_t zmin = print_object->slicing_parameters().object_print_z_min;
    Layer   *prev = nullptr;
    for (size_t i_layer = 0; i_layer < object_layers.size(); i_layer += 2) {
        coordf_t lo = object_layers[i_layer];
        coordf_t hi = object_layers[i_layer + 1];
        coordf_t slice_z = 0.5 * (lo + hi);
        Layer *layer = new Layer(id ++, print_object, hi - lo, hi + zmin, slice_z);
        out.emplace_back(layer);
        if (prev != nullptr) {
            prev->upper_layer = layer;
            layer->lower_layer = prev;
        }
        prev = layer;
    }
    return out;
}

// Slice single triangle mesh.
static std::vector<ExPolygons> slice_volume(
    const ModelVolume             &volume,
    const std::vector<float>      &zs, 
    const MeshSlicingParamsEx     &params,
    const std::function<void()>   &throw_on_cancel_callback)
{
    std::vector<ExPolygons> layers;
    if (! zs.empty()) {
        indexed_triangle_set its = volume.mesh().its;
        if (its.indices.size() > 0) {
            MeshSlicingParamsEx params2 { params };
            params2.trafo = params2.trafo * volume.get_matrix();
            if (params2.trafo.rotation().determinant() < 0.)
                its_flip_triangles(its);
            layers = slice_mesh_ex(its, zs, params2, throw_on_cancel_callback);
            throw_on_cancel_callback();
        }
    }
    return layers;
}

// Slice single triangle mesh.
// Filter the zs not inside the ranges. The ranges are closed at the bottom and open at the top, they are sorted lexicographically and non overlapping.
static std::vector<ExPolygons> slice_volume(
    const ModelVolume                           &volume,
    const std::vector<float>                    &z,
    const std::vector<t_layer_height_range>     &ranges,
    const MeshSlicingParamsEx                   &params,
    const std::function<void()>                 &throw_on_cancel_callback)
{
    std::vector<ExPolygons> out;
    if (! z.empty() && ! ranges.empty()) {
        if (ranges.size() == 1 && z.front() >= ranges.front().first && z.back() < ranges.front().second) {
            // All layers fit into a single range.
            out = slice_volume(volume, z, params, throw_on_cancel_callback);
        } else {
            std::vector<float>                     z_filtered;
            std::vector<std::pair<size_t, size_t>> n_filtered;
            z_filtered.reserve(z.size());
            n_filtered.reserve(2 * ranges.size());
            size_t i = 0;
            for (const t_layer_height_range &range : ranges) {
                for (; i < z.size() && z[i] < range.first; ++ i) ;
                size_t first = i;
                for (; i < z.size() && z[i] < range.second; ++ i)
                    z_filtered.emplace_back(z[i]);
                if (i > first)
                    n_filtered.emplace_back(std::make_pair(first, i));
            }
            if (! n_filtered.empty()) {
                std::vector<ExPolygons> layers = slice_volume(volume, z_filtered, params, throw_on_cancel_callback);
                out.assign(z.size(), ExPolygons());
                i = 0;
                for (const std::pair<size_t, size_t> &span : n_filtered)
                    for (size_t j = span.first; j < span.second; ++ j)
                        out[j] = std::move(layers[i ++]);
            }
        }
    }
    return out;
}

struct VolumeSlices
{
    ObjectID                volume_id;
    std::vector<ExPolygons> slices;
};

static inline bool model_volume_needs_slicing(const ModelVolume &mv)
{
    ModelVolumeType type = mv.type();
    return type == ModelVolumeType::MODEL_PART || type == ModelVolumeType::NEGATIVE_VOLUME || type == ModelVolumeType::PARAMETER_MODIFIER;
}

// Slice printable volumes, negative volumes and modifier volumes, sorted by ModelVolume::id().
// Apply closing radius.
// Apply positive XY compensation to ModelVolumeType::MODEL_PART and ModelVolumeType::PARAMETER_MODIFIER, not to ModelVolumeType::NEGATIVE_VOLUME.
// Apply contour simplification.
static std::vector<VolumeSlices> slice_volumes_inner(
    const PrintConfig                                        &print_config,
    const PrintObjectConfig                                  &print_object_config,
    const Transform3d                                        &object_trafo,
    ModelVolumePtrs                                           model_volumes,
    const std::vector<PrintObjectRegions::LayerRangeRegions> &layer_ranges,
    const std::vector<float>                                 &zs,
    const std::function<void()>                              &throw_on_cancel_callback)
{
    model_volumes_sort_by_id(model_volumes);

    std::vector<VolumeSlices> out;
    out.reserve(model_volumes.size());

    std::vector<t_layer_height_range> slicing_ranges;
    if (layer_ranges.size() > 1)
        slicing_ranges.reserve(layer_ranges.size());

    MeshSlicingParamsEx params_base;
    params_base.closing_radius = print_object_config.slice_closing_radius.value;
    params_base.extra_offset   = 0;
    params_base.trafo          = object_trafo;
    params_base.resolution     = print_config.resolution.value;

    switch (print_object_config.slicing_mode.value) {
    case SlicingMode::Regular:    params_base.mode = MeshSlicingParams::SlicingMode::Regular; break;
    case SlicingMode::EvenOdd:    params_base.mode = MeshSlicingParams::SlicingMode::EvenOdd; break;
    case SlicingMode::CloseHoles: params_base.mode = MeshSlicingParams::SlicingMode::Positive; break;
    }

    params_base.mode_below     = params_base.mode;

    const size_t num_extruders = print_config.nozzle_diameter.size();
    const bool   is_mm_painted = num_extruders > 1 && std::any_of(model_volumes.cbegin(), model_volumes.cend(), [](const ModelVolume *mv) { return mv->is_mm_painted(); });
    const auto   extra_offset  = is_mm_painted ? 0.f : std::max(0.f, float(print_object_config.xy_size_compensation.value));

    for (const ModelVolume *model_volume : model_volumes)
        if (model_volume_needs_slicing(*model_volume)) {
            MeshSlicingParamsEx params { params_base };
            if (! model_volume->is_negative_volume())
                params.extra_offset = extra_offset;
            if (layer_ranges.size() == 1) {
                if (const PrintObjectRegions::LayerRangeRegions &layer_range = layer_ranges.front(); layer_range.has_volume(model_volume->id())) {
                    if (model_volume->is_model_part() && print_config.spiral_vase) {
                        auto it = std::find_if(layer_range.volume_regions.begin(), layer_range.volume_regions.end(), 
                            [model_volume](const auto &slice){ return model_volume == slice.model_volume; });
                        params.mode = MeshSlicingParams::SlicingMode::PositiveLargestContour;
                        // Slice the bottom layers with SlicingMode::Regular.
                        // This needs to be in sync with LayerRegion::make_perimeters() spiral_vase!
                        const PrintRegionConfig &region_config = it->region->config();
                        params.slicing_mode_normal_below_layer = size_t(region_config.bottom_solid_layers.value);
                        for (; params.slicing_mode_normal_below_layer < zs.size() && zs[params.slicing_mode_normal_below_layer] < region_config.bottom_solid_min_thickness - EPSILON;
                            ++ params.slicing_mode_normal_below_layer);
                    }
                    out.push_back({
                        model_volume->id(), 
                        slice_volume(*model_volume, zs, params, throw_on_cancel_callback)
                    });
                }
            } else {
                assert(! print_config.spiral_vase);
                slicing_ranges.clear();
                for (const PrintObjectRegions::LayerRangeRegions &layer_range : layer_ranges)
                    if (layer_range.has_volume(model_volume->id()))
                        slicing_ranges.emplace_back(layer_range.layer_height_range);
                if (! slicing_ranges.empty())
                    out.push_back({ 
                        model_volume->id(), 
                        slice_volume(*model_volume, zs, slicing_ranges, params, throw_on_cancel_callback)
                    });
            }
            if (! out.empty() && out.back().slices.empty())
                out.pop_back();
        }

    return out;
}

static inline VolumeSlices& volume_slices_find_by_id(std::vector<VolumeSlices> &volume_slices, const ObjectID id)
{
    auto it = lower_bound_by_predicate(volume_slices.begin(), volume_slices.end(), [id](const VolumeSlices &vs) { return vs.volume_id < id; });
    assert(it != volume_slices.end() && it->volume_id == id);
    return *it;
}

static inline bool overlap_in_xy(const PrintObjectRegions::BoundingBox &l, const PrintObjectRegions::BoundingBox &r)
{
    return ! (l.max().x() < r.min().x() || l.min().x() > r.max().x() ||
              l.max().y() < r.min().y() || l.min().y() > r.max().y());
}

static std::vector<PrintObjectRegions::LayerRangeRegions>::const_iterator layer_range_first(const std::vector<PrintObjectRegions::LayerRangeRegions> &layer_ranges, double z)
{
    auto  it = lower_bound_by_predicate(layer_ranges.begin(), layer_ranges.end(),
        [z](const PrintObjectRegions::LayerRangeRegions &lr) { return lr.layer_height_range.second < z; });
    assert(it != layer_ranges.end() && it->layer_height_range.first <= z && z <= it->layer_height_range.second);
    if (z == it->layer_height_range.second)
        if (auto it_next = it; ++ it_next != layer_ranges.end() && it_next->layer_height_range.first == z)
            it = it_next;
    assert(it != layer_ranges.end() && it->layer_height_range.first <= z && z <= it->layer_height_range.second);
    return it;
}

static std::vector<PrintObjectRegions::LayerRangeRegions>::const_iterator layer_range_next(
    const std::vector<PrintObjectRegions::LayerRangeRegions>            &layer_ranges, 
    std::vector<PrintObjectRegions::LayerRangeRegions>::const_iterator   it,
    double                                                               z)
{
    for (; it->layer_height_range.second <= z; ++ it)
        assert(it != layer_ranges.end());
    assert(it != layer_ranges.end() && it->layer_height_range.first <= z && z < it->layer_height_range.second);
    return it;
}

static std::vector<std::vector<ExPolygons>> slices_to_regions(
    ModelVolumePtrs                                           model_volumes,
    const PrintObjectRegions                                 &print_object_regions,
    const std::vector<float>                                 &zs,
    std::vector<VolumeSlices>                               &&volume_slices,
    const std::function<void()>                              &throw_on_cancel_callback)
{
    model_volumes_sort_by_id(model_volumes);

    std::vector<std::vector<ExPolygons>> slices_by_region(print_object_regions.all_regions.size(), std::vector<ExPolygons>(zs.size(), ExPolygons()));

    // First shuffle slices into regions if there is no overlap with another region possible, collect zs of the complex cases.
    std::vector<std::pair<size_t, float>> zs_complex;
    {
        size_t z_idx = 0;
        for (const PrintObjectRegions::LayerRangeRegions &layer_range : print_object_regions.layer_ranges) {
            for (; z_idx < zs.size() && zs[z_idx] < layer_range.layer_height_range.first; ++ z_idx) ;
            if (layer_range.volume_regions.empty()) {
            } else if (layer_range.volume_regions.size() == 1) {
                const ModelVolume *model_volume = layer_range.volume_regions.front().model_volume;
                assert(model_volume != nullptr);
                if (model_volume->is_model_part()) {
                    VolumeSlices &slices_src = volume_slices_find_by_id(volume_slices, model_volume->id());
                    auto         &slices_dst = slices_by_region[layer_range.volume_regions.front().region->print_object_region_id()];
                    for (; z_idx < zs.size() && zs[z_idx] < layer_range.layer_height_range.second; ++ z_idx)
                        slices_dst[z_idx] = std::move(slices_src.slices[z_idx]);
                }
            } else {
                zs_complex.reserve(zs.size());
                for (; z_idx < zs.size() && zs[z_idx] < layer_range.layer_height_range.second; ++ z_idx) {
                    float z                          = zs[z_idx];
                    int   idx_first_printable_region = -1;
                    bool  complex                    = false;
                    for (int idx_region = 0; idx_region < int(layer_range.volume_regions.size()); ++ idx_region) {
                        const PrintObjectRegions::VolumeRegion &region = layer_range.volume_regions[idx_region];
                        if (region.bbox->min().z() <= z && region.bbox->max().z() >= z) {
                            if (idx_first_printable_region == -1 && region.model_volume->is_model_part())
                                idx_first_printable_region = idx_region;
                            else if (idx_first_printable_region != -1) {
                                // Test for overlap with some other region.
                                for (int idx_region2 = idx_first_printable_region; idx_region2 < idx_region; ++ idx_region2) {
                                    const PrintObjectRegions::VolumeRegion &region2 = layer_range.volume_regions[idx_region2];
                                    if (region2.bbox->min().z() <= z && region2.bbox->max().z() >= z && overlap_in_xy(*region.bbox, *region2.bbox)) {
                                        complex = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    if (complex)
                        zs_complex.push_back({ z_idx, z });
                    else if (idx_first_printable_region >= 0) {
                        const PrintObjectRegions::VolumeRegion &region = layer_range.volume_regions[idx_first_printable_region];
                        slices_by_region[region.region->print_object_region_id()][z_idx] = std::move(volume_slices_find_by_id(volume_slices, region.model_volume->id()).slices[z_idx]);
                    }
                }
            }
            throw_on_cancel_callback();
        }
    }

    // Second perform region clipping and assignment in parallel.
    if (! zs_complex.empty()) {
        std::vector<std::vector<VolumeSlices*>> layer_ranges_regions_to_slices(print_object_regions.layer_ranges.size(), std::vector<VolumeSlices*>());
        for (const PrintObjectRegions::LayerRangeRegions &layer_range : print_object_regions.layer_ranges) {
            std::vector<VolumeSlices*> &layer_range_regions_to_slices = layer_ranges_regions_to_slices[&layer_range - print_object_regions.layer_ranges.data()];
            layer_range_regions_to_slices.reserve(layer_range.volume_regions.size());
            for (const PrintObjectRegions::VolumeRegion &region : layer_range.volume_regions)
                layer_range_regions_to_slices.push_back(&volume_slices_find_by_id(volume_slices, region.model_volume->id()));
        }
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, zs_complex.size()),
            [&slices_by_region, &print_object_regions, &zs_complex, &layer_ranges_regions_to_slices, &throw_on_cancel_callback]
                (const tbb::blocked_range<size_t> &range) {
                float z              = zs_complex[range.begin()].second;
                auto  it_layer_range = layer_range_first(print_object_regions.layer_ranges, z);
                // Per volume_regions slices at this Z height.
                struct RegionSlice { 
                    ExPolygons  expolygons;
                    // Identifier of this region in PrintObjectRegions::all_regions
                    int         region_id;
                    ObjectID    volume_id;
                    bool operator<(const RegionSlice &rhs) const {
                        bool this_empty = this->region_id < 0 || this->expolygons.empty();
                        bool rhs_empty  = rhs.region_id < 0 || rhs.expolygons.empty();
                        // Sort the empty items to the end of the list.
                        // Sort by region_id & volume_id lexicographically.
                        return ! this_empty && (rhs_empty || (this->region_id < rhs.region_id || (this->region_id == rhs.region_id && volume_id < volume_id)));
                    }
                };
                std::vector<RegionSlice> temp_slices;
                for (size_t zs_complex_idx = range.begin(); zs_complex_idx < range.end(); ++ zs_complex_idx) {
                    auto [z_idx, z] = zs_complex[zs_complex_idx];
                    it_layer_range = layer_range_next(print_object_regions.layer_ranges, it_layer_range, z);
                    const PrintObjectRegions::LayerRangeRegions &layer_range = *it_layer_range;
                    {
                        std::vector<VolumeSlices*> &layer_range_regions_to_slices = layer_ranges_regions_to_slices[it_layer_range - print_object_regions.layer_ranges.begin()];
                        // Per volume_regions slices at thiz Z height.
                        temp_slices.clear();
                        temp_slices.reserve(layer_range.volume_regions.size());
                        for (VolumeSlices* &slices : layer_range_regions_to_slices) {
                            const PrintObjectRegions::VolumeRegion &volume_region = layer_range.volume_regions[&slices - layer_range_regions_to_slices.data()];
                            temp_slices.push_back({ std::move(slices->slices[z_idx]), volume_region.region ? volume_region.region->print_object_region_id() : -1, volume_region.model_volume->id() });
                        }
                    }
                    for (int idx_region = 0; idx_region < int(layer_range.volume_regions.size()); ++ idx_region)
                        if (! temp_slices[idx_region].expolygons.empty()) {
                            const PrintObjectRegions::VolumeRegion &region = layer_range.volume_regions[idx_region];
                            if (region.model_volume->is_modifier()) {
                                assert(region.parent > -1);
                                bool next_region_same_modifier = idx_region + 1 < int(temp_slices.size()) && layer_range.volume_regions[idx_region + 1].model_volume == region.model_volume;
                                RegionSlice &parent_slice = temp_slices[region.parent];
                                RegionSlice &this_slice   = temp_slices[idx_region];
                                ExPolygons   source       = std::move(this_slice.expolygons);
                                if (parent_slice.expolygons.empty()) {
                                    this_slice  .expolygons.clear();
                                } else {
                                    this_slice  .expolygons = intersection_ex(parent_slice.expolygons, source);
                                    parent_slice.expolygons = diff_ex        (parent_slice.expolygons, source);
                                }
                                if (next_region_same_modifier)
                                    // To be used in the following iteration.
                                    temp_slices[idx_region + 1].expolygons = std::move(source);
                            } else if (region.model_volume->is_model_part() || region.model_volume->is_negative_volume()) {
                                // Clip every non-zero region preceding it.
                                for (int idx_region2 = 0; idx_region2 < idx_region; ++ idx_region2)
                                    if (! temp_slices[idx_region2].expolygons.empty()) {
                                        if (const PrintObjectRegions::VolumeRegion &region2 = layer_range.volume_regions[idx_region2];
                                            ! region2.model_volume->is_negative_volume() && overlap_in_xy(*region.bbox, *region2.bbox))
                                            temp_slices[idx_region2].expolygons = diff_ex(temp_slices[idx_region2].expolygons, temp_slices[idx_region].expolygons);
                                    }
                            }
                        }
                    // Sort by region_id, push empty slices to the end.
                    std::sort(temp_slices.begin(), temp_slices.end());
                    // Remove the empty slices.
                    temp_slices.erase(std::find_if(temp_slices.begin(), temp_slices.end(), [](const auto &slice) { return slice.region_id == -1 || slice.expolygons.empty(); }), temp_slices.end());
                    // Merge slices and store them to the output.
                    for (int i = 0; i < int(temp_slices.size());) {
                        // Find a range of temp_slices with the same region_id.
                        int j = i;
                        bool merged = false;
                        ExPolygons &expolygons = temp_slices[i].expolygons;
                        for (++ j; j < int(temp_slices.size()) && temp_slices[i].region_id == temp_slices[j].region_id; ++ j)
                            if (ExPolygons &expolygons2 = temp_slices[j].expolygons; ! expolygons2.empty()) {
                                if (expolygons.empty()) {
                                    expolygons = std::move(expolygons2);
                                } else {
                                    append(expolygons, std::move(expolygons2));
                                    merged = true;
                                }
                            }
                        if (merged)
                            expolygons = closing_ex(expolygons, float(scale_(EPSILON)));
                        slices_by_region[temp_slices[i].region_id][z_idx] = std::move(expolygons);
                        i = j;
                    }
                    throw_on_cancel_callback();
                }
            });
    }

    return slices_by_region;
}

// Layer::slicing_errors is no more set since 1.41.1 or possibly earlier, thus this code
// was not really functional for a long day and nobody missed it.
// Could we reuse this fixing code one day?
/*
std::string fix_slicing_errors(LayerPtrs &layers, const std::function<void()> &throw_if_canceled)
{
    // Collect layers with slicing errors.
    // These layers will be fixed in parallel.
    std::vector<size_t> buggy_layers;
    buggy_layers.reserve(layers.size());
    for (size_t idx_layer = 0; idx_layer < layers.size(); ++ idx_layer)
        if (layers[idx_layer]->slicing_errors)
            buggy_layers.push_back(idx_layer);

    BOOST_LOG_TRIVIAL(debug) << "Slicing objects - fixing slicing errors in parallel - begin";
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, buggy_layers.size()),
        [&layers, &throw_if_canceled, &buggy_layers](const tbb::blocked_range<size_t>& range) {
            for (size_t buggy_layer_idx = range.begin(); buggy_layer_idx < range.end(); ++ buggy_layer_idx) {
                throw_if_canceled();
                size_t idx_layer = buggy_layers[buggy_layer_idx];
                Layer *layer     = layers[idx_layer];
                assert(layer->slicing_errors);
                // Try to repair the layer surfaces by merging all contours and all holes from neighbor layers.
                // BOOST_LOG_TRIVIAL(trace) << "Attempting to repair layer" << idx_layer;
                for (size_t region_id = 0; region_id < layer->region_count(); ++ region_id) {
                    LayerRegion *layerm = layer->get_region(region_id);
                    // Find the first valid layer below / above the current layer.
                    const Surfaces *upper_surfaces = nullptr;
                    const Surfaces *lower_surfaces = nullptr;
                    for (size_t j = idx_layer + 1; j < layers.size(); ++ j)
                        if (! layers[j]->slicing_errors) {
                            upper_surfaces = &layers[j]->regions()[region_id]->slices().surfaces;
                            break;
                        }
                    for (int j = int(idx_layer) - 1; j >= 0; -- j)
                        if (! layers[j]->slicing_errors) {
                            lower_surfaces = &layers[j]->regions()[region_id]->slices().surfaces;
                            break;
                        }
                    // Collect outer contours and holes from the valid layers above & below.
                    Polygons outer;
                    outer.reserve(
                        ((upper_surfaces == nullptr) ? 0 : upper_surfaces->size()) + 
                        ((lower_surfaces == nullptr) ? 0 : lower_surfaces->size()));
                    size_t num_holes = 0;
                    if (upper_surfaces)
                        for (const auto &surface : *upper_surfaces) {
                            outer.push_back(surface.expolygon.contour);
                            num_holes += surface.expolygon.holes.size();
                        }
                    if (lower_surfaces)
                        for (const auto &surface : *lower_surfaces) {
                            outer.push_back(surface.expolygon.contour);
                            num_holes += surface.expolygon.holes.size();
                        }
                    Polygons holes;
                    holes.reserve(num_holes);
                    if (upper_surfaces)
                        for (const auto &surface : *upper_surfaces)
                            polygons_append(holes, surface.expolygon.holes);
                    if (lower_surfaces)
                        for (const auto &surface : *lower_surfaces)
                            polygons_append(holes, surface.expolygon.holes);
                    layerm->m_slices.set(diff_ex(union_(outer), holes), stInternal);
                }
                // Update layer slices after repairing the single regions.
                layer->make_slices();
            }
        });
    throw_if_canceled();
    BOOST_LOG_TRIVIAL(debug) << "Slicing objects - fixing slicing errors in parallel - end";

    // remove empty layers from bottom
    while (! layers.empty() && (layers.front()->lslices.empty() || layers.front()->empty())) {
        delete layers.front();
        layers.erase(layers.begin());
        layers.front()->lower_layer = nullptr;
        for (size_t i = 0; i < layers.size(); ++ i)
            layers[i]->set_id(layers[i]->id() - 1);
    }

    return buggy_layers.empty() ? "" :
        "The model has overlapping or self-intersecting facets. I tried to repair it, "
        "however you might want to check the results or repair the input file and retry.\n";
}
*/

// ---- Overhangs cut (SuperSlicer overhangs_max_slope, overhangs_bridge_threshold) ----

namespace {

// Counter-clockwise angle from vector a to vector b, in (-PI, PI].
double overhangs_cut_angle_ccw(const Point &a, const Point &b)
{
    const double ax = double(a.x()), ay = double(a.y()), bx = double(b.x()), by = double(b.y());
    return std::atan2(ax * by - ay * bx, ax * bx + ay * by);
}

Point overhangs_cut_point_at(const Point &from, const Point &to, double distance)
{
    const Vec2d  v   = (to - from).cast<double>();
    const double len = v.norm();
    return len <= 0. ? from : Point(from + (v * (distance / len)).cast<coord_t>());
}

// Removes zig-zags left by the cut (ported from SuperSlicer smoothen()): very short segments are merged into their middle,
// short segments between a convex and a concave corner (less sharp than threshold) are flattened.
void overhangs_cut_smoothen(Polygon &poly, double max_length, double threshold)
{
    Points &pts = poly.points;
    const auto prev_idx = [&pts](size_t i, size_t back) { return (i + pts.size() - back) % pts.size(); };
    double min_d2 = (max_length / 20) * (max_length / 20);
    for (size_t i = 0; i < pts.size() && pts.size() > 3; ++ i) {
        const size_t im1 = prev_idx(i, 1);
        if ((pts[i] - pts[im1]).cast<double>().squaredNorm() < min_d2) {
            pts[im1] = Line(pts[im1], pts[i]).midpoint();
            pts.erase(pts.begin() + i);
            -- i;
        }
    }
    const double max_d2 = max_length * max_length;
    min_d2 = (max_length / 5) * (max_length / 5);
    bool   previous_culled   = false;
    double prev_angle_culled = 0;
    for (size_t i = 0; i < pts.size(); ++ i) {
        if (pts.size() <= 3)
            return;
        bool next_culled = false;
        const size_t im1 = prev_idx(i, 1), im2 = prev_idx(i, 2), ip1 = (i + 1) % pts.size();
        const double my_d2 = (pts[i] - pts[im1]).cast<double>().squaredNorm();
        if (my_d2 < max_d2) {
            const bool   too_short  = my_d2 < min_d2;
            const double prev_angle = previous_culled ? prev_angle_culled : overhangs_cut_angle_ccw(pts[im2] - pts[im1], pts[i] - pts[im1]);
            const double next_angle = overhangs_cut_angle_ccw(pts[im1] - pts[i], pts[ip1] - pts[i]);
            if ((prev_angle < 0 && next_angle > 0) || (prev_angle > 0 && next_angle < 0)) {
                if (too_short || (std::abs(prev_angle) < threshold && std::abs(next_angle) < threshold)) {
                    const Point mid = Line(pts[im1], pts[i]).midpoint();
                    if ((pts[im2] - pts[im1]).cast<double>().squaredNorm() > max_d2) {
                        pts[im1] = overhangs_cut_point_at(pts[im1], pts[im2], max_length);
                        if ((pts[i] - pts[ip1]).cast<double>().squaredNorm() > max_d2)
                            pts[i] = overhangs_cut_point_at(pts[i], pts[ip1], max_length);
                        else
                            pts[i] = mid;
                    } else {
                        pts[im1] = mid;
                        if ((pts[i] - pts[ip1]).cast<double>().squaredNorm() > max_d2) {
                            pts[i] = overhangs_cut_point_at(pts[i], pts[ip1], max_length);
                        } else {
                            pts.erase(pts.begin() + i);
                            -- i;
                        }
                    }
                    next_culled       = true;
                    prev_angle_culled = next_angle;
                }
            }
        }
        previous_culled = next_culled;
    }
}

// The part of an unsupported area that can be bridged: lines at the bridge angle found by the BridgeDetector, spaced by the
// bridge line spacing, anchored on the lower slices at both ends (two separate anchor pieces) and, if max_length > 0, not
// longer than max_length over the unsupported area. Like SuperSlicer's BridgeDetector::coverage(), but PrusaSlicer's own
// detector (used for the bridges) is left unchanged. SuperSlicer's max length test is never reached (good_line is still
// false there); this one is applied.
// A hole of the area that stays empty in the layer above (`upper`, nullptr = nothing above), like the screw hole through
// the ceiling of a counterbore, is bridged over: the lines may cross it (as the bridge infill does). Without that the
// lines next to it end in the hole, aren't anchored at both ends, and the ceiling was cut into layers of blobs. An area
// around a hole that closes in the layer above (a step of a dome around its opening) is a slope, not a bridge: no coverage
// (straight lines across the ring between its supported edges left bumps in the cut).
// True if a hole of the area doesn't stay empty in the layer above (`upper`, nullptr = nothing above): the area is a ring
// around an opening that closes further up (a step of a dome), a slope, not a bridge.
bool overhangs_cut_around_closing_hole(const ExPolygon &area, const ExPolygons *upper, coord_t spacing)
{
    if (upper == nullptr)
        return false;
    for (const Polygon &hole : area.holes) {
        Polygon inside = hole;
        inside.make_counter_clockwise();
        // Any material of the layer above inside the hole (a few microns in, against rounding): a dome closes by only the
        // slope per layer, a straight screw hole has the same outline on every layer.
        const Polygons shrunk = offset(inside, -float(scale_(0.02)));
        if (shrunk.empty() || Slic3r::area(intersection(shrunk, to_polygons(*upper))) > scale_(0.1) * scale_(0.1))
            return true;
    }
    return false;
}

ExPolygons overhangs_cut_bridge_coverage(const ExPolygon &area, const ExPolygons &lower, const ExPolygons &material, const ExPolygons *upper, coord_t spacing, double max_length)
{
    if (overhangs_cut_around_closing_hole(area, upper, spacing))
        return {};
    const ExPolygon &span = area;
    BridgeDetector detector(span, lower, spacing);
    if (! detector.detect_angle())
        return {};
    const double     angle   = detector.angle;
    const Polygons   grown   = offset(span, float(spacing));
    const ExPolygons anchors = intersection_ex(grown, lower);
    if (anchors.empty())
        return {};
    const Polygons anchor_polys = to_polygons(anchors);
    const Polygons area_polys   = to_polygons(span);
    Lines lines;
    {
        const BoundingBox bbox = get_extents_rotated(grown, -angle);
        const double s = std::sin(angle), c = std::cos(angle);
        for (coord_t y = bbox.min.y() + spacing / 2; y <= bbox.max.y(); y += spacing)
            lines.emplace_back(Point(coord_t(std::round(c * bbox.min.x() - s * y)), coord_t(std::round(c * y + s * bbox.min.x()))),
                               Point(coord_t(std::round(c * bbox.max.x() - s * y)), coord_t(std::round(c * y + s * bbox.max.x()))));
    }
    Polygons     covered;
    const double half = 0.5 * spacing + SCALED_EPSILON;
    // Clipped half as far out as the anchors reach, so that the ends of the lines are inside the anchors, not on their edge,
    // and to the material of the layer shrunk by as much: a line passing over (or grazing) empty space of the layer, like a
    // screw hole, is cut there and not anchored, so the edge of the bridged part next to it is one straight line. (Growing
    // only the holes of the area missed a screw hole that splits the area in two, and the lines next to it kinked the edge.)
    const Polygons clip = intersection(offset(span, 0.5f * float(spacing)), offset(material, -0.5f * float(spacing)));
    for (const Line &line : intersection_ln(lines, clip)) {
        if (! expolygons_contain(anchors, line.a) || ! expolygons_contain(anchors, line.b))
            continue;
        // Anchored on two separate pieces, i.e. it crosses the unsupported area.
        if (intersection_ln(Lines{ line }, anchor_polys).size() < 2)
            continue;
        if (max_length > 0) {
            double over = 0;
            for (const Line &l : intersection_ln(Lines{ line }, area_polys))
                over += l.length();
            if (over > max_length)
                continue;
        }
        const Vec2d d = (line.b - line.a).cast<double>().normalized() * half;
        const Vec2d n(-d.y(), d.x());
        const Vec2d a = line.a.cast<double>(), b = line.b.cast<double>();
        Polygon rect(Points{ Point((a - n).cast<coord_t>()), Point((b - n).cast<coord_t>()), Point((b + n).cast<coord_t>()), Point((a + n).cast<coord_t>()) });
        rect.make_counter_clockwise();
        covered.emplace_back(std::move(rect));
    }
    if (covered.empty())
        return {};
    covered = union_(covered);
    // Up to the tangents of the holes of the layer along the lines: where the lines fall left up to a line width between
    // the last line and the hole, so the hole printed on the bridges (a screw hole on the square of a counterbore ceiling)
    // had its perimeter over the edge of the bridged area or past it, randomly overhang. Straight, across the area (grown
    // along the hole's outline, the edges of the bridges got bumps).
    const BoundingBox bbox = get_extents(grown);
    const BoundingBox rotated_bbox = get_extents_rotated(grown, -angle);
    Polygons slabs;
    for (const ExPolygon &island : material)
        if (island.contour.bounding_box().overlap(bbox))
            for (const Polygon &hole : island.holes)
                if (hole.bounding_box().overlap(bbox)) {
                    const BoundingBox hb = get_extents_rotated(hole, -angle);
                    for (const coord_t y : { hb.max.y(), hb.min.y() - spacing }) {
                        Polygon slab(Points{ Point(rotated_bbox.min.x(), y), Point(rotated_bbox.max.x(), y),
                                             Point(rotated_bbox.max.x(), y + spacing), Point(rotated_bbox.min.x(), y + spacing) });
                        slab.rotate(angle);
                        slabs.emplace_back(std::move(slab));
                    }
                }
    if (! slabs.empty())
        covered = union_(covered, intersection(offset(covered, float(spacing)), slabs));
    return intersection_ex(covered, ExPolygons{ area });
}

// The same outlines in the same order with the same first points whatever the order of the input: the layers come out of
// the parallel slicing in an order that changes from run to run, the clipper results of the cut followed it, and the
// G-code changed between two exports of the same plate (the stock slices don't go through clipper here).
void overhangs_cut_canonical(ExPolygons &expolygons)
{
    const auto less = [](const Point &a, const Point &b) { return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y()); };
    const auto start_at_min = [&less](Polygon &polygon) {
        if (! polygon.points.empty())
            std::rotate(polygon.points.begin(), std::min_element(polygon.points.begin(), polygon.points.end(), less), polygon.points.end());
    };
    for (ExPolygon &expolygon : expolygons) {
        start_at_min(expolygon.contour);
        for (Polygon &hole : expolygon.holes)
            start_at_min(hole);
        std::sort(expolygon.holes.begin(), expolygon.holes.end(),
            [&less](const Polygon &a, const Polygon &b) { return ! b.points.empty() && (a.points.empty() || less(a.points.front(), b.points.front())); });
    }
    std::sort(expolygons.begin(), expolygons.end(), [&less](const ExPolygon &a, const ExPolygon &b) {
        return ! b.contour.points.empty() && (a.contour.points.empty() || less(a.contour.points.front(), b.contour.points.front()));
    });
}

} // namespace

// Ported from SuperSlicer PrintObject::_max_overhang_threshold() (2.7.63): cuts the region slices, layer by layer from the
// bottom, so that no layer sticks out past the layer below by more than overhangs_max_slope, except over areas that can be
// bridged (overhangs_bridge_threshold). SuperSlicer's overhangs_bridge_upper_layers (leave empty what a bridge of the next
// layers will cover) was ported and removed on 2026-10-30: it only made those bridges a little longer.
// Runs after slice_volumes() (after the XY and hole compensations), before the slices are backed up; sequential, each layer
// uses the cut layer below.
bool PrintObject::apply_overhangs_cut()
{
    coord_t max_nozzle = 0;
    for (unsigned int extruder : this->object_extruders())
        max_nozzle = std::max(max_nozzle, coord_t(scale_(m_print->config().nozzle_diameter.get_at(extruder))));
    if (max_nozzle == 0)
        max_nozzle = coord_t(scale_(0.4));
    const auto enlargement_of = [max_nozzle](const PrintRegion &region) {
        return coord_t(scale_(region.config().overhangs_max_slope.get_abs_value(unscale<double>(max_nozzle))));
    };
    // Round joins: the cut outline of a layer is the one below grown, layer after layer; with mitered joins a round hole
    // turned into a lumpy polygon after a few layers at a small slope.
    const double round_tolerance = scale_(0.005);
    bool enabled = false;
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id)
        enabled |= enlargement_of(this->printing_region(region_id)) > 0;
    if (! enabled)
        return false;

    // The layer below as sliced, before its cut.
    ExPolygons lower_uncut = m_layers.front()->lslices;
    for (size_t layer_id = 1; layer_id < m_layers.size(); ++ layer_id) {
        m_print->throw_if_canceled();
        Layer       *layer       = m_layers[layer_id];
        const Layer *lower_layer = m_layers[layer_id - 1];
        bool modified = false;
        ExPolygons uncut = layer->lslices;
        // Where the cut removed material of the layer below.
        const Polygons lower_cut_away = diff(lower_uncut, lower_layer->lslices);
        const ExPolygons supported_area = intersection_ex(layer->lslices, lower_layer->lslices);
        for (LayerRegion *layerm : layer->m_regions) {
            const PrintRegionConfig &config      = layerm->region().config();
            const coord_t            enlargement = enlargement_of(layerm->region());
            if (enlargement <= 0 || layerm->slices().empty())
                continue;
            const coord_t    spacing       = layerm->bridging_flow(frSolidInfill).scaled_spacing();
            const double     max_bridge    = config.overhangs_bridge_threshold.value > 0 ? scale_(config.overhangs_bridge_threshold.value) : 0.;
            const bool       bridges       = config.overhangs_bridge_threshold.value != 0;
            const ExPolygons region_slices = to_expolygons(layerm->slices().surfaces);
            ExPolygons bridged_area;
            if (bridges) {
                const ExPolygons unsupported = diff_ex(region_slices, lower_layer->lslices, ApplySafetyOffset::Yes);
                if (! unsupported.empty()) {
                    const ExPolygons *upper_slices = layer_id + 1 < m_layers.size() ? &m_layers[layer_id + 1]->lslices : nullptr;
                    // Rings around an opening that closes further up are slopes, decided on the whole ring: the filter below
                    // breaks a thin ring into arcs, which have no hole any more and were bridged by chords (flat sides).
                    // Holes of the layer that stay open (a screw hole):
                    Polygons open_holes;
                    for (const ExPolygon &island : region_slices)
                        for (const Polygon &hole : island.holes) {
                            Polygon inside = hole;
                            inside.make_counter_clockwise();
                            ExPolygon around(offset(inside, float(spacing)).front());
                            around.holes.push_back(hole);
                            if (! overhangs_cut_around_closing_hole(around, upper_slices, spacing))
                                open_holes.push_back(inside);
                        }
                    const Polygons near_open_hole = offset(open_holes, float(spacing));
                    // A ring left around an open hole by a dome closing faster than the slope follows it: the hole's edge is
                    // near the support (within 3 line widths) all around but for gaps under 30 degrees seen from the hole's
                    // center, touches it nowhere, and the layer below was cut next to it. It is left to the slope until it
                    // touches (then kept as it is, below): printed as is it was a full ring of overhang perimeters, bridged
                    // its chords gave flat sides whose overhanging arcs were printed as is in the next layer.
                    Polygons slope_rings;
                    {
                        const Polygons support_near    = offset(lower_layer->lslices, float(3. * spacing));
                        const Polygons support_touched = offset(lower_layer->lslices, 0.25f * float(spacing));
                        for (const Polygon &hole : open_holes) {
                            const bool touched = ! intersection_pl(Polylines{ to_polyline(hole) }, support_touched).empty();
                            if (! touched && intersection(lower_cut_away, offset(hole, float(3. * spacing))).empty())
                                continue;
                            const Vec2d c = hole.centroid().cast<double>();
                            const auto angle_of = [&c](const Point &p) { return std::atan2(double(p.y()) - c.y(), double(p.x()) - c.x()); };
                            // Supported angular intervals [start, start + sweep], swept along each arc.
                            std::vector<std::pair<double, double>> covered;
                            for (const Polyline &arc : intersection_pl(Polylines{ to_polyline(hole) }, support_near)) {
                                if (arc.points.size() < 2)
                                    continue;
                                double start = angle_of(arc.points.front()), sweep = 0, prev = start;
                                for (size_t i = 1; i < arc.points.size(); ++ i) {
                                    const double a = angle_of(arc.points[i]);
                                    double d = a - prev;
                                    while (d > PI) d -= 2. * PI;
                                    while (d < -PI) d += 2. * PI;
                                    sweep += d;
                                    prev = a;
                                }
                                if (sweep < 0) {
                                    start += sweep;
                                    sweep = -sweep;
                                }
                                start = std::fmod(start + 4. * PI, 2. * PI);
                                covered.emplace_back(start, start + sweep);
                            }
                            if (covered.empty())
                                continue;
                            std::sort(covered.begin(), covered.end());
                            double max_gap = 0, reach = covered.front().second;
                            for (size_t i = 1; i < covered.size(); ++ i) {
                                max_gap = std::max(max_gap, covered[i].first - reach);
                                reach   = std::max(reach, covered[i].second);
                            }
                            max_gap = std::max(max_gap, covered.front().first + 2. * PI - reach);
                            if (max_gap < PI / 6. && ! touched)
                                append(slope_rings, offset(hole, float(8 * spacing)));
                        }
                    }
                    for (const ExPolygon &expoly : unsupported) {
                        if (overhangs_cut_around_closing_hole(expoly, upper_slices, spacing))
                            continue;
                        if (! slope_rings.empty() && ! intersection(to_polygons(expoly), near_open_hole).empty() &&
                            diff_ex(ExPolygons{ expoly }, union_ex(slope_rings)).empty())
                            continue;
                        // Around a hole that stays open (a screw hole over a counterbore or a countersink, also one the cut
                        // left floating): kept as the model has it, PrusaSlicer bridges it in one layer. No bridges of the
                        // cut beside the hole and across it: bridge layers under a floating hole are "Support floating
                        // holes"' job (user, 2026-10-30; the cut had printed bands and a square around the hole).
                        if (! intersection(to_polygons(expoly), near_open_hole).empty()) {
                            bridged_area.push_back(expoly);
                            continue;
                        }
                        // Small overhangs (thinner than a nozzle) can't be bridges.
                        const ExPolygons wide = intersection_ex(ExPolygons{ expoly },
                            offset2_ex(ExPolygons{ expoly }, -float(max_nozzle / 2), float(max_nozzle), ClipperLib::jtMiter, 5.));
                        for (const ExPolygon &to_bridge : wide)
                            append(bridged_area, overhangs_cut_bridge_coverage(to_bridge, lower_layer->lslices, layer->lslices, upper_slices, spacing, max_bridge));
                    }
                    bridged_area = union_ex(bridged_area);
                }
            }

            // Supported area grown by the slope.
            ExPolygons enlarged_support = offset_ex(supported_area, float(enlargement), ClipperLib::jtRound, round_tolerance);
            enlarged_support = union_safety_offset_ex(enlarged_support);
            ExPolygons max_enlarged = offset_ex(enlarged_support, float(enlargement * 0.5), ClipperLib::jtRound, round_tolerance);
            append(max_enlarged, bridged_area);
            max_enlarged = union_ex(max_enlarged);
            ExPolygons min_enlarged = supported_area;
            append(min_enlarged, offset_ex(enlarged_support, -float(enlargement * 0.5), ClipperLib::jtRound, round_tolerance));
            append(min_enlarged, bridged_area);
            min_enlarged = union_ex(min_enlarged);
            // No zig-zags in the cut outline.
            const double smooth_length = std::max(double(enlargement) * 2, double(max_nozzle) * 2);
            for (ExPolygon &expoly : enlarged_support) {
                overhangs_cut_smoothen(expoly.contour, smooth_length, PI * 5. / 6.);
                for (Polygon &hole : expoly.holes)
                    overhangs_cut_smoothen(hole, smooth_length, PI * 5. / 6.);
            }
            enlarged_support = intersection_ex(enlarged_support, max_enlarged);
            append(enlarged_support, min_enlarged);
            enlarged_support = union_ex(enlarged_support);
            ExPolygons cut = intersection_ex(region_slices, enlarged_support);
            // Floating hole bridge layers (apply_floating_hole_bridges()) are kept as they are.
            if (! layer->floating_hole_zones.empty())
                cut = union_ex(cut, intersection_ex(region_slices, layer->floating_hole_zones));
            overhangs_cut_canonical(cut);
            layerm->m_slices.set(std::move(cut), stInternal);
            modified = true;
        }
        if (modified)
            layer->make_slices();
        lower_uncut = std::move(uncut);
    }
    return true;
}

// ---- Support floating holes: bridge layers (fork, floating_hole_bridges) ----

namespace {

// Range of the projections of the points on the unit vector n.
std::pair<double, double> floating_hole_extent(const Polygon &polygon, const Vec2d &n)
{
    double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
    for (const Point &p : polygon.points) {
        const double t = p.cast<double>().dot(n);
        lo = std::min(lo, t);
        hi = std::max(hi, t);
    }
    return { lo, hi };
}

// The band of the plane between the two lines along `angle` that touch the hole on both sides, long enough to cross `reach`
// (a box around the void).
Polygon floating_hole_strip(const Polygon &hole, double angle, const BoundingBox &reach)
{
    const Vec2d  u(std::cos(angle), std::sin(angle));
    const Vec2d  n(-u.y(), u.x());
    const auto [lo, hi] = floating_hole_extent(hole, n);
    const Vec2d  c   = reach.center().cast<double>();
    const double len = reach.size().cast<double>().norm();
    const double mid = c.dot(u);
    Polygon strip(Points{ Point((u * (mid - len) + n * lo).cast<coord_t>()), Point((u * (mid + len) + n * lo).cast<coord_t>()),
                          Point((u * (mid + len) + n * hi).cast<coord_t>()), Point((u * (mid - len) + n * hi).cast<coord_t>()) });
    strip.make_counter_clockwise();
    return strip;
}

// Opening left in the void after the bridge layers 0..k: the void cut down to the strips along angle0 + i * PI / n.
Polygons floating_hole_opening(const Polygon &void_area, const Polygon &hole, double angle0, size_t n, size_t k)
{
    const BoundingBox reach = get_extents(void_area);
    Polygons opening{ void_area };
    for (size_t i = 0; i <= k && ! opening.empty(); ++ i)
        opening = intersection(opening, Polygons{ floating_hole_strip(hole, angle0 + double(i) * PI / double(n), reach) });
    return opening;
}

// Longest bridge line of the first layer at this angle: lines along it across the void beside the hole.
double floating_hole_longest_line(const Polygon &void_area, const Polygon &hole, double angle, coord_t spacing)
{
    const Polygons beside = diff(Polygons{ void_area }, Polygons{ floating_hole_strip(hole, angle, get_extents(void_area)) });
    if (beside.empty())
        return 0.;
    // The lines as the bridges lay them, from the hole's tangents outwards (from the bounding box, their places moved with
    // the angle and so did the length of the line next to the hole).
    const Vec2d u(std::cos(angle), std::sin(angle));
    const Vec2d n(-u.y(), u.x());
    const auto [lo, hi] = floating_hole_extent(hole, n);
    const auto [vlo, vhi] = floating_hole_extent(void_area, n);
    const auto [ulo, uhi] = floating_hole_extent(void_area, u);
    const auto line_at = [&](double t) {
        return Line(Point((u * (ulo - spacing) + n * t).cast<coord_t>()), Point((u * (uhi + spacing) + n * t).cast<coord_t>()));
    };
    Lines lines;
    for (double t = hi + 0.5 * spacing; t < vhi; t += spacing)
        lines.push_back(line_at(t));
    for (double t = lo - 0.5 * spacing; t > vlo; t -= spacing)
        lines.push_back(line_at(t));
    double longest = 0.;
    for (const Line &line : intersection_ln(lines, beside))
        longest = std::max(longest, line.length());
    return longest;
}

// Farthest a corner of the opening is from the hole.
double floating_hole_corner_gap(const Polygons &opening, const Polygon &hole)
{
    double gap = 0.;
    for (const Polygon &polygon : opening)
        for (const Point &p : polygon.points) {
            double best = std::numeric_limits<double>::max();
            for (size_t i = 0; i < hole.points.size(); ++ i) {
                const Vec2d a = hole.points[i].cast<double>(), b = hole.points[(i + 1) % hole.points.size()].cast<double>();
                const Vec2d v = b - a, w = p.cast<double>() - a;
                const double t = v.squaredNorm() > 0 ? std::clamp(w.dot(v) / v.squaredNorm(), 0., 1.) : 0.;
                best = std::min(best, (a + v * t - p.cast<double>()).norm());
            }
            gap = std::max(gap, best);
        }
    return gap;
}

// The layer still has this hole (same place, about the same size) and closes the void around it.
bool floating_hole_continues(const ExPolygons &slices, const Polygon &void_area, const Polygon &hole)
{
    const double hole_area = hole.area();
    bool found = false;
    for (const ExPolygon &island : slices)
        for (const Polygon &h : island.holes)
            if (std::abs(std::abs(h.area()) - hole_area) < 0.1 * hole_area &&
                (h.centroid() - hole.centroid()).cast<double>().norm() < scale_(0.1))
                found = true;
    if (! found)
        return false;
    Polygons material = to_polygons(slices);
    material.push_back(hole);
    return Slic3r::area(diff(Polygons{ void_area }, union_(material))) < 0.02 * void_area.area();
}

} // namespace

// A hole of a layer floating over a void of the layer below (material all around the void, the hole inside it with a gap
// all around), like a screw hole over a counterbore printed from below: with floating_hole_bridges, the layers over the void
// are cut to two parallel bridges each, tangent to the hole, turned by PI / N per layer, so that after N layers the opening is
// a regular 2N-gon around the hole (N chosen so that its corners are within half an external perimeter width of the hole).
// The bridge directions are kept in Layer::forced_bridges, the areas in Layer::floating_hole_zones (left alone by the
// overhangs cut). Without the option, floating holes wider than 2 mm give a warning. Runs after slice_volumes(), before
// the overhangs cut; sequential (a layer is checked against the layer below as already cut).
void PrintObject::apply_floating_hole_bridges(bool after_cut, FloatingHoleWarning &warning)
{
    if (! after_cut)
        for (Layer *layer : m_layers) {
            layer->forced_bridges.clear();
            layer->floating_hole_zones.clear();
        }
    for (size_t layer_id = 1; layer_id < m_layers.size(); ++ layer_id) {
        m_print->throw_if_canceled();
        Layer       *layer = m_layers[layer_id];
        const Layer *lower = m_layers[layer_id - 1];
        // Collect first, then cut: the cut changes the layers above, not this one's holes.
        struct Floating { Polygon void_area; Polygon hole; const LayerRegion *layerm; };
        std::vector<Floating> floating;
        for (const ExPolygon &island : layer->lslices)
            for (const Polygon &hole_cw : island.holes) {
                Polygon hole = hole_cw;
                hole.make_counter_clockwise();
                // Second pass: the holes found before the cut are done.
                if (after_cut && expolygons_contain(layer->floating_hole_zones, hole.points.front()))
                    continue;
                const BoundingBox hole_bbox = get_extents(hole);
                // The void of the layer below around it.
                const Polygon *void_found = nullptr;
                for (const ExPolygon &lower_island : lower->lslices)
                    if (get_extents(lower_island.contour).contains(hole_bbox))
                        for (const Polygon &v : lower_island.holes) {
                            const BoundingBox vb = get_extents(v);
                            if (vb.contains(hole_bbox.min) && vb.contains(hole_bbox.max) && v.contains(hole.points.front())) {
                                void_found = &v;
                                break;
                            }
                        }
                if (void_found == nullptr)
                    continue;
                Polygon void_area = *void_found;
                void_area.make_counter_clockwise();
                // The region of the ceiling over the void, for its options and line width.
                const LayerRegion *layerm = nullptr;
                double best = 0.;
                for (const LayerRegion *r : layer->regions()) {
                    const double a = Slic3r::area(intersection(to_polygons(r->slices().surfaces), Polygons{ void_area }));
                    if (a > best) {
                        best   = a;
                        layerm = r;
                    }
                }
                if (layerm == nullptr)
                    continue;
                const coord_t width = layerm->flow(frExternalPerimeter).scaled_width();
                // A gap all around the hole, nothing else in the void (one hole, no island in it or below it), and the
                // ceiling closed around the hole.
                if (! diff(Polygons{ hole }, offset(void_area, -float(width))).empty())
                    continue;
                size_t holes_in_void = 0;
                bool   other = false;
                for (const ExPolygon &i : layer->lslices) {
                    if (hole.contains(i.contour.points.front()))
                        other = true;
                    for (const Polygon &h : i.holes)
                        if (void_area.contains(h.points.front()))
                            ++ holes_in_void;
                }
                for (const ExPolygon &i : lower->lslices)
                    if (void_area.contains(i.contour.points.front()))
                        other = true;
                // The hole goes on up unchanged (a countersink printed from below narrows on every layer: a slope).
                if (other || holes_in_void != 1 || ! floating_hole_continues(layer->lslices, void_area, hole) ||
                    layer_id + 1 >= m_layers.size() || ! floating_hole_continues(m_layers[layer_id + 1]->lslices, void_area, hole))
                    continue;
                floating.push_back({ std::move(void_area), std::move(hole), layerm });
            }

        for (const Floating &f : floating) {
            const PrintRegionConfig &config = f.layerm->region().config();
            if (! config.floating_hole_bridges.value) {
                // The overhangs cut leaves the ceiling as the model has it (PrusaSlicer's bridge over the void): bridges
                // under floating holes are this option's job (user, 2026-10-30).
                append(layer->floating_hole_zones, ExPolygons{ ExPolygon(f.void_area) });
                // Width of the hole: largest of its widths over the directions.
                double w = 0.;
                for (int deg = 0; deg < 180; deg += 5) {
                    const double a = Geometry::deg2rad(double(deg));
                    const auto [lo, hi] = floating_hole_extent(f.hole, Vec2d(-std::sin(a), std::cos(a)));
                    w = std::max(w, unscale<double>(hi - lo));
                }
                if (w > 2.) {
                    ++ warning.count;
                    if (w > warning.width) {
                        warning.width = w;
                        warning.z     = layer->print_z;
                    }
                }
                continue;
            }
            const coord_t spacing = f.layerm->bridging_flow(frSolidInfill).scaled_spacing();
            // Number of layers per direction of the first one: the polygon's corners within half an external perimeter width
            // of the hole. The fewest layers first (a hexagon hole: 3 along its sides; across its corners the polygon never
            // fits it), then the closest fit (the sides exactly, not a few degrees off), then the shortest bridges among
            // those directions; X when that is as short (a round void).
            const double max_gap = 0.5 * double(f.layerm->flow(frExternalPerimeter).scaled_width());
            std::vector<size_t> needed(180);
            std::vector<double> gap(180);
            for (int deg = 0; deg < 180; ++ deg) {
                const double angle = Geometry::deg2rad(double(deg));
                size_t k = 2;
                for (;; ++ k) {
                    gap[deg] = floating_hole_corner_gap(floating_hole_opening(f.void_area, f.hole, angle, k, k - 1), f.hole);
                    if (gap[deg] <= max_gap || k == 16)
                        break;
                }
                needed[deg] = k;
            }
            const size_t n = *std::min_element(needed.begin(), needed.end());
            double best_gap = std::numeric_limits<double>::max();
            for (int deg = 0; deg < 180; ++ deg)
                if (needed[deg] == n)
                    best_gap = std::min(best_gap, gap[deg]);
            std::vector<double> longest(180, std::numeric_limits<double>::max());
            for (int deg = 0; deg < 180; ++ deg)
                if (needed[deg] == n && gap[deg] <= best_gap + scale_(0.02))
                    longest[deg] = floating_hole_longest_line(f.void_area, f.hole, Geometry::deg2rad(double(deg)), spacing);
            const auto   shortest = std::min_element(longest.begin(), longest.end());
            const int    best_deg = longest.front() <= *shortest * 1.005 + scale_(0.01) ? 0 : int(shortest - longest.begin());
            const double angle0   = Geometry::deg2rad(double(best_deg));
            Polygons prev{ f.void_area };
            size_t   k = 0;
            for (; k < n && layer_id + k < m_layers.size(); ++ k) {
                Layer *l = m_layers[layer_id + k];
                if (k > 0 && ! floating_hole_continues(l->lslices, f.void_area, f.hole))
                    break;
                const Polygons opening = floating_hole_opening(f.void_area, f.hole, angle0, n, k);
                for (LayerRegion *layerm : l->m_regions) {
                    if (layerm->slices().empty())
                        continue;
                    ExPolygons cut = diff_ex(to_expolygons(layerm->slices().surfaces), opening);
                    overhangs_cut_canonical(cut);
                    layerm->m_slices.set(std::move(cut), stInternal);
                }
                l->make_slices();
                double angle = std::fmod(angle0 + double(k) * PI / double(n), PI);
                l->forced_bridges.push_back({ diff_ex(prev, opening), angle < 0 ? angle + PI : angle });
                append(l->floating_hole_zones, ExPolygons{ ExPolygon(f.void_area) });
                prev = opening;
            }
            // The hole printed on the polygon: its corners left as they are.
            if (layer_id + k < m_layers.size())
                append(m_layers[layer_id + k]->floating_hole_zones, ExPolygons{ ExPolygon(f.void_area) });
        }
    }
}

// Called by make_perimeters()
// 1) Decides Z positions of the layers,
// 2) Initializes layers and their regions
// 3) Slices the object meshes
// 4) Slices the modifier meshes and reclassifies the slices of the object meshes by the slices of the modifier meshes
// 5) Applies size compensation (offsets the slices in XY plane)
// 6) Replaces bad slices by the slices reconstructed from the upper/lower layer
// Resulting expolygons of layer regions are marked as Internal.
void PrintObject::slice()
{
    if (! this->set_started(posSlice))
        return;
    m_print->set_status(10, _u8L("Processing triangulated mesh"));
    std::vector<coordf_t> layer_height_profile;
    this->update_layer_height_profile(*this->model_object(), m_slicing_params, layer_height_profile);
    m_print->throw_if_canceled();
    m_typed_slices = false;
    this->clear_layers();
    m_layers = new_layers(this, generate_object_layers(m_slicing_params, layer_height_profile));
    this->slice_volumes();
    m_print->throw_if_canceled();
    // Support floating holes (fork), before the overhangs cut, which leaves them alone, and again after it for the holes
    // the cut left floating (the top of a countersink printed from below: the cut widens its last steps).
    {
        FloatingHoleWarning warning;
        this->apply_floating_hole_bridges(false, warning);
        m_print->throw_if_canceled();
        // Overhangs cut (SuperSlicer): needs the lslices of the layer below.
        if (this->apply_overhangs_cut()) {
            m_print->throw_if_canceled();
            this->apply_floating_hole_bridges(true, warning);
        }
        m_print->throw_if_canceled();
        if (warning.count > 0)
            this->active_step_add_warning(PrintStateBase::WarningLevel::NON_CRITICAL,
                format(_u8L("%1% floating hole(s) wider than 2 mm over a void (the widest %2% mm, at Z %3% mm): printed on "
                            "bridges with overhang perimeters around the hole, the hole may sag or come out out of round. Turn "
                            "on \"Support floating holes\" (Print Settings > Layers and perimeters) to print them on bridge "
                            "layers that keep them round."),
                       warning.count, std::round(warning.width * 10.) / 10., std::round(warning.z * 100.) / 100.) +
                "\n" + _u8L("Object name") + ": " + this->model_object()->name);
    }
#if 0
    // Layer::slicing_errors is no more set since 1.41.1 or possibly earlier, thus this code
    // was not really functional for a long day and nobody missed it.
    // Could we reuse this fixing code one day?

    // Fix the model.
    //FIXME is this the right place to do? It is done repeateadly at the UI and now here at the backend.
    std::string warning = fix_slicing_errors(m_layers, [this](){ m_print->throw_if_canceled(); });
    m_print->throw_if_canceled();
    if (! warning.empty())
        BOOST_LOG_TRIVIAL(info) << warning;
#endif
    // Update bounding boxes, back up raw slices of complex models.
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, m_layers.size()),
        [this](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                m_print->throw_if_canceled();
                Layer &layer = *m_layers[layer_idx];
                layer.lslices_ex.clear();
                layer.lslices_ex.reserve(layer.lslices.size());
                for (const ExPolygon &expoly : layer.lslices)
                	layer.lslices_ex.push_back({ get_extents(expoly) });
                layer.backup_untyped_slices();
            }
        });
    // Interlink the lslices into a Z graph.
    tbb::parallel_for(
        tbb::blocked_range<size_t>(1, m_layers.size()),
        [this](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                m_print->throw_if_canceled();
                Layer::build_up_down_graph(*m_layers[layer_idx - 1], *m_layers[layer_idx]);
            }
        });
    if (m_layers.empty())
        throw Slic3r::SlicingError("No layers were detected. You might want to repair your STL file(s) or check their size or thickness and retry.\n");    
    this->set_done(posSlice);
}

template<typename ThrowOnCancel>
void apply_mm_segmentation(PrintObject &print_object, ThrowOnCancel throw_on_cancel)
{
    // Returns MM segmentation based on painting in MM segmentation gizmo
    std::vector<std::vector<ExPolygons>> segmentation = multi_material_segmentation_by_painting(print_object, throw_on_cancel);
    assert(segmentation.size() == print_object.layer_count());

    if (!print_object.print()->virtual_extruders().empty()) {
        std::vector<double> print_z_per_layer(segmentation.size());
        for (size_t layer_id = 0; layer_id < segmentation.size(); ++layer_id) {
            print_z_per_layer[layer_id] = print_object.get_layer(int(layer_id))->print_z;
        }

        FullSpectrum::remap_virtual_extruders_to_physical(
            segmentation,
            print_z_per_layer,
            print_object.print()->num_physical_extruders(),
            print_object.print()->virtual_extruders()
        );
    }

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, segmentation.size(), std::max(segmentation.size() / 128, size_t(1))),
        [&print_object, &segmentation, throw_on_cancel](const tbb::blocked_range<size_t> &range) {
            const auto  &layer_ranges   = print_object.shared_regions()->layer_ranges;
            double       z              = print_object.get_layer(int(range.begin()))->slice_z;
            auto         it_layer_range = layer_range_first(layer_ranges, z);
            const size_t num_extruders = print_object.print()->config().nozzle_diameter.size();

            struct ByExtruder {
                ExPolygons  expolygons;
                BoundingBox bbox;
            };

            struct ByRegion {
                ExPolygons expolygons;
                bool       needs_merge { false };
            };

            std::vector<ByExtruder> by_extruder;
            std::vector<ByRegion>   by_region;
            for (size_t layer_id = range.begin(); layer_id < range.end(); ++layer_id) {
                throw_on_cancel();
                Layer &layer = *print_object.get_layer(int(layer_id));
                it_layer_range = layer_range_next(layer_ranges, it_layer_range, layer.slice_z);
                const PrintObjectRegions::LayerRangeRegions &layer_range = *it_layer_range;
                // Gather per extruder expolygons.
                by_extruder.assign(num_extruders, ByExtruder());
                by_region.assign(layer.region_count(), ByRegion());
                bool layer_split = false;
                for (size_t extruder_id = 0; extruder_id < num_extruders; ++ extruder_id) {
                    ByExtruder &region = by_extruder[extruder_id];
                    append(region.expolygons, std::move(segmentation[layer_id][extruder_id]));
                    if (! region.expolygons.empty()) {
                        region.bbox = get_extents(region.expolygons);
                        layer_split = true;
                    }
                }

                if (!layer_split)
                    continue;

                // Split LayerRegions by by_extruder regions.
                // layer_range.painted_regions are sorted by extruder ID and parent PrintObject region ID.
                auto it_painted_region_begin = layer_range.painted_regions.cbegin();
                for (int parent_layer_region_idx = 0; parent_layer_region_idx < layer.region_count(); ++parent_layer_region_idx) {
                    if (it_painted_region_begin == layer_range.painted_regions.cend())
                        continue;

                    const LayerRegion &parent_layer_region = *layer.get_region(parent_layer_region_idx);
                    const PrintRegion &parent_print_region = parent_layer_region.region();
                    assert(parent_print_region.print_object_region_id() == parent_layer_region_idx);
                    if (parent_layer_region.slices().empty())
                        continue;

                    // Find the first PaintedRegion, which overrides the parent PrintRegion.
                    auto it_first_painted_region = std::find_if(it_painted_region_begin, layer_range.painted_regions.cend(), [&layer_range, &parent_print_region](const auto &painted_region) {
                        return layer_range.volume_regions[painted_region.parent].region->print_object_region_id() == parent_print_region.print_object_region_id();
                    });

                    if (it_first_painted_region == layer_range.painted_regions.cend())
                        continue; // This LayerRegion isn't overrides by any PaintedRegion.

                    assert(&parent_print_region == layer_range.volume_regions[it_first_painted_region->parent].region);

                    // Update the beginning PaintedRegion iterator for the next iteration.
                    it_painted_region_begin = it_first_painted_region;

                    const BoundingBox parent_layer_region_bbox = get_extents(parent_layer_region.slices().surfaces);
                    bool              self_trimmed             = false;
                    int               self_extruder_id         = -1; // 1-based extruder ID
                    for (int extruder_id = 1; extruder_id <= int(by_extruder.size()); ++extruder_id) {
                        const ByExtruder &segmented = by_extruder[extruder_id - 1];
                        if (!segmented.bbox.defined || !parent_layer_region_bbox.overlap(segmented.bbox))
                            continue;

                        // Find the first target region iterator.
                        auto it_target_region = std::find_if(it_painted_region_begin, layer_range.painted_regions.cend(), [extruder_id](const auto &painted_region) {
                            return int(painted_region.extruder_id) >= extruder_id;
                        });

                        assert(it_target_region != layer_range.painted_regions.end());
                        assert(layer_range.volume_regions[it_target_region->parent].region == &parent_print_region && int(it_target_region->extruder_id) == extruder_id);

                        // Update the beginning PaintedRegion iterator for the next iteration.
                        it_painted_region_begin = it_target_region;

                        // FIXME: Don't trim by self, it is not reliable.
                        if (it_target_region->region == &parent_print_region) {
                            self_extruder_id = extruder_id;
                            continue;
                        }

                        // Steal from this region.
                        int        target_region_id = it_target_region->region->print_object_region_id();
                        ExPolygons stolen           = intersection_ex(parent_layer_region.slices().surfaces, segmented.expolygons);
                        if (!stolen.empty()) {
                            ByRegion &dst = by_region[target_region_id];
                            if (dst.expolygons.empty()) {
                                dst.expolygons = std::move(stolen);
                            } else {
                                append(dst.expolygons, std::move(stolen));
                                dst.needs_merge = true;
                            }
                        }
                    }

                    if (!self_trimmed) {
                        // Trim slices of this LayerRegion with all the MM regions.
                        Polygons mine = to_polygons(parent_layer_region.slices().surfaces);
                        for (auto &segmented : by_extruder) {
                            if (&segmented - by_extruder.data() + 1 != self_extruder_id && segmented.bbox.defined && parent_layer_region_bbox.overlap(segmented.bbox)) {
                                mine = diff(mine, segmented.expolygons);
                                if (mine.empty())
                                    break;
                            }
                        }

                        // Filter out unprintable polygons produced by subtraction multi-material painted regions from layerm.region().
                        // ExPolygon returned from multi-material segmentation does not precisely match ExPolygons in layerm.region()
                        // (because of preprocessing of the input regions in multi-material segmentation). Therefore, subtraction from
                        // layerm.region() could produce a huge number of small unprintable regions for the model's base extruder.
                        // This could, on some models, produce bulges with the model's base color (#7109).
                        if (!mine.empty()) {
                            mine = opening(union_ex(mine), scaled<float>(5. * EPSILON), scaled<float>(5. * EPSILON));
                        }

                        if (!mine.empty()) {
                            ByRegion &dst = by_region[parent_print_region.print_object_region_id()];
                            if (dst.expolygons.empty()) {
                                dst.expolygons = union_ex(mine);
                            } else {
                                append(dst.expolygons, union_ex(mine));
                                dst.needs_merge = true;
                            }
                        }
                    }
                }

                // Re-create Surfaces of LayerRegions.
                for (int region_id = 0; region_id < layer.region_count(); ++region_id) {
                    ByRegion &src = by_region[region_id];
                    if (src.needs_merge) {
                        // Multiple regions were merged into one.
                        src.expolygons = closing_ex(src.expolygons, scaled<float>(10. * EPSILON));
                    }

                    layer.get_region(region_id)->m_slices.set(std::move(src.expolygons), stInternal);
                }
            }
        });
}

template<typename ThrowOnCancel>
void apply_fuzzy_skin_segmentation(PrintObject &print_object, ThrowOnCancel throw_on_cancel)
{
    // Returns fuzzy skin segmentation based on painting in the fuzzy skin painting gizmo.
    std::vector<std::vector<ExPolygons>> segmentation = fuzzy_skin_segmentation_by_painting(print_object, throw_on_cancel);
    assert(segmentation.size() == print_object.layer_count());

    struct ByRegion
    {
        ExPolygons expolygons;
        bool       needs_merge { false };
    };

    tbb::parallel_for(tbb::blocked_range<size_t>(0, segmentation.size(), std::max(segmentation.size() / 128, size_t(1))), [&print_object, &segmentation, throw_on_cancel](const tbb::blocked_range<size_t> &range) {
        const auto &layer_ranges   = print_object.shared_regions()->layer_ranges;
        auto        it_layer_range = layer_range_first(layer_ranges, print_object.get_layer(int(range.begin()))->slice_z);

        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            throw_on_cancel();

            Layer &layer = *print_object.get_layer(int(layer_idx));
            it_layer_range = layer_range_next(layer_ranges, it_layer_range, layer.slice_z);
            const PrintObjectRegions::LayerRangeRegions &layer_range = *it_layer_range;

            assert(segmentation[layer_idx].size() == 1);
            const ExPolygons &fuzzy_skin_segmentation      = segmentation[layer_idx][0];
            const BoundingBox fuzzy_skin_segmentation_bbox = get_extents(fuzzy_skin_segmentation);
            if (fuzzy_skin_segmentation.empty())
                continue;

            // Split LayerRegions by painted fuzzy skin regions.
            // layer_range.fuzzy_skin_painted_regions are sorted by parent PrintObject region ID.
            std::vector<ByRegion> by_region(layer.region_count());
            auto                  it_fuzzy_skin_region_begin = layer_range.fuzzy_skin_painted_regions.cbegin();
            for (int parent_layer_region_idx = 0; parent_layer_region_idx < layer.region_count(); ++parent_layer_region_idx) {
                if (it_fuzzy_skin_region_begin == layer_range.fuzzy_skin_painted_regions.cend())
                    continue;

                const LayerRegion &parent_layer_region = *layer.get_region(parent_layer_region_idx);
                const PrintRegion &parent_print_region = parent_layer_region.region();
                assert(parent_print_region.print_object_region_id() == parent_layer_region_idx);
                if (parent_layer_region.slices().empty())
                    continue;

                // Find the first FuzzySkinPaintedRegion, which overrides the parent PrintRegion.
                auto it_fuzzy_skin_region = std::find_if(it_fuzzy_skin_region_begin, layer_range.fuzzy_skin_painted_regions.cend(), [&layer_range, &parent_print_region](const auto &fuzzy_skin_region) {
                    return fuzzy_skin_region.parent_print_object_region_id(layer_range) == parent_print_region.print_object_region_id();
                });

                if (it_fuzzy_skin_region == layer_range.fuzzy_skin_painted_regions.cend())
                    continue; // This LayerRegion isn't overrides by any FuzzySkinPaintedRegion.

                assert(it_fuzzy_skin_region->parent_print_object_region(layer_range) == &parent_print_region);

                // Update the beginning FuzzySkinPaintedRegion iterator for the next iteration.
                it_fuzzy_skin_region_begin = std::next(it_fuzzy_skin_region);

                const BoundingBox parent_layer_region_bbox        = get_extents(parent_layer_region.slices().surfaces);
                Polygons          layer_region_remaining_polygons = to_polygons(parent_layer_region.slices().surfaces);
                // Don't trim by self, it is not reliable.
                if (parent_layer_region_bbox.overlap(fuzzy_skin_segmentation_bbox) && it_fuzzy_skin_region->region != &parent_print_region) {
                    // Steal from this region.
                    const int  target_region_id = it_fuzzy_skin_region->region->print_object_region_id();
                    ExPolygons stolen           = intersection_ex(parent_layer_region.slices().surfaces, fuzzy_skin_segmentation);
                    if (!stolen.empty()) {
                        ByRegion &dst = by_region[target_region_id];
                        if (dst.expolygons.empty()) {
                            dst.expolygons = std::move(stolen);
                        } else {
                            append(dst.expolygons, std::move(stolen));
                            dst.needs_merge = true;
                        }
                    }

                    // Trim slices of this LayerRegion by the fuzzy skin region.
                    layer_region_remaining_polygons = diff(layer_region_remaining_polygons, fuzzy_skin_segmentation);

                    // Filter out unprintable polygons. Detailed explanation is inside apply_mm_segmentation.
                    if (!layer_region_remaining_polygons.empty()) {
                        layer_region_remaining_polygons = opening(union_ex(layer_region_remaining_polygons), scaled<float>(5. * EPSILON), scaled<float>(5. * EPSILON));
                    }
                }

                if (!layer_region_remaining_polygons.empty()) {
                    ByRegion &dst = by_region[parent_print_region.print_object_region_id()];
                    if (dst.expolygons.empty()) {
                        dst.expolygons = union_ex(layer_region_remaining_polygons);
                    } else {
                        append(dst.expolygons, union_ex(layer_region_remaining_polygons));
                        dst.needs_merge = true;
                    }
                }
            }

            // Re-create Surfaces of LayerRegions.
            for (int region_id = 0; region_id < layer.region_count(); ++region_id) {
                ByRegion &src = by_region[region_id];
                if (src.needs_merge) {
                    // Multiple regions were merged into one.
                    src.expolygons = closing_ex(src.expolygons, scaled<float>(10. * EPSILON));
                }

                layer.get_region(region_id)->m_slices.set(std::move(src.expolygons), stInternal);
            }
        }
    }); // end of parallel_for
}

// 1) Decides Z positions of the layers,
// 2) Initializes layers and their regions
// 3) Slices the object meshes
// 4) Slices the modifier meshes and reclassifies the slices of the object meshes by the slices of the modifier meshes
// 5) Applies size compensation (offsets the slices in XY plane)
// 6) Replaces bad slices by the slices reconstructed from the upper/lower layer
// Resulting expolygons of layer regions are marked as Internal.
//
// this should be idempotent
// True if all corners of the hole are convex, seen from inside the hole, with a tolerance of 0.1 rad (as in SuperSlicer).
static bool is_convex_hole(const Polygon &hole)
{
    const Points &pts = hole.points;
    const size_t  n   = pts.size();
    if (n < 3)
        return false;
    for (size_t i = 0; i < n; ++ i) {
        const Vec2d p    = pts[i].cast<double>();
        const Vec2d prev = pts[(i + n - 1) % n].cast<double>() - p;
        const Vec2d next = pts[(i + 1) % n].cast<double>() - p;
        double angle = std::atan2(cross2(prev, next), prev.dot(next));
        if (angle < 0)
            angle += 2. * PI;
        if (angle > PI + 0.1)
            return false;
    }
    return true;
}

void PrintObject::slice_volumes()
{
    BOOST_LOG_TRIVIAL(info) << "Slicing volumes..." << log_memory_info();
    const Print *print                      = this->print();
    const auto   throw_on_cancel_callback   = std::function<void()>([print](){ print->throw_if_canceled(); });

    // Clear old LayerRegions, allocate for new PrintRegions.
    for (Layer* layer : m_layers) {
        layer->m_regions.clear();
        layer->m_regions.reserve(m_shared_regions->all_regions.size());
        for (const std::unique_ptr<PrintRegion> &pr : m_shared_regions->all_regions)
            layer->m_regions.emplace_back(new LayerRegion(layer, pr.get()));
    }

    std::vector<float>                   slice_zs      = zs_from_layers(m_layers);
    std::vector<std::vector<ExPolygons>> region_slices = slices_to_regions(this->model_object()->volumes, *m_shared_regions, slice_zs,
        slice_volumes_inner(
            print->config(), this->config(), this->trafo_centered(),
            this->model_object()->volumes, m_shared_regions->layer_ranges, slice_zs, throw_on_cancel_callback),
        throw_on_cancel_callback);

    for (size_t region_id = 0; region_id < region_slices.size(); ++ region_id) {
        std::vector<ExPolygons> &by_layer = region_slices[region_id];
        for (size_t layer_id = 0; layer_id < by_layer.size(); ++ layer_id)
            m_layers[layer_id]->regions()[region_id]->m_slices.append(std::move(by_layer[layer_id]), stInternal);
    }
    region_slices.clear();
    
    BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - removing top empty layers";
    while (! m_layers.empty()) {
        const Layer *layer = m_layers.back();
        if (! layer->empty())
            break;
        delete layer;
        m_layers.pop_back();
    }
    if (! m_layers.empty())
        m_layers.back()->upper_layer = nullptr;
    m_print->throw_if_canceled();

    // Is any ModelVolume multi-material painted?
    if (m_print->config().nozzle_diameter.size() > 1 && this->model_object()->is_mm_painted()) {
        // If XY Size compensation is also enabled, notify the user that XY Size compensation
        // would not be used because the object is multi-material painted.
        if (m_config.xy_size_compensation.value != 0.f) {
            this->active_step_add_warning(
                PrintStateBase::WarningLevel::CRITICAL,
                _u8L("An object has enabled XY Size compensation which will not be used because it is also multi-material painted.\nXY Size "
                  "compensation cannot be combined with multi-material painting.") +
                    "\n" + (_u8L("Object name")) + ": " + this->model_object()->name);
        }

        BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - MMU segmentation";
        apply_mm_segmentation(*this, [print]() { print->throw_if_canceled(); });
    }

    // Is any ModelVolume fuzzy skin painted?
    if (this->model_object()->is_fuzzy_skin_painted()) {
        // If XY Size compensation is also enabled, notify the user that XY Size compensation
        // would not be used because the object has custom fuzzy skin painted.
        if (m_config.xy_size_compensation.value != 0.f) {
            this->active_step_add_warning(
                PrintStateBase::WarningLevel::CRITICAL,
                _u8L("An object has enabled XY Size compensation which will not be used because it is also fuzzy skin painted.\nXY Size "
                     "compensation cannot be combined with fuzzy skin painting.") +
                    "\n" + (_u8L("Object name")) + ": " + this->model_object()->name);
        }

        BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - Fuzzy skin segmentation";
        apply_fuzzy_skin_segmentation(*this, [print]() { print->throw_if_canceled(); });
    }

    if (!m_print->virtual_extruders().empty()) {
        FullSpectrum::remap_virtual_region_slices_to_physical(
            *this,
            m_print->num_physical_extruders(),
            m_print->virtual_extruders()
        );
    }

    if (m_config.interlocking_beam) {
        BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - Applying multi-material interlocking";
        InterlockingGenerator::generate_interlocking_structure(*this);
        m_print->throw_if_canceled();
    }

    BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - make_slices in parallel - begin";
    {
        // Compensation value, scaled. Only applying the negative scaling here, as the positive scaling has already been applied during slicing.
        const size_t num_extruders = print->config().nozzle_diameter.size();
        const auto   xy_compensation_scaled            = (num_extruders > 1 && this->is_mm_painted()) ? scaled<float>(0.f) : scaled<float>(std::min(m_config.xy_size_compensation.value, 0.));
        const float  elephant_foot_compensation_scaled = (m_config.raft_layers == 0) ?
        	// Only enable Elephant foot compensation if printing directly on the print bed.
            float(scale_(m_config.elefant_foot_compensation.value)) :
        	0.f;
        // Grow (compensation < 0) or shrink (compensation > 0) the convex holes of a layer, ported from SuperSlicer
        // (hole_size_compensation, hole_size_threshold). The compensation applies fully to holes up to threshold mm² and fades
        // out linearly up to four times that area. Holes are taken from the whole layer, so they work across regions.
        auto apply_hole_size_compensation = [](Layer &layer, const double compensation, const double threshold) {
            const double max_hole_area = scale_(scale_(threshold));
            // Offsets of the holes seen as contours: a positive offset makes the hole bigger.
            Polygons grown_holes;   // compensation < 0: area to remove
            // compensation > 0: area to add, one ring per hole (contour and its hole kept together) with its faded compensation.
            std::vector<std::pair<ExPolygons, double>> filled_rings;
            const ExPolygons merged = layer.m_regions.size() == 1 ? to_expolygons(layer.m_regions.front()->slices().surfaces) :
                                                                   layer.merged(float(SCALED_EPSILON));
            for (const ExPolygon &expoly : merged)
                for (const Polygon &hole : expoly.holes) {
                    if (! is_convex_hole(hole))
                        continue;
                    double delta = compensation;
                    const double area = std::abs(hole.area());
                    if (max_hole_area > 0 && area > max_hole_area * 4)
                        continue;
                    if (max_hole_area > 0 && area > max_hole_area)
                        // Not a hard threshold, to avoid artefacts on sloped holes.
                        delta *= (max_hole_area * 4 - area) / (max_hole_area * 3);
                    Polygon contour = hole;
                    contour.make_counter_clockwise();
                    if (delta < 0) {
                        append(grown_holes, offset(contour, float(scale_(-delta))));
                    } else if (delta > 0) {
                        // Grown a bit outwards to overlap the slices: with several regions the hole comes from the merged
                        // layer, whose edges don't exactly match the regions' (the ring would stay a separate island).
                        if (ExPolygons ring = diff_ex(offset(contour, float(4 * SCALED_EPSILON)), offset(contour, float(scale_(-delta))));
                            ! ring.empty())
                            filled_rings.emplace_back(std::move(ring), delta);
                    }
                }
            if (! grown_holes.empty())
                for (LayerRegion *layerm : layer.m_regions)
                    layerm->m_slices.set(diff_ex(to_expolygons(layerm->slices().surfaces), grown_holes), stInternal);
            if (! filled_rings.empty()) {
                if (layer.m_regions.size() == 1) {
                    LayerRegion *layerm = layer.m_regions.front();
                    Polygons     polys  = to_polygons(to_expolygons(layerm->slices().surfaces));
                    for (const auto &[ring, delta] : filled_rings)
                        append(polys, to_polygons(ring));
                    layerm->m_slices.set(union_ex(polys), stInternal);
                } else {
                    // Give each ring to the region it borders the most: the regions around the ring, as far as it fills.
                    for (const auto &[ring, delta] : filled_rings) {
                        const Polygons around = offset(ring, float(scale_(delta)));
                        size_t best = 0;
                        double best_area = -1;
                        for (size_t i = 0; i < layer.m_regions.size(); ++ i) {
                            const double a = area(intersection(to_polygons(layer.m_regions[i]->slices().surfaces), around));
                            if (a > best_area) {
                                best_area = a;
                                best      = i;
                            }
                        }
                        LayerRegion *layerm = layer.m_regions[best];
                        Polygons polys = to_polygons(layerm->slices().surfaces);
                        append(polys, to_polygons(ring));
                        layerm->m_slices.set(union_ex(polys), stInternal);
                    }
                }
            }
        };

        // Holes compensation (SuperSlicer), not combined with multi-material painting, as the XY compensation.
        const double hole_compensation = (num_extruders > 1 && this->is_mm_painted()) ? 0. : m_config.hole_size_compensation.value;
        // Uncompensated slices for the first layer in case the Elephant foot compensation is applied.
	    ExPolygons  lslices_1st_layer;
	    tbb::parallel_for(
	        tbb::blocked_range<size_t>(0, m_layers.size()),
			[this, xy_compensation_scaled, elephant_foot_compensation_scaled, hole_compensation, &apply_hole_size_compensation, &lslices_1st_layer](const tbb::blocked_range<size_t>& range) {
	            for (size_t layer_id = range.begin(); layer_id < range.end(); ++ layer_id) {
	                m_print->throw_if_canceled();
	                Layer *layer = m_layers[layer_id];
	                // Apply size compensation and perform clipping of multi-part objects.
	                float elfoot = (layer_id == 0) ? elephant_foot_compensation_scaled : 0.f;
	                if (layer->m_regions.size() == 1) {
	                    // Optimized version for a single region layer.
	                    // Single region, growing or shrinking.
	                    LayerRegion *layerm = layer->m_regions.front();
	                    if (elfoot > 0) {
		                    // Apply the elephant foot compensation and store the 1st layer slices without the Elephant foot compensation applied.
		                    lslices_1st_layer = to_expolygons(std::move(layerm->m_slices.surfaces));
		                    float delta = xy_compensation_scaled;
	                        if (delta > elfoot) {
	                            delta -= elfoot;
	                            elfoot = 0.f;
	                        } else if (delta > 0)
	                            elfoot -= delta;
							layerm->m_slices.set(
								union_ex(
									Slic3r::elephant_foot_compensation(
										(delta == 0.f) ? lslices_1st_layer : offset_ex(lslices_1st_layer, delta), 
	                            		layerm->flow(frExternalPerimeter), unscale<double>(elfoot))),
								stInternal);
							if (xy_compensation_scaled < 0.f)
								lslices_1st_layer = offset_ex(std::move(lslices_1st_layer), xy_compensation_scaled);
	                    } else if (xy_compensation_scaled < 0.f) {
	                        // Apply the XY compensation.
	                        layerm->m_slices.set(
                                offset_ex(to_expolygons(std::move(layerm->m_slices.surfaces)), xy_compensation_scaled),
	                            stInternal);
	                    }
	                } else {
	                    if (xy_compensation_scaled < 0.f || elfoot > 0.f) {
	                        // Apply the negative XY compensation.
	                        Polygons trimming;
	                        static const float eps = float(scale_(m_config.slice_closing_radius.value) * 1.5);
	                        if (elfoot > 0.f) {
	                        	lslices_1st_layer = offset_ex(layer->merged(eps), std::min(xy_compensation_scaled, 0.f) - eps);
								trimming = to_polygons(Slic3r::elephant_foot_compensation(lslices_1st_layer,
									layer->m_regions.front()->flow(frExternalPerimeter), unscale<double>(elfoot)));
	                        } else
		                        trimming = offset(layer->merged(float(SCALED_EPSILON)), xy_compensation_scaled - float(SCALED_EPSILON));
	                        for (size_t region_id = 0; region_id < layer->m_regions.size(); ++ region_id)
	                            layer->m_regions[region_id]->trim_surfaces(trimming);
	                    }
	                }
	                if (hole_compensation != 0.)
	                    apply_hole_size_compensation(*layer, hole_compensation, m_config.hole_size_threshold.value);
	                // Merge all regions' slices to get islands sorted topologically, chain them by a shortest path in separate index list
	                layer->make_slices();
	            }
	        });
	    if (elephant_foot_compensation_scaled > 0.f && ! m_layers.empty()) {
	    	// The Elephant foot has been compensated, therefore the 1st layer's lslices are shrank with the Elephant foot compensation value.
	    	// Store the uncompensated value there.
            //FIXME is this operation needed? MMU painting and brim now have to do work arounds to work with compensated layer, not with the uncompensated layer.
            // There may be subtle issues removing this block such as support raft sticking too well with the first object layer.
            Layer &layer = *m_layers.front();
	    	assert(layer.id() == 0);
			layer.lslices = std::move(lslices_1st_layer);
            layer.lslice_indices_sorted_by_print_order = chain_expolygons(layer.lslices);
		}
	}

    m_print->throw_if_canceled();
    BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - make_slices in parallel - end";
}

std::vector<Polygons> PrintObject::slice_support_volumes(const ModelVolumeType model_volume_type) const
{
    auto it_volume     = this->model_object()->volumes.begin();
    auto it_volume_end = this->model_object()->volumes.end();
    for (; it_volume != it_volume_end && (*it_volume)->type() != model_volume_type; ++ it_volume) ;
    std::vector<Polygons> slices;
    if (it_volume != it_volume_end) {
        // Found at least a single support volume of model_volume_type.
        std::vector<float> zs = zs_from_layers(this->layers());
        std::vector<char>  merge_layers;
        bool               merge = false;
        const Print       *print = this->print();
        auto               throw_on_cancel_callback = std::function<void()>([print](){ print->throw_if_canceled(); });
        MeshSlicingParamsEx params;
        params.trafo = this->trafo_centered();
        for (; it_volume != it_volume_end; ++ it_volume)
            if ((*it_volume)->type() == model_volume_type) {
                std::vector<ExPolygons> slices2 = slice_volume(*(*it_volume), zs, params, throw_on_cancel_callback);
                if (slices.empty()) {
                    slices.reserve(slices2.size());
                    for (ExPolygons &src : slices2)
                        slices.emplace_back(to_polygons(std::move(src)));
                } else if (!slices2.empty()) {
                    if (merge_layers.empty())
                        merge_layers.assign(zs.size(), false);
                    for (size_t i = 0; i < zs.size(); ++ i) {
                        if (slices[i].empty())
                            slices[i] = to_polygons(std::move(slices2[i]));
                        else if (! slices2[i].empty()) {
                            append(slices[i], to_polygons(std::move(slices2[i])));
                            merge_layers[i] = true;
                            merge = true;
                        }
                    }
                }
            }
        if (merge) {
            std::vector<Polygons*> to_merge;
            to_merge.reserve(zs.size());
            for (size_t i = 0; i < zs.size(); ++ i)
                if (merge_layers[i])
                    to_merge.emplace_back(&slices[i]);
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, to_merge.size()),
                [&to_merge](const tbb::blocked_range<size_t> &range) {
                    for (size_t i = range.begin(); i < range.end(); ++ i)
                        *to_merge[i] = union_(*to_merge[i]);
            });
        }
    }
    return slices;
}

} // namespace Slic3r
