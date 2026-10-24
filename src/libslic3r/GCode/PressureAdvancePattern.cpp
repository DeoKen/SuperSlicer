// Pressure advance calibration pattern, ported from Ellis' Pressure Advance / Linear Advance Calibration Tool
// (javascript/pressure_linear_advance.js, https://github.com/AndrewEllis93/Pressure_Linear_Advance_Tool).
// Copyright (C) 2019 Sineos [https://github.com/Sineos]
// Copyright (C) 2022 AndrewEllis93 [https://github.com/AndrewEllis93]
// Licensed under the GNU General Public License v3 or later; this port is part of PrusaSlicer (AGPL-3.0).

#include "PressureAdvancePattern.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <boost/algorithm/string/predicate.hpp>

#include "BedKeepOut.hpp"
#include "../BoundingBox.hpp"
#include "../Exception.hpp"
#include "../ExtrusionRole.hpp"
#include "../I18N.hpp"
#include "../PlaceholderParser.hpp"
#include "../PrintConfig.hpp"
#include "../Utils.hpp"
#include "../format.hpp"

namespace Slic3r {

namespace {

constexpr double GLYPH_PADDING_HORIZONTAL = 1.;
constexpr double GLYPH_PADDING_VERTICAL   = 1.;
constexpr double ENCROACHMENT             = 1. / 3.;

double rad(double degrees) { return degrees * M_PI / 180.; }

// Math.round10(value, -decimals) of the original.
double round_to(double value, int decimals)
{
    const double f = std::pow(10., decimals);
    return std::round(value * f) / f;
}

// A rounded number as JavaScript prints it: no trailing zeros.
std::string num(double value, int decimals)
{
    value = round_to(value, decimals);
    if (value == 0.)
        value = 0.; // no "-0"
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        s.erase(s.find_last_not_of('0') + 1);
        if (s.back() == '.')
            s.pop_back();
    }
    return s;
}

// Does the custom G-code set the temperature with one of the M-codes (as GCode.cpp custom_gcode_sets_temperature())?
bool gcode_sets_temperature(const std::string &gcode, int mcode_set, int mcode_wait)
{
    size_t pos = 0;
    while (pos < gcode.size()) {
        size_t end = gcode.find('\n', pos);
        if (end == std::string::npos)
            end = gcode.size();
        size_t i = gcode.find_first_not_of(" \t", pos);
        if (i != std::string::npos && i < end && gcode[i] == 'M') {
            char *endptr = nullptr;
            const long code = std::strtol(gcode.c_str() + i + 1, &endptr, 10);
            if (endptr != gcode.c_str() + i + 1 && (code == mcode_set || code == mcode_wait))
                return true;
        }
        pos = end + 1;
    }
    return false;
}

// Letter of a Klipper speed mode for {speed_mode}, empty for "Off" (as GCode.cpp, doc section 22).
const char* speed_mode_letter(const std::string &mode)
{
    return boost::iequals(mode, "slow") ? "S" : boost::iequals(mode, "normal") ? "N" : boost::iequals(mode, "fast") ? "F" : "";
}

class Generator
{
public:
    Generator(const DynamicPrintConfig &config, const PressureAdvancePatternParams &p) : m_p(p)
    {
        m_nozzle          = config.opt<ConfigOptionFloats>("nozzle_diameter")->get_at(0);
        m_filament_area   = M_PI * std::pow(config.opt<ConfigOptionFloats>("filament_diameter")->get_at(0) / 2., 2);
        m_ext_mult        = config.opt<ConfigOptionFloats>("extrusion_multiplier")->get_at(0);
        m_layer_height    = config.opt_float("layer_height");
        m_first_layer     = config.get_abs_value("first_layer_height", m_layer_height);
        m_travel_speed    = config.opt_float("travel_speed") * 60.;
        m_retract_dist    = config.opt<ConfigOptionFloats>("retract_length")->get_at(0);
        m_retract_speed   = config.opt<ConfigOptionFloats>("retract_speed")->get_at(0) * 60.;
        const double deretract = config.opt<ConfigOptionFloats>("deretract_speed")->get_at(0);
        m_unretract_speed = deretract > 0. ? deretract * 60. : m_retract_speed;
        m_zhop            = config.opt<ConfigOptionFloats>("retract_lift")->get_at(0);
        m_fw_retract      = config.opt_bool("use_firmware_retraction");
        const BoundingBoxf bed(config.opt<ConfigOptionPoints>("bed_shape")->values);
        m_bed             = bed;
        m_center          = bed.center();

        m_line_width        = m_nozzle * p.line_ratio / 100.;
        m_anchor_width      = m_nozzle * p.anchor_line_ratio / 100.;
        m_line_spacing      = m_line_width - m_layer_height * (1. - M_PI / 4.);
        m_anchor_spacing    = m_anchor_width - m_first_layer * (1. - M_PI / 4.);
        m_spacing_angle     = m_line_spacing / std::sin(rad(p.corner_angle) / 2.);
        m_patterns          = pressure_advance_pattern_count(p);
    }

