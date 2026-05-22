///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Api_host_steps_SlicingStep_hpp_
#define slic3r_Api_host_steps_SlicingStep_hpp_

#include <memory>
#include <stddef.h>

#include "libslic3r/Api/plugin/c/steps/slic3r_step_slicing.h"

namespace Slic3r {

class Print;

namespace ApiHost::Steps {

struct SlicingRunContext
{
    run_ctx_slicing context_step = {};
};

std::unique_ptr<SlicingRunContext> make_slicing_run_context(Print &print, size_t object_idx);

} // namespace ApiHost::Steps
} // namespace Slic3r

#endif // slic3r_Api_host_steps_SlicingStep_hpp_
