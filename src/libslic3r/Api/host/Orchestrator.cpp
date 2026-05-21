///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "Orchestrator.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include "libslic3r/Api/plugin/c/slic3r_config_def.h"
#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/slic3r_plugin_types.h"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Plugins/BridgeDetector.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Steps/StepPipeline.hpp"

#include "ClipperShapes.hpp"
#include "Plugin.hpp"

namespace Slic3r {

Orchestrator *orchestrator_from_handle(orchestrator_handle *me) {
    return me == nullptr ? &Orchestrator::instance() : reinterpret_cast<Orchestrator *>(me);
}

const Orchestrator *orchestrator_from_handle(const orchestrator_handle *me) {
    return me == nullptr ? &Orchestrator::instance() : reinterpret_cast<const Orchestrator *>(me);
}

static ConfigOptionContainerType config_option_container_type(raw_container_type type)
{
    switch (type) {
    case RAW_CONTAINER_TYPE_PROJECT: return ConfigOptionContainerType::Project;
    case RAW_CONTAINER_TYPE_PLATER:  return ConfigOptionContainerType::Plater;
    case RAW_CONTAINER_TYPE_OBJECT:  return ConfigOptionContainerType::Object;
    case RAW_CONTAINER_TYPE_LAYER:   return ConfigOptionContainerType::Layer;
    case RAW_CONTAINER_TYPE_REGION:  return ConfigOptionContainerType::Region;
    case RAW_CONTAINER_TYPE_NONE:
    default:                         return ConfigOptionContainerType::None;
    }
}

} // namespace Slic3r

extern "C" {

option_def_error_code orchestrator_create_option_def(orchestrator_handle *me, const raw_config_option_def *def) {
    if (def == nullptr)
        return OPTION_DEF_ERROR_INVALID_ARGUMENT;

    // removed for now
    // if (def->struct_size < offsetof(raw_config_option_def, reserved_u64))
    //    return OPTION_DEF_ERROR_INVALID_ARGUMENT;

    if (def->opt_key == nullptr || def->default_serialized_value == nullptr ||
        def->container_type == RAW_CONTAINER_TYPE_NONE || def->option_preset_type == RAW_PRESET_TYPE_NONE ||
        def->printer_technology == RAW_PT_NONE)
        return OPTION_DEF_ERROR_INVALID_ARGUMENT;

    // TODO: check for OPTION_DEF_ERROR_ALREADY_EXISTS

    try {
        Slic3r::orchestrator_from_handle(me)->create_new_print_config(def);
        return OPTION_DEF_ERROR_OK;
    } catch (...) { return OPTION_DEF_ERROR_INTERNAL; }
}
}

