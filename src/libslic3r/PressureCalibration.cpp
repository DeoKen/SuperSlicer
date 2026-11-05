#include "PressureCalibration.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <random>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>

#include "ClipperUtils.hpp"
#include "Exception.hpp"
#include "FileReader.hpp"
#include "Flow.hpp"
#include "format.hpp"
#include "GCode/BedKeepOut.hpp"
#include "I18N.hpp"
#include "Model.hpp"
#include "Slicing.hpp"
#include "Tesselate.hpp"
#include "TriangleMesh.hpp"

namespace Slic3r {

static const std::string CHECK_ALL = "CheckAll";

const std::vector<std::string>& pressure_calibration_roles()
{
    static const std::vector<std::string> roles {
        "InternalInfill", "BridgeInfill", "ExternalPerimeter", "GapFill", "InternalBridgeInfill", "Ironing", "OverhangPerimeter",
        "Perimeter", "SolidInfill", "SupportMaterial", "SupportMaterialInterface", "ThinWall", "TopSolidInfill", "FirstLayer"
    };
    return roles;
}

// The settings of each extrusion role (SuperSlicer's maps). The SuperSlicer only settings (overhangs_width,
// thin_walls_min_width, thin_walls_speed) are missing: those roles get the defaults, as SuperSlicer does for a role without
// a value. A role without a width entry (bridges, gap fill, ironing) gets the default width, as in SuperSlicer.
static const std::map<std::string, std::string> WIDTH_KEY {
    { "InternalInfill",           "infill_extrusion_width" },
    { "ExternalPerimeter",        "external_perimeter_extrusion_width" },
    { "OverhangPerimeter",        "" },
    { "Perimeter",                "perimeter_extrusion_width" },
    { "SolidInfill",              "solid_infill_extrusion_width" },
    { "SupportMaterial",          "support_material_extrusion_width" },
    { "SupportMaterialInterface", "support_material_extrusion_width" },
    { "ThinWall",                 "" },
    { "TopSolidInfill",           "top_infill_extrusion_width" },
    { "FirstLayer",               "first_layer_extrusion_width" },
};
static const std::map<std::string, std::string> SPEED_KEY {
    { "InternalInfill",           "infill_speed" },
    { "BridgeInfill",             "bridge_speed" },
    { "ExternalPerimeter",        "external_perimeter_speed" },
    { "GapFill",                  "gap_fill_speed" },
    { "InternalBridgeInfill",     "internal_bridge_speed" },
    { "Ironing",                  "ironing_speed" },
    { "OverhangPerimeter",        "overhangs_speed" },
    { "Perimeter",                "perimeter_speed" },
    { "SolidInfill",              "solid_infill_speed" },
    { "SupportMaterial",          "support_material_speed" },
    { "SupportMaterialInterface", "support_material_interface_speed" },
    { "ThinWall",                 "" },
    { "TopSolidInfill",           "top_solid_infill_speed" },
    { "FirstLayer",               "first_layer_speed" },
};

std::vector<double> pressure_calibration_values(double start, double end, double increment)
{
    std::vector<double> out;
    for (double value = start; value <= end + increment / 2.;) {
        if (value <= end) {
            out.emplace_back(std::round(value * 1000000.) / 1000000.);
            value += increment;
        } else {
            out.emplace_back(end);
            break;
        }
    }
    return out;
}

std::string pressure_calibration_command(GCodeFlavor flavor, bool smooth_time)
{
    return flavor == gcfKlipper ? (smooth_time ? "SET_PRESSURE_ADVANCE SMOOTH_TIME=" : "SET_PRESSURE_ADVANCE ADVANCE=") :
           flavor == gcfMarlinFirmware ? "M900 K" : flavor == gcfRepRapFirmware ? "M572 S" : "";
}

static std::string calibration_file(const std::string &resources_dir, const std::string &file)
{
    return (boost::filesystem::path(resources_dir) / "calibration" / "filament_pressure" / file).string();
}

std::string pressure_calibration_bend_file(const std::string &resources_dir, double nozzle)
{
    // The model of the nozzle, as SuperSlicer names it: "0.40" for 0.4.
    std::string nozzle_str = std::to_string(nozzle);
    nozzle_str.erase(nozzle_str.find_last_not_of('0') + 2, std::string::npos);
    if (nozzle_str.back() == '.')
        nozzle_str += '0';
    const std::string file = calibration_file(resources_dir, "scaled_with_nozzle_size/90_bend_" + nozzle_str + ".3mf");
    return boost::filesystem::exists(file) ? file : std::string();
}

std::string pressure_calibration_base_file(const std::string &resources_dir)
{
    return calibration_file(resources_dir, "base_plate.3mf");
}

ModelVolume* calibration_add_part(ModelObject *object, const std::string &file, const Vec3d &move, const Vec3d &scale)
{
    Model        model = FileReader::load_model(file);
    ModelVolume *added = nullptr;
    for (ModelObject *part_object : model.objects) {
        // The object was centered when it was loaded: move the part as the object was moved.
        Vec3d delta = Vec3d::Zero();
        if (object->origin_translation != Vec3d::Zero()) {
            part_object->center_around_origin();
            delta = object->origin_translation - part_object->origin_translation;
        }
        for (ModelVolume *volume : part_object->volumes) {
            volume->translate(delta + move);
            if (scale != Vec3d(1., 1., 1.))
                volume->scale(scale);
            added = object->add_volume(*volume, ModelVolumeType::MODEL_PART);
            added->name = boost::filesystem::path(file).filename().string();
        }
    }
    object->invalidate_bounding_box();
    return added;
}

// SuperSlicer's magical_scaling(): the 90° bend models are 4 lines of the nozzle thick, scale them to 2 loops of the width
// without gap fill. The overlaps are SuperSlicer's defaults (100 %), which is PrusaSlicer's spacing.
static double bend_scale(double nozzle, double width, double layer_height)
{
    const double spacing      = width - layer_height * (1. - 0.25 * M_PI);
    const double overlap      = width - spacing;
    const double first_pair   = 2. * width - overlap;
    const double second_pair  = 2. * width - overlap;
    return (first_pair + second_pair) / (nozzle * 4.);
}

void pressure_calibration_apply_settings(DynamicPrintConfig &config, const PressureCalibrationSettings &s)
{
    if (s.speed <= 0. || s.first_layer_speed <= 0. || s.line_ratio <= 0. || s.layers < 2 || s.layer_height <= 0. ||
        s.first_layer_height <= 0. || s.fan < 0 || s.fan > 100 || s.acceleration < 0. || s.wall_count < 1 || s.wall_count > 10 ||
        s.side_length < 5. || s.pattern_spacing < 0.)
        throw Slic3r::RuntimeError(_u8L("Invalid pressure calibration settings: speeds, line width and layer heights above 0, "
                                        "at least 2 layers, fan 0 to 100 %, acceleration 0 or more, 1 to 10 walls, side length at "
                                        "least 5 mm, spacing 0 or more."));
    if (s.first_layer_fan != 0 && s.first_layer_fan != s.fan)
        throw Slic3r::RuntimeError(_u8L("First layer fan: 0 (off) or the same as the fan. PrusaSlicer has no fan speed of its "
                                        "own for the first layer."));
    const auto set_float = [&config](const char *key, double value) {
        if (config.has(key))
            config.set_key_value(key, new ConfigOptionFloat(value));
    };
    const auto set_float_or_percent = [&config](const char *key, double value) {
        if (config.has(key))
            config.set_key_value(key, new ConfigOptionFloatOrPercent(value, false));
    };
    // Per extruder values: all of them.
    const auto set_ints = [&config](const char *key, int value) {
        if (auto *opt = config.option<ConfigOptionInts>(key); opt != nullptr)
            opt->values.assign(std::max<size_t>(1, opt->values.size()), value);
    };
    // Print.
    set_float("layer_height", s.layer_height);
    set_float_or_percent("first_layer_height", s.first_layer_height);
    set_float_or_percent("first_layer_speed", s.first_layer_speed);
    // Every acceleration the default one: Ellis sets one acceleration for everything, or none.
    set_float_or_percent("default_acceleration", s.acceleration);
    for (const char *key : { "perimeter_acceleration", "external_perimeter_acceleration", "first_layer_acceleration",
                             "first_layer_acceleration_over_raft", "travel_acceleration", "travel_short_distance_acceleration",
                             "infill_acceleration", "solid_infill_acceleration", "top_solid_infill_acceleration",
                             "bridge_acceleration", "internal_bridge_acceleration", "wipe_tower_acceleration",
                             "gap_fill_acceleration", "ironing_acceleration", "support_material_acceleration",
                             "support_material_interface_acceleration", "brim_acceleration" })
        set_float_or_percent(key, 0.);
    // Filament: the fan from the second layer (or from the first one), no ramp, no layer time cooling (fan speed-up or
    // slowdown), the feature fan speeds falling back to the default fan speed.
    set_ints("default_fan_speed", s.fan);
    for (const char *key : { "perimeter_fan_speed", "external_perimeter_fan_speed", "infill_fan_speed", "solid_infill_fan_speed",
                             "top_fan_speed", "gap_fill_fan_speed", "support_material_fan_speed", "support_material_interface_fan_speed" })
        set_ints(key, -1);
    set_ints("full_fan_speed_layer", 0);
    set_ints("fan_below_layer_time", 0);
    set_ints("slowdown_below_layer_time", 0);
    set_ints("disable_fan_first_layers", s.first_layer_fan == 0 ? 1 : 0);
}

static std::string num(double value)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", value);
    return buf;
}

std::string pressure_calibration_object_name(const PressureCalibrationTest &test, size_t id_item)
{
    std::string name = "PA test " + std::to_string(id_item) + " " + test.role;
    if (test.speed > 0.)
        name += " " + num(test.speed) + " mm/s";
    if (test.acceleration > 0.)
        name += " " + num(test.acceleration) + " mm/s2";
    if (! test.values.empty())
        name += " " + num(test.values.front()) + " - " + num(test.values.back()) +
                (test.values.size() > 1 ? " by " + num(test.values[1] - test.values[0]) : std::string());
    return name;
}

