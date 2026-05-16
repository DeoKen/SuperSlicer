///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "PrintSteps.hpp"

namespace Slic3r {

int printstep_percent(slicing_step_t step) {
    switch (step) {
    case psAlertWhenSupportsNeeded: return 45;
    case psSkirtBrim: return 70;
    case psWipeTower: return 75;
    case psCheckConflict: return 80;
    case psGCodeExport: return 85;
    }
    if (step == psCount)
        return 100;
    return 0;
}

int objectstep_percent(slicing_step_t step) {
    switch (step) {
    case posSlice: return 0;
    case posPerimeters: return 10;
    case posPrepareInfill: return 20;
    case posInfill: return 30;
    case posIroning: return 40;
    case posSupportSpotsSearch: return 45;
    case posSupportMaterial: return 50;
    case posEstimateCurledExtrusions: return 60;
    case posCalculateOverhangingPerimeters: return 65;
    case posSimplifyPath: return 80;
    }
    if (step == posCount)
        return 85;
    return 0;
}

bool is_print_step(slicing_step_t step)
{
    switch (step) {
    case psAlertWhenSupportsNeeded:
    case psSkirtBrim:
    case psCheckConflict:
    case psToolOrdering:
    case psWipeTower:
    case psGCodeExport:
        return true;
    default:
        return false;
    }
}

bool is_print_object_step(slicing_step_t step)
{
    switch (step) {
    case posSlice:
    case posPerimeters:
    case posPrepareInfill:
    case posInfill:
    case posIroning:
    case posSupportSpotsSearch:
    case posSupportMaterial:
    case posEstimateCurledExtrusions:
    case posCalculateOverhangingPerimeters:
    case posSimplifyPath:
        return true;
    default:
        return false;
    }
}

} // namespace Slic3r
