///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "PrintSteps.hpp"

namespace Slic3r {

int printstep_percent(PrintStep step) {
    switch (step) {
    case PrintStep::psAlertWhenSupportsNeeded: return 45;
    case PrintStep::psSkirtBrim: return 70;
    case PrintStep::psWipeTower: return 75;
    case PrintStep::psCheckConflict: return 80;
    case PrintStep::psGCodeExport: return 85;
    case PrintStep::psCount: return 100;
    }
    return 0;
}

int objectstep_percent(PrintObjectStep step) {
    switch (step) {
    case PrintObjectStep::posSlice: return 0;
    case PrintObjectStep::posPerimeters: return 10;
    case PrintObjectStep::posPrepareInfill: return 20;
    case PrintObjectStep::posInfill: return 30;
    case PrintObjectStep::posIroning: return 40;
    case PrintObjectStep::posSupportSpotsSearch: return 45;
    case PrintObjectStep::posSupportMaterial: return 50;
    case PrintObjectStep::posEstimateCurledExtrusions: return 60;
    case PrintObjectStep::posCalculateOverhangingPerimeters: return 65;
    case PrintObjectStep::posSimplifyPath: return 80;
    case PrintObjectStep::posCount: return 85;
    }
    return 0;
}

} // namespace Slic3r