    // The pattern from the first layer to the end, before the end G-code.
    std::string body()
    {
        const PressureAdvancePatternParams &p = m_p;
        const double start_x = pattern_start_x(), start_y = pattern_start_y();
        const double sin_half = std::sin(rad(p.corner_angle) / 2.), cos_half = std::cos(rad(p.corner_angle) / 2.);

        out += "G21 ; Millimeter units\nG90 ; Absolute XYZ\nM83 ; Relative E\nG92 E0 ; Reset extruder distance\n";
        out += fan(p.first_layer_fan);
        if (p.acceleration > 0.)
            out += "SET_VELOCITY_LIMIT ACCEL=" + num(p.acceleration, 0) + " ; Set printing acceleration\n";

        // Move to the first layer.
        retract(false, false);
        move_z(5., "Z raise");
        move_to(start_x, start_y, "Move to start position", false, false);
        layer_change(m_first_layer, m_first_layer);
        move_z(m_first_layer, "Move to start layer height");
        retract(true, false);
        set_pa(p.pa_start, "Set pressure advance to start value");

        // Anchor frame or layer, with the tab for the numbers.
        const double tab_y      = start_y + frame_size_y() + m_anchor_spacing;
        const double tab_size_x = glyph_tab_max_x() - start_x;
        const double tab_size_y = max_numbering_height() + m_anchor_spacing + GLYPH_PADDING_VERTICAL * 2.;
        if (p.anchor == PressureAdvancePatternParams::Anchor::Frame) {
            box(start_x, start_y, print_size_x(), frame_size_y(), false, p.anchor_perimeters);
            if (p.numbers)
                box(start_x, tab_y, tab_size_x, tab_size_y, true, p.anchor_perimeters);
        } else if (p.anchor == PressureAdvancePatternParams::Anchor::Layer) {
            box(start_x, start_y, print_size_x(), frame_size_y(), true, p.anchor_perimeters);
            if (p.numbers)
                box(start_x, tab_y, tab_size_x, tab_size_y, true, p.anchor_perimeters);
        }

        // The patterns, the first layer skipped with a full anchor layer.
        for (int i = (p.anchor == PressureAdvancePatternParams::Anchor::Layer ? 1 : 0); i < p.layers; ++ i) {
            const double height = i == 0 ? m_first_layer : m_layer_height;
            const double speed  = (i == 0 ? p.first_layer_speed : p.speed) * 60.;
            if (i == 1)
                out += fan(p.fan);
            const double z = i * m_layer_height + m_first_layer;
            if (i > 0)
                layer_change(z, height);
            move_z(z, "Move to layer height");

            // Numbers on the tab, on the second layer (on the first one without an anchor).
            if (p.numbers && ((p.anchor != PressureAdvancePatternParams::Anchor::None && i == 1) ||
                              (p.anchor == PressureAdvancePatternParams::Anchor::None && i == 0))) {
                set_pa(p.pa_start, "Set pressure advance to start value for numbering");
                for (int j = 0; j < m_patterns; j += 2) {
                    double x = start_x + j * (p.spacing + m_line_width) + j * ((p.wall_count - 1) * m_spacing_angle);
                    x += ((p.wall_count - 1) / 2.) * m_spacing_angle - 2.; // center the glyph on the walls (2 = half a glyph)
                    x += pattern_shift();
                    draw_number(x, start_y + frame_size_y() + GLYPH_PADDING_VERTICAL + m_line_width, pa_value_string(j), height);
                }
            }

            double to_x = start_x + pattern_shift(), to_y = start_y, side = p.side_length;
            if (i == 0 && p.anchor == PressureAdvancePatternParams::Anchor::Frame) {
                // First layer inside the frame: shrink to fit.
                const double shrink = (m_anchor_spacing * (p.anchor_perimeters - 1) + m_anchor_width * (1. - ENCROACHMENT)) / sin_half;
                side -= shrink;
                to_x += shrink * std::sin(rad(90.) - rad(p.corner_angle) / 2.);
                to_y += m_anchor_spacing * (p.anchor_perimeters - 1) + m_anchor_width * (1. - ENCROACHMENT);
            }
            const double initial_x = to_x, initial_y = to_y;
            move_to(to_x, to_y, "Move to pattern start");

            for (int j = 0; j < m_patterns; ++ j) {
                set_pa(pa_value(j), "Set pressure advance");
                for (int k = 0; k < p.wall_count; ++ k) {
                    to_x += cos_half * side;
                    to_y += sin_half * side;
                    line(to_x, to_y, height, m_line_width, speed, GCodeExtrusionRole::ExternalPerimeter, "Print pattern wall");
                    to_x -= cos_half * side;
                    to_y += sin_half * side;
                    line(to_x, to_y, height, m_line_width, speed, GCodeExtrusionRole::ExternalPerimeter, "Print pattern wall");
                    to_y = initial_y;
                    if (k != p.wall_count - 1) {
                        to_x += m_spacing_angle;
                        move_to(to_x, to_y, "Move to start next pattern wall");
                    } else if (j != m_patterns - 1) {
                        to_x += p.spacing + m_line_width;
                        move_to(to_x, to_y, "Move to next pattern");
                    } else if (i != p.layers - 1) {
                        to_x = initial_x;
                        move_to(to_x, to_y, "Move back to start position");
                    }
                }
            }
        }

        set_pa(p.pa_start, "Set pressure advance back to start value");
        retract(false, m_zhop > 0.);
        move_z(m_cur_z + 5., "Z raise");
        out += "M104 S0 ; Turn off hotend\nM140 S0 ; Turn off bed\nM107 ; Turn off fan\n";
        return out;
    }

