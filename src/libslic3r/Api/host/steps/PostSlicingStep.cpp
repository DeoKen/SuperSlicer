///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "PostSlicingStep.hpp"

#include <utility>

#include "libslic3r/Api/internal/LayerAccess.hpp"
#include "libslic3r/Api/internal/LayerIslandAccess.hpp"
#include "libslic3r/Api/internal/LayerRegionAccess.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"

namespace Slic3r::ApiHost::Steps {
namespace {

void layer_assign_islands_by_moving_contents(layer_handle *me, expolygon_collection_handle *in_out_islands)
{
    if (me == nullptr || in_out_islands == nullptr)
        return;

    ExPolygons *islands = reinterpret_cast<ExPolygons *>(in_out_islands);
    ApiInternal::LayerAccess::set_islands(*reinterpret_cast<Layer *>(me), std::move(*islands));
    islands->clear();
}

void layer_recompute_slices_from_islands(layer_handle *me)
{
    if (me == nullptr)
        return;
    ApiInternal::LayerAccess::recompute_slices_from_islands(*reinterpret_cast<Layer *>(me));
}

void layer_recompute_slices_and_islands_from_layer_region(layer_handle *me)
{
    if (me == nullptr)
        return;
    ApiInternal::LayerAccess::recompute_slices_from_layer_regions(*reinterpret_cast<Layer *>(me));
}

expolygon_collection_handle *layer_borrow_mutable_slices(layer_handle *me)
{
    return me == nullptr ? nullptr :
                           reinterpret_cast<expolygon_collection_handle *>(
                               &ApiInternal::LayerAccess::slices_mutable(*reinterpret_cast<Layer *>(me)));
}

expolygon_collection_handle *layer_region_borrow_mutable_slices(const layer_region_handle *me) {
    return me == nullptr ?
        nullptr :
        reinterpret_cast<expolygon_collection_handle *>(&ApiInternal::LayerRegionAccess::slices_mutable(
            *const_cast<LayerRegion *>(reinterpret_cast<const LayerRegion *>(me))));
}

expolygon_handle *layer_island_borrow_mutable_slice(layer_island_handle *me)
{
    return me == nullptr ? nullptr :
                           reinterpret_cast<expolygon_handle *>(
                               &ApiInternal::LayerIslandAccess::slice_mutable(*reinterpret_cast<LayerSliceIsland *>(me)));
}

layer_handle *object_borrow_mutable_layer(const object_handle *me, uint32_t idx) {
    return me == nullptr ?
        nullptr :
        reinterpret_cast<layer_handle *>(
            &const_cast<PrintObject *>(reinterpret_cast<const PrintObject *>(me))->layer(static_cast<size_t>(idx)));
}

} // namespace

run_ctx_post_slicing make_post_slicing_run_context(Print &print, size_t object_idx)
{
    run_ctx_post_slicing context_step = {};
    context_step.print = reinterpret_cast<const print_handle *>(&print);
    context_step.object = reinterpret_cast<const object_handle *>(&print.object(object_idx));
    context_step.layer_assign_islands_by_moving_contents = layer_assign_islands_by_moving_contents;
    context_step.layer_recompute_slices_from_islands = layer_recompute_slices_from_islands;
    context_step.object_borrow_mutable_layer = object_borrow_mutable_layer;
    context_step.layer_recompute_slices_and_islands_from_layer_region = layer_recompute_slices_and_islands_from_layer_region;
    context_step.layer_borrow_mutable_slices = layer_borrow_mutable_slices;
    context_step.layer_region_borrow_mutable_slices = layer_region_borrow_mutable_slices;
    context_step.layer_island_borrow_mutable_slice = layer_island_borrow_mutable_slice;
    return context_step;
}

} // namespace Slic3r::ApiHost::Steps
