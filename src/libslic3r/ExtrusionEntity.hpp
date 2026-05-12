///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/ Copyright (c) 2017 Eyal Soha @eyal0
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_ExtrusionEntity_hpp_
#define slic3r_ExtrusionEntity_hpp_

#include <cassert>
#include <limits>
#include <numeric>
#include <optional>
#include <string_view>

#include "ExtrusionRole.hpp"
#include "ExtrusionProperty.hpp"
#include "libslic3r.h"
#include "Polygon.hpp"
#include "Polyline.hpp"

namespace Slic3r {

class ExPolygon;
using ExPolygons = std::vector<ExPolygon>;
class ExtrusionEntityCollection;
class Extruder;


class ExtrusionEntity;
class ExtrusionPath;
class ExtrusionPath3D;
class ExtrusionMultiPath;
class ExtrusionMultiPath3D;
class ExtrusionLoop;
class ExtrusionNop;


class ExtrusionVisitor {
public:
    virtual void default_use(ExtrusionEntity &entity) { assert(false); };
    virtual void use(ExtrusionPath &path);
    virtual void use(ExtrusionPath3D &path3D);
    virtual void use(ExtrusionMultiPath &multipath);
    virtual void use(ExtrusionMultiPath3D &multipath3D);
    virtual void use(ExtrusionLoop &loop);
    virtual void use(ExtrusionEntityCollection &collection);
    virtual void use(ExtrusionNop &nop);
};
class ExtrusionVisitorConst {
public:
    virtual void default_use(const ExtrusionEntity &entity) { assert(false); };
    virtual void use(const ExtrusionPath &path);
    virtual void use(const ExtrusionPath3D &path3D);
    virtual void use(const ExtrusionMultiPath &multipath);
    virtual void use(const ExtrusionMultiPath3D &multipath3D);
    virtual void use(const ExtrusionLoop &loop);
    virtual void use(const ExtrusionEntityCollection &collection);
    virtual void use(const ExtrusionNop &nop);
};

class ExtrusionEntity : public ExtrusionPropertyContainer
{
protected:
    static inline std::atomic_int32_t id_generator;
    uint32_t m_id; // for travel map
    // even if no_sort, allow to reverse() us (and our entities if they allow it, but they should) 
    bool m_can_reverse; //TODO: use (int64_t) m_id sign to embed this property, currently not an issue as 32+8 <= 64

    ExtrusionEntity(bool can_reverse) : m_can_reverse(can_reverse) , m_id(++id_generator) {}
    ExtrusionEntity(ExtrusionPropertyUPtr &&eprop, bool can_reverse)
        : ExtrusionPropertyContainer(std::move(eprop)), m_can_reverse(can_reverse), m_id(++id_generator) {}
    ExtrusionEntity(ExtrusionPropertyUPtrs &&eprops, bool can_reverse)
        : ExtrusionPropertyContainer(std::move(eprops)), m_can_reverse(can_reverse), m_id(++id_generator) {}
    ExtrusionEntity(const ExtrusionEntity &rhs)
        : ExtrusionPropertyContainer(rhs), m_can_reverse(rhs.m_can_reverse), m_id(rhs.m_id) {}
    ExtrusionEntity(ExtrusionEntity &&rhs)
        : ExtrusionPropertyContainer(std::move(rhs)), m_can_reverse(rhs.m_can_reverse), m_id(rhs.m_id) {}
    