    // Bounding box of the extrusions, bed coordinates.
    const BoundingBoxf& extrusion_bbox() const { return m_bbox; }
    double              max_z()          const { return m_max_z; }
    int                 layer_count()    const { return m_layer_count; }
    double              first_layer()    const { return m_first_layer; }
    double              line_width()     const { return m_line_width; }
    const BoundingBoxf& bed()            const { return m_bed; }

    std::string pa_value_string(int j) const { return num(pa_value(j), 4); }

private:
    double pa_value(int j) const { return round_to(m_p.pa_start + j * m_p.pa_step, 4); }

    // Sizes, as the getters of the original Settings object.
    double object_size_x() const
    {
        const PressureAdvancePatternParams &p = m_p;
        double size = round_to(m_patterns * ((p.wall_count - 1) * m_spacing_angle) + (m_patterns - 1) * (p.spacing + m_line_width) +
                               std::cos(rad(p.corner_angle) / 2.) * p.side_length, 4);
        // The frame grows to the right, so that the last pattern doesn't run over it.
        if (p.anchor == PressureAdvancePatternParams::Anchor::Frame)
            size += m_anchor_spacing * p.anchor_perimeters;
        return size;
    }
    double object_size_y() const
    {
        return round_to(2. * std::sin(rad(m_p.corner_angle) / 2.) * m_p.side_length, 4) + max_numbering_height() +
               GLYPH_PADDING_VERTICAL * 2. + m_anchor_width;
    }
    double frame_size_y()    const { return std::sin(rad(m_p.corner_angle / 2.)) * m_p.side_length * 2.; }
    double pattern_start_x() const { return m_center.x() - (object_size_x() + pattern_shift()) / 2.; }
    double pattern_start_y() const { return m_center.y() - object_size_y() / 2.; }
    double print_size_x()    const { return object_size_x() + pattern_shift(); }
    double glyph_start_x()   const
    {
        return m_p.numbers ? m_center.x() - object_size_x() / 2. + (((m_p.wall_count - 1) / 2.) * m_spacing_angle - 2.) : 0.;
    }
    double pattern_shift() const
    {
        if (! m_p.numbers)
            return 0.;
        const double shift = m_center.x() - object_size_x() / 2. - glyph_start_x() + GLYPH_PADDING_HORIZONTAL;
        return shift > 0. ? shift + m_anchor_width / 2. : 0.;
    }
    double glyph_end_x() const
    {
        return m_center.x() - object_size_x() / 2. + (m_patterns - 1) * (m_p.spacing + m_line_width) +
               (m_patterns - 1) * ((m_p.wall_count - 1) * m_spacing_angle) + 4.;
    }
    double glyph_tab_max_x() const { return glyph_end_x() + GLYPH_PADDING_HORIZONTAL + m_anchor_width / 2.; }
    double max_numbering_height() const
    {
        double max_height = 0.;
        for (int i = 0; i < m_patterns; i += 2) {
            double height = 0.;
            for (char c : pa_value_string(i))
                if (c != '1' && c != '.')
                    height += 3.; // glyph spacing
            max_height = std::max(max_height, height);
        }
        return max_height;
    }

