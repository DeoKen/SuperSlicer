///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#pragma once

namespace Slic3r {

class LayerSliceIsland;
class ExPolygon;

namespace ApiInternal {

struct LayerIslandAccess
{
    static ExPolygon &slice_mutable(LayerSliceIsland &island);
};

} // namespace ApiInternal

} // namespace Slic3r
