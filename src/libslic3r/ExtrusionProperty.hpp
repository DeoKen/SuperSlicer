///|/ Copyright (c) SuperSlicer 2026 Remi Durand @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef slic3r_ExtrusionProperty_hpp_
#define slic3r_ExtrusionProperty_hpp_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "Api/plugin/c/slic3r_extrusion_property.h"
#include "ExtrusionRole.hpp"
#include "libslic3r.h"

namespace Slic3r {

class Flow;
class ExtrusionProperty;
struct ExtrusionAttributes;
class ExtrusionPropertySpeed;
class ExtrusionPropertyModifier;
class ExtrusionPropertyCustomGcode;
class ExtrusionPropertySpecialCommand;
class ExtrusionPropertyOverhang;
class ExtrusionPropertyZOffset;
class ExtrusionPropertyZProfile;
class ExtrusionPropertyLoopRole;
namespace ApiInternal { struct ExtrusionPropertyAccess; }

using ExtrusionPropertyUPtr = std::unique_ptr<ExtrusionProperty>;
using ExtrusionPropertyUPtrs = std::vector<ExtrusionPropertyUPtr>;

using extrusion_property_type = ::extrusion_property_type;

enum : extrusion_property_type {
    extrusion_property_type_invalid         = EXTRUSION_PROPERTY_TYPE_INVALID,
    extrusion_property_type_attributes      = EXTRUSION_PROPERTY_TYPE_ATTRIBUTES,
    extrusion_property_type_speed           = EXTRUSION_PROPERTY_TYPE_SPEED,
    extrusion_property_type_modifier        = EXTRUSION_PROPERTY_TYPE_MODIFIER,
    extrusion_property_type_custom_gcode    = EXTRUSION_PROPERTY_TYPE_CUSTOM_GCODE,
    extrusion_property_type_special_command = EXTRUSION_PROPERTY_TYPE_SPECIAL_COMMAND,
    extrusion_property_type_overhang        = EXTRUSION_PROPERTY_TYPE_OVERHANG,
    extrusion_property_type_z_offset        = EXTRUSION_PROPERTY_TYPE_Z_OFFSET,
    extrusion_property_type_z_profile       = 8,
    extrusion_property_type_loop_role       = EXTRUSION_PROPERTY_TYPE_PERIMETER,
};

class ExtrusionProperty
{
public:
    virtual ~ExtrusionProperty() = default;
    virtual extrusion_property_type type() const = 0;
    virtual ExtrusionPropertyUPtr clone() const = 0;
};

struct ExtrusionFlow
{
    ExtrusionFlow() = default;
    ExtrusionFlow(double mm3_per_mm, float width, float height) :
        mm3_per_mm{ mm3_per_mm }, width{ width }, height{ height } {}
    ExtrusionFlow(const Flow &flow);

    void set_force_e_per_mm() { this->height = -2; }
    bool force_e_per_mm() const { return this->height == -2; }

    // Volumetric velocity. mm^3 of plastic per mm of linear head motion. Used by the G-code generator.
    // !!! if height == -2, then mm3_per_mm is changed into e_per_mm (used for exact unretraction) !!! (very unsafe, don't use it for normal extrusions)
    double          mm3_per_mm{ -1. };
    // Width of the extrusion, used for visualization purposes & for seam notch %. Unscaled
    float           width{ -1.f };
    // Height of the extrusion, used for visualization purposes. Unscaled
    float           height{ -1.f };
};

inline bool operator==(const ExtrusionFlow &lhs, const ExtrusionFlow &rhs)
{
    return lhs.mm3_per_mm == rhs.mm3_per_mm && lhs.width == rhs.width && lhs.height == rhs.height;
}

struct ExtrusionAttributes : ExtrusionProperty, ExtrusionFlow
{
    static constexpr extrusion_property_type property_type = extrusion_property_type_attributes;

    ExtrusionAttributes() = default;
    ExtrusionAttributes(ExtrusionRole role) : role{ role } {}
    ExtrusionAttributes(ExtrusionRole role, const Flow &flow);
    ExtrusionAttributes(ExtrusionRole role, const ExtrusionFlow &flow) : ExtrusionFlow{ flow }, role{ role } {}