    // Rotation by print_dir (clockwise) around the bed center.
    Vec2d rotate(double x, double y) const
    {
        const double a = rad(m_p.print_dir), c = std::cos(a), s = std::sin(a);
        return { c * (x - m_center.x()) + s * (y - m_center.y()) + m_center.x(), c * (y - m_center.y()) - s * (x - m_center.x()) + m_center.y() };
    }
    std::string xy(double x, double y) const
    {
        const Vec2d r = rotate(x, y);
        return "X" + num(r.x(), 4) + " Y" + num(r.y(), 4);
    }

    std::string fan(int percent) const { return "M106 S" + std::to_string(int(std::lround(std::clamp(percent, 0, 100) * 2.55))) + " ; Set fan speed\n"; }

    void set_pa(double value, const char *comment)
    {
        out += "SET_PRESSURE_ADVANCE ADVANCE=" + num(value, 4) + " ; " + comment + "\nM117 PA " + num(value, 4) + "\n";
    }

    void layer_change(double z, double height)
    {
        out += ";LAYER_CHANGE\n;Z:" + num(z, 3) + "\n;HEIGHT:" + num(height, 3) + "\n";
        ++ m_layer_count;
        m_max_z = std::max(m_max_z, z);
    }

    void line(double to_x, double to_y, double height, double width, double speed, GCodeExtrusionRole role, const char *comment)
    {
        if (role != m_role) {
            out += ";TYPE:" + gcode_extrusion_role_to_string(role) + "\n";
            m_role = role;
        }
        if (width != m_width) {
            out += ";WIDTH:" + num(width, 4) + "\n";
            m_width = width;
        }
        const double length = std::hypot(to_x - m_cur_x, to_y - m_cur_y);
        const double area   = (width - height) * height + M_PI * std::pow(height / 2., 2); // cross section of the extrusion
        const double e      = length * area / m_filament_area * m_ext_mult;
        m_bbox.merge(rotate(m_cur_x, m_cur_y));
        m_bbox.merge(rotate(to_x, to_y));
        out += "G1 " + xy(to_x, to_y) + " E" + num(e, 5) + " F" + num(speed, 3) + " ; " + comment + "\n";
        m_cur_x = to_x;
        m_cur_y = to_y;
    }

    void move_to(double to_x, double to_y, const char *comment = "Move", bool do_retract = true, bool hop = true)
    {
        hop = hop && m_zhop > 0.;
        if (to_x == m_cur_x && to_y == m_cur_y)
            return;
        const bool retract_here = do_retract && std::hypot(to_x - m_cur_x, to_y - m_cur_y) > 2.; // no retraction under 2 mm
        if (retract_here)
            retract(false, hop);
        out += "G0 " + xy(to_x, to_y) + " F" + num(m_travel_speed, 3) + " ; " + comment + "\n";
        m_cur_x = to_x;
        m_cur_y = to_y;
        if (retract_here)
            retract(true, hop);
    }

    void move_z(double z, const char *comment)
    {
        out += "G0 Z" + num(z, 3) + " F" + num(m_travel_speed, 3) + " ; " + comment + "\n";
        m_cur_z = z;
    }

    // Retract (unretract = false) with an optional Z hop, or unretract and return from the hop.
    void retract(bool unretract, bool hop)
    {
        if (! unretract) {
            if (! m_retracted) {
                out += m_fw_retract ? std::string("G10 ; Retract\n") : "G1 E-" + num(m_retract_dist, 5) + " F" + num(m_retract_speed, 3) + " ; Retract\n";
                m_retracted = true;
            }
            if (hop && ! m_hopped) {
                out += "G1 Z" + num(m_cur_z + m_zhop, 3) + " F" + num(m_travel_speed, 3) + " ; Z hop\n";
                m_hopped = true;
            }
        } else {
            if (m_hopped) {
                out += "G1 Z" + num(m_cur_z, 3) + " F" + num(m_travel_speed, 3) + " ; Z hop return\n";
                m_hopped = false;
            }
            if (m_retracted) {
                out += m_fw_retract ? std::string("G11 ; Un-retract\n") : "G1 E" + num(m_retract_dist, 5) + " F" + num(m_unretract_speed, 3) + " ; Un-retract\n";
                m_retracted = false;
            }
        }
    }

