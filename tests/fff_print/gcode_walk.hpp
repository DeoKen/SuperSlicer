#ifndef slic3r_tests_gcode_walk_hpp_
#define slic3r_tests_gcode_walk_hpp_

#include <cmath>
#include <string>
#include <vector>
#include <boost/algorithm/string/predicate.hpp>

#include "libslic3r/GCodeReader.hpp"

namespace Slic3r { namespace Test {

// State of the printer at one extruding move, collected by walk_gcode().
struct Extrusion
{
    int         layer; // 0 based, counted from ;LAYER_CHANGE
    std::string type;  // from ;TYPE:
    int         fan;   // percent
    int         accel; // mm/s^2
    double      F;     // mm/min
};

// Walk G-code and record the fan speed, acceleration and feed rate of every extruding move.
// redundant_fan_commands counts M106 / M107 that set the fan speed already set.
inline std::vector<Extrusion> walk_gcode(const std::string &gcode, int *redundant_fan_commands = nullptr)
{
    std::vector<Extrusion> out;
    int         layer = -1;
    std::string type;
    int         fan   = -1;
    int         accel = 0;
    int         redundant = 0;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        const std::string &raw = line.raw();
        if (boost::starts_with(raw, ";LAYER_CHANGE")) {
            ++ layer;
        } else if (boost::starts_with(raw, ";TYPE:")) {
            type = raw.substr(6);
        } else if (line.cmd_is("M106") || line.cmd_is("M107")) {
            float s = 0;
            if (line.cmd_is("M106"))
                line.has_value('S', s);
            const int new_fan = int(std::round(s * 100.f / 255.f));
            if (new_fan == fan)
                ++ redundant;
            fan = new_fan;
        } else if (line.cmd_is("M204")) {
            float s = 0;
            if (line.has_value('S', s) || line.has_value('P', s))
                accel = int(std::round(s));
        } else if ((line.cmd_is("G1") || line.cmd_is("G2") || line.cmd_is("G3")) && line.dist_E(self) > 0 && line.dist_XY(self) > 0) {
            out.push_back({ layer, type, std::max(fan, 0), accel, line.new_F(self) });
        }
    });
    if (redundant_fan_commands)
        *redundant_fan_commands = redundant;
    return out;
}

inline size_t count_type(const std::vector<Extrusion> &extrusions, const std::string &type)
{
    size_t n = 0;
    for (const Extrusion &e : extrusions)
        if (e.type == type)
            ++ n;
    return n;
}

} } // namespace Slic3r::Test

#endif // slic3r_tests_gcode_walk_hpp_
