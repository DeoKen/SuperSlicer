///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Api_plugin_cpp_ExtrusionViews_hpp_
#define slic3r_Api_plugin_cpp_ExtrusionViews_hpp_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "libslic3r/Api/plugin/c/slic3r_extrusion_entity.h"
#include "libslic3r/Api/plugin/c/slic3r_extrusion_polyline.h"
#include "libslic3r/Api/plugin/c/slic3r_extrusion_property.h"
#include "libslic3r/Api/plugin/cpp/GeometryViews.hpp"

namespace slic3r_api {

class ExtrusionEntity;
class MutableExtrusionEntity;
class StoredExtrusionEntity;

/*
Extrusion entity C++ views
==========================

This file is the ergonomic C++ layer over the strict C extrusion ABI.

The model is intentionally simple:
- ExtrusionEntity is a borrowed read-only view.
- MutableExtrusionEntity is a borrowed mutable view; it never frees the handle.
- StoredExtrusionEntity owns an entity created in a storage_handle and frees it
  with storage_free() when destroyed.

An extrusion entity may have a local polyline or children. The helper methods
below keep that distinction visible: point/segment operations affect only the
local polyline, while front(), back(), middle(), length() and empty() walk the
tree when the entity contains children.
*/

inline bool points_equal(c_point lhs, c_point rhs)
{
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

inline bool is_invalid_index(uint32_t idx)
{
    return idx == EXTRUSION_INDEX_INVALID;
}

/* ========================= property payload helpers ========================= */

template<class Payload, extrusion_property_type TypeValue>
struct EPropertyPayload : Payload
{
    static constexpr extrusion_property_type property_type = TypeValue;
};

struct EPropertyAttributes :
    EPropertyPayload<c_extrusion_property_attributes, EXTRUSION_PROPERTY_TYPE_ATTRIBUTES>
{
    EPropertyAttributes &extrusion_role(int32_t value) { role = value; return *this; }
    int32_t extrusion_role() const { return role; }
    EPropertyAttributes &no_seam_enabled(bool enabled = true) { no_seam = enabled ? 1 : 0; return *this; }
    bool no_seam_enabled() const { return no_seam != 0; }
    EPropertyAttributes &mm3_per_mm(double value) { flow.mm3_per_mm = value; return *this; }
    EPropertyAttributes &width(float value) { flow.width = value; return *this; }
    EPropertyAttributes &height(float value) { flow.height = value; return *this; }
};

struct EPropertySpeed :
    EPropertyPayload<c_extrusion_property_speed, EXTRUSION_PROPERTY_TYPE_SPEED>
{
    EPropertySpeed &speed(float value) { speed_mm_per_s = value; return *this; }
    EPropertySpeed &acceleration(float value) { accel_mm_per_s2 = value; return *this; }
    EPropertySpeed &pressure_advance(float value) { pressure_adv = value; return *this; }
    EPropertySpeed &fan_speed(float value) { fan_speed_percent = value; return *this; }
    EPropertySpeed &temperature(float value) { temperature_C = value; return *this; }
};

struct EPropertyModifier :
    EPropertyPayload<c_extrusion_property_modifier, EXTRUSION_PROPERTY_TYPE_MODIFIER>
{
    EPropertyModifier &set_enforce_travel(bool enabled = true) { enforce_travel = enabled ? 1 : 0; return *this; }
    EPropertyModifier &set_enforce_retraction(bool enabled = true) { enforce_retraction = enabled ? 1 : 0; return *this; }
    EPropertyModifier &set_enforce_unlift(bool enabled = true) { enforce_unlift = enabled ? 1 : 0; return *this; }
    EPropertyModifier &set_disable_retraction(bool enabled = true) { disable_retraction = enabled ? 1 : 0; return *this; }
    EPropertyModifier &set_disable_lift(bool enabled = true) { disable_lift = enabled ? 1 : 0; return *this; }
    EPropertyModifier &set_toolchange_retraction(bool enabled = true) { toolchange_retraction = enabled ? 1 : 0; return *this; }
};

struct EPropertyCustomGcode :
    EPropertyPayload<c_extrusion_property_custom_gcode, EXTRUSION_PROPERTY_TYPE_CUSTOM_GCODE>
{
    EPropertyCustomGcode &set_kind(c_extrusion_custom_gcode_kind value) { kind = value; return *this; }
    EPropertyCustomGcode &text(extrusion_data_id value) { text_id = value; return *this; }
};

struct EPropertySpecialCommand :
    EPropertyPayload<c_extrusion_property_special_command, EXTRUSION_PROPERTY_TYPE_SPECIAL_COMMAND>
{
    EPropertySpecialCommand &set(c_extrusion_special_command value, double data = 0.) {
        command = value;
        extra_data = data;
        return *this;
    }
};

struct EPropertyOverhang :
    EPropertyPayload<c_extrusion_property_overhang, EXTRUSION_PROPERTY_TYPE_OVERHANG>
{
    EPropertyOverhang &distance(float start, float end) {
        start_distance_from_prev_layer = start;
        end_distance_from_prev_layer = end;
        return *this;
    }
    EPropertyOverhang &curled_proximity(float value) { proximity_to_curled_lines = value; return *this; }
    EPropertyOverhang &full_overhangs_flow(bool enabled = true) { has_full_overhangs_flow = enabled ? 1 : 0; return *this; }
    EPropertyOverhang &full_overhangs_speed(bool enabled = true) { has_full_overhangs_speed = enabled ? 1 : 0; return *this; }
    EPropertyOverhang &dynamic_overhangs_flow(bool enabled = true) { has_dynamic_overhangs_flow = enabled ? 1 : 0; return *this; }
    EPropertyOverhang &dynamic_overhangs_speed(bool enabled = true) { has_dynamic_overhangs_speed = enabled ? 1 : 0; return *this; }
};

struct EPropertyZOffset :
    EPropertyPayload<c_extrusion_property_z_offset, EXTRUSION_PROPERTY_TYPE_Z_OFFSET>
{
    EPropertyZOffset &set(coord_t value) { z_offset = value; return *this; }
    coord_t get() const { return z_offset; }
};

struct EPropertyPerimeter :
    EPropertyPayload<c_extrusion_property_perimeter, EXTRUSION_PROPERTY_TYPE_PERIMETER>
{
    EPropertyPerimeter &shell_count(int16_t value) { perimeter_idx = value; return *this; }
    int16_t shell_count() const { return perimeter_idx; }
    EPropertyPerimeter &perimeter_role(int32_t value) { loop_role = value; return *this; }
    int32_t perimeter_role() const { return loop_role; }
};

static_assert(sizeof(EPropertyAttributes) == sizeof(c_extrusion_property_attributes), "ABI payload mismatch");
static_assert(sizeof(EPropertySpeed) == sizeof(c_extrusion_property_speed), "ABI payload mismatch");
static_assert(sizeof(EPropertyModifier) == sizeof(c_extrusion_property_modifier), "ABI payload mismatch");
static_assert(sizeof(EPropertyCustomGcode) == sizeof(c_extrusion_property_custom_gcode), "ABI payload mismatch");
static_assert(sizeof(EPropertySpecialCommand) == sizeof(c_extrusion_property_special_command), "ABI payload mismatch");
static_assert(sizeof(EPropertyOverhang) == sizeof(c_extrusion_property_overhang), "ABI payload mismatch");
static_assert(sizeof(EPropertyZOffset) == sizeof(c_extrusion_property_z_offset), "ABI payload mismatch");
static_assert(sizeof(EPropertyPerimeter) == sizeof(c_extrusion_property_perimeter), "ABI payload mismatch");

template<class Payload>
inline const Payload *property_payload_cast(const void *data)
{
    static_assert(std::is_trivially_copyable<Payload>::value, "Extrusion property payloads must be trivially copyable");
    return reinterpret_cast<const Payload *>(data);
}

template<class Payload>
inline Payload *property_payload_cast(void *data)
{
    static_assert(std::is_trivially_copyable<Payload>::value, "Extrusion property payloads must be trivially copyable");
    return reinterpret_cast<Payload *>(data);
}

template<class Payload>
inline extrusion_property_type register_extrusion_property_type(orchestrator_handle *orchestrator,
                                                                const char *namespaced_name)
{
    assert(orchestrator != nullptr);
    static_assert(std::is_trivially_copyable<Payload>::value, "Custom extrusion properties must be trivially copyable");
    return extrusion_property_register_type(orchestrator, namespaced_name, sizeof(Payload), alignof(Payload));
}

/* ========================= read / mutable CRTP APIs ========================= */

template<class Derived> class ExtrusionPropertyReadApi
{
public:
    uint32_t property_count() const { return extrusion_property_count(self().handle()); }
    extrusion_property_type property_type_at(uint32_t idx) const { return extrusion_property_type_at(self().handle(), idx); }
    bool has_property(extrusion_property_type type) const { return extrusion_property_has(self().handle(), type) != 0; }

    template<class Payload> bool has_property() const { return has_property(Payload::property_type); }

    const void *property_data(extrusion_property_type type) const {
        return extrusion_property_data(self().handle(), type);
    }

    template<class Payload> const Payload *property() const {
        return property_payload_cast<Payload>(property_data(Payload::property_type));
    }

    template<class Payload> Payload property_or(Payload fallback) const {
        const Payload *payload = property<Payload>();
        return payload != nullptr ? *payload : fallback;
    }

    const void *stored_data(extrusion_data_id id, uint32_t *byte_size_out = nullptr) const {
        return extrusion_data(self().handle(), id, byte_size_out);
    }

    std::string stored_string(extrusion_data_id id) const {
        uint32_t byte_size = 0;
        const char *data = static_cast<const char *>(stored_data(id, &byte_size));
        if (data == nullptr || byte_size == 0)
            return {};
        if (data[byte_size - 1] == '\0')
            --byte_size;
        return std::string(data, data + byte_size);
    }

protected:
    const Derived &self() const { return static_cast<const Derived &>(*this); }
};

template<class Derived> class ExtrusionPropertyMutableApi
{
public:
    void *property_data_mutable(extrusion_property_type type) {
        return extrusion_property_data_mutable(self().mutable_handle(), type);
    }

    void *get_or_add_property_data_mutable(orchestrator_handle *orchestrator, extrusion_property_type type) {
        return extrusion_property_get_or_add_data_mutable(orchestrator, self().mutable_handle(), type);
    }

    template<class Payload> Payload *property_mutable() {
        return property_payload_cast<Payload>(property_data_mutable(Payload::property_type));
    }

    template<class Payload> Payload &property(orchestrator_handle *orchestrator = nullptr) {
        Payload *payload = property_payload_cast<Payload>(
            get_or_add_property_data_mutable(orchestrator, Payload::property_type));
        assert(payload != nullptr);
        return *payload;
    }

    bool remove_property(extrusion_property_type type) {
        return extrusion_property_remove(self().mutable_handle(), type) != 0;
    }

    template<class Payload> bool remove_property() { return remove_property(Payload::property_type); }

    extrusion_data_id store_data(const void *data, uint32_t byte_size, uint32_t alignment) {
        return extrusion_store_data_aligned(self().mutable_handle(), data, byte_size, alignment);
    }

    template<class Payload> extrusion_data_id store_value(const Payload &payload) {
        static_assert(std::is_trivially_copyable<Payload>::value, "Stored data payloads must be trivially copyable");
        return store_data(&payload, sizeof(Payload), alignof(Payload));
    }

    extrusion_data_id store_string(std::string_view text) {
        const std::string text_copy(text);
        return store_data(text_copy.c_str(), static_cast<uint32_t>(text_copy.size() + 1), alignof(char));
    }

    bool free_data(extrusion_data_id id) {
        return extrusion_free_data(self().mutable_handle(), id) != 0;
    }

    EPropertyCustomGcode &custom_gcode(std::string_view text,
                                       c_extrusion_custom_gcode_kind kind = C_EXTRUSION_CUSTOM_GCODE_GCODE)
    {
        EPropertyCustomGcode &payload = property<EPropertyCustomGcode>();
        payload.kind = kind;
        payload.text_id = store_string(text);
        return payload;
    }

protected:
    Derived &self() { return static_cast<Derived &>(*this); }
};

template<class Derived> class ExtrusionPolylineReadApi
{
public:
    bool has_polyline() const { return extrusion_has_polyline(self().handle()) != 0; }
    uint32_t point_count() const { return extrusion_polyline_point_count(self().handle()); }
    uint32_t segment_count() const { return extrusion_polyline_segment_count(self().handle()); }
    bool has_local_points() const { return point_count() > 0; }
    c_point point(uint32_t idx) const { return extrusion_polyline_point(self().handle(), idx); }
    c_point operator[](uint32_t idx) const { return point(idx); }
    c_point local_front() const { assert(point_count() > 0); return point(0); }
    c_point local_back() const { assert(point_count() > 0); return point(point_count() - 1); }
    c_point local_middle() const { assert(point_count() > 0); return point(point_count() / 2); }
    distf_t local_length() const { return extrusion_polyline_length(self().handle()); }
    bool local_is_closed() const {
        return point_count() > 1 && points_equal(local_front(), local_back());
    }

    uint32_t find_point(c_point point, coord_t max_distance) const {
        return extrusion_polyline_find_point(self().handle(), point, max_distance);
    }

    c_point point_from_begin(distf_t distance) const {
        const distf_t length = local_length();
        return point_from_end(distance >= length ? 0. : length - distance);
    }

    c_point point_from_start(distf_t distance) const { return point_from_begin(distance); }

    c_point point_from_end(distf_t distance) const {
        return extrusion_polyline_point_from_end(self().handle(), distance);
    }

    c_extrusion_segment segment(uint32_t idx) const {
        c_extrusion_segment out = {};
        const int32_t ok = extrusion_polyline_segment(self().handle(), idx, &out);
        assert(ok != 0);
        (void) ok;
        return out;
    }

    std::vector<c_point> points() const {
        std::vector<c_point> out(point_count());
        if (!out.empty())
            extrusion_polyline_copy_points(self().handle(), out.data(), static_cast<uint32_t>(out.size()));
        return out;
    }

    std::vector<c_extrusion_segment> segments() const {
        std::vector<c_extrusion_segment> out(segment_count());
        if (!out.empty())
            extrusion_polyline_copy_segments(self().handle(), out.data(), static_cast<uint32_t>(out.size()));
        return out;
    }

    bool has_z_offsets() const { return extrusion_polyline_has_z_offsets(self().handle()) != 0; }
    coord_t z_offset(uint32_t idx) const {
        coord_t out = 0;
        const int32_t ok = extrusion_polyline_z_offset(self().handle(), idx, &out);
        assert(ok != 0);
        (void) ok;
        return out;
    }

protected:
    const Derived &self() const { return static_cast<const Derived &>(*this); }
};

template<class Derived> class ExtrusionPolylineMutableApi
{
public:
    bool clear_polyline() { return extrusion_polyline_clear(self().mutable_handle()) != 0; }
    bool set_point(uint32_t idx, c_point point) {
        return extrusion_polyline_set_point(self().mutable_handle(), idx, point) != 0;
    }
    uint32_t insert_point(uint32_t idx, c_point point) {
        return extrusion_polyline_insert_point(self().mutable_handle(), idx, point);
    }
    bool remove_point(uint32_t idx) {
        return extrusion_polyline_remove_point(self().mutable_handle(), idx) != 0;
    }
    bool set_points(const c_point *points, uint32_t count) {
        return extrusion_polyline_set_points(self().mutable_handle(), points, count) != 0;
    }
    bool set_points(const std::vector<c_point> &points) {
        return set_points(points.empty() ? nullptr : points.data(), static_cast<uint32_t>(points.size()));
    }
    bool set_segment(uint32_t idx, const c_extrusion_segment &segment) {
        return extrusion_polyline_set_segment(self().mutable_handle(), idx, &segment) != 0;
    }
    bool set_segments(const c_extrusion_segment *segments, uint32_t count) {
        return extrusion_polyline_set_segments(self().mutable_handle(), segments, count) != 0;
    }
    bool set_segments(const std::vector<c_extrusion_segment> &segments) {
        return set_segments(segments.empty() ? nullptr : segments.data(), static_cast<uint32_t>(segments.size()));
    }
    bool clip_end(distf_t distance) { return extrusion_polyline_clip_end(self().mutable_handle(), distance) != 0; }
    bool clip_start(distf_t distance) {
        const bool reversed = reverse();
        assert(reversed);
        if (!reversed)
            return false;
        const bool clipped = clip_end(distance);
        const bool restored = reverse();
        assert(restored);
        return clipped && restored;
    }
    bool translate(c_point offset) { return extrusion_polyline_translate(self().mutable_handle(), offset) != 0; }
    bool rotate(double angle) { return extrusion_polyline_rotate(self().mutable_handle(), angle) != 0; }
    bool reverse() { return extrusion_polyline_reverse(self().mutable_handle()) != 0; }
    bool set_z_offset(uint32_t idx, coord_t z_offset) {
        return extrusion_polyline_set_z_offset(self().mutable_handle(), idx, z_offset) != 0;
    }
    bool clear_z_offsets() { return extrusion_polyline_clear_z_offsets(self().mutable_handle()) != 0; }

    template<class PolylineLike> bool set(const PolylineLike &polyline) {
        return set_points(polyline.points());
    }

protected:
    Derived &self() { return static_cast<Derived &>(*this); }
};

template<class Derived> class ExtrusionEntityReadApi
{
public:
    bool valid() const { return self().handle() != nullptr; }
    explicit operator bool() const { return valid(); }
    uint32_t flags() const { return extrusion_flags(self().handle()); }
    bool reversible() const { return (flags() & RAW_EXTRUSION_FLAG_REVERSIBLE) != 0; }
    bool sortable() const { return (flags() & RAW_EXTRUSION_FLAG_SORTABLE) != 0; }
    bool continuous() const { return (flags() & RAW_EXTRUSION_FLAG_CONTINUOUS) != 0; }
    bool is_leaf() const { return extrusion_has_children(self().handle()) == 0; }
    uint32_t child_count() const { return extrusion_child_count(self().handle()); }

    ExtrusionEntity child(uint32_t idx) const;

    bool empty() const {
        if (self().point_count() > 0)
            return false;
        for (uint32_t idx = 0; idx < child_count(); ++idx)
            if (!child(idx).empty())
                return false;
        return true;
    }

    c_point front() const {
        assert(!empty());
        if (self().point_count() > 0)
            return self().local_front();
        for (uint32_t idx = 0; idx < child_count(); ++idx) {
            ExtrusionEntity candidate = child(idx);
            if (!candidate.empty())
                return candidate.front();
        }
        return {};
    }

    c_point back() const {
        assert(!empty());
        if (self().point_count() > 0)
            return self().local_back();
        for (uint32_t idx = child_count(); idx > 0; --idx) {
            ExtrusionEntity candidate = child(idx - 1);
            if (!candidate.empty())
                return candidate.back();
        }
        return {};
    }

    c_point middle() const {
        assert(!empty());
        if (self().point_count() > 0)
            return self().local_middle();
        for (uint32_t idx = child_count() / 2; idx < child_count(); ++idx) {
            ExtrusionEntity candidate = child(idx);
            if (!candidate.empty())
                return candidate.middle();
        }
        return front();
    }

    distf_t length() const {
        if (self().point_count() > 0)
            return self().local_length();
        distf_t out = 0;
        for (uint32_t idx = 0; idx < child_count(); ++idx)
            out += child(idx).length();
        return out;
    }

    bool is_closed() const {
        return !empty() && points_equal(front(), back());
    }

    std::vector<c_point> collect_points() const {
        std::vector<c_point> out;
        collect_points(out);
        return out;
    }

    void collect_points(std::vector<c_point> &out) const {
        const std::vector<c_point> local_points = self().points();
        out.insert(out.end(), local_points.begin(), local_points.end());
        for (uint32_t idx = 0; idx < child_count(); ++idx)
            child(idx).collect_points(out);
    }

    class children_iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = ExtrusionEntity;
        using difference_type = std::ptrdiff_t;

        children_iterator() = default;
        children_iterator(const Derived *owner, uint32_t idx) : m_owner(owner), m_idx(idx) {}
        value_type operator*() const;
        children_iterator &operator++() { ++m_idx; return *this; }
        children_iterator operator++(int) { children_iterator copy = *this; ++(*this); return copy; }
        bool operator==(const children_iterator &rhs) const { return m_owner == rhs.m_owner && m_idx == rhs.m_idx; }
        bool operator!=(const children_iterator &rhs) const { return !(*this == rhs); }

    private:
        const Derived *m_owner = nullptr;
        uint32_t m_idx = 0;
    };

    class children_range
    {
    public:
        explicit children_range(const Derived *owner) : m_owner(owner) {}
        children_iterator begin() const { return children_iterator(m_owner, 0); }
        children_iterator end() const { return children_iterator(m_owner, m_owner == nullptr ? 0 : m_owner->child_count()); }

    private:
        const Derived *m_owner = nullptr;
    };

    children_range children() const { return children_range(&self()); }

    StoredExtrusionEntity clone(storage_handle *storage) const;

protected:
    const Derived &self() const { return static_cast<const Derived &>(*this); }
};

template<class Derived> class ExtrusionEntityMutableApi
{
public:
    MutableExtrusionEntity child_mutable(uint32_t idx) const;

    bool set_flags(uint32_t flags) { return extrusion_set_flags(self().mutable_handle(), flags) != 0; }
    Derived &reversible(bool enabled = true) {
        const uint32_t new_flags = enabled ?
            (self().flags() | RAW_EXTRUSION_FLAG_REVERSIBLE) :
            (self().flags() & ~RAW_EXTRUSION_FLAG_REVERSIBLE);
        const bool ok = set_flags(new_flags);
        assert(ok);
        (void) ok;
        return self();
    }
    Derived &disable_reverse() { return reversible(false); }
    Derived &sortable(bool enabled = true) {
        const uint32_t new_flags = enabled ?
            (self().flags() | RAW_EXTRUSION_FLAG_SORTABLE) :
            (self().flags() & ~RAW_EXTRUSION_FLAG_SORTABLE);
        const bool ok = set_flags(new_flags);
        assert(ok);
        (void) ok;
        return self();
    }
    Derived &disable_sort() { return sortable(false); }
    Derived &continuous(bool enabled = true) {
        const uint32_t new_flags = enabled ?
            ((self().flags() | RAW_EXTRUSION_FLAG_CONTINUOUS) & ~RAW_EXTRUSION_FLAG_SORTABLE) :
            (self().flags() & ~RAW_EXTRUSION_FLAG_CONTINUOUS);
        const bool ok = set_flags(new_flags);
        assert(ok);
        (void) ok;
        return self();
    }

    bool clear_content() { return extrusion_clear_content(self().mutable_handle()) != 0; }
    uint32_t insert_child_copy(uint32_t idx, const ExtrusionEntity &child);
    uint32_t insert_child_move(uint32_t idx, MutableExtrusionEntity child);
    uint32_t add_child(const ExtrusionEntity &child);
    uint32_t add_child(MutableExtrusionEntity child);
    bool remove_child(uint32_t idx) { return extrusion_remove_child(self().mutable_handle(), idx) != 0; }
    uint32_t move_child_from(uint32_t dst_idx, MutableExtrusionEntity src_parent, uint32_t src_idx);

protected:
    Derived &self() { return static_cast<Derived &>(*this); }
    const Derived &self() const { return static_cast<const Derived &>(*this); }
};

class ExtrusionEntity :
    public ExtrusionPropertyReadApi<ExtrusionEntity>,
    public ExtrusionPolylineReadApi<ExtrusionEntity>,
    public ExtrusionEntityReadApi<ExtrusionEntity>
{
public:
    ExtrusionEntity() = default;
    explicit ExtrusionEntity(const extrusion_entity *handle) : m_handle(handle) { assert(handle != nullptr); }

    const extrusion_entity *handle() const {
        assert(m_handle != nullptr);
        return m_handle;
    }
    bool same_handle(const ExtrusionEntity &other) const { return m_handle == other.m_handle; }

protected:
    const extrusion_entity *m_handle = nullptr;
};

class MutableExtrusionEntity :
    public ExtrusionPropertyReadApi<MutableExtrusionEntity>,
    public ExtrusionPropertyMutableApi<MutableExtrusionEntity>,
    public ExtrusionPolylineReadApi<MutableExtrusionEntity>,
    public ExtrusionPolylineMutableApi<MutableExtrusionEntity>,
    public ExtrusionEntityReadApi<MutableExtrusionEntity>,
    public ExtrusionEntityMutableApi<MutableExtrusionEntity>
{
public:
    MutableExtrusionEntity() = default;
    explicit MutableExtrusionEntity(extrusion_entity *handle) : m_handle(handle) { assert(handle != nullptr); }

    const extrusion_entity *handle() const {
        assert(m_handle != nullptr);
        return m_handle;
    }
    extrusion_entity *mutable_handle() const {
        assert(m_handle != nullptr);
        return m_handle;
    }
    ExtrusionEntity readonly() const { return ExtrusionEntity(handle()); }
    operator ExtrusionEntity() const { return readonly(); }

private:
    extrusion_entity *m_handle = nullptr;
};

class StoredExtrusionEntity :
    public ExtrusionPropertyReadApi<StoredExtrusionEntity>,
    public ExtrusionPropertyMutableApi<StoredExtrusionEntity>,
    public ExtrusionPolylineReadApi<StoredExtrusionEntity>,
    public ExtrusionPolylineMutableApi<StoredExtrusionEntity>,
    public ExtrusionEntityReadApi<StoredExtrusionEntity>,
    public ExtrusionEntityMutableApi<StoredExtrusionEntity>
{
public:
    StoredExtrusionEntity() = delete;
    StoredExtrusionEntity(const StoredExtrusionEntity &) = delete;
    StoredExtrusionEntity &operator=(const StoredExtrusionEntity &) = delete;

    explicit StoredExtrusionEntity(storage_handle *storage) :
        StoredExtrusionEntity(storage, extrusion_create_empty(storage)) {}

    StoredExtrusionEntity(storage_handle *storage, const ExtrusionEntity &src) :
        StoredExtrusionEntity(storage, extrusion_clone(storage, src.handle())) {}

    StoredExtrusionEntity(storage_handle *storage, const std::vector<c_point> &points) :
        StoredExtrusionEntity(storage) { set_points(points); }

    StoredExtrusionEntity(storage_handle *storage, std::initializer_list<c_point> points) :
        StoredExtrusionEntity(storage, std::vector<c_point>(points)) {}

    template<class PolylineLike> StoredExtrusionEntity(storage_handle *storage, const PolylineLike &polyline) :
        StoredExtrusionEntity(storage) { set(polyline); }

    StoredExtrusionEntity(StoredExtrusionEntity &&rhs) noexcept :
        m_storage(rhs.m_storage), m_handle(rhs.m_handle) {
        rhs.m_storage = nullptr;
        rhs.m_handle = nullptr;
    }

    StoredExtrusionEntity &operator=(StoredExtrusionEntity &&rhs) noexcept {
        if (this == &rhs)
            return *this;
        reset();
        m_storage = rhs.m_storage;
        m_handle = rhs.m_handle;
        rhs.m_storage = nullptr;
        rhs.m_handle = nullptr;
        return *this;
    }

    ~StoredExtrusionEntity() { reset(); }

    static StoredExtrusionEntity adopt(storage_handle *storage, extrusion_entity *handle) {
        return StoredExtrusionEntity(storage, handle);
    }

    const extrusion_entity *handle() const {
        assert(m_handle != nullptr);
        return m_handle;
    }
    extrusion_entity *mutable_handle() const {
        assert(m_handle != nullptr);
        return m_handle;
    }
    storage_handle *storage() const {
        assert(m_storage != nullptr);
        return m_storage;
    }
    ExtrusionEntity readonly() const & { return ExtrusionEntity(handle()); }
    ExtrusionEntity readonly() && = delete;
    MutableExtrusionEntity mutable_view() const { return MutableExtrusionEntity(mutable_handle()); }
    operator ExtrusionEntity() const & { return readonly(); }
    operator ExtrusionEntity() && = delete;
    operator MutableExtrusionEntity() const { return mutable_view(); }

    bool free_from_storage() { return reset(); }
    bool copy_from(const ExtrusionEntity &src) { return extrusion_copy_from(mutable_handle(), src.handle()) != 0; }
    bool move_from(MutableExtrusionEntity src) { return extrusion_move_from(mutable_handle(), src.mutable_handle()) != 0; }

    MutableExtrusionEntity emplace_child() {
        StoredExtrusionEntity child(storage());
        const uint32_t idx = add_child(child.mutable_view());
        assert(!is_invalid_index(idx));
        return child_mutable(idx);
    }

private:
    StoredExtrusionEntity(storage_handle *storage, extrusion_entity *handle) :
        m_storage(storage), m_handle(handle) {
        assert(storage != nullptr);
        assert(handle != nullptr);
    }

    bool reset() {
        if (m_storage == nullptr || m_handle == nullptr)
            return false;
        const bool freed = storage_free(m_storage, m_handle) != 0;
        assert(freed);
        m_storage = nullptr;
        m_handle = nullptr;
        return freed;
    }

    storage_handle *m_storage = nullptr;
    extrusion_entity *m_handle = nullptr;
};

template<class Derived>
inline ExtrusionEntity ExtrusionEntityReadApi<Derived>::child(uint32_t idx) const
{
    const extrusion_entity *child_handle = extrusion_child(self().handle(), idx);
    assert(child_handle != nullptr);
    return ExtrusionEntity(child_handle);
}

template<class Derived>
inline typename ExtrusionEntityReadApi<Derived>::children_iterator::value_type
ExtrusionEntityReadApi<Derived>::children_iterator::operator*() const
{
    return m_owner->child(m_idx);
}

template<class Derived>
inline StoredExtrusionEntity ExtrusionEntityReadApi<Derived>::clone(storage_handle *storage) const
{
    return StoredExtrusionEntity(storage, ExtrusionEntity(self().handle()));
}

template<class Derived>
inline MutableExtrusionEntity ExtrusionEntityMutableApi<Derived>::child_mutable(uint32_t idx) const
{
    extrusion_entity *child_handle = extrusion_child_mutable(self().mutable_handle(), idx);
    assert(child_handle != nullptr);
    return MutableExtrusionEntity(child_handle);
}

template<class Derived>
inline uint32_t ExtrusionEntityMutableApi<Derived>::insert_child_move(uint32_t idx, MutableExtrusionEntity child)
{
    return extrusion_insert_child_move(self().mutable_handle(), idx, child.mutable_handle());
}

template<class Derived>
inline uint32_t ExtrusionEntityMutableApi<Derived>::insert_child_copy(uint32_t idx, const ExtrusionEntity &child)
{
    return extrusion_insert_child_copy(self().mutable_handle(), idx, child.handle());
}

template<class Derived>
inline uint32_t ExtrusionEntityMutableApi<Derived>::add_child(const ExtrusionEntity &child)
{
    return insert_child_copy(self().child_count(), child);
}

template<class Derived>
inline uint32_t ExtrusionEntityMutableApi<Derived>::add_child(MutableExtrusionEntity child)
{
    return insert_child_move(self().child_count(), child);
}

template<class Derived>
inline uint32_t ExtrusionEntityMutableApi<Derived>::move_child_from(uint32_t dst_idx,
                                                                    MutableExtrusionEntity src_parent,
                                                                    uint32_t src_idx)
{
    return extrusion_move_child(self().mutable_handle(), dst_idx, src_parent.mutable_handle(), src_idx);
}

} // namespace slic3r_api

#endif // slic3r_Api_plugin_cpp_ExtrusionViews_hpp_
