///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "DefaultSurfaceGenerator.hpp"

#include <utility>

#include "libslic3r/Api/internal/LayerRegionAccess.hpp"
#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_surface_generation.h"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/SurfaceCollection.hpp"

namespace slic3r_api { namespace SurfaceGeneration { namespace DefaultSurfaceGeneratorPlugin {
namespace {

const char *k_default_surface_generator_id = "surface.generator.default";
const char *k_no_dependencies[] = { nullptr };

Slic3r::PrintObject *to_object(const object_handle *handle)
{
    return const_cast<Slic3r::PrintObject *>(reinterpret_cast<const Slic3r::PrintObject *>(handle));
}

void build_region_fill_surfaces(Slic3r::Layer &layer)
{
    Slic3r::ExPolygons all_fill_expolygons;
    Slic3r::ExPolygons all_fill_no_overlap_expolygons;
    for (Slic3r::LayerSliceIsland &island : layer.islands()) {
        Slic3r::append(all_fill_expolygons, island.fill_expolygons());
        if (island.fill_no_overlap_expolygons().empty())
            Slic3r::append(all_fill_no_overlap_expolygons, island.fill_expolygons());
        else
            Slic3r::append(all_fill_no_overlap_expolygons, island.fill_no_overlap_expolygons());
    }

    all_fill_no_overlap_expolygons = Slic3r::union_safety_offset_ex(all_fill_no_overlap_expolygons);
    for (Slic3r::LayerRegion &region : layer.regions()) {
        region.set_fill_surfaces().clear();
        for (const Slic3r::Surface &raw_surface : region.slices()) {
            Slic3r::ExPolygons expolygons =
                Slic3r::intersection_ex(Slic3r::ExPolygons{raw_surface.expolygon}, all_fill_expolygons);
            region.set_fill_surfaces().append(std::move(expolygons), raw_surface);
        }

        Slic3r::ExPolygons &fill_no_overlap =
            Slic3r::ApiInternal::LayerRegionAccess::fill_no_overlap_expolygons_mutable(region);
        fill_no_overlap = Slic3r::intersection_ex(region.get_raw_slices(), all_fill_no_overlap_expolygons);
        if (fill_no_overlap == region.get_raw_slices())
            Slic3r::ensure_valid(fill_no_overlap);
    }
}

} // namespace

DefaultSurfaceGenerator &
DefaultSurfaceGenerator::instance(orchestrator_handle *orch)
{
    static DefaultSurfaceGenerator s_instance(orch);
    return s_instance;
}

const char *DefaultSurfaceGenerator::id_impl() const noexcept
{
    return k_default_surface_generator_id;
}

slicing_step_t DefaultSurfaceGenerator::step_impl() const noexcept
{
    return STEP_SURFACE_GENERATION;
}

const char *const *DefaultSurfaceGenerator::dependencies_impl() const noexcept
{
    return k_no_dependencies;
}

int32_t DefaultSurfaceGenerator::priority_impl() const noexcept
{
    return 0;
}

const char *DefaultSurfaceGenerator::progress_message_format_impl() const noexcept
{
    return "Default surface generator: %u / %u layers";
}

void DefaultSurfaceGenerator::setup_run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_surface_generation *ctx = plugin_ctx_as_surface_generation(run_ctx);
    const Slic3r::PrintObject *object = ctx == nullptr ? nullptr : to_object(ctx->object);
    if (object != nullptr)
        progress().add_max(uint32_t(object->layer_count()));
}

void DefaultSurfaceGenerator::run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_surface_generation *ctx = plugin_ctx_as_surface_generation(run_ctx);
    Slic3r::PrintObject *object = ctx == nullptr ? nullptr : to_object(ctx->object);
    if (object == nullptr)
        return;

    for (Slic3r::Layer &layer : object->layers()) {
        throw_if_cancelled(run_ctx);
        build_region_fill_surfaces(layer);
        progress().increment();
    }
}

void register_default_surface_generator_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, DefaultSurfaceGenerator::instance(orch).c_instance());
}

}}} // namespace slic3r_api::SurfaceGeneration::DefaultSurfaceGeneratorPlugin

#ifdef DEFAULT_SURFACE_GENERATOR_PLUGIN_DLL
extern "C" void register_plugin(orchestrator_handle *orch)
{
    slic3r_api::SurfaceGeneration::DefaultSurfaceGeneratorPlugin::register_default_surface_generator_plugin(orch);
}
#endif // DEFAULT_SURFACE_GENERATOR_PLUGIN_DLL
