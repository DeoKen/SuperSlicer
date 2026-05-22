///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Api_host_steps_PostSlicingStep_hpp_
#define slic3r_Api_host_steps_PostSlicingStep_hpp_

#include <stddef.h>

#include "libslic3r/Api/plugin/c/steps/slic3r_step_post_slicing.h"

namespace Slic3r {

class Print;

namespace ApiHost::Steps {

run_ctx_post_slicing make_post_slicing_run_context(Print &print, size_t object_idx);

} // namespace ApiHost::Steps
} // namespace Slic3r

#endif // slic3r_Api_host_steps_PostSlicingStep_hpp_
