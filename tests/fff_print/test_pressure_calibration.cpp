#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/FileReader.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/PressureCalibration.hpp"
#include "libslic3r/GCode/PressureAdvancePattern.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// SuperSlicer's pressure calibration (Calibration menu), built by pressure_calibration_build() as the dialog does.

namespace {

const std::string& resources()
{
    static const std::string dir = (boost::filesystem::path(TEST_DATA_DIR) / ".." / ".." / "resources").string();
    return dir;
}

// As the dialog: the settings into the config, per test one base plate as the plater loads it (centered around its origin)
// for the frame and numbers and one per wall of Ellis' chevrons, all with the same instance transformation, the summary as a
// custom G-code on the first layer; sliced. objects: the model, if wanted.
ModelObject* load_base(Model &model)
{
    Model base = FileReader::load_model(pressure_calibration_base_file(resources()));
    REQUIRE(base.objects.size() == 1);
    ModelObject *object = model.add_object(*base.objects.front());
    object->center_around_origin();
    if (object->instances.empty())
        object->add_instance();
    return object;
}

std::string slice_calibration(const DynamicPrintConfig &config_in, const PressureCalibrationSettings &settings,
                              const std::vector<PressureCalibrationTest> &tests, size_t first_id = 0)
{
    DynamicPrintConfig config = config_in;
    pressure_calibration_apply_settings(config, settings);
    pressure_calibration_print_config(config);
    Model model;
    for (size_t id_item = 0; id_item < tests.size(); ++ id_item) {
        REQUIRE(pressure_calibration_build(*load_base(model), config, settings, tests[id_item], first_id + id_item, resources()));
        std::vector<ModelObject*> walls;
        for (size_t k = 0; k < pressure_calibration_wall_objects(settings, tests[id_item]); ++ k)
            walls.emplace_back(load_base(model));
        pressure_calibration_build_walls(walls, config, settings, tests[id_item], first_id + id_item);
    }
    model.custom_gcode_per_print_z().mode = CustomGCode::SingleExtruder;
    model.custom_gcode_per_print_z().gcodes.push_back({ settings.first_layer_height, CustomGCode::Custom, -1, "",
                                                        pressure_calibration_summary(config, settings, tests) });
    // One test: in the middle of the bed (the plater arranges them).
    REQUIRE(tests.size() == 1);
    const BoundingBoxf bed(config.opt<ConfigOptionPoints>("bed_shape")->values);
    model.center_instances_around_point(bed.center());
    for (ModelObject *object : model.objects)
        object->ensure_on_bed();
    Print print;
    print.apply(model, config);
    print.validate();
    return gcode(print);
}

// For every extrusion: its layer, feature type, the last pressure advance value set before it, its path (extrusions without a
// travel or retraction between them), speed (mm/s), width (;WIDTH), fan (%) and acceleration commands seen so far.
struct PAExtrusion { int layer; std::string type; std::string value; size_t path; double speed; double width; int fan; };
std::vector<PAExtrusion> walk(const std::string &gcode, std::map<size_t, double> *path_lengths = nullptr, int *accel_commands = nullptr)
{
    std::vector<PAExtrusion> out;
    size_t      path    = 0;
    bool        in_path = false;
    int         layer   = -1;
    int         fan     = 0;
    double      width   = 0.;
    std::string type, value;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        const std::string &raw = line.raw();
        if (raw.rfind(";LAYER_CHANGE", 0) == 0)
            ++ layer;
        else if (raw.rfind(";TYPE:", 0) == 0)
            type = raw.substr(6);
        else if (raw.rfind(";WIDTH:", 0) == 0)
            width = std::stod(raw.substr(7));
        else if (raw.rfind("SET_PRESSURE_ADVANCE ADVANCE=", 0) == 0)
            value = raw.substr(29, raw.find(' ', 29) - 29);
        else if (line.cmd_is("M106")) {
            float s = 0;
            line.has_value('S', s);
            fan = int(std::round(s * 100.f / 255.f));
        } else if (line.cmd_is("M107"))
            fan = 0;
        else if ((line.cmd_is("M204") || raw.rfind("SET_VELOCITY_LIMIT", 0) == 0) && accel_commands)
            ++ *accel_commands;
        else if ((line.cmd_is("G1") || line.cmd_is("G2") || line.cmd_is("G3")) && line.dist_E(self) > 0 && line.dist_XY(self) > 0) {
            if (! in_path)
                ++ path;
            in_path = true;
            out.push_back({ layer, type, value, path, line.new_F(self) / 60., width, fan });
            if (path_lengths)
                (*path_lengths)[path] += line.dist_XY(self);
        } else if (line.cmd_is("G1") && (line.dist_XY(self) > 0 || line.dist_E(self) < 0))
            in_path = false;
    });
    return out;
}

DynamicPrintConfig test_config()
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({ { "gcode_flavor", "klipper" }, { "skirts", "0" }, { "default_acceleration", "3000" },
                                    { "perimeter_acceleration", "2000" }, { "disable_fan_first_layers", "3" },
                                    { "slowdown_below_layer_time", "60" },
                                    // The user's Arachne settings (Arachne split the corner of the walls with them).
                                    { "min_bead_width", "40%" }, { "wall_transition_filter_deviation", "5%" }, { "min_feature_size", "25%" } });
    return config;
}

// The width of a border of the frame, with the settings applied: 4 lines of the first layer flow (the first layer width, else
// the perimeter width, else the default width), as pressure_calibration_build() for Ellis' frame (no overlap).
double frame_border(const DynamicPrintConfig &config)
{
    const double first_layer = config.get_abs_value("first_layer_height", config.opt_float("layer_height"));
    const double nozzle      = config.opt<ConfigOptionFloats>("nozzle_diameter")->get_at(0);
    const ConfigOptionFloatOrPercent *width = nullptr;
    for (const char *key : { "first_layer_extrusion_width", "perimeter_extrusion_width", "extrusion_width" })
        if (width == nullptr && (config.option<ConfigOptionFloatOrPercent>(key)->value > 0. || std::string(key) == "extrusion_width"))
            width = config.option<ConfigOptionFloatOrPercent>(key);
    const Flow flow = Flow::new_from_config_width(frPerimeter, *width, float(nozzle), float(first_layer));
    return flow.width() + 3 * flow.spacing();
}

} // namespace