    extrusion_property_type type() const override { return property_type; }
    ExtrusionPropertyUPtr clone() const override { return std::make_unique<ExtrusionAttributes>(*this); }

    // What is the role / purpose of this extrusion?
    ExtrusionRole   role{ ExtrusionRole::None };
    // set to true to prevent seam on this path.
    bool no_seam = false;
};

inline bool operator==(const ExtrusionAttributes &lhs, const ExtrusionAttributes &rhs)
{
    return static_cast<const ExtrusionFlow&>(lhs) == static_cast<const ExtrusionFlow&>(rhs) &&
           lhs.role == rhs.role;
}

// These are a state. They are used for all children if it's not overriden.
// After the end of this entity, it's reverted to previous state.
class ExtrusionPropertySpeed : public ExtrusionProperty
{
public:
    static constexpr extrusion_property_type property_type = extrusion_property_type_speed;

    float speed_mm_per_s = -1.f;
    float accel_mm_per_s2 = -1.f;
    float pressure_adv = -1.f;
    float fan_speed_percent = -1.f;
    float temperature_C = -1.f;

    ExtrusionPropertySpeed(float speed = -1, float accel = -1, float pa = -1, float fan = -1, float temp = -1)
        : speed_mm_per_s(speed)
        , accel_mm_per_s2(accel)
        , pressure_adv(pa)
        , fan_speed_percent(fan)
        , temperature_C(temp) {}

    ExtrusionPropertySpeed& speed(float speed) { speed_mm_per_s = speed; return *this; }
    ExtrusionPropertySpeed& acceleration(float accel) { accel_mm_per_s2 = accel; return *this; }
    ExtrusionPropertySpeed& presure_advance(float pa) { pressure_adv = pa; return *this; }
    ExtrusionPropertySpeed& fan_speed(float fspeed) { assert(fspeed >= -1 && fspeed <= 100); fan_speed_percent = fspeed; return *this; }
    ExtrusionPropertySpeed& temperature(float temp) { temperature_C = temp; return *this; }

    extrusion_property_type type() const override { return property_type; }
    ExtrusionPropertyUPtr clone() const override { return std::make_unique<ExtrusionPropertySpeed>(*this); }
};

// Store switches to activate/deactivate/enforce gcode features like retract, lift, etc.
class ExtrusionPropertyModifier : public ExtrusionProperty
{
public:
    static constexpr extrusion_property_type property_type = extrusion_property_type_modifier;

    bool enforce_travel = false;
    bool enforce_retraction = false;
    bool enforce_unlift = false;
    bool disable_retraction = false;
    bool disable_lift = false;
    bool toolchange_retraction = false;

    ExtrusionPropertyModifier& set_enforce_travel(bool enforce = true) { enforce_travel = enforce; return *this; }
    ExtrusionPropertyModifier& set_enforce_retraction(bool enforce = true) { enforce_retraction = enforce; return *this; }
    ExtrusionPropertyModifier& set_enforce_unlift(bool enforce = true) { enforce_unlift = enforce; return *this; }
    ExtrusionPropertyModifier& set_disable_retraction(bool disable = true) { disable_retraction = disable; return *this; }
    ExtrusionPropertyModifier& set_disable_lift(bool disable = true) { disable_lift = disable; return *this; }
    ExtrusionPropertyModifier& set_toolchange_retraction(bool is = true) { toolchange_retraction = is; return *this; }

    extrusion_property_type type() const override { return property_type; }
    ExtrusionPropertyUPtr clone() const override { return std::make_unique<ExtrusionPropertyModifier>(*this); }
};

class ExtrusionPropertyCustomGcode : public ExtrusionProperty
{
public:
    static constexpr extrusion_property_type property_type = extrusion_property_type_custom_gcode;

    enum class Code {
        GCODE,
        COMMENT,
    };

    Code code;
    std::string gcode;

    ExtrusionPropertyCustomGcode(const std::string &str);
    ExtrusionPropertyCustomGcode(Code c, const std::string &str) : code(c), gcode(str) {}

