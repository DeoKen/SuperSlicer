#ifndef slic3r_GCode_PressureAdvancePattern_hpp_
#define slic3r_GCode_PressureAdvancePattern_hpp_

// Pressure advance calibration pattern (Calibration menu), ported from Ellis' Pressure Advance / Linear Advance Calibration
// Tool (https://github.com/AndrewEllis93/Pressure_Linear_Advance_Tool, GPL-3.0; original Marlin tool by Sineos). Like the
// original it writes the G-code itself instead of slicing a model: chevrons of a few walls, one per pressure advance value,
// on an anchor frame with the values printed next to them. Klipper only (SET_PRESSURE_ADVANCE ADVANCE=).
//
// The printer, filament and print settings give the rest: bed (pattern at its center), nozzle and filament diameter,
// extrusion multiplier, temperatures, retraction, Z hop, travel speed, layer heights, the start / end G-codes (placeholders
// processed as for a print) and the Klipper speed mode macro. The file has the PrusaSlicer header, the feature type / width /
// layer tags and the settings at the end, so the G-code viewer shows it like a sliced print.

#include <string>

namespace Slic3r {

class DynamicPrintConfig;

struct PressureAdvancePatternParams
{
    enum class Anchor { Frame, Layer, None };

    double pa_start          { 0. };
    double pa_end            { 0.08 };
    double pa_step           { 0.005 };
    int    layers            { 4 };
    int    wall_count        { 3 };
    double side_length       { 30. };   // mm, one side of a chevron
    double spacing           { 2. };    // mm between the patterns
    double corner_angle      { 90. };   // degrees
    double print_dir         { 0. };    // degrees, clockwise rotation of the whole print around the bed center
    double speed             { 100. };  // mm/s, pattern walls
    double first_layer_speed { 30. };   // mm/s, first layer, anchor and numbers
    double acceleration      { 0. };    // mm/s², SET_VELOCITY_LIMIT ACCEL=; 0 = no command
    int    fan               { 100 };   // %, from the second layer
    int    first_layer_fan   { 0 };     // %
    Anchor anchor            { Anchor::Frame };
    bool   numbers           { true };
    // Line width in % of the nozzle diameter, of the pattern and of the anchor (Ellis: 112.5 / 140).
    double line_ratio        { 112.5 };
    double anchor_line_ratio { 140. };
    int    anchor_perimeters { 4 };
};

// The number of patterns (pressure advance values).
int pressure_advance_pattern_count(const PressureAdvancePatternParams &params);

// The whole G-code file. config: the full print config (print, filament and printer of the first extruder).
// Throws Slic3r::RuntimeError with a message for the user if the values are invalid, the pattern doesn't fit the bed or
// crosses a keep-out zone, or a custom G-code fails to process.
std::string pressure_advance_pattern_gcode(const DynamicPrintConfig &config, const PressureAdvancePatternParams &params);

} // namespace Slic3r

#endif // slic3r_GCode_PressureAdvancePattern_hpp_
