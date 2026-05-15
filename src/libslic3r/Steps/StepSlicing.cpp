///|/ Copyright (c) SuperSlicer 2026 Durand R?mi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "StepSlicing.hpp"

#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#include "libslic3r/Api/host/ApiHostUtils.hpp"
#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/Api/internal/LayerAccess.hpp"
#include "libslic3r/Api/internal/LayerRegionAccess.hpp"
#include "libslic3r/Api/internal/PrintObjectAccess.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/SurfaceCollection.hpp"
#include "libslic3r/Thread.hpp"

#include "StepRunner.hpp"

namespace Slic3r {
LayerUPtrs new_layers(PrintObject *print_object, const std::vector<double> &object_layers);
}

namespace Slic3r::Steps::StepSlicing {
namespace {

using SlicingLayerRange = PrintObjectRegions::LayerRangeRegions;
using SlicingVolumeRegion = PrintObjectRegions::VolumeRegion;

std::vector<double> to_unscaled_layer_height_profile(const std::vector<coord_t> &layer_profile)
{
    std::vector<double> out;
    out.reserve(layer_profile.size());
    for (coord_t value : layer_profile)
        out.push_back(unscaled(value));
    return out;
}

void recreate_object_layers(PrintObject &object)
{
    std::vector<double> layer_height_profile =
        to_unscaled_layer_height_profile(object.layer_profile());
    LayerUPtrs object_layers =
        new_layers(&object, generate_object_layers(object.slicing_parameters(), layer_height_profile));
    for (std::unique_ptr<Layer> &layer : object_layers) {
        ApiInternal::LayerAccess::init_regions_from_object(*layer);
    }
    ApiInternal::PrintObjectAccess::replace_layers_by_moving_contents(object, std::move(object_layers));
}

void recompute_layer_slices_from_raw_regions(PrintObject &object)
{
    for (Layer &layer : object.layers()) {
        ExPolygons slices;
        if (layer.region_count() == 1) {
            slices = layer.region(0).get_raw_slices();
        } else {
            ExPolygons slices_exp;
            for (const LayerRegion &region : layer.regions())
                append(slices_exp, region.get_raw_slices());
            slices = union_safety_offset_ex(slices_exp);
        }
        ensure_valid(slices, std::max(scale_i(object.print()->config().resolution), SCALED_EPSILON));
        ApiInternal::LayerAccess::set_islands(layer, std::move(slices));
    }
}

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

struct SlicingRunContext
{
    run_ctx_slicing context_step = {};
};

std::unique_ptr<SlicingRunContext> make_slicing_run_context(Print &print, size_t object_idx)
{
    auto out = std::make_unique<SlicingRunContext>();
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

} // namespace

void clean_and_prepare(Print & print) {
    parallel_for(size_t(0), print.objects().size(), [&print](const size_t object_idx) {
        PrintObject &object = print.object(object_idx);
        recreate_object_layers(object);
    });
}

bool validate_pre(const Print &print, std::string &out_error)
{
    bool ok = true;

    for (size_t object_idx = 0; object_idx < print.objects().size(); ++object_idx) {
        const PrintObject &object = print.objects()[object_idx];

        const std::vector<coord_t> &layer_profile = object.layer_profile();
        if (layer_profile.empty()) {
            ok = false;
            std::ostringstream msg;
            msg << "object " << object_idx << ": layer height profile is empty";
            out_error += msg.str();
            continue;
        }

        if ((layer_profile.size() & 1) != 0) {
            ok = false;
            std::ostringstream msg;
            msg << "object " << object_idx << ": layer height profile has an odd element count";
            out_error += msg.str();
        }
    }

    return ok;
}

bool validate_post(const Print &print, std::string &out_error)
{
    bool ok = true;

    for (size_t object_idx = 0; object_idx < print.objects().size(); ++object_idx) {
        const PrintObject &object = print.objects()[object_idx];

        if (object.layer_count() == 0) {
            ok = false;
            std::ostringstream msg;
            msg << "object " << object_idx << ": no layers were created";
            out_error += msg.str();
        }
    }

    return ok;
}

void run_step(Orchestrator &orchestrator, Print &print)
{
    Detail::validate_or_report(validate_pre, print, "Slicing pre-step validation");

    clean_and_prepare(print);

    std::vector<Plugin *> plugins = orchestrator.get_all_plugins_for_step(STEP_SLICING);

    for (Plugin *plugin : plugins) {
        const size_t run_count = print.objects().size();
        plugin_host_context host_context = orchestrator.prepare_plugin_host_context(STEP_SLICING, plugin, &print);
        plugin_run_context run_context = orchestrator.prepare_plugin_run_context(STEP_SLICING, plugin, &host_context);
        plugin->setup(run_context, uint32_t(run_count));

        std::vector<std::unique_ptr<SlicingRunContext>> run_contexts;
        run_contexts.reserve(run_count);
        for (size_t object_idx = 0; object_idx < run_count; ++object_idx)
            run_contexts.push_back(make_slicing_run_context(print, object_idx));

        parallel_for(size_t(0), run_count, [plugin, &run_context, &run_contexts](const size_t object_idx) {
            plugin_run_context context_copy = run_context;
            if (context_copy.is_cancelled != nullptr && context_copy.is_cancelled(context_copy.host_context))
                return;

            context_copy.data = &run_contexts[object_idx]->context_step;
            plugin->setup_run(context_copy);
        });

        parallel_for(size_t(0), run_count, [plugin, &run_context, &run_contexts](const size_t object_idx) {
            plugin_run_context context_copy = run_context;
            if (context_copy.is_cancelled != nullptr && context_copy.is_cancelled(context_copy.host_context))
                return;

            context_copy.data = &run_contexts[object_idx]->context_step;
            plugin->run(context_copy);
        });

        parallel_for(size_t(0), run_count, [&print](const size_t object_idx) {
            PrintObject &object = print.object(object_idx);
            recompute_layer_slices_from_raw_regions(object);
        });

        Detail::validate_or_report(validate_post, print, "Slicing post-plugin validation");
    }
}

} // namespace Slic3r::Steps::StepSlicing