    // Anchor box on the first layer: perimeters stepping inwards, then optionally a 45° fill.
    void box(double min_x, double min_y, double size_x, double size_y, bool fill, int num_perims)
    {
        const double height = m_first_layer, width = m_anchor_width, speed = m_p.first_layer_speed * 60.;
        const GCodeExtrusionRole role = GCodeExtrusionRole::Skirt;
        const double spacing = width - height * (1. - M_PI / 4.);
        const double max_x = min_x + size_x, max_y = min_y + size_y;
        const double s45 = std::sin(rad(45.));
        // Not more perimeters than the box can take (as many as a concentric fill).
        const int max_perims = int(std::min(std::floor(size_x * s45 / (spacing / s45)), std::floor(size_y * s45 / (spacing / s45))));
        num_perims = std::min(num_perims, max_perims);

        double x = min_x, y = min_y;
        move_to(min_x, min_y, "Move to box start");
        for (int i = 0; i < num_perims; ++ i) {
            if (i != 0) {
                x += spacing;
                y += spacing;
                move_to(x, y, "Step inwards to print next perimeter");
            }
            y += size_y - (i * spacing) * 2.;
            line(x, y, height, width, speed, role, "Draw perimeter (up)");
            x += size_x - (i * spacing) * 2.;
            line(x, y, height, width, speed, role, "Draw perimeter (right)");
            y -= size_y - (i * spacing) * 2.;
            line(x, y, height, width, speed, role, "Draw perimeter (down)");
            x -= size_x - (i * spacing) * 2.;
            line(x, y, height, width, speed, role, "Draw perimeter (left)");
        }
        if (! fill)
            return;

        const double spacing_45 = spacing / s45;
        const double inset      = spacing * (num_perims - 1) + width * (1. - ENCROACHMENT);
        const double x_min = min_x + inset, x_max = max_x - inset, y_min = min_y + inset, y_max = max_y - inset;
        const int    x_count = int(std::floor((x_max - x_min) / spacing_45)), y_count = int(std::floor((y_max - y_min) / spacing_45));
        double x_rem = std::fmod(x_max - x_min, spacing_45), y_rem = std::fmod(y_max - y_min, spacing_45);
        const int lines = y_count + x_count + (x_rem + y_rem >= spacing_45 ? 1 : 0);

        x = x_min;
        y = y_min;
        move_to(x, y, "Move to fill start");
        for (int i = 0; i < lines; ++ i) {
            if (i < std::min(y_count, x_count)) {
                if (i % 2 == 0) {
                    x += spacing_45;
                    y = y_min;
                    move_to(x, y); // step right
                    y += x - x_min;
                    x = x_min;
                    line(x, y, height, width, speed, role, "Fill"); // up / left
                } else {
                    y += spacing_45;
                    x = x_min;
                    move_to(x, y); // step up
                    x += y - y_min;
                    y = y_min;
                    line(x, y, height, width, speed, role, "Fill"); // down / right
                }
            } else if (i < std::max(x_count, y_count)) {
                if (x_count > y_count) { // wider than tall
                    if (i % 2 == 0) {
                        x += spacing_45;
                        y = y_min;
                        move_to(x, y); // step right
                        x -= y_max - y_min;
                        y = y_max;
                        line(x, y, height, width, speed, role, "Fill"); // up / left
                    } else {
                        if (i == y_count) {
                            x += spacing_45 - y_rem;
                            y_rem = 0.;
                        } else
                            x += spacing_45;
                        y = y_max;
                        move_to(x, y); // step right
                        x += y_max - y_min;
                        y = y_min;
                        line(x, y, height, width, speed, role, "Fill"); // down / right
                    }
                } else { // taller than wide
                    if (i % 2 == 0) {
                        x = x_max;
                        if (i == x_count) {
                            y += spacing_45 - x_rem;
                            x_rem = 0.;
                        } else
                            y += spacing_45;
                        move_to(x, y); // step up
                        x = x_min;
                        y += x_max - x_min;
                        line(x, y, height, width, speed, role, "Fill"); // up / left
                    } else {
                        x = x_min;
                        y += spacing_45;
                        move_to(x, y); // step up
                        x = x_max;
                        y -= x_max - x_min;
                        line(x, y, height, width, speed, role, "Fill"); // down / right
                    }
                }
            } else {
                if (i % 2 == 0) {
                    x = x_max;
                    y += i == x_count ? spacing_45 - x_rem : spacing_45;
                    move_to(x, y); // step up
                    x -= y_max - y;
                    y = y_max;
                    line(x, y, height, width, speed, role, "Fill"); // up / left
                } else {
                    x += i == y_count ? spacing_45 - y_rem : spacing_45;
                    y = y_max;
                    move_to(x, y); // step right
                    y -= x_max - x;
                    x = x_max;
                    line(x, y, height, width, speed, role, "Fill"); // down / right
                }
            }
        }
    }

