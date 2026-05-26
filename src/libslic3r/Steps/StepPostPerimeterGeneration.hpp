///|/ Copyright (c) SuperSlicer 2026 Durand R?mi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef steps_steppostperimetergeneration_hpp_
#define steps_steppostperimetergeneration_hpp_

#include <string>

#include "libslic3r/Api/plugin/c/slic3r_plugin_types.h"

namespace Slic3r {
class Orchestrator;
class Print;

// Extension point after perimeter generation.
//
// Typical modules here edit already generated perimeter extrusions or add
// perimeter-derived metadata: overhang speed enforcement, fuzzy skin,
// extra perimeters over overhangs, seam tags, and similar post-processing.
namespace Steps::StepPostPerimeterGeneration {

void clean_and_prepare(Print &print);
bool validate_pre(const Print &print, std::string &error);
bool validate_post(const Print &print, std::string &error);
void run_step(Orchestrator &orchestrator, Print &print);

} // namespace Steps::StepPostPerimeterGeneration
} // namespace Slic3r

#endif // steps_steppostperimetergeneration_hpp_
