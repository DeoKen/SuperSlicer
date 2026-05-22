///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "libslic3r/Api/host/Orchestrator.hpp"

#include <algorithm>
#include <vector>

#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_bridge_detector.h"

namespace Slic3r {

bridge_detector_instance Orchestrator::create_bridge_detector(const bridge_detector_create_input &input)
{
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

} // namespace Slic3r
