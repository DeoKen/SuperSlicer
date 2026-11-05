#ifndef slic3r_PressureCalibration_hpp_
#define slic3r_PressureCalibration_hpp_

// SuperSlicer's pressure calibration (CalibrationPressureAdvDialog), the model part: 90° bends, each a part of the test
// object with its own pressure (linear) advance as per region G-code, the borders, the test number and every second value
// engraved, with the object settings. Used by the Calibration menu (GUI/CalibrationDialogs.cpp) and the tests.

#include <string>
#include <vector>

#include "BoundingBox.hpp"
#include "Point.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

class ModelObject;
class ModelVolume;

// The brim on the outside of the frame of every pressure calibration (mm; user 2026-11-01).
constexpr double PRESSURE_CALIBRATION_BRIM_WIDTH = 5.;

// One test (one row of the dialog).
struct PressureCalibrationTest
{
    double              first_pa    { 0.04 };
    std::vector<double> values;
    // One of pressure_calibration_roles() or "CheckAll".
    std::string         role        { "ExternalPerimeter" };
    bool                smooth_time { false };
    // Adaptive pressure advance calibration (Ellis' chevrons only): the speed of the walls (0: the settings' speed), the
    // acceleration set with SET_VELOCITY_LIMIT ACCEL= before each wall (0: no command) and the length of a side of the chevrons
    // (0: the settings' side_length).
    double              speed        { 0. };
    double              acceleration { 0. };
    double              side_length  { 0. };
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
};

// Writes the settings into a print or a filament config (the keys it has): layer heights, first layer speed, accelerations
// (all the default acceleration) / fan speeds (default fan speed, no ramp, no layer time cooling, the fan off on the first
// layer when first_layer_fan is 0). Throws Slic3r::RuntimeError for invalid settings.
void pressure_calibration_apply_settings(DynamicPrintConfig &config, const PressureCalibrationSettings &settings);

// The settings and the values (and speed, acceleration, flow) of the tests as G-code comments (as Ellis writes them at the top of his G-code), for a custom
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

// The size (mm, without the brim) of the frame of a test as pressure_calibration_build() makes it (Ellis' chevrons: the walls stay
// inside it), without loading the parts from model files: for the layout of the calibration (user 2026-11-05).
Vec2d pressure_calibration_frame_size(const DynamicPrintConfig &config, const PressureCalibrationSettings &settings,
                                      const PressureCalibrationTest &test, size_t id_item);

// Where a frame goes (calibration_layout()): the bed (0, 1... as the plater's multiple beds), the minimum corner of the frame without
// its brim in bed coordinates, and whether it is turned 90° (counter-clockwise).
struct CalibrationPlacement
{
    int   bed { -1 };
    Vec2d position { Vec2d::Zero() };
    bool  turned { false };
};
// The frames (sizes without the brim) on as few beds as possible (user 2026-11-05: as many per plate as possible): each frame's brim
// inside the bed with 1 mm to spare and clear of the keep-out zones (grown by their clearance); neighbouring frames share the brim
// between them (one brim width apart) or, when that needs fewer beds, touch (no brim between them). turn: frames may be turned 90°
// (the arms of Ellis' chevrons stay at ±45°, the same test). MaxRects (best short side fit) over many orders of the frames, the
// largest first, seeded (the same input gives the same layout); the fewest beds, then the most frames on the first bed. Of the
// layouts with that many beds the tidiest (user 2026-11-05: "organized nicely"): rows in the order of the frames (groups: the
// acceleration of each frame, a row each if they fit), all upright, else all turned, else the packed one; each bed's frames
// centred. Empty if a frame fits on no bed or more than max_beds are needed.
std::vector<CalibrationPlacement> calibration_layout(const std::vector<Vec2d> &sizes, const BoundingBoxf &bed, const std::vector<BoundingBoxf> &zones,
                                                     double brim, bool turn, int max_beds, const std::vector<int> &groups = {});

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