std::string pressure_calibration_summary(const DynamicPrintConfig &config, const PressureCalibrationSettings &s,
                                         const std::vector<PressureCalibrationTest> &tests)
{
    const double nozzle = config.opt<ConfigOptionFloats>("nozzle_diameter")->get_at(0);
    std::string  out;
    const auto   line = [&out](const std::string &text) { out += "; " + text + "\n"; };
    // Adaptive pressure advance calibration: the speed and acceleration of each test below.
    const bool speed_per_test = ! tests.empty() && std::all_of(tests.begin(), tests.end(), [](const PressureCalibrationTest &t) { return t.speed > 0.; });
    const bool accel_per_test = std::any_of(tests.begin(), tests.end(), [](const PressureCalibrationTest &t) { return t.acceleration > 0.; });
    line(accel_per_test ? "### Adaptive pressure advance calibration (Calibration menu) ###" :
                          "### Pressure calibration (SuperSlicer style, Calibration menu) ###");
    line("Print settings (Ellis' defaults unless changed in the dialog):");
    if (s.ellis_bends) {
        line(" - Print speed: " + (speed_per_test ? std::string("per test") : num(s.speed) + " mm/s"));
        line(" - Line width: " + num(s.line_ratio) + " % (" + num(nozzle * s.line_ratio / 100.) + " mm)");
        line(" - Layer count: " + std::to_string(s.layers));
        line(" - Wall count: " + std::to_string(s.wall_count) + " (open lines)");
        line(" - Side length: " + num(s.side_length) + " mm");
        line(" - Spacing: " + num(s.pattern_spacing) + " mm");
        line(" - Corner angle: 90 degrees");
    } else
        line(" - Bends: SuperSlicer's, width and speed of the extrusion role of each test, 5 layers");
    line(" - Layer height: " + num(s.layer_height) + " mm");
    line(" - First layer height: " + num(s.first_layer_height) + " mm");
    line(" - First layer printing speed: " + num(s.first_layer_speed) + " mm/s");
    line(" - Acceleration: " + (accel_per_test ? std::string("per test (SET_VELOCITY_LIMIT before each wall)") :
                                s.acceleration > 0. ? num(s.acceleration) + " mm/s^2" : std::string("Disabled")));
    line(" - Fan speed: " + std::to_string(s.fan) + " %");
    line(" - First layer fan speed: " + std::to_string(s.first_layer_fan) + " %");
    line("Printer and filament: the presets (nozzle " + num(nozzle) + " mm, temperatures, retraction, travel speed).");
    for (size_t i = 0; i < tests.size(); ++ i) {
        const PressureCalibrationTest &t = tests[i];
        line("Test " + std::to_string(i) + ": " + t.role + (t.smooth_time ? ", smooth time" : "") + ", first layer value " + num(t.first_pa));
        std::string values;
        for (double v : t.values)
            values += (values.empty() ? "" : ", ") + num(v);
        line(" - " + std::string(t.smooth_time ? "Smooth time" : "PA") + " values (" + std::to_string(t.values.size()) + "): " + values);
        if (t.speed > 0. || t.acceleration > 0.) {
            const double speed = t.speed > 0. ? t.speed : s.speed;
            line(" - Speed " + num(adaptive_pa_effective_speed(config, s, speed)) + " mm/s, acceleration " +
                 (t.acceleration > 0. ? num(t.acceleration) + " mm/s^2" : std::string("not set")) + ", flow " +
                 num(std::round(adaptive_pa_flow(config, s, speed) * 100.) / 100.) + " mm^3/s");
        }
    }
    return out;
}

// A prism of the polygons from z0 to z1.
static indexed_triangle_set prism(const ExPolygons &polys, double z0, double z1)
{
    indexed_triangle_set its;
    for (const ExPolygon &poly : polys) {
        its_merge(its, wall_strip(poly.contour, z0, z1));
        for (const Polygon &hole : poly.holes)
            its_merge(its, wall_strip(hole, z0, z1));
    }
    its_merge(its, triangulate_expolygons_3d(polys, z1, true));
    its_merge(its, triangulate_expolygons_3d(polys, z0, false));
    its_merge_vertices(its);
    return its;
}

// The width of a border of the frame: SuperSlicer's 4 lines of the first layer flow (the first layer width, else the
// perimeter width, else the default width) and an overlap. Without the overlap (Ellis' frame) the 4 lines fill it exactly: with
// it PrusaSlicer printed a line of gap fill in the middle of every border (0.11 mm wide on the user's profile, 2026-10-26).
static double frame_border(const DynamicPrintConfig &config, bool superslicer_overlap)
{
    const double nozzle             = config.opt<ConfigOptionFloats>("nozzle_diameter")->get_at(0);
    const double first_layer_height = config.get_abs_value("first_layer_height", config.opt_float("layer_height"));
    const auto  &first_width_opt = config.option<ConfigOptionFloatOrPercent>("first_layer_extrusion_width")->value > 0. ?
        *config.option<ConfigOptionFloatOrPercent>("first_layer_extrusion_width") :
        (config.option<ConfigOptionFloatOrPercent>("perimeter_extrusion_width")->value > 0. ?
            *config.option<ConfigOptionFloatOrPercent>("perimeter_extrusion_width") : *config.option<ConfigOptionFloatOrPercent>("extrusion_width"));
    const Flow   flow = Flow::new_from_config_width(frPerimeter, first_width_opt, float(nozzle), float(first_layer_height));
    return flow.width() + 3 * flow.spacing() + (superslicer_overlap ? flow.width() - flow.spacing() : 0.);
}

// Ellis' chevrons pointing to -Y (as SuperSlicer's bends), 90°, stacked along Y: wall k of chevron j has its apex at
// apex(j, k); the box of a chevron (all its walls, with the line width) is centered on (0, j * pitch).
struct EllisChevrons
{
    double width;         // line width
    double spacing_angle; // Ellis: the line spacing / sin(half the corner angle), between the apexes of two walls
    double pitch;         // Ellis: spacing + line width + (walls - 1) * spacing_angle, between two chevrons
    Vec2d  box_size;
    Vec2d  box_center;    // of chevron 0 with the apex of its first wall at (0, 0)

    EllisChevrons(double nozzle, double layer_height, const PressureCalibrationSettings &ps)
    {
        width         = nozzle * ps.line_ratio / 100.;
        spacing_angle = (width - layer_height * (1. - 0.25 * M_PI)) / std::sin(0.25 * M_PI);
        pitch         = ps.pattern_spacing + width + (ps.wall_count - 1) * spacing_angle;
        BoundingBoxf bb;
        for (int k : { 0, ps.wall_count - 1 })
            for (const Vec2d &p : outline(Vec2d(0., k * spacing_angle), ps.side_length, ps.side_length))
                bb.merge(p);
        box_size   = bb.size();
        box_center = bb.center();
    }
    Vec2d apex(const Vec2d &center_of_chevron, int wall) const { return center_of_chevron - box_center + Vec2d(0., wall * spacing_angle); }

    // One wall: the line from the end of the left arm through the apex to the end of the right arm, one line width wide, with
    // square ends half a width beyond the ends of the line and a round outer corner, the shape such a line leaves. Arachne
    // slices it into one open line of the width from end to end (wider only within 0.15 mm of the corner, the thicker inside
    // of the corner); classic perimeters vary it.
    std::vector<Vec2d> outline(const Vec2d &apex, double side_left, double side_right) const
    {
        const Vec2d  dL = Vec2d(-1., 1.).normalized(), dR = Vec2d(1., 1.).normalized();
        const Vec2d  nL = Vec2d(-1., -1.).normalized(), nR = Vec2d(1., -1.).normalized();
        const double r  = width / 2.;
        const Vec2d  eL = apex + dL * (side_left + r), eR = apex + dR * (side_right + r);
        std::vector<Vec2d> out;
        const double aL = std::atan2(nL.y(), nL.x()), aR = std::atan2(nR.y(), nR.x());
        for (int i = 0; i <= 16; ++ i) {
            const double a = aL + (aR - aL) * i / 16.;
            out.emplace_back(apex + Vec2d(std::cos(a), std::sin(a)) * r);
        }
        for (const Vec2d &p : { Vec2d(eR + nR * r), Vec2d(eR - nR * r), Vec2d(apex + Vec2d(0., r * std::sqrt(2.))), Vec2d(eL - nL * r), Vec2d(eL + nL * r) })
            out.emplace_back(p);
        return out;
    }

    // Wall k of the chevron whose box is centered on center, scaled, as long as its box.
    Polygons wall(const Vec2d &center, int k, double side) const
    {
        Polygon poly;
        for (const Vec2d &p : outline(apex(center, k), side, side))
            poly.points.emplace_back(Point::new_scale(p.x(), p.y()));
        poly.make_counter_clockwise();
        return { poly };
    }
    // The same with the arms running over the frame, the left one to left_end, the right one to right_end, cut there square to
    // the edge (above the first layer, user 2026-10-25/26).
    Polygons wall(const Vec2d &center, int k, double side, double left_end, double right_end) const
    {
        const Vec2d a = apex(center, k);
        Polygon     poly;
        for (const Vec2d &p : outline(a, std::max(side, (a.x() - left_end) * std::sqrt(2.) + width), std::max(side, (right_end - a.x()) * std::sqrt(2.) + width)))
            poly.points.emplace_back(Point::new_scale(p.x(), p.y()));
        poly.make_counter_clockwise();
        const BoundingBox bb = poly.bounding_box();
        const Polygon     keep({ Point(scaled(left_end), bb.min.y() - 1), Point(scaled(right_end), bb.min.y() - 1),
                                 Point(scaled(right_end), bb.max.y() + 1), Point(scaled(left_end), bb.max.y() + 1) });
        return intersection(Polygons{ poly }, Polygons{ keep });
    }
};

// SuperSlicer places the bends at this x.
static constexpr double X_OFFSET_90_BEND = 1.2;

size_t pressure_calibration_wall_objects(const PressureCalibrationSettings &settings, const PressureCalibrationTest &test)
{
    return settings.ellis_bends && test.role != CHECK_ALL ? size_t(settings.wall_count) : 0;
}

