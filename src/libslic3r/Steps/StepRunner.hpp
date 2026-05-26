///|/ Copyright (c) SuperSlicer 2026 Durand R?mi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef steps_steprunner_hpp_
#define steps_steprunner_hpp_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <boost/log/trivial.hpp>

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/DataTreeFwd.hpp"
#include "libslic3r/Thread.hpp"

namespace Slic3r::Steps::Detail {

inline void validate_or_report(bool (*validator)(const Print &, std::string &),
                               const Print &print,
                               const char *validation_name)
{
    std::string error;
    if (validator(print, error))
        return;

    BOOST_LOG_TRIVIAL(error) << validation_name << " failed:\n" << error;
    assert(false && "Step data tree validation failed");
}

template<class PayloadFactory>
void run_object_step_plugin(Orchestrator &orchestrator,
                            Print &print,
                            slicing_step_t step,
                            Plugin &plugin,
                            size_t run_count,
                            PayloadFactory payload_factory)
{
    assert(run_count <= UINT32_MAX);

    // One host context is shared as the immutable template for this plugin
    // execution. Per-object workers copy it below to set their own object_idx;
    // this avoids sharing mutable host_context fields between threads.
    plugin_host_context host_context = orchestrator.prepare_plugin_host_context(step, &plugin, &print);
    host_context.object_count = run_count;

    // setup() is the plugin-level entry point. It sees the step and print, but
    // no object payload yet, and it runs once before all object setup/run calls.
    plugin_run_context run_context = orchestrator.prepare_plugin_run_context(step, &plugin, &host_context);
    plugin.setup(run_context, uint32_t(run_count));

    // Prepare every object payload before any object run starts. This gives
    // plugins a deterministic discovery pass even if the host executes the
    // per-object setup in parallel.
    parallel_for(size_t(0), run_count, [&host_context, &payload_factory, &run_context, &plugin](const size_t idx) {
        plugin_run_context context_copy = run_context;
        plugin_host_context host_context_copy = host_context;
        host_context_copy.object_idx = idx;
        context_copy.host_context = &host_context_copy;
        if (context_copy.is_cancelled != nullptr && context_copy.is_cancelled(context_copy.host_context))
            return;

        // Payloads are stack objects valid only for the duration of the plugin
        // callback. Plugins may inspect the handles immediately, but must not
        // store the payload pointer itself.
        decltype(payload_factory(idx)) payload = payload_factory(idx);
        context_copy.data = &payload;
        plugin.setup_run(context_copy);
    });

    parallel_for(size_t(0), run_count, [&host_context, &payload_factory, &run_context, &plugin](const size_t idx) {
        plugin_run_context context_copy = run_context;
        plugin_host_context host_context_copy = host_context;
        host_context_copy.object_idx = idx;
        context_copy.host_context = &host_context_copy;
        if (context_copy.is_cancelled != nullptr && context_copy.is_cancelled(context_copy.host_context))
            return;

        // Rebuild the same payload for run(): setup_run() may have been called
        // on another thread, and keeping payload ownership local makes the ABI
        // lifetime rule straightforward.
        decltype(payload_factory(idx)) payload = payload_factory(idx);
        context_copy.data = &payload;
        plugin.run(context_copy);
    });
}

template<class PayloadFactory>
void run_object_step_plugins(Orchestrator &orchestrator,
                             Print &print,
                             slicing_step_t step,
                             size_t run_count,
                             PayloadFactory payload_factory)
{
    std::vector<Plugin *> plugins = orchestrator.get_active_plugins_for_step(step);

    // Non-exclusive steps execute every active plugin in orchestrator order.
    // The orchestrator owns priority sorting; this helper only applies the
    // common object-level setup/setup_run/run protocol.
    for (Plugin *plugin : plugins) {
        if (plugin != nullptr)
            run_object_step_plugin(orchestrator, print, step, *plugin, run_count, payload_factory);
    }
}

} // namespace Slic3r::Steps::Detail

#endif // steps_steprunner_hpp_
