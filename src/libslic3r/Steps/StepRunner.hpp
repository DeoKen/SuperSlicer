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

namespace Slic3r {
class ConfigBase;
}

namespace Slic3r::Steps {
std::vector<Plugin *> selected_or_active_plugins_for_step(Orchestrator &orchestrator,
                                                          slicing_step_t step,
                                                          const ConfigBase *config);
}

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

inline void validate_or_report(bool (*validator)(const Print &, std::string *),
                               const Print &print,
                               const char *validation_name)
{
    std::string error;
    if (validator(print, &error))
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
    plugin_host_context host_context = orchestrator.prepare_plugin_host_context(step, &plugin, &print);
    host_context.object_count = run_count;
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

        decltype(payload_factory(idx)) payload = payload_factory(idx);
        context_copy.data = &payload;
        plugin.run(context_copy);
    });
}

template<class PayloadFactory>
void run_object_step_plugins(Orchestrator &orchestrator,
                             Print &print,
                             slicing_step_t step,
                             const ConfigBase *config,
                             size_t run_count,
                             PayloadFactory payload_factory)
{
    std::vector<Plugin *> plugins = selected_or_active_plugins_for_step(orchestrator, step, config);

    for (Plugin *plugin : plugins) {
        if (plugin != nullptr)
            run_object_step_plugin(orchestrator, print, step, *plugin, run_count, payload_factory);
    }
}

} // namespace Slic3r::Steps::Detail

#endif // steps_steprunner_hpp_
