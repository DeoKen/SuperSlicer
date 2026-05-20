///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2014 Petr Ledvina @ledvinap
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "ExtrusionEntity.hpp"

#include <cmath>
#include <limits>
#include <sstream>

#include "ClipperUtils.hpp"
#include "ConfigOption.hpp"
#include "Exception.hpp"
#include "ExPolygon.hpp"
#include "Extruder.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "Flow.hpp"

namespace Slic3r {

//// extrusion entity visitor
void ExtrusionVisitor::use(ExtrusionPath &path) { default_use(path); };
void ExtrusionVisitor::use(ExtrusionMultiPath &multipath) { default_use(multipath); }
void ExtrusionVisitor::use(ExtrusionLoop &loop) { default_use(loop); }
void ExtrusionVisitor::use(ExtrusionEntityCollection &collection) { default_use(collection); }
void ExtrusionVisitor::use(ExtrusionNop &nop) { default_use(nop); }

void ExtrusionVisitorConst::use(const ExtrusionPath &path) { default_use(path); }
void ExtrusionVisitorConst::use(const ExtrusionMultiPath &multipath) { default_use(multipath); }
void ExtrusionVisitorConst::use(const ExtrusionLoop &loop) { default_use(loop); }
void ExtrusionVisitorConst::use(const ExtrusionEntityCollection &collection) { default_use(collection); }
void ExtrusionVisitorConst::use(const ExtrusionNop &nop) { default_use(nop); }

void ExtrusionEntity::visit(ExtrusionVisitor &visitor) { visitor.default_use(*this); }
void ExtrusionEntity::visit(ExtrusionVisitorConst &visitor) const { visitor.default_use(*this); }
void ExtrusionEntity::visit(ExtrusionVisitor &&visitor) { this->visit(visitor); }
void ExtrusionEntity::visit(ExtrusionVisitorConst &&visitor) const { this->visit(visitor); }

Point ExtrusionNop::NOT_A_POINT = Point((std::numeric_limits<coord_t>::max)(), (std::numeric_limits<coord_t>::max)());

Point ExtrusionEntity::NOT_A_POINT = Point((std::numeric_limits<coord_t>::max)(), (std::numeric_limits<coord_t>::max)());

ExtrusionEntity::Content ExtrusionEntity::clone_content(const Content &content)
{
    if (const ArcPolyline *polyline = std::get_if<ArcPolyline>(&content))
        return *polyline;
    if (const Children *children = std::get_if<Children>(&content)) {
        Children cloned;
        cloned.reserve(children->size());
        for (const ExtrusionEntityUPtr &child : *children) {
            assert(child);
            cloned.emplace_back(child ? ExtrusionEntityUPtr(child->clone()) : nullptr);
        }
        return cloned;
    }
    return std::monostate();
}

bool ExtrusionEntity::has_polyline() const
{
    return std::holds_alternative<ArcPolyline>(m_content);
}

bool ExtrusionEntity::is_leaf() const
{
    return !std::holds_alternative<Children>(m_content);
}

bool ExtrusionEntity::is_nop() const
{
    return std::holds_alternative<std::monostate>(m_content);
}

const ArcPolyline* ExtrusionEntity::polyline_or_null() const
{
    return std::get_if<ArcPolyline>(&m_content);
}

ArcPolyline* ExtrusionEntity::polyline_or_null()
{
    return std::get_if<ArcPolyline>(&m_content);
}

const ArcPolyline& ExtrusionEntity::polyline_ref() const
{
    const ArcPolyline *polyline = this->polyline_or_null();
    assert(polyline != nullptr);
    return *polyline;
}

ArcPolyline& ExtrusionEntity::polyline_ref()
{
    ArcPolyline *polyline = this->polyline_or_null();
    assert(polyline != nullptr);
    return *polyline;
}

void ExtrusionEntity::set_polyline(const ArcPolyline &polyline)
{
    if (Children *children = std::get_if<Children>(&m_content)) {
        assert(false);
        children->insert(children->begin(), std::make_unique<ExtrusionEntity>(m_can_reverse, polyline));
        return;
    }

    m_content = polyline;
    m_can_sort = false;
    m_continuous = false;
}

void ExtrusionEntity::set_polyline(ArcPolyline &&polyline)
{
    if (Children *children = std::get_if<Children>(&m_content)) {
        assert(false);
        children->insert(children->begin(), std::make_unique<ExtrusionEntity>(m_can_reverse, std::move(polyline)));
        return;
    }

    m_content = std::move(polyline);
    m_can_sort = false;
    m_continuous = false;
}

const ExtrusionEntity::Children& ExtrusionEntity::children() const
{
    static const Children no_children;
    const Children *children = std::get_if<Children>(&m_content);
    return children != nullptr ? *children : no_children;
}

ExtrusionEntity::Children& ExtrusionEntity::children()
{
    return this->ensure_children();
}

size_t ExtrusionEntity::child_count() const
{
    return this->children().size();
}

const ExtrusionEntity& ExtrusionEntity::child(size_t idx) const
{
    assert(!this->is_leaf());
    const Children &children = this->children();
    assert(idx < children.size());
    assert(children[idx]);
    return *children[idx];
}

ExtrusionEntity& ExtrusionEntity::child(size_t idx)
{
    Children &children = this->children();
    assert(idx < children.size());
    assert(children[idx]);
    return *children[idx];
}

ExtrusionEntity::Children& ExtrusionEntity::ensure_children()
{
    if (Children *children = std::get_if<Children>(&m_content))
        return *children;

    Children children;
    if (ArcPolyline *polyline = std::get_if<ArcPolyline>(&m_content)) {
        if (!polyline->empty()) {
            std::unique_ptr<ExtrusionEntity> polyline_child = std::make_unique<ExtrusionEntity>(m_can_reverse, std::move(*polyline));
            polyline_child->m_properties = m_properties;
            children.emplace_back(std::move(polyline_child));
        }
    }

    m_content = std::move(children);
    m_can_sort = true;
    m_continuous = false;
    return std::get<Children>(m_content);
}

ExtrusionEntity& ExtrusionEntity::append_child(ExtrusionEntityUPtr &&child)
{
    assert(child);
    Children &children = this->ensure_children();
    children.emplace_back(std::move(child));
    return *children.back();
}

ExtrusionEntity& ExtrusionEntity::append_child(const ExtrusionEntity &child)
{
    return this->append_child(ExtrusionEntityUPtr(child.clone()));
}

ExtrusionEntity& ExtrusionEntity::append_child(ExtrusionEntity &&child)
{
    return this->append_child(ExtrusionEntityUPtr(child.clone_move()));
}

void ExtrusionEntity::insert_child(size_t idx, ExtrusionEntityUPtr &&child)
{
    assert(child);
    Children &children = this->ensure_children();
    if (idx > children.size())
        idx = children.size();
    children.insert(children.begin() + idx, std::move(child));
}

void ExtrusionEntity::remove_child(size_t idx)
{
    Children &children = this->ensure_children();
    assert(idx < children.size());
    children.erase(children.begin() + idx);
}

void ExtrusionEntity::clear_content()
{
    m_content = std::monostate();
    m_can_sort = false;
    m_continuous = false;
}

bool ExtrusionEntity::is_collection() const
{
    return !this->is_leaf() && !m_continuous && !this->is_loop();
}

bool ExtrusionEntity::is_loop() const
{
    if (this->empty())
        return false;
    return m_continuous && this->first_point() == this->last_point();
}

void ExtrusionEntity::set_can_sort_reverse(bool can_sort, bool can_reverse)
{
    m_can_sort = can_sort;
    m_can_reverse = can_reverse;
}

ExtrusionRole ExtrusionEntity::role() const
{
    if (const ExtrusionAttributes *attributes = this->get_property<ExtrusionAttributes>())
        return attributes->role;

    ExtrusionRole out{ ExtrusionRole::None };
    if (!this->is_leaf()) {
        for (const ExtrusionEntityUPtr &child : this->children()) {
            if (!child)
                continue;
            ExtrusionRole child_role = child->role();
            if (out == ExtrusionRole::None)
                out = child_role;
            else if (out != child_role)
                return ExtrusionRole::Mixed;
        }
    }
    return out;
}

bool ExtrusionEntity::has_role(ExtrusionRole test_role) const
{
    if (const ExtrusionAttributes *attributes = this->get_property<ExtrusionAttributes>())
        return (attributes->role & test_role) == test_role;

    if (!this->is_leaf())
        for (const ExtrusionEntityUPtr &child : this->children())
            if (child && child->has_role(test_role))
                return true;
    return false;
}

void ExtrusionEntity::reverse()
{
    if (ArcPolyline *polyline = this->polyline_or_null()) {
        polyline->reverse();
        return;
    }

    Children *children = std::get_if<Children>(&m_content);
    if (children == nullptr)
        return;
    for (ExtrusionEntityUPtr &child : *children)
        if (child && child->can_reverse() && !child->is_loop())
            child->reverse();
    std::reverse(children->begin(), children->end());
}

const Point& ExtrusionEntity::first_point() const
{
    if (const ArcPolyline *polyline = this->polyline_or_null())
        return polyline->empty() ? NOT_A_POINT : polyline->front();
    if (!this->is_leaf())
        for (const ExtrusionEntityUPtr &child : this->children())
            if (child && !child->empty())
                return child->first_point();
    return NOT_A_POINT;
}

const Point& ExtrusionEntity::last_point() const
{
    if (const ArcPolyline *polyline = this->polyline_or_null())
        return polyline->empty() ? NOT_A_POINT : polyline->back();
    if (!this->is_leaf())
        for (Children::const_reverse_iterator it = this->children().rbegin(); it != this->children().rend(); ++it)
            if (*it && !(*it)->empty())
                return (*it)->last_point();
    return NOT_A_POINT;
}

const Point& ExtrusionEntity::middle_point() const
{
    if (const ArcPolyline *polyline = this->polyline_or_null())
        return polyline->empty() ? NOT_A_POINT : polyline->middle();
    if (!this->is_leaf()) {
        const Children &children = this->children();
        if (children.empty())
            return NOT_A_POINT;
        const ExtrusionEntityUPtr &child = children[children.size() / 2];
        if (child && !child->empty())
            return child->middle_point();
    }
    return NOT_A_POINT;
}

void ExtrusionEntity::polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const
{
    if (const ArcPolyline *polyline = this->polyline_or_null()) {
        const ExtrusionAttributes *attributes = this->get_property<ExtrusionAttributes>();
        if (attributes != nullptr)
            out = union_(out, offset(polyline->to_polyline(), scale_d(attributes->width / 2) + scaled_epsilon));
        return;
    }

    if (!this->is_leaf())
        for (const ExtrusionEntityUPtr &child : this->children())
            if (child)
                child->polygons_covered_by_width(out, scaled_epsilon);
}

void ExtrusionEntity::polygons_covered_by_spacing(Polygons &out, const float spacing_ratio, const float scaled_epsilon) const
{
    if (const ArcPolyline *polyline = this->polyline_or_null()) {
        const ExtrusionAttributes *attributes = this->get_property<ExtrusionAttributes>();
        if (attributes == nullptr)
            return;
        const bool bridge = attributes->role.is_bridge() || (attributes->width * 4 < attributes->height);
        Flow flow = bridge ? Flow::bridging_flow(attributes->width, 0.f) :
                             Flow::new_from_width(attributes->width, 0.f, attributes->height, spacing_ratio);
        if (out.empty()) {
            out = offset(polyline->to_polyline(), 0.5f * float(flow.scaled_spacing()) + scaled_epsilon,
                         Slic3r::ClipperLib::jtMiter, 10);
        } else {
            out = union_(out,
                         offset(polyline->to_polyline(), 0.5f * float(flow.scaled_spacing()) + scaled_epsilon,
                                Slic3r::ClipperLib::jtMiter, 10));
        }
        return;
    }

    if (!this->is_leaf())
        for (const ExtrusionEntityUPtr &child : this->children())
            if (child)
                child->polygons_covered_by_spacing(out, spacing_ratio, scaled_epsilon);
}

ArcPolyline ExtrusionEntity::as_polyline() const
{
    if (const ArcPolyline *polyline = this->polyline_or_null())
        return *polyline;

    ArcPolyline out;
    if (!this->is_leaf())
        for (const ExtrusionEntityUPtr &child : this->children())
            if (child)
                out.append(child->as_polyline());
    return out;
}

void ExtrusionEntity::collect_polylines(ArcPolylines &dst) const
{
    if (const ArcPolyline *polyline = this->polyline_or_null()) {
        if (!polyline->empty())
            dst.emplace_back(*polyline);
        return;
    }

    if (!this->is_leaf())
        for (const ExtrusionEntityUPtr &child : this->children())
            if (child)
                child->collect_polylines(dst);
}

void ExtrusionEntity::collect_points(Points &dst) const
{
    if (const ArcPolyline *polyline = this->polyline_or_null()) {
        append(dst, polyline->to_polyline().points);
        return;
    }

    if (!this->is_leaf())
        for (const ExtrusionEntityUPtr &child : this->children())
            if (child)
                child->collect_points(dst);
}

coordf_t ExtrusionEntity::length() const
{
    if (const ArcPolyline *polyline = this->polyline_or_null())
        return polyline->length();

    coordf_t len = 0;
    if (!this->is_leaf())
        for (const ExtrusionEntityUPtr &child : this->children())
            if (child)
                len += child->length();
    return len;
}

bool ExtrusionEntity::empty() const
{
    if (const ArcPolyline *polyline = this->polyline_or_null())
        return polyline->empty();

    if (!this->is_leaf())
        for (const ExtrusionEntityUPtr &child : this->children())
            if (child && !child->empty())
                return false;
    return true;
}

double ExtrusionEntity::total_volume() const
{
    if (this->has_polyline()) {
        const ExtrusionAttributes *attributes = this->get_property<ExtrusionAttributes>();
        return attributes != nullptr ? attributes->mm3_per_mm * unscaled(this->length()) : 0.;
    }

    double volume = 0.;
    if (!this->is_leaf())
        for (const ExtrusionEntityUPtr &child : this->children())
            if (child)
                volume += child->total_volume();
    return volume;
}

ExtrusionPropertyOverhang &ExtrusionPath::overhang_attributes_mutable() {
    return this->get_or_add_property<ExtrusionPropertyOverhang>();
}

const ExtrusionPropertyOverhang *ExtrusionPath::overhang_attributes() const {
    return this->get_property<ExtrusionPropertyOverhang>();
}

void ExtrusionPath::intersect_expolygons(const ExPolygons &collection, ExtrusionEntityCollection *retval) const
{
    this->_inflate_collection(intersection_pl(Polylines{this->polyline().to_polyline()}, collection), retval);
}

void ExtrusionPath::subtract_expolygons(const ExPolygons &collection, ExtrusionEntityCollection *retval) const
{
    this->_inflate_collection(diff_pl(Polylines{this->polyline().to_polyline()}, collection), retval);
}

void ExtrusionPath::clip_end(coordf_t distance) { this->polyline().clip_end(distance); }

void ExtrusionPath::simplify(coordf_t tolerance, ArcFittingType with_fitting_arc, double fitting_arc_tolerance)
{
    if (this->polyline().has_z_offset()){
        this->polyline().make_arc(ArcFittingType::Disabled, tolerance, fitting_arc_tolerance);
        // TODO: simplify but only for sub-path with same zheight.
        // if (with_fitting_arc) {
        //    this->polyline().simplify(tolerance, with_fitting_arc, fitting_arc_tolerance);
        //}
        return;
    }
    if (with_fitting_arc != ArcFittingType::Disabled) {
        if (role().is_sparse_infill())
            // Use 3x lower resolution than the object fine detail for sparse infill.
            tolerance *= 3.;
        else if (role().is_support())
            // Use 4x lower resolution than the object fine detail for support.
            tolerance *= 4.;
        else if (role().is_skirt())
            // Brim is currently marked as skirt.
            // Use 4x lower resolution than the object fine detail for skirt & brim.
            tolerance *= 4.;
    }
    this->polyline().make_arc(with_fitting_arc, tolerance, fitting_arc_tolerance);
}

coordf_t ExtrusionPath::length() const { return this->polyline().length(); }

void ExtrusionPath::_inflate_collection(const Polylines &polylines, ExtrusionEntityCollection *collection) const
{
    ExtrusionEntitiesPtr to_add;
    for (const Polyline &polyline : polylines)
        to_add.push_back(new ExtrusionPath(ArcPolyline{polyline}, this->attributes(), this->clone_properties(), this->can_reverse()));
    collection->append(std::move(to_add));
}

void ExtrusionPath::polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const
{
    //polygons_append(out, offset(this->polyline().to_polyline(), double(scale_(attributes().width / 2)) + scaled_epsilon));
    out = union_(out, offset(this->polyline().to_polyline(), scale_d(attributes().width / 2) + scaled_epsilon));
}

void ExtrusionPath::polygons_covered_by_spacing(Polygons &out, const float spacing_ratio, const float scaled_epsilon) const
{
    // Instantiating the Flow class to get the line spacing.
    // Don't know the nozzle diameter, setting to zero. It shall not matter it shall be optimized out by the compiler.
    bool bridge = this->role().is_bridge() || (this->width() * 4 < this->height());
    assert(!bridge || attributes().width == attributes().height);
    // TODO: check BRIDGE_FLOW here
    Flow flow = bridge ? Flow::bridging_flow(attributes().width, 0.f) :
                         Flow::new_from_width(attributes().width, 0.f, attributes().height, spacing_ratio);
    if (out.empty()) {
        out = offset(this->polyline().to_polyline(), 0.5f * float(flow.scaled_spacing()) + scaled_epsilon,
                     Slic3r::ClipperLib::jtMiter, 10);
    } else {
        out = union_(out,
                     offset(this->polyline().to_polyline(), 0.5f * float(flow.scaled_spacing()) + scaled_epsilon,
                            Slic3r::ClipperLib::jtMiter, 10));
    }
}

//note: don't suppport arc
double ExtrusionLoop::area() const
{
    double a = 0;
    for (const ExtrusionPath &path : this->paths()) {
        assert(path.size() >= 2);
        if (path.size() >= 2) {
            if (path.polyline().has_arc()) {
                Polyline poly = path.polyline().to_polyline();
                Point prev = poly.front();
                for (size_t idx = 1; idx < poly.size(); ++idx) {
                    const Point &curr = poly[idx];
                    a += cross2(prev.cast<double>(), curr.cast<double>());
                    prev = curr;
                }
            } else {
                // Assumming that the last point of one path segment is repeated at the start of the following path segment.
                Point prev = path.polyline().front();
                for (size_t idx = 1; idx < path.polyline().size(); ++idx) {
                    const Point &curr = path.polyline().get_point(idx);
                    a += cross2(prev.cast<double>(), curr.cast<double>());
                    prev = curr;
                }
            }
        }
    }
    return a * 0.5;
}

bool ExtrusionLoop::is_counter_clockwise() const {
    return this->area() > 0;
}

bool ExtrusionLoop::is_clockwise() const { return !is_counter_clockwise(); }

void ExtrusionLoop::reverse()
{
    for (ExtrusionPath &path : this->paths())
        path.reverse();
    std::reverse(this->paths().begin(), this->paths().end());
}

Polygon ExtrusionLoop::polygon() const
{
    Polygon polygon;
    for (const ExtrusionPath &path : this->paths()) {
        // for each polyline, append all points except the last one (because it coincides with the first one of the next polyline)
        Polyline poly = path.polyline().to_polyline();
        polygon.points.insert(polygon.points.end(), poly.begin(), poly.end() - 1);
    }
    return polygon;
}

ArcPolyline ExtrusionLoop::as_polyline() const
{
    ArcPolyline polyline;
    for (const ExtrusionPath &path : this->paths()) {
        polyline.append(path.as_polyline());
    }
    return polyline;
}

double ExtrusionLoop::length() const
{
    double len = 0;
    for (const ExtrusionPath &path : this->paths())
        len += path.polyline().length();
    return len;
}

ExtrusionRole ExtrusionLoop::role() const
{
    if (this->paths().empty())
        return ExtrusionRole::None;
    ExtrusionRole role = this->paths().front().role();
    for (const ExtrusionPath &path : this->paths())
        if (role != path.role()) {
            // ignore travel role
            if (role == ExtrusionRole::Travel) {
                role = path.role();
            } else if (path.role() != ExtrusionRole::Travel) {
                return ExtrusionRole::Mixed;
            }
        }
    return role;
}
bool ExtrusionLoop::has_role(ExtrusionRole test_role) const
{
    if (this->paths().empty())
        return false;
    for (const ExtrusionPath &path : this->paths())
        if (path.has_role(test_role)) {
            return true;
        }
    return false;
}

bool ExtrusionLoop::split_at_vertex(const Point &point, const double scaled_epsilon)
{
    ExtrusionPathView paths = this->paths();
    for (ExtrusionPathView::iterator path = paths.begin(); path != paths.end(); ++path) {
        if (int idx = path->polyline().find_point(point, scaled_epsilon); idx != -1) {
            if (paths.size() == 1) {
                if (idx == 0 || idx == path->size() - 1) {
                    assert(this->first_point().distance_to(point) <= scaled_epsilon);
                    return true;
                }
                // just change the order of points
                ArcPolyline p1, p2;
                path->polyline().split_at_index(idx, p1, p2);
                if (p1.is_valid() && p2.is_valid()) {
                    p2.append(std::move(p1));
                    path->polyline().swap(p2); // swap points & fitting result
                }
            } else if (idx > 0) {
                if (idx < path->size() - 1) {
                    // new paths list starts with the second half of current path
                    ExtrusionPaths new_paths;
                    ArcPolyline p1, p2;
                    path->polyline().split_at_index(idx, p1, p2);
                    new_paths.reserve(paths.size() + 1);
                    {
                        ExtrusionPath p = *path;
                        p.polyline().swap(p2);
                        if (p.polyline().is_valid())
                            new_paths.push_back(p);
                    }

                    // then we add all paths until the end of current path list
                    new_paths.insert(new_paths.end(), path + 1, paths.end()); // not including this path

                    // then we add all paths since the beginning of current list up to the previous one
                    new_paths.insert(new_paths.end(), paths.begin(), path); // not including this path

                    // finally we add the first half of current path
                    {
                        ExtrusionPath p = *path;
                        p.polyline().swap(p1);
                        if (p.polyline().is_valid())
                            new_paths.push_back(p);
                    }
                    // we can now override the old path list with the new one and stop looping
                    this->paths() = std::move(new_paths);
                } else {
                    // last point
                    assert((path)->last_point().distance_to(point) <= scaled_epsilon);
                    assert((path + 1)->first_point().distance_to(point) <= scaled_epsilon);
                    ExtrusionPaths new_paths;
                    new_paths.reserve(paths.size());
                    // then we add all paths until the end of current path list
                    new_paths.insert(new_paths.end(), path + 1, paths.end()); // not including this path
                    // then we add all paths since the beginning of current list up to the previous one
                    new_paths.insert(new_paths.end(), paths.begin(), path + 1); // including this path
                    // we can now override the old path list with the new one and stop looping
                    this->paths() = std::move(new_paths);
                }
            } else {
                // else first point ->
                // if first path - nothign to change.
                // else, then impossible as it's also the last point of the previous path.
                assert(path == paths.begin());
                assert(path->first_point().distance_to(point) <= scaled_epsilon);
            }
            assert(this->first_point().distance_to(point) <= scaled_epsilon);
            return true;
        }
    }
    // The point was not found.
    return false;
}

ExtrusionLoop::ClosestPathPoint ExtrusionLoop::get_closest_path_and_point(const Point &point, bool prefer_non_overhang) const
{
    // Find the closest path and closest point belonging to that path. Avoid overhangs, if asked for.
    ClosestPathPoint out{0, 0};
    double           min2 = std::numeric_limits<double>::max();
    ClosestPathPoint best_non_overhang{0, 0};
    double           min2_non_overhang = std::numeric_limits<double>::max();
    size_t path_idx = 0;
    for (const ExtrusionPath &path : this->paths()) {
        std::pair<int, Point> foot_pt_ = path.polyline().foot_pt(point);
        double                d2       = (foot_pt_.second - point).cast<double>().squaredNorm();
        if (d2 < min2) {
            out.foot_pt     = foot_pt_.second;
            out.path_idx    = path_idx;
            out.segment_idx = foot_pt_.first;
            min2            = d2;
        }
        if (prefer_non_overhang && !path.role().is_bridge() && d2 < min2_non_overhang) {
            best_non_overhang.foot_pt     = foot_pt_.second;
            best_non_overhang.path_idx    = path_idx;
            best_non_overhang.segment_idx = foot_pt_.first;
            min2_non_overhang             = d2;
        }
        ++path_idx;
    }
    if (prefer_non_overhang && min2_non_overhang != std::numeric_limits<double>::max()) {
        // Only apply the non-overhang point if there is one.
        out = best_non_overhang;
    }
    return out;
}

// Splitting an extrusion loop, possibly made of multiple segments, some of the segments may be bridging.
void ExtrusionLoop::split_at(const Point &point, bool prefer_non_overhang, const double scaled_epsilon)
{
    if (this->paths().empty())
        return;
    ExtrusionLoop::ClosestPathPoint close_p = get_closest_path_and_point(point, prefer_non_overhang);
    // Snap p to start or end of segment_idx if closer than scaled_epsilon.
    //{
        const Point pt1 = this->paths()[close_p.path_idx].polyline().get_point(close_p.segment_idx);
        const Point  pt2   = this->paths()[close_p.path_idx].polyline().get_point(close_p.segment_idx + 1);
        // Use close_p.foot_pt instead of point for the comparison, as it's the one that will be used.
        double       d2_1 = (close_p.foot_pt - pt1).cast<double>().squaredNorm();
        double       d2_2 = (close_p.foot_pt - pt2).cast<double>().squaredNorm();
        const double thr2 = scaled_epsilon * scaled_epsilon;
        if (d2_1 < d2_2) {
            if (d2_1 < thr2)
                close_p.foot_pt = pt1;
        } else {
            if (d2_2 < thr2)
                close_p.foot_pt = pt2;
        }
    //}

    // now split path_idx in two parts
    const ExtrusionPath &path = this->paths()[close_p.path_idx];
    assert(path.polyline().is_valid());
    ExtrusionPath        p1(path.attributes(), path.clone_properties(), can_reverse());
    ExtrusionPath        p2(path.attributes(), path.clone_properties(), can_reverse());
    path.polyline().split_at(close_p.foot_pt, p1.polyline(), p2.polyline());

    if (this->paths().size() == 1) {
        if (p1.polyline().size() < 2) {
            this->paths().front().polyline() = std::move(p2.polyline());
        } else if (p2.polyline().size() < 2) {
            this->paths().front().polyline() = std::move(p1.polyline());
        } else {
            p2.polyline().append(std::move(p1.polyline()));
            this->paths().front().polyline() = std::move(p2.polyline());
        }
    } else {
        // install the begining of the new paths
        if (p2.polyline().size() >= 2) {
            this->paths()[close_p.path_idx].polyline() = std::move(p2.polyline());
        } else {
            this->paths().erase(this->paths().begin() + close_p.path_idx);
        }
        //rotate
        if (close_p.path_idx > 0) {
            std::rotate(this->paths().begin(), this->paths().begin() + close_p.path_idx, this->paths().end());
        }
        // install the end
        if (p1.polyline().size() >= 2) {
            this->paths().push_back(std::move(p1));
        }
    }
    // check if it's doing its job.
#ifdef _DEBUG
    Point last_pt = this->last_point();
    for (const ExtrusionPath &path : paths()) {
        assert(last_pt == path.first_point());
        for (int i = 1; i < path.polyline().size(); ++i)
            assert(!path.polyline().get_point(i - 1).coincides_with_epsilon(path.polyline().get_point(i)));
        last_pt = path.last_point();
    }
    assert(close_p.foot_pt.coincides_with_epsilon(this->first_point()));
    //assert(point.distance_to(this->first_point()) <= scaled_epsilon); // can be false, still ok?
#endif
}

ExtrusionPaths clip_end(ExtrusionPaths &paths, coordf_t distance)
{
    ExtrusionPaths removed;

    while (distance > 0 && !paths.empty()) {
        ExtrusionPath &last = paths.back();
        removed.push_back(last);
        coordf_t len = last.length();
        if (len <= distance) {
            paths.pop_back();
            distance -= len;
        } else {
            last.polyline().clip_end(distance);
            removed.back().polyline().clip_start(removed.back().polyline().length() - distance);
            break;
        }
    }
    for(auto& path : paths)
        DEBUG_VISIT(path, LoopAssertVisitor())
    std::reverse(removed.begin(), removed.end());
    return removed;
}

//bool ExtrusionLoop::has_overhang_point(const Point &point) const
//{
//    for (const ExtrusionPath &path : this->paths()) {
//        int pos = path.polyline().find_point(point);
//        if (pos != -1) {
//            // point belongs to this path
//            // we consider it overhang only if it's not an endpoint
//            return (path.role().is_bridge() && pos > 0 && pos != int(path.polyline().size()) - 1);
//        }
//    }
//    return false;
//}

void ExtrusionLoop::polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const
{
    for (const ExtrusionPath &path : this->paths())
        path.polygons_covered_by_width(out, scaled_epsilon);
}

void ExtrusionLoop::polygons_covered_by_spacing(Polygons &out, const float spacing_ratio, const float scaled_epsilon) const
{
    for (const ExtrusionPath &path : this->paths())
        path.polygons_covered_by_spacing(out, spacing_ratio, scaled_epsilon);
}

//TODO del
//double ExtrusionLoop::min_mm3_per_mm() const
//{
//    double min_mm3_per_mm = std::numeric_limits<double>::max();
//    for (const ExtrusionPath &path : this->paths())
//        min_mm3_per_mm = std::min(min_mm3_per_mm, path.min_mm3_per_mm());
//    return min_mm3_per_mm;
//}

void ExtrusionPrinter::default_use(const ExtrusionEntity &entity)
{
    if (const ArcPolyline *polyline = entity.polyline_or_null()) {
        const bool has_z_profile = polyline->has_z_offset();
        ss << (json?"\"":"") << "ExtrusionPath" << (has_z_profile ? "3D" : "") << (entity.can_reverse()?"":"Oriented") << (json?"_":":") << role_to_code(entity.role()) << (json?"\":":"") << "[";
        for (int i = 0; i < polyline->size(); i++) {
            if (i != 0)
                ss << ",";
            double x = (mult * (polyline->get_point(i).x()));
            double y = (mult * (polyline->get_point(i).y()));
            if (has_z_profile) {
                double z = mult * polyline->z_offset(size_t(i));
                ss << std::fixed << "[" << (trunc>0?(int(x*trunc))/double(trunc):x) << "," << (trunc>0?(int(y*trunc))/double(trunc):y) << "," << (trunc>0?(int(z*trunc))/double(trunc):z) << "]";
            } else {
                ss << std::fixed << "["<<(trunc>0?(int(x*trunc))/double(trunc):x) << "," << (trunc>0?(int(y*trunc))/double(trunc):y) <<"]";
            }
        }
        ss << "]";
        return;
    }

    if (!entity.is_leaf()) {
        if (entity.is_loop()) {
            const ExtrusionPropertyLoopRole *loop_role_property = entity.get_property<ExtrusionPropertyLoopRole>();
            ExtrusionLoopRole loop_role = loop_role_property == nullptr ? elrDefault : loop_role_property->loop_role;
            ss << (json?"\"":"") << "ExtrusionLoop" << (json?"_":":") << role_to_code(entity.role())<<"_" << looprole_to_code(loop_role) << (json?"\":":"") << "{";
            if(!entity.can_reverse()) ss << (json?"\"":"") << "oriented" << (json?"\":":"=") << "true,";
        } else if (entity.is_continuous()) {
            ss << (json?"\"":"") << "ExtrusionMultiPath" << (entity.can_reverse()?"":"Oriented") << (json?"_":":") << role_to_code(entity.role()) << (json?"\":":"") << "{";
        } else {
            ss << (json?"\"":"") << "ExtrusionEntityCollection" << (json?"_":":") << role_to_code(entity.role()) << (json?"\":":"") << "{";
            if(!entity.can_sort()) ss << (json?"\"":"") << "no_sort" << (json?"\":":"=") << "true,";
            if(!entity.can_reverse()) ss << (json?"\"":"") << "oriented" << (json?"\":":"=") << "true,";
        }
        const ExtrusionEntity::Children &children = entity.children();
        for (int i = 0; i < children.size(); i++) {
            if (i != 0)
                ss << ",";
            children[i]->visit(*this);
        }
        ss << "}";
    }
}

void ExtrusionLength::default_use(const ExtrusionEntity &entity)
{
    if (!entity.is_leaf()) {
        for (const ExtrusionEntityUPtr &child : entity.children())
            if (child)
                child->visit(*this);
    } else {
        dist += entity.length();
    }
}

double ExtrusionVolume::get(const ExtrusionEntityCollection &coll) {
    coll.visit(*this);
    return volume;
}

void ExtrusionModifyFlow::set(ExtrusionEntityCollection &coll) {
    coll.visit(*this);
}

void ExtrusionVisitorRecursiveConst::default_use(const ExtrusionEntity &entity)
{
    if (!entity.is_leaf())
        for (const ExtrusionEntityUPtr &child : entity.children())
            if (child)
                child->visit(*this);
}
void ExtrusionVisitorRecursive::default_use(ExtrusionEntity &entity)
{
    if (entity.is_leaf())
        return;
    for (ExtrusionEntityUPtr &child : entity.children())
        if (child)
            child->visit(*this);
}

void HasRoleVisitor::default_use(const ExtrusionEntity& entity) {
    if (!entity.is_leaf()) {
        for (const ExtrusionEntityUPtr &child : entity.children()) {
            if (child)
                child->visit(*this);
            if (found)
                return;
        }
    } else {
        found = this->matches(entity);
    }
}
bool HasRoleVisitor::search(const ExtrusionEntity &entity, HasRoleVisitor&& visitor) {
    entity.visit(visitor);
    return visitor.found;
}
bool HasRoleVisitor::search(const ExtrusionEntitiesPtr &entities, HasRoleVisitor&& visitor) {
    for (ExtrusionEntity *ptr : entities) {
        ptr->visit(visitor);
        if (visitor.found) return true;
    }
    return visitor.found;
}

void SimplifyVisitor::start(ExtrusionEntityCollection &coll)
{
    m_last_deleted = false;
    coll.visit(*this);
}

void SimplifyVisitor::default_use(ExtrusionEntity& entity) {
    const ExtrusionAttributes *entity_attributes = entity.get_property<ExtrusionAttributes>();
    const ExtrusionAttributes *attributes        = entity_attributes != nullptr ? entity_attributes : m_current_attributes;
    if (ArcPolyline *polyline = entity.polyline_or_null()) {
        assert(entity_attributes != nullptr);
        if (attributes == nullptr)
            return;

        if (m_min_path_size > 0 && entity.length() < m_min_path_size) {
            m_last_deleted = true;
            return;
        }
        assert(m_scaled_resolution >= SCALED_EPSILON);
        coordf_t tolerance = m_scaled_resolution;
        if (m_use_arc_fitting != ArcFittingType::Disabled) {
            if (attributes->role.is_sparse_infill())
                // Use 3x lower resolution than the object fine detail for sparse infill.
                tolerance *= 3.;
            else if (attributes->role.is_support())
                // Use 4x lower resolution than the object fine detail for support.
                tolerance *= 4.;
            else if (attributes->role.is_skirt())
                // Brim is currently marked as skirt.
                // Use 4x lower resolution than the object fine detail for skirt & brim.
                tolerance *= 4.;
        }
        coordf_t fitting_tolerance = scale_d(m_arc_fitting_tolearance->get_effective_value(attributes->width));
        if (polyline->has_z_offset()) {
            // TODO: simplify but only for sub-path with same zheight.
            //polyline->make_arc(ArcFittingType::Disabled, tolerance, fitting_tolerance);
        } else {
            polyline->make_arc(m_use_arc_fitting, tolerance, fitting_tolerance);
        }
        // extra simplify if points are too close (unless z-profile, as they can have same position but different z)
        if (!polyline->has_z_offset()) {
            for (int i = 1; i < polyline->size(); ++i) {
                if (polyline->get_point(i - 1).coincides_with_epsilon(polyline->get_point(i))) {
                    polyline->make_arc(m_use_arc_fitting, tolerance, fitting_tolerance);
                    break;
                }
            }
            for (int i = 1; i < polyline->size(); ++i) {
                assert(!polyline->get_point(i - 1).coincides_with_epsilon(polyline->get_point(i)));
            }
        }
        return;
    }

    if (entity.is_leaf())
        return;
    if (m_ignore_holes && entity.is_loop()) {
        const ExtrusionPropertyLoopRole *loop_role_property = entity.get_property<ExtrusionPropertyLoopRole>();
        ExtrusionLoopRole loop_role = loop_role_property == nullptr ? elrDefault : loop_role_property->loop_role;
        if ((loop_role & elrHole) != 0)
            return;
    }

    const ExtrusionAttributes *old_current_attributes = m_current_attributes;
    if (entity_attributes != nullptr)
        m_current_attributes = entity_attributes;

    ExtrusionEntity::Children &children = entity.children();
    for (size_t i = 0; i < children.size(); ++i) {
        ExtrusionEntity *child = children[i].get();
        child->visit(*this);
        while (m_last_deleted) {
            if (!entity.is_continuous()) {
                children.erase(children.begin() + i);
                --i;
                m_last_deleted = false;
                break;
            }
            if (i > 0) {
                ArcPolyline *path = child->polyline_or_null();
                ArcPolyline *path_previous = children[i - 1]->polyline_or_null();
                assert(path != nullptr);
                assert(path_previous != nullptr);
                if (path == nullptr || path_previous == nullptr) {
                    m_current_attributes = old_current_attributes;
                    return;
                }
                path_previous->append(*path);
                children.erase(children.begin() + i);
                --i;
            } else if (i + 1 < children.size()) {
                ArcPolyline *path = child->polyline_or_null();
                ArcPolyline *path_next = children[i + 1]->polyline_or_null();
                assert(path != nullptr);
                assert(path_next != nullptr);
                if (path == nullptr || path_next == nullptr) {
                    m_current_attributes = old_current_attributes;
                    return;
                }
                path->append(*path_next);
                children.erase(children.begin() + i + 1);
            } else {
                // return, the caller need to delete me.
                m_current_attributes = old_current_attributes;
                return;
            }
            m_last_deleted = false;
            child = children[i].get();
            child->visit(*this);
        }
    }
    m_current_attributes = old_current_attributes;
}

void GetPathsVisitor::default_use(ExtrusionEntity& entity)
{
    if (ExtrusionPath *path = dynamic_cast<ExtrusionPath*>(&entity)) {
        paths.push_back(path);
        return;
    }
    ExtrusionVisitorRecursive::default_use(entity);
}

void ExtrusionVolume::default_use(const ExtrusionEntity &entity)
{
    if (!entity.is_leaf()) {
        ExtrusionVisitorRecursiveConst::default_use(entity);
        return;
    }
    const ExtrusionAttributes *attributes = entity.get_property<ExtrusionAttributes>();
    if (attributes == nullptr)
        return;
    if (entity.role() == ExtrusionRole::GapFill && !_with_gap_fill)
        return;
    volume += unscaled(entity.length()) * attributes->mm3_per_mm * _flow_ratio;
}

void ExtrusionModifyFlow::default_use(ExtrusionEntity &entity)
{
    if (!entity.is_leaf()) {
        ExtrusionVisitorRecursive::default_use(entity);
        return;
    }
    ExtrusionAttributes *attributes = entity.get_property<ExtrusionAttributes>();
    if (attributes == nullptr)
        return;
    attributes->mm3_per_mm *= _flow_mult;
    attributes->width *= _flow_mult;
}

void CreateBoundingBoxVisitor::default_use(ExtrusionEntity &entity)
{
    if (!entity.is_leaf()) {
        ExtrusionVisitorRecursive::default_use(entity);
        return;
    }
    const ArcPolyline *polyline = entity.polyline_or_null();
    if (polyline == nullptr)
        return;
    for (const Geometry::ArcWelder::Segment &pt : polyline->get_arc())
        bb.merge(pt.point);
}

#ifdef _DEBUGINFO
void LoopAssertVisitor::default_use(const ExtrusionEntity& entity) {
    if (!entity.is_leaf()) {
        release_assert(!entity.empty());
        Point last_pt = entity.is_loop() ? entity.last_point() : entity.first_point();
        const ExtrusionEntity::Children &children = entity.children();
        for (const ExtrusionEntityUPtr &child : children) {
            if (!child)
                continue;
            if (entity.is_loop() || entity.is_continuous())
                release_assert(child->first_point() == last_pt);
            child->visit(*this);
            last_pt = child->last_point();
        }
        if (entity.is_loop())
            release_assert(entity.first_point() == entity.last_point());
        return;
    }
    const ArcPolyline *polyline = entity.polyline_or_null();
    const ExtrusionPropertyOverhang *overhang = entity.get_property<ExtrusionPropertyOverhang>();
    release_assert (!entity.role().is_overhang() || overhang != nullptr);
    if (m_check_length <= 0 || polyline == nullptr)
        return;
    release_assert(!entity.empty());
    const ExtrusionAttributes *attributes = entity.get_property<ExtrusionAttributes>();
    const bool has_z_offset = polyline->has_z_offset();
    // Sawtooth support may use z-profile paths as non-extruding 3D moves.
    release_assert(attributes == nullptr || attributes->mm3_per_mm > 0.000001 || entity.role() == ExtrusionRole::Travel || has_z_offset);
    if (has_z_offset) {
        double length_3d = 0.;
        for (size_t idx = 1; idx < polyline->size(); ++idx) {
            const Point &previous_point = polyline->get_point(idx - 1);
            const Point &point          = polyline->get_point(idx);
            coord_t previous_z = polyline->z_offset(idx - 1);
            coord_t z          = polyline->z_offset(idx);
            coord_t dz         = previous_z < z ? z - previous_z : previous_z - z;
            release_assert(!previous_point.coincides_with_epsilon(point) || dz > 0);
            double xy_length = previous_point.distance_to(point);
            length_3d += std::sqrt(xy_length * xy_length + double(dz) * double(dz));
        }
        release_assert(length_3d > m_check_length);
    } else {
        release_assert(entity.length() > m_check_length);
        for (size_t idx = 1; idx < polyline->size(); ++idx)
            release_assert(!polyline->get_point(idx - 1).coincides_with_epsilon(polyline->get_point(idx)));
    }
}
#endif

} // namespace Slic3r
