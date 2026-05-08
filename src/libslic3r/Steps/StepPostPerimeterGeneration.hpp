#pragma once

#include "libslic3r/Api/plugin/c/slic3r_plugin_types.h"

#include <string>

namespace Slic3r {
class Orchestrator;
class Print;

namespace Steps::StepPostPerimeterGeneration {

void clean_and_prepare(Print &print);
bool validate_pre(const Print &print, std::string &error);
bool validate_post(const Print &print, std::string &error);
void run_step(Orchestrator &orchestrator, Print &print);

} // namespace Steps::StepPostPerimeterGeneration
} // namespace Slic3r
