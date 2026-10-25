#ifndef slic3r_PressureCalibration_hpp_
#define slic3r_PressureCalibration_hpp_

// SuperSlicer's pressure calibration (CalibrationPressureAdvDialog), the model part: 90° bends, each a part of the test
// object with its own pressure (linear) advance as per region G-code, the borders, the test number and every second value
// engraved, with the object settings. Used by the Calibration menu (GUI/CalibrationDialogs.cpp) and the tests.

#include <string>
#include <vector>

#include "Point.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

class ModelObject;
class ModelVolume;

// One test (one row of the dialog).
struct PressureCalibrationTest
{
    double              first_pa    { 0.04 };
    std::vector<double> values;
    // One of pressure_calibration_roles() or "CheckAll".
    std::string         role        { "ExternalPerimeter" };
    bool                smooth_time { false };
};

// The print settings of the calibration, Ellis' defaults (his tool's DEFAULT_SETTINGS, pressure_linear_advance.js), so that it
// prints as his pattern does. The printer and filament settings (nozzle, temperatures, retraction, travel) are the presets'.
struct PressureCalibrationSettings
{
    // True: Ellis' chevrons, wall_count open lines of line_ratio at speed, side_length long, layers high, in a frame. False:
    // SuperSlicer's 90° bends (closed loops) with the width and speed of the extrusion role of each test, 5 layers high.
    bool   ellis_bends        { true };
    int    wall_count         { 3 };
    double side_length        { 30. };   // mm, one arm of a chevron
    double pattern_spacing    { 2. };    // mm between two chevrons
    double speed              { 100. };  // mm/s, the bends
    double line_ratio         { 112.5 }; // % of the nozzle, the bends
    int    layers             { 4 };     // with the first layer
    double first_layer_speed  { 30. };   // mm/s, the first layer, and the numbers
    double layer_height       { 0.2 };
    double first_layer_height { 0.25 };
    int    fan                { 100 };   // %, from the second layer
    int    first_layer_fan    { 0 };     // %, 0 or the fan (PrusaSlicer has no fan of its own for the first layer)
    double acceleration       { 0. };    // mm/s², 0 = no acceleration command (Ellis: "Acceleration: Disabled")
    int    frame_lines        { 8 };     // first layer lines of each border of the frame, both bends (SuperSlicer: 4)
};

// Writes the settings into a print or a filament config (the keys it has): layer heights, first layer speed, accelerations
// (all the default acceleration) / fan speeds (default fan speed, no ramp, no layer time cooling, the fan off on the first
// layer when first_layer_fan is 0). Throws Slic3r::RuntimeError for invalid settings.
void pressure_calibration_apply_settings(DynamicPrintConfig &config, const PressureCalibrationSettings &settings);

// The settings and the values of the tests as G-code comments (as Ellis writes them at the top of his G-code), for a custom
// G-code on the first layer, so that the G-code and the preview show how the test was made.
std::string pressure_calibration_summary(const DynamicPrintConfig &config, const PressureCalibrationSettings &settings,
                                         const std::vector<PressureCalibrationTest> &tests);

// The name of the object of a test, with its values.
std::string pressure_calibration_object_name(const PressureCalibrationTest &test, size_t id_item);

// The extrusion roles of the dialog, without "CheckAll".
const std::vector<std::string>& pressure_calibration_roles();

// The values from start to end by the increment, the end value always included (SuperSlicer's calc_PA_values()).
std::vector<double> pressure_calibration_values(double start, double end, double increment);

// The pressure advance command of the firmware ("SET_PRESSURE_ADVANCE ADVANCE=", "M900 K", "M572 S"), empty if unsupported.
std::string pressure_calibration_command(GCodeFlavor flavor, bool smooth_time);

// resources/calibration/filament_pressure/scaled_with_nozzle_size/90_bend_<nozzle>.3mf, empty if there is none.
std::string pressure_calibration_bend_file(const std::string &resources_dir, double nozzle);
// resources/calibration/filament_pressure/base_plate.3mf
std::string pressure_calibration_base_file(const std::string &resources_dir);

// Adds the volumes of a model file to the object as parts, moved by move (in the coordinates of the file, as the object was
// loaded: the object is centered around its origin when it was loaded) and scaled by scale. The last added part.
ModelVolume* calibration_add_part(ModelObject *object, const std::string &file, const Vec3d &move, const Vec3d &scale);

// Builds test number id_item on base, the loaded base_plate.3mf: the frame, the numbers and (SuperSlicer's bends, CheckAll)
// the bends. config: the full print config, with the settings applied. Returns false for CheckAll (not to be sliced before the
// values are typed in).
bool pressure_calibration_build(ModelObject &base, const DynamicPrintConfig &config, const PressureCalibrationSettings &settings,
                                const PressureCalibrationTest &test, size_t id_item, const std::string &resources_dir);

// The number of objects of the walls of Ellis' chevrons for a test: settings.wall_count, 0 with SuperSlicer's bends or CheckAll.
// Each wall is an object of its own (wall k of every chevron, one part per chevron with its value as per region G-code):
// Ellis' walls overlap a little and overlapping parts of one object clip each other.
size_t pressure_calibration_wall_objects(const PressureCalibrationSettings &settings, const PressureCalibrationTest &test);

// Builds the walls of the chevrons of a test into walls (pressure_calibration_wall_objects() loaded base_plate.3mf, in the
// coordinates of the base of the test: the same instance transformation). Each wall: a strip of one line width with a round
// outer corner, sliced with Arachne into one open line, as Ellis' lines.
void pressure_calibration_build_walls(const std::vector<ModelObject*> &walls, const DynamicPrintConfig &config,
                                      const PressureCalibrationSettings &settings, const PressureCalibrationTest &test, size_t id_item);

// The print settings SuperSlicer changes for the calibration.
void pressure_calibration_print_config(DynamicPrintConfig &print);

} // namespace Slic3r

#endif // slic3r_PressureCalibration_hpp_
