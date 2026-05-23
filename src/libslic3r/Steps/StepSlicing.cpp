///|/ Copyright (c) SuperSlicer 2026 Durand R?mi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "StepSlicing.hpp"

#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/Api/host/steps/SlicingStep.hpp"
#include "libslic3r/Api/internal/LayerAccess.hpp"
#include "libslic3r/Api/internal/PrintObjectAccess.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/SurfaceCollection.hpp"
#include "libslic3r/Thread.hpp"

#include "StepRunner.hpp"

namespace Slic3r {
LayerUPtrs new_layers(PrintObject *print_object, const std::vector<double> &object_layers);
}

namespace Slic3r::Steps::StepSlicing {
namespace {

std::vector<double> to_unscaled_layer_height_profile(const std::vector<coord_t> &layer_profile)
{
    std::vector<double> out;
    out.reserve(layer_profile.size());
    for (coord_t value : layer_profile)
        out.push_back(unscaled(value));
    return out;
}

void recreate_object_layers(PrintObject &object)
{
    std::vector<double> layer_height_profile =
        to_unscaled_layer_height_profile(object.layer_profile());
    LayerUPtrs object_layers =
        new_layers(&object, generate_object_layers(object.slicing_parameters(), layer_height_profile));
    for (std::unique_ptr<Layer> &layer : object_layers) {
        ApiInternal::LayerAccess::init_regions_from_object(*layer);
    }
    ApiInternal::PrintObjectAccess::replace_layers_by_moving_contents(object, std::move(object_layers));
}

void recompute_layer_slices_from_raw_regions(PrintObject &object)
{
    for (Layer &layer : object.layers()) {
        ExPolygons slices;
        if (layer.region_count() == 1) {
            slices = layer.region(0).get_raw_slices();
        } else {
            ExPolygons slices_exp;
            for (const LayerRegion &region : layer.regions())
                append(slices_exp, region.get_raw_slices());
            slices = union_safety_offset_ex(slices_exp);
        }
        ensure_valid(slices, std::max(scale_i(object.print()->config().resolution), SCALED_EPSILON));
        ApiInternal::LayerAccess::set_islands(layer, std::move(slices));
    }
}

} // namespace

void clean_and_prepare(Print & print) {
    parallel_for(size_t(0), print.objects().size(), [&print](const size_t object_idx) {
        PrintObject &object = print.object(object_idx);
        recreate_object_layers(object);
    });
}

bool validate_pre(const Print &print, std::string &out_error)
{
    bool ok = true;

    for (size_t object_idx = 0; object_idx < print.objects().size(); ++object_idx) {
        const PrintObject &object = print.objects()[object_idx];

        const std::vector<coord_t> &layer_profile = object.layer_profile();
        if (layer_profile.empty()) {
            ok = false;
            std::ostringstream msg;
            msg << "object " << object_idx << ": layer height profile is empty";
            out_error += msg.str();
            continue;
        }

        if ((layer_profile.size() & 1) != 0) {
            ok = false;
            std::ostringstream msg;
            msg << "object " << object_idx << ": layer height profile has an odd element count";
            out_error += msg.str();
        }
    }

    return ok;
}

bool validate_post(const Print &print, std::string &out_error)
{
    bool ok = true;

    for (size_t object_idx = 0; object_idx < print.objects().size(); ++object_idx) {
        const PrintObject &object = print.objects()[object_idx];

        if (object.layer_count() == 0) {
            ok = false;
            std::ostringstream msg;
            msg << "object " << object_idx << ": no layers were created";
            out_error += msg.str();
        }
    }

    return ok;
}

void run_step(Orchestrator &orchestrator, Print &print)
{
    Detail::validate_or_report(validate_pre, print, "Slicing pre-step validation");

    clean_and_prepare(print);

    std::vector<Plugin *> plugins = orchestrator.get_active_plugins_for_step(STEP_SLICING);

    for (Plugin *plugin : plugins) {
        const size_t run_count = print.objects().size();
        plugin_host_context host_context = orchestrator.prepare_plugin_host_context(STEP_SLICING, plugin, &print);
        plugin_run_context run_context = orchestrator.prepare_plugin_run_context(STEP_SLICING, plugin, &host_context);
        plugin->setup(run_context, uint32_t(run_count));

        std::vector<std::unique_ptr<ApiHost::Steps::SlicingRunContext>> run_contexts;
        run_contexts.reserve(run_count);
        for (size_t object_idx = 0; object_idx < run_count; ++object_idx)
            run_contexts.push_back(ApiHost::Steps::make_slicing_run_context(print, object_idx));

        parallel_for(size_t(0), run_count, [plugin, &run_context, &run_contexts](const size_t object_idx) {
            plugin_run_context context_copy = run_context;
            if (context_copy.is_cancelled != nullptr && context_copy.is_cancelled(context_copy.host_context))
                return;

            context_copy.data = &run_contexts[object_idx]->context_step;
            plugin->setup_run(context_copy);
        });

        parallel_for(size_t(0), run_count, [plugin, &run_context, &run_contexts](const size_t object_idx) {
            plugin_run_context context_copy = run_context;
            if (context_copy.is_cancelled != nullptr && context_copy.is_cancelled(context_copy.host_context))
                return;

            context_copy.data = &run_contexts[object_idx]->context_step;
            plugin->run(context_copy);
        });

        parallel_for(size_t(0), run_count, [&print](const size_t object_idx) {
            PrintObject &object = print.object(object_idx);
            recompute_layer_slices_from_raw_regions(object);
        });

        Detail::validate_or_report(validate_post, print, "Slicing post-plugin validation");
    }
}

} // namespace Slic3r::Steps::StepSlicing