    extrusion_property_type type() const override { return property_type; }
    ExtrusionPropertyUPtr clone() const override { return std::make_unique<ExtrusionPropertyCustomGcode>(*this); }
};

class ExtrusionPropertySpecialCommand : public ExtrusionProperty
{
public:
    static constexpr extrusion_property_type property_type = extrusion_property_type_special_command;

    enum class Code {
        TOOLCHANGE,
        SAVE_AND_RESET_SPEED_RATIO,
        RESTORE_SPEED_RATIO,
        FLUSH_PLANNER_QUEUE,
        EXTRUSION,
        RETRACT,
        PAUSE,
        WAIT_FOR_TEMP,
        DISABLE_PREVIEW,
        ENABLE_PREVIEW,
        EXTRUDER_CURRENT,
    };

    Code code;
    double extra_data;

    ExtrusionPropertySpecialCommand(Code c) : code(c), extra_data(0) {}
    ExtrusionPropertySpecialCommand(Code c, double data) : code(c), extra_data(data) {}

    extrusion_property_type type() const override { return property_type; }
    ExtrusionPropertyUPtr clone() const override { return std::make_unique<ExtrusionPropertySpecialCommand>(*this); }
};

class ExtrusionPropertyOverhang : public ExtrusionProperty
{
public:
    static constexpr extrusion_property_type property_type = extrusion_property_type_overhang;

    float start_distance_from_prev_layer = -1.f;
    float end_distance_from_prev_layer = -1.f;
    float proximity_to_curled_lines = 0.f;
    bool has_full_overhangs_flow = false;
    bool has_full_overhangs_speed = false;
    bool has_dynamic_overhangs_flow = false;
    bool has_dynamic_overhangs_speed = false;

    ExtrusionPropertyOverhang() = default;
    ExtrusionPropertyOverhang(float start_dist, float end_dist)
        : start_distance_from_prev_layer(start_dist), end_distance_from_prev_layer(end_dist) {}
    ExtrusionPropertyOverhang(float start_dist, float end_dist, float curled_ratio)
        : start_distance_from_prev_layer(start_dist)
        , end_distance_from_prev_layer(end_dist)
        , proximity_to_curled_lines(curled_ratio) {}
    ExtrusionPropertyOverhang(float start_dist, float end_dist, float curled_ratio, bool full_flow, bool full_speed, bool dynamic_flow, bool dynamic_speed)
        : start_distance_from_prev_layer(start_dist)
        , end_distance_from_prev_layer(end_dist)
        , proximity_to_curled_lines(curled_ratio)
        , has_full_overhangs_flow(full_flow)
        , has_full_overhangs_speed(full_speed)
        , has_dynamic_overhangs_flow(dynamic_flow)
        , has_dynamic_overhangs_speed(dynamic_speed) {}

    extrusion_property_type type() const override { return property_type; }
    ExtrusionPropertyUPtr clone() const override { return std::make_unique<ExtrusionPropertyOverhang>(*this); }
};

class ExtrusionPropertyZOffset : public ExtrusionProperty
{
public:
    static constexpr extrusion_property_type property_type = extrusion_property_type_z_offset;

    coord_t z_offset = 0;

    ExtrusionPropertyZOffset() = default;
    explicit ExtrusionPropertyZOffset(coord_t offset) : z_offset(offset) {}

    extrusion_property_type type() const override { return property_type; }
    ExtrusionPropertyUPtr clone() const override { return std::make_unique<ExtrusionPropertyZOffset>(*this); }
};

class ExtrusionPropertyLoopRole : public ExtrusionProperty
{
public:
    static constexpr extrusion_property_type property_type = extrusion_property_type_loop_role;

    // if perimeter, this is the perimeter count. 0 = external, negative = not a perimeter.
    int16_t perimeter_idx;
    // Set of tags to identify the loop.
    // important one: elrHole => hole-perimeter, else it's a contour-perimeter
    ExtrusionLoopRole loop_role { elrDefault };

    ExtrusionPropertyLoopRole() = default;
    explicit ExtrusionPropertyLoopRole(ExtrusionLoopRole role) : loop_role(role) {}

