///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "SupportDemandOverhangsAndBridges.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/cpp/Views.hpp"

namespace slic3r_api { namespace Support { namespace SupportDemandOverhangsAndBridgesPlugin {

namespace {

const char *k_support_demand_overhangs_and_bridges_id = "support.demand.overhangs_and_bridges";
const char *k_no_dependencies[] = { nullptr };

bool has_auto_support(const Object &object)
{
    const Config object_config = object.config();
    return object_config.get("support_material").get_bool() &&
           object_config.get("support_material_auto").get_bool();
}

uint32_t island_work_count(const Object &object)
{
    uint32_t count = 0;
    for (uint32_t layer_idx = 1; layer_idx < object.layer_count(); ++layer_idx)
        count += object.layer(layer_idx).island_count();
    return count;
}

coord_t lower_layer_offset(const Object &object, const Layer &layer)
{
    const Config object_config = object.config();
    if (object_config.get("support_material_threshold").get_int() <= 0)
        return 0;

    const double threshold_rad = PI * double(object_config.get("support_material_threshold").get_int() + 1) / 180.;
    if (threshold_rad <= 0. || threshold_rad >= PI / 2.)
        return 0;

    return coord_t(double(layer.height()) / std::tan(threshold_rad));
}

c_flow island_bridge_flow(const LayerIsland &island)
{
    for (uint32_t region_idx = 0; region_idx < island.region_count(); ++region_idx) {
        const c_flow flow = island.region(region_idx).flow(RAW_EXTRUSION_ROLE_BRIDGE_INFILL);
        if (flow.spacing > 0)
            return flow;
    }

    c_flow fallback = {};
    fallback.spacing = scale_i(0.4);
    fallback.width = fallback.spacing;
    fallback.height = scale_i(0.2);
    fallback.nozzle_diameter = scale_i(0.4);
    fallback.is_bridge = 1;
    return fallback;
}

void remove_bridgeable_areas(const plugin_run_context *run_ctx,
                             orchestrator_handle *orchestrator,
                             const Print &print,
                             uint32_t layer_idx,
                             const LayerIsland &island,
                             const ExPolygonCollection &lower_slices,
                             storage_handle *storage,
                             ClipperOperand &unsupported)
{
    if (unsupported.empty())
        return;

    const c_flow bridge_flow = island_bridge_flow(island);
    const coord_t spacing = bridge_flow.spacing > 0 ? bridge_flow.spacing : scale_i(0.4);
    const coord_t precision =
        scale_i(print.config().get("bridge_precision").get_effective_value(unscaled(spacing)));

    ClipperContext clip(storage);
    ClipperOperand bridgeable = clip.empty();
    unsupported.for_each_expolygon([&](const ExPolygon &to_bridge) {
        throw_if_cancelled(run_ctx);

        bridge_detector_create_input input = {};
        input.expolygon = to_bridge.handle();
        input.lower_slices = lower_slices.handle();
        input.spacing = spacing;
        input.precision = std::max<coord_t>(precision, SCALED_EPSILON);
        input.layer_id = int32_t(layer_idx);

        BridgeDetector detector(orchestrator_create_bridge_detector(orchestrator, &input));
        if (!detector.detect_angle())
            return;

        StoredPolygonCollection coverage(storage);
        if (detector.coverage(coverage) == 0)
            return;

        StoredExPolygonCollection coverage_expolygons(storage);
        if (polygons_to_expolygons(coverage.handle(), coverage_expolygons.mutable_handle()) != EXPOLYGON_STATUS_OK)
            return;

        bridgeable += clipper_union(clip(coverage_expolygons));
    });

    if (!bridgeable.empty())
        unsupported = clipper_diff(unsupported, bridgeable);
}

void add_to_demand(const run_ctx_support_demand &ctx,
                   const LayerIsland &island,
                   storage_handle *storage,
                   ClipperOperand &unsupported)
{
    if (unsupported.empty())
        return;

    ClipperContext clip(storage);
    StoredExPolygonCollection polygons = unsupported.to_expolygon_collection();
    polygons.ensure_valid();

    expolygon_collection_handle *existing = ctx.get(ctx.demand, island.handle());
    if (existing != nullptr) {
        ExPolygonCollection existing_polygons(existing);
        polygons = clipper_union2(clip(existing_polygons), clip(polygons)).to_expolygon_collection();
        polygons.ensure_valid();
    }

    ctx.set(ctx.demand, island.handle(), polygons.mutable_handle());
}

} // namespace

SupportDemandOverhangsAndBridges &
SupportDemandOverhangsAndBridges::instance(orchestrator_handle *orch)
{
    static SupportDemandOverhangsAndBridges s_instance(orch);
    return s_instance;
}

const char *SupportDemandOverhangsAndBridges::id_impl() const noexcept
{
    return k_support_demand_overhangs_and_bridges_id;
}

slicing_step_t SupportDemandOverhangsAndBridges::step_impl() const noexcept
{
    return STEP_SUPPORT_DEMAND;
}

const char *const *SupportDemandOverhangsAndBridges::dependencies_impl() const noexcept
{
    return k_no_dependencies;
}

int32_t SupportDemandOverhangsAndBridges::priority_impl() const noexcept
{
    return 0;
}

const char *SupportDemandOverhangsAndBridges::progress_message_format_impl() const noexcept
{
    return "Support demand overhangs: %u / %u islands";
}

void SupportDemandOverhangsAndBridges::setup_run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_support_demand *ctx = plugin_ctx_as_support_demand(run_ctx);
    if (ctx == nullptr || ctx->object == nullptr)
        return;