TEST_CASE("Pressure calibration with Ellis' settings", "[PressureCalibration]") {
    PressureCalibrationTest test;
    test.values = pressure_calibration_values(0., 0.01, 0.005);
    REQUIRE(test.values == std::vector<double>{ 0., 0.005, 0.01 });
    const PressureCalibrationSettings settings; // Ellis' defaults

    for (const char *generator : { "classic", "arachne" }) {
        DYNAMIC_SECTION(generator) {
            DynamicPrintConfig config = test_config();
            config.set_deserialize_strict({ { "perimeter_generator", generator } });
            std::map<size_t, double> path_lengths;
            int                      accel_commands = 0;
            const std::string        gcode          = slice_calibration(config, settings, { test });
            const std::vector<PAExtrusion> extrusions = walk(gcode, &path_lengths, &accel_commands);

            // The summary of the settings at the start of the first layer, before the first extrusion.
            const size_t summary = gcode.find("; ### Pressure calibration (SuperSlicer style");
            REQUIRE(summary != std::string::npos);
            CHECK(summary < gcode.find(";TYPE:", gcode.find(";LAYER_CHANGE")));
            CHECK(gcode.find(";  - Print speed: 100 mm/s") != std::string::npos);
            CHECK(gcode.find(";  - PA values (3): 0, 0.005, 0.01") != std::string::npos);

            std::map<std::string, std::set<int>> layers_of;
            std::map<std::string, size_t>        bend_types;
            std::set<long>                       bend_speeds;
            std::map<long, size_t>               bend_widths;
            std::set<int>                        bend_fans, first_layer_fans;
            double                               shortest_bend_path = 1e10;
            for (const PAExtrusion &e : extrusions) {
                // The brim prints before the frame's per region G-code.
                if (e.type == "Skirt/Brim")
                    continue;
                layers_of[e.value].insert(e.layer);
                if (e.layer == 0)
                    first_layer_fans.insert(e.fan);
                // From the third layer only the bends print (the numbers are two first layers high).
                if (e.layer >= 2) {
                    ++ bend_types[e.type];
                    bend_speeds.insert(std::lround(e.speed));
                    ++ bend_widths[std::lround(e.width * 1000.)];
                    bend_fans.insert(e.fan);
                    shortest_bend_path = std::min(shortest_bend_path, path_lengths[e.path]);
                }
            }
            // Each bend with its value on its 4 layers (Ellis' layer count); base, borders and numbers with the first layer value.
            for (const char *value : { "0.000000", "0.005000", "0.010000" }) {
                INFO(value);
                CHECK(layers_of[value] == std::set<int>{ 0, 1, 2, 3 });
            }
            CHECK(layers_of.count("") == 0);
            std::string types;
            for (const auto &[type, n] : bend_types)
                types += type + " " + std::to_string(n) + "; ";
            INFO(types);
            // Ellis' walls: open lines (external perimeters) from end to end (2 x 30 mm, both arms longer from the second layer:
            // over the left border and as far into the tab), 3 per chevron, 3 chevrons, on every layer, at Ellis' speed and width (100 mm/s, 112.5 % of
            // 0.4 mm) whatever the print settings (generator, small perimeter or layer time slowdown); wider only on the few
            // short moves at the corner.
            DynamicPrintConfig applied = config;
            pressure_calibration_apply_settings(applied, settings);
            const double line_length = 60. + 2. * (frame_border(applied) - applied.opt<ConfigOptionFloats>("nozzle_diameter")->get_at(0) * settings.line_ratio / 200.) * std::sqrt(2.);
            CHECK(bend_types.size() == 1);
            CHECK(bend_types.count("External perimeter") == 1);
            // The second layer too: the ends of the arms lie on the frame (another object), not printed as overhangs.
            CHECK(std::none_of(extrusions.begin(), extrusions.end(), [](const PAExtrusion &e) { return e.type == "Overhang perimeter"; }));
            // The frame without gap fill (its borders whole lines wide).
            CHECK(std::none_of(extrusions.begin(), extrusions.end(), [](const PAExtrusion &e) { return e.layer == 0 && e.type == "Gap fill"; }));
            std::map<int, std::vector<double>> paths_per_layer;
            {
                std::set<size_t> seen;
                for (const PAExtrusion &e : extrusions)
                    if (e.layer >= 2 && seen.insert(e.path).second)
                        paths_per_layer[e.layer].emplace_back(path_lengths[e.path]);
            }
            for (const auto &[layer, lengths] : paths_per_layer) {
                INFO("layer " << layer);
                CHECK(lengths.size() == 9);
                for (double l : lengths)
                    CHECK(std::abs(l - line_length) < 0.5);
            }
            CHECK(bend_speeds == std::set<long>{ 100 });
            size_t moves = 0;
            for (const auto &[w, n] : bend_widths) {
                INFO("width " << w);
                CHECK((w >= 445 && w <= 520));
                moves += n;
            }
            CHECK(bend_widths[450] >= moves / 3);
            // Fan: off on the first layer, 100 % from the second one (not the filament's 3 layers off); no acceleration command.
            CHECK(first_layer_fans == std::set<int>{ 0 });
            CHECK(bend_fans == std::set<int>{ 100 });
            CHECK(accel_commands == 0);
        }
    }
}

// The user's print profile (0.4 mm widths, 0.25 mm first layer, 4 perimeters) printed a line of gap fill in every border of the
// frame (2026-10-26): its borders were a little wider than the 4 lines.
TEST_CASE("Pressure calibration frame without gap fill", "[PressureCalibration]") {
    PressureCalibrationTest test;
    test.values = pressure_calibration_values(0., 0.01, 0.005);
    const PressureCalibrationSettings settings;
    for (const char *first_layer_height : { "0.2", "0.25", "0.3" })
        for (const char *perimeters : { "2", "4" }) {
            DYNAMIC_SECTION("first layer " << first_layer_height << " perimeters " << perimeters) {
                DynamicPrintConfig config = test_config();
                config.set_deserialize_strict({ { "first_layer_height", first_layer_height }, { "perimeters", perimeters },
                                                { "first_layer_extrusion_width", "0.4" }, { "perimeter_extrusion_width", "0.4" },
                                                { "external_perimeter_extrusion_width", "0.4" }, { "extrusion_width", "0.4" } });
                const std::vector<PAExtrusion> extrusions = walk(slice_calibration(config, settings, { test }));
                CHECK(std::none_of(extrusions.begin(), extrusions.end(), [](const PAExtrusion &e) { return e.layer == 0 && e.type == "Gap fill"; }));
            }
        }
}