    extrusion_property_type type() const override { return property_type; }
    ExtrusionPropertyUPtr clone() const override { return std::make_unique<ExtrusionPropertyLoopRole>(*this); }
};

template<typename PropertyType>
struct ExtrusionPropertyTraits
{
    static_assert(std::is_base_of<ExtrusionProperty, PropertyType>::value, "PropertyType must inherit ExtrusionProperty");
    static constexpr extrusion_property_type type = PropertyType::property_type;
};

class RawBuffer
{
public:
    RawBuffer() = default;
    RawBuffer(const RawBuffer&) = delete;
    RawBuffer(RawBuffer &&rhs) noexcept;
    RawBuffer& operator=(const RawBuffer&) = delete;
    RawBuffer& operator=(RawBuffer &&rhs) noexcept;
    ~RawBuffer();

    void allocate(size_t byte_count, size_t alignment);
    void copy_from(const void *data, size_t byte_count, size_t alignment);
    void reset();

    void* data() { return m_data; }
    const void* data() const { return m_data; }
    size_t size() const { return m_size; }
    size_t alignment() const { return m_alignment; }

    template<typename T> T& as()
    {
        assert(m_data != nullptr);
        assert(m_size == sizeof(T));
        assert(m_alignment >= alignof(T));
        return *reinterpret_cast<T*>(m_data);
    }

    template<typename T> const T& as() const
    {
        assert(m_data != nullptr);
        assert(m_size == sizeof(T));
        assert(m_alignment >= alignof(T));
        return *reinterpret_cast<const T*>(m_data);
    }

private:
    // The buffer owns raw storage only. PropertySlot is responsible for
    // constructing and destroying the property object stored in this memory.
    void  *m_data = nullptr;
    size_t m_size = 0;
    size_t m_alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
};

// Type-tagged raw storage for one extrusion property.
// Call as<T>() / get_if<T>() when the slot type is known by the caller.
class PropertySlot;

struct PropertySlotOps
{
    using DestroyFn = void (*)(void*);
    using CloneFn = void (*)(PropertySlot&, const void*);
    using PropertyFn = ExtrusionProperty* (*)(void*);
    using ConstPropertyFn = const ExtrusionProperty* (*)(const void*);

    extrusion_property_type type;
    DestroyFn destroy;
    CloneFn clone;
    PropertyFn property;
    ConstPropertyFn const_property;
};

class PropertySlot
{
    friend class ExtrusionPropertyContainer;
    friend struct ApiInternal::ExtrusionPropertyAccess;

public:
    PropertySlot() = default;
    PropertySlot(const PropertySlot &rhs);
    PropertySlot(PropertySlot &&rhs) noexcept;
    PropertySlot& operator=(const PropertySlot &rhs);
    PropertySlot& operator=(PropertySlot &&rhs) noexcept;
    ~PropertySlot();

    extrusion_property_type type() const { return m_ops != nullptr ? m_ops->type : m_raw_type; }
    bool empty() const { return m_ops == nullptr && m_raw_type == extrusion_property_type_invalid; }

    template<typename PropertyType, typename... Args> PropertyType& emplace(Args&&... args)
    {
        static_assert(std::is_base_of<ExtrusionProperty, PropertyType>::value, "PropertyType must inherit ExtrusionProperty");
        this->reset();
        m_data.allocate(sizeof(PropertyType), alignof(PropertyType));
        PropertyType *property = new (m_data.data()) PropertyType(std::forward<Args>(args)...);
        m_ops = &PropertySlot::ops<PropertyType>();
        return *property;
    }

    template<typename PropertyType> PropertyType* get_if()
    {
        static_assert(std::is_base_of<ExtrusionProperty, PropertyType>::value, "PropertyType must inherit ExtrusionProperty");
        return this->type() == ExtrusionPropertyTraits<PropertyType>::type ? &m_data.as<PropertyType>() : nullptr;
    }