    ExtrusionEntity &operator=(const ExtrusionEntity &rhs) {
        this->m_id = rhs.m_id;
        this->m_can_reverse = rhs.m_can_reverse;
        ExtrusionPropertyContainer::operator=(rhs);
        return *this;
    }
    ExtrusionEntity &operator=(ExtrusionEntity &&rhs) {
        this->m_id = rhs.m_id;
        this->m_can_reverse = rhs.m_can_reverse;
        ExtrusionPropertyContainer::operator=(std::move(rhs));
        return *this;
    }
public:
    uint64_t get_id() const { return m_id; }
    virtual ExtrusionRole role() const = 0;
    virtual bool has_role(ExtrusionRole) const = 0;
    virtual bool is_collection() const { return false; }
    virtual bool is_loop() const { return false; }
    virtual bool can_reverse() const { return m_can_reverse; }
    virtual ExtrusionEntity* clone() const = 0;
    // Create a new object, initialize it with this object using the move semantics.
    virtual ExtrusionEntity* clone_move() = 0;
    virtual ~ExtrusionEntity() = default;
    virtual void reverse() = 0;
    virtual const Point& first_point() const = 0;
    virtual const Point& last_point() const = 0;
    // Returns an approximately middle point of a path, loop or an extrusion collection.
    // Used to get a sample point of an extrusion or extrusion collection, which is possibly deep inside its island.
    virtual const Point& middle_point() const = 0;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    virtual void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const = 0;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    virtual void polygons_covered_by_spacing(Polygons &out, const float spacing_ratio, const float scaled_epsilon) const = 0;
    virtual Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    virtual Polygons polygons_covered_by_spacing(const float spacing_ratio, const float scaled_epsilon) const
        { Polygons out; this->polygons_covered_by_spacing(out, spacing_ratio, scaled_epsilon); return out; }
    virtual ArcPolyline as_polyline() const = 0;
    virtual void   collect_polylines(ArcPolylines &dst) const = 0;
    virtual void   collect_points(Points &dst) const = 0;
    virtual ArcPolylines as_polylines() const { ArcPolylines dst; this->collect_polylines(dst); return dst; }
    virtual coordf_t length() const = 0;
    virtual bool empty() const = 0;
    virtual double total_volume() const = 0;
    virtual void visit(ExtrusionVisitor &visitor) = 0;
    virtual void visit(ExtrusionVisitorConst &visitor) const = 0;
    void visit(ExtrusionVisitor &&visitor); // note: need 'using ExtrusionEntity::visit;' to be called from children classes
    void visit(ExtrusionVisitorConst &&visitor) const;

};

// only cary an ExtrusionProperty
class ExtrusionNop : public ExtrusionEntity
{
    ExtrusionRole m_role = ExtrusionRole::None;
public:
    static Point NOT_A_POINT;
    // this can have a position, to move the head.
    Point position = NOT_A_POINT;
    ExtrusionNop() : ExtrusionEntity(true) {}
    ExtrusionNop(const ExtrusionNop& other) : ExtrusionEntity(other), m_role(other.m_role), position(other.position) {}
    ExtrusionNop(ExtrusionNop&& other) : ExtrusionEntity(std::move(other)), m_role(other.m_role), position(std::move(other.position)) {}
    ExtrusionNop(const ExtrusionProperty &attr) : ExtrusionEntity(true) { this->add_property(attr); }
    ExtrusionNop &operator=(const ExtrusionNop &rhs) {
        ExtrusionEntity::operator=(rhs);
        this->m_role = rhs.m_role;
        this->position = rhs.position;
        return *this;
    }
    ExtrusionNop &operator=(ExtrusionNop &rhs) {
        ExtrusionEntity::operator=(rhs);
        this->m_role = rhs.m_role;
        this->position = rhs.position;
        return *this;
    }
    ExtrusionRole role() const override { return m_role; }
    void set_role(ExtrusionRole new_role) { m_role = new_role; }
    bool has_role(ExtrusionRole test_role) const override { return (m_role & test_role) == test_role; }
    ExtrusionEntity *clone() const override { return new ExtrusionNop(*this); }
    // Create a new object, initialize it with this object using the move semantics.
    virtual ExtrusionEntity* clone_move() { return new ExtrusionNop(std::move(*this)); }
    void reverse() override {}
    const Point &first_point() const override { return position; }
    const Point& last_point() const override { return position; }
    const Point& middle_point() const override { return position; }
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const override {}
    void polygons_covered_by_spacing(Polygons &out, const float spacing_ratio, const float scaled_epsilon) const override {}
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const override { return {}; }
    Polygons polygons_covered_by_spacing(const float spacing_ratio, const float scaled_epsilon) const override { return {}; }
    ArcPolyline as_polyline() const override { return {}; }
    void collect_polylines(ArcPolylines &dst) const override {}
    void collect_points(Points &dst) const override {}
    coordf_t length() const override { return 0; }
    bool empty() const override { return true; }
    double total_volume() const override { return 0; }
    void visit(ExtrusionVisitor &visitor) override { visitor.use(*this); }
    void visit(ExtrusionVisitorConst &visitor) const override { visitor.use(*this); }
};

//FIXME: this is unsafe. it's a collection of row pointer that isn't ours
using ExtrusionEntitiesPtr = std::vector<ExtrusionEntity*>;

//FIXME: this is unsafe. it contains a raw pointer that isn't ours
// Const reference for ordering extrusion entities without having to modify them.
class ExtrusionEntityReference final
{
public:
    ExtrusionEntityReference() = delete;
    ExtrusionEntityReference(const ExtrusionEntity &extrusion_entity, bool flipped) : 
        m_extrusion_entity(&extrusion_entity), m_flipped(flipped) {}
    ExtrusionEntityReference operator=(const ExtrusionEntityReference &rhs) 
        { m_extrusion_entity = rhs.m_extrusion_entity; m_flipped = rhs.m_flipped; return *this; }

    const ExtrusionEntity& extrusion_entity() const { return *m_extrusion_entity; }
    template<typename Type>
    const Type*            cast()             const { return dynamic_cast<const Type*>(m_extrusion_entity); }
    bool                   flipped()          const { return m_flipped; }

private:
    const ExtrusionEntity *m_extrusion_entity;
    bool                   m_flipped;
};

//FIXME: this is still unsafe. it's a collection of unsafe container.
using ExtrusionEntityReferences = std::vector<ExtrusionEntityReference>;

class ExtrusionPath : public ExtrusionEntity
{
public:
    ArcPolyline polyline; //TODO: protected
    // force to set the ExtrusionProperty (to nullptr) to be sure you didn't forget it
    //ExtrusionPath(ExtrusionRole role) : ExtrusionEntity(true), m_attributes{role} {}
    //ExtrusionPath(const ExtrusionAttributes &attributes, bool can_reverse = true) : ExtrusionEntity(can_reverse), m_attributes(attributes) {}
    ExtrusionPath(const ExtrusionAttributes &attributes,
                  ExtrusionPropertyUPtr &&eprop,
                  bool can_reverse = true)
        : ExtrusionEntity(can_reverse) { this->set_attributes(attributes); if (eprop) this->add_property(std::move(eprop)); }
    ExtrusionPath(const ExtrusionAttributes &attributes,
                  ExtrusionPropertyUPtrs &&eprops,
                  bool can_reverse = true)
        : ExtrusionEntity(can_reverse) { this->set_attributes(attributes); for (ExtrusionPropertyUPtr &property : eprops) this->add_property(std::move(property)); }
    ExtrusionPath(const ExtrusionAttributes &attributes,
                  bool can_reverse = true)
        : ExtrusionEntity(can_reverse) { this->set_attributes(attributes); }
    ExtrusionPath(const ExtrusionPath &rhs) : ExtrusionEntity(rhs), polyline(rhs.polyline) {}
    ExtrusionPath(ExtrusionPath &&rhs) : ExtrusionEntity(rhs), polyline(std::move(rhs.polyline)) {}
    //ExtrusionPath(const ArcPolyline &polyline, const ExtrusionAttributes &attribs, bool can_reverse = true)
        //: ExtrusionEntity(can_reverse), polyline(polyline), m_attributes(attribs) {}
    //ExtrusionPath(ArcPolyline &&polyline, const ExtrusionAttributes &attribs, bool can_reverse = true)
        //: ExtrusionEntity(can_reverse), polyline(std::move(polyline)), m_attributes(attribs) {}
    ExtrusionPath(const ArcPolyline &polyline,
                  const ExtrusionAttributes &attribs,
                  ExtrusionPropertyUPtr &&eprop,
                  bool can_reverse = true)
        : ExtrusionEntity(can_reverse), polyline(polyline) { this->set_attributes(attribs); if (eprop) this->add_property(std::move(eprop)); }
    ExtrusionPath(const ArcPolyline &polyline,
                  const ExtrusionAttributes &attribs,
                  ExtrusionPropertyUPtrs &&eprops,
                  bool can_reverse = true)
        : ExtrusionEntity(can_reverse), polyline(polyline) { this->set_attributes(attribs); for (ExtrusionPropertyUPtr &property : eprops) this->add_property(std::move(property)); }
    ExtrusionPath(ArcPolyline &&polyline,
                  const ExtrusionAttributes &attribs,
                  ExtrusionPropertyUPtr &&eprop,
                  bool can_reverse = true)
        : ExtrusionEntity(can_reverse)
        , polyline(std::move(polyline)) { this->set_attributes(attribs); if (eprop) this->add_property(std::move(eprop)); }
    ExtrusionPath(ArcPolyline &&polyline,
                  const ExtrusionAttributes &attribs,
                  ExtrusionPropertyUPtrs &&eprops,
                  bool can_reverse = true)
        : ExtrusionEntity(can_reverse)
        , polyline(std::move(polyline)) { this->set_attributes(attribs); for (ExtrusionPropertyUPtr &property : eprops) this->add_property(std::move(property)); }