    const Object object(ctx->object);
    progress().add_max(has_auto_support(object) ? island_work_count(object) : 0);
}

void SupportDemandOverhangsAndBridges::run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_support_demand *ctx = plugin_ctx_as_support_demand(run_ctx);
    if (ctx == nullptr || ctx->print == nullptr || ctx->object == nullptr || ctx->demand == nullptr)
        return;

    const Print print(ctx->print);
    const Object object(ctx->object);
    if (!has_auto_support(object)) {
        progress().finish_run();
        return;
    }

    storage_handle *storage = run_ctx->plugin_storage;
    ClipperContext clip(storage);
    const Config object_config = object.config();
    const bool skip_bridgeable_areas = object_config.get("dont_support_bridges").get_bool();

    for (uint32_t layer_idx = 1; layer_idx < object.layer_count(); ++layer_idx) {
        throw_if_cancelled(run_ctx);

        const Layer layer = object.layer(layer_idx);
        const Layer lower_layer = object.layer(layer_idx - 1);
        const ExPolygonCollection lower_slices = lower_layer.slices();
        ClipperOperand lower_support = clip(lower_slices);

        const coord_t offset = lower_layer_offset(object, layer);
        if (offset > 0)
            lower_support = clipper_offset(lower_support, double(offset));

        for (uint32_t island_idx = 0; island_idx < layer.island_count(); ++island_idx) {
            throw_if_cancelled(run_ctx);

            const LayerIsland island = layer.island(island_idx);
            ClipperOperand unsupported = clipper_diff_with_safety_offset(clip(island.slice()), lower_support);
            if (skip_bridgeable_areas)
                remove_bridgeable_areas(run_ctx,
                                        m_orchestrator,
                                        print,
                                        layer_idx,
                                        island,
                                        lower_slices,
                                        storage,
                                        unsupported);
            add_to_demand(*ctx, island, storage, unsupported);
            progress().increment();
        }
    }

    progress().finish_run();
}

void register_support_demand_overhangs_and_bridges_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, SupportDemandOverhangsAndBridges::instance(orch).c_instance());
}

}}} // namespace slic3r_api::Support::SupportDemandOverhangsAndBridgesPlugin

#ifdef SUPPORT_DEMAND_OVERHANGS_AND_BRIDGES_PLUGIN_DLL
extern "C" void register_plugin(orchestrator_handle *orch)
{
    slic3r_api::Support::SupportDemandOverhangsAndBridgesPlugin::
        register_support_demand_overhangs_and_bridges_plugin(orch);
}
#endif // SUPPORT_DEMAND_OVERHANGS_AND_BRIDGES_PLUGIN_DLL