    template<typename PropertyType> const PropertyType* get_if() const
    {
        static_assert(std::is_base_of<ExtrusionProperty, PropertyType>::value, "PropertyType must inherit ExtrusionProperty");
        return this->type() == ExtrusionPropertyTraits<PropertyType>::type ? &m_data.as<PropertyType>() : nullptr;
    }

    template<typename PropertyType> PropertyType& as()
    {
        assert(this->type() == ExtrusionPropertyTraits<PropertyType>::type);
        return m_data.as<PropertyType>();
    }

    template<typename PropertyType> const PropertyType& as() const
    {
        assert(this->type() == ExtrusionPropertyTraits<PropertyType>::type);
        return m_data.as<PropertyType>();
    }

    ExtrusionProperty& property() { assert(m_ops != nullptr); return *m_ops->property(m_data.data()); }
    const ExtrusionProperty& property() const { assert(m_ops != nullptr); return *m_ops->const_property(m_data.data()); }
    ExtrusionProperty* property_or_null() { return m_ops != nullptr ? m_ops->property(m_data.data()) : nullptr; }
    const ExtrusionProperty* property_or_null() const { return m_ops != nullptr ? m_ops->const_property(m_data.data()) : nullptr; }

    void reset();

private:
    void emplace_raw(extrusion_property_type type, const void *data, size_t byte_count, size_t alignment);
    void emplace_zeroed(extrusion_property_type type, size_t byte_count, size_t alignment);
    void* data();
    const void* data() const;
    size_t byte_count() const { return m_data.size(); }
    size_t alignment() const { return m_data.alignment(); }

    template<typename PropertyType> static const PropertySlotOps& ops()
    {
        static const PropertySlotOps slot_ops = {
            ExtrusionPropertyTraits<PropertyType>::type,
            &PropertySlot::destroy_property<PropertyType>,
            &PropertySlot::clone_property<PropertyType>,
            &PropertySlot::property<PropertyType>,
            &PropertySlot::const_property<PropertyType>
        };
        return slot_ops;
    }

    template<typename PropertyType> static void destroy_property(void *data) { m_data_as<PropertyType>(data).~PropertyType(); }
    template<typename PropertyType> static void clone_property(PropertySlot &dst, const void *src) { dst.emplace<PropertyType>(m_const_data_as<PropertyType>(src)); }
    template<typename PropertyType> static ExtrusionProperty* property(void *data) { return &m_data_as<PropertyType>(data); }
    template<typename PropertyType> static const ExtrusionProperty* const_property(const void *data) { return &m_const_data_as<PropertyType>(data); }
    template<typename PropertyType> static PropertyType& m_data_as(void *data) { return *reinterpret_cast<PropertyType*>(data); }
    template<typename PropertyType> static const PropertyType& m_const_data_as(const void *data) { return *reinterpret_cast<const PropertyType*>(data); }

    RawBuffer m_data;
    const PropertySlotOps *m_ops = nullptr;
    extrusion_property_type m_raw_type = extrusion_property_type_invalid;
};

// Small typed property bag for extrusion interpretation modifiers.
// Most entities have no property, and the few that do usually carry one or two;
// the vector keeps storage compact while PropertySlot makes the "one property per
// type" rule explicit and avoids dynamic casts on typed lookups.
class ExtrusionPropertyContainer
{
    friend struct ApiInternal::ExtrusionPropertyAccess;

public:
    ExtrusionPropertyContainer() = default;
    explicit ExtrusionPropertyContainer(ExtrusionPropertyUPtr &&property);
    explicit ExtrusionPropertyContainer(ExtrusionPropertyUPtrs &&properties);
    ExtrusionPropertyContainer(const ExtrusionPropertyContainer &rhs);
    ExtrusionPropertyContainer(ExtrusionPropertyContainer &&rhs) noexcept = default;
    ExtrusionPropertyContainer& operator=(const ExtrusionPropertyContainer &rhs);
    ExtrusionPropertyContainer& operator=(ExtrusionPropertyContainer &&rhs) noexcept = default;

    bool has_properties() const { return !m_properties.empty(); }
    void clear_properties() { m_properties.clear(); }
    ExtrusionPropertyUPtrs clone_properties() const;