// A 5 mm brim on the outside of the frame (user 2026-11-01), whatever the brim settings of the print profile; none inside the
// frame, none around Ellis' walls.
TEST_CASE("Pressure calibration frame with an outer brim", "[PressureCalibration]") {
    PressureCalibrationTest test;
    test.values = pressure_calibration_values(0., 0.01, 0.005);
    const PressureCalibrationSettings settings;
    for (const char *brim_type : { "no_brim", "outer_and_inner" }) {
        DYNAMIC_SECTION("print profile " << brim_type) {
            DynamicPrintConfig config = test_config();
            config.set_deserialize_strict({ { "brim_type", brim_type }, { "brim_width", "2" }, { "brim_separation", "1" },
                                            { "brim_ears", "1" } });
            const std::string gcode = slice_calibration(config, settings, { test });
            // The first layer: the bounding boxes of the brim and of everything else.
            BoundingBoxf brim, part;
            std::string  type;
            int          layer = -1;
            GCodeReader  parser;
            parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
                if (line.raw().rfind(";LAYER_CHANGE", 0) == 0)
                    ++ layer;
                else if (line.raw().rfind(";TYPE:", 0) == 0)
                    type = line.raw().substr(6);
                else if (line.cmd_is("G1") && line.dist_E(self) > 0 && line.dist_XY(self) > 0) {
                    if (type == "Skirt/Brim")
                        CHECK(layer == 0);
                    if (layer != 0)
                        return;
                    BoundingBoxf &box = type == "Skirt/Brim" ? brim : part;
                    box.merge(Vec2d(self.x(), self.y()));
                    box.merge(Vec2d(line.new_X(self), line.new_Y(self)));
                }
            });
            REQUIRE(brim.defined);
            REQUIRE(part.defined);
            // Lines centred half a line from the outer edges: 5 mm out on every side, give or take a line width.
            const Vec2d out_min = part.min - brim.min, out_max = brim.max - part.max;
            INFO("brim beyond the frame " << out_min.transpose() << " / " << out_max.transpose());
            for (double d : { out_min.x(), out_min.y(), out_max.x(), out_max.y() })
                CHECK((d > PRESSURE_CALIBRATION_BRIM_WIDTH - 0.6 && d < PRESSURE_CALIBRATION_BRIM_WIDTH + 0.1));
        }
    }
}

TEST_CASE("Pressure calibration with the extrusion role's settings (SuperSlicer)", "[PressureCalibration]") {
    PressureCalibrationTest test;
    test.first_pa = 0.04;
    test.values   = pressure_calibration_values(0., 0.01, 0.005);
    PressureCalibrationSettings settings;
    settings.ellis_bends = false;
    DynamicPrintConfig config = test_config();
    config.set_deserialize_strict({ { "external_perimeter_speed", "40" } });
    const std::vector<PAExtrusion> extrusions = walk(slice_calibration(config, settings, { test }));
    std::map<std::string, std::set<int>> layers_of;
    std::set<long>                       bend_speeds;
    for (const PAExtrusion &e : extrusions) {
        layers_of[e.value].insert(e.layer);
        if (e.layer >= 2)
            bend_speeds.insert(std::lround(e.speed));
    }
    // SuperSlicer's 5 layers, the speed of the role (external perimeter), the first layer value for base, borders and numbers.
    for (const char *value : { "0.000000", "0.005000", "0.010000" }) {
        INFO(value);
        CHECK(layers_of[value] == std::set<int>{ 0, 1, 2, 3, 4 });
    }
    CHECK(layers_of["0.040000"] == std::set<int>{ 0, 1 });
    CHECK(bend_speeds == std::set<long>{ 40 });
}

