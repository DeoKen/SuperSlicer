#include "BedKeepOut.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <boost/algorithm/string.hpp>

#include "../ClipperUtils.hpp"

namespace Slic3r {

static constexpr double EPS = 1e-9;
// Detour turning points are placed this far outside the zone corners, so that a detour never runs exactly on a boundary.
static constexpr double CORNER_MARGIN = 0.05;

bool BedKeepOut::parse(const std::string &text, std::vector<BoundingBoxf> &zones, std::string *error)
{
    zones.clear();
    std::vector<std::string> items;
    boost::split(items, text, boost::is_any_of(";"));
    for (std::string item : items) {
        boost::trim(item);
        if (item.empty())
            continue;
        std::vector<std::string> nums;
        boost::split(nums, item, boost::is_any_of(","));
        double v[4];
        bool   ok = nums.size() == 4;
        for (size_t i = 0; ok && i < 4; ++ i) {
            std::string n = boost::trim_copy(nums[i]);
            char *end = nullptr;
            v[i] = std::strtod(n.c_str(), &end);
            ok = ! n.empty() && end != nullptr && *end == 0;
        }
        if (! ok || v[2] <= v[0] || v[3] <= v[1]) {
            if (error)
                *error = "Invalid bed keep-out zone \"" + item + "\", expected x0,y0,x1,y1 with x1 > x0 and y1 > y0";
            zones.clear();
            return false;
        }
        zones.emplace_back(Vec2d(v[0], v[1]), Vec2d(v[2], v[3]));
    }
    return true;
}

std::vector<BoundingBoxf> BedKeepOut::parse(const std::string &text)
{
    std::vector<BoundingBoxf> zones;
    parse(text, zones);
    return zones;
}

// Liang-Barsky: true when the segment p-q passes through the interior of the box.
static bool segment_hits(const Vec2d &p, const Vec2d &q, const BoundingBoxf &z)
{
    const double dx = q.x() - p.x(), dy = q.y() - p.y();
    double t0 = 0., t1 = 1.;
    const double num[4] = { -dx, dx, -dy, dy };
    const double den[4] = { p.x() - z.min.x(), z.max.x() - p.x(), p.y() - z.min.y(), z.max.y() - p.y() };
    for (int i = 0; i < 4; ++ i) {
        if (std::abs(num[i]) < EPS) {
            // Parallel to this edge: outside or on the edge line means no interior crossing.
            if (den[i] <= EPS)
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
    if (t1 - t0 <= EPS)
        return false;
    // The clipped part must be strictly inside, not just along an edge.
    const Vec2d m = p + (q - p) * (0.5 * (t0 + t1));
    return m.x() > z.min.x() + EPS && m.x() < z.max.x() - EPS && m.y() > z.min.y() + EPS && m.y() < z.max.y() - EPS;
}

static bool point_inside(const Vec2d &p, const BoundingBoxf &z)
{
    return p.x() > z.min.x() + EPS && p.x() < z.max.x() - EPS && p.y() > z.min.y() + EPS && p.y() < z.max.y() - EPS;
}

static Vec2d to_mm(const Point &p) { return unscaled<double>(p); }

void BedKeepOut::init(const std::vector<BoundingBoxf> &zones, const BoundingBoxf &bed)
{
    m_zones.clear();
    m_corners.clear();
    for (const BoundingBoxf &z : zones)
        m_zones.emplace_back(z.min - Vec2d(CLEARANCE, CLEARANCE), z.max + Vec2d(CLEARANCE, CLEARANCE));
    for (const BoundingBoxf &z : m_zones)
        for (const Vec2d &c : { Vec2d(z.min.x() - CORNER_MARGIN, z.min.y() - CORNER_MARGIN), Vec2d(z.max.x() + CORNER_MARGIN, z.min.y() - CORNER_MARGIN),
                                Vec2d(z.max.x() + CORNER_MARGIN, z.max.y() + CORNER_MARGIN), Vec2d(z.min.x() - CORNER_MARGIN, z.max.y() + CORNER_MARGIN) })
            if (bed.contains(c) && ! this->contains(c))
                m_corners.emplace_back(c);
}

bool BedKeepOut::crosses(const Vec2d &a, const Vec2d &b) const
{
    for (const BoundingBoxf &z : m_zones)
        if (segment_hits(a, b, z))
            return true;
    return false;
}

bool BedKeepOut::contains(const Vec2d &p) const
{
    for (const BoundingBoxf &z : m_zones)
        if (point_inside(p, z))
            return true;
    return false;
}

bool BedKeepOut::intersects(const Polyline &polyline, const Point &offset) const
{
    if (m_zones.empty() || polyline.empty())
        return false;
    if (polyline.size() == 1)
        return this->contains(to_mm(polyline.front() + offset));
    Vec2d prev = to_mm(polyline.points.front() + offset);
    for (size_t i = 1; i < polyline.size(); ++ i) {
        const Vec2d next = to_mm(polyline.points[i] + offset);
        if (this->crosses(prev, next))
            return true;
        prev = next;
    }
    return false;
}

bool BedKeepOut::intersects(const ExPolygons &expolygons) const
{
    for (const BoundingBoxf &z : m_zones) {
        const BoundingBox zs(Point::new_scale(z.min.x(), z.min.y()), Point::new_scale(z.max.x(), z.max.y()));
        Polygons zone { Polygon { zs.min, Point(zs.max.x(), zs.min.y()), zs.max, Point(zs.min.x(), zs.max.y()) } };
        for (const ExPolygon &expoly : expolygons)
            // Cheap reject first, the exact test only for areas near the zone.
            if (get_extents(expoly.contour).overlap(zs) && ! intersection_ex(expoly, zone).empty())
                return true;
    }
    return false;
}

bool BedKeepOut::reroute_segment(const Vec2d &start, const Vec2d &end, std::vector<Vec2d> &via) const
{
    // Shortest path in the visibility graph of start, end and the zone corners (Dijkstra). There are only a few
    // zones, so the graph has a handful of nodes, and this only runs for the rare travels that hit a zone.
    std::vector<Vec2d> nodes;
    nodes.reserve(m_corners.size() + 2);
    nodes.emplace_back(start);
    nodes.emplace_back(end);
    nodes.insert(nodes.end(), m_corners.begin(), m_corners.end());
    const size_t n = nodes.size();
    std::vector<double> dist(n, std::numeric_limits<double>::infinity());
    std::vector<size_t> prev(n, size_t(-1));
    std::vector<bool>   done(n, false);
    dist[0] = 0.;
    for (;;) {
        size_t u = size_t(-1);
        for (size_t i = 0; i < n; ++ i)
            if (! done[i] && (u == size_t(-1) || dist[i] < dist[u]))
                u = i;
        if (u == size_t(-1) || std::isinf(dist[u]) || u == 1)
            break;
        done[u] = true;
        for (size_t v = 0; v < n; ++ v)
            if (! done[v] && v != u) {
                const double d = dist[u] + (nodes[v] - nodes[u]).norm();
                if (d < dist[v] && ! this->crosses(nodes[u], nodes[v])) {
                    dist[v] = d;
                    prev[v] = u;
                }
            }
    }
    if (std::isinf(dist[1]))
        return false;
    std::vector<Vec2d> path;
    for (size_t v = prev[1]; v != 0; v = prev[v])
        path.emplace_back(nodes[v]);
    via.insert(via.end(), path.rbegin(), path.rend());
    return true;
}

bool BedKeepOut::reroute(const Polyline &in, Polyline &out) const
{
    out.points.clear();
    if (in.empty())
        return true;
    out.points.reserve(in.points.size() + 4);
    out.points.emplace_back(in.points.front());
    std::vector<Vec2d> via;
    for (size_t i = 1; i < in.points.size(); ++ i) {
        const Vec2d a = to_mm(in.points[i - 1]);
        const Vec2d b = to_mm(in.points[i]);
        // A move starting inside a zone gets the toolhead out of it: blocking it would trap it there.
        if (this->crosses(a, b) && ! this->contains(a)) {
            via.clear();
            if (! this->reroute_segment(a, b, via))
                return false;
            for (const Vec2d &w : via)
                out.points.emplace_back(Point::new_scale(w.x(), w.y()));
        }
        out.points.emplace_back(in.points[i]);
    }
    return true;
}

} // namespace Slic3r
