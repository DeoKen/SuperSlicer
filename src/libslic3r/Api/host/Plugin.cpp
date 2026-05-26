///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "Plugin.hpp"

#include <stdexcept>
#include <string>

namespace Slic3r {

namespace {

const int32_t MAX_USED_CONFIG_KEYS = 1024;

void validate_plugin_instance(const plugin_instance &c_api)
{
    if (c_api.vt == nullptr)
        throw std::runtime_error("Plugin instance has no vtable.");
    if (c_api.vt->abi_version != SLIC3R_PLUGIN_ABI_VERSION)
        throw std::runtime_error(
            "Plugin ABI version mismatch: plugin ABI " + std::to_string(c_api.vt->abi_version) +
            ", host ABI " + std::to_string(SLIC3R_PLUGIN_ABI_VERSION) +
            ". Plugin id is unavailable because the vtable layout may be incompatible.");
    if (c_api.vt->get_id == nullptr || c_api.vt->get_name == nullptr ||
        c_api.vt->get_description == nullptr || c_api.vt->get_step == nullptr ||
        c_api.vt->get_dependencies == nullptr || c_api.vt->get_priority == nullptr ||
        c_api.vt->initialize == nullptr || c_api.vt->setup == nullptr ||
        c_api.vt->setup_run == nullptr || c_api.vt->run == nullptr)
        throw std::runtime_error("Plugin vtable has missing callbacks.");
}

} // namespace

Plugin::Plugin(plugin_instance c_api) : m_c_api(c_api) {
    validate_plugin_instance(c_api);

    const char *plugin_id = c_api.vt->get_id(c_api.ctx);
    const char *plugin_name = c_api.vt->get_name(c_api.ctx);
    const char *plugin_description = c_api.vt->get_description(c_api.ctx);
    this->m_id = plugin_id != nullptr ? plugin_id : "";
    this->m_name = plugin_name != nullptr && plugin_name[0] != '\0' ? plugin_name : this->m_id;
    this->m_description = plugin_description != nullptr ? plugin_description : "";
    this->m_step = c_api.vt->get_step(c_api.ctx);
    this->m_priority = c_api.vt->get_priority(c_api.ctx);
    const_strings_t cstrings = c_api.vt->get_dependencies(c_api.ctx);
    if (cstrings.items) {
        for (size_t i = 0; i < cstrings.size; ++i) {
            m_dependencies.emplace_back(cstrings.items[i]);
        }
    }

    const int32_t used_config_key_count = c_api.vt->used_config_keys != nullptr ?
        c_api.vt->used_config_keys(c_api.ctx, nullptr) :
        0;
    if (used_config_key_count < 0 || used_config_key_count > MAX_USED_CONFIG_KEYS)
        throw std::runtime_error("Plugin returned an invalid used_config_keys count.");

    if (used_config_key_count > 0) {
        std::vector<const char *> used_config_keys;
        used_config_keys.assign(size_t(used_config_key_count), nullptr);
        const int32_t written_count = c_api.vt->used_config_keys(c_api.ctx, used_config_keys.data());
        if (written_count < 0 || written_count > used_config_key_count)
            throw std::runtime_error("Plugin wrote an invalid used_config_keys count.");

        for (int32_t i = 0; i < written_count; ++i)
            if (used_config_keys[size_t(i)] != nullptr)
                m_used_config_keys.emplace_back(used_config_keys[size_t(i)]);
    }
}

} // namespace Slic3r