// The frame follows the number of chevrons: one solid part on the first layer around them (a ring), the tab of the numbers on
// the right with 2 mm between the numbers and its edges. On the first layer the ends of the lines reach half a line width into
// the frame; above it the left arms run over the whole left border to its outer edge, the right ones as far into the tab (over
// its perimeters, short of the numbers). With the walls the frame is one piece (closed). SuperSlicer's frame was open with an even number of bends.
TEST_CASE("Pressure calibration frame for any number of bends", "[PressureCalibration]") {
    DynamicPrintConfig config = test_config();
    const PressureCalibrationSettings settings;
    pressure_calibration_apply_settings(config, settings);
    const auto text = [](const BoundingBoxf3 &bb) {
        char buf[128];
        snprintf(buf, sizeof(buf), "x %.2f..%.2f y %.2f..%.2f", bb.min.x(), bb.max.x(), bb.min.y(), bb.max.y());
        return std::string(buf);
    };
    const auto bbox = [](const ModelVolume *v) { return v->mesh().transformed_bounding_box(v->get_matrix()); };
    const auto slice = [](const ModelVolume *v, double z) {
        MeshSlicingParamsEx params;
        params.trafo = v->get_matrix();
        return slice_mesh_ex(v->mesh().its, { float(z) }, params).front();
    };
    const double margin = 2., eps = 0.01;
    for (int count : { 2, 3, 4, 5, 7, 16, 17, 30 }) {
        DYNAMIC_SECTION(count << " bends") {
            PressureCalibrationTest test;
            test.values = pressure_calibration_values(0., 0.005 * (count - 1), 0.005);
            REQUIRE(int(test.values.size()) == count);
            Model        model;
            ModelObject *object = load_base(model);
            REQUIRE(pressure_calibration_build(*object, config, settings, test, 0, resources()));
            std::vector<ModelObject*> walls;
            for (size_t k = 0; k < pressure_calibration_wall_objects(settings, test); ++ k)
                walls.emplace_back(load_base(model));
            pressure_calibration_build_walls(walls, config, settings, test, 0);
            const ModelVolume         *frame = nullptr;
            std::vector<BoundingBoxf3> bends, numbers;
            // The first volume is the base plate (a 0.3 mm placeholder under the first layer).
            for (size_t i = 1; i < object->volumes.size(); ++ i) {
                if (object->volumes[i]->name == "frame")
                    frame = object->volumes[i];
                else
                    // The digits and points of the values and the test number.
                    numbers.emplace_back(bbox(object->volumes[i]));
            }
            REQUIRE(frame != nullptr);
            const BoundingBoxf3 fb = bbox(frame);
            const double        z  = fb.center().z();
            // The walls of each chevron (the base plate is the first volume of the objects of the walls too), on the first and the
            // second layer.
            const double z2 = fb.max.z() + 0.1;
            Polygons     walls_first_layer, walls_second_layer;
            for (int j = 0; j < count; ++ j) {
                BoundingBoxf3 bb;
                for (const ModelObject *wall : walls) {
                    bb.merge(bbox(wall->volumes[1 + j]));
                    append(walls_first_layer, to_polygons(slice(wall->volumes[1 + j], z)));
                    append(walls_second_layer, to_polygons(slice(wall->volumes[1 + j], z2)));
                }
                bends.emplace_back(bb);
            }
            REQUIRE(walls.size() == 3);
            REQUIRE(int(bends.size()) == count);
            INFO("frame " << text(fb));
            // Solid: a ring, its hole the inside of the frame.
            const ExPolygons frame_first_layer = slice(frame, z);
            REQUIRE(frame_first_layer.size() == 1);
            REQUIRE(frame_first_layer.front().holes.size() == 1);
            const BoundingBox inner = frame_first_layer.front().holes.front().bounding_box();
            const BoundingBox first = get_extents(walls_first_layer), second = get_extents(walls_second_layer);
            const double      line  = config.opt<ConfigOptionFloats>("nozzle_diameter")->get_at(0) * settings.line_ratio / 100.;
            // First layer: the ends of the lines half a line width into the left border and the tab. Above: the left arms to the outer
            // edge of the frame, the right ones a border width into the tab.
            CHECK(std::abs(unscaled(inner.min.x()) - (unscaled(first.min.x()) + line / 2.)) < eps);
            CHECK(std::abs(unscaled(inner.max.x()) - (unscaled(first.max.x()) - line / 2.)) < eps);
            CHECK(std::abs(unscaled(second.min.x()) - fb.min.x()) < eps);
            const double border = unscaled(inner.min.x()) - fb.min.x();
            CHECK(std::abs(unscaled(second.max.x()) - (unscaled(inner.max.x()) + border)) < eps);
            const double box_right = unscaled(first.max.x());
            CHECK(unscaled(second.max.x()) < box_right + margin);
            // The first layer of the chevrons between the top and bottom borders; above it inside the frame (the left arm of the
            // last one over the corner of the frame); the chevrons not overlapping each other.
            CHECK(first.min.y() > inner.min.y());
            CHECK(first.max.y() < inner.max.y());
            for (size_t i = 0; i < bends.size(); ++ i) {
                INFO("chevron " << i << " " << text(bends[i]));
                CHECK(bends[i].min.y() > fb.min.y());
                CHECK(bends[i].max.y() < fb.max.y());
                if (i > 0)
                    CHECK(bends[i].min.y() > bends[i - 1].min.y());
            }
            // One piece with the walls.
            CHECK(union_ex(to_polygons(frame_first_layer), walls_first_layer).size() == 1);
            // The numbers on the tab, 2 mm from its edges (the top row may reach onto the top border, which runs over the tab).
            REQUIRE(! numbers.empty());
            double numbers_min_x = std::numeric_limits<double>::max(), numbers_max_x = std::numeric_limits<double>::lowest();
            for (size_t i = 0; i < numbers.size(); ++ i) {
                INFO("number part " << i << " " << text(numbers[i]));
                CHECK(numbers[i].min.x() >= box_right + margin - eps);
                CHECK(numbers[i].max.x() <= fb.max.x() - margin + eps);
                CHECK(numbers[i].min.y() >= fb.min.y() - eps);
                CHECK(numbers[i].max.y() <= fb.max.y() + eps);
                numbers_min_x = std::min(numbers_min_x, numbers[i].min.x());
                numbers_max_x = std::max(numbers_max_x, numbers[i].max.x());
            }
            CHECK(std::abs(numbers_min_x - (box_right + margin)) < eps);
            CHECK(std::abs(numbers_max_x - (fb.max.x() - margin)) < eps);
        }
    }
    SECTION("SuperSlicer's bends: 4 borders, 2 mm around the numbers") {
        PressureCalibrationSettings ss = settings;
        ss.ellis_bends = false;
        PressureCalibrationTest test;
        test.values = pressure_calibration_values(0., 0.02, 0.005);
        Model        model;
        ModelObject *object = load_base(model);
        REQUIRE(pressure_calibration_build(*object, config, ss, test, 0, resources()));
        std::vector<BoundingBoxf3> borders, numbers;
        for (size_t i = 1; i < object->volumes.size(); ++ i) {
            const std::string &name = object->volumes[i]->name;
            if (name == "pa_border.3mf")
                borders.emplace_back(bbox(object->volumes[i]));
            else if (name.rfind("90_bend", 0) != 0)
                numbers.emplace_back(bbox(object->volumes[i]));
        }
        REQUIRE(borders.size() == 4);
        REQUIRE(! numbers.empty());
        const BoundingBoxf3 &tab = borders[1];
        double numbers_min_x = std::numeric_limits<double>::max(), numbers_max_x = std::numeric_limits<double>::lowest();
        for (const BoundingBoxf3 &n : numbers) {
            numbers_min_x = std::min(numbers_min_x, n.min.x());
            numbers_max_x = std::max(numbers_max_x, n.max.x());
        }
        CHECK(std::abs(numbers_min_x - (tab.min.x() + margin)) < eps);
        CHECK(std::abs(numbers_max_x - (tab.max.x() - margin)) < eps);
    }
    SECTION("At least 2 values") {
        PressureCalibrationTest test;
        test.values = { 0.02 };
        Model model;
        CHECK_THROWS_AS(pressure_calibration_build(*load_base(model), config, settings, test, 0, resources()), Slic3r::RuntimeError);
    }
}

