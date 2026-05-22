///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "SupportDemandModifiers.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/cpp/Views.hpp"

namespace slic3r_api { namespace Support { namespace SupportDemandModifiersPlugin {

namespace {

const char *k_support_demand_modifiers_id = "support.demand.modifiers";
const char *k_dependencies[] = { "support.demand.overhangs", nullptr };

bool is_support_modifier_type(raw_volume_type type)
{
    return type == RAW_VOLUME_TYPE_SUPPORT_ENFORCER || type == RAW_VOLUME_TYPE_SUPPORT_BLOCKER;
}

bool object_has_support_modifier_volume(const Object &object)
{
    for (uint32_t volume_idx = 0; volume_idx < object.volume_count(); ++volume_idx)
        if (is_support_modifier_type(object.volume(volume_idx).type()))
            return true;
    return false;
}

uint32_t island_work_count(const Object &object)
{
    uint32_t count = 0;
    for (uint32_t layer_idx = 0; layer_idx < object.layer_count(); ++layer_idx)
        count += object.layer(layer_idx).island_count();
    return count;
}

std::vector<float> layer_slice_zs(const Object &object)
{
    std::vector<float> slice_zs;
    slice_zs.reserve(object.layer_count());
    for (uint32_t layer_idx = 0; layer_idx < object.layer_count(); ++layer_idx)
        slice_zs.push_back(float(unscaled(object.layer(layer_idx).slice_z())));
    return slice_zs;
}

c_mesh_slicing_params support_modifier_slicing_params(const Print &print, const Object &object)
{
    const Config print_config = print.config();

    c_mesh_slicing_params params = {};
    params.transform = object.transform_centered();
    params.resolution = std::max(EPSILON, print_config.get("resolution").get_float());
    params.mode = RAW_MESH_SLICING_MODE_REGULAR;
    params.mode_below = params.mode;
    return params;
}

void merge_slices(std::vector<StoredExPolygonCollection> &dst,
                  std::vector<StoredExPolygonCollection> &&src,
                  storage_handle *storage,
                  coord_t resolution)
{
    if (src.empty())
        return;

    if (dst.empty()) {
        dst = std::move(src);
        return;
    }

    ClipperContext clip(storage);
    const size_t count = std::min(dst.size(), src.size());
    for (size_t layer_idx = 0; layer_idx < count; ++layer_idx) {
        if (src[layer_idx].empty())
            continue;
        if (dst[layer_idx].empty()) {
            dst[layer_idx].move_from(src[layer_idx]);
            dst[layer_idx].ensure_valid(resolution);
            continue;
        }

        ClipperOperand merged = clipper_union2(clip(dst[layer_idx]), clip(src[layer_idx]));
        merged.write_expolygons_to(dst[layer_idx]);
        dst[layer_idx].ensure_valid(resolution);
    }
}

std::vector<StoredExPolygonCollection> slice_support_modifier_volumes(const Object &object,
                                                                      raw_volume_type type,
                                                                      const std::vector<float> &slice_zs,
                                                                      const c_mesh_slicing_params &base_params,
                                                                      storage_handle *storage)
{
    std::vector<StoredExPolygonCollection> out;
    const coord_t resolution = scale_i(base_params.resolution);

    for (uint32_t volume_idx = 0; volume_idx < object.volume_count(); ++volume_idx) {
        const Volume volume = object.volume(volume_idx);
        if (volume.type() != type || volume.mesh().empty())
            continue;

        c_mesh_slicing_params params = base_params;
        params.transform = matrix4d_mul(base_params.transform, volume.matrix());
        std::vector<StoredExPolygonCollection> sliced = volume.mesh().slice_to_expolygons(storage, params, slice_zs);
        for (StoredExPolygonCollection &layer_polygons : sliced)
            layer_polygons.ensure_valid(resolution);
        merge_slices(out, std::move(sliced), storage, resolution);
    }

    return out;
}

bool has_layer_slices(const std::vector<StoredExPolygonCollection> &by_layer, uint32_t layer_idx)
{
    return layer_idx < by_layer.size() && !by_layer[layer_idx].empty();
}

ClipperOperand clip_layer_slices(const std::vector<StoredExPolygonCollection> &by_layer,
                                 uint32_t layer_idx,
                                 const ClipperContext &clip)
{
    assert(has_layer_slices(by_layer, layer_idx));
    return clip(by_layer[layer_idx]);
}

void set_demand_from_operand(const run_ctx_support_demand &ctx,
                             const LayerIsland &island,
                             ClipperOperand &&operand)
{
    StoredExPolygonCollection polygons = operand.to_expolygon_collection();
    polygons.ensure_valid();
    ctx.set(ctx.demand, island.handle(), polygons.mutable_handle());
}

void add_enforcers_to_island(const run_ctx_support_demand &ctx,
                             const LayerIsland &island,
                             const ClipperContext &clip,
                             const std::vector<StoredExPolygonCollection> &enforcers,
                             uint32_t layer_idx)
{
    if (!has_layer_slices(enforcers, layer_idx))
        return;

    ClipperOperand enforced = clipper_intersection(clip(island.slice()), clip_layer_slices(enforcers, layer_idx, clip));
    if (enforced.empty())
        return;

    expolygon_collection_handle *existing_handle = ctx.get(ctx.demand, island.handle());
    if (existing_handle != nullptr) {
        ExPolygonCollection existing(existing_handle);
        enforced = clipper_union2(clip(existing), enforced);
    }
    set_demand_from_operand(ctx, island, std::move(enforced));
}

void remove_blockers_from_island(const run_ctx_support_demand &ctx,
                                 const LayerIsland &island,
                                 const ClipperContext &clip,
                                 const std::vector<StoredExPolygonCollection> &blockers,
                                 uint32_t layer_idx)
{
    if (!has_layer_slices(blockers, layer_idx))
        return;

    expolygon_collection_handle *existing_handle = ctx.get(ctx.demand, island.handle());
    if (existing_handle == nullptr)
        return;

    ExPolygonCollection existing(existing_handle);
    ClipperOperand blocked = clipper_offset(clip_layer_slices(blockers, layer_idx, clip), 1000. * double(SCALED_EPSILON));
    ClipperOperand remaining = clipper_diff(clip(existing), blocked);
    set_demand_from_operand(ctx, island, std::move(remaining));
}

} // namespace

SupportDemandModifiers &
SupportDemandModifiers::instance(orchestrator_handle *orch)
{
    static SupportDemandModifiers s_instance(orch);
    return s_instance;
}

const char *SupportDemandModifiers::id_impl() const noexcept
{
    return k_support_demand_modifiers_id;
}

slicing_step_t SupportDemandModifiers::step_impl() const noexcept
{
    return STEP_SUPPORT_DEMAND;
}

const char *const *SupportDemandModifiers::dependencies_impl() const noexcept
{
    return k_dependencies;
}

int32_t SupportDemandModifiers::priority_impl() const noexcept
{
    return 10;
}

const char *SupportDemandModifiers::progress_message_format_impl() const noexcept
{
    return "Support demand modifiers: %u / %u islands";
}

void SupportDemandModifiers::setup_run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_support_demand *ctx = plugin_ctx_as_support_demand(run_ctx);
    if (ctx == nullptr || ctx->object == nullptr)
        return;