void pressure_calibration_build_walls(const std::vector<ModelObject*> &walls, const DynamicPrintConfig &config,
                                      const PressureCalibrationSettings &ps_in, const PressureCalibrationTest &test, size_t id_item)
{
    PressureCalibrationSettings ps = ps_in;
    if (test.side_length > 0.)
        ps.side_length = test.side_length;
    if (walls.size() != pressure_calibration_wall_objects(ps, test))
        throw Slic3r::RuntimeError("pressure_calibration_build_walls: wrong number of wall objects");
    const GCodeFlavor   flavor      = config.opt_enum<GCodeFlavor>("gcode_flavor");
    const double        nozzle      = config.opt<ConfigOptionFloats>("nozzle_diameter")->get_at(0);
    const double        layer       = config.opt_float("layer_height");
    const double        first_layer = config.get_abs_value("first_layer_height", layer);
    const double        height      = first_layer + layer * (ps.layers - 1);
    const EllisChevrons chevrons(nozzle, layer, ps);
    const double        border      = frame_border(config, false);
    for (size_t k = 0; k < walls.size(); ++ k) {
        ModelObject *object = walls[k];
        for (size_t j = 0; j < test.values.size(); ++ j) {
            // In the coordinates of the base (as calibration_add_part() places the parts of the frame).
            const Vec2d  center(X_OFFSET_90_BEND + object->origin_translation.x(), double(j) * chevrons.pitch + object->origin_translation.y());
            // The first layer within the box of the chevron, its ends half a line width into the frame; above it (no frame there) the
            // arms over the frame: the left ones over the whole left border, to its outer edge, the right ones as far into the tab
            // (over its perimeters, short of the numbers 2 mm from the box). The first layer anchors the lines by half a line width,
            // the layers above by the whole border (user 2026-10-25, the right side 2026-10-26).
            const double z0   = object->origin_translation.z();
            const double half = chevrons.box_size.x() / 2. - chevrons.width / 2. + border;
            indexed_triangle_set its = prism(union_ex(chevrons.wall(center, int(k), ps.side_length)), z0, z0 + first_layer);
            its_merge(its, prism(union_ex(chevrons.wall(center, int(k), ps.side_length, center.x() - half, center.x() + half)),
                                 z0 + first_layer, z0 + height));
            ModelVolume *wall = object->add_volume(TriangleMesh(std::move(its)));
            wall->name = "wall " + std::to_string(k + 1) + " " + num(test.values[j]);
            // Adaptive PA calibration: the acceleration of the test before every wall (the print settings write none), so that each
            // test prints at its own acceleration whatever printed before it.
            const std::string accel = test.acceleration > 0. ?
                "SET_VELOCITY_LIMIT ACCEL=" + std::to_string(std::lround(test.acceleration)) + "\n" : std::string();
            wall->config.set_key_value("region_gcode", new ConfigOptionString(accel +
                pressure_calibration_command(flavor, test.smooth_time) + std::to_string(test.values[j]) + " ; " + test.role +
                " wall " + std::to_string(k + 1) + "\n"));
        }
        // One open line of the width per wall and layer, at the speed whatever the small perimeter settings.
        const double speed = test.speed > 0. ? test.speed : ps.speed;
        ModelConfig &oc = object->config;
        oc.set_key_value("perimeter_generator",                new ConfigOptionEnum<PerimeterGeneratorType>(PerimeterGeneratorType::Arachne));
        oc.set_key_value("perimeters",                         new ConfigOptionInt(1));
        oc.set_key_value("perimeter_extrusion_width",          new ConfigOptionFloatOrPercent(chevrons.width, false));
        oc.set_key_value("external_perimeter_extrusion_width", new ConfigOptionFloatOrPercent(chevrons.width, false));
        oc.set_key_value("perimeter_speed",                    new ConfigOptionFloat(speed));
        oc.set_key_value("external_perimeter_speed",           new ConfigOptionFloatOrPercent(speed, false));
        oc.set_key_value("small_perimeter_speed",              new ConfigOptionFloatOrPercent(100., true));
        oc.set_key_value("thin_walls",                         new ConfigOptionBool(false));
        // The left arms above the first layer lie on the frame, another object: no overhang (bridge flow, overhang speed and fan,
        // the value set after the first piece) where this object has nothing below.
        oc.set_key_value("overhangs",                          new ConfigOptionBool(false));
        oc.set_key_value("enable_dynamic_overhang_speeds",     new ConfigOptionBool(false));
        oc.set_key_value("fill_density",                       new ConfigOptionPercent(0));
        oc.set_key_value("top_solid_layers",                   new ConfigOptionInt(0));
        oc.set_key_value("bottom_solid_layers",                new ConfigOptionInt(0));
        oc.set_key_value("brim_width",                         new ConfigOptionFloat(0));
        // PrusaSlicer's default Arachne settings: with a small minimum bead width (the user's 40 %, filter deviation 5 %) Arachne
        // splits the slightly thicker inside of the corner into two thin beads: the line stops before the corner, travels
        // and a blob is printed at the corner on its own (found on the user's profile, 2026-10-23).
        for (const char *key : { "min_bead_width", "min_feature_size", "wall_transition_length", "wall_transition_filter_deviation",
                                 "wall_transition_angle", "wall_distribution_count" })
            oc.set_key_value(key, FullPrintConfig::defaults().option(key)->clone());
        object->name = pressure_calibration_object_name(test, id_item) + " wall " + std::to_string(k + 1);
        object->invalidate_bounding_box();
    }
}

