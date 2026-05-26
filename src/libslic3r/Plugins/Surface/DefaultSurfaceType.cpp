///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "DefaultSurfaceType.hpp"

#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_surface_type.h"
#include "libslic3r/PrintObject.hpp"

namespace slic3r_api { namespace SurfaceType { namespace DefaultSurfaceTypePlugin {
namespace {

const char *k_default_surface_type_id = "surface.type.default";
const char *k_no_dependencies[] = { nullptr };

Slic3r::PrintObject *to_object(const object_handle *handle)
{
    return const_cast<Slic3r::PrintObject *>(reinterpret_cast<const Slic3r::PrintObject *>(handle));
}

} // namespace

DefaultSurfaceType &
DefaultSurfaceType::instance(orchestrator_handle *orch)
{
    static DefaultSurfaceType s_instance(orch);
    return s_instance;
}

const char *DefaultSurfaceType::id_impl() const noexcept
{
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
    progress().add_max(1);
}

void DefaultSurfaceType::run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_detect_surface_type *ctx = plugin_ctx_as_detect_surface_type(run_ctx);
    Slic3r::PrintObject *object = ctx == nullptr ? nullptr : to_object(ctx->object);
    if (object == nullptr)
        return;

    throw_if_cancelled(run_ctx);
    object->prepare_infill();
    progress().increment();
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