    ExtrusionPath &operator=(const ExtrusionPath &rhs) {
        ExtrusionEntity::operator=(rhs);
        this->polyline = rhs.polyline;
        return *this;
    }
    ExtrusionPath &operator=(ExtrusionPath &&rhs) {
        ExtrusionEntity::operator=(rhs);
        this->polyline = std::move(rhs.polyline);
        return *this;
    }

	ExtrusionEntity* clone() const override { return new ExtrusionPath(*this); }
    // Create a new object, initialize it with this object using the move semantics.
    virtual ExtrusionPath* clone_move() override { return new ExtrusionPath(std::move(*this)); }
    void reverse() override { this->polyline.reverse(); }
    void set_can_reverse(bool can_reverse) { this->m_can_reverse = can_reverse; }
    const Point& first_point() const override { return this->polyline.front(); }
    const Point& last_point() const override { return this->polyline.back(); }
    // Is it really what you can call a middle point?: yes, it's more random than middle.
    const Point &middle_point() const override { return this->polyline.middle(); }
    size_t size() const { return this->polyline.size(); }
    bool empty() const { return this->polyline.empty(); }
    bool is_closed() const { return ! this->empty() && this->polyline.front() == this->polyline.back(); }
    // Produce a list of extrusion paths into retval by clipping this path by ExPolygons.
    // Currently not used.
    void intersect_expolygons(const ExPolygons &collection, ExtrusionEntityCollection* retval) const;
    // Produce a list of extrusion paths into retval by removing parts of this path by ExPolygons.
    // Currently not used.
    void subtract_expolygons(const ExPolygons &collection, ExtrusionEntityCollection* retval) const;
    void clip_end(coordf_t distance);
    virtual void simplify(coordf_t tolerance, ArcFittingType with_fitting_arc, double fitting_arc_tolerance);
    coordf_t length() const override;

    const ExtrusionAttributes&  attributes() const { const ExtrusionAttributes *attributes = this->get_property<ExtrusionAttributes>(); assert(attributes != nullptr); return *attributes; }
    ExtrusionRole               role() const override { return attributes().role; }
    bool has_role(ExtrusionRole test_role) const override { return (attributes().role & test_role) == test_role; }
    float                       width() const { return attributes().width; }
    float                       height() const { return attributes().height; }
    double                      mm3_per_mm() const { return attributes().mm3_per_mm; }
    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    double                      min_mm3_per_mm() const { return attributes().mm3_per_mm; }
    ExtrusionPropertyOverhang &overhang_attributes_mutable();
    const ExtrusionPropertyOverhang *overhang_attributes() const; // can be null if not present
    ExtrusionAttributes& attributes_mutable() { return this->get_or_add_property<ExtrusionAttributes>(); }

    void set_role(ExtrusionRole new_role) { attributes_mutable().role = new_role; }
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const override;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void polygons_covered_by_spacing(Polygons &out, const float spacing_ratio, const float scaled_epsilon) const override;
    virtual Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    virtual Polygons polygons_covered_by_spacing(const float spacing_ratio, const float scaled_epsilon) const
        { Polygons out; this->polygons_covered_by_spacing(out, spacing_ratio, scaled_epsilon); return out; }
    ArcPolyline as_polyline() const override { return this->polyline; }
    void          collect_polylines(ArcPolylines &dst) const override { if (! this->polyline.empty()) dst.emplace_back(this->polyline); }
    void          collect_points(Points &dst) const override { append(dst, this->polyline.to_polyline().points); }
    double      total_volume() const override { return attributes().mm3_per_mm * unscaled(length()); }
    using ExtrusionEntity::visit;
    virtual void visit(ExtrusionVisitor &visitor) override { visitor.use(*this); };
    virtual void visit(ExtrusionVisitorConst &visitor) const override { visitor.use(*this); };

protected:
    void _inflate_collection(const Polylines &polylines, ExtrusionEntityCollection* collection) const;