// add_parts false: the parts from model files (the numbers, SuperSlicer's bends and borders) are left out, for the size of the frame.
static bool pressure_calibration_build_impl(ModelObject &base, const DynamicPrintConfig &config, const PressureCalibrationSettings &ps_in,
                                            const PressureCalibrationTest &test, size_t id_item, const std::string &resources_dir, bool add_parts)
{
    const auto add_part = [add_parts](ModelObject *o, const std::string &file, const Vec3d &move, const Vec3d &scale) -> ModelVolume* {
        return add_parts ? calibration_add_part(o, file, move, scale) : nullptr;
    };
    // CheckAll: SuperSlicer's bends, one per extrusion role.
    PressureCalibrationSettings ps = ps_in;
    ps.ellis_bends = pressure_calibration_wall_objects(ps_in, test) > 0;
    if (test.side_length > 0.)
        ps.side_length = test.side_length;
    ModelObject *object = &base;
    const GCodeFlavor flavor = config.opt_enum<GCodeFlavor>("gcode_flavor");
    const double      nozzle = config.opt<ConfigOptionFloats>("nozzle_diameter")->get_at(0);
    const std::string bend_file = pressure_calibration_bend_file(resources_dir, nozzle);
    const auto        resource  = [&resources_dir](const std::string &file) { return calibration_file(resources_dir, file); };
    const std::vector<std::string> &roles = pressure_calibration_roles();

    // The settings of the profiles.
    const double first_layer_height = config.get_abs_value("first_layer_height", config.opt_float("layer_height"));
    const double base_layer_height  = config.opt_float("layer_height");
    const int    infill_every       = config.opt_int("infill_every_layers");
    // A width in mm, 0 if not set. PrusaSlicer: a % is of the layer height.
    const auto   width_of = [&config, nozzle](const std::string &key, double height) -> double {
        if (key.empty())
            return 0.;
        const auto *opt = config.option<ConfigOptionFloatOrPercent>(key);
        return opt == nullptr || opt->value == 0. ? 0. : Flow::new_from_config_width(frPerimeter, *opt, float(nozzle), float(height)).width();
    };
    const auto   speed_of = [&config](const std::string &role) -> double {
        const auto it = SPEED_KEY.find(role);
        return it == SPEED_KEY.end() || it->second.empty() || ! config.has(it->second) ? 0. : config.get_abs_value(it->second);
    };
    // The defaults when a role has no value (SuperSlicer: extrusion_width or the nozzle, default_speed).
    double default_width = width_of("extrusion_width", base_layer_height);
    if (default_width == 0.)
        default_width = nozzle;
    // SuperSlicer's default_speed doesn't exist here: the perimeter speed.
    const double default_speed = config.get_abs_value("perimeter_speed");

    // The width and speed of a role.
    struct RoleSettings { double width; double speed; std::string comment; };
    const auto role_settings = [&](const std::string &role) {
        RoleSettings out;
        out.comment = role;
        const auto width_it = WIDTH_KEY.find(role);
        if (width_it != WIDTH_KEY.end()) {
            out.width = width_of(width_it->second, role == "FirstLayer" ? first_layer_height : base_layer_height);
            if (out.width == 0.)
                out.width = default_width;
        } else {
            out.width   = default_width;
            out.comment = "defaults for " + role + " width spacing";
        }
        if (role == "ThinWall")
            out.width = default_width;
        const auto *first_layer_speed = config.option<ConfigOptionFloatOrPercent>("first_layer_speed");
        out.speed = role == "FirstLayer" && first_layer_speed->percent ? default_speed : speed_of(role);
        if (out.speed == 0.)
            out.speed = default_speed;
        return out;
    };

    // SuperSlicer's layout (models made for a 0.4 mm nozzle, exported with their origin at their center).
    const double initial_model_height = 0.2;
    const double initial_90_bend_x    = 42.;
    const double initial_90_bend_y    = 21.;
    const double initial_number_x     = 2.;
    const double initial_number_y     = 4.;
    const double initial_border_x     = 1.6;
    const double initial_border_y     = 21.;
    const double initial_point_xy     = 0.6;
    const double x_offset_90_bend     = X_OFFSET_90_BEND;
    const double xyz_scale            = nozzle / 0.4;
    const double z_scale_others       = first_layer_height / initial_model_height;
    const double z_others_pos         = first_layer_height / 2.;
    const double z_scaled_model_height = initial_model_height * (first_layer_height / initial_model_height);
    const double first_layer_border   = frame_border(config, ! ps.ellis_bends);
    // Between the numbers and the inner (the ends of the bends) and outer edges of the tab (SuperSlicer: a nozzle on the inner
    // side, about none on the outer one; user 2026-10-25: "the frame that has the numbers is too small").
    const double tab_margin           = 2.;

    const bool   check_all = test.role == CHECK_ALL;
    const size_t count     = check_all ? roles.size() : test.values.size();
    // The frame is made from the first two bends.
    if (count < 2)
        throw Slic3r::RuntimeError(_u8L("Pressure calibration: at least 2 values."));

    // Layer height of the bends: the combined infill layers, the first layer (SuperSlicer's bends only).
    double combined_layer_height = base_layer_height;
    bool   own_layer_height      = false;
    if (! ps.ellis_bends && test.role == "InternalInfill" && infill_every > 1) {
        combined_layer_height = std::min(infill_every * base_layer_height, nozzle);
        own_layer_height      = true;
    } else if (! ps.ellis_bends && test.role == "FirstLayer") {
        combined_layer_height = first_layer_height;
        own_layer_height      = true;
    }
    const double bend_layer_height = own_layer_height ? combined_layer_height : base_layer_height;
    // SuperSlicer: 5 layers (6 with an own layer height), Ellis: his layer count.
    const double bend_height       = ps.ellis_bends ? first_layer_height + base_layer_height * (ps.layers - 1) :
                                     own_layer_height ? first_layer_height + combined_layer_height * 5. :
                                                        first_layer_height + base_layer_height * 4.;
    const double z_scale_90_bend   = bend_height / initial_model_height;
    const double z_90_bend_pos     = bend_height / 2.;

    // Size of the numbers and borders: from the selected role.
    const RoleSettings selected      = role_settings(check_all ? std::string("ExternalPerimeter") : test.role);
    const double       scale_role    = bend_scale(nozzle, ps.ellis_bends ? nozzle * ps.line_ratio / 100. : check_all ? default_width : selected.width, bend_layer_height);
    // Ellis' chevrons: their box instead of the bend's.
    const EllisChevrons chevrons(nozzle, base_layer_height, ps);
    const double       xy_scaled_90_bend_x = ps.ellis_bends ? chevrons.box_size.x() : initial_90_bend_x * scale_role;
    const double       xy_scaled_90_bend_y = ps.ellis_bends ? chevrons.box_size.y() : initial_90_bend_y * scale_role;
    const double       xy_scaled_number_x  = initial_number_x * xyz_scale * scale_role;
    const double       xy_scaled_number_y  = initial_number_y * xyz_scale * scale_role;
    const double       xy_scaled_point_y   = initial_point_xy * xyz_scale * 1.5 * scale_role;

    // The bends.
    std::vector<Vec3d>        bend_positions;
    std::vector<ModelVolume*> bends;
    double                    bend_y_pos = 0.;
    for (size_t nb = 0; nb < count; ++ nb) {
        const std::string role = check_all ? roles[nb] : test.role;
        double            y_offset = 0.;
        if (check_all) {
            y_offset = 10.;
            if (nb > 0 && roles[nb - 1] == "ThinWall")
                bend_y_pos += y_offset;
        }
        if (ps.ellis_bends) {
            // The walls are objects of their own (pressure_calibration_build_walls()).
            bend_positions.emplace_back(x_offset_90_bend, double(nb) * chevrons.pitch, z_90_bend_pos);
            continue;
        }
        const RoleSettings settings = role_settings(role);
        const double       width    = check_all && WIDTH_KEY.find(role) == WIDTH_KEY.end() ? default_width : settings.width;
        const double       speed    = settings.speed;
        const double       scale    = bend_scale(nozzle, width, bend_layer_height);
        ModelVolume       *bend     = add_part(object, bend_file, Vec3d(x_offset_90_bend, bend_y_pos, z_90_bend_pos), Vec3d(scale, scale, z_scale_90_bend));
        bend_positions.emplace_back(x_offset_90_bend, bend_y_pos + y_offset, z_90_bend_pos);
        bend_y_pos = bend_positions.back().y() + width * 4. + nozzle * 4.;
        if (bend == nullptr)
            continue;
        bends.emplace_back(bend);
        // The width and speed of the role. SuperSlicer also sets the accelerations (print settings here: the bends print with
        // the perimeter accelerations of the print settings), a first layer width and a layer height (object settings, no
        // effect on a part).
        ModelConfig &vc = bend->config;
        vc.set_key_value("perimeter_extrusion_width",          new ConfigOptionFloatOrPercent(width, false));
        vc.set_key_value("external_perimeter_extrusion_width", new ConfigOptionFloatOrPercent(width, false));
        vc.set_key_value("perimeter_speed",                    new ConfigOptionFloat(speed));
        vc.set_key_value("external_perimeter_speed",           new ConfigOptionFloatOrPercent(speed, false));
        vc.set_key_value("gap_fill_speed",                     new ConfigOptionFloat(speed));
        vc.set_key_value("region_gcode", new ConfigOptionString(check_all ?
            ";" + pressure_calibration_command(flavor, test.smooth_time) + " ; " + settings.comment :
            pressure_calibration_command(flavor, test.smooth_time) + std::to_string(test.values[nb]) + " ; " + settings.comment + "\n"));
    }

    // The label of the test, on the row under the values: its number, and with an acceleration (adaptive PA calibration, user
    // 2026-11-05) the acceleration after a blank, e.g. "1 28000". Characters one step apart, as the digits of the values.
    const double      number_step = xy_scaled_number_x + nozzle;
    const std::string label       = std::to_string(id_item) + (test.acceleration > 0. ? " " + std::to_string(std::lround(test.acceleration)) : std::string());
    const double      label_width = double(label.size() - 1) * number_step + xy_scaled_number_x;

    // The values (every second bend), the borders and the number of the test.
    std::vector<Vec3d> number_positions;
    for (size_t nb = 0; nb < count && ! check_all; ++ nb) {
        if (nb == 1) {
            // The borders, from the numbers of the first bend.
            const Vec3d &bend_first = bend_positions.front();
            const Vec3d &bend_mid   = bend_positions[count / 2];
            // The sides centered between the first and the last bend. SuperSlicer centers them on the middle bend, half a bend
            // off with an even number of bends: the sides were short at one end and the bottom border apart from them
            // (SuperSlicer's "TOFIX: on odd/uneven PA_values array results in the borders being a little too short").
            const Vec3d &bend_last  = bend_positions.back();
            const double center_y   = (bend_first.y() + bend_last.y()) / 2.;
            const Vec3d  number_first = number_positions.front();
            const Vec3d  number_mid   = number_positions[number_positions.size() / 2];
            const Vec3d  number_last  = number_positions.back();
            // The tab as wide as the values or the label, whichever is wider.
            const double numbers_total_width  = std::max((number_last.x() + xy_scaled_number_x / 2.) - (number_first.x() - xy_scaled_number_x / 2.), label_width);
            const double total_height         = (bend_last.y() + xy_scaled_90_bend_y / 2.) - (bend_first.y() - xy_scaled_90_bend_y / 2.);
            const double box_left             = bend_mid.x() - xy_scaled_90_bend_x / 2.;
            const double box_right            = bend_mid.x() + xy_scaled_90_bend_x / 2.;
            const double right_outer          = box_right + numbers_total_width + 2. * tab_margin;
            const double bottom_inner         = bend_first.y() - xy_scaled_90_bend_y / 2. - nozzle;
            const double top_inner            = bend_last.y() + xy_scaled_90_bend_y / 2. + nozzle;
            if (ps.ellis_bends) {
                // One solid part, the outer rectangle without the inner one. The left border and the tab reach half a line width
                // into the chevrons (other objects), anchoring the ends of the lines: 0.13 mm overlap with the frame's lines on the
                // user's profile, as Ellis' pattern (0.132 mm; a whole line width: 0.36 mm; touching only, the lines stopped
                // 0.09 mm short). The frame is not cut around the walls (user 2026-10-25: "the frame should be a solid line",
                // "Maybe half a line width instead").
                // As wide as the other borders: half a line wider, it printed a line of gap fill.
                const double left_outer  = box_left + chevrons.width / 2. - first_layer_border;
                const double left_inner  = box_left + chevrons.width / 2.;
                const double right_inner = box_right - chevrons.width / 2.;
                const Vec2d  o           = object->origin_translation.head<2>();
                const auto   rect        = [&o](double x0, double y0, double x1, double y1) {
                    return Polygon({ Point::new_scale(x0 + o.x(), y0 + o.y()), Point::new_scale(x1 + o.x(), y0 + o.y()),
                                     Point::new_scale(x1 + o.x(), y1 + o.y()), Point::new_scale(x0 + o.x(), y1 + o.y()) });
                };
                const double z0    = object->origin_translation.z();
                ModelVolume *frame = object->add_volume(TriangleMesh(prism(diff_ex(Polygons{ rect(left_outer, bottom_inner - first_layer_border,
                    right_outer, top_inner + first_layer_border) }, Polygons{ rect(left_inner, bottom_inner, right_inner, top_inner) }),
                    z0, z0 + first_layer_height)));
                frame->name = "frame";
            } else {
                // SuperSlicer's 4 borders, the left one reaching a little into the bends.
                const double left_inner        = box_left - nozzle + first_layer_border / 2.;
                const double left_outer        = left_inner - first_layer_border;
                const double tb_border_x_pos   = (left_outer + right_outer) / 2.;
                const double tb_total_width_mm = right_outer - left_outer;
                const std::string border       = resource("pa_border.3mf");
                add_part(object, border, Vec3d((left_outer + left_inner) / 2., center_y, z_others_pos),
                                     Vec3d(first_layer_border / initial_border_x, (total_height + first_layer_border) / initial_90_bend_y, z_scale_others));
                add_part(object, border, Vec3d((box_right + right_outer) / 2., center_y, z_others_pos),
                                     Vec3d((right_outer - box_right) / initial_border_x, (total_height + first_layer_border) / initial_90_bend_y, z_scale_others));
                add_part(object, border, Vec3d(tb_border_x_pos, bottom_inner - first_layer_border / 2., z_others_pos),
                                     Vec3d(tb_total_width_mm / initial_border_x, first_layer_border / initial_border_y, z_scale_others));
                add_part(object, border, Vec3d(tb_border_x_pos, top_inner + first_layer_border / 2., z_others_pos),
                                     Vec3d(tb_total_width_mm / initial_border_x, first_layer_border / initial_border_y, z_scale_others));
            }
            // The label centered under the values (a label of one digit: where the test number always was).
            {
                const double values_center = (number_first.x() + number_last.x()) / 2.;
                const double center        = label.size() == 1 ? number_mid.x() : values_center;
                const double label_y       = bend_first.y() - xy_scaled_90_bend_y / 2. + xy_scaled_number_y / 2.;
                for (size_t j = 0; j < label.size(); ++ j)
                    if (std::isdigit(static_cast<unsigned char>(label[j])))
                        add_part(object, resource(std::string(1, label[j]) + ".3mf"),
                                             Vec3d(center - label_width / 2. + xy_scaled_number_x / 2. + double(j) * number_step, label_y, z_scaled_model_height),
                                             Vec3d(xyz_scale * scale_role, xyz_scale * scale_role, z_scale_others * 2.));
            }
        }
        if (nb % 2 == 1)
            continue;
        // The value, right of its bend: SuperSlicer engraves std::to_string() (6 decimals).
        const Vec3d      &bend   = bend_positions[nb];
        const std::string value  = std::to_string(test.values[nb]);
        double            xpos   = bend.x() + xy_scaled_90_bend_x / 2. + xy_scaled_number_x / 2. + tab_margin;
        const double      ypos   = bend.y() + xy_scaled_90_bend_y / 2. - xy_scaled_number_y / 2. + nozzle * 3.;
        const double      space  = xy_scaled_number_x / 2. + nozzle + xy_scaled_number_x / 2.;
        for (size_t j = 0; j < value.size(); ++ j) {
            if (j != 0)
                xpos += space;
            if (value[j] == '.') {
                const double right_edge_of_left_number = xpos + xy_scaled_number_x / 2. - space;
                const double left_edge_of_right_number = xpos + xy_scaled_number_x / 2. + nozzle * 2. + xy_scaled_number_x / 2. - space;
                const double point_xpos                = (right_edge_of_left_number + left_edge_of_right_number) / 2.;
                const Vec3d  pos(point_xpos, ypos - xy_scaled_number_y / 2. + xy_scaled_point_y / 2., z_scaled_model_height);
                // As wide as the strokes of the digits (0.4 of 2 mm), printed as they are (a thin wall). SuperSlicer's 0.6 mm was
                // too wide for a thin wall and too narrow for a loop: PrusaSlicer left the point out (user 2026-10-25).
                add_part(object, resource("point.3mf"), pos,
                                     Vec3d(0.4 / initial_point_xy * xyz_scale * scale_role, (xyz_scale + xyz_scale / 2.) * scale_role, z_scale_others * 2.));
                number_positions.emplace_back(pos);
                xpos -= xy_scaled_number_x / 2.;
            } else if (std::isdigit(static_cast<unsigned char>(value[j]))) {
                const Vec3d pos(xpos, ypos, z_scaled_model_height);
                add_part(object, resource(std::string(1, value[j]) + ".3mf"), pos,
                                     Vec3d(xyz_scale * scale_role, xyz_scale * scale_role, z_scale_others * 2.));
                number_positions.emplace_back(pos);
                xpos = number_positions.back().x();
            }
        }
    }

    // Object settings, as SuperSlicer. The first layer value for the base, the borders and the numbers on all their layers: the
    // numbers are two first layers high, SuperSlicer sets it on the first layer only (layer_z <= first layer height), so the
    // second layer of the numbers printed with the value of the last bend; Ellis prints the numbers with the start value.
    ModelConfig &oc = object->config;
    oc.set_key_value("bottom_fill_pattern",            new ConfigOptionEnum<InfillPattern>(ipMonotonic));
    oc.set_key_value("thin_walls",                     new ConfigOptionBool(true));
    oc.set_key_value("bottom_solid_layers",            new ConfigOptionInt(1));
    // Not in SuperSlicer: a 5 mm brim on the outside of the frame for adhesion, whatever the print settings (user 2026-11-01).
    // The objects of Ellis' walls have none.
    oc.set_key_value("brim_width",                     new ConfigOptionFloat(PRESSURE_CALIBRATION_BRIM_WIDTH));
    oc.set_key_value("brim_type",                      new ConfigOptionEnum<BrimType>(btOuterOnly));
    oc.set_key_value("brim_separation",                new ConfigOptionFloat(0));
    oc.set_key_value("brim_ears",                      new ConfigOptionBool(false));
    oc.set_key_value("fill_density",                   new ConfigOptionPercent(0));
    oc.set_key_value("gap_fill_enabled",               new ConfigOptionBool(true));
    oc.set_key_value("top_one_perimeter_type",         new ConfigOptionEnum<TopOnePerimeterType>(TopOnePerimeterType::None));
    oc.set_key_value("only_one_perimeter_first_layer", new ConfigOptionBool(false));
    oc.set_key_value("seam_position",                  new ConfigOptionEnum<SeamPosition>(spRear));
    // Not in SuperSlicer: classic perimeters, lines of the exact width as Ellis' pattern. Arachne widens the inner loop and
    // adds a tiny loop with its own retract / travel 1 mm from the corner of every bend (compared with Ellis' pattern on the
    // user's profile, 2026-10-23).
    oc.set_key_value("perimeter_generator",            new ConfigOptionEnum<PerimeterGeneratorType>(PerimeterGeneratorType::Classic));
    oc.set_key_value("top_solid_layers",               new ConfigOptionInt(0));
    if (ps.ellis_bends) {
        // The numbers (two layers high) at the first layer speed on their second layer too, as Ellis prints them.
        oc.set_key_value("perimeter_speed",            new ConfigOptionFloat(ps.first_layer_speed));
        oc.set_key_value("external_perimeter_speed",   new ConfigOptionFloatOrPercent(ps.first_layer_speed, false));
        oc.set_key_value("small_perimeter_speed",      new ConfigOptionFloatOrPercent(100., true));
    }
    object->name = pressure_calibration_object_name(test, id_item) + (ps.ellis_bends ? " frame" : "");
    oc.set_key_value("region_gcode", new ConfigOptionString(pressure_calibration_command(flavor, false) + std::to_string(test.first_pa) +
                                                            " ; first layer, borders and numbers\n"));
    // The combined infill layers / the first layer height for the bends.
    if (own_layer_height && ! bends.empty()) {
        const double model_height = bends.back()->mesh().bounding_box().size().z() * bends.back()->get_scaling_factor().z();
        ModelConfig  range_config;
        range_config.set_key_value("layer_height", new ConfigOptionFloat(combined_layer_height));
        object->layer_config_ranges[t_layer_height_range(first_layer_height, model_height + first_layer_height)] = range_config;
    }
    return ! check_all;
}

