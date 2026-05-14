///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#pragma once

#include "libslic3r/libslic3r.h"
#include "libslic3r/DataTreeFwd.hpp"

#include <vector>

namespace Slic3r {

class Layer;
class PrintObject;

namespace ApiInternal {

struct PrintObjectAccess
{
    static void set_layer_profile(PrintObject &object, std::vector<coord_t> &&layer_profile);
    static void replace_layers_by_moving_contents(PrintObject &object, LayerUPtrs &&new_layers);
};

} // namespace ApiInternal

} // namespace Slic3r
