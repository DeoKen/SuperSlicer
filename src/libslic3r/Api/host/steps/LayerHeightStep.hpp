///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Api_host_steps_LayerHeightStep_hpp_
#define slic3r_Api_host_steps_LayerHeightStep_hpp_

#include <memory>
#include <stddef.h>
#include <vector>

#include "libslic3r/Api/plugin/c/steps/slic3r_step_layer_height.h"

namespace Slic3r {

class Print;

namespace ApiHost::Steps {

struct LayerHeightRunContext
{
    std::vector<coord_t> enforced_layer_profile;
    std::vector<c_layer_config_range> layer_config_ranges;
    run_ctx_layer_height_generation context_step = {};
};

std::unique_ptr<LayerHeightRunContext> make_layer_height_run_context(Print &print, size_t object_idx);

} // namespace ApiHost::Steps
} // namespace Slic3r

#endif // slic3r_Api_host_steps_LayerHeightStep_hpp_