bool pressure_calibration_build(ModelObject &base, const DynamicPrintConfig &config, const PressureCalibrationSettings &settings,
                                const PressureCalibrationTest &test, size_t id_item, const std::string &resources_dir)
{
    return pressure_calibration_build_impl(base, config, settings, test, id_item, resources_dir, true);
}

Vec2d pressure_calibration_frame_size(const DynamicPrintConfig &config, const PressureCalibrationSettings &settings,
                                      const PressureCalibrationTest &test, size_t id_item)
{
    Model        model;
    ModelObject *object = model.add_object();
    object->add_instance();
    pressure_calibration_build_impl(*object, config, settings, test, id_item, std::string(), false);
    if (object->volumes.empty())
        return Vec2d::Zero();
    object->invalidate_bounding_box();
    return to_2d(object->instance_bounding_box(0).size());
}

// MaxRects bin of one bed: the free rectangles (maximal), split by every placed rectangle and keep-out zone.
namespace {
struct FreeRects
{
    std::vector<BoundingBoxf> free;

    explicit FreeRects(const BoundingBoxf &area) { free.push_back(area); }

    void occupy(const BoundingBoxf &r)
    {
        std::vector<BoundingBoxf> out;
        for (const BoundingBoxf &f : free) {
            if (r.min.x() >= f.max.x() - EPSILON || r.max.x() <= f.min.x() + EPSILON || r.min.y() >= f.max.y() - EPSILON || r.max.y() <= f.min.y() + EPSILON) {
                out.push_back(f);
                continue;
            }
            if (r.min.x() > f.min.x() + EPSILON) out.emplace_back(f.min, Vec2d(r.min.x(), f.max.y()));
            if (r.max.x() < f.max.x() - EPSILON) out.emplace_back(Vec2d(r.max.x(), f.min.y()), f.max);
            if (r.min.y() > f.min.y() + EPSILON) out.emplace_back(f.min, Vec2d(f.max.x(), r.min.y()));
            if (r.max.y() < f.max.y() - EPSILON) out.emplace_back(Vec2d(f.min.x(), r.max.y()), f.max);
        }
        // Only the maximal ones (of two equal ones the first).
        free.clear();
        for (size_t i = 0; i < out.size(); ++ i) {
            bool contained = false;
            for (size_t j = 0; j < out.size() && ! contained; ++ j) {
                if (i == j || ! out[j].contains(out[i].min) || ! out[j].contains(out[i].max))
                    continue;
                const bool equal = out[i].contains(out[j].min) && out[i].contains(out[j].max);
                contained = ! equal || j < i;
            }
            if (! contained)
                free.push_back(out[i]);
        }
    }

