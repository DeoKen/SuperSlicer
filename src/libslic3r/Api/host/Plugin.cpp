///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "Plugin.hpp"

namespace Slic3r {

Plugin::Plugin(plugin_instance c_api) : m_c_api(c_api) {
    this->m_id = c_api.vt->get_id(c_api.ctx);
    this->m_step = c_api.vt->get_step(c_api.ctx);
    this->m_priority = c_api.vt->get_priority(c_api.ctx);
    const_strings_t cstrings = c_api.vt->get_dependencies(c_api.ctx);
    if (cstrings.items) {
        for (size_t i = 0; i < cstrings.size; ++i) {
            m_dependencies.emplace_back(cstrings.items[i]);
        }
    }
}

} // namespace Slic3r