    void set_attributes(const ExtrusionAttributes &attributes) { this->add_property(attributes); }
};
/* just set the path to can_reverse = false
class ExtrusionPathOriented : public ExtrusionPath
{
public:
    ExtrusionPathOriented(const ExtrusionAttributes &attribs) : ExtrusionPath(attribs) {}
    ExtrusionPathOriented(const Polyline &polyline, const ExtrusionAttributes &attribs) : ExtrusionPath(polyline, attribs) {}
    ExtrusionPathOriented(Polyline &&polyline, const ExtrusionAttributes &attribs) : ExtrusionPath(std::move(polyline), attribs) {}

    ExtrusionEntity* clone() const override { return new ExtrusionPathOriented(*this); }
    // Create a new object, initialize it with this object using the move semantics.
    ExtrusionEntity* clone_move() override { return new ExtrusionPathOriented(std::move(*this)); }
    virtual bool can_reverse() const override { return false; }
};
*/

typedef std::vector<ExtrusionPath> ExtrusionPaths;
ExtrusionPaths clip_end(ExtrusionPaths& paths, coordf_t distance);

//TODO remove, replaced by ExtrusionPropertyZOffset
class ExtrusionPath3D : public ExtrusionPath {
protected:
    void init() {
#ifdef _DEBUG
        polyline.is_3D = true;
#endif
    }
public:
    std::vector<coord_t> z_offsets;

    //ExtrusionPath3D(ExtrusionRole role) : ExtrusionPath(role) { /*std::cout << "new path3D\n"; */};
    ExtrusionPath3D(const ExtrusionAttributes &attributes,
                    ExtrusionPropertyUPtr &&eprop,
                    bool can_reverse)
        : ExtrusionPath(attributes, std::move(eprop), can_reverse) {
        init();
    };
    ExtrusionPath3D(const ExtrusionAttributes &attributes,
                    ExtrusionPropertyUPtrs &&eprops,
                    bool can_reverse)
        : ExtrusionPath(attributes, std::move(eprops), can_reverse) {
        init();
    };
    ExtrusionPath3D(const ExtrusionPath &rhs) : ExtrusionPath(rhs) { init();  }
    ExtrusionPath3D(ExtrusionPath &&rhs) : ExtrusionPath(rhs) { init();  }
    ExtrusionPath3D(const ExtrusionPath3D &rhs) : ExtrusionPath(rhs), z_offsets(rhs.z_offsets) { init();  }
    ExtrusionPath3D(ExtrusionPath3D &&rhs) : ExtrusionPath(rhs), z_offsets(std::move(rhs.z_offsets)) { init();  }


    ExtrusionPath3D &operator=(const ExtrusionPath3D &rhs)
    {
        ExtrusionPath::operator=(rhs);
        z_offsets = rhs.z_offsets;
        return *this;
    }
    ExtrusionPath3D &operator=(ExtrusionPath3D &&rhs)
    {
        ExtrusionPath::operator=(rhs);
        z_offsets = std::move(rhs.z_offsets);
        return *this;
    }
    virtual ExtrusionPath3D* clone() const override { return new ExtrusionPath3D(*this); }
    virtual ExtrusionPath3D* clone_move() override { return new ExtrusionPath3D(std::move(*this)); }
    using ExtrusionEntity::visit;
    virtual void visit(ExtrusionVisitor &visitor) override { visitor.use(*this); };
    virtual void visit(ExtrusionVisitorConst &visitor) const override { visitor.use(*this); };

    void push_back(Point p, coord_t z_offset) { 
        assert(!polyline.has_arc());
        polyline.append(p);
        z_offsets.push_back(z_offset);
    }
    void reverse() override {
        this->polyline.reverse();
        std::reverse(this->z_offsets.begin(), this->z_offsets.end());
    }

    //TODO: simplify only for points that have the same z-offset
    void simplify(double tolerance, ArcFittingType use_arc_fitting, double fitting_arc_tolerance) override;
};
typedef std::vector<ExtrusionPath3D> ExtrusionPaths3D;

// Single continuous extrusion path, possibly with varying extrusion thickness, extrusion height or bridging / non bridging.
// it's like an unsortable collection of only unreversable THING
// note: the ExtrusionProperty of a multipath is applied to each path, properties of a path is not transfered to the next one.
template <typename THING = ExtrusionEntity>
class ExtrusionMultiEntity : public ExtrusionEntity {
public:
    std::vector<THING> paths;

    ExtrusionMultiEntity(): ExtrusionEntity(false) {};
    ExtrusionMultiEntity(const ExtrusionMultiEntity &rhs) : paths(rhs.paths), ExtrusionEntity(rhs) {}
    ExtrusionMultiEntity(ExtrusionMultiEntity &&rhs) : paths(std::move(rhs.paths)), ExtrusionEntity(rhs) {}
    ExtrusionMultiEntity(const std::vector<THING> &paths) : paths(paths), ExtrusionEntity(false) {};
    ExtrusionMultiEntity(const THING &path): ExtrusionEntity(false) { this->paths.push_back(path); }

    ExtrusionMultiEntity &operator=(const ExtrusionMultiEntity &rhs) {
        ExtrusionEntity::operator=(rhs);
        this->paths = rhs.paths;
        return *this;
    }
    ExtrusionMultiEntity &operator=(ExtrusionMultiEntity &&rhs) {
        ExtrusionEntity::operator=(rhs);
        this->paths = std::move(rhs.paths);
        return *this;
    }