    // Best short side fit: the free rectangle leaving the least on its shorter side, then on its longer side. False if none fits.
    bool find(const Vec2d &size, Vec2d &position, double &score1, double &score2) const
    {
        bool found = false;
        for (const BoundingBoxf &f : free) {
            const Vec2d room = f.size() - size;
            if (room.x() < -EPSILON || room.y() < -EPSILON)
                continue;
            const double s1 = std::min(room.x(), room.y()), s2 = std::max(room.x(), room.y());
            if (! found || s1 < score1 - EPSILON || (s1 < score1 + EPSILON && s2 < score2 - EPSILON)) {
                found    = true;
                score1   = s1;
                score2   = s2;
                position = f.min;
            }
        }
        return found;
    }
};

struct CalibrationLayoutResult
{
    std::vector<CalibrationPlacement> placements;
    size_t                            beds { 0 };
    size_t                            first { 0 };
};

// One spacing: gap between the frames (without their brim).
CalibrationLayoutResult calibration_layout_gap(const std::vector<Vec2d> &sizes, const BoundingBoxf &bed, const std::vector<BoundingBoxf> &zones,
                                               double brim, double gap, bool turn, int max_beds)
{
    // The brim inside the bed with this much to spare, and clear of the zones grown by their clearance.
    constexpr double margin = 1.;
    CalibrationLayoutResult best;
    // Each frame takes its size plus half the gap on every side; the area shrinks and the zones grow by the rest.
    const double half = gap / 2.;
    const BoundingBoxf area(bed.min + Vec2d::Constant(brim + margin - half), bed.max - Vec2d::Constant(brim + margin - half));
    if (area.size().x() <= 0. || area.size().y() <= 0.)
        return best;
    std::vector<BoundingBoxf> obstacles;
    for (const BoundingBoxf &z : zones) {
        const double grow = BedKeepOut::CLEARANCE + brim + margin - half;
        obstacles.emplace_back(z.min - Vec2d::Constant(grow), z.max + Vec2d::Constant(grow));
    }
    std::mt19937 rng(12345);
    for (int attempt = 0; attempt < 400; ++ attempt) {
        // The largest first; from the second attempt on, shuffled a little.
        std::vector<std::pair<double, size_t>> order;
        for (size_t i = 0; i < sizes.size(); ++ i)
            order.emplace_back(std::max(sizes[i].x(), sizes[i].y()) * (attempt == 0 ? 1. : std::uniform_real_distribution<double>(0.75, 1.25)(rng)), i);
        std::sort(order.begin(), order.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        std::vector<FreeRects>            beds;
        std::vector<size_t>               count;
        std::vector<CalibrationPlacement> out(sizes.size());
        bool                              failed = false;
        for (const auto &entry : order) {
            const size_t i      = entry.second;
            bool         placed = false;
            for (size_t b = 0; ! placed; ++ b) {
                if (b == beds.size()) {
                    if (int(b) >= max_beds)
                        break;
                    beds.emplace_back(area);
                    count.push_back(0);
                    for (const BoundingBoxf &o : obstacles)
                        beds.back().occupy(o);
                }
                Vec2d  pos, best_pos;
                bool   best_turned = false, found = false;
                double s1 = 0., s2 = 0., best1 = 0., best2 = 0.;
                for (const bool turned : { false, true }) {
                    if (turned && ! turn)
                        continue;
                    const Vec2d size = (turned ? Vec2d(sizes[i].y(), sizes[i].x()) : sizes[i]) + Vec2d::Constant(gap);
                    if (beds[b].find(size, pos, s1, s2) && (! found || s1 < best1 - EPSILON || (s1 < best1 + EPSILON && s2 < best2 - EPSILON))) {
                        found = true; best_pos = pos; best_turned = turned; best1 = s1; best2 = s2;
                    }
                }
                if (! found) {
                    if (count[b] == 0)
                        break;   // fits on no empty bed
                    continue;
                }
                const Vec2d size = (best_turned ? Vec2d(sizes[i].y(), sizes[i].x()) : sizes[i]) + Vec2d::Constant(gap);
                beds[b].occupy(BoundingBoxf(best_pos, best_pos + size));
                out[i] = { int(b), best_pos + Vec2d::Constant(half), best_turned };
                ++ count[b];
                placed = true;
            }
            if (! placed) {
                failed = true;
                break;
            }
        }
        if (failed)
            continue;
        if (best.placements.empty() || beds.size() < best.beds || (beds.size() == best.beds && count.front() > best.first))
            best = { out, beds.size(), count.front() };
    }
    return best;
}
} // namespace

// The area the frames (with half the gap around each) must stay in and the zones they must stay out of (grown the same way).
static void calibration_layout_area(const BoundingBoxf &bed, const std::vector<BoundingBoxf> &zones, double brim, double gap,
                                    BoundingBoxf &area, std::vector<BoundingBoxf> &obstacles)
{
    constexpr double margin = 1.;
    const double half = gap / 2.;
    area = BoundingBoxf(bed.min + Vec2d::Constant(brim + margin - half), bed.max - Vec2d::Constant(brim + margin - half));
    obstacles.clear();
    for (const BoundingBoxf &z : zones) {
        const double grow = BedKeepOut::CLEARANCE + brim + margin - half;
        obstacles.emplace_back(z.min - Vec2d::Constant(grow), z.max + Vec2d::Constant(grow));
    }
}

static bool overlaps(const BoundingBoxf &a, const BoundingBoxf &b)
{
    return a.min.x() < b.max.x() - EPSILON && a.max.x() > b.min.x() + EPSILON && a.min.y() < b.max.y() - EPSILON && a.max.y() > b.min.y() + EPSILON;
}

// Tidy rows (user 2026-11-05: "on a single accel test they should still be organized nicely"): the frames in their order (the
// tests 1, 2, 3 of each acceleration), all upright or all turned, left to right, a new row when the bed is full or (per_group) at
// every new group (acceleration), a new bed when the rows are; past the zones.
static CalibrationLayoutResult calibration_layout_rows(const std::vector<Vec2d> &sizes, const std::vector<int> &groups, const BoundingBoxf &bed,
                                                       const std::vector<BoundingBoxf> &zones, double brim, double gap, bool turned,
                                                       bool per_group, bool above_zones, int max_beds)
{
    BoundingBoxf              area;
    std::vector<BoundingBoxf> obstacles;
    calibration_layout_area(bed, zones, brim, gap, area, obstacles);
    // above_zones: the rows start above the zones at the front edge (the first row not squeezed between them).
    double first_y = area.min.y();
    if (above_zones)
        for (const BoundingBoxf &o : obstacles)
            if (o.min.y() <= area.min.y() + EPSILON)
                first_y = std::max(first_y, o.max.y());
    CalibrationLayoutResult out;
    out.placements.resize(sizes.size());
    int    b = 0;
    double row_y = first_y, row_x = area.min.x(), row_h = 0.;
    size_t on_bed = 0;
    std::vector<size_t> count(1, 0);
    for (size_t i = 0; i < sizes.size(); ++ i) {
        const Vec2d size = (turned ? Vec2d(sizes[i].y(), sizes[i].x()) : sizes[i]) + Vec2d::Constant(gap);
        if (size.x() > area.size().x() + EPSILON || size.y() > area.size().y() + EPSILON)
            return {};
        if (per_group && i > 0 && ! groups.empty() && groups[i] != groups[i - 1] && row_h > 0.) {
            row_y += row_h; row_x = area.min.x(); row_h = 0.;
        }
        for (int guard = 0;; ++ guard) {
            if (guard > 1000)
                return {};
            double x = row_x;
            // Past the zones in the way.
            for (bool moved = true; moved;) {
                moved = false;
                for (const BoundingBoxf &o : obstacles)
                    if (overlaps(BoundingBoxf(Vec2d(x, row_y), Vec2d(x, row_y) + size), o)) {
                        x     = o.max.x();
                        moved = true;
                    }
            }
            if (x + size.x() <= area.max.x() + EPSILON && row_y + size.y() <= area.max.y() + EPSILON) {
                out.placements[i] = { b, Vec2d(x, row_y) + Vec2d::Constant(gap / 2.), turned };
                row_x = x + size.x();
                row_h = std::max(row_h, size.y());
                ++ count[b];
                ++ on_bed;
                break;
            }
            if (row_h > 0.) {
                // The next row.
                row_y += row_h; row_x = area.min.x(); row_h = 0.;
            } else if (row_x > area.min.x() + EPSILON || row_y + size.y() > area.max.y() + EPSILON) {
                // An empty row too short: the next bed.
                if (on_bed == 0 || b + 1 >= max_beds)
                    return {};
                ++ b; count.push_back(0); on_bed = 0;
                row_y = first_y; row_x = area.min.x(); row_h = 0.;
            } else {
                // A zone takes the whole row: above it.
                double above = row_y;
                for (const BoundingBoxf &o : obstacles)
                    if (o.min.y() < row_y + size.y() - EPSILON && o.max.y() > row_y + EPSILON)
                        above = std::max(above, o.max.y());
                if (above <= row_y + EPSILON) {
                    if (on_bed == 0 || b + 1 >= max_beds)
                        return {};
                    ++ b; count.push_back(0); on_bed = 0;
                    row_y = first_y; row_x = area.min.x(); row_h = 0.;
                } else
                    row_y = above;
            }
        }
    }
    out.beds  = count.size();
    out.first = count.front();
    return out;
}

// Each bed's frames moved together to the middle of the bed when they stay inside it and clear of the zones (else only along x, or
// only along y, or not at all).
static void calibration_layout_center(CalibrationLayoutResult &layout, const std::vector<Vec2d> &sizes, const BoundingBoxf &bed,
                                      const std::vector<BoundingBoxf> &zones, double brim, double gap)
{
    BoundingBoxf              area;
    std::vector<BoundingBoxf> obstacles;
    calibration_layout_area(bed, zones, brim, gap, area, obstacles);
    for (int b = 0; b < int(layout.beds); ++ b) {
        std::vector<BoundingBoxf> boxes;
        std::vector<size_t>       idx;
        BoundingBoxf              all;
        for (size_t i = 0; i < layout.placements.size(); ++ i)
            if (layout.placements[i].bed == b) {
                const CalibrationPlacement &p = layout.placements[i];
                const Vec2d size = (p.turned ? Vec2d(sizes[i].y(), sizes[i].x()) : sizes[i]) + Vec2d::Constant(gap);
                boxes.emplace_back(p.position - Vec2d::Constant(gap / 2.), p.position - Vec2d::Constant(gap / 2.) + size);
                idx.push_back(i);
                all.merge(boxes.back());
            }
        if (boxes.empty())
            continue;
        const Vec2d delta = area.center() - all.center();
        for (const Vec2d &d : { delta, Vec2d(delta.x(), 0.), Vec2d(0., delta.y()) }) {
            bool ok = true;
            for (const BoundingBoxf &box : boxes) {
                const BoundingBoxf moved(box.min + d, box.max + d);
                if (moved.min.x() < area.min.x() - EPSILON || moved.min.y() < area.min.y() - EPSILON ||
                    moved.max.x() > area.max.x() + EPSILON || moved.max.y() > area.max.y() + EPSILON ||
                    std::any_of(obstacles.begin(), obstacles.end(), [&moved](const BoundingBoxf &o) { return overlaps(moved, o); }))
                    ok = false;
            }
            if (ok) {
                for (size_t i : idx)
                    layout.placements[i].position += d;
                break;
            }
        }
    }
}

std::vector<CalibrationPlacement> calibration_layout(const std::vector<Vec2d> &sizes, const BoundingBoxf &bed, const std::vector<BoundingBoxf> &zones,
                                                     double brim, bool turn, int max_beds, const std::vector<int> &groups)
{
    if (sizes.empty())
        return {};
    // The fewest beds: MaxRects with shared brims, or with the frames touching (no brim between them).
    const CalibrationLayoutResult shared   = calibration_layout_gap(sizes, bed, zones, brim, brim, turn, max_beds);
    const CalibrationLayoutResult touching = shared.beds == 1 ? CalibrationLayoutResult() :
                                             calibration_layout_gap(sizes, bed, zones, brim, 0., turn, max_beds);
    size_t fewest = shared.placements.empty() ? std::numeric_limits<size_t>::max() : shared.beds;
    if (! touching.placements.empty())
        fewest = std::min(fewest, touching.beds);
    if (fewest == std::numeric_limits<size_t>::max())
        return {};
    // The tidiest layout with that many beds: rows per acceleration, then rows of all; upright, then turned; shared brims first.
    for (const double gap : { brim, 0. }) {
        for (const bool per_group : { true, false })
            for (const bool turned : { false, true })
                for (const bool above_zones : { false, true }) {
                    if ((turned && ! turn) || (above_zones && zones.empty()))
                        continue;
                    CalibrationLayoutResult rows = calibration_layout_rows(sizes, groups, bed, zones, brim, gap, turned, per_group, above_zones, max_beds);
                    if (! rows.placements.empty() && rows.beds <= fewest) {
                        calibration_layout_center(rows, sizes, bed, zones, brim, gap);
                        return rows.placements;
                    }
                }
        CalibrationLayoutResult packed = gap == brim ? shared : touching;
        if (! packed.placements.empty() && packed.beds == fewest) {
            calibration_layout_center(packed, sizes, bed, zones, brim, gap);
            return packed.placements;
        }
    }
    return {};
}

void pressure_calibration_print_config(DynamicPrintConfig &print)
{
    print.set_key_value("avoid_crossing_perimeters", new ConfigOptionBool(false));
    print.set_key_value("complete_objects",          new ConfigOptionBool(false));
    print.set_key_value("elefant_foot_compensation", new ConfigOptionFloat(0));
    print.set_key_value("xy_size_compensation",      new ConfigOptionFloat(0));
}

// Sorted, rounded to whole numbers, without duplicates or values of 0.
static std::vector<double> sorted_unique(std::vector<double> values)
{
    for (double &v : values)
        v = std::round(v);
    values.erase(std::remove_if(values.begin(), values.end(), [](double v) { return v <= 0.; }), values.end());
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

std::vector<double> adaptive_pa_default_speeds(const DynamicPrintConfig &config, const PressureCalibrationSettings &settings)
{
    const auto speed = [&config](const char *key) { return config.has(key) ? config.get_abs_value(key) : 0.; };
    // 0: autospeed, no fixed speed to test.
    const double high = adaptive_pa_effective_speed(config, settings, std::max({ speed("external_perimeter_speed"), speed("perimeter_speed"),
        speed("infill_speed"), speed("solid_infill_speed"), speed("top_solid_infill_speed") }));
    const double low  = std::min(speed("small_perimeter_speed"), high);
    return sorted_unique(low > 0. ? std::vector<double>{ low, (low + high) / 2., high } : std::vector<double>{ high });
}

double adaptive_pa_speed_mode_accel(const DynamicPrintConfig &config)
{
    const double normal = config.opt_float("klipper_max_accel");
    const auto  *modes  = config.option<ConfigOptionStrings>("klipper_speed_mode");
    const std::string mode = modes != nullptr && ! modes->values.empty() ? modes->get_at(0) : std::string();
    const char  *key    = boost::iequals(mode, "slow") ? "klipper_max_accel_slow" : boost::iequals(mode, "fast") ? "klipper_max_accel_fast" : nullptr;
    const auto  *opt    = key != nullptr ? config.option<ConfigOptionFloatOrPercent>(key) : nullptr;
    return opt != nullptr ? opt->get_abs_value(normal) : normal;
}

std::vector<double> adaptive_pa_default_accelerations(const DynamicPrintConfig &config)
{
    const double normal  = config.opt_float("klipper_max_accel");
    const auto   mode    = [&config, normal](const char *key) {
        const auto *opt = config.option<ConfigOptionFloatOrPercent>(key);
        return opt != nullptr ? opt->get_abs_value(normal) : normal;
    };
    const double printed = config.get_abs_value("default_acceleration");
    return sorted_unique({ 0.25 * mode("klipper_max_accel_slow"), printed > 0. ? printed : adaptive_pa_speed_mode_accel(config),
                           mode("klipper_max_accel_fast") });
}

double adaptive_pa_side_length(const DynamicPrintConfig &config, double speed, double acceleration, double min_side)
{
    if (acceleration <= 0.)
        return min_side;
    const double cruise   = config.has("klipper_minimum_cruise_ratio") ? config.opt_float("klipper_minimum_cruise_ratio") : 0.5;
    // The fraction of a side spent accelerating and decelerating: at most half, less with a higher minimum_cruise_ratio.
    const double fraction = std::clamp(1. - cruise, 0.01, 0.5);
    // Rounded up, but not for a floating point excess (1 - 0.8 is not exactly 0.2).
    return std::max(min_side, std::ceil(speed * speed / (acceleration * fraction) - 1e-6));
}

// The cross section of a wall of the chevrons (mm³/mm).
static double adaptive_pa_mm3_per_mm(const DynamicPrintConfig &config, const PressureCalibrationSettings &settings)
{
    const double nozzle = config.opt<ConfigOptionFloats>("nozzle_diameter")->get_at(0);
    return Flow(float(nozzle * settings.line_ratio / 100.), float(config.opt_float("layer_height")), float(nozzle)).mm3_per_mm();
}

double adaptive_pa_effective_speed(const DynamicPrintConfig &config, const PressureCalibrationSettings &settings, double speed)
{
    const double mm3_per_mm = adaptive_pa_mm3_per_mm(config, settings);
    for (double cap : { config.has("max_volumetric_speed") ? config.opt_float("max_volumetric_speed") : 0.,
                        config.has("filament_max_volumetric_speed") ? config.opt<ConfigOptionFloats>("filament_max_volumetric_speed")->get_at(0) : 0. })
        if (cap > 0. && mm3_per_mm > 0.)
            speed = std::min(speed, cap / mm3_per_mm);
    return speed;
}

double adaptive_pa_flow(const DynamicPrintConfig &config, const PressureCalibrationSettings &settings, double speed)
{
    return adaptive_pa_effective_speed(config, settings, speed) * adaptive_pa_mm3_per_mm(config, settings);
}

std::string adaptive_pa_model(std::vector<AdaptivePAPoint> points)
{
    std::sort(points.begin(), points.end(), [](const AdaptivePAPoint &a, const AdaptivePAPoint &b) {
        return a.acceleration < b.acceleration || (a.acceleration == b.acceleration && a.flow < b.flow); });
    std::string out;
    char        buf[128];
    for (const AdaptivePAPoint &p : points) {
        snprintf(buf, sizeof(buf), "%g,%.2f,%ld\n", std::round(p.pa * 10000.) / 10000., p.flow, std::lround(p.acceleration));
        out += buf;
    }
    return out;
}

std::string adaptive_pa_nozzle_header(double nozzle)
{
    return "; nozzle " + num(nozzle);
}

// The model as lines, and the index of the header of the nozzle's section (lines.size() if none) and of the line after it.
static std::vector<std::string> model_lines(const std::string &model, double nozzle, size_t &begin, size_t &end)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < model.size()) {
        size_t eol = model.find('\n', start);
        if (eol == std::string::npos)
            eol = model.size();
        std::string line = model.substr(start, eol - start);
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        lines.emplace_back(std::move(line));
        start = eol + 1;
    }
    const std::string header = adaptive_pa_nozzle_header(nozzle);
    begin = end = lines.size();
    for (size_t i = 0; i < lines.size(); ++ i)
        if (lines[i] == header) {
            begin = i;
            for (end = i + 1; end < lines.size() && lines[end].rfind("; nozzle ", 0) != 0; ++ end) ;
            break;
        }
    return lines;
}