namespace Slic3r {

Orchestrator &Orchestrator::instance() {
    static Orchestrator s_instance;
    static bool s_default_bridge_detector_registered = []() {
        slic3r_api::BridgeDetectorPlugin::register_bridge_detector_plugin(
            reinterpret_cast<orchestrator_handle *>(&s_instance));
        return true;
    }();
    (void) s_default_bridge_detector_registered;
    return s_instance;
}

bool Orchestrator::register_plugin(plugin_instance plugin) {
    const char *new_id = plugin.vt->get_id(plugin.ctx);
    const Plugin *check_exists = get_plugin(new_id);
    if (check_exists) {
        BOOST_LOG_TRIVIAL(error) << "Plugin with id " << new_id << " already exists, cannot register plugin"
                                 << std::endl;
        return false;
    }
    m_registered_plugins.emplace_back(new Plugin(plugin));
    return true;
}

extrusion_property_type Orchestrator::register_custom_extrusion_property(const char *namespaced_name,
                                                                         uint32_t byte_count,
                                                                         uint32_t alignment)
{
    if (const CustomExtrusionPropertyInfo *existing = this->custom_extrusion_property_info(namespaced_name)) {
        return existing->byte_count == byte_count && existing->alignment == alignment ?
            existing->type :
            EXTRUSION_PROPERTY_TYPE_INVALID;
    }

    CustomExtrusionPropertyInfo info;
    info.type = m_next_custom_extrusion_property_type++;
    if (info.type == EXTRUSION_PROPERTY_TYPE_INVALID)
        info.type = m_next_custom_extrusion_property_type++;
    info.name = namespaced_name;
    info.byte_count = byte_count;
    info.alignment = alignment;
    m_custom_extrusion_property_infos.emplace_back(std::move(info));
    return m_custom_extrusion_property_infos.back().type;
}

const Orchestrator::CustomExtrusionPropertyInfo*
Orchestrator::custom_extrusion_property_info(extrusion_property_type type) const
{
    for (const CustomExtrusionPropertyInfo &info : m_custom_extrusion_property_infos)
        if (info.type == type)
            return &info;
    return nullptr;
}

const Orchestrator::CustomExtrusionPropertyInfo*
Orchestrator::custom_extrusion_property_info(const char *namespaced_name) const
{
    if (namespaced_name == nullptr)
        return nullptr;

    for (const CustomExtrusionPropertyInfo &info : m_custom_extrusion_property_infos)
        if (info.name == namespaced_name)
            return &info;
    return nullptr;
}

plugin_host_context Orchestrator::prepare_plugin_host_context(slicing_step_t step,
                                                              Plugin *plugin,
                                                              Print *print)
{
    plugin_host_context context = {};
    context.orchestrator = this;
    context.print = print;
    context.plugin = plugin;
    context.step = step;
    return context;
}

plugin_run_context Orchestrator::prepare_plugin_run_context(slicing_step_t step,
                                                            Plugin *plugin,
                                                            plugin_host_context *host_context) {
    plugin_run_context context = {};
    context.step = step;
    context.plugin_storage = reinterpret_cast<storage_handle *>(&this->plugin_storage()[plugin]);
    context.host_context = host_context;
    context.is_cancelled = orchestrator_plugin_is_cancelled;
    context.report_warning = orchestrator_plugin_report_warning;
    context.report_error = orchestrator_plugin_report_error;
    context.report_progress = orchestrator_plugin_report_progress;
    return context;
}

bool Orchestrator::is_plugin_cancelled() const { return m_plugin_cancel_requested.load(std::memory_order_relaxed); }

void Orchestrator::request_plugin_cancel() { m_plugin_cancel_requested.store(true, std::memory_order_relaxed); }

void Orchestrator::reset_plugin_cancel() { m_plugin_cancel_requested.store(false, std::memory_order_relaxed); }

void Orchestrator::create_new_print_config(const raw_config_option_def *def) {
    //PrintOptionPresetType preset_type = static_cast<PrintOptionPresetType>(def->option_preset_type);
    //PrintOptionContainer container = static_cast<PrintOptionContainer>(def->container_type);

    ConfigOptionDef &out = *PrintConfigDef::instance_mutable().add(def->opt_key, static_cast<ConfigOptionType>(def->type));

    out.opt_key = def->opt_key;
    out.type = static_cast<ConfigOptionType>(def->type);
    out.category = static_cast<OptionCategory>(def->category);
    out.gui_type = static_cast<ConfigOptionDef::GUIType>(def->gui_type);
    out.printer_technology = static_cast<PrinterTechnology>(def->printer_technology);
    out.container_type = config_option_container_type(def->container_type);
    out.option_preset_type = static_cast<uint32_t>(def->option_preset_type);

    out.can_be_disabled = def->can_be_disabled != 0;
    out.is_optional = def->is_optional != 0;
    out.multiline = def->multiline != 0;
    out.full_width = def->full_width != 0;
    out.is_code = def->is_code != 0;
    out.is_vector_extruder = def->is_vector_extruder != 0;
    out.readonly = def->readonly != 0;
    out.can_phony = def->can_phony != 0;
    out.can_be_disabled = def->can_be_disabled != 0;
    out.aligned_label_left = def->aligned_label_left != 0;
    out.is_script = false;

    if (def->gui_flags)
        out.gui_flags = def->gui_flags;
    if (def->label)
        out.label = def->label;
    if (def->full_label)
        out.full_label = def->full_label;
    if (def->tooltip)
        out.tooltip = def->tooltip;
    if (def->sidetext)
        out.sidetext = def->sidetext;
    if (def->cli)
        out.cli = def->cli;
    if (def->ratio_over)
        out.ratio_over = def->ratio_over;

    out.height = def->height;
    out.width = def->width;
    out.label_width = def->label_width;
    out.sidetext_width = def->sidetext_width;

    if (def->has_min)
        out.min = def->min_value;
    if (def->has_max)
        out.max = def->max_value;

    if (def->has_max_literal) {
        out.max_literal = FloatOrPercent{def->max_literal_value, def->max_literal_is_percent != 0};
    }

    out.precision = def->precision;
    out.mode = static_cast<ConfigOptionMode>(def->mode);

    for (size_t i = 0; i < def->aliases.size; ++i) {
        const char *s = def->aliases.items[i];
        if (s)
            out.aliases.emplace_back(s);
    }

    for (size_t i = 0; i < def->shortcut.size; ++i) {
        const char *s = def->shortcut.items[i];
        if (s)
            out.shortcut.emplace_back(s);
    }

    for (size_t i = 0; i < def->depends_on.size; ++i) {
        const char *s = def->depends_on.items[i];
        if (s)
            out.depends_on.emplace_back(s);
    }

    ConfigOption *temp_default_option;
    switch (def->type) {
    case RAW_CO_NONE: assert(false); break;
    case RAW_CO_BOOL: temp_default_option = new ConfigOptionBool(); break;
    case RAW_CO_INT: temp_default_option = new ConfigOptionInt(); break;
    case RAW_CO_FLOAT: temp_default_option = new ConfigOptionFloat(); break;
    case RAW_CO_FLOAT_OR_PERCENT: temp_default_option = new ConfigOptionFloatOrPercent(); break;
    case RAW_CO_STRING: temp_default_option = new ConfigOptionString();
    case RAW_CO_POINT: temp_default_option = new ConfigOptionPoint(); break;
    case RAW_CO_ENUM: temp_default_option = new ConfigOptionEnumGeneric(); break;
    case RAW_CO_GRAPH: temp_default_option = new ConfigOptionGraph(); break;
    case RAW_CO_VECTOR_BOOL: temp_default_option = new ConfigOptionBools(); break;
    case RAW_CO_VECTOR_INT: temp_default_option = new ConfigOptionInts(); break;
    case RAW_CO_VECTOR_FLOAT: temp_default_option = new ConfigOptionFloats(); break;
    case RAW_CO_VECTOR_FLOAT_OR_PERCENT: temp_default_option = new ConfigOptionFloatsOrPercents(); break;
    case RAW_CO_VECTOR_STRING: temp_default_option = new ConfigOptionStrings(); break;
    case RAW_CO_VECTOR_POINT: temp_default_option = new ConfigOptionPoints(); break;
    case RAW_CO_VECTOR_ENUM: assert(false); break; // not implemented
    case RAW_CO_VECTOR_GRAPH: temp_default_option = new ConfigOptionGraphs(); break;
    default: assert(false);
    }
    temp_default_option->deserialize(def->default_serialized_value);
    out.set_default_value(temp_default_option);

    const bool has_pair_enum = def->enum_def.value_label_pairs.items != nullptr &&
        def->enum_def.value_label_pairs.count > 0;
    // const bool has_split_enum = def->enum_def.values.items != nullptr &&
    //                             def->enum_def.values.count > 0;

    if (has_pair_enum /*|| has_split_enum*/) {
        std::vector<std::string> values;
        std::vector<std::pair<std::string, std::string>> values_labels;

        // if (has_pair_enum) {
        values.reserve(def->enum_def.value_label_pairs.count);
        values_labels.reserve(def->enum_def.value_label_pairs.count);
        for (size_t i = 0; i < def->enum_def.value_label_pairs.count; ++i) {
            const key_value_string_pair_t &p = def->enum_def.value_label_pairs.items[i];
            values.emplace_back(p.value ? p.value : "");
            values_labels.emplace_back(values.back(), p.label ? p.label : "");
        }
        //} else {
        // values.reserve(def->enum_def.values.count);
        // for (size_t i = 0; i < def->enum_def.values.count; ++i)
        //    values.emplace_back(def->enum_def.values.items[i] ? def->enum_def.values.items[i] : "");

        // values_labels.reserve(def->enum_def.labels.count);
        // for (size_t i = 0; i < def->enum_def.labels.count; ++i)
        //     values_labels.emplace_back(values[i], def->enum_def.labels.items[i] ? def->enum_def.labels.items[i] : "");
        //}

        if (!values.empty()) {
            if (!values_labels.empty()) {
                out.set_enum_values(out.gui_type == ConfigOptionDef::GUIType::undefined ?
                                        ConfigOptionDef::GUIType::select_close :
                                        out.gui_type,
                                    values_labels);
            } else {
                out.set_enum_values(out.gui_type == ConfigOptionDef::GUIType::undefined ?
                                        ConfigOptionDef::GUIType::select_open :
                                        out.gui_type,
                                    values);
            }
        }
    }

    // publish it?
    PrintConfigDef::instance_mutable().option_keys(def->option_preset_type).insert(out.opt_key);
    if(def->option_preset_type == RAW_PRESET_TYPE_FFF_FILAMENT_OVERRIDE) {
        assert(false); // please do'nt yet, not made for that
    }
    if(def->option_preset_type == RAW_PRESET_TYPE_SLA_MATERIAL_OVERRIDE) {
        assert(false); // please do'nt yet, not made for that
    }
    if(def->option_preset_type == RAW_PRESET_TYPE_FFF_TOOL_EXTRUDER
        || def->option_preset_type == RAW_PRESET_TYPE_FFF_PRINTER_MACHINE_LIMITS
        || def->option_preset_type == RAW_PRESET_TYPE_FFF_TOOL_MILLING) {
        PrintConfigDef::instance_mutable().option_keys(RAW_PRESET_TYPE_FFF_PRINTER).insert(out.opt_key);
    }
    if(def->option_preset_type == RAW_PRESET_TYPE_FFF_TOOL_EXTRUDER_RETRACTION) {
        PrintConfigDef::instance_mutable().option_keys(RAW_PRESET_TYPE_FFF_TOOL_EXTRUDER).insert(out.opt_key);
        PrintConfigDef::instance_mutable().option_keys(RAW_PRESET_TYPE_FFF_PRINTER).insert(out.opt_key);
    }
    add_to_prusa_export_to_remove_keys(out.opt_key);
}

std::vector<Plugin *> Orchestrator::get_all_plugins_for_step(slicing_step_t step) const {
    std::vector<Plugin *> list;
    for (const std::unique_ptr<Plugin> &plugin : m_registered_plugins) {
        if (plugin->get_step() == step)
            list.push_back(plugin.get());
    }
    return list;
}

std::vector<Plugin *> Orchestrator::get_current_plugins_for_step(slicing_step_t step) const {
    auto it = m_plugins_by_step.find(step);
    return it == m_plugins_by_step.end() ? std::vector<Plugin *>{} : it->second;
}

const Plugin *Orchestrator::get_plugin(const std::string &plugin_id) const {
    const Plugin *plugin = nullptr;
    for (const std::unique_ptr<Plugin> &plugin_test : m_registered_plugins) {
        if (plugin_test->get_id() == plugin_id) {
            plugin = plugin_test.get();
            break;
        }
    }
    return plugin;
}

void Orchestrator::add_plugin_to_step(Plugin *plugin, slicing_step_t step) {
    if (!plugin) {
        return;
    }
    auto it_unique = m_plugins_by_step.find(step);
    if (it_unique != m_plugins_by_step.end()) {
        // replace
        it_unique->second = {plugin};
    } else {
        // create new
        std::vector<Plugin *> &plugins = m_plugins_by_step[step];
        // check not already inside, if so remove it.
        plugins.erase(std::remove(plugins.begin(), plugins.end(), plugin), plugins.end());
        // search best place
        // search from end until a dep is found
        // we have to put it between plugins[i-1] and plugins[i]
        size_t i = plugins.size();
        const std::vector<std::string> &deps = plugin->get_dependencies();
        for (; i > 0; i--) {
            const std::string candidate_id = plugins[i - 1]->get_id();
            for (const std::string &dep : deps) {
                if (candidate_id == dep) {
                    goto found_dep;
                }
            }
        }
    found_dep:
        // now move forward until the  priority is higher than our plugin's
        for (; i < plugins.size(); i++) {
            if (plugins[i]->get_priority() > plugin->get_priority()) {
                break;
            }
        }
        plugins.insert(plugins.begin() + i, plugin);
    }
}

bridge_detector_instance Orchestrator::create_bridge_detector(const bridge_detector_create_input &input) {
    bridge_detector_instance out = {};

    std::vector<Plugin *> plugins = this->get_current_plugins_for_step(BRIDGE_DETECTOR);
    if (plugins.empty())
        plugins = this->get_all_plugins_for_step(BRIDGE_DETECTOR);
    if (plugins.empty())
        return out;

    Plugin *plugin = *std::max_element(plugins.begin(), plugins.end(), [](const Plugin *lhs, const Plugin *rhs) {
        return lhs->get_priority() < rhs->get_priority();
    });

    plugin_host_context host_context = this->prepare_plugin_host_context(BRIDGE_DETECTOR, plugin);
    plugin_run_context context = this->prepare_plugin_run_context(BRIDGE_DETECTOR, plugin, &host_context);

    run_ctx_bridge_detector context_bd = {};
    context_bd.input = input;
    context.data = &context_bd;
    plugin->setup(context, 1);
    plugin->setup_run(context);
    plugin->run(context);
    out = context_bd.detector;
    return out;
}

void Orchestrator::slice(Print &print) {
    Steps::StepPipeline::run(*this, print);
}

void Orchestrator::initialize_plugins() {
    for (const std::unique_ptr<Plugin> &plugin_ptr : m_registered_plugins) {
        plugin_ptr->initialize(reinterpret_cast<storage_handle *>(&m_plugin_storage[plugin_ptr.get()]));
    }
}

PluginStorage::PluginStorage() = default;
PluginStorage::~PluginStorage() = default;

void PluginStorage::clear() {
    polylines.clear();
    polygons.clear();
    expolygons.clear();
    polyline_collections.clear();
    polygon_collections.clear();
    expolygon_collections.clear();
    extrusions.clear();
    clipper_shapes.clear();
    generic_storage.clear();
}

bool PluginStorage::contains(void *ptr) const {
    return ptr != nullptr && generic_storage.find(ptr) != generic_storage.end();
}

size_t PluginStorage::size() const { return generic_storage.size(); }

bool PluginStorage::free(void *ptr) {
    if (!contains(ptr)) {
        assert(false);
        return false;
    }

    for (auto it = polygons.begin(); it != polygons.end(); ++it) {
        if (ptr == static_cast<void *>(it->get())) {
            polygons.erase(it);
            generic_storage.erase(ptr);
            return true;
        }
    }

    for (auto it = polylines.begin(); it != polylines.end(); ++it) {
        if (ptr == static_cast<void *>(it->get())) {
            polylines.erase(it);
            generic_storage.erase(ptr);
            return true;
        }
    }

    for (auto it = expolygons.begin(); it != expolygons.end(); ++it) {
        if (ptr == static_cast<void *>(it->get())) {
            expolygons.erase(it);
            generic_storage.erase(ptr);
            return true;
        }
    }

    for (auto it = polyline_collections.begin(); it != polyline_collections.end(); ++it) {
        if (ptr == static_cast<void *>(it->get())) {
            polyline_collections.erase(it);
            generic_storage.erase(ptr);
            return true;
        }
    }

    for (auto it = polygon_collections.begin(); it != polygon_collections.end(); ++it) {
        if (ptr == static_cast<void *>(it->get())) {
            polygon_collections.erase(it);
            generic_storage.erase(ptr);
            return true;
        }
    }

    for (auto it = expolygon_collections.begin(); it != expolygon_collections.end(); ++it) {
        if (ptr == static_cast<void *>(it->get())) {
            expolygon_collections.erase(it);
            generic_storage.erase(ptr);
            return true;
        }
    }

    for (auto it = extrusions.begin(); it != extrusions.end(); ++it) {
        if (ptr == static_cast<void *>(it->get())) {
            extrusions.erase(it);
            generic_storage.erase(ptr);
            return true;
        }
    }

    for (auto it = clipper_shapes.begin(); it != clipper_shapes.end(); ++it) {
        if (ptr == static_cast<void *>(it->get())) {
            clipper_shapes.erase(it);
            generic_storage.erase(ptr);
            return true;
        }
    }

    generic_storage.erase(ptr);
    return false;
}

} // namespace Slic3r
