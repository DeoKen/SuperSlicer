///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "PostInfillGapFill.hpp"

#include <cassert>

#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_post_infill.h"

namespace slic3r_api { namespace Infill { namespace PostInfillGapFillPlugin {
namespace {

const char *k_post_infill_gap_fill_id = "infill.post_process.gap_fill";
const char *k_no_dependencies[] = { nullptr };

} // namespace

PostInfillGapFill &
PostInfillGapFill::instance(orchestrator_handle *orch)
{
    static PostInfillGapFill s_instance(orch);
    return s_instance;
}

const char *PostInfillGapFill::id_impl() const noexcept
{
    return k_post_infill_gap_fill_id;
}

const char *PostInfillGapFill::name_impl() const noexcept
{
    return "Post-infill gap fill";
}

const char *PostInfillGapFill::description_impl() const noexcept
{
    return "Reserved post-infill pass for generating narrow residual gap-fill extrusion after normal infill.";
}

slicing_step_t PostInfillGapFill::step_impl() const noexcept
{
    return STEP_POST_INFILL;
}

const char *const *PostInfillGapFill::dependencies_impl() const noexcept
{
    return k_no_dependencies;
}

int32_t PostInfillGapFill::priority_impl() const noexcept
{
    return 0;
}

void PostInfillGapFill::run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_post_infill_generation *ctx = plugin_ctx_as_post_infill_generation(run_ctx);
    assert(ctx != nullptr);
    (void) ctx;

    // This plugin is intentionally a no-op until the post-infill step exposes
    // the residual-area data needed to recreate gap fill independently from
    // Fill::fill_surface_extrusion(). Keeping the pass registered now prevents
    // new INFILL_PATTERN plugins from depending on the old per-pattern gap-fill
    // flag and gives the pipeline a stable extension point.
}

void register_post_infill_gap_fill_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, PostInfillGapFill::instance(orch).c_instance());
}

}}} // namespace slic3r_api::Infill::PostInfillGapFillPlugin
