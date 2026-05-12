///|/ Copyright (c) SuperSlicer 2026 Remi Durand @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "ExtrusionProperty.hpp"

#include "Flow.hpp"

namespace Slic3r {

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

ExtrusionPropertyContainer::ExtrusionPropertyContainer(ExtrusionPropertyUPtr &&property)
{
    if (property)
        m_properties.emplace_back(std::move(property));
}

ExtrusionPropertyContainer::ExtrusionPropertyContainer(ExtrusionPropertyUPtrs &&properties)
    : m_properties(std::move(properties))
{
}

ExtrusionPropertyContainer::ExtrusionPropertyContainer(const ExtrusionPropertyContainer &rhs)
{
    m_properties.reserve(rhs.m_properties.size());
    for (const ExtrusionPropertyUPtr &property : rhs.m_properties)
        m_properties.emplace_back(property->clone());
}

ExtrusionPropertyContainer& ExtrusionPropertyContainer::operator=(const ExtrusionPropertyContainer &rhs)
{
    if (this != &rhs) {
        m_properties.clear();
        m_properties.reserve(rhs.m_properties.size());
        for (const ExtrusionPropertyUPtr &property : rhs.m_properties)
            m_properties.emplace_back(property->clone());
    }
    return *this;
}

ExtrusionPropertyUPtrs ExtrusionPropertyContainer::clone_properties() const
{
    ExtrusionPropertyUPtrs out;
    out.reserve(m_properties.size());
    for (const ExtrusionPropertyUPtr &property : m_properties)
        out.emplace_back(property->clone());
    return out;
}

ExtrusionProperty& ExtrusionPropertyContainer::add_property(const ExtrusionProperty &property)
{
    ExtrusionPropertyUPtr clone = property.clone();
    return this->add_property(std::move(clone));
}

ExtrusionProperty& ExtrusionPropertyContainer::add_property(ExtrusionPropertyUPtr &&property)
{
    assert(property != nullptr);
    for (ExtrusionPropertyUPtr &stored : m_properties)
        if (typeid(*stored) == typeid(*property)) {
            stored = std::move(property);
            return *stored;
        }
    m_properties.emplace_back(std::move(property));
    return *m_properties.back();
}

} // namespace Slic3r
