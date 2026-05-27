///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <algorithm>
#include <string>
#include <vector>

#include <boost/log/trivial.hpp>

#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Print.hpp"

#include "Orchestrator.hpp"
#include "Plugin.hpp"

extern "C" {

void orchestrator_register_plugin(orchestrator_handle *orch, plugin_instance plugin) {
    Slic3r::Orchestrator *orchestrator = orch == nullptr ? &Slic3r::Orchestrator::instance() :
                                                           reinterpret_cast<Slic3r::Orchestrator *>(orch);
    if (orchestrator != nullptr) {
        orchestrator->register_plugin(plugin);
    }
}

bridge_detector_instance orchestrator_create_bridge_detector(orchestrator_handle *orch,
                                                             const bridge_detector_create_input *input) {
    bridge_detector_instance out = {};
    if (input == nullptr)
        return out;
    Slic3r::Orchestrator *orchestrator = orch == nullptr ? &Slic3r::Orchestrator::instance() :
                                                           reinterpret_cast<Slic3r::Orchestrator *>(orch);
    return orchestrator == nullptr ? out : orchestrator->create_bridge_detector(*input);
}

int32_t orchestrator_add_ui_fragment(orchestrator_handle *orch,
                                     const char *target_file,
                                     const char *fragment_id,
                                     const char *ui_fragment,
                                     int32_t priority)
{
    if (target_file == nullptr || fragment_id == nullptr || ui_fragment == nullptr)
        return -1;

    try {
        Slic3r::Orchestrator *orchestrator = orch == nullptr ? &Slic3r::Orchestrator::instance() :
                                                               reinterpret_cast<Slic3r::Orchestrator *>(orch);
        if (orchestrator == nullptr)
            return -1;
        return orchestrator->add_ui_fragment(target_file, fragment_id, ui_fragment, priority) ? 1 : 0;
    } catch (...) {
        return -2;
    }
}

int32_t orchestrator_add_gui_rule(orchestrator_handle *orch, const raw_gui_rule *rule)
{
    if (rule == nullptr)
        return -1;

    try {
        Slic3r::Orchestrator *orchestrator = orch == nullptr ? &Slic3r::Orchestrator::instance() :
                                                               reinterpret_cast<Slic3r::Orchestrator *>(orch);
        if (orchestrator == nullptr)
            return -1;
        return orchestrator->add_gui_rule(rule) ? 1 : 0;
    } catch (...) {
        return -2;
    }
}

int orchestrator_plugin_is_cancelled(plugin_host_context *host_context)
{
    return host_context != nullptr && host_context->orchestrator != nullptr &&
                   host_context->orchestrator->is_plugin_cancelled() ?
               1 :
               0;
}

void orchestrator_plugin_report_warning(plugin_host_context *host_context, const char *message)
{
    const char *plugin_id = host_context != nullptr && host_context->plugin != nullptr ?
                                host_context->plugin->get_id().c_str() :
                                "<unknown>";
    BOOST_LOG_TRIVIAL(warning) << "Plugin warning from " << plugin_id << ": " << (message != nullptr ? message : "");

    if (host_context != nullptr && host_context->orchestrator != nullptr)
        host_context->orchestrator->add_plugin_message(Slic3r::Orchestrator::PluginMessageLevel::Warning,
                                                       host_context->plugin,
                                                       host_context->step,
                                                       message);
}

void orchestrator_plugin_report_error(plugin_host_context *host_context, const char *message)
{
    const char *plugin_id = host_context != nullptr && host_context->plugin != nullptr ?
                                host_context->plugin->get_id().c_str() :
                                "<unknown>";
    BOOST_LOG_TRIVIAL(error) << "Plugin error from " << plugin_id << ": " << (message != nullptr ? message : "");

    if (host_context != nullptr && host_context->orchestrator != nullptr) {
        host_context->orchestrator->add_plugin_message(Slic3r::Orchestrator::PluginMessageLevel::Error,
                                                       host_context->plugin,
                                                       host_context->step,
                                                       message);
        host_context->orchestrator->request_plugin_cancel();
    }
}

void orchestrator_plugin_report_progress(plugin_host_context *host_context, double progress, const char *message)
{
    if (host_context == nullptr || host_context->print == nullptr)
        return;

    const int percent = int(std::clamp(progress, 0.0, 1.0) * 100.0 + 0.5);

    if (message != nullptr && message[0] != '\0') {
        host_context->print->set_status(percent,
                                        message,
                                        Slic3r::PrintBase::SlicingStatus::SECONDARY_STATE);
        return;
    }

    const std::string plugin_id = host_context->plugin != nullptr ? host_context->plugin->get_id() : "<unknown>";
    const std::string message_text = "Plugin " + plugin_id;
    host_context->print->set_status(percent,
                                    message_text,
                                    Slic3r::PrintBase::SlicingStatus::SECONDARY_STATE);
}

}