TEST_CASE("Pressure calibration settings", "[PressureCalibration]") {
    DynamicPrintConfig config = test_config();
    PressureCalibrationSettings settings;
    pressure_calibration_apply_settings(config, settings);
    CHECK(config.opt_float("layer_height") == 0.2);
    CHECK(config.get_abs_value("first_layer_height", 0.2) == 0.25);
    CHECK(config.option<ConfigOptionFloatOrPercent>("default_acceleration")->value == 0.);
    CHECK(config.get_abs_value("perimeter_acceleration", 1000.) == 0.);
    CHECK(config.opt<ConfigOptionInts>("default_fan_speed")->get_at(0) == 100);
    CHECK(config.opt<ConfigOptionInts>("disable_fan_first_layers")->get_at(0) == 1);
    CHECK(config.opt<ConfigOptionInts>("slowdown_below_layer_time")->get_at(0) == 0);
    settings.first_layer_fan = 50;
    CHECK_THROWS_AS(pressure_calibration_apply_settings(config, settings), Slic3r::RuntimeError);
    settings.first_layer_fan = 100;
    pressure_calibration_apply_settings(config, settings);
    CHECK(config.opt<ConfigOptionInts>("disable_fan_first_layers")->get_at(0) == 0);
}

TEST_CASE("Adaptive PA calibration defaults", "[AdaptivePACalibration]") {
    DynamicPrintConfig config = test_config();
    PressureCalibrationSettings settings;
    settings.line_ratio = 100.;
    pressure_calibration_apply_settings(config, settings);
    // The user's Voron (2026-11-01): max accel 28000, slow 6000, fast 38000.
    config.set_deserialize_strict({ { "perimeter_speed", "80" }, { "external_perimeter_speed", "50%" }, { "infill_speed", "120" },
                                    { "solid_infill_speed", "150%" }, { "top_solid_infill_speed", "20" }, { "small_perimeter_speed", "30" },
                                    { "klipper_max_accel", "28000" }, { "klipper_max_accel_slow", "6000" }, { "klipper_max_accel_fast", "38000" } });

    SECTION("Speeds: small perimeter, fastest, halfway") {
        CHECK(adaptive_pa_default_speeds(config, settings) == std::vector<double>{ 30., 105., 180. });
    }
    SECTION("Small perimeter speed in % of the perimeter speed") {
        config.set_deserialize_strict({ { "small_perimeter_speed", "50%" } });
        CHECK(adaptive_pa_default_speeds(config, settings) == std::vector<double>{ 40., 110., 180. });
    }
    SECTION("The fastest speed capped by the max volumetric speed") {
        // 0.4 x 0.2 mm line: 0.0714 mm³/mm, 10 mm³/s = 140 mm/s.
        config.set_deserialize_strict({ { "filament_max_volumetric_speed", "10" } });
        CHECK(adaptive_pa_default_speeds(config, settings) == std::vector<double>{ 30., 85., 140. });
    }
    SECTION("All the speeds the same as the small perimeter speed: one") {
        config.set_deserialize_strict({ { "perimeter_speed", "30" }, { "external_perimeter_speed", "30" }, { "infill_speed", "30" },
                                        { "solid_infill_speed", "30" }, { "top_solid_infill_speed", "30" } });
        CHECK(adaptive_pa_default_speeds(config, settings) == std::vector<double>{ 30. });
    }
    SECTION("Accelerations: 25 % of slow, the speed mode's (default acceleration 0), fast") {
        config.set_deserialize_strict({ { "default_acceleration", "0" }, { "klipper_speed_mode", "Slow" } });
        CHECK(adaptive_pa_speed_mode_accel(config) == 6000.);
        CHECK(adaptive_pa_default_accelerations(config) == std::vector<double>{ 1500., 6000., 38000. });
        config.set_deserialize_strict({ { "klipper_speed_mode", "Off" } });
        CHECK(adaptive_pa_default_accelerations(config) == std::vector<double>{ 1500., 28000., 38000. });
        config.set_deserialize_strict({ { "klipper_speed_mode", "Fast" } });
        CHECK(adaptive_pa_default_accelerations(config) == std::vector<double>{ 1500., 38000. });
    }
    SECTION("Accelerations: the default acceleration when set, in mm/s² or % of the Klipper max accel") {
        config.set_deserialize_strict({ { "default_acceleration", "6000" }, { "klipper_speed_mode", "Slow" } });
        CHECK(adaptive_pa_default_accelerations(config) == std::vector<double>{ 1500., 6000., 38000. });
        config.set_deserialize_strict({ { "default_acceleration", "50%" } });
        CHECK(adaptive_pa_default_accelerations(config) == std::vector<double>{ 1500., 14000., 38000. });
    }
    SECTION("Slow and fast in % of the Klipper max accel") {
        config.set_deserialize_strict({ { "default_acceleration", "0" }, { "klipper_max_accel", "10000" }, { "klipper_max_accel_slow", "80%" },
                                        { "klipper_max_accel_fast", "120%" } });
        CHECK(adaptive_pa_default_accelerations(config) == std::vector<double>{ 2000., 10000., 12000. });
    }
}

TEST_CASE("Adaptive PA model per nozzle", "[AdaptivePACalibration]") {
    const std::string model04 = "0.04,3.96,1500\n0.03,7.91,1500\n";
    // Orca's lines before any header (pasted) stay.
    std::string model = adaptive_pa_model_set_section("0.05,2,3000\n", 0.4, model04);
    CHECK(model == "0.05,2,3000\n; nozzle 0.4\n" + model04);
    model = adaptive_pa_model_set_section(model, 0.6, "0.06,5.2,1500\n");
    CHECK(model == "0.05,2,3000\n; nozzle 0.4\n" + model04 + "; nozzle 0.6\n0.06,5.2,1500\n");
    // Again for 0.4: only its section changes, moved to the end.
    model = adaptive_pa_model_set_section(model, 0.4, "0.02,8,6000\n");
    CHECK(model == "0.05,2,3000\n; nozzle 0.6\n0.06,5.2,1500\n; nozzle 0.4\n0.02,8,6000\n");
    CHECK(adaptive_pa_model_section(model, 0.4) == "0.02,8,6000\n");
    CHECK(adaptive_pa_model_section(model, 0.6) == "0.06,5.2,1500\n");
    CHECK(adaptive_pa_model_section(model, 0.25).empty());
    CHECK(adaptive_pa_model_set_section("", 0.25, "0.08,1,1500\n") == "; nozzle 0.25\n0.08,1,1500\n");
    // Windows line ends in a pasted model.
    CHECK(adaptive_pa_model_section("; nozzle 0.4\r\n0.04,3.96,1500\r\n", 0.4) == "0.04,3.96,1500\n");
}

