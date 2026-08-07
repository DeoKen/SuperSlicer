///|/ Copyright (c) 2026
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "NotchAvoidance.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Slic3r {

// Front corner keep-out rectangles in bed coordinates, millimetres, already
// grown by the clearance. Measured by jogging the nozzle toward each corner
// until the toolhead contacted the stepper mount, which stopped it 40 mm out
// on both axes; CLEARANCE keeps travels off that contact point.
static const double NOTCH_SIZE = 40.;
static const double BED_WIDTH  = 350.;
static const double CLEARANCE  = 1.;

struct Zone { double x0, y0, x1, y1; };

static const std::array<Zone, 2> ZONES { {
    // front left
    { -CLEARANCE,                        -CLEARANCE,
       NOTCH_SIZE + CLEARANCE,            NOTCH_SIZE + CLEARANCE },
    // front right
    { BED_WIDTH - NOTCH_SIZE - CLEARANCE, -CLEARANCE,
      BED_WIDTH + CLEARANCE,              NOTCH_SIZE + CLEARANCE }
} };

// The only corners a detour can usefully turn through: the inner corner of each
// zone, nudged out so a waypoint never lands exactly on a boundary.
static const double WAYPOINT_MARGIN = 0.05;

static const std::array<Vec2d, 2> WAYPOINTS { {
    { NOTCH_SIZE + CLEARANCE + WAYPOINT_MARGIN,             NOTCH_SIZE + CLEARANCE + WAYPOINT_MARGIN },
    { BED_WIDTH - NOTCH_SIZE - CLEARANCE - WAYPOINT_MARGIN, NOTCH_SIZE + CLEARANCE + WAYPOINT_MARGIN }
} };

static const double EPS = 1e-9;

// Liang-Barsky. True when the segment passes through the interior of z; merely
// grazing an edge leaves a zero length interval and does not count.
static bool segment_hits(const Vec2d &p, const Vec2d &q, const Zone &z)
{
    const double dx = q.x() - p.x(), dy = q.y() - p.y();
    double t0 = 0., t1 = 1.;
    const double num[4] = { -dx, dx, -dy, dy };
    const double den[4] = { p.x() - z.x0, z.x1 - p.x(), p.y() - z.y0, z.y1 - p.y() };
    for (int i = 0; i < 4; ++ i) {
        if (std::abs(num[i]) < EPS) {
            if (den[i] < 0.)
                return false;
        } else {
            const double r = den[i] / num[i];
            if (num[i] < 0.) {
                if (r > t1) return false;
                t0 = std::max(t0, r);
            } else {
                if (r < t0) return false;
                t1 = std::min(t1, r);
            }
        }
    }
    return (t1 - t0) > EPS;
}

static bool point_inside(const Vec2d &p, const Zone &z)
{
    return p.x() > z.x0 + EPS && p.x() < z.x1 - EPS &&
           p.y() > z.y0 + EPS && p.y() < z.y1 - EPS;
}

static Vec2d to_mm(const Point &p) { return Vec2d(unscaled<double>(p.x()), unscaled<double>(p.y())); }

bool NotchAvoidance::crosses(const Point &a, const Point &b) const
{
    if (! m_enabled)
        return false;
    const Vec2d p = to_mm(a), q = to_mm(b);
    for (const Zone &z : ZONES)
        if (segment_hits(p, q, z))
            return true;
    return false;
}

bool NotchAvoidance::contains(const Point &p) const
{
    if (! m_enabled)
        return false;
    const Vec2d v = to_mm(p);
    for (const Zone &z : ZONES)
        if (point_inside(v, z))
            return true;
    return false;
}

bool NotchAvoidance::reroute(const Point &a, const Point &b, Points &out) const
{
    const Vec2d start = to_mm(a), end = to_mm(b);

    auto clear = [](const Vec2d &p, const Vec2d &q) {
        for (const Zone &z : ZONES)
            if (segment_hits(p, q, z))
                return false;
        return true;
    };

    if (clear(start, end))
        return true;                       // nothing to do

    // A move starting inside a zone is an escape move; blocking it would trap
    // the toolhead there. Let it out untouched.
    for (const Zone &z : ZONES)
        if (point_inside(start, z))
            return true;

    // Two candidate turning points. Try each alone, then both in either order.
    // That covers every route this geometry admits: the notches sit in opposite
    // corners of one open bed, so a path never needs more than two turns.
    double best_len = std::numeric_limits<double>::infinity();
    std::vector<Vec2d> best;

    auto consider = [&](std::vector<Vec2d> &&via) {
        Vec2d prev = start;
        double len = 0.;
        for (const Vec2d &w : via) {
            if (! clear(prev, w))
                return;
            len += (w - prev).norm();
            prev = w;
        }
        if (! clear(prev, end))
            return;
        len += (end - prev).norm();
        if (len < best_len) {
            best_len = len;
            best     = std::move(via);
        }
    };

    consider({ WAYPOINTS[0] });
    consider({ WAYPOINTS[1] });
    consider({ WAYPOINTS[0], WAYPOINTS[1] });
    consider({ WAYPOINTS[1], WAYPOINTS[0] });

    if (best.empty())
        return false;

    for (const Vec2d &w : best)
        out.emplace_back(Point::new_scale(w.x(), w.y()));
    return true;
}

bool NotchAvoidance::reroute_polyline(const Polyline &in, Polyline &out) const
{
    if (in.size() < 2)
        return true;
    out.points.clear();
    out.points.reserve(in.points.size() + 4);
    out.points.emplace_back(in.points.front());
    for (size_t i = 1; i < in.points.size(); ++ i) {
        const Point &a = in.points[i - 1];
        const Point &b = in.points[i];
        if (this->crosses(a, b) && ! this->reroute(a, b, out.points))
            return false;
        out.points.emplace_back(b);
    }
    return true;
}

Polygons NotchAvoidance::zone_polygons()
{
    Polygons out;
    out.reserve(ZONES.size());
    for (const Zone &z : ZONES) {
        Polygon p;
        p.points = { Point::new_scale(z.x0, z.y0), Point::new_scale(z.x1, z.y0),
                     Point::new_scale(z.x1, z.y1), Point::new_scale(z.x0, z.y1) };
        out.emplace_back(std::move(p));
    }
    return out;
}

} // namespace Slic3r
