///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2014 Petr Ledvina @ledvinap
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "ExtrusionEntityVisitors.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>

#include "ConfigOption.hpp"

namespace Slic3r {

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

void ExtrusionNop::visit(ExtrusionVisitor &visitor) { visitor.use(*this); }
void ExtrusionNop::visit(ExtrusionVisitorConst &visitor) const { visitor.use(*this); }

void ExtrusionPath::visit(ExtrusionVisitor &visitor) { visitor.use(*this); }
void ExtrusionPath::visit(ExtrusionVisitorConst &visitor) const { visitor.use(*this); }

void ExtrusionMultiPath::visit(ExtrusionVisitor &visitor) { visitor.use(*this); }
void ExtrusionMultiPath::visit(ExtrusionVisitorConst &visitor) const { visitor.use(*this); }

void ExtrusionLoop::visit(ExtrusionVisitor &visitor) { visitor.use(*this); }
void ExtrusionLoop::visit(ExtrusionVisitorConst &visitor) const { visitor.use(*this); }

void ExtrusionEntityCollection::visit(ExtrusionVisitor &visitor) { visitor.use(*this); }
void ExtrusionEntityCollection::visit(ExtrusionVisitorConst &visitor) const { visitor.use(*this); }

void ExtrusionPrinter::begin_entity()
{
    if (!m_first_child_stack.empty()) {
        if (!m_first_child_stack.back())
            ss << ",";
        m_first_child_stack.back() = false;
    }
}

void ExtrusionPrinter::print_leaf(const ExtrusionEntity &entity)
{
    const ArcPolyline *polyline = entity.polyline_or_null();
    if (polyline == nullptr)
        return;

    this->begin_entity();
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
}

void ExtrusionPrinter::enter_node(const ExtrusionEntity &entity)
{
    this->begin_entity();
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
    m_first_child_stack.push_back(true);
}

void ExtrusionPrinter::visit_leaf(const ExtrusionEntity &entity)
{
    this->print_leaf(entity);
}

void ExtrusionPrinter::leave_node(const ExtrusionEntity&)
{
    ss << "}";
    assert(!m_first_child_stack.empty());
    m_first_child_stack.pop_back();
}

void ExtrusionLength::visit_leaf(const ExtrusionEntity &entity)
{
    dist += entity.length();
}

double ExtrusionVolume::get(const ExtrusionEntityCollection &coll) {
    this->traverse(coll);
    return volume;
}

void ExtrusionModifyFlow::set(ExtrusionEntityCollection &coll) {
    this->traverse(coll);
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

void HasRoleVisitor::visit_leaf(const ExtrusionEntity& entity)
{
    if (found)
        return;
    const ExtrusionAttributes *attributes = entity.get_property<ExtrusionAttributes>();
    found = attributes ? this->matches(entity, attributes->role) : false;
}

bool HasRoleVisitor::search(const ExtrusionEntity &entity, HasRoleVisitor&& visitor)
{
    visitor.traverse(entity);
    return visitor.found;
}

bool HasRoleVisitor::search(const ExtrusionEntitiesPtr &entities, HasRoleVisitor&& visitor)
{
    for (ExtrusionEntity *ptr : entities) {
        visitor.traverse(*ptr);
        if (visitor.found) return true;
    }
    return visitor.found;
}

void SimplifyVisitor::simplify(ExtrusionEntity &entity, coordf_t tolerance, ArcFittingType with_fitting_arc, double fitting_arc_tolerance)
{
    ArcPolyline *polyline = entity.polyline_or_null();
    const ExtrusionAttributes *attributes = entity.get_property<ExtrusionAttributes>();
    if (polyline == nullptr || attributes == nullptr)
        return;

    if (polyline->has_z_offset()) {
        polyline->make_arc(ArcFittingType::Disabled, tolerance, fitting_arc_tolerance);
        // TODO: simplify but only for sub-path with same zheight.
        return;
    }
    if (with_fitting_arc != ArcFittingType::Disabled) {
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
    polyline->make_arc(with_fitting_arc, tolerance, fitting_arc_tolerance);
}

void SimplifyVisitor::traverse(ExtrusionEntity &entity)
{
    m_last_deleted = false;
    this->simplify_entity(entity);
}

void SimplifyVisitor::simplify_entity(ExtrusionEntity& entity) {
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
        this->simplify_entity(*child);
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
            this->simplify_entity(*child);
        }
    }
    m_current_attributes = old_current_attributes;
}

void GetPathsVisitor::visit_leaf(ExtrusionEntity& entity)
{
    if (entity.polyline_or_null() != nullptr)
        paths.push_back(&entity);
}

void ExtrusionVolume::visit_leaf(const ExtrusionEntity &entity)
{
    const ExtrusionAttributes *attributes = entity.get_property<ExtrusionAttributes>();
    if (attributes == nullptr)
        return;
    if (entity.role() == ExtrusionRole::GapFill && !_with_gap_fill)
        return;
    volume += unscaled(entity.length()) * attributes->mm3_per_mm * _flow_ratio;
}

void ExtrusionModifyFlow::visit_leaf(ExtrusionEntity &entity)
{
    ExtrusionAttributes *attributes = entity.get_property<ExtrusionAttributes>();
    if (attributes == nullptr)
        return;
    attributes->mm3_per_mm *= _flow_mult;
    attributes->width *= _flow_mult;
}

void CreateBoundingBoxVisitor::visit_leaf(const ExtrusionEntity &entity)
{
    const ArcPolyline *polyline = entity.polyline_or_null();
    if (polyline == nullptr)
        return;
    for (const Geometry::ArcWelder::Segment &pt : polyline->get_arc())
        bb.merge(pt.point);
}

void CountEntities::default_use(const ExtrusionEntity &entity)
{
    if (!entity.is_leaf()) {
        for (const ExtrusionEntityUPtr &child : entity.children())
            if (child)
                child->visit(*this);
    } else {
        ++leaf_number;
    }
}

void FlatenEntities::default_use(const ExtrusionEntity &entity) {
    if (!entity.is_collection()) {
        to_fill.append(entity);
        return;
    }

    assert(!entity.is_leaf());
    const ExtrusionEntity::Children &children = entity.children();
    if (children.size() == 1) {
        // only one element, sort or reverse are meaningless.
        children.front()->visit(*this);
    } else if ((!entity.can_sort() || !this->to_fill.can_sort()) && preserve_ordering) {
        FlatenEntities unsortable(entity, preserve_ordering);
        for (const ExtrusionEntityUPtr &child : children)
            if (child)
                child->visit(unsortable);
        to_fill.append(std::move(unsortable.to_fill));
    } else {
        for (const ExtrusionEntityUPtr &child : children)
            if (child)
                child->visit(*this);
    }
}

ExtrusionEntityCollection&& FlatenEntities::flatten(const ExtrusionEntityCollection &to_flatten) && {
    to_flatten.visit(*this);
    return std::move(to_fill);
}

#ifdef _DEBUG
void TestCollection::default_use(const ExtrusionEntity& entity)
{
    if (!entity.is_leaf()) {
        for (const ExtrusionEntityUPtr &child : entity.children()) {
            assert(child);
            std::cout << "entity at " << ((uint64_t)(void*)child.get()) << "\n";
            child->visit(*this);
        }
    } else {
        assert(entity.as_polyline().size() > 0);
    }
}
#endif

#ifdef _DEBUGINFO
void LoopAssertVisitor::enter_node(const ExtrusionEntity& entity)
{
    if (!entity.is_leaf()) {
        release_assert(!entity.empty());
        Point last_pt = entity.is_loop() ? entity.last_point() : entity.first_point();
        const ExtrusionEntity::Children &children = entity.children();
        for (const ExtrusionEntityUPtr &child : children) {
            if (!child)
                continue;
            if (entity.is_loop() || entity.is_continuous())
                release_assert(child->first_point() == last_pt);
            last_pt = child->last_point();
        }
        if (entity.is_loop())
            release_assert(entity.first_point() == entity.last_point());
        return;
    }
}

void LoopAssertVisitor::visit_leaf(const ExtrusionEntity& entity)
{
    const ArcPolyline *polyline = entity.polyline_or_null();
    const ExtrusionPropertyOverhang *overhang = this->current_property<ExtrusionPropertyOverhang>();
    const ExtrusionAttributes *attributes = this->current_property<ExtrusionAttributes>();
    const ExtrusionRole role = attributes != nullptr ? attributes->role : entity.role();
    release_assert (!role.is_overhang() || overhang != nullptr);
    if (m_check_length <= 0 || polyline == nullptr)
        return;
    release_assert(!entity.empty());
    const bool has_z_offset = polyline->has_z_offset();
    // Sawtooth support may use z-profile paths as non-extruding 3D moves.
    release_assert(attributes == nullptr || attributes->mm3_per_mm > 0.000001 || role == ExtrusionRole::Travel || has_z_offset);
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
