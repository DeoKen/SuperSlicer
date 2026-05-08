///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "Orchestrator.hpp"
#include "Plugin.hpp"
#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Print.hpp"

#include <boost/log/trivial.hpp>

#include <algorithm>
#include <string>
#include <vector>

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
}

void orchestrator_plugin_report_error(plugin_host_context *host_context, const char *message)
{
    const char *plugin_id = host_context != nullptr && host_context->plugin != nullptr ?
                                host_context->plugin->get_id().c_str() :
                                "<unknown>";
    BOOST_LOG_TRIVIAL(error) << "Plugin error from " << plugin_id << ": " << (message != nullptr ? message : "");

    if (host_context != nullptr && host_context->orchestrator != nullptr)
        host_context->orchestrator->request_plugin_cancel();
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
