///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Plugin_hpp_
#define slic3r_Plugin_hpp_

#include <string>
#include <vector>

#include "libslic3r/Api/plugin/c/slic3r_plugin.h"
#include "libslic3r/Api/plugin/c/slic3r_plugin_types.h"

namespace Slic3r {

class Plugin
{
protected:
    plugin_instance m_c_api;
    std::string m_id;
    std::string m_name;
    std::string m_description;
    std::string m_exclusive_group;
    std::string m_exclusive_group_label;
    std::string m_exclusive_group_tooltip;
    slicing_step_t m_step;
    std::vector<std::string> m_dependencies;
    std::vector<std::string> m_used_config_keys;
    std::vector<std::string> m_defined_config_keys;
    int m_priority;

public:
    Plugin(plugin_instance c_api);

    const std::string& get_id() const noexcept { return m_id; }
    const std::string& get_name() const noexcept { return m_name; }
    const std::string& get_description() const noexcept { return m_description; }
    const std::string& get_exclusive_group() const noexcept { return m_exclusive_group; }
    const std::string& get_exclusive_group_label() const noexcept { return m_exclusive_group_label; }
    const std::string& get_exclusive_group_tooltip() const noexcept { return m_exclusive_group_tooltip; }
    slicing_step_t get_step() const noexcept { return m_step; }
    const std::vector<std::string>& get_dependencies() const noexcept { return m_dependencies; }
    const std::vector<std::string>& get_used_config_keys() const noexcept { return m_used_config_keys; }
    const std::vector<std::string>& get_defined_config_keys() const noexcept { return m_defined_config_keys; }
    int get_priority() const noexcept { return m_priority; }
    void initialize(storage_handle *storage) const {
        m_c_api.vt->initialize(m_c_api.ctx, storage);
    }
    void setup(const plugin_run_context &context, uint32_t run_count) const {
        m_c_api.vt->setup(m_c_api.ctx, &context, run_count);
    }
    void setup_run(const plugin_run_context &context) const { m_c_api.vt->setup_run(m_c_api.ctx, &context); }
    void run(const plugin_run_context &context) const { m_c_api.vt->run(m_c_api.ctx, &context); }

};

} // namespace Slic3r



#endif // slic3r_plugin_hpp_