// Adaptive pressure advance calibration (OrcaSlicer's adaptive PA model, adaptive_pressure_advance_model): Ellis' chevrons once
// per speed and acceleration, Klipper only. Orca's recommendation: the external perimeter, perimeter and fastest infill speeds, at
// the slowest and fastest print accelerations.

// The default speeds (mm/s, sorted, without duplicates; user 2026-11-01): the small perimeter speed (the slowest walls), the fastest
// speed of the print settings (perimeters, infill, solid infill, top solid infill) capped by the max volumetric speeds, and
// halfway between them. config: with the settings applied.
std::vector<double> adaptive_pa_default_speeds(const DynamicPrintConfig &config, const PressureCalibrationSettings &settings);
// Klipper's max accel of the filament's speed mode (klipper_max_accel_slow / _fast, else klipper_max_accel): what the printer
// prints at without an acceleration command.
double adaptive_pa_speed_mode_accel(const DynamicPrintConfig &config);
// The default accelerations (mm/s², sorted, without duplicates; user 2026-11-01): 25 % of the Klipper max accel of the slow speed
// mode, the acceleration the filament prints at (the default acceleration, 0: adaptive_pa_speed_mode_accel()) and the Klipper
// max accel of the fast speed mode.
std::vector<double> adaptive_pa_default_accelerations(const DynamicPrintConfig &config);

// The length of a side of the chevrons (mm) for a test at speed and acceleration, so that at least half of each side prints at
// the speed: the acceleration and deceleration take speed² / acceleration, and Klipper's minimum_cruise_ratio
// (klipper_minimum_cruise_ratio) keeps that fraction of a move at the top speed. At least min_side, rounded up to 1 mm.
double adaptive_pa_side_length(const DynamicPrintConfig &config, double speed, double acceleration, double min_side);

// The speed the walls of the chevrons print at (mm/s): speed capped by the max volumetric speeds (print and filament), as the
// G-code generator does. config: with the settings applied.
double adaptive_pa_effective_speed(const DynamicPrintConfig &config, const PressureCalibrationSettings &settings, double speed);
// The volumetric flow of the walls at speed (mm³/s), the effective speed times the cross section of a line of the settings'
// width and layer height. This is the flow the adaptive PA model takes.
double adaptive_pa_flow(const DynamicPrintConfig &config, const PressureCalibrationSettings &settings, double speed);

// One measurement of the model: the best pressure advance at a flow (mm³/s) and acceleration (mm/s²).
struct AdaptivePAPoint
{
    double pa;
    double flow;
    double acceleration;
};
// The model text, one "PA,flow,acceleration" line per point (Orca's format), sorted by acceleration and flow.
std::string adaptive_pa_model(std::vector<AdaptivePAPoint> points);

// The model of a filament holds one section per nozzle diameter (user 2026-11-01: the filaments are shared by the 0.25 to 0.6 mm
// nozzles), each headed by "; nozzle <diameter>" with Orca's lines under it. Lines before the first header belong to no nozzle.
std::string adaptive_pa_nozzle_header(double nozzle);
// The lines of the section of a nozzle, empty if there is none.
std::string adaptive_pa_model_section(const std::string &model, double nozzle);
// The model lines the G-code uses for a nozzle (doc section 46): its section, else the lines before the first section.
std::string adaptive_pa_model_for_nozzle(const std::string &model, double nozzle);
// The model with the section of a nozzle replaced by lines (added at the end if it had none), the other sections as they were.
std::string adaptive_pa_model_set_section(const std::string &model, double nozzle, const std::string &lines);

