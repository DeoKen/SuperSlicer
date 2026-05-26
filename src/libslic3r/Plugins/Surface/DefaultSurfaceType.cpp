///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "DefaultSurfaceType.hpp"

#include <boost/log/trivial.hpp>

#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_surface_type.h"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/Surface.hpp"

namespace slic3r_api { namespace SurfaceType { namespace DefaultSurfaceTypePlugin {
namespace {

const char *k_default_surface_type_id = "surface.type.default";
const char *k_no_dependencies[] = { nullptr };

// Built-in host bridge: convert the opaque C handle back to the native
// PrintObject so this first migration pass can reuse the legacy internals.
Slic3r::PrintObject *to_object(const object_handle *handle)
{
    return const_cast<Slic3r::PrintObject *>(reinterpret_cast<const Slic3r::PrintObject *>(handle));
}

} // namespace

DefaultSurfaceType &
DefaultSurfaceType::instance(orchestrator_handle *orch)
{
    // Built-in plugins are registered once and never unloaded. A singleton
    // keeps the C plugin_instance stable after registration.
    static DefaultSurfaceType s_instance(orch);
    return s_instance;
}

const char *DefaultSurfaceType::id_impl() const noexcept
{
    // Stable id used by activation.ini and by the generated exclusive-step
    // config option when more than one surface type plugin is active.
    return k_default_surface_type_id;
}

slicing_step_t DefaultSurfaceType::step_impl() const noexcept
{
    return STEP_SURFACE_TYPE;
}

const char *const *DefaultSurfaceType::dependencies_impl() const noexcept
{
    return k_no_dependencies;
}

int32_t DefaultSurfaceType::priority_impl() const noexcept
{
    return 0;
}

const char *DefaultSurfaceType::progress_message_format_impl() const noexcept
{
    return "Default surface type detection: %u / %u objects";
}

void DefaultSurfaceType::setup_run_impl(const plugin_run_context *) const
{
    // The whole object is one work unit: internally the legacy code performs
    // several passes over all layers, but the public progress remains object
    // based until those passes become real modules.
    progress().add_max(1);
}

void DefaultSurfaceType::run_impl(const plugin_run_context *run_ctx) const
{
    // run_step() provides one payload per PrintObject. The selected plugin owns
    // the final typing/refinement of all LayerRegion::fill_surfaces() in it.
    const run_ctx_detect_surface_type *ctx = plugin_ctx_as_detect_surface_type(run_ctx);
    Slic3r::PrintObject *object = ctx == nullptr ? nullptr : to_object(ctx->object);
    if (object == nullptr)
        return;

    run_surface_type_pipeline(run_ctx, *object);
    progress().increment();
}

void DefaultSurfaceType::run_surface_type_pipeline(const plugin_run_context *run_ctx,
                                                   Slic3r::PrintObject &object) const
{
    // This method mirrors the old PrintObject::prepare_infill() order, but each
    // group now has a name. That makes the data flow visible before we decide
    // whether to expose a SURFACE_TYPE_MODULE ABI.
    if (!start_prepare_infill(object))
        return;

    restore_untyped_input_if_needed(run_ctx, object);
    classify_top_bottom_surfaces(run_ctx, object);
    prepare_fill_surfaces(run_ctx, object);

    // The old path changes where bridge/external surface expansion runs
    // depending on ensure_vertical_shell_thickness. Preserve that order exactly:
    // "disabled" and "enabled_old" expand before vertical shells; "partial" and
    // "enabled" expand after vertical shells.
    const Slic3r::EnsureVerticalShellThickness ensure_vertical_shell_thickness =
        object.default_region_config(object.m_print->default_region_config())
            .option<Slic3r::ConfigOptionEnum<Slic3r::EnsureVerticalShellThickness>>("ensure_vertical_shell_thickness")->value;
    if (ensure_vertical_shell_thickness != Slic3r::EnsureVerticalShellThickness::Partial &&
        ensure_vertical_shell_thickness != Slic3r::EnsureVerticalShellThickness::Enabled) {
        apply_external_expansion_and_bridge_detection(run_ctx, object, true);
    }

    ensure_vertical_shells(run_ctx, object);

    if (ensure_vertical_shell_thickness == Slic3r::EnsureVerticalShellThickness::Partial ||
        ensure_vertical_shell_thickness == Slic3r::EnsureVerticalShellThickness::Enabled) {
        apply_external_expansion_and_bridge_detection(run_ctx, object, false);
    }

    ensure_horizontal_shells(run_ctx, object);
    clean_surface_collections(run_ctx, object);
    build_bridge_over_infill_data(run_ctx, object);
    combine_infill_surfaces(run_ctx, object);

    object.set_done(Slic3r::posPrepareInfill);
}

bool DefaultSurfaceType::start_prepare_infill(Slic3r::PrintObject &object) const
{
    // Reuse the existing PrintObject state bit while STEP_PRE_INFILL is being
    // split. If another path already prepared infill, the plugin becomes a no-op.
    return object.set_started(Slic3r::posPrepareInfill);
}

void DefaultSurfaceType::restore_untyped_input_if_needed(const plugin_run_context *run_ctx,
                                                         Slic3r::PrintObject &object) const
{
    throw_if_cancelled(run_ctx);
    if (!object.has_typed_slices())
        return;

    // Re-running from already typed slices is fragile: the old native code
    // first goes back to the raw internal/sparse slices, then classifies from
    // scratch. Keep that behavior as the first module in this pipeline.
    object.restore_untyped_slices();
    object.m_print->throw_if_canceled();
}

void DefaultSurfaceType::classify_top_bottom_surfaces(const plugin_run_context *run_ctx,
                                                      Slic3r::PrintObject &object) const
{
    throw_if_cancelled(run_ctx);
    // Classify raw slices as top, bottom, bridge-bottom or internal, then clip
    // that classification onto the fill_surfaces created by STEP_SURFACE_GENERATION.
    object.detect_surfaces_type();
    object.m_print->throw_if_canceled();
}

void DefaultSurfaceType::prepare_fill_surfaces(const plugin_run_context *run_ctx,
                                               Slic3r::PrintObject &object) const
{
    throw_if_cancelled(run_ctx);
    BOOST_LOG_TRIVIAL(info) << "Preparing fill surfaces..." << Slic3r::log_memory_info();

    // LayerRegion owns the region-local decisions: disabling top/bottom solid
    // infill when layer counts are zero and promoting tiny sparse areas to
    // solid infill. Keep it after top/bottom classification and before any
    // shell expansion.
    for (Slic3r::Layer &layer : object.layers()) {
        for (Slic3r::LayerRegion &region : layer.regions()) {
            region.prepare_fill_surfaces();
            object.m_print->throw_if_canceled();
        }
        throw_if_cancelled(run_ctx);
    }

    // This is still object-wide because the legacy setting compares the total
    // printed area at one Z, not just one LayerRegion.
    object.apply_solid_infill_below_layer_area();
    object.m_print->throw_if_canceled();
}

void DefaultSurfaceType::apply_external_expansion_and_bridge_detection(const plugin_run_context *run_ctx,
                                                                       Slic3r::PrintObject &object,
                                                                       const bool old_algorithm) const
{
    throw_if_cancelled(run_ctx);
    // Bridge detection expands top/bottom areas, computes bridge direction,
    // clips overlaps and merges compatible surfaces. It intentionally stays as
    // one module until the bridge data flow is disentangled from surface growth.
    object.process_external_surfaces(old_algorithm);
    object.m_print->throw_if_canceled();
}

void DefaultSurfaceType::ensure_vertical_shells(const plugin_run_context *run_ctx,
                                                Slic3r::PrintObject &object) const
{
    throw_if_cancelled(run_ctx);
    // Add solid fill around sloping walls so the configured vertical shell
    // thickness is respected.
    object.discover_vertical_shells();
    object.m_print->throw_if_canceled();
}

void DefaultSurfaceType::ensure_horizontal_shells(const plugin_run_context *run_ctx,
                                                  Slic3r::PrintObject &object) const
{
    throw_if_cancelled(run_ctx);
    // Split internal areas near top/bottom surfaces so top and bottom shells
    // receive the required number of solid layers.
    object.discover_horizontal_shells();
    object.m_print->throw_if_canceled();
}

void DefaultSurfaceType::clean_surface_collections(const plugin_run_context *run_ctx,
                                                   Slic3r::PrintObject &object) const
{
    throw_if_cancelled(run_ctx);
    // Remove too-thin remnants and merge contiguous surfaces with compatible
    // types before bridge-over-infill creates additional overlapping tags.
    object.clean_surfaces();
    object.m_print->throw_if_canceled();
}

void DefaultSurfaceType::build_bridge_over_infill_data(const plugin_run_context *run_ctx,
                                                       Slic3r::PrintObject &object) const
{
    throw_if_cancelled(run_ctx);
    // Dense infill near bridges is tagged before bridge_over_infill(), because
    // bridge expansion must not grow through dense support areas unchecked.
    object.tag_under_bridge();
    object.m_print->throw_if_canceled();

    object.bridge_over_infill();
    object.m_print->throw_if_canceled();

    // Mark solid surfaces that sit above bridge-tagged solid surfaces. The four
    // calls cover top/internal surfaces above either internal bridges or bottom
    // bridges, matching the old prepare_infill() sequence one-for-one.
    object.replaceSurfaceType(Slic3r::stPosInternal | Slic3r::stDensSolid,
        Slic3r::stPosInternal | Slic3r::stDensSolid | Slic3r::stModOverBridge,
        Slic3r::stPosInternal | Slic3r::stDensSolid | Slic3r::stModBridge);
    object.m_print->throw_if_canceled();
    object.replaceSurfaceType(Slic3r::stPosTop | Slic3r::stDensSolid,
        Slic3r::stPosTop | Slic3r::stDensSolid | Slic3r::stModOverBridge,
        Slic3r::stPosInternal | Slic3r::stDensSolid | Slic3r::stModBridge);
    object.m_print->throw_if_canceled();
    object.replaceSurfaceType(Slic3r::stPosInternal | Slic3r::stDensSolid,
        Slic3r::stPosInternal | Slic3r::stDensSolid | Slic3r::stModOverBridge,
        Slic3r::stPosBottom | Slic3r::stDensSolid | Slic3r::stModBridge);
    object.m_print->throw_if_canceled();
    object.replaceSurfaceType(Slic3r::stPosTop | Slic3r::stDensSolid,
        Slic3r::stPosTop | Slic3r::stDensSolid | Slic3r::stModOverBridge,
        Slic3r::stPosBottom | Slic3r::stDensSolid | Slic3r::stModBridge);
    object.m_print->throw_if_canceled();
}

void DefaultSurfaceType::combine_infill_surfaces(const plugin_run_context *run_ctx,
                                                 Slic3r::PrintObject &object) const
{
    throw_if_cancelled(run_ctx);
    // Final staging for infill generation: combine sparse infill according to
    // "infill every N layers" and refresh fill_aligned_z sparse spacing data.
    object.combine_infill();
    object.m_print->throw_if_canceled();
    object._compute_max_sparse_spacing();
}

void register_default_surface_type_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, DefaultSurfaceType::instance(orch).c_instance());
}

}}} // namespace slic3r_api::SurfaceType::DefaultSurfaceTypePlugin

#ifdef DEFAULT_SURFACE_TYPE_PLUGIN_DLL
extern "C" void register_plugin(orchestrator_handle *orch)
{
    slic3r_api::SurfaceType::DefaultSurfaceTypePlugin::register_default_surface_type_plugin(orch);
}
#endif // DEFAULT_SURFACE_TYPE_PLUGIN_DLL