TEST_CASE("Adaptive PA calibration flow and model", "[AdaptivePACalibration]") {
    DynamicPrintConfig config = test_config();
    PressureCalibrationSettings settings;
    settings.line_ratio = 100.;
    pressure_calibration_apply_settings(config, settings);
    // 0.4 x 0.2 mm line: (0.4 - 0.2) * 0.2 + pi * 0.1² mm³/mm.
    const double mm3_per_mm = 0.2 * 0.2 + M_PI * 0.01;

    SECTION("Flow = speed x cross section") {
        CHECK(adaptive_pa_effective_speed(config, settings, 100.) == 100.);
        CHECK(std::abs(adaptive_pa_flow(config, settings, 100.) - 100. * mm3_per_mm) < 1e-6);
    }
    SECTION("Capped by the max volumetric speed, the lowest of print and filament") {
        config.set_deserialize_strict({ { "max_volumetric_speed", "6" }, { "filament_max_volumetric_speed", "5" } });
        CHECK(std::abs(adaptive_pa_effective_speed(config, settings, 100.) - 5. / mm3_per_mm) < 1e-3); // Flow: floats
        CHECK(std::abs(adaptive_pa_flow(config, settings, 100.) - 5.) < 1e-6);
        CHECK(adaptive_pa_effective_speed(config, settings, 50.) == 50.);
    }
    SECTION("Model lines as OrcaSlicer's, sorted by acceleration then flow") {
        CHECK(adaptive_pa_model({ { 0.026, 7.912, 10000. }, { 0.04, 3.956, 3000. }, { 0.0333, 3.96, 10000. }, { 0.029, 7.91, 3000. } }) ==
              "0.04,3.96,3000\n0.029,7.91,3000\n0.0333,3.96,10000\n0.026,7.91,10000\n");
    }
    SECTION("Tests kept between the calibration and the results") {
        const std::vector<AdaptivePATest> tests { { 50., 2000., 3.5708, 0.4 }, { 120.5, 6000., 8.57, 0.6, 0.026, 7 } };
        const std::vector<AdaptivePATest> back = adaptive_pa_tests_deserialize(adaptive_pa_tests_serialize(tests));
        REQUIRE(back.size() == 2);
        CHECK(back[1].speed == 120.5);
        CHECK(back[1].acceleration == 6000.);
        CHECK(back[0].flow == 3.5708);
        CHECK(back[1].nozzle == 0.6);
        CHECK(back[0].pa == -1.);
        CHECK(back[1].pa == 0.026);
        CHECK(back[0].id == -1);
        CHECK(back[1].id == 7);
        // Without the typed value (4 fields): none.
        CHECK(adaptive_pa_tests_deserialize("50,2000,3.5,0.4").front().pa == -1.);
        CHECK(adaptive_pa_tests_deserialize("").empty());
    }
}

TEST_CASE("Adaptive PA calibration side length", "[AdaptivePACalibration]") {
    DynamicPrintConfig config = test_config();
    // Klipper's default minimum_cruise_ratio 0.5: accelerating and decelerating take speed² / acceleration, half of the side.
    config.set_deserialize_strict({ { "klipper_minimum_cruise_ratio", "0.5" } });
    CHECK(adaptive_pa_side_length(config, 300., 1500., 20.) == 120.);
    CHECK(adaptive_pa_side_length(config, 300., 6000., 20.) == 30.);
    CHECK(adaptive_pa_side_length(config, 165., 1500., 20.) == 37.);   // 36.3 rounded up
    CHECK(adaptive_pa_side_length(config, 100., 38000., 20.) == 20.);  // at least the minimum
    CHECK(adaptive_pa_side_length(config, 100., 0., 25.) == 25.);
    // A higher minimum cruise ratio: Klipper keeps more of the move at the top speed, the side gets longer.
    config.set_deserialize_strict({ { "klipper_minimum_cruise_ratio", "0.8" } });
    CHECK(adaptive_pa_side_length(config, 300., 1500., 20.) == 300.);
    // None: still half of the side at the speed.
    config.set_deserialize_strict({ { "klipper_minimum_cruise_ratio", "0" } });
    CHECK(adaptive_pa_side_length(config, 300., 1500., 20.) == 120.);
}

TEST_CASE("Adaptive PA calibration tests kept per acceleration", "[AdaptivePACalibration]") {
    const std::vector<AdaptivePATest> kept { { 15., 6000., 1., 0.4, 0.03 }, { 100., 6000., 7., 0.4, 0.02 }, { 15., 1500., 1., 0.4, 0.035 } };
    SECTION("Another acceleration: added, sorted by acceleration, the typed values kept") {
        const std::vector<AdaptivePATest> out = adaptive_pa_tests_merge(kept, { { 15., 38000., 1., 0.4 }, { 100., 38000., 7., 0.4 } });
        REQUIRE(out.size() == 5);
        CHECK(out[0].acceleration == 1500.);
        CHECK(out[1].acceleration == 6000.);
        CHECK(out[1].pa == 0.03);
        CHECK(out[2].speed == 100.);
        CHECK(out[3].acceleration == 38000.);
        CHECK(out[4].pa == -1.);
    }
    SECTION("The same acceleration again: replaced") {
        const std::vector<AdaptivePATest> out = adaptive_pa_tests_merge(kept, { { 30., 6000., 2., 0.4 } });
        REQUIRE(out.size() == 2);
        CHECK(out[0].acceleration == 1500.);
        CHECK(out[1].speed == 30.);
    }
    SECTION("Another nozzle: all replaced") {
        const std::vector<AdaptivePATest> out = adaptive_pa_tests_merge(kept, { { 30., 1500., 3., 0.6 } });
        REQUIRE(out.size() == 1);
        CHECK(out[0].nozzle == 0.6);
    }
}

