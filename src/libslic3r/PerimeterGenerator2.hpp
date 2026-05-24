///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_PerimeterGenerator2_hpp_
#define slic3r_PerimeterGenerator2_hpp_

namespace Slic3r {
class Layer;
class LayerSliceIsland;
class Print;
class PrintObject;
}

namespace Slic3r::PerimeterGenerator2 {

// New perimeter-generation entry point.
//
// This is intentionally kept as a function-level orchestrator for now. The goal
// is to make the data flow explicit before moving any of the old
// PerimeterGenerator internals into reusable plugin steps.
void process(Print &print, PrintObject &object, Layer &layer, LayerSliceIsland &island);

} // namespace Slic3r::PerimeterGenerator2

#endif // slic3r_PerimeterGenerator2_hpp_
