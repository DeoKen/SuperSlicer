///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#pragma once

#include "libslic3r/Api/plugin/c/slic3r_plugin_types.h"

#include <exception>

namespace slic3r_api {

// Local C++ exception used by plugin code to unwind quickly when the host asks
// the plugin to stop. It must be caught before returning through the C ABI.
class PluginCancelled : public std::exception
{
public:
    const char *what() const noexcept override { return "Plugin execution cancelled"; }
};

inline bool is_cancelled(const plugin_run_context *ctx)
{
    return ctx != nullptr && ctx->is_cancelled != nullptr && ctx->is_cancelled(ctx->host_context) != 0;
}

inline void throw_if_cancelled(const plugin_run_context *ctx)
{
    if (is_cancelled(ctx))
        throw PluginCancelled();
}

inline void report_warning(const plugin_run_context *ctx, const char *message)
{
    if (ctx != nullptr && ctx->report_warning != nullptr)
        ctx->report_warning(ctx->host_context, message);
}

inline void report_error(const plugin_run_context *ctx, const char *message)
{
    if (ctx != nullptr && ctx->report_error != nullptr)
        ctx->report_error(ctx->host_context, message);
}

inline void report_progress(const plugin_run_context *ctx, double progress, const char *message = nullptr)
{
    if (ctx != nullptr && ctx->report_progress != nullptr)
        ctx->report_progress(ctx->host_context, progress, message);
}

} // namespace slic3r_api
