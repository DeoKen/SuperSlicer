///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#pragma once

#include <vector>

namespace Slic3r {

class LayerRegion;
class SurfaceCollection;
class ExPolygon;
using ExPolygons = std::vector<ExPolygon>;

namespace ApiInternal {

struct LayerRegionAccess
{
    static ExPolygons &slices_mutable(LayerRegion &layer_region);
    static SurfaceCollection &surfaces_mutable(LayerRegion &layer_region);
};

} // namespace ApiInternal

} // namespace Slic3r
