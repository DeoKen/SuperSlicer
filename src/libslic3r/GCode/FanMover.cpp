// Ported from SuperSlicer (src/libslic3r/GCode/FanMover.cpp), without the fan kickstart and the per tool fan offsets.
#include "FanMover.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>

#include <fast_float.h>


namespace Slic3r {

// Value of the axis word (for example " X12.5") in a G-code line, NAN if not found. Locale independent.
static float get_axis_value(const std::string &line, char axis)
{
    const char match[3] = { ' ', axis, 0 };
    const size_t found = line.find(match);
    if (found == std::string::npos)
        return NAN;
    const char *begin = line.data() + found + 2;
    float v = 0;
    const auto res = fast_float::from_chars(begin, line.data() + line.size(), v);
    return res.ec == std::errc() && res.ptr != begin ? v : NAN;
}

// Number without trailing zeros, with at most decimal_digits decimals. Locale independent.
static std::string to_string_nozero(float value, int decimal_digits)
{
    std::array<char, 64> buf;
    const auto res = std::to_chars(buf.data(), buf.data() + buf.size(), value, std::chars_format::fixed, decimal_digits);
    std::string out(buf.data(), res.ptr);
    if (out.find('.') != std::string::npos) {
        while (! out.empty() && out.back() == '0')
            out.pop_back();
        if (! out.empty() && out.back() == '.')
            out.pop_back();
    }
    if (out == "-0")
        out = "0";
    return out;
}

// Replace the value of the axis word in a G-code line.
static void change_axis_value(std::string &line, char axis, const float new_value, const int decimal_digits)
{
    const char match[3] = { ' ', axis, 0 };
    const size_t pos = line.find(match) + 2;
    const size_t end = std::min(line.find(' ', pos + 1), line.find(';', pos + 1));
    line = line.replace(pos, end - pos, to_string_nozero(new_value, decimal_digits));
}

// Fan speed (0-255 scale, as written) set by the line, -1 if the line doesn't set the fan.
static float get_fan_speed_raw(const std::string &line, GCodeFlavor flavor)
{
    // M106 / M107 with a fan index other than 0 (P word, except for Mach3 / Machinekit where P is the speed) is another
    // fan, for example the auxiliary fan (M106 P2): not the part cooling fan.
    if (flavor != gcfMach3 && flavor != gcfMachinekit && (line.compare(0, 4, "M106") == 0 || line.compare(0, 4, "M107") == 0))
        if (const float index = get_axis_value(line, 'P'); ! std::isnan(index) && index != 0.f)
            return -1;
    if (line.compare(0, 4, "M106") == 0)
        return get_axis_value(line, (flavor == gcfMach3 || flavor == gcfMachinekit) ? 'P' : 'S');
    if (line.compare(0, 4, "M127") == 0 || line.compare(0, 4, "M107") == 0)
        return 0;
    if ((flavor == gcfMakerWare || flavor == gcfSailfish) && line.compare(0, 4, "M126") == 0)
        return get_axis_value(line, 'T');
    return -1;
}

// Fan speed set by the line in hundredths of the written value (M106 S89.25 -> 8925), -1 if the line doesn't set the fan.
// Fork: SuperSlicer compared whole percents (int(S) * 100 / 255), so two neighbouring percents (PrusaSlicer writes
// M106 S102 for 40 % and S104.55 for 41 %) could compare equal and the second one was dropped as a duplicate.
static int16_t get_fan_speed(const std::string &line, GCodeFlavor flavor)
{
    const float raw = get_fan_speed_raw(line, flavor);
    return std::isnan(raw) || raw < 0.f ? int16_t(-1) : int16_t(std::lround(std::min(raw, 300.f) * 100.f));
}

static bool is_G1(const std::string &raw) { return raw.size() > 2 && raw[0] == 'G' && raw[1] == '1' && raw[2] == ' '; }

FanMover::BufferData& FanMover::put_in_buffer(BufferData &&data)
{
    assert(data.time >= 0 && data.time < 1000000 && ! std::isnan(data.time));
    m_buffer_time_size += data.time;
    if (data.fan_speed >= 0 && ! m_buffer.empty() && m_buffer.back().fan_speed >= 0)
        // Two fan commands in a row: the last one wins.
        m_buffer.back() = std::move(data);
    else
        m_buffer.emplace_back(std::move(data));
    return m_buffer.back();
}

std::list<FanMover::BufferData>::iterator FanMover::remove_from_buffer(std::list<BufferData>::iterator data)
{
    assert(data->time >= 0 && data->time < 1000000 && ! std::isnan(data->time));
    m_buffer_time_size -= data->time;
    return m_buffer.erase(data);
}

const std::string& FanMover::process_gcode(const std::string &gcode, bool flush)
{
    m_process_output.clear();

    // Recompute the buffer time to recover from rounding.
    m_buffer_time_size = 0;
    for (const BufferData &data : m_buffer)
        m_buffer_time_size += data.time;

    if (! gcode.empty())
        m_parser.parse_buffer(gcode, [this](GCodeReader &reader, const GCodeReader::GCodeLine &line) { this->process_gcode_line(reader, line); });

    if (flush)
        while (! m_buffer.empty())
            this->write_buffer_data();

    return m_process_output;
}

void FanMover::print_in_middle_G1(BufferData &line_to_split, float nb_sec_from_item_start, const std::string &line_to_write)
{
    const std::string eol = line_to_write.back() == '\n' ? "" : "\n";
    if (nb_sec_from_item_start > line_to_split.time * 0.9 && line_to_split.time < m_nb_seconds_delay / 4) {
        // Doesn't really need to be split, print it after.
        m_process_output += line_to_split.raw + "\n";
        m_process_output += line_to_write + eol;
    } else if (nb_sec_from_item_start < line_to_split.time * 0.1 && line_to_split.time < m_nb_seconds_delay / 4) {
        // Doesn't really need to be split, print it before (also when line_to_split.time == 0).
        m_process_output += line_to_write + eol;
        m_process_output += line_to_split.raw + "\n";
    } else if (is_G1(line_to_split.raw) && ! line_to_split.in_custom) {
        const float percent = nb_sec_from_item_start / line_to_split.time;
        std::string before = line_to_split.raw;
        std::string &after = line_to_split.raw;
        if (line_to_split.dx != 0)
            change_axis_value(before, 'X', line_to_split.x + line_to_split.dx * percent, 3);
        if (line_to_split.dy != 0)
            change_axis_value(before, 'Y', line_to_split.y + line_to_split.dy * percent, 3);
        if (line_to_split.dz != 0)
            change_axis_value(before, 'Z', line_to_split.z + line_to_split.dz * percent, 3);
        if (line_to_split.de != 0) {
            if (m_relative_e) {
                change_axis_value(before, 'E', line_to_split.de * percent, 5);
                change_axis_value(after, 'E', line_to_split.de * (1 - percent), 5);
            } else {
                change_axis_value(before, 'E', line_to_split.e + line_to_split.de * percent, 5);
            }
        }
        m_process_output += before + "\n";
        m_process_output += line_to_write + eol;
        m_process_output += line_to_split.raw + "\n";
    } else {
        // Not a G1, print it before.
        m_process_output += line_to_write + eol;
        m_process_output += line_to_split.raw + "\n";
    }
}

void FanMover::remove_slow_fan(int16_t min_speed, float past_sec)
{
    // Erase the fan commands of the buffer that are lower: don't slow down while speeding up.
    // Start at the oldest side, as long as past_sec is not used up.
    auto it = m_buffer.begin();
    while (it != m_buffer.end() && past_sec > 0) {
        past_sec -= it->time;
        if (it->fan_speed >= 0 && it->fan_speed < min_speed)
            it = this->remove_from_buffer(it);
        else
            ++ it;
    }
}

// Fan speed in effect just before data: the last fan command before it in the buffer, else the last one written out.
int16_t FanMover::fan_speed_before(std::list<BufferData>::iterator data) const
{
    while (data != m_buffer.begin()) {
        -- data;
        if (data->fan_speed >= 0)
            return data->fan_speed;
    }
    return int16_t(m_front_buffer_fan_speed);
}

void FanMover::speed_up_in_buffer(const std::string &line, const int16_t fan_speed, const float delay)
{
    // The buffer item containing the insertion point, delay seconds before the back of the buffer
    // (end() for the back), and the start time of that item from the front of the buffer.
    // Items without time (fan commands among them) are never the insertion item, so it is not erased below.
    const double insert_time = std::max(0., m_buffer_time_size - delay);
    double start = 0;
    auto it = delay > 0 ? m_buffer.begin() : m_buffer.end();
    while (it != m_buffer.end() && (it->time <= 0 || start + it->time <= insert_time)) {
        start += it->time;
        ++ it;
    }
    // Don't slow down while speeding up: erase the slower fan commands after the insertion point.
    for (auto next = it; next != m_buffer.end();)
        next = next->fan_speed >= 0 && next->fan_speed < fan_speed ? this->remove_from_buffer(next) : std::next(next);
    // Erase the slow downs that would last less than m_slowdown_min_time before the insertion point: the fan stays up
    // across the dip. A speed up from lower (not a dip) or a speed at least as high ends the search.
    double elapsed = it == m_buffer.end() ? 0. : insert_time - start;
    for (auto at = it; at != m_buffer.begin() && elapsed < m_slowdown_min_time;) {
        auto prev = std::prev(at);
        if (prev->fan_speed >= 0) {
            if (prev->fan_speed >= fan_speed || prev->fan_speed >= this->fan_speed_before(prev))
                break;
            this->remove_from_buffer(prev);
            continue;
        }
        elapsed += prev->time;
        at = prev;
    }
    // Already at this speed (the dip was erased): nothing to write.
    if (this->fan_speed_before(it) == fan_speed)
        return;

    BufferData fan_data(line, 0, fan_speed);
    if (it == m_buffer.end()) {
        m_buffer.emplace_back(std::move(fan_data));
        return;
    }
    // As print_in_middle_G1(): before or after a short item near its ends, else split a G1.
    const float offset = float(insert_time - start);
    if (offset > it->time * 0.9 && it->time < delay / 4) {
        m_buffer.insert(std::next(it), std::move(fan_data));
    } else if (offset <= 0 || (offset < it->time * 0.1 && it->time < delay / 4) || ! is_G1(it->raw) || it->in_custom) {
        m_buffer.insert(it, std::move(fan_data));
    } else {
        const float percent = offset / it->time;
        BufferData before = *it;
        BufferData &after = *it;
        before.time = it->time * percent;
        after.time  = it->time - before.time;
        if (after.dx != 0) {
            change_axis_value(before.raw, 'X', after.x + after.dx * percent, 3);
            after.x  += after.dx * percent;
            after.dx *= 1 - percent;
            before.dx = after.x - before.x;
        }
        if (after.dy != 0) {
            change_axis_value(before.raw, 'Y', after.y + after.dy * percent, 3);
            after.y  += after.dy * percent;
            after.dy *= 1 - percent;
            before.dy = after.y - before.y;
        }
        if (after.dz != 0) {
            change_axis_value(before.raw, 'Z', after.z + after.dz * percent, 3);
            after.z  += after.dz * percent;
            after.dz *= 1 - percent;
            before.dz = after.z - before.z;
        }
        if (after.de != 0) {
            if (m_relative_e) {
                change_axis_value(before.raw, 'E', after.de * percent, 5);
                change_axis_value(after.raw, 'E', after.de * (1 - percent), 5);
            } else {
                change_axis_value(before.raw, 'E', after.e + after.de * percent, 5);
                after.e += after.de * percent;
            }
            before.de = after.de * percent;
            after.de *= 1 - percent;
        }
        m_buffer.insert(it, std::move(before));
        m_buffer.insert(it, std::move(fan_data));
    }
}

void FanMover::process_gcode_line(GCodeReader &reader, const GCodeReader::GCodeLine &line)
{
    bool    need_flush = false;
    const std::string cmd(line.cmd());
    double  time      = 0;
    int16_t fan_speed = -1;
    if (cmd.length() > 1) {
        if (::toupper(cmd[0]) == 'G' && line.has(F) && line.f() > 0)
            m_current_speed = line.f() / 60.0f;
        switch (::toupper(cmd[0])) {
        case 'G': {
            const int g = ::atoi(&cmd[1]);
            if (g == 0 || g == 1 || g == 2 || g == 3) {
                const double distx = line.dist_X(reader);
                const double disty = line.dist_Y(reader);
                // Arcs: the chord length is used (as SuperSlicer does).
                const double distz = g <= 1 ? line.dist_Z(reader) : 0.;
                const double dist = std::sqrt(distx * distx + disty * disty + distz * distz);
                if (dist > 0)
                    time = dist / m_current_speed;
            }
            break;
        }
        case 'M': {
            fan_speed = get_fan_speed(line.raw(), m_flavor);
            if (fan_speed >= 0) {
                if (! m_is_custom_gcode) {
                    const bool move_it = m_nb_seconds_delay > 0 &&
                        (! m_only_overhangs || m_current_role == GCodeExtrusionRole::OverhangPerimeter);
                    if (m_back_buffer_fan_speed < fan_speed && m_slowdown_min_time > 0) {
                        // Fork (fan_slowdown_min_time): the speed up is inserted into the buffer, so that a short slow
                        // down before it can still be erased.
                        time = -1;
                        this->speed_up_in_buffer(line.raw(), fan_speed, move_it ? m_nb_seconds_delay : 0.f);
                    } else if (m_back_buffer_fan_speed < fan_speed && move_it) {
                        // This speed up goes back in time: don't put it in the buffer.
                        time = -1;
                        // First erase everything slower than it,
                        this->remove_slow_fan(fan_speed, float(m_buffer_time_size) + 1);
                        // then write the fan command in the past.
                        if (m_front_buffer_fan_speed == fan_speed &&
                            std::none_of(m_buffer.begin(), m_buffer.end(), [](const BufferData &data) { return data.fan_speed >= 0; })) {
                            // Fork: the slow down in between was erased, so the fan is already at this speed: don't write
                            // the same command again (SuperSlicer wrote it once more for every erased slow down).
                        } else if (! m_buffer.empty() && (m_buffer_time_size - m_buffer.front().time * 0.1) > m_nb_seconds_delay) {
                            this->print_in_middle_G1(m_buffer.front(), float(m_buffer_time_size - m_nb_seconds_delay), line.raw());
                            this->remove_from_buffer(m_buffer.begin());
                        } else {
                            m_process_output += line.raw() + "\n";
                        }
                        m_front_buffer_fan_speed = fan_speed;
                    }
                    m_back_buffer_fan_speed = fan_speed;
                } else {
                    // Flush the buffer, so that no fan command of the custom G-code is erased.
                    need_flush = true;
                }
            }
            break;
        }
        }
    } else if (! line.raw().empty() && line.raw().front() == ';') {
        if (line.raw().size() > 10 && line.raw().rfind(";TYPE:", 0) == 0)
            // Type of the next extrusions.
            m_current_role = string_to_gcode_extrusion_role(line.raw().substr(6));
        if (line.raw().rfind("; custom gcode end", 0) == 0)
            m_is_custom_gcode = false;
        else if (line.raw().rfind("; custom gcode", 0) == 0)
            m_is_custom_gcode = true;
    }

    if (time >= 0) {
        BufferData &new_data = this->put_in_buffer(BufferData(line.raw(), float(time), fan_speed));
        new_data.in_custom = m_is_custom_gcode;
        if (line.has(X)) {
            new_data.x  = reader.x();
            new_data.dx = line.dist_X(reader);
        }
        if (line.has(Y)) {
            new_data.y  = reader.y();
            new_data.dy = line.dist_Y(reader);
        }
        if (line.has(Z)) {
            new_data.z  = reader.z();
            new_data.dz = line.dist_Z(reader);
        }
        if (line.has(E)) {
            if (m_relative_e) {
                // The G-code reader doesn't know the extrusion is relative.
                new_data.e  = 0;
                new_data.de = line.e();
            } else {
                new_data.e  = reader.e();
                new_data.de = line.dist_E(reader);
            }
        }
        // Write out what is older than the delay. EPSILON keeps a buffer even with a 0 time, so that multiple
        // consecutive fan commands can be culled.
        while (! m_buffer.empty() && (need_flush || m_buffer_time_size - m_buffer.front().time > m_nb_seconds_delay + m_slowdown_min_time + EPSILON))
            this->write_buffer_data();
    }
}

void FanMover::write_buffer_data()
{
    const BufferData &front = m_buffer.front();
    if (front.fan_speed < 0 || front.fan_speed != m_front_buffer_fan_speed) {
        m_process_output += front.raw + "\n";
        if (front.fan_speed >= 0)
            m_front_buffer_fan_speed = front.fan_speed;
    }
    this->remove_from_buffer(m_buffer.begin());
}

} // namespace Slic3r
