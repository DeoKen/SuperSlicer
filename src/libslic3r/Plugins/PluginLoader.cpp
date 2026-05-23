///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "PluginLoader.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include <map>
#include <string>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Plugins/MaxOverhangThreshold.hpp"
#include "libslic3r/Plugins/SliceVolume.hpp"
#include "libslic3r/Plugins/StandardLayerHeightGenerator.hpp"
#include "libslic3r/Plugins/Support/SupportDemandBridgeRemoval.hpp"
#include "libslic3r/Steps/StepPipeline.hpp"
#include "libslic3r/Utils.hpp"

namespace Slic3r {
namespace {

using RegisterPluginFn = void (*)(orchestrator_handle *);

const char *const PLUGIN_ACTIVATION_DIR = "plugin";
const char *const ACTIVATED_PLUGINS_FILENAME = "activated.ini";
const char *const DEFAULT_ACTIVATED_PLUGINS_DIR = "plugins";
const char *const DEFAULT_ACTIVATED_PLUGINS_FILENAME = "default_activated.ini";

bool ini_value_is_enabled(const std::string &value)
{
    return boost::algorithm::iequals(value, "1") ||
           boost::algorithm::iequals(value, "true") ||
           boost::algorithm::iequals(value, "yes") ||
           boost::algorithm::iequals(value, "on") ||
           boost::algorithm::iequals(value, "enabled");
}

std::vector<std::string> read_active_plugin_ini(const boost::filesystem::path &config_path)
{
    boost::nowide::ifstream stream(config_path.string());
    if (!stream) {
        BOOST_LOG_TRIVIAL(warning) << "Cannot read active plugin configuration '" << config_path.string() << "'.";
        return {};
    }

    boost::property_tree::ptree tree;
    try {
        boost::property_tree::read_ini(stream, tree);
    } catch (const boost::property_tree::ini_parser_error &error) {
        BOOST_LOG_TRIVIAL(warning) << "Cannot parse active plugin configuration '" << config_path.string()
                                   << "': " << error.what();
        return {};
    }

    const boost::property_tree::ptree &const_tree = tree;
    const boost::optional<const boost::property_tree::ptree&> activated = const_tree.get_child_optional("activated");
    if (!activated) {
        BOOST_LOG_TRIVIAL(warning) << "Active plugin configuration '" << config_path.string()
                                   << "' has no [activated] section.";
        return {};
    }

    std::vector<std::string> plugin_ids;
    for (const boost::property_tree::ptree::value_type &entry : *activated) {
        const std::string plugin_id = boost::algorithm::trim_copy(entry.first);
        if (!plugin_id.empty() && ini_value_is_enabled(entry.second.get_value<std::string>()))
            plugin_ids.push_back(plugin_id);
    }

    return plugin_ids;
}

boost::filesystem::path default_active_plugin_config_path()
{
    return boost::filesystem::path(Slic3r::resources_dir()) / DEFAULT_ACTIVATED_PLUGINS_DIR / DEFAULT_ACTIVATED_PLUGINS_FILENAME;
}

boost::filesystem::path active_plugin_config_path(const boost::filesystem::path &config_dir)
{
    return config_dir / PLUGIN_ACTIVATION_DIR / ACTIVATED_PLUGINS_FILENAME;
}

boost::filesystem::path ensure_active_plugin_config(const boost::filesystem::path &config_dir,
                                                    bool &from_user_config)
{
    const boost::filesystem::path default_config_path = default_active_plugin_config_path();
    from_user_config = false;

    if (config_dir.empty()) {
        BOOST_LOG_TRIVIAL(trace) << "data_dir is not available before plugin activation. Using default active plugin "
                                    "configuration from resources.";
        return default_config_path;
    }

    const boost::filesystem::path config_path = active_plugin_config_path(config_dir);
    if (boost::filesystem::exists(config_path)) {
        from_user_config = true;
        return config_path;
    }

    try {
        boost::filesystem::create_directories(config_path.parent_path());
        boost::filesystem::copy_file(default_config_path, config_path);
        from_user_config = true;
        return config_path;
    } catch (const boost::filesystem::filesystem_error &error) {
        BOOST_LOG_TRIVIAL(warning) << "Cannot create active plugin configuration '" << config_path.string()
                                   << "' from '" << default_config_path.string() << "': " << error.what()
                                   << ". Falling back to resources.";
        return default_config_path;
    }
}

std::vector<std::string> read_active_plugin_ids(const boost::filesystem::path &config_dir,
                                                bool &from_user_config)
{
    const boost::filesystem::path config_path = ensure_active_plugin_config(config_dir, from_user_config);
    return read_active_plugin_ini(config_path);
}

void activate_plugins_from_ids(Orchestrator &orchestrator,
                               const std::vector<std::string> &plugin_ids,
                               bool from_user_config)
{
    orchestrator.clear_active_plugins();

    for (const std::string &plugin_id : plugin_ids) {
        if (orchestrator.set_plugin_active(plugin_id, true)) {
            BOOST_LOG_TRIVIAL(info) << "Activated plugin '" << plugin_id << "'.";
            continue;
        }

        if (from_user_config)
            BOOST_LOG_TRIVIAL(warning) << "Active plugin '" << plugin_id << "' is listed in "
                                       << ACTIVATED_PLUGINS_FILENAME << " but is not loaded.";
        else
            BOOST_LOG_TRIVIAL(trace) << "Default active plugin '" << plugin_id << "' is not loaded.";
    }
}

bool is_plugin_library_path(const boost::filesystem::path &path)
{
#ifdef _WIN32
    return boost::algorithm::iequals(path.extension().string(), ".dll");
#elif defined(__APPLE__)
    return path.extension() == ".dylib";
#else
    return path.extension() == ".so";
#endif
}

void load_plugin_library(const boost::filesystem::path &plugin_path, orchestrator_handle *orchestrator)
{
#ifdef _WIN32
    static std::vector<HMODULE> loaded_modules;
    HMODULE module = LoadLibraryW(plugin_path.wstring().c_str());
    if (module == NULL) {
        BOOST_LOG_TRIVIAL(warning) << "Cannot load plugin DLL '" << plugin_path.string()
                                   << "': error " << GetLastError();
        return;
    }

    FARPROC farproc = GetProcAddress(module, "register_plugin");
    if (farproc == NULL) {
        BOOST_LOG_TRIVIAL(warning) << "Plugin DLL '" << plugin_path.string()
                                   << "' does not export register_plugin().";
        FreeLibrary(module);
        return;
    }

    RegisterPluginFn register_plugin_fn = reinterpret_cast<RegisterPluginFn>(farproc);
    register_plugin_fn(orchestrator);
    loaded_modules.push_back(module);
#else
    static std::vector<void *> loaded_modules;
    void *module = dlopen(plugin_path.string().c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (module == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << "Cannot load plugin library '" << plugin_path.string()
                                   << "': " << dlerror();
        return;
    }

    void *symbol = dlsym(module, "register_plugin");
    if (symbol == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << "Plugin library '" << plugin_path.string()
                                   << "' does not export register_plugin(): " << dlerror();
        dlclose(module);
        return;
    }

    RegisterPluginFn register_plugin_fn = reinterpret_cast<RegisterPluginFn>(symbol);
    register_plugin_fn(orchestrator);
    loaded_modules.push_back(module);
#endif
}

void load_plugins_from_repository(const boost::filesystem::path &repository, orchestrator_handle *orchestrator)
{
    if (!boost::filesystem::exists(repository)) {
        BOOST_LOG_TRIVIAL(trace) << "Plugin repository '" << repository.string() << "' does not exist.";
        return;
    }
    if (!boost::filesystem::is_directory(repository)) {
        BOOST_LOG_TRIVIAL(warning) << "Plugin repository path '" << repository.string() << "' is not a directory.";
        return;
    }

    for (boost::filesystem::directory_iterator it(repository), end; it != end; ++it) {
        const boost::filesystem::path plugin_path = it->path();
        if (boost::filesystem::is_regular_file(plugin_path) && is_plugin_library_path(plugin_path)) {
            BOOST_LOG_TRIVIAL(info) << "Loading plugin '" << plugin_path.string() << "'.";
            load_plugin_library(plugin_path, orchestrator);
        }
    }
}

void register_builtin_plugins(orchestrator_handle *orchestrator)
{
    slic3r_api::StandardLayerHeightGeneratorPlugin::register_standard_layer_height_generator_plugin(orchestrator);
    slic3r_api::SliceVolumePlugin::register_slice_volume_plugin(orchestrator);
    slic3r_api::MaxOverhangThresholdPlugin::register_max_overhang_threshold_plugin(orchestrator);
    slic3r_api::Support::SupportDemandBridgeRemovalPlugin::register_support_demand_bridge_removal_plugin(orchestrator);
}

void add_exclusive_step_used_setting_rules(Orchestrator &orchestrator,
                                           const Steps::StepExclusiveGroup &group,
                                           const std::vector<Plugin *> &plugins)
{
    for (size_t plugin_idx = 0; plugin_idx < plugins.size(); ++plugin_idx) {
        const Plugin *plugin = plugins[plugin_idx];
        for (const std::string &setting_key : plugin->get_used_config_keys()) {
            raw_gui_rule rule = raw_gui_rule_init();
            rule.action = RAW_GUI_RULE_ACTION_ENABLE_ANY;
            rule.condition = RAW_GUI_RULE_CONDITION_INT_EQUALS;
            rule.target_key = setting_key.c_str();
            rule.condition_key = group.option_def.opt_key;
            rule.condition_int_value = int32_t(plugin_idx);
            orchestrator.add_gui_rule(&rule);
        }
    }
}

void register_exclusive_step_groups(Orchestrator &orchestrator)
{
    const std::map<slicing_step_t, Steps::StepExclusiveGroup> &templates = Steps::get_exclusive_steps();
    for (const std::pair<const slicing_step_t, Steps::StepExclusiveGroup> &entry : templates) {
        const std::vector<Plugin *> active_plugins = orchestrator.get_active_plugins_for_step(entry.first);
        if (active_plugins.size() <= 1)
            continue;

        std::vector<std::string> plugin_ids;
        plugin_ids.reserve(active_plugins.size());
        for (const Plugin *plugin : active_plugins)
            plugin_ids.push_back(plugin->get_id());

        Steps::StepExclusiveGroup group = entry.second;
        group.set_enum_plugins(plugin_ids);
        orchestrator.create_new_print_config(&group.option_def);
        orchestrator.add_ui_fragment("print.ui", group.option_def.opt_key, group.ui_fragment.c_str(), 0);
        add_exclusive_step_used_setting_rules(orchestrator, group, active_plugins);
        for (const raw_gui_rule &rule : group.gui_activation_rules)
            orchestrator.add_gui_rule(&rule);
    }
}

} // namespace

void load_plugins()
{
    Orchestrator &orchestrator = Orchestrator::instance();
    orchestrator_handle *orchestrator_handle_ptr = reinterpret_cast<orchestrator_handle *>(&orchestrator);

    register_builtin_plugins(orchestrator_handle_ptr);
    load_plugins_from_repository(Slic3r::install_path() / "plugins", orchestrator_handle_ptr);

    bool active_plugins_loaded_from_user_config = false;
    const boost::filesystem::path config_dir = has_data_dir() ? boost::filesystem::path(data_dir()) :
                                                                boost::filesystem::path();
    const std::vector<std::string> active_plugin_ids =
        read_active_plugin_ids(config_dir, active_plugins_loaded_from_user_config);
    BOOST_LOG_TRIVIAL(info) << "Loaded " << active_plugin_ids.size() << " active plugin id(s) from "
                            << (active_plugins_loaded_from_user_config ? active_plugin_config_path(config_dir).string() :
                                default_active_plugin_config_path().string()) << ".";
    activate_plugins_from_ids(orchestrator, active_plugin_ids, active_plugins_loaded_from_user_config);
    register_exclusive_step_groups(orchestrator);

    orchestrator.initialize_plugins();
}

} // namespace Slic3r
