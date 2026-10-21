///|/ Copyright (c) Prusa Research 2021 Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Brim_hpp_
#define slic3r_Brim_hpp_

#include <utility>
#include <vector>

#include "ExPolygon.hpp"

namespace Slic3r {

class Print;
class PrintObject;

// Owner of each brim extrusion entity (object and instance index) when some objects have their own brim
// (brim_per_object); empty when all the brim is shared (printed at once, as stock).
using BrimOwners = std::vector<std::pair<const PrintObject*, size_t>>;

// Produce brim lines around those objects, that have the brim enabled.
// Collect islands_area to be merged into the final 1st layer convex hull.
ExtrusionEntityCollection make_brim(const Print &print, PrintTryCancel try_cancel, Polygons &islands_area, BrimOwners &owners);

// Paint-on brim, for the supports: the brim added by the object's enforcers and the brim removed by its blockers (object
// coordinates, first layer).
struct BrimPaintAreas
{
    ExPolygons enforced;
    ExPolygons blocked;
};
BrimPaintAreas brim_paint_areas(const PrintObject &object);

} // Slic3r

#endif // slic3r_Brim_hpp_