    bool is_loop() const override { return false; }
    virtual const Point& first_point() const override { return this->paths.front().polyline.front(); }
    virtual const Point& last_point() const override { return this->paths.back().polyline.back(); }

    virtual void reverse() override {
        for (THING &entity : this->paths)
            entity.reverse();
        std::reverse(this->paths.begin(), this->paths.end());
    }
    ExtrusionRole role() const override
    {
        if (this->paths.empty())
            return ExtrusionRole::None;
        ExtrusionRole role = this->paths.front().role();
        for (const ExtrusionPath &path : this->paths)
            if (role != path.role()) {
                return ExtrusionRole::Mixed;
            }
        return role;
    }
    bool has_role(ExtrusionRole test_role) const override {
        if (this->paths.empty())
            return false;
        for (const ExtrusionPath &path : this->paths)
            if (path.has_role(test_role)) {
                return true;
            }
        return false;
    }


    // Is it really what you can call a middle point?:
    const Point& middle_point() const override { auto &path = this->paths[this->paths.size() / 2]; return path.polyline.middle(); }
    size_t size() const { return this->paths.size(); }
    coordf_t length() const override {
        coordf_t len = 0;
        for (const THING &entity : this->paths)
            len += entity.length();
        return len;
    }
    bool empty() const override {
        for (const THING &entity : this->paths)
            if (!entity.empty())
                return false;
        return true;
    }

    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const override {
        for (const THING &entity : this->paths)
            entity.polygons_covered_by_width(out, scaled_epsilon);
    }

    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void polygons_covered_by_spacing(Polygons &out, const float spacing_ratio, const float scaled_epsilon) const override {
        for (const THING &entity : this->paths)
            entity.polygons_covered_by_spacing(out, spacing_ratio, scaled_epsilon);
    }

    ArcPolyline as_polyline() const override {
        ArcPolyline out;
        if (!paths.empty()) {
            out = paths.front().as_polyline();
            for (size_t i = 1; i < paths.size(); ++i) {
                out.append(paths[i].as_polyline());
            }
        }
        return out;
    }
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const override{ Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float spacing_ratio, const float scaled_epsilon) const override { Polygons out; this->polygons_covered_by_spacing(out, spacing_ratio,  scaled_epsilon); return out; }
    void collect_polylines(ArcPolylines &dst) const override { ArcPolyline pl = this->as_polyline(); if (!pl.empty()) dst.emplace_back(std::move(pl)); }
    void collect_points(Points &dst) const override { 
        size_t n = std::accumulate(paths.begin(), paths.end(), 0, [](const size_t n, const ExtrusionPath &p){ return n + p.polyline.size(); });
        dst.reserve(dst.size() + n);
        for (const ExtrusionPath &p : this->paths)
            append(dst, p.polyline.to_polyline().points);
    }
    double total_volume() const override { double volume = 0.; for (const auto& path : paths) volume += path.total_volume(); return volume; }
};

// Single continuous extrusion path, possibly with varying extrusion thickness, extrusion height or bridging / non bridging.
// it's like an unsortable collection of only unreversable ExtrusionPaths
class ExtrusionMultiPath : public ExtrusionMultiEntity<ExtrusionPath> {
public:

    ExtrusionMultiPath() {};
    ExtrusionMultiPath(const ExtrusionMultiPath &rhs) : ExtrusionMultiEntity(rhs) {}
    ExtrusionMultiPath(ExtrusionMultiPath &&rhs) : ExtrusionMultiEntity(rhs) {}
    ExtrusionMultiPath(const ExtrusionPaths &paths) : ExtrusionMultiEntity(paths) {};
    ExtrusionMultiPath(const ExtrusionPath &path) :ExtrusionMultiEntity(path) {}

    ExtrusionMultiPath &operator=(const ExtrusionMultiPath &rhs) {
        ExtrusionEntity::operator=(rhs);
        this->paths = rhs.paths;
        return *this;
    }
    ExtrusionMultiPath &operator=(ExtrusionMultiPath &&rhs) {
        ExtrusionEntity::operator=(rhs);
        this->paths = std::move(rhs.paths);
        return *this;
    }

    void set_can_reverse(bool can_reverse) { m_can_reverse = can_reverse; }

    virtual ExtrusionMultiPath* clone() const override { return new ExtrusionMultiPath(*this); }
    virtual ExtrusionMultiPath* clone_move() override { return new ExtrusionMultiPath(std::move(*this)); }

    using ExtrusionEntity::visit;
    virtual void visit(ExtrusionVisitor &visitor) override { visitor.use(*this); };
    virtual void visit(ExtrusionVisitorConst &visitor) const override { visitor.use(*this); };
};
// Single continuous extrusion path, possibly with varying extrusion thickness, extrusion height or bridging / non bridging.
class ExtrusionMultiPath3D : public ExtrusionMultiEntity<ExtrusionPath3D> {
public:

    ExtrusionMultiPath3D() {};
    ExtrusionMultiPath3D(const ExtrusionMultiPath3D &rhs) : ExtrusionMultiEntity(rhs) {}
    ExtrusionMultiPath3D(ExtrusionMultiPath3D &&rhs) : ExtrusionMultiEntity(rhs) {}
    ExtrusionMultiPath3D(const ExtrusionPaths3D &paths) : ExtrusionMultiEntity(paths) {};
    ExtrusionMultiPath3D(const ExtrusionPath3D &path) :ExtrusionMultiEntity(path) {}

    ExtrusionMultiPath3D &operator=(const ExtrusionMultiPath3D &rhs) {
        ExtrusionEntity::operator=(rhs);
        this->paths = rhs.paths;
        return *this;
    }
    ExtrusionMultiPath3D &operator=(ExtrusionMultiPath3D &&rhs) {
        ExtrusionEntity::operator=(rhs);
        this->paths = std::move(rhs.paths);
        return *this;
    }

