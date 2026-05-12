///|/ Copyright (c) SuperSlicer 2026 Remi Durand @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef slic3r_ExtrusionProperty_hpp_
#define slic3r_ExtrusionProperty_hpp_

#include <cassert>
#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

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
class ExtrusionPropertyLoopRole;

using ExtrusionPropertyUPtr = std::unique_ptr<ExtrusionProperty>;
using ExtrusionPropertyUPtrs = std::vector<ExtrusionPropertyUPtr>;

class ExtrusionProperty
{
public:
    virtual ~ExtrusionProperty() = default;
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
    ExtrusionAttributes() = default;
    ExtrusionAttributes(ExtrusionRole role) : role{ role } {}
    ExtrusionAttributes(ExtrusionRole role, const Flow &flow);
    ExtrusionAttributes(ExtrusionRole role, const ExtrusionFlow &flow) : ExtrusionFlow{ flow }, role{ role } {}

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

    ExtrusionPropertyUPtr clone() const override { return std::make_unique<ExtrusionPropertySpeed>(*this); }
};

// Store switches to activate/deactivate/enforce gcode features like retract, lift, etc.
class ExtrusionPropertyModifier : public ExtrusionProperty
{
public:
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

    ExtrusionPropertyUPtr clone() const override { return std::make_unique<ExtrusionPropertyModifier>(*this); }
};

class ExtrusionPropertyCustomGcode : public ExtrusionProperty
{
public:
    enum class Code {
        GCODE,
        COMMENT,
    };

    Code code;
    std::string gcode;

    ExtrusionPropertyCustomGcode(const std::string &str);
    ExtrusionPropertyCustomGcode(Code c, const std::string &str) : code(c), gcode(str) {}

    ExtrusionPropertyUPtr clone() const override { return std::make_unique<ExtrusionPropertyCustomGcode>(*this); }
};

class ExtrusionPropertySpecialCommand : public ExtrusionProperty
{
public:
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

    ExtrusionPropertyUPtr clone() const override { return std::make_unique<ExtrusionPropertySpecialCommand>(*this); }
};

class ExtrusionPropertyOverhang : public ExtrusionProperty
{
public:
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

    ExtrusionPropertyUPtr clone() const override { return std::make_unique<ExtrusionPropertyOverhang>(*this); }
};

class ExtrusionPropertyZOffset : public ExtrusionProperty
{
public:
    coord_t z_offset = 0;

    ExtrusionPropertyZOffset() = default;
    explicit ExtrusionPropertyZOffset(coord_t offset) : z_offset(offset) {}

    ExtrusionPropertyUPtr clone() const override { return std::make_unique<ExtrusionPropertyZOffset>(*this); }
};

class ExtrusionPropertyLoopRole : public ExtrusionProperty
{
public:
    // if perimeter, this is the perimeter count. 0 = external, negative = not a perimeter.
    int16_t perimeter_idx;
    // Set of tags to identify the loop.
    // important one: elrHole => hole-perimeter, else it's a contour-perimeter
    ExtrusionLoopRole loop_role { elrDefault };

    ExtrusionPropertyLoopRole() = default;
    explicit ExtrusionPropertyLoopRole(ExtrusionLoopRole role) : loop_role(role) {}

    ExtrusionPropertyUPtr clone() const override { return std::make_unique<ExtrusionPropertyLoopRole>(*this); }
};

// Small typed property bag for extrusion interpretation modifiers.
// Most entities have no property, and the few that do usually carry one or two;
// a linear scan keeps the storage compact while making typed access explicit.
class ExtrusionPropertyContainer
{
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
        for (ExtrusionPropertyUPtr &property : m_properties)
            if (PropertyType *out = dynamic_cast<PropertyType*>(property.get()))
                return out;
        return nullptr;
    }

    template<typename PropertyType> const PropertyType* get_property() const
    {
        static_assert(std::is_base_of<ExtrusionProperty, PropertyType>::value, "PropertyType must inherit ExtrusionProperty");
        for (const ExtrusionPropertyUPtr &property : m_properties)
            if (const PropertyType *out = dynamic_cast<const PropertyType*>(property.get()))
                return out;
        return nullptr;
    }

    template<typename PropertyType, typename... Args> PropertyType& get_or_add_property(Args&&... args)
    {
        static_assert(std::is_base_of<ExtrusionProperty, PropertyType>::value, "PropertyType must inherit ExtrusionProperty");
        if (PropertyType *property = this->get_property<PropertyType>())
            return *property;
        ExtrusionPropertyUPtr property = std::make_unique<PropertyType>(std::forward<Args>(args)...);
        PropertyType *out = static_cast<PropertyType*>(property.get());
        m_properties.emplace_back(std::move(property));
        return *out;
    }

    template<typename PropertyType> bool remove_property()
    {
        static_assert(std::is_base_of<ExtrusionProperty, PropertyType>::value, "PropertyType must inherit ExtrusionProperty");
        for (ExtrusionPropertyUPtrs::iterator it = m_properties.begin(); it != m_properties.end(); ++ it)
            if (dynamic_cast<PropertyType*>(it->get()) != nullptr) {
                m_properties.erase(it);
                return true;
            }
        return false;
    }

protected:
    ExtrusionPropertyUPtrs m_properties;
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
