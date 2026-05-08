#pragma once

#include "libslic3r/Api/plugin/c/slic3r_plugin_types.h"

#include <string>

namespace Slic3r {
class Orchestrator;
class Print;

namespace Steps::StepGenerateInfill {

void clean_and_prepare(Print &print);
bool validate_pre(const Print &print, std::string *error = nullptr);
bool validate_post(const Print &print, std::string *error = nullptr);
void run_step(Orchestrator &orchestrator, Print &print);

} // namespace Steps::StepGenerateInfill
} // namespace Slic3r