    // Digits of segments, written upwards (the pattern is read from the side).
    void draw_number(double start_x, double start_y, const std::string &value, double height)
    {
        constexpr double seg = 2., dot = 0.75, glyph_spacing = 3.;
        const double speed = m_p.first_layer_speed * 60.;
        const GCodeExtrusionRole role = GCodeExtrusionRole::Perimeter;
        auto segments = [](char c) -> std::vector<const char*> {
            switch (c) {
            case '1': return { "bl", "right", "right" };
            case '2': return { "bl", "up", "right", "down", "right", "up" };
            case '3': return { "bl", "up", "right", "down", "mup", "right", "down" };
            case '4': return { "ul", "right", "right", "mleft", "down", "left" };
            case '5': return { "ul", "down", "right", "up", "right", "down" };
            case '6': return { "ul", "down", "right", "right", "up", "left", "down" };
            case '7': return { "bl", "up", "right", "right" };
            case '8': return { "bl", "right", "right", "up", "left", "left", "down", "mright", "up" };
            case '9': return { "br", "up", "left", "left", "down", "right", "up" };
            case '0': return { "bl", "right", "right", "up", "left", "left", "down" };
            case '.': return { "br", "dot" };
            default:  return {};
            }
        };
        double total = 0.;
        for (char c : value) {
            const std::string comment = std::string("Glyph: ") + c;
            for (const std::string s : segments(c)) {
                if (s == "bl")          move_to(start_x, start_y + total);
                else if (s == "br")     move_to(start_x + seg * 2., start_y + total);
                else if (s == "ul")     move_to(start_x, start_y + total + seg);
                else if (s == "up")     line(m_cur_x, m_cur_y + seg, height, m_line_width, speed, role, comment.c_str());
                else if (s == "down")   line(m_cur_x, m_cur_y - seg, height, m_line_width, speed, role, comment.c_str());
                else if (s == "right")  line(m_cur_x + seg, m_cur_y, height, m_line_width, speed, role, comment.c_str());
                else if (s == "left")   line(m_cur_x - seg, m_cur_y, height, m_line_width, speed, role, comment.c_str());
                else if (s == "mup")    move_to(m_cur_x, m_cur_y + seg);
                else if (s == "mright") move_to(m_cur_x + seg, m_cur_y);
                else if (s == "mleft")  move_to(m_cur_x - seg, m_cur_y);
                else if (s == "dot")    line(m_cur_x - dot, m_cur_y, height, m_line_width, speed, role, comment.c_str());
            }
            total += (c == '1' || c == '.') ? 1. : glyph_spacing;
        }
    }

    const PressureAdvancePatternParams &m_p;
    double m_nozzle, m_filament_area, m_ext_mult, m_layer_height, m_first_layer, m_travel_speed;
    double m_retract_dist, m_retract_speed, m_unretract_speed, m_zhop;
    bool   m_fw_retract;
    BoundingBoxf m_bed;
    Vec2d  m_center;
    double m_line_width, m_anchor_width, m_line_spacing, m_anchor_spacing, m_spacing_angle;
    int    m_patterns;

    std::string        out;
    double             m_cur_x { 0. }, m_cur_y { 0. }, m_cur_z { 0. };
    bool               m_retracted { false }, m_hopped { false };
    GCodeExtrusionRole m_role { GCodeExtrusionRole::None };
    double             m_width { 0. };
    BoundingBoxf       m_bbox;
    double             m_max_z { 0. };
    int                m_layer_count { 0 };
};

} // namespace

int pressure_advance_pattern_count(const PressureAdvancePatternParams &params)
{
    return params.pa_step > 0. ? int(std::lround((params.pa_end - params.pa_start) / params.pa_step + 1.)) : 0;
}

