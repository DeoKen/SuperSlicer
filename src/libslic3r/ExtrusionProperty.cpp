///|/ Copyright (c) SuperSlicer 2026 Remi Durand @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "ExtrusionProperty.hpp"

#include <algorithm>
#include <cstring>

#include "Flow.hpp"

namespace Slic3r {

namespace {

PropertySlot slot_from_property(const ExtrusionProperty &property)
{
    PropertySlot slot;
    switch (property.type()) {
    case ExtrusionAttributes::property_type:
        slot.emplace<ExtrusionAttributes>(static_cast<const ExtrusionAttributes&>(property));
        break;
    case ExtrusionPropertySpeed::property_type:
        slot.emplace<ExtrusionPropertySpeed>(static_cast<const ExtrusionPropertySpeed&>(property));
        break;
    case ExtrusionPropertyModifier::property_type:
        slot.emplace<ExtrusionPropertyModifier>(static_cast<const ExtrusionPropertyModifier&>(property));
        break;
    case ExtrusionPropertyCustomGcode::property_type:
        slot.emplace<ExtrusionPropertyCustomGcode>(static_cast<const ExtrusionPropertyCustomGcode&>(property));
        break;
    case ExtrusionPropertySpecialCommand::property_type:
        slot.emplace<ExtrusionPropertySpecialCommand>(static_cast<const ExtrusionPropertySpecialCommand&>(property));
        break;
    case ExtrusionPropertyOverhang::property_type:
        slot.emplace<ExtrusionPropertyOverhang>(static_cast<const ExtrusionPropertyOverhang&>(property));
        break;
    case ExtrusionPropertyZOffset::property_type:
        slot.emplace<ExtrusionPropertyZOffset>(static_cast<const ExtrusionPropertyZOffset&>(property));
        break;
    case ExtrusionPropertyLoopRole::property_type:
        slot.emplace<ExtrusionPropertyLoopRole>(static_cast<const ExtrusionPropertyLoopRole&>(property));
        break;
    default:
        assert(false);
        break;
    }
    return slot;
}

PropertySlot slot_from_property(ExtrusionProperty &&property)
{
    PropertySlot slot;
    switch (property.type()) {
    case ExtrusionAttributes::property_type:
        slot.emplace<ExtrusionAttributes>(std::move(static_cast<ExtrusionAttributes&>(property)));
        break;
    case ExtrusionPropertySpeed::property_type:
        slot.emplace<ExtrusionPropertySpeed>(std::move(static_cast<ExtrusionPropertySpeed&>(property)));
        break;
    case ExtrusionPropertyModifier::property_type:
        slot.emplace<ExtrusionPropertyModifier>(std::move(static_cast<ExtrusionPropertyModifier&>(property)));
        break;
    case ExtrusionPropertyCustomGcode::property_type:
        slot.emplace<ExtrusionPropertyCustomGcode>(std::move(static_cast<ExtrusionPropertyCustomGcode&>(property)));
        break;
    case ExtrusionPropertySpecialCommand::property_type:
        slot.emplace<ExtrusionPropertySpecialCommand>(std::move(static_cast<ExtrusionPropertySpecialCommand&>(property)));
        break;
    case ExtrusionPropertyOverhang::property_type:
        slot.emplace<ExtrusionPropertyOverhang>(std::move(static_cast<ExtrusionPropertyOverhang&>(property)));
        break;
    case ExtrusionPropertyZOffset::property_type:
        slot.emplace<ExtrusionPropertyZOffset>(std::move(static_cast<ExtrusionPropertyZOffset&>(property)));
        break;
    case ExtrusionPropertyLoopRole::property_type:
        slot.emplace<ExtrusionPropertyLoopRole>(std::move(static_cast<ExtrusionPropertyLoopRole&>(property)));
        break;
    default:
        assert(false);
        break;
    }
    return slot;
}

} // namespace

ExtrusionFlow::ExtrusionFlow(const Flow &flow)
    : mm3_per_mm(flow.mm3_per_mm())
    , width(flow.width())
    , height(flow.height())
{
}

ExtrusionAttributes::ExtrusionAttributes(ExtrusionRole role, const Flow &flow)
    : ExtrusionFlow{ flow }
    , role{ role }
{
}

ExtrusionPropertyCustomGcode::ExtrusionPropertyCustomGcode(const std::string &str)
    : code(Code::GCODE)
    , gcode(str)
{
    if (!str.empty() && str[0] == ';') {
        code = Code::COMMENT;
        if (str.size() > 1 && str[1] == ' ')
            gcode = gcode.substr(2);
        else
            gcode = gcode.substr(1);
    }
}

RawBuffer::RawBuffer(RawBuffer &&rhs) noexcept
    : m_data(rhs.m_data)
    , m_size(rhs.m_size)
    , m_alignment(rhs.m_alignment)
{
    rhs.m_data = nullptr;
    rhs.m_size = 0;
    rhs.m_alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
}

RawBuffer& RawBuffer::operator=(RawBuffer &&rhs) noexcept
{
    if (this != &rhs) {
        this->reset();
        m_data = rhs.m_data;
        m_size = rhs.m_size;
        m_alignment = rhs.m_alignment;
        rhs.m_data = nullptr;
        rhs.m_size = 0;
        rhs.m_alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
    }
    return *this;
}

RawBuffer::~RawBuffer()
{
    this->reset();
}

void RawBuffer::allocate(size_t byte_count, size_t alignment)
{
    this->reset();
    if (byte_count == 0)
        return;
    m_data = ::operator new(byte_count, std::align_val_t(alignment));
    m_size = byte_count;
    m_alignment = alignment;
}

void RawBuffer::reset()
{
    if (m_data != nullptr) {
        ::operator delete(m_data, std::align_val_t(m_alignment));
        m_data = nullptr;
    }
    m_size = 0;
    m_alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
}

PropertySlot::PropertySlot(const PropertySlot &rhs)
{
    if (!rhs.empty()) {
        assert(rhs.m_ops != nullptr);
        rhs.m_ops->clone(*this, rhs.m_data.data());
    }
}

PropertySlot::PropertySlot(PropertySlot &&rhs) noexcept
    : m_data(std::move(rhs.m_data))
    , m_ops(rhs.m_ops)
{
    rhs.m_ops = nullptr;
}

PropertySlot& PropertySlot::operator=(const PropertySlot &rhs)
{
    if (this != &rhs) {
        this->reset();
        if (!rhs.empty()) {
            assert(rhs.m_ops != nullptr);
            rhs.m_ops->clone(*this, rhs.m_data.data());
        }
    }
    return *this;
}

PropertySlot& PropertySlot::operator=(PropertySlot &&rhs) noexcept
{
    if (this != &rhs) {
        this->reset();
        m_data = std::move(rhs.m_data);
        m_ops = rhs.m_ops;
        rhs.m_ops = nullptr;
    }
    return *this;
}

PropertySlot::~PropertySlot()
{
    this->reset();
}

void PropertySlot::reset()
{
    if (m_ops != nullptr && m_data.data() != nullptr)
        m_ops->destroy(m_data.data());
    m_data.reset();
    m_ops = nullptr;
}

ExtrusionPropertyContainer::ExtrusionPropertyContainer(ExtrusionPropertyUPtr &&property)
{
    if (property)
        m_properties.emplace_back(slot_from_property(std::move(*property)));
}

ExtrusionPropertyContainer::ExtrusionPropertyContainer(ExtrusionPropertyUPtrs &&properties)
{
    m_properties.reserve(properties.size());
    for (ExtrusionPropertyUPtr &property : properties) {
        if (property)
            m_properties.emplace_back(slot_from_property(std::move(*property)));
    }
}

ExtrusionPropertyContainer::ExtrusionPropertyContainer(const ExtrusionPropertyContainer &rhs)
    : m_properties(rhs.m_properties)
{
}

ExtrusionPropertyContainer& ExtrusionPropertyContainer::operator=(const ExtrusionPropertyContainer &rhs)
{
    if (this != &rhs) {
        m_properties = rhs.m_properties;
    }
    return *this;
}

ExtrusionPropertyUPtrs ExtrusionPropertyContainer::clone_properties() const
{
    ExtrusionPropertyUPtrs out;
    out.reserve(m_properties.size());
    for (const PropertySlot &property : m_properties)
        out.emplace_back(property.property().clone());
    return out;
}

ExtrusionProperty& ExtrusionPropertyContainer::add_property(const ExtrusionProperty &property)
{
    PropertySlot slot = slot_from_property(property);
    PropertySlot *stored = this->find_slot(slot.type());
    if (stored != nullptr) {
        *stored = std::move(slot);
        return stored->property();
    }
    m_properties.emplace_back(std::move(slot));
    return m_properties.back().property();
}

ExtrusionProperty& ExtrusionPropertyContainer::add_property(ExtrusionPropertyUPtr &&property)
{
    assert(property != nullptr);
    PropertySlot slot = slot_from_property(std::move(*property));
    PropertySlot *stored = this->find_slot(slot.type());
    if (stored != nullptr) {
        *stored = std::move(slot);
        return stored->property();
    }
    m_properties.emplace_back(std::move(slot));
    return m_properties.back().property();
}

PropertySlot* ExtrusionPropertyContainer::find_slot(extrusion_property_type type)
{
    for (PropertySlot &slot : m_properties)
        if (slot.type() == type)
            return &slot;
    return nullptr;
}

const PropertySlot* ExtrusionPropertyContainer::find_slot(extrusion_property_type type) const
{
    for (const PropertySlot &slot : m_properties)
        if (slot.type() == type)
            return &slot;
    return nullptr;
}

} // namespace Slic3r
