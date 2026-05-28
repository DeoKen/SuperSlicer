///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_infill_legacyinfillpatterns_hpp_
#define plugins_infill_legacyinfillpatterns_hpp_

#include "libslic3r/Api/plugin/cpp/PluginBase.hpp"

namespace slic3r_api { namespace Infill { namespace LegacyInfillPatternsPlugin {

// INFILL_PATTERN service plugins backed by the existing Fill implementations.
//
// They let the new infill step select a pattern through the plugin pipeline
// while the individual algorithms are migrated one by one. The plugin id is
// deliberately the same string as the serialized InfillPattern enum value, so
// existing project settings can select it without a translation table.
void register_legacy_infill_pattern_plugins(orchestrator_handle *orch);

}}} // namespace slic3r_api::Infill::LegacyInfillPatternsPlugin

#endif // plugins_infill_legacyinfillpatterns_hpp_