TEST_CASE("Adaptive PA calibration longer sides", "[AdaptivePACalibration]") {
    // The longest wall of a layer above the first one, with the side length of the settings and twice as long for the test.
    const auto longest_wall = [](double side_length) {
        DynamicPrintConfig config = test_config();
        PressureCalibrationSettings settings;
        PressureCalibrationTest test;
        test.values       = pressure_calibration_values(0., 0.01, 0.005);
        test.speed        = 100.;
        test.side_length  = side_length;
        std::map<size_t, double> path_lengths;
        const std::vector<PAExtrusion> extrusions = walk(slice_calibration(config, settings, { test }), &path_lengths);
        double longest = 0.;
        for (const PAExtrusion &e : extrusions)
            if (e.layer >= 2)
                longest = std::max(longest, path_lengths[e.path]);
        return longest;
    };
    const double ellis  = longest_wall(0.);   // Ellis' 30 mm
    const double longer = longest_wall(60.);
    // Both arms 30 mm longer (the walls over the frame keep their extra length).
    CHECK(std::abs(longer - ellis - 60.) < 1.);
}

TEST_CASE("Adaptive PA calibration test print", "[AdaptivePACalibration]") {
    DynamicPrintConfig config = test_config();
    PressureCalibrationSettings settings;
    PressureCalibrationTest test;
    test.values       = pressure_calibration_values(0., 0.01, 0.005);
    test.speed        = 150.;
    test.acceleration = 4000.;
    int accel_commands = 0;
    const std::string gcode = slice_calibration(config, settings, { test });
    const std::vector<PAExtrusion> extrusions = walk(gcode, nullptr, &accel_commands);

    // The walls at the test's speed (from the third layer only the walls print).
    std::set<long> wall_speeds;
    for (const PAExtrusion &e : extrusions)
        if (e.layer >= 2)
            wall_speeds.insert(std::lround(e.speed));
    CHECK(wall_speeds == std::set<long>{ 150 });
    // The test's acceleration before every wall's value, and no other acceleration command (no M204).
    size_t walls = 0, with_accel = 0;
    for (size_t pos = gcode.find(" wall "); pos != std::string::npos; pos = gcode.find(" wall ", pos + 1)) {
        const size_t line = gcode.rfind('\n', pos) + 1;
        if (gcode.compare(line, 29, "SET_PRESSURE_ADVANCE ADVANCE=") != 0)
            continue;
        ++ walls;
        const size_t previous = gcode.rfind('\n', line - 2) + 1;
        with_accel += gcode.compare(previous, 30, "SET_VELOCITY_LIMIT ACCEL=4000\n") == 0;
    }
    CHECK(walls > 0);
    CHECK(with_accel == walls);
    CHECK(gcode.find("M204") == std::string::npos);
    CHECK(size_t(accel_commands) == walls);
    // The summary with the speed, acceleration and flow of the test.
    CHECK(gcode.find("; ### Adaptive pressure advance calibration") != std::string::npos);
    CHECK(gcode.find(";  - Print speed: per test") != std::string::npos);
    CHECK(gcode.find(";  - Speed 150 mm/s, acceleration 4000 mm/s^2, flow ") != std::string::npos);
}

// Like for like with Ellis' pattern (Calibration > Pressure advance pattern), with Ellis' defaults on both and the printer /
// filament of real profiles: writes both G-code files for Stuff/compare_pa.py (feature types, speeds, widths, PA, corners of
// each section). Not run by ctest. Environment: PA_CMP_INI = the print, filament and printer .ini separated by '|',
// PA_CMP_OUT = the folder of the output.
TEST_CASE("Pressure calibration vs Ellis' pattern", "[.PACompare]") {
    const char *ini = std::getenv("PA_CMP_INI");
    const char *out = std::getenv("PA_CMP_OUT");
    REQUIRE(ini != nullptr);
    REQUIRE(out != nullptr);
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    std::vector<std::string> files;
    boost::split(files, std::string(ini), boost::is_any_of("|"));
    for (const std::string &file : files)
        config.load(file, ForwardCompatibilitySubstitutionRule::Enable);
    config.normalize_fdm();

    // Ellis' defaults on both: his pattern's parameters are his defaults, the settings go into the config of both.
    const PressureCalibrationSettings settings;
    const PressureAdvancePatternParams ellis;
    REQUIRE(ellis.speed == settings.speed);
    REQUIRE(ellis.first_layer_speed == settings.first_layer_speed);
    REQUIRE(ellis.line_ratio == settings.line_ratio);
    REQUIRE(ellis.layers == settings.layers);
    REQUIRE(ellis.fan == settings.fan);
    REQUIRE(ellis.first_layer_fan == settings.first_layer_fan);
    REQUIRE(ellis.acceleration == settings.acceleration);
    DynamicPrintConfig ellis_config = config;
    pressure_calibration_apply_settings(ellis_config, settings);
    PressureCalibrationTest test;
    test.first_pa = ellis.pa_start;
    test.values   = pressure_calibration_values(ellis.pa_start, ellis.pa_end, ellis.pa_step);
    REQUIRE(int(test.values.size()) == pressure_advance_pattern_count(ellis));
    // PA_CMP_COUNT: only the SuperSlicer style test, with that many bends (to look at its frame).
    if (const char *count = std::getenv("PA_CMP_COUNT"); count != nullptr) {
        test.values.resize(std::atoi(count));
        const std::string gcode = slice_calibration(config, settings, { test });
        boost::nowide::ofstream f((boost::filesystem::path(out) / ("pa_superslicer_" + std::string(count) + ".gcode")).string(), std::ios::binary);
        f << gcode;
        return;
    }

    const auto write = [out](const std::string &name, const std::string &gcode) {
        boost::nowide::ofstream f((boost::filesystem::path(out) / name).string(), std::ios::binary);
        f << gcode;
        REQUIRE(f.good());
    };
    write("pa_superslicer.gcode", slice_calibration(config, settings, { test }));
    write("pa_ellis.gcode", pressure_advance_pattern_gcode(ellis_config, ellis));
}