std::string adaptive_pa_model_section(const std::string &model, double nozzle)
{
    size_t begin, end;
    const std::vector<std::string> lines = model_lines(model, nozzle, begin, end);
    std::string out;
    for (size_t i = begin + 1; i < end; ++ i)
        if (! lines[i].empty())
            out += lines[i] + "\n";
    return out;
}

std::string adaptive_pa_model_for_nozzle(const std::string &model, double nozzle)
{
    if (std::string section = adaptive_pa_model_section(model, nozzle); ! section.empty())
        return section;
    // The lines before the first section (e.g. pasted from OrcaSlicer).
    size_t begin, end;
    const std::vector<std::string> lines = model_lines(model, nozzle, begin, end);
    std::string out;
    for (const std::string &line : lines) {
        if (line.rfind("; nozzle ", 0) == 0)
            break;
        if (! line.empty())
            out += line + "\n";
    }
    return out;
}

std::string adaptive_pa_model_set_section(const std::string &model, double nozzle, const std::string &new_lines)
{
    size_t begin, end;
    std::vector<std::string> lines = model_lines(model, nozzle, begin, end);
    // Without the old section and the empty lines at the end.
    lines.erase(lines.begin() + begin, lines.begin() + end);
    while (! lines.empty() && lines.back().empty())
        lines.pop_back();
    std::string out;
    for (const std::string &line : lines)
        out += line + "\n";
    out += adaptive_pa_nozzle_header(nozzle) + "\n" + new_lines;
    return out;
}

std::string adaptive_pa_tests_serialize(const std::vector<AdaptivePATest> &tests)
{
    std::string out;
    for (const AdaptivePATest &t : tests)
        out += (out.empty() ? "" : ";") + num(t.speed) + "," + num(t.acceleration) + "," + num(t.flow) + "," + num(t.nozzle) + "," + num(t.pa) +
               "," + std::to_string(t.id);
    return out;
}

std::vector<AdaptivePATest> adaptive_pa_tests_deserialize(const std::string &text)
{
    std::vector<AdaptivePATest> out;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(';', start);
        if (end == std::string::npos)
            end = text.size();
        AdaptivePATest t;
        if (sscanf(text.substr(start, end - start).c_str(), "%lf,%lf,%lf,%lf,%lf,%d", &t.speed, &t.acceleration, &t.flow, &t.nozzle, &t.pa, &t.id) >= 4)
            out.emplace_back(t);
        start = end + 1;
    }
    return out;
}

