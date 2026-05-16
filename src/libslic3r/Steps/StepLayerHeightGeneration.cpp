///|/ Copyright (c) SuperSlicer 2026 Durand R?mi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "StepLayerHeightGeneration.hpp"

#include <cmath>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#include "libslic3r/Api/host/ApiHostUtils.hpp"
#include "libslic3r/Api/internal/PrintObjectAccess.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Slicing.hpp"

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

void set_layer_height_profile(object_handle *object_handler, coord_t *layer_zs, uint32_t layer_zs_size);

struct LayerHeightRunContext
{
    std::vector<coord_t> layer_z_profile;
    std::vector<c_layer_config_range> layer_config_ranges;
    run_ctx_layer_height_generation context_step = {};
};

std::unique_ptr<LayerHeightRunContext> make_layer_height_run_context(Print &print, size_t object_idx)
{
    auto out = std::make_unique<LayerHeightRunContext>();

    PrintObject &print_object = print.object(object_idx);
    ModelObject &model_object = *print_object.model_object();
    const double object_print_z_max = check_z_step(model_object.max_z(), print.config().z_step.value);

    for (coordf_t layer_z : model_object.layer_height_profile.get())
        out->layer_z_profile.push_back(coord_t(layer_z + 0.5));

    out->layer_config_ranges.reserve(model_object.layer_config_ranges.size());
    for (const auto &range : model_object.layer_config_ranges) {
        c_layer_config_range c_range = {};
        c_range.z_min = scale_i(range.first.first);
        c_range.z_max = scale_i(range.first.second);
        c_range.config = ApiHost::to_config_handle(&range.second.get());
        out->layer_config_ranges.push_back(c_range);
    }

    out->context_step.print = reinterpret_cast<print_handle *>(&print);
    out->context_step.object = reinterpret_cast<object_handle *>(&print_object);
    out->context_step.enforce_layer_zs = out->layer_z_profile.data();
    out->context_step.enforce_layer_zs_size = out->layer_z_profile.size();
    out->context_step.layer_config_ranges = out->layer_config_ranges.data();
    out->context_step.layer_config_ranges_size = out->layer_config_ranges.size();
    out->context_step.set_layer_height_profile = set_layer_height_profile;
    out->context_step.max_z = scale_i(object_print_z_max);
    return out;
}

void set_layer_height_profile(object_handle *object_handler, coord_t *layer_zs, uint32_t layer_zs_size) {
    if (object_handler == nullptr || (layer_zs == nullptr && layer_zs_size != 0))
        return;

    PrintObject *object = reinterpret_cast<PrintObject *>(object_handler);
    std::vector<coord_t> new_layer_profile;
    new_layer_profile.reserve(layer_zs_size);
    for (uint32_t i = 0; i < layer_zs_size; ++i)
        new_layer_profile.push_back(layer_zs[i]);
    ApiInternal::PrintObjectAccess::set_layer_profile(*object, std::move(new_layer_profile));
}

void run_step(Orchestrator &orchestrator, Print &print) {
    Detail::validate_or_report(validate_pre, print, "Layer-height pre-step validation");

    std::vector<Plugin *> plugins = orchestrator.get_all_plugins_for_step(STEP_LAYER_HEIGHT);

    for (Plugin *plugin : plugins) {
        const size_t run_count = print.objects().size();
        plugin_host_context host_context = orchestrator.prepare_plugin_host_context(STEP_LAYER_HEIGHT, plugin, &print);
        plugin_run_context run_context = orchestrator.prepare_plugin_run_context(STEP_LAYER_HEIGHT, plugin,
                                                                                   &host_context);
        plugin->setup(run_context, uint32_t(run_count));

        std::vector<std::unique_ptr<LayerHeightRunContext>> layer_contexts;
        layer_contexts.reserve(run_count);
        for (size_t object_idx = 0; object_idx < run_count; ++object_idx)
            layer_contexts.push_back(make_layer_height_run_context(print, object_idx));

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
