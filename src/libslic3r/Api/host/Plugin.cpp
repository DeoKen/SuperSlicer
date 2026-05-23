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

    const int32_t used_config_key_count = c_api.vt->used_config_keys(c_api.ctx, nullptr);
    if (used_config_key_count > 0) {
        std::vector<const char *> used_config_keys;
        used_config_keys.assign(size_t(used_config_key_count), nullptr);
        const int32_t written_count = c_api.vt->used_config_keys(c_api.ctx, used_config_keys.data());
        for (int32_t i = 0; i < written_count && i < used_config_key_count; ++i)
            if (used_config_keys[size_t(i)] != nullptr)
                m_used_config_keys.emplace_back(used_config_keys[size_t(i)]);
    }
}

} // namespace Slic3r
