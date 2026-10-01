#ifndef slic3r_GCode_FanMover_hpp_
#define slic3r_GCode_FanMover_hpp_

#include <cstdint>
#include <list>
#include <string>
#include <string_view>

#include "../GCodeReader.hpp"
#include "../ExtrusionRole.hpp"
#include "../PrintConfig.hpp"

namespace Slic3r {

// Fan mover, ported from SuperSlicer (fan_speedup_time): a fan speed increase is moved back in time by the given delay,
// so that the fan has already spun up when the feature that needs it starts. Slowing the fan down is never moved.
// The time is estimated from the feed rates of the G0/G1/G2/G3 moves (infinite acceleration), and the G-code is
// processed one layer at a time, so a fan command is never moved into the previous layer. Fan commands inside custom
// G-code (between "; custom gcode:" and "; custom gcode end:" tags) are not moved and act as a barrier.
// SuperSlicer's fan kickstart is not ported: Klipper's [fan] kick_start_time does it.
class FanMover
{
public:
    FanMover(const GCodeFlavor flavor, const float nb_seconds_delay, const bool relative_e, const bool only_overhangs)
        : m_flavor(flavor), m_nb_seconds_delay(nb_seconds_delay > 0 ? std::max(0.01f, nb_seconds_delay) : 0),
          m_relative_e(relative_e), m_only_overhangs(only_overhangs)
    {}

    // Process the G-code of a layer and return it with the fan speed increases moved back in time.
    // With flush, everything buffered is written out.
    const std::string& process_gcode(const std::string &gcode, bool flush);

private:
    struct BufferData
    {
        // Raw line, contains the end position.
        std::string raw;
        // Time to go from the start to the end of the move.
        float       time;
        // Fan speed in percent set by this line, -1 if it doesn't set the fan.
        int16_t     fan_speed;
        // Start position and delta to the end position.
        float x = 0, y = 0, z = 0, e = 0;
        float dx = 0, dy = 0, dz = 0, de = 0;

        BufferData(std::string line, float time = 0, int16_t fan_speed = -1) : raw(std::move(line)), time(time), fan_speed(fan_speed)
        {
            // Avoid a double end of line.
            if (! raw.empty() && raw.back() == '\n')
                raw.pop_back();
        }
    };

    BufferData& put_in_buffer(BufferData &&data);
    std::list<BufferData>::iterator remove_from_buffer(std::list<BufferData>::iterator data);
    void process_gcode_line(GCodeReader &reader, const GCodeReader::GCodeLine &line);
    // Print line_to_split into the output, with line_to_write before, inside (splitting a G1) or after it.
    void print_in_middle_G1(BufferData &line_to_split, float nb_sec_from_item_start, const std::string &line_to_write);
    // Remove the fan commands lower than min_speed from the last past_sec of the buffer.
    void remove_slow_fan(int16_t min_speed, float past_sec);
    void write_buffer_data();

    const GCodeFlavor m_flavor;
    const float       m_nb_seconds_delay;
    const bool        m_relative_e;
    const bool        m_only_overhangs;

    GCodeReader        m_parser;
    // Role of the extrusions at the back of the buffer.
    GCodeExtrusionRole m_current_role = GCodeExtrusionRole::Custom;
    // Current feed rate, in unit / second.
    double             m_current_speed = 1000. / 60.;
    bool               m_is_custom_gcode = false;

    // Fan speed at the front (already written) and at the back (last parsed) of the buffer.
    int                m_front_buffer_fan_speed = 1;
    int                m_back_buffer_fan_speed  = 1;

    std::list<BufferData> m_buffer;
    double                m_buffer_time_size = 0;

    std::string m_process_output;
};

} // namespace Slic3r

#endif // slic3r_GCode_FanMover_hpp_