    virtual ExtrusionMultiPath3D* clone() const override { return new ExtrusionMultiPath3D(*this); }
    virtual ExtrusionMultiPath3D* clone_move() override { return new ExtrusionMultiPath3D(std::move(*this)); }

    using ExtrusionEntity::visit;
    virtual void visit(ExtrusionVisitor &visitor) override { visitor.use(*this); };
    virtual void visit(ExtrusionVisitorConst &visitor) const override { visitor.use(*this); };

    virtual void reverse() override {
        std::cout << "I SAID NO REVERSE!!!FFFS\n";
    }
};

// Single continuous extrusion loop, possibly with varying extrusion thickness, extrusion height or bridging / non bridging.
// note: the ExtrusionProperty of a multipath is applied to each path, properties of a path is not transfered to the next one.
class ExtrusionLoop : public ExtrusionEntity
{
public:
    ExtrusionPaths paths;
    
    //ExtrusionLoop(const ExtrusionLoop& rhs) : ExtrusionEntity(rhs), paths(rhs.paths), m_loop_role(rhs.m_loop_role) {}
    ExtrusionLoop(ExtrusionLoopRole role = elrDefault) : ExtrusionEntity(false) { this->set_loop_role(role); }
    ExtrusionLoop(const ExtrusionPaths &paths, ExtrusionLoopRole role = elrDefault) : paths(paths), ExtrusionEntity(false) {
        this->set_loop_role(role);
        assert(!this->paths.empty());
        assert(this->first_point().coincides_with_epsilon(this->paths.back().polyline.back()));
    }
    ExtrusionLoop(ExtrusionPaths &&paths, ExtrusionLoopRole role = elrDefault) : paths(std::move(paths)), ExtrusionEntity(false) {
        this->set_loop_role(role);
        assert(!this->paths.empty());
        assert(this->first_point().coincides_with_epsilon(this->paths.back().polyline.back()));
    }
    ExtrusionLoop(const ExtrusionPath &path, ExtrusionLoopRole role = elrDefault) : ExtrusionEntity(false) {
        this->set_loop_role(role);
        this->paths.push_back(path);
        assert(!this->paths.empty());
        assert(this->first_point().coincides_with_epsilon(this->paths.back().polyline.back()));
    }
    ExtrusionLoop(ExtrusionPath &&path, ExtrusionLoopRole role = elrDefault) : ExtrusionEntity(false) {
        this->set_loop_role(role);
        this->paths.emplace_back(std::move(path));
        assert(!this->paths.empty());
        assert(this->first_point().coincides_with_epsilon(this->paths.back().polyline.back()));
    }
    virtual bool is_loop() const override{ return true; }
    virtual ExtrusionEntity* clone() const override{ return new ExtrusionLoop (*this); }
    // Create a new object, initialize it with this object using the move semantics.
    virtual ExtrusionEntity* clone_move() override { return new ExtrusionLoop(std::move(*this)); }
    double          area() const;
    bool            is_counter_clockwise() const;
    bool            is_clockwise() const;
    // Used by PerimeterGenerator to reorient extrusion loops. (old make_clockwise() and make_counter_clockwise())
    void            reverse() override;
    const Point&    first_point() const override { return this->paths.front().polyline.front(); }
    const Point&    last_point() const override { assert(this->first_point() == this->paths.back().polyline.back()); return this->first_point(); }
    // Is it really what you can call a middle point?: 
    const Point&    middle_point() const override { auto& path = this->paths[this->paths.size() / 2]; return path.polyline.middle(); }
    Polygon polygon() const;
    coordf_t length() const override;
    bool empty() const override {
        for (const ExtrusionPath &path : paths)
            if (!path.empty())
                return false;
        return true;
    }
    bool split_at_vertex(const Point &point, const coordf_t scaled_epsilon = scale_d(0.001));
    void split_at(const Point &point, bool prefer_non_overhang, const coordf_t scaled_epsilon = scale_d(0.001));
    struct ClosestPathPoint {
        size_t path_idx;
        size_t segment_idx;
        Point  foot_pt;
    };
    ClosestPathPoint get_closest_path_and_point(const Point& point, bool prefer_non_overhang) const;
    // Test, whether the point is extruded by a bridging flow.
    // This used to be used to avoid placing seams on overhangs, but now the EdgeGrid is used instead.
    //bool has_overhang_point(const Point &point) const;
    ExtrusionRole role() const override;
    bool has_role(ExtrusionRole test_role) const override;
    ExtrusionLoopRole loop_role() const
    {
        const ExtrusionPropertyLoopRole *property = this->get_property<ExtrusionPropertyLoopRole>();
        return property == nullptr ? elrDefault : property->loop_role;
    }
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const override;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void polygons_covered_by_spacing(Polygons &out, const float spacing_ratio, const float scaled_epsilon) const  override;
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float spacing_ratio, const float scaled_epsilon) const
        { Polygons out; this->polygons_covered_by_spacing(out, spacing_ratio, scaled_epsilon); return out; }
    ArcPolyline as_polyline() const override;
    void   collect_polylines(ArcPolylines &dst) const override { ArcPolyline pl = this->as_polyline(); if (! pl.empty()) dst.emplace_back(std::move(pl)); }
    void   collect_points(Points &dst) const override { 
        size_t n = std::accumulate(paths.begin(), paths.end(), 0, [](const size_t n, const ExtrusionPath &p){ return n + p.polyline.size(); });
        dst.reserve(dst.size() + n);
        for (const ExtrusionPath &p : this->paths)
            append(dst, p.as_polyline().to_polyline().points);
    }
    double total_volume() const override { double volume =0.; for (const auto& path : paths) volume += path.total_volume(); return volume; }