// Writes the G-code of an adaptive PA calibration test (speed 100, acceleration 28000, test number 1) to APA_PREVIEW_OUT, to look
// at the frame and its labels. Not run by ctest.
TEST_CASE("Adaptive PA calibration preview", "[.APAPreview]") {
    const char *out = std::getenv("APA_PREVIEW_OUT");
    REQUIRE(out != nullptr);
    DynamicPrintConfig config = test_config();
    PressureCalibrationSettings settings;
    PressureCalibrationTest test;
    test.values       = pressure_calibration_values(0.01, 0.05, 0.001);
    test.speed        = 100.;
    test.acceleration = 28000.;
    test.side_length  = 20.;
    boost::nowide::ofstream(out) << slice_calibration(config, settings, { test }, 1);
}

TEST_CASE("Adaptive PA calibration label: test number and acceleration", "[AdaptivePACalibration]") {
    // The frame of a test: its number, and with an acceleration the acceleration beside it (user 2026-11-05): "1 28000" = 5 more digits.
    DynamicPrintConfig config = test_config();
    PressureCalibrationSettings settings;
    pressure_calibration_apply_settings(config, settings);
    pressure_calibration_print_config(config);
    const auto parts = [&config, &settings](double acceleration, size_t id) {
        PressureCalibrationTest test;
        test.values       = pressure_calibration_values(0.01, 0.05, 0.001);
        test.speed        = 100.;
        test.acceleration = acceleration;
        Model model;
        ModelObject *base = load_base(model);
        const size_t before = base->volumes.size();
        REQUIRE(pressure_calibration_build(*base, config, settings, test, id, resources()));
        return base->volumes.size() - before;
    };
    CHECK(parts(28000., 1) == parts(0., 1) + 5);
    CHECK(parts(1500., 2) == parts(0., 2) + 4);
    // A number above 9 is printed too (it wasn't before).
    CHECK(parts(0., 12) == parts(0., 1) + 1);
    // Sliced: the walls at the test's acceleration.
    PressureCalibrationTest test;
    test.values       = pressure_calibration_values(0.01, 0.03, 0.005);
    test.speed        = 100.;
    test.acceleration = 28000.;
    CHECK(slice_calibration(test_config(), settings, { test }, 1).find("SET_VELOCITY_LIMIT ACCEL=28000") != std::string::npos);
}

TEST_CASE("Adaptive PA results", "[AdaptivePACalibration]") {
    // Two prints of the 0.4 nozzle (1500 and 6000, 3 tests each in the order of their frames) and one of the 0.6 nozzle.
    const std::vector<AdaptivePATest> kept_in {
        { 15., 1500., 1.1, 0.4 }, { 58., 1500., 4.1, 0.4 }, { 100., 1500., 7.1, 0.4 },
        { 15., 6000., 1.2, 0.4, 0.04 }, { 58., 6000., 4.2, 0.4 }, { 100., 6000., 7.2, 0.4 },
        { 20., 6000., 2.0, 0.6, 0.05 } };
    std::vector<AdaptivePATest> kept = kept_in;
    std::string error;
    SECTION("Calibrations of a nozzle") {
        CHECK(adaptive_pa_calibration_accelerations(kept, 0.4) == std::vector<double>{ 1500., 6000. });
        CHECK(adaptive_pa_calibration_accelerations(kept, 0.6) == std::vector<double>{ 6000. });
        CHECK(adaptive_pa_calibration_tests(kept, 0.4, 6000.) == std::vector<size_t>{ 3, 4, 5 });
        CHECK(adaptive_pa_calibration_tests(kept, 0.4, 6000.3) == std::vector<size_t>{ 3, 4, 5 });
        CHECK(adaptive_pa_calibration_tests(kept, 0.4, 28000.).empty());
    }
    SECTION("Values matched to the flows of the tests, the rows in any order") {
        const std::vector<AdaptivePAPoint> points = adaptive_pa_results(kept, 0.4,
            { { 6000., { 0.03, 0.028, 0.025 } }, { 1500., { -1., 0.033, 0.031 } } }, error);
        CHECK(error.empty());
        REQUIRE(points.size() == 5);
        CHECK(adaptive_pa_model(points) == "0.033,4.10,1500\n0.031,7.10,1500\n0.03,1.20,6000\n0.028,4.20,6000\n0.025,7.20,6000\n");
        // The typed values kept, the empty one -1, the other nozzle untouched.
        CHECK(kept[0].pa == -1.);
        CHECK(kept[1].pa == 0.033);
        CHECK(kept[3].pa == 0.03);
        CHECK(kept[6].pa == 0.05);
    }
    SECTION("A row without values is left out, its kept values cleared") {
        CHECK(adaptive_pa_results(kept, 0.4, { { 1500., { 0.033, 0.031, -1. } }, { 6000., { -1., -1., -1. } } }, error).size() == 2);
        CHECK(kept[3].pa == -1.);
    }
    SECTION("Errors, kept unchanged") {
        CHECK(adaptive_pa_results(kept, 0.4, { { 28000., { 0.02, 0.018 } } }, error).empty());
        CHECK(error.find("28000 mm/s²: no calibration printed") == 0);
        CHECK(adaptive_pa_results(kept, 0.4, { { 6000., { 0.03, 0.028, 0.025, 0.02 } } }, error).empty());
        CHECK(error.find("test 4 doesn't exist, that print had 3 tests") != std::string::npos);
        CHECK(adaptive_pa_results(kept, 0.4, { { 6000., { 0.03, -1., -1. } } }, error).empty());
        CHECK(error.find("at least 2 tests") != std::string::npos);
        CHECK(adaptive_pa_results(kept, 0.4, { { 6000., { 0.03, 0.02 } }, { 6000., { 0.03, 0.02 } } }, error).empty());
        CHECK(error.find("two rows") != std::string::npos);
        CHECK(adaptive_pa_results(kept, 0.4, { { 6000., { 0.03, 2.5 } } }, error).empty());
        CHECK(error.find("from 0 to 2") != std::string::npos);
        CHECK(adaptive_pa_results(kept, 0.4, { { 6000., { -1., -1. } } }, error).empty());
        CHECK(error.find("No value") == 0);
        CHECK(kept[3].pa == 0.04);
    }
}
