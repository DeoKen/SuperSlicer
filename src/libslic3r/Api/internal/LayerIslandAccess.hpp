///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Api_internal_LayerIslandAccess_hpp_
#define slic3r_Api_internal_LayerIslandAccess_hpp_

#include <vector>

namespace Slic3r {

class LayerSliceIsland;
class ExPolygon;
using ExPolygons = std::vector<ExPolygon>;

namespace ApiInternal {

struct LayerIslandAccess
{
    static ExPolygon &slice_mutable(LayerSliceIsland &island);
    static void set_fill_expolygons(LayerSliceIsland &island, ExPolygons &&fill_expolygons);
    static ExPolygons &fill_no_overlap_expolygons_mutable(LayerSliceIsland &island);
    static ExPolygons &perimeter_slices_mutable(LayerSliceIsland &island);
};

} // namespace ApiInternal

} // namespace Slic3r


#endif // slic3r_Api_internal_LayerIslandAccess_hpp_