    using ExtrusionEntity::visit;
    virtual void visit(ExtrusionVisitor &visitor) override { visitor.use(*this); };
    virtual void visit(ExtrusionVisitorConst &visitor) const override { visitor.use(*this); };

#ifndef NDEBUG
	bool validate() const {
		assert(this->first_point() == this->paths.back().polyline.back());
		for (size_t i = 1; i < paths.size(); ++ i)
			assert(this->paths[i - 1].polyline.back() == this->paths[i].polyline.front());
		return true;
	}
#endif /* NDEBUG */

private:
    void set_loop_role(ExtrusionLoopRole role)
    {
        if (role == elrDefault)
            this->remove_property<ExtrusionPropertyLoopRole>();
        else
            this->get_or_add_property<ExtrusionPropertyLoopRole>().loop_role = role;
    }
};

inline void extrusion_paths_append(ExtrusionPaths &dst, Polylines &polylines, const ExtrusionAttributes &attributes, bool can_reverse = true)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines) {
        assert(polyline.is_valid());
        if (polyline.is_valid())
            dst.emplace_back(polyline, attributes, nullptr, can_reverse);
    }
}

inline void extrusion_paths_append(ExtrusionPaths &dst, Polylines &&polylines, const ExtrusionAttributes &attributes, bool can_reverse = true)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines) {
        assert(polyline.is_valid());
        if (polyline.is_valid())
            dst.emplace_back(std::move(polyline), attributes, nullptr, can_reverse);
    }
    polylines.clear();
}
inline void extrusion_paths_append(ExtrusionPaths &dst,
                                   Polylines &polylines,
                                   const ExtrusionAttributes &attributes,
                                   const ExtrusionPropertyOverhang &overhangs_attr,
                                   bool can_reverse = true) {
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines) {
        assert(polyline.is_valid());
        if (polyline.is_valid())
            dst.emplace_back(polyline, attributes, overhangs_attr.clone(), can_reverse);
    }
}

inline void extrusion_paths_append(ExtrusionPaths &dst,
                                   Polylines &&polylines,
                                   const ExtrusionAttributes &attributes,
                                   const ExtrusionPropertyOverhang &overhangs_attr,
                                   bool can_reverse = true) {
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines) {
        assert(polyline.is_valid());
        if (polyline.is_valid())
            dst.emplace_back(std::move(polyline), attributes, overhangs_attr.clone(), can_reverse);
    }
    polylines.clear();
}

class ExtrusionPrinter : public ExtrusionVisitorConst {
    std::stringstream ss;
    double mult;
    int trunc;
    bool json;
public:
    ExtrusionPrinter(double mult = 0.000001, int trunc = 0, bool json = false) : mult(mult), trunc(trunc), json(json) { }
    virtual void use(const ExtrusionPath& path) override;
    virtual void use(const ExtrusionPath3D& path3D) override;
    virtual void use(const ExtrusionMultiPath& multipath) override;
    virtual void use(const ExtrusionMultiPath3D& multipath) override;
    virtual void use(const ExtrusionLoop& loop) override;
    virtual void use(const ExtrusionEntityCollection& collection) override;
    std::string str() { return ss.str(); }
    std::string print(const ExtrusionEntity& entity)&& {
        entity.visit(*this);
        return ss.str();
    }
};

class ExtrusionLength : public ExtrusionVisitorConst {
    coordf_t dist;
public:
    ExtrusionLength() : dist(0){ }
    virtual void default_use(const ExtrusionEntity& path) override;
    virtual void use(const ExtrusionEntityCollection& collection) override;
    double get() { return dist; }
    double length(const ExtrusionEntity& entity)&& {
        entity.visit(*this);
        return get();
    }
};

class ExtrusionVisitorRecursiveConst : public ExtrusionVisitorConst {
public:
    virtual void use(const ExtrusionMultiPath& multipath) override;
    virtual void use(const ExtrusionMultiPath3D& multipath) override;
    virtual void use(const ExtrusionLoop& loop) override;
    virtual void use(const ExtrusionEntityCollection& collection) override;
};

class ExtrusionVisitorRecursive : public ExtrusionVisitor {
public:
    virtual void use(ExtrusionMultiPath& multipath) override;
    virtual void use(ExtrusionMultiPath3D& multipath) override;
    virtual void use(ExtrusionLoop& loop) override;
    virtual void use(ExtrusionEntityCollection& collection) override;
};

class HasRoleVisitor : public ExtrusionVisitorConst{
public:
    bool found = false;
    void use(const ExtrusionMultiPath& multipath) override;
    void use(const ExtrusionMultiPath3D& multipath3D) override;
    void use(const ExtrusionLoop& loop) override;
    void use(const ExtrusionEntityCollection& collection) override;
    static bool search(const ExtrusionEntity &entity, HasRoleVisitor&& visitor);
    static bool search(const ExtrusionEntitiesPtr &entities, HasRoleVisitor&& visitor);
};
struct HasInfillVisitor : public HasRoleVisitor{
    void default_use(const ExtrusionEntity &entity) override { found = entity.role().is_infill(); };
};
struct HasSolidInfillVisitor : public HasRoleVisitor{
    void default_use(const ExtrusionEntity &entity) override { found = entity.role().is_solid_infill(); };
};
struct HasThisRoleVisitor : public HasRoleVisitor{
    ExtrusionRole role_to_find;
    HasThisRoleVisitor(ExtrusionRole role) : role_to_find(role) {}
    void default_use(const ExtrusionEntity &entity) override { found = entity.role() == role_to_find; };
};