    ExtrusionProperty& add_property(const ExtrusionProperty &property);
    ExtrusionProperty& add_property(ExtrusionPropertyUPtr &&property);

    template<typename PropertyType> PropertyType* get_property()
    {
        static_assert(std::is_base_of<ExtrusionProperty, PropertyType>::value, "PropertyType must inherit ExtrusionProperty");
        PropertySlot *slot = this->find_slot(ExtrusionPropertyTraits<PropertyType>::type);
        if (slot != nullptr)
            return slot->get_if<PropertyType>();
        return nullptr;
    }

    template<typename PropertyType> const PropertyType* get_property() const
    {
        static_assert(std::is_base_of<ExtrusionProperty, PropertyType>::value, "PropertyType must inherit ExtrusionProperty");
        const PropertySlot *slot = this->find_slot(ExtrusionPropertyTraits<PropertyType>::type);
        if (slot != nullptr)
            return slot->get_if<PropertyType>();
        return nullptr;
    }

    template<typename PropertyType, typename... Args> PropertyType& get_or_add_property(Args&&... args)
    {
        static_assert(std::is_base_of<ExtrusionProperty, PropertyType>::value, "PropertyType must inherit ExtrusionProperty");
        if (PropertyType *property = this->get_property<PropertyType>())
            return *property;
        PropertySlot slot;
        PropertyType &out = slot.emplace<PropertyType>(std::forward<Args>(args)...);
        m_properties.emplace_back(std::move(slot));
        return out;
    }

    template<typename PropertyType> bool remove_property()
    {
        static_assert(std::is_base_of<ExtrusionProperty, PropertyType>::value, "PropertyType must inherit ExtrusionProperty");
        for (std::vector<PropertySlot>::iterator it = m_properties.begin(); it != m_properties.end(); ++ it)
            if (it->type() == ExtrusionPropertyTraits<PropertyType>::type) {
                m_properties.erase(it);
                return true;
            }
        return false;
    }

protected:

    size_t property_count() const { return m_properties.size(); }
    extrusion_property_type property_type_at(size_t idx) const;
    bool has_property(extrusion_property_type type) const { return this->find_slot(type) != nullptr; }
    const void* property_data(extrusion_property_type type) const;
    void* property_data_mutable(extrusion_property_type type);
    void* get_or_add_property_data_mutable(extrusion_property_type type, size_t byte_count, size_t alignment);
    bool remove_property(extrusion_property_type type);

    uint32_t store_data_aligned(const void *data, size_t byte_count, size_t alignment);
    const void* stored_data(uint32_t data_id, uint32_t *byte_size_out) const;
    bool free_data(uint32_t data_id);

    PropertySlot* find_slot(extrusion_property_type type);
    const PropertySlot* find_slot(extrusion_property_type type) const;

    struct DataResource
    {
        uint32_t id = 0;
        RawBuffer data;

        DataResource() = default;
        DataResource(const DataResource &rhs);
        DataResource(DataResource &&rhs) noexcept = default;
        DataResource& operator=(const DataResource &rhs);
        DataResource& operator=(DataResource &&rhs) noexcept = default;
    };

    std::vector<PropertySlot> m_properties;
    std::vector<DataResource> m_data_resources;
    uint32_t m_next_data_resource_id = 1;
};

template<typename PropertyType>
class AddGetEEAttribute
{
    static_assert(std::is_base_of<ExtrusionProperty, PropertyType>::value, "PropertyType must inherit ExtrusionProperty");
public:
    PropertyType *found = nullptr;
    PropertyType& add_or_get(ExtrusionPropertyContainer &entity)
    {
        found = &entity.get_or_add_property<PropertyType>();
        return *found;
    }
};

template<typename PropertyType>
class GetEEAttribute
{
    static_assert(std::is_base_of<ExtrusionProperty, PropertyType>::value, "PropertyType must inherit ExtrusionProperty");
public:
    const PropertyType *found = nullptr;
    const PropertyType* get(const ExtrusionPropertyContainer &entity)
    {
        found = entity.get_property<PropertyType>();
        return found;
    }
};

} // namespace Slic3r

#endif
