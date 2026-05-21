///|/ Copyright (c) SuperSlicer 2026 Remi Durand @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "ExtrusionProperty.hpp"

#include <algorithm>
#include <cstring>

#include "Api/internal/ExtrusionPropertyAccess.hpp"
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

void RawBuffer::copy_from(const void *data, size_t byte_count, size_t alignment)
{
    this->allocate(byte_count, alignment);
    if (byte_count > 0 && data != nullptr)
        std::memcpy(m_data, data, byte_count);
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
        if (rhs.m_ops != nullptr) {
            rhs.m_ops->clone(*this, rhs.m_data.data());
        } else {
            m_data.copy_from(rhs.m_data.data(), rhs.m_data.size(), rhs.m_data.alignment());
            m_raw_type = rhs.m_raw_type;
        }
    }
}

PropertySlot::PropertySlot(PropertySlot &&rhs) noexcept
    : m_data(std::move(rhs.m_data))
    , m_ops(rhs.m_ops)
    , m_raw_type(rhs.m_raw_type)
{
    rhs.m_ops = nullptr;
    rhs.m_raw_type = extrusion_property_type_invalid;
}

PropertySlot& PropertySlot::operator=(const PropertySlot &rhs)
{
    if (this != &rhs) {
        this->reset();
        if (!rhs.empty()) {
            if (rhs.m_ops != nullptr) {
                rhs.m_ops->clone(*this, rhs.m_data.data());
            } else {
                m_data.copy_from(rhs.m_data.data(), rhs.m_data.size(), rhs.m_data.alignment());
                m_raw_type = rhs.m_raw_type;
            }
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
        m_raw_type = rhs.m_raw_type;
        rhs.m_ops = nullptr;
        rhs.m_raw_type = extrusion_property_type_invalid;
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
    m_raw_type = extrusion_property_type_invalid;
}

void PropertySlot::emplace_raw(extrusion_property_type type, const void *data, size_t byte_count, size_t alignment)
{
    this->reset();
    m_data.copy_from(data, byte_count, alignment);
    m_raw_type = type;
}

void PropertySlot::emplace_zeroed(extrusion_property_type type, size_t byte_count, size_t alignment)
{
    this->reset();
    m_data.allocate(byte_count, alignment);
    if (byte_count > 0)
        std::memset(m_data.data(), 0, byte_count);
    m_raw_type = type;
}

void* PropertySlot::data()
{
    if (m_ops == nullptr)
        return m_data.data();

    switch (m_ops->type) {
    case ExtrusionAttributes::property_type:
        return static_cast<ExtrusionFlow*>(this->get_if<ExtrusionAttributes>());
    case ExtrusionPropertySpeed::property_type:
        return &this->as<ExtrusionPropertySpeed>().speed_mm_per_s;
    case ExtrusionPropertyModifier::property_type:
        return &this->as<ExtrusionPropertyModifier>().enforce_travel;
    case ExtrusionPropertySpecialCommand::property_type:
        return &this->as<ExtrusionPropertySpecialCommand>().code;
    case ExtrusionPropertyOverhang::property_type:
        return &this->as<ExtrusionPropertyOverhang>().start_distance_from_prev_layer;
    case ExtrusionPropertyZOffset::property_type:
        return &this->as<ExtrusionPropertyZOffset>().z_offset;
    case ExtrusionPropertyLoopRole::property_type:
        return &this->as<ExtrusionPropertyLoopRole>().perimeter_idx;
    case ExtrusionPropertyCustomGcode::property_type:
    default:
        return nullptr;
    }
}

const void* PropertySlot::data() const
{
    return const_cast<PropertySlot*>(this)->data();
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
    , m_data_resources(rhs.m_data_resources)
    , m_next_data_resource_id(rhs.m_next_data_resource_id)
{
}

ExtrusionPropertyContainer& ExtrusionPropertyContainer::operator=(const ExtrusionPropertyContainer &rhs)
{
    if (this != &rhs) {
        m_properties = rhs.m_properties;
        m_data_resources = rhs.m_data_resources;
        m_next_data_resource_id = rhs.m_next_data_resource_id;
    }
    return *this;
}

ExtrusionPropertyUPtrs ExtrusionPropertyContainer::clone_properties() const
{
    ExtrusionPropertyUPtrs out;
    out.reserve(m_properties.size());
    for (const PropertySlot &property : m_properties)
        if (const ExtrusionProperty *cpp_property = property.property_or_null())
            out.emplace_back(cpp_property->clone());
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

ExtrusionPropertyContainer::DataResource::DataResource(const DataResource &rhs)
    : id(rhs.id)
{
    data.copy_from(rhs.data.data(), rhs.data.size(), rhs.data.alignment());
}

ExtrusionPropertyContainer::DataResource&
ExtrusionPropertyContainer::DataResource::operator=(const DataResource &rhs)
{
    if (this != &rhs) {
        id = rhs.id;
        data.copy_from(rhs.data.data(), rhs.data.size(), rhs.data.alignment());
    }
    return *this;
}

extrusion_property_type ExtrusionPropertyContainer::property_type_at(size_t idx) const
{
    return idx < m_properties.size() ? m_properties[idx].type() : extrusion_property_type_invalid;
}

const void* ExtrusionPropertyContainer::property_data(extrusion_property_type type) const
{
    const PropertySlot *slot = this->find_slot(type);
    return slot != nullptr ? slot->data() : nullptr;
}

void* ExtrusionPropertyContainer::property_data_mutable(extrusion_property_type type)
{
    PropertySlot *slot = this->find_slot(type);
    return slot != nullptr ? slot->data() : nullptr;
}

void* ExtrusionPropertyContainer::get_or_add_property_data_mutable(extrusion_property_type type, size_t byte_count, size_t alignment)
{
    if (PropertySlot *slot = this->find_slot(type))
        return slot->data();

    switch (type) {
    case ExtrusionAttributes::property_type:
        return static_cast<ExtrusionFlow*>(&this->get_or_add_property<ExtrusionAttributes>());
    case ExtrusionPropertySpeed::property_type:
        return &this->get_or_add_property<ExtrusionPropertySpeed>().speed_mm_per_s;
    case ExtrusionPropertyModifier::property_type:
        return &this->get_or_add_property<ExtrusionPropertyModifier>().enforce_travel;
    case ExtrusionPropertySpecialCommand::property_type:
        return &this->get_or_add_property<ExtrusionPropertySpecialCommand>(ExtrusionPropertySpecialCommand::Code::TOOLCHANGE).code;
    case ExtrusionPropertyOverhang::property_type:
        return &this->get_or_add_property<ExtrusionPropertyOverhang>().start_distance_from_prev_layer;
    case ExtrusionPropertyZOffset::property_type:
        return &this->get_or_add_property<ExtrusionPropertyZOffset>().z_offset;
    case ExtrusionPropertyLoopRole::property_type:
        return &this->get_or_add_property<ExtrusionPropertyLoopRole>().perimeter_idx;
    case ExtrusionPropertyCustomGcode::property_type:
        return nullptr;
    default:
        if (type == extrusion_property_type_invalid || byte_count == 0 || alignment == 0)
            return nullptr;
        PropertySlot raw_slot;
        raw_slot.emplace_zeroed(type, byte_count, alignment);
        m_properties.emplace_back(std::move(raw_slot));
        return m_properties.back().data();
    }
}

bool ExtrusionPropertyContainer::remove_property(extrusion_property_type type)
{
    for (std::vector<PropertySlot>::iterator it = m_properties.begin(); it != m_properties.end(); ++it)
        if (it->type() == type) {
            m_properties.erase(it);
            return true;
        }
    return false;
}

uint32_t ExtrusionPropertyContainer::store_data_aligned(const void *data, size_t byte_count, size_t alignment)
{
    if (alignment == 0 || (byte_count > 0 && data == nullptr))
        return uint32_t(-1);

    DataResource resource;
    resource.id = m_next_data_resource_id++;
    if (resource.id == uint32_t(-1))
        resource.id = m_next_data_resource_id++;
    resource.data.copy_from(data, byte_count, alignment);
    m_data_resources.emplace_back(std::move(resource));
    return m_data_resources.back().id;
}

const void* ExtrusionPropertyContainer::stored_data(uint32_t data_id, uint32_t *byte_size_out) const
{
    if (byte_size_out != nullptr)
        *byte_size_out = 0;
    for (const DataResource &resource : m_data_resources)
        if (resource.id == data_id) {
            if (byte_size_out != nullptr)
                *byte_size_out = uint32_t(resource.data.size());
            return resource.data.data();
        }
    return nullptr;
}

bool ExtrusionPropertyContainer::free_data(uint32_t data_id)
{
    for (std::vector<DataResource>::iterator it = m_data_resources.begin(); it != m_data_resources.end(); ++it)
        if (it->id == data_id) {
            m_data_resources.erase(it);
            return true;
        }
    return false;
}

namespace ApiInternal {

size_t ExtrusionPropertyAccess::property_count(const ExtrusionPropertyContainer &container)
{
    return container.property_count();
}

extrusion_property_type ExtrusionPropertyAccess::property_type_at(const ExtrusionPropertyContainer &container,
                                                                  size_t idx)
{
    return container.property_type_at(idx);
}

bool ExtrusionPropertyAccess::has_property(const ExtrusionPropertyContainer &container, extrusion_property_type type)
{
    return container.has_property(type);
}

const void *ExtrusionPropertyAccess::property_data(const ExtrusionPropertyContainer &container,
                                                   extrusion_property_type type)
{
    return container.property_data(type);
}

void *ExtrusionPropertyAccess::property_data_mutable(ExtrusionPropertyContainer &container,
                                                     extrusion_property_type type)
{
    return container.property_data_mutable(type);
}

void *ExtrusionPropertyAccess::get_or_add_property_data_mutable(ExtrusionPropertyContainer &container,
                                                                extrusion_property_type type,
                                                                size_t byte_count,
                                                                size_t alignment)
{
    return container.get_or_add_property_data_mutable(type, byte_count, alignment);
}

bool ExtrusionPropertyAccess::remove_property(ExtrusionPropertyContainer &container, extrusion_property_type type)
{
    return container.remove_property(type);
}

uint32_t ExtrusionPropertyAccess::store_data_aligned(ExtrusionPropertyContainer &container,
                                                     const void *data,
                                                     size_t byte_count,
                                                     size_t alignment)
{
    return container.store_data_aligned(data, byte_count, alignment);
}

const void *ExtrusionPropertyAccess::stored_data(const ExtrusionPropertyContainer &container,
                                                 uint32_t data_id,
                                                 uint32_t *byte_size_out)
{
    return container.stored_data(data_id, byte_size_out);
}

bool ExtrusionPropertyAccess::free_data(ExtrusionPropertyContainer &container, uint32_t data_id)
{
    return container.free_data(data_id);
}

} // namespace ApiInternal

} // namespace Slic3r
