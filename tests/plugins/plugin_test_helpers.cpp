#include "plugin_test_helpers.hpp"

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/FFFPrintConfig.hpp"
#include "libslic3r/Plugins/SliceVolume.hpp"
#include "libslic3r/Plugins/StandardLayerHeightGenerator.hpp"
#include "libslic3r/Plugins/Support/SupportDemandModifiers.hpp"
#include "libslic3r/Plugins/Support/SupportDemandOverhangs.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SLA/SLAPrintConfig.hpp"

namespace Slic3r::Test::Plugins {

void ensure_plugin_test_runtime_initialized()
{
    static bool initialized = []() {
        PrintConfigDef::instance_mutable().init_common_params();
        init_fff_params(PrintConfigDef::instance_mutable());
        init_sla_params(PrintConfigDef::instance_mutable());

        Orchestrator &orchestrator = Orchestrator::instance();
        orchestrator_handle *orchestrator_handle_value =
            reinterpret_cast<orchestrator_handle *>(&orchestrator);
        slic3r_api::StandardLayerHeightGeneratorPlugin::register_standard_layer_height_generator_plugin(
            orchestrator_handle_value);
        slic3r_api::SliceVolumePlugin::register_slice_volume_plugin(orchestrator_handle_value);
        slic3r_api::Support::SupportDemandOverhangsPlugin::register_support_demand_overhangs_plugin(
            orchestrator_handle_value);
        slic3r_api::Support::SupportDemandModifiersPlugin::register_support_demand_modifiers_plugin(
            orchestrator_handle_value);

        orchestrator.initialize_plugins();
        initialize_fff_print_config_cache();
        initialize_sla_print_config_cache();
        PrintConfigDef::instance_mutable().finalize();
        return true;
    }();
    (void)initialized;
}

} // namespace Slic3r::Test::Plugins
