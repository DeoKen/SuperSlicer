///|/ Copyright (c) SuperSlicer 2026 Durand R?mi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "StepLayerHeightGeneration.hpp"

#include <cmath>
#include <memory>
#include <sstream>
#include <vector>

#include "libslic3r/Api/host/steps/LayerHeightStep.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/Steps/StepPipeline.hpp"

#ifdef _DEBUG
#include "libslic3r/Plugins/StandardLayerHeightGenerator.hpp"
#endif
#include "StepRunner.hpp"

namespace Slic3r::Steps::StepLayerHeightGeneration {

void clean_and_prepare(Print &) {}

bool validate_pre(const Print &print, std::string &out_error)
{
    for (size_t object_idx = 0; object_idx < print.objects().size(); ++object_idx) {
        const PrintObject &print_object = print.objects()[object_idx];

        const ModelObject *model_object = print_object.model_object();
        if (model_object == nullptr) {
            out_error += "Error: can't validate layer height profile: model object is null.";
            return false;
        }

        const double object_print_z_max = check_z_step(model_object->max_z(), print.config().z_step.value);
#ifdef _DEBUG
        std::string params_error;
        if (!slic3r_api::StandardLayerHeightGeneratorPlugin::test_layer_height_slicing_parameters(
                print, print_object, params_error)) {
            out_error += "Error: StandardLayerHeightGenerator slicing parameters mismatch: ";
            out_error += params_error;
            return false;
        }
#endif
        const std::vector<coordf_t> &layer_height_profile = model_object->layer_height_profile.get();
        if (!layer_height_profile.empty()) {
            if ((layer_height_profile.size() & 1) != 0) {
                out_error += "Error: can't apply the layer height profile: layer_height_profile array is odd, not even.";
                return false;
            }

            if (std::abs(layer_height_profile[layer_height_profile.size() - 2] - object_print_z_max) >
                10 * EPSILON) {
                std::ostringstream msg;
                msg << "Error: can't apply the layer height profile for object " << object_idx
                    << ": layer_height_profile last layer is at "
                    << layer_height_profile[layer_height_profile.size() - 2]
                    << ", and it's too far away from object_print_z_max = "
                    << object_print_z_max;
                out_error += msg.str();
                return false;
            }
        }
    }

    return true;
}

bool validate_post(const Print &, std::string &) { return true; }

void run_step(Orchestrator &orchestrator, Print &print) {
    Detail::validate_or_report(validate_pre, print, "Layer-height pre-step validation");

    std::vector<Plugin *> plugins = selected_or_active_plugins_for_step(orchestrator,
                                                                        STEP_LAYER_HEIGHT,
                                                                        &print.full_print_config());

    for (Plugin *plugin : plugins) {
        const size_t run_count = print.objects().size();
        plugin_host_context host_context = orchestrator.prepare_plugin_host_context(STEP_LAYER_HEIGHT, plugin, &print);
        plugin_run_context run_context = orchestrator.prepare_plugin_run_context(STEP_LAYER_HEIGHT, plugin,
                                                                                   &host_context);
        plugin->setup(run_context, uint32_t(run_count));

        std::vector<std::unique_ptr<ApiHost::Steps::LayerHeightRunContext>> layer_contexts;
        layer_contexts.reserve(run_count);
        for (size_t object_idx = 0; object_idx < run_count; ++object_idx)
            layer_contexts.push_back(ApiHost::Steps::make_layer_height_run_context(print, object_idx));

        parallel_for(size_t(0), run_count, [plugin, &run_context, &layer_contexts](const size_t object_idx) {
            plugin_run_context context_copy = run_context;
            if (context_copy.is_cancelled != nullptr && context_copy.is_cancelled(context_copy.host_context))
                return;

            context_copy.data = &layer_contexts[object_idx]->context_step;
            plugin->setup_run(context_copy);
        });

        parallel_for(size_t(0), run_count, [plugin, &run_context, &layer_contexts](const size_t object_idx) {
            plugin_run_context context_copy = run_context;
            if (context_copy.is_cancelled != nullptr && context_copy.is_cancelled(context_copy.host_context))
                return;

            context_copy.data = &layer_contexts[object_idx]->context_step;
            plugin->run(context_copy);
        });

        Detail::validate_or_report(validate_post, print, "Layer-height post-plugin validation");
    }
}

} // namespace Slic3r::Steps::StepLayerHeightGeneration
