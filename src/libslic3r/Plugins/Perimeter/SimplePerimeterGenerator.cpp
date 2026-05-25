///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "SimplePerimeterGenerator.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_perimeter.h"
#include "libslic3r/Api/plugin/cpp/ClipperViews.hpp"
#include "libslic3r/Api/plugin/cpp/DataTreeViews.hpp"
#include "libslic3r/Api/plugin/cpp/ExtrusionViews.hpp"
#include "libslic3r/Api/plugin/cpp/PerimeterStepViews.hpp"

namespace slic3r_api { namespace Perimeter { namespace SimplePerimeterGeneratorPlugin {

namespace {

const char *k_simple_perimeter_generator_id = "perimeter.generator.simple";
const char *k_no_dependencies[] = { nullptr };

constexpr uint16_t k_loop_role_default = 1u << 0;
constexpr uint16_t k_loop_role_hole    = 1u << 3;

template<class Payload>
Payload &get_or_add_property(StoredExtrusionEntity &entity)
{
    ExtrusionPropertyMutableApi<StoredExtrusionEntity> &properties = entity;
    return properties.template property<Payload>();
}

StoredExPolygonCollection offset_surface(storage_handle *storage, const ExPolygon &surface, double delta)
{
    ClipperOperand subject(storage, surface);
    StoredExPolygonCollection out = clipper_offset(subject, delta).to_expolygon_collection();
    out.ensure_valid();
    return out;
}

void append_perimeter_loop(StoredExtrusionEntity &dst,
                           const Polygon &polygon,
                           const c_flow &flow,
                           uint16_t loop_role)
{
    if (!polygon.valid_polygon() || polygon.empty())
        return;

    std::vector<c_point> points = polygon.points();
    if (points.empty())
        return;
    points.push_back(points.front());

    StoredExtrusionEntity path(dst.storage(), points);
    EPropertyAttributes &attributes = get_or_add_property<EPropertyAttributes>(path);
    attributes.extrusion_role(RAW_EXTRUSION_ROLE_EXTERNAL_PERIMETER)
        .mm3_per_mm(flow.mm3_per_mm)
        .width(float(unscaled(flow.width)))
        .height(float(unscaled(flow.height)));

    StoredExtrusionEntity loop(dst.storage());
    loop.set_flags(RAW_EXTRUSION_FLAG_CONTINUOUS | RAW_EXTRUSION_FLAG_REVERSIBLE);
    get_or_add_property<EPropertyPerimeter>(loop).shell_count(0).perimeter_role(loop_role);
    loop.add_child(path.mutable_view());
    dst.add_child(loop.mutable_view());
}

StoredExtrusionEntity make_external_perimeter_extrusion(storage_handle *storage,
                                                        const ExPolygon &surface,
                                                        const c_flow &flow)
{
    StoredExtrusionEntity extrusion(storage);
    extrusion.disable_reverse().disable_sort();

    StoredExPolygonCollection loops = offset_surface(storage, surface, -0.5 * double(flow.width));
    for (ExPolygon loop : loops) {
        append_perimeter_loop(extrusion, loop.contour(), flow, k_loop_role_default);
        for (Polygon hole : loop.holes())
            append_perimeter_loop(extrusion, hole, flow, k_loop_role_hole);
    }
    return extrusion;
}

struct SimpleGeneratorState
{
    c_flow flow = {};
};

int32_t generate_node(void *generator_context,
                      perimeter_generation_context *context,
                      perimeter_node *node,
                      expolygon_collection_handle *inner_surfaces_out,
                      expolygon_collection_handle *inner_fill_surfaces_out)
{
    if (generator_context == nullptr || context == nullptr || context->run_ctx == nullptr ||
        context->run_ctx->plugin_storage == nullptr || node == nullptr ||
        inner_surfaces_out == nullptr || inner_fill_surfaces_out == nullptr)
        return 0;

    const SimpleGeneratorState &state = *reinterpret_cast<const SimpleGeneratorState *>(generator_context);
    storage_handle *storage = context->run_ctx->plugin_storage;
    PerimeterNodeView node_view(node);

    StoredExtrusionEntity extrusion = make_external_perimeter_extrusion(storage, node_view.surface(), state.flow);
    extrusion_move_from(node->extrusions, extrusion.mutable_handle());

    StoredExPolygonCollection inner_surfaces =
        offset_surface(storage, node_view.surface(), -double(state.flow.spacing));
    expolygons_move(inner_surfaces_out, inner_surfaces.mutable_handle());

    // The fill/anchor area is slightly larger than the next perimeter surface,
    // matching the old "perimeter spacing minus 25%" anchoring convention.
    StoredExPolygonCollection inner_fill_surfaces =
        offset_surface(storage, node_view.surface(), -0.75 * double(state.flow.spacing));
    expolygons_move(inner_fill_surfaces_out, inner_fill_surfaces.mutable_handle());

    return 1;
}

c_flow external_perimeter_flow(const LayerIsland &island)
{
    return island.region_count() > 0 ? island.region(0).flow(RAW_EXTRUSION_ROLE_EXTERNAL_PERIMETER) : c_flow{};
}

} // namespace

SimplePerimeterGenerator &
SimplePerimeterGenerator::instance(orchestrator_handle *orch)
{
    static SimplePerimeterGenerator s_instance(orch);
    return s_instance;
}

const char *SimplePerimeterGenerator::id_impl() const noexcept
{
    return k_simple_perimeter_generator_id;
}

slicing_step_t SimplePerimeterGenerator::step_impl() const noexcept
{
    return STEP_PERIMETER;
}

const char *const *SimplePerimeterGenerator::dependencies_impl() const noexcept
{
    return k_no_dependencies;
}

int32_t SimplePerimeterGenerator::priority_impl() const noexcept
{
    return 0;
}

const char *SimplePerimeterGenerator::progress_message_format_impl() const noexcept
{
    return "Simple perimeter generator: %u / %u islands";
}

void SimplePerimeterGenerator::setup_run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_generate_perimeter *ctx = plugin_ctx_as_generate_perimeter(run_ctx);
    if (ctx != nullptr && ctx->island != nullptr)
        progress().add_max(1);
}

void SimplePerimeterGenerator::run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_generate_perimeter *ctx = plugin_ctx_as_generate_perimeter(run_ctx);
    if (ctx == nullptr || ctx->island == nullptr || ctx->run_region_group == nullptr)
        return;

    throw_if_cancelled(run_ctx);

    const LayerIsland island(ctx->island);
    if (island.region_count() == 0)
        return;

    std::vector<const layer_region_handle *> regions;
    regions.reserve(island.region_count());
    for (uint32_t region_idx = 0; region_idx < island.region_count(); ++region_idx)
        regions.push_back(island.region(region_idx).handle());

    SimpleGeneratorState state;
    state.flow = external_perimeter_flow(island);
    ctx->run_region_group(ctx,
                          regions.empty() ? nullptr : regions.data(),
                          uint32_t(regions.size()),
                          island.slice().handle(),
                          &state,
                          &generate_node);

    progress().increment();
}

void register_simple_perimeter_generator_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, SimplePerimeterGenerator::instance(orch).c_instance());
}

}}} // namespace slic3r_api::Perimeter::SimplePerimeterGeneratorPlugin

#ifdef SIMPLE_PERIMETER_GENERATOR_PLUGIN_DLL
extern "C" void register_plugin(orchestrator_handle *orch)
{
    slic3r_api::Perimeter::SimplePerimeterGeneratorPlugin::register_simple_perimeter_generator_plugin(orch);
}
#endif // SIMPLE_PERIMETER_GENERATOR_PLUGIN_DLL