// The tests of the calibration (speed, acceleration, flow, nozzle, the best value typed in the results, -1: none, and the number on
// its frame, -1: unknown), kept between the calibration prints (one per acceleration) and the results, as text.
struct AdaptivePATest
{
    double speed;
    double acceleration;
    double flow;
    double nozzle;
    double pa { -1. };
    int    id { -1 };
};
std::string                 adaptive_pa_tests_serialize(const std::vector<AdaptivePATest> &tests);
std::vector<AdaptivePATest> adaptive_pa_tests_deserialize(const std::string &text);
// The kept tests of a filament with the tests of one print (all of one nozzle and acceleration) instead of the kept ones of that
// nozzle and acceleration, sorted by nozzle and acceleration (the order of the prints' test numbers kept); the other nozzles'
// and accelerations' tests stay (user 2026-11-05: kept per filament, the filaments are shared by the nozzles).
std::vector<AdaptivePATest> adaptive_pa_tests_merge(const std::vector<AdaptivePATest> &kept, const std::vector<AdaptivePATest> &print);

// The measurements of a nozzle in a filament's adaptive_pressure_advance_model (its section, adaptive_pa_model_section()) as tests,
// for the accelerations kept has no tests of (user 2026-11-05: the results dialog shows them when the kept tests are gone, e.g.
// another computer): speed 0 (unknown), the flow, acceleration and value of each line, in the order of the flows.
std::vector<AdaptivePATest> adaptive_pa_tests_from_model(const std::vector<AdaptivePATest> &kept, const std::string &model, double nozzle);

// The accelerations the adaptive pressure advance uses for the features of a print (doc section 46; user 2026-11-05: shown in the
// results so the calibrated accelerations can be checked against the print profiles). config: the full config with the print
// preset. Default acceleration 0 (no acceleration commands): one entry, the Klipper max accel of the filament's speed mode.
// Otherwise per feature its acceleration as GCodeGenerator::_extrude() resolves it (a percentage of the default acceleration,
// 0 = the next setting of the chain, then the default acceleration). Sorted by acceleration, the features of each together.
struct AdaptivePAPrintAcceleration
{
    double                   acceleration;
    std::vector<std::string> features;
};
std::vector<AdaptivePAPrintAcceleration> adaptive_pa_print_accelerations(const DynamicPrintConfig &config);

// The kept tests of one calibration print (a nozzle and an acceleration, within 0.5 mm/s²), as indices into kept in the order of
// their frames: the test numbered 1 on its frame first (user 2026-11-05: each print numbers its tests 1, 2, 3... in the order of the
// speeds typed; the kept tests of an acceleration keep that order).
std::vector<size_t> adaptive_pa_calibration_tests(const std::vector<AdaptivePATest> &kept, double nozzle, double acceleration);
// The accelerations of the kept calibrations of a nozzle, sorted.
std::vector<double> adaptive_pa_calibration_accelerations(const std::vector<AdaptivePATest> &kept, double nozzle);

// One row of the results: an acceleration and the best value of the tests numbered 1, 2, 3... on the frames of its print
// (pa[k] for test k + 1, -1: empty).
struct AdaptivePAResultRow
{
    double              acceleration;
    std::vector<double> pa;
};
// The model points of the results (user 2026-11-05: the user types the acceleration and the values, the flows come from the kept
// tests): each row's values with the flow and acceleration of its tests. The typed values go into kept (pa of the tests of the
// nozzle, -1 for the ones without a value). Empty with error set when: a row's acceleration has no kept calibration for the nozzle,
// a value is for a test number the print didn't have, two rows have the same acceleration, a row has a single value (the model
// interpolates over the flow of each acceleration), a value is not from 0 to 2, or there is no value at all.
std::vector<AdaptivePAPoint> adaptive_pa_results(std::vector<AdaptivePATest> &kept, double nozzle, const std::vector<AdaptivePAResultRow> &rows,
                                                 std::string &error);
// The typed values of a nozzle (kept after adaptive_pa_results()) that go against OrcaSlicer's rule "the optimal pressure advance
// value is less the faster you print and the higher the acceleration" (user 2026-11-05: a warning, a misread chevron): a test
// with a higher value than a test of lower flow at the same acceleration, or than the test of the same speed at a lower
// acceleration. One line each, empty if none.
std::vector<std::string> adaptive_pa_results_warnings(const std::vector<AdaptivePATest> &kept, double nozzle);

} // namespace Slic3r

#endif // slic3r_PressureCalibration_hpp_
