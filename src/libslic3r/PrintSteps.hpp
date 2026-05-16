///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_PrintSteps_hpp_
#define slic3r_PrintSteps_hpp_

#include <cstdint>

namespace Slic3r {

// Print step IDs for keeping track of the print state.
// The Print steps are applied in this order.
enum PrintStep : uint8_t {
    psWipeTower,
    // Ordering of the tools on PrintObjects for a multi-material print.
    // psToolOrdering is a synonym to psWipeTower, as the Wipe Tower calculates and modifies the ToolOrdering,
    // while if printing without the Wipe Tower, the ToolOrdering is calculated as well.
    psToolOrdering = psWipeTower,
    psAlertWhenSupportsNeeded,
    psSkirtBrim,
    psCheckConflict,
    // Last step before G-code export, after this step is finished, the initial extrusion path preview
    // should be refreshed.
    psSlicingFinished = psCheckConflict,
    psGCodeExport,
    // TODO: psGCodeLoader (for params that are only used for time display and such)
    psCount,
};

enum PrintObjectStep : uint8_t {
    posSlice,
    posPerimeters,
    posPrepareInfill,
    posInfill,
    posIroning,
    posSupportSpotsSearch,
    posSupportMaterial,
    posEstimateCurledExtrusions,
    posCalculateOverhangingPerimeters,
    posSimplifyPath, // simplify &  arc fitting from BBS
    posCount,
};

int printstep_percent(PrintStep step);
int objectstep_percent(PrintObjectStep step);

} // namespace Slic3r

#endif // slic3r_PrintSteps_hpp_
