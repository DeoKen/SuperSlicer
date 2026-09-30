#ifndef slic3r_GCode_BedKeepOut_hpp_
#define slic3r_GCode_BedKeepOut_hpp_

#include <string>
#include <vector>

#include "../libslic3r.h"
#include "../BoundingBox.hpp"
#include "../ExPolygon.hpp"
#include "../Point.hpp"
#include "../Polyline.hpp"

namespace Slic3r {

// Rectangles of the bed the toolhead cannot reach at any height (bed_keep_out_zones), for example the front
// corners taken by the stepper mounts of an AWD Voron.
//
// The bed shape stays a plain rectangle on purpose: a concave bed_shape turns the build volume into
// BuildVolume::Type::Custom, which moves the plater's outside-of-bed test, its shader and arrange onto much
// slower per-vertex code paths. Keeping the zones separate keeps every test here an axis aligned box test.
class BedKeepOut
{
public:
    // Clearance added around every zone, for the travels and the checks.
    static constexpr double CLEARANCE = 1.;

    // Parse "x0,y0,x1,y1;x0,y0,x1,y1" (bed coordinates, mm). Returns false and fills error on a syntax error.
    static bool parse(const std::string &text, std::vector<BoundingBoxf> &zones, std::string *error = nullptr);
    // The zones of a bed_keep_out_zones value, empty if the value is empty or invalid.
    static std::vector<BoundingBoxf> parse(const std::string &text);

    // Set up the zones (without clearance) and the bed bounding box the detours have to stay in.
    void init(const std::vector<BoundingBoxf> &zones, const BoundingBoxf &bed);
    bool is_active() const { return ! m_zones.empty(); }
    // Zones grown by CLEARANCE, bed coordinates in mm.
    const std::vector<BoundingBoxf>& zones() const { return m_zones; }

    // True if the segment passes through the interior of a zone. Running along an edge does not count.
    bool crosses(const Vec2d &a, const Vec2d &b) const;
    // True if the point is strictly inside a zone.
    bool contains(const Vec2d &p) const;
    // Scaled polyline, shifted by offset into bed coordinates: true if any part of it is inside a zone.
    bool intersects(const Polyline &polyline, const Point &offset = Point(0, 0)) const;
    // Scaled areas in bed coordinates: true if they overlap a zone.
    bool intersects(const ExPolygons &expolygons) const;

    // Reroute a scaled travel polyline in bed coordinates around the zones, along the shortest detour through
    // the zone corners. A segment starting inside a zone is left alone (it moves out of the zone).
    // Returns false if some segment cannot be routed.
    bool reroute(const Polyline &in, Polyline &out) const;

private:
    bool reroute_segment(const Vec2d &a, const Vec2d &b, std::vector<Vec2d> &via) const;

    std::vector<BoundingBoxf> m_zones;
    // Candidate detour turning points: the zone corners, nudged outwards, inside the bed and outside all zones.
    std::vector<Vec2d>        m_corners;
};

} // namespace Slic3r

#endif // slic3r_GCode_BedKeepOut_hpp_