//call simplify for all paths.
class ConfigOptionFloatOrPercent;
class SimplifyVisitor : public ExtrusionVisitor{
    ArcFittingType                    m_use_arc_fitting;
    bool                              m_ignore_holes;
    coordf_t                          m_scaled_resolution;
    const ConfigOptionFloatOrPercent* m_arc_fitting_tolearance;
    // when an entity is too small, this is set to true do the collection that is higher in the stack can merge & delete.
    coord_t                           m_min_path_size = 0;
    bool                              m_last_deleted = false;
public:
    using ExtrusionVisitor::use;
    SimplifyVisitor(coordf_t scaled_resolution, ArcFittingType use_arc_fitting, bool ignore_holes, const ConfigOptionFloatOrPercent *arc_fitting_tolearance)
        : m_scaled_resolution(scaled_resolution), m_ignore_holes(ignore_holes), m_use_arc_fitting(use_arc_fitting), m_arc_fitting_tolearance(arc_fitting_tolearance)
    {}
    SimplifyVisitor(coordf_t scaled_resolution, ArcFittingType use_arc_fitting, bool ignore_holes, const ConfigOptionFloatOrPercent *arc_fitting_tolearance, coord_t min_path_size)
        : m_scaled_resolution(scaled_resolution), m_ignore_holes(ignore_holes), m_use_arc_fitting(use_arc_fitting), m_arc_fitting_tolearance(arc_fitting_tolearance), m_min_path_size(min_path_size)
    {}
    
    virtual void use(ExtrusionPath& path) override;
    virtual void use(ExtrusionMultiPath& path) override;
    virtual void use(ExtrusionPath3D& path3D) override;
    virtual void use(ExtrusionMultiPath3D& path) override;
    virtual void use(ExtrusionLoop& loop) override;
    virtual void use(ExtrusionEntityCollection& coll) override;
    void start(ExtrusionEntityCollection &coll) {
        m_last_deleted = false;
        use(coll);
    }
    bool is_valid() { return !m_last_deleted; }
};
class GetPathsVisitor : public ExtrusionVisitorRecursive {
public:
    using ExtrusionVisitorRecursive::use;
    std::vector<ExtrusionPath*> paths;
    std::vector<ExtrusionPath3D*> paths3D;
    virtual void use(ExtrusionPath& path) override {
        paths.push_back(&path);
    }
    virtual void use(ExtrusionPath3D& path3D) override {
        paths3D.push_back(&path3D);
    }
};

class ExtrusionVolume : public ExtrusionVisitorRecursiveConst {
    bool _with_gap_fill = true;
    double _flow_ratio = 1.;
public:
    using ExtrusionVisitorRecursiveConst::use;
    double volume = 0; //unscaled
    ExtrusionVolume() {}
    void set_use_gap_fill(bool with_gap_fill = true) { _with_gap_fill = (with_gap_fill); }
    void set_flow_mult(double mult) { _flow_ratio = (mult); }
    void use(const ExtrusionPath &path) override {
        if(path.role() == ExtrusionRole::GapFill && !_with_gap_fill) return;
        volume += unscaled(path.length()) * path.mm3_per_mm() * _flow_ratio;
    }
    void use(const ExtrusionPath3D &path3D) override { volume += unscaled(path3D.length()) * path3D.mm3_per_mm(); }
    double get(const ExtrusionEntityCollection &coll);
};

class ExtrusionModifyFlow : public ExtrusionVisitorRecursive {
    double _flow_mult = 1.;
public:
    using ExtrusionVisitorRecursive::use;
    ExtrusionModifyFlow(double flow_mult) : _flow_mult(flow_mult) {}
    void use(ExtrusionPath &path) override {
        path.attributes_mutable().mm3_per_mm *= _flow_mult;
        path.attributes_mutable().width *= _flow_mult;
    }
    void use(ExtrusionPath3D &path3D) override {
        path3D.attributes_mutable().mm3_per_mm *= _flow_mult;
        path3D.attributes_mutable().width *= _flow_mult;
    }
    void set(ExtrusionEntityCollection &coll);
};


class CreateBoundingBoxVisitor : public ExtrusionVisitorRecursive {
    BoundingBox bb;
public:
    using ExtrusionVisitorRecursive::use;
    CreateBoundingBoxVisitor() {}
    void use(ExtrusionPath &path) override {
        for (const Geometry::ArcWelder::Segment & pt: path.polyline.get_arc()) {
            bb.merge(pt.point);
        }
    }
    void use(ExtrusionPath3D &path3D) override {
        for (const Geometry::ArcWelder::Segment & pt: path3D.polyline.get_arc()) {
            bb.merge(pt.point);
        }
    }
    static inline BoundingBox create(ExtrusionEntity &ee) {
        CreateBoundingBoxVisitor visitor;
        ee.visit(visitor);
        return visitor.bb;
    }
};

#ifdef _DEBUGINFO
struct LoopAssertVisitor : public ExtrusionVisitorRecursiveConst {
    using ExtrusionVisitorRecursiveConst::use;
    coord_t m_check_length;
    LoopAssertVisitor() : m_check_length(SCALED_EPSILON) {}
    LoopAssertVisitor(coord_t check_length) : m_check_length(check_length) {}
    virtual void default_use(const ExtrusionEntity& entity) override {};
    virtual void use(const ExtrusionPath &path) override;
    virtual void use(const ExtrusionLoop& loop) override;
};
#define DEBUGINFO_VISIT(ENTITY,VISITOR) (ENTITY).visit(VISITOR);
#endif

#ifdef _DEBUG
#define DEBUG_VISIT(ENTITY,VISITOR) (ENTITY).visit(VISITOR);
#else
#define DEBUG_VISIT(ENTITY,VISITOR)
#endif

}

#endif
