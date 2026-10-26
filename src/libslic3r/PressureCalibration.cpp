#include "PressureCalibration.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>

#include <boost/filesystem.hpp>

#include "ClipperUtils.hpp"
#include "Exception.hpp"
#include "FileReader.hpp"
#include "Flow.hpp"
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
    set_float("default_acceleration", s.acceleration);
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
    line("### Pressure calibration (SuperSlicer style, Calibration menu) ###");
    line("Print settings (Ellis' defaults unless changed in the dialog):");
    if (s.ellis_bends) {
        line(" - Print speed: " + num(s.speed) + " mm/s");
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
    line(" - Acceleration: " + (s.acceleration > 0. ? num(s.acceleration) + " mm/s^2" : std::string("Disabled")));
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
                                      const PressureCalibrationSettings &ps, const PressureCalibrationTest &test, size_t id_item)
{
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
            wall->config.set_key_value("region_gcode", new ConfigOptionString(
                pressure_calibration_command(flavor, test.smooth_time) + std::to_string(test.values[j]) + " ; " + test.role +
                " wall " + std::to_string(k + 1) + "\n"));
        }
        // One open line of the width per wall and layer, at the speed whatever the small perimeter settings.
        ModelConfig &oc = object->config;
        oc.set_key_value("perimeter_generator",                new ConfigOptionEnum<PerimeterGeneratorType>(PerimeterGeneratorType::Arachne));
        oc.set_key_value("perimeters",                         new ConfigOptionInt(1));
        oc.set_key_value("perimeter_extrusion_width",          new ConfigOptionFloatOrPercent(chevrons.width, false));
        oc.set_key_value("external_perimeter_extrusion_width", new ConfigOptionFloatOrPercent(chevrons.width, false));
        oc.set_key_value("perimeter_speed",                    new ConfigOptionFloat(ps.speed));
        oc.set_key_value("external_perimeter_speed",           new ConfigOptionFloatOrPercent(ps.speed, false));
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

bool pressure_calibration_build(ModelObject &base, const DynamicPrintConfig &config, const PressureCalibrationSettings &ps_in,
                                const PressureCalibrationTest &test, size_t id_item, const std::string &resources_dir)
{
    // CheckAll: SuperSlicer's bends, one per extrusion role.
    PressureCalibrationSettings ps = ps_in;
    ps.ellis_bends = pressure_calibration_wall_objects(ps_in, test) > 0;
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
        ModelVolume       *bend     = calibration_add_part(object, bend_file, Vec3d(x_offset_90_bend, bend_y_pos, z_90_bend_pos), Vec3d(scale, scale, z_scale_90_bend));
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
            const double numbers_total_width  = (number_last.x() + xy_scaled_number_x / 2.) - (number_first.x() - xy_scaled_number_x / 2.);
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
                calibration_add_part(object, border, Vec3d((left_outer + left_inner) / 2., center_y, z_others_pos),
                                     Vec3d(first_layer_border / initial_border_x, (total_height + first_layer_border) / initial_90_bend_y, z_scale_others));
                calibration_add_part(object, border, Vec3d((box_right + right_outer) / 2., center_y, z_others_pos),
                                     Vec3d((right_outer - box_right) / initial_border_x, (total_height + first_layer_border) / initial_90_bend_y, z_scale_others));
                calibration_add_part(object, border, Vec3d(tb_border_x_pos, bottom_inner - first_layer_border / 2., z_others_pos),
                                     Vec3d(tb_total_width_mm / initial_border_x, first_layer_border / initial_border_y, z_scale_others));
                calibration_add_part(object, border, Vec3d(tb_border_x_pos, top_inner + first_layer_border / 2., z_others_pos),
                                     Vec3d(tb_total_width_mm / initial_border_x, first_layer_border / initial_border_y, z_scale_others));
            }
            if (id_item < 10)
                calibration_add_part(object, resource(std::to_string(id_item) + ".3mf"),
                                     Vec3d(number_mid.x(), bend_first.y() - xy_scaled_90_bend_y / 2. + xy_scaled_number_y / 2., z_scaled_model_height),
                                     Vec3d(xyz_scale * scale_role, xyz_scale * scale_role, z_scale_others * 2.));
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
                calibration_add_part(object, resource("point.3mf"), pos,
                                     Vec3d(0.4 / initial_point_xy * xyz_scale * scale_role, (xyz_scale + xyz_scale / 2.) * scale_role, z_scale_others * 2.));
                number_positions.emplace_back(pos);
                xpos -= xy_scaled_number_x / 2.;
            } else if (std::isdigit(static_cast<unsigned char>(value[j]))) {
                const Vec3d pos(xpos, ypos, z_scaled_model_height);
                calibration_add_part(object, resource(std::string(1, value[j]) + ".3mf"), pos,
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
    oc.set_key_value("brim_width",                     new ConfigOptionFloat(0));
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

void pressure_calibration_print_config(DynamicPrintConfig &print)
{
    print.set_key_value("avoid_crossing_perimeters", new ConfigOptionBool(false));
    print.set_key_value("complete_objects",          new ConfigOptionBool(false));
    print.set_key_value("elefant_foot_compensation", new ConfigOptionFloat(0));
    print.set_key_value("xy_size_compensation",      new ConfigOptionFloat(0));
}

} // namespace Slic3r