    const Object object(ctx->object);
    if (object_has_support_modifier_volume(object))
        progress().add_max(island_work_count(object));
}

void SupportDemandModifiers::run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_support_demand *ctx = plugin_ctx_as_support_demand(run_ctx);
    if (ctx == nullptr || ctx->print == nullptr || ctx->object == nullptr || ctx->demand == nullptr)
        return;

    const Object object(ctx->object);
    if (!object_has_support_modifier_volume(object)) {
        progress().finish_run();
        return;
    }

    storage_handle *storage = run_ctx->plugin_storage;
    const Print print(ctx->print);
    const std::vector<float> slice_zs = layer_slice_zs(object);
    const c_mesh_slicing_params params = support_modifier_slicing_params(print, object);
    std::vector<StoredExPolygonCollection> enforcers =
        slice_support_modifier_volumes(object, RAW_VOLUME_TYPE_SUPPORT_ENFORCER, slice_zs, params, storage);
    std::vector<StoredExPolygonCollection> blockers =
        slice_support_modifier_volumes(object, RAW_VOLUME_TYPE_SUPPORT_BLOCKER, slice_zs, params, storage);

    if (enforcers.empty() && blockers.empty()) {
        progress().finish_run();
        return;
    }

    ClipperContext clip(storage);
    for (uint32_t layer_idx = 0; layer_idx < object.layer_count(); ++layer_idx) {
        throw_if_cancelled(run_ctx);

        const Layer layer = object.layer(layer_idx);
        for (uint32_t island_idx = 0; island_idx < layer.island_count(); ++island_idx) {
            throw_if_cancelled(run_ctx);

            const LayerIsland island = layer.island(island_idx);
            add_enforcers_to_island(*ctx, island, clip, enforcers, layer_idx);
            remove_blockers_from_island(*ctx, island, clip, blockers, layer_idx);
            progress().increment();
        }
    }

    progress().finish_run();
}

void register_support_demand_modifiers_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, SupportDemandModifiers::instance(orch).c_instance());
}

}}} // namespace slic3r_api::Support::SupportDemandModifiersPlugin

#ifdef SUPPORT_DEMAND_MODIFIERS_PLUGIN_DLL
extern "C" void register_plugin(orchestrator_handle *orch)
{
    slic3r_api::Support::SupportDemandModifiersPlugin::register_support_demand_modifiers_plugin(orch);
}
#endif // SUPPORT_DEMAND_MODIFIERS_PLUGIN_DLL
