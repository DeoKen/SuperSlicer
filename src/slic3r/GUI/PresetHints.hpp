///|/ Copyright (c) Prusa Research 2017 - 2020 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_PresetHints_hpp_
#define slic3r_PresetHints_hpp_

#include <optional>
#include <string>

#include "libslic3r/PresetBundle.hpp"

namespace Slic3r {

// GUI utility functions to produce hint messages from the current profile.
class PresetHints
{
public:
    // Produce a textual description of the cooling logic of a currently active filament.
    static std::string cooling_description(const Preset &preset);
    
    // Produce a textual description of the maximum flow achived for the current configuration
    // (the current printer, filament and print settigns).
    // This description will be useful for getting a gut feeling for the maximum volumetric
    // print speed achievable with the extruder.
    static std::string maximum_volumetric_flow_description(const PresetBundle &preset_bundle);

    // Produce a textual description of a recommended thin wall thickness
    // from the provided number of perimeters and the external / internal perimeter width.
    static std::string recommended_thin_wall_thickness(const PresetBundle &preset_bundle);

    // The overhang threshold as the steepest wall that is not an overhang, at the layer height, nozzle diameter and
    // external perimeter width of the current profiles. Empty if not available (invalid layer height or width).
    struct OverhangThresholdGeometry {
        double angle;           // degrees from vertical
        double stick_out;       // mm the outer edge of each layer sticks out past the layer below at that angle
        double layer_height;    // mm
        double nozzle_diameter; // mm
        double width;           // mm, external perimeter
    };
    static std::optional<OverhangThresholdGeometry> overhang_threshold_geometry(const PresetBundle &preset_bundle);

    // Produce a textual explanation of the combined effects of the top/bottom_solid_layers
    // versus top/bottom_min_shell_thickness. Which of the two values wins depends
    // on the active layer height.
    static std::string top_bottom_shell_thickness_explanation(const PresetBundle &preset_bundle);    
};

} // namespace Slic3r

#endif /* slic3r_PresetHints_hpp_ */