std::string pressure_advance_pattern_gcode(const DynamicPrintConfig &config, const PressureAdvancePatternParams &p)
{
    if (config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value != gcfKlipper)
        throw RuntimeError(_u8L("The pressure advance pattern is for Klipper printers (G-code flavor Klipper)."));
    const int patterns = pressure_advance_pattern_count(p);
    if (p.pa_start < 0. || p.pa_step <= 0. || p.pa_end <= p.pa_start || patterns < 2)
        throw RuntimeError(_u8L("Pressure advance: the end value must be above the start value and the step above 0."));
    if (patterns > 100)
        throw RuntimeError(format(_u8L("%1% patterns: use a larger step (at most 100 patterns)."), patterns));
    if (p.layers < 1 || p.wall_count < 1 || p.anchor_perimeters < 1 || p.side_length <= 0. || p.spacing < 0. ||
        p.corner_angle <= 0. || p.corner_angle >= 180. || p.speed <= 0. || p.first_layer_speed <= 0.)
        throw RuntimeError(_u8L("Invalid pattern values."));

    Generator   gen(config, p);
    std::string body = gen.body();

    // It has to fit the bed and stay out of the keep-out zones.
    const BoundingBoxf &bbox = gen.extrusion_bbox();
    const double        w    = gen.line_width() / 2.;
    const BoundingBoxf  bed  = gen.bed();
    if (bbox.min.x() - w < bed.min.x() || bbox.min.y() - w < bed.min.y() || bbox.max.x() + w > bed.max.x() || bbox.max.y() + w > bed.max.y())
        throw RuntimeError(format(_u8L("The pattern (%1% x %2% mm) doesn't fit the bed: fewer patterns, a shorter side or a "
                                       "smaller angle."), num(bbox.size().x(), 1), num(bbox.size().y(), 1)));
    for (const BoundingBoxf &zone : BedKeepOut::parse(config.opt_string("bed_keep_out_zones")))
        if (bbox.max.x() > zone.min.x() && bbox.min.x() < zone.max.x() && bbox.max.y() > zone.min.y() && bbox.min.y() < zone.max.y())
            throw RuntimeError(_u8L("The pattern crosses a bed keep-out zone: fewer patterns, a shorter side or another print direction."));

    // Custom G-codes, with the placeholders of a print.
    PlaceholderParser parser;
    parser.apply_config(config);
    parser.update_timestamp();
    parser.set("initial_tool", 0);
    parser.set("initial_extruder", 0);
    parser.set("current_extruder", 0);
    parser.set("current_object_idx", 0);
    parser.set("total_layer_count", gen.layer_count());
    parser.set("total_print_height", gen.max_z());
    parser.set("speed_mode", std::string(speed_mode_letter(config.opt<ConfigOptionStrings>("klipper_speed_mode")->get_at(0))));
    parser.set("has_wipe_tower", false);
    parser.set("has_single_extruder_multi_material_priming", false);
    parser.set("total_toolchanges", 0);
    parser.set("num_extruders", int(config.opt<ConfigOptionFloats>("nozzle_diameter")->size()));
    parser.set("is_extruder_used", new ConfigOptionBools(std::vector<unsigned char>(std::max<size_t>(255, config.opt<ConfigOptionFloats>("nozzle_diameter")->size()), 0)));
    parser.config_writable().option<ConfigOptionBools>("is_extruder_used")->values[0] = true;
    parser.set("print_bed_min",  new ConfigOptionFloats({ bed.min.x(), bed.min.y() }));
    parser.set("print_bed_max",  new ConfigOptionFloats({ bed.max.x(), bed.max.y() }));
    parser.set("print_bed_size", new ConfigOptionFloats({ bed.size().x(), bed.size().y() }));
    parser.set("first_layer_print_convex_hull", new ConfigOptionPoints({ bbox.min, Vec2d(bbox.max.x(), bbox.min.y()), bbox.max, Vec2d(bbox.min.x(), bbox.max.y()) }));
    parser.set("first_layer_print_min",  new ConfigOptionFloats({ bbox.min.x(), bbox.min.y() }));
    parser.set("first_layer_print_max",  new ConfigOptionFloats({ bbox.max.x(), bbox.max.y() }));
    parser.set("first_layer_print_size", new ConfigOptionFloats({ bbox.size().x(), bbox.size().y() }));
    parser.set("input_filename_base", "pa_pattern");
    parser.set("zhop", 0.);
    for (const char *key : { "print", "filament", "printer", "physical_printer" })
        if (const ConfigOption *opt = config.option(std::string(key) + "_settings_id"))
            parser.set(std::string(key) + "_preset", opt->clone());
    PlaceholderParser::ContextData context;
    context.global_config = std::make_unique<DynamicConfig>();
    auto process = [&parser, &context](const char *name, const std::string &templ, const DynamicConfig *override_config = nullptr) {
        try {
            std::string gcode = parser.process(templ, 0, override_config, &context);
            if (! gcode.empty() && gcode.back() != '\n')
                gcode += '\n';
            return gcode;
        } catch (const std::exception &ex) {
            throw RuntimeError(format(_u8L("Failed to process the %1%:\n%2%"), name, ex.what()));
        }
    };

    std::string out = "; " + header_slic3r_generated() + "\n;\n";
    out += "; Pressure advance calibration pattern, from Ellis' Pressure Advance / Linear Advance Calibration Tool\n"
           "; (https://github.com/AndrewEllis93/Pressure_Linear_Advance_Tool; original Marlin tool by Sineos)\n";
    out += "; Pressure advance: " + num(p.pa_start, 4) + " to " + num(p.pa_end, 4) + ", step " + num(p.pa_step, 4) + ", values:";
    for (int j = 0; j < patterns; ++ j)
        out += (j == 0 ? " " : ", ") + gen.pa_value_string(j);
    out += "\n; Print size: " + num(bbox.size().x(), 2) + " x " + num(bbox.size().y(), 2) + " mm\n;\n";

    // Temperatures and start G-codes as a print (GCode.cpp): temperatures emitted unless the start G-code sets them.
    const std::string start_gcode = process("start G-code", config.opt_string("start_gcode"));
    const bool autoemit   = config.opt_bool("autoemit_temperature_commands");
    const int  bed_temp   = config.opt<ConfigOptionInts>("first_layer_bed_temperature")->get_at(0);
    const int  nozzle_tmp = config.opt<ConfigOptionInts>("first_layer_temperature")->get_at(0);
    const bool emit_nozzle = autoemit && ! gcode_sets_temperature(start_gcode, 104, 109) && nozzle_tmp > 0;
    if (autoemit && ! gcode_sets_temperature(start_gcode, 140, 190) && bed_temp > 0)
        out += "M190 S" + std::to_string(bed_temp) + " ; set bed temperature and wait for it to be reached\n";
    if (emit_nozzle)
        out += "M104 S" + std::to_string(nozzle_tmp) + " ; set temperature\n";
    out += ";TYPE:" + gcode_extrusion_role_to_string(GCodeExtrusionRole::Custom) + "\n";
    out += start_gcode;
    if (emit_nozzle)
        out += "M109 S" + std::to_string(nozzle_tmp) + " ; set temperature and wait for it to be reached\n";
    {
        DynamicConfig filament;
        filament.set_key_value("filament_extruder_id", new ConfigOptionInt(0));
        out += process("filament start G-code", config.opt<ConfigOptionStrings>("start_filament_gcode")->get_at(0), &filament);
    }
    if (*speed_mode_letter(config.opt<ConfigOptionStrings>("klipper_speed_mode")->get_at(0)) != 0 && ! config.opt_string("klipper_speed_mode_gcode").empty())
        out += process("Klipper speed mode G-code", config.opt_string("klipper_speed_mode_gcode"));

    out += body;

    out += ";TYPE:" + gcode_extrusion_role_to_string(GCodeExtrusionRole::Custom) + "\n";
    {
        DynamicConfig end;
        end.set_key_value("layer_num",            new ConfigOptionInt(gen.layer_count() - 1));
        end.set_key_value("layer_z",              new ConfigOptionFloat(gen.max_z()));
        end.set_key_value("max_layer_z",          new ConfigOptionFloat(gen.max_z()));
        end.set_key_value("current_layer_height", new ConfigOptionFloat(p.layers > 1 ? config.opt_float("layer_height") : gen.first_layer()));
        end.set_key_value("filament_extruder_id", new ConfigOptionInt(0));
        out += process("filament end G-code", config.opt<ConfigOptionStrings>("end_filament_gcode")->get_at(0), &end);
        out += process("end G-code", config.opt_string("end_gcode"), &end);
    }

    // The settings, as a print, for the G-code viewer.
    out += "\n; prusaslicer_config = begin\n";
    for (const std::string &key : config.keys())
        if (key != "compatible_printers" && key != "compatible_prints" && ! boost::starts_with(key, "printhost_") && key != "print_host")
            out += "; " + key + " = " + config.opt_serialize(key) + "\n";
    out += "; prusaslicer_config = end\n";
    return out;
}

} // namespace Slic3r