std::vector<AdaptivePATest> adaptive_pa_tests_merge(const std::vector<AdaptivePATest> &kept, const std::vector<AdaptivePATest> &print)
{
    if (print.empty())
        return kept;
    std::vector<AdaptivePATest> out;
    for (const AdaptivePATest &t : kept)
        if (std::abs(t.nozzle - print.front().nozzle) > EPSILON || std::abs(t.acceleration - print.front().acceleration) >= 0.5)
            out.emplace_back(t);
    out.insert(out.end(), print.begin(), print.end());
    std::stable_sort(out.begin(), out.end(), [](const AdaptivePATest &a, const AdaptivePATest &b) {
        return a.nozzle < b.nozzle - EPSILON || (std::abs(a.nozzle - b.nozzle) <= EPSILON && a.acceleration < b.acceleration); });
    return out;
}

std::vector<AdaptivePATest> adaptive_pa_tests_from_model(const std::vector<AdaptivePATest> &kept, const std::string &model, double nozzle)
{
    std::vector<AdaptivePATest> out;
    const std::string section = adaptive_pa_model_section(model, nozzle);
    size_t start = 0;
    while (start < section.size()) {
        size_t end = section.find('\n', start);
        if (end == std::string::npos)
            end = section.size();
        const std::string line = section.substr(start, end - start);
        start = end + 1;
        AdaptivePATest t { 0., 0., 0., nozzle };
        if (line.empty() || line[0] == ';' || sscanf(line.c_str(), "%lf , %lf , %lf", &t.pa, &t.flow, &t.acceleration) != 3 || t.acceleration <= 0.)
            continue;
        if (adaptive_pa_calibration_tests(kept, nozzle, t.acceleration).empty())
            out.emplace_back(t);
    }
    std::stable_sort(out.begin(), out.end(), [](const AdaptivePATest &a, const AdaptivePATest &b) {
        return a.acceleration < b.acceleration || (a.acceleration == b.acceleration && a.flow < b.flow); });
    return out;
}

std::vector<AdaptivePAPrintAcceleration> adaptive_pa_print_accelerations(const DynamicPrintConfig &config)
{
    std::vector<AdaptivePAPrintAcceleration> out;
    const auto add = [&out](const std::string &feature, double acceleration) {
        acceleration = std::round(acceleration);
        auto it = std::find_if(out.begin(), out.end(), [acceleration](const AdaptivePAPrintAcceleration &a) { return a.acceleration == acceleration; });
        if (it == out.end())
            out.push_back({ acceleration, { feature } });
        else
            it->features.emplace_back(feature);
    };
    const double normal = config.has("klipper_max_accel") ? config.opt_float("klipper_max_accel") : 0.;
    const double def    = config.has("default_acceleration") ? config.get_abs_value("default_acceleration", normal) : 0.;
    if (def <= 0.) {
        add("all features (no acceleration commands: the Klipper max accel of the filament's speed mode)", adaptive_pa_speed_mode_accel(config));
        return out;
    }
    // The first setting above 0 of the chain (percentages of the default acceleration), else the default acceleration, as
    // GCodeGenerator::_extrude() and feature_acceleration().
    const auto chain = [&config, def](std::initializer_list<const char*> keys) {
        for (const char *key : keys)
            if (config.has(key))
                if (const double a = config.get_abs_value(key, def); a > 0.)
                    return a;
        return def;
    };
    add("first layer",       chain({ "first_layer_acceleration" }));
    add("outer wall",        chain({ "external_perimeter_acceleration", "perimeter_acceleration" }));
    add("walls",             chain({ "perimeter_acceleration" }));
    add("top surface",       chain({ "top_solid_infill_acceleration", "solid_infill_acceleration", "infill_acceleration" }));
    add("solid infill",      chain({ "solid_infill_acceleration", "infill_acceleration" }));
    add("infill",            chain({ "infill_acceleration" }));
    add("bridges",           chain({ "bridge_acceleration", "solid_infill_acceleration", "infill_acceleration" }));
    add("internal bridges",  chain({ "internal_bridge_acceleration", "bridge_acceleration", "solid_infill_acceleration", "infill_acceleration" }));
    add("gap fill",          chain({ "gap_fill_acceleration" }));
    add("support",           chain({ "support_material_acceleration" }));
    add("support interface", chain({ "support_material_interface_acceleration", "support_material_acceleration" }));
    std::sort(out.begin(), out.end(), [](const AdaptivePAPrintAcceleration &a, const AdaptivePAPrintAcceleration &b) { return a.acceleration < b.acceleration; });
    return out;
}

std::vector<size_t> adaptive_pa_calibration_tests(const std::vector<AdaptivePATest> &kept, double nozzle, double acceleration)
{
    std::vector<size_t> out;
    for (size_t i = 0; i < kept.size(); ++ i)
        if (std::abs(kept[i].nozzle - nozzle) < EPSILON && std::abs(kept[i].acceleration - acceleration) < 0.5)
            out.emplace_back(i);
    return out;
}

std::vector<double> adaptive_pa_calibration_accelerations(const std::vector<AdaptivePATest> &kept, double nozzle)
{
    std::vector<double> out;
    for (const AdaptivePATest &t : kept)
        if (std::abs(t.nozzle - nozzle) < EPSILON &&
            std::none_of(out.begin(), out.end(), [&t](double a) { return std::abs(a - t.acceleration) < 0.5; }))
            out.emplace_back(t.acceleration);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<AdaptivePAPoint> adaptive_pa_results(std::vector<AdaptivePATest> &kept, double nozzle, const std::vector<AdaptivePAResultRow> &rows,
                                                 std::string &error)
{
    error.clear();
    std::vector<AdaptivePAPoint> points;
    std::vector<double>          new_pa(kept.size(), -1.);
    std::vector<double>          seen;
    for (const AdaptivePAResultRow &row : rows) {
        const std::string accel = num(row.acceleration);
        if (std::any_of(seen.begin(), seen.end(), [&row](double a) { return std::abs(a - row.acceleration) < 0.5; })) {
            error = format(_u8L("%1% mm/s²: two rows with this acceleration."), accel);
            return {};
        }
        seen.emplace_back(row.acceleration);
        const std::vector<size_t> tests = adaptive_pa_calibration_tests(kept, nozzle, row.acceleration);
        size_t values = 0;
        for (size_t k = 0; k < row.pa.size(); ++ k) {
            if (row.pa[k] < 0.)
                continue;
            if (tests.empty()) {
                error = format(_u8L("%1% mm/s²: no calibration printed at this acceleration with the %2% mm nozzle (Calibration > Adaptive "
                                    "pressure advance calibration), the flows of its tests are unknown."), accel, num(nozzle));
                return {};
            }
            if (k >= tests.size()) {
                error = format(_u8L("%1% mm/s²: test %2% doesn't exist, that print had %3% tests."), accel, k + 1, tests.size());
                return {};
            }
            if (row.pa[k] >= 2.) {
                error = format(_u8L("%1% mm/s², test %2%: the pressure advance must be from 0 to 2."), accel, k + 1);
                return {};
            }
            const AdaptivePATest &t = kept[tests[k]];
            points.push_back({ row.pa[k], t.flow, t.acceleration });
            new_pa[tests[k]] = row.pa[k];
            ++ values;
        }
        if (values == 1) {
            error = format(_u8L("%1% mm/s²: values for at least 2 tests (or none), the model interpolates between the speeds of each "
                                "acceleration."), accel);
            return {};
        }
    }
    if (points.empty()) {
        error = _u8L("No value: type the best value of at least 2 tests.");
        return {};
    }
    for (size_t i = 0; i < kept.size(); ++ i)
        if (std::abs(kept[i].nozzle - nozzle) < EPSILON)
            kept[i].pa = new_pa[i];
    return points;
}

std::vector<std::string> adaptive_pa_results_warnings(const std::vector<AdaptivePATest> &kept, double nozzle)
{
    std::vector<std::string> out;
    const std::vector<double> accels = adaptive_pa_calibration_accelerations(kept, nozzle);
    // The number of a test on its frame.
    const auto number = [&kept, nozzle](size_t i) {
        const std::vector<size_t> tests = adaptive_pa_calibration_tests(kept, nozzle, kept[i].acceleration);
        return size_t(std::find(tests.begin(), tests.end(), i) - tests.begin()) + 1;
    };
    const auto name = [&kept, &number](size_t i) {
        return format(_u8L("%1% mm/s² test %2% (%3%)"), num(kept[i].acceleration), number(i), num(kept[i].pa));
    };
    for (double accel : accels) {
        // In the order of the flows, the ones with a value.
        std::vector<size_t> tests;
        for (size_t i : adaptive_pa_calibration_tests(kept, nozzle, accel))
            if (kept[i].pa >= 0.)
                tests.emplace_back(i);
        std::stable_sort(tests.begin(), tests.end(), [&kept](size_t a, size_t b) { return kept[a].flow < kept[b].flow; });
        for (size_t j = 1; j < tests.size(); ++ j)
            if (kept[tests[j]].pa > kept[tests[j - 1]].pa + EPSILON && kept[tests[j]].flow > kept[tests[j - 1]].flow + EPSILON)
                out.emplace_back(format(_u8L("%1% is higher than %2%, which prints slower."), name(tests[j]), name(tests[j - 1])));
    }
    // The same speed at a higher acceleration.
    for (size_t a = 0; a < accels.size(); ++ a)
        for (size_t b = a + 1; b < accels.size(); ++ b)
            for (size_t i : adaptive_pa_calibration_tests(kept, nozzle, accels[a]))
                for (size_t k : adaptive_pa_calibration_tests(kept, nozzle, accels[b]))
                    if (kept[i].pa >= 0. && kept[k].pa >= 0. && kept[i].speed > 0. && std::abs(kept[i].speed - kept[k].speed) < 0.5 &&
                        kept[k].pa > kept[i].pa + EPSILON)
                        out.emplace_back(format(_u8L("%1% is higher than %2%, which has a lower acceleration."), name(k), name(i)));
    return out;
}

} // namespace Slic3r
