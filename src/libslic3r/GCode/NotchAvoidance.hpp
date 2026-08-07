///|/ Copyright (c) 2026
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GCode_NotchAvoidance_hpp_
#define slic3r_GCode_NotchAvoidance_hpp_

#include "../libslic3r.h"
#include "../BoundingBox.hpp"
#include "../Point.hpp"
#include "../Polygon.hpp"
#include "../Polyline.hpp"

#include <vector>

namespace Slic3r {

// Keeps travel moves out of two hard coded rectangles at the front corners of
// the bed: the space taken by the AWD stepper mounts on a 350 Voron, which the
// toolhead cannot enter at any Z.
//
// Deliberately hard coded rather than derived from bed_shape. A concave
// bed_shape makes the build volume Type::Custom, which switches large parts of
// the interactive UI (outside-of-bed checks, arrange) onto much slower code
// paths for the whole session. With the zones held here the printer profile
// keeps a plain rectangular bed and none of that is triggered; the notches
// exist only while G-code is being generated.
//
// Everything is axis aligned, so the tests are plain float comparisons -- no
// polygon clipping, nothing allocated per move.
class NotchAvoidance
{
public:
    // Zones are only built when enabled; while disabled every entry point is a
    // single bool test.
    void        set_enabled(bool enabled) { m_enabled = enabled; }
    bool        is_active() const { return m_enabled; }

    // True if the segment enters a zone. Endpoints touching a boundary do not
    // count, so a travel may run along the edge of a zone.
    bool        crosses(const Point &a, const Point &b) const;
    // True if the point is strictly inside a zone.
    bool        contains(const Point &p) const;

    // Detour waypoints taking a to b clear of both zones, appended to out.
    // False if no route exists, in which case out is untouched.
    bool        reroute(const Point &a, const Point &b, Points &out) const;

    // Rewrite a whole travel polyline, inserting waypoints around any segment
    // that enters a zone. False if some segment cannot be routed.
    bool        reroute_polyline(const Polyline &in, Polyline &out) const;

    // True if any part of the polyline lies inside a zone. Used for extrusions
    // that cannot be rerouted, so a hit has to abort the slice.
    bool        intersects(const Polyline &pl) const;

    // The zones as polygons, for the object placement check.
    static Polygons zone_polygons();

    // The zones as unscaled axis aligned boxes in bed coordinates. Used by the
    // plater's outside-of-bed test and by the print volume shader, both of
    // which work in unscaled world coordinates. Independent of m_enabled, so
    // callers gate on their own config read.
    static const std::vector<BoundingBoxf>& zone_boxes();

private:
    bool m_enabled { false };
};

} // namespace Slic3r

#endif // slic3r_GCode_NotchAvoidance_hpp_
