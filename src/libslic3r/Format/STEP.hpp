///|/ Copyright (c) SuperSlicer 2026 Durand R?mi @supermerill
///|/ Copyright (c) Prusa Research 2022 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef format_step_hpp_
#define format_step_hpp_

#include <optional>
#include <utility>

// Original implementation of STEP format import created by Bambulab.
// https://github.com/bambulab/BambuStudio
// Forked off commit 1555904, modified by Prusa Research.

namespace Slic3r {

class Model;

//typedef std::function<void(int load_stage, int current, int total, bool& cancel)> ImportStepProgressFn;

// Load a step file into a provided model.
// Inside deflections pair:
// * first value is linear deflection
// * second value is angle deflection
extern bool load_step(const char *path_str, Model *model /*LMBBS:, ImportStepProgressFn proFn = nullptr*/, std::optional<std::pair<double, double>> deflections = std::nullopt);

}; // namespace Slic3r

#endif /* format_step_hpp_ */
