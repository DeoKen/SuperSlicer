// Adaptive pressure advance (doc section 46), ported from OrcaSlicer b5517b6 (AdaptivePAProcessor.cpp,
// AdaptivePAInterpolator.cpp, PchipInterpolatorHelper.cpp). See AdaptivePA.hpp.
#include "AdaptivePA.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace Slic3r {

// ---------------------------------------------------------------------------------------------------------------------------
// PCHIP, as OrcaSlicer's PchipInterpolatorHelper (end slopes = the slopes of the first / last interval).

PchipInterpolator::PchipInterpolator(std::vector<double> x, std::vector<double> y)
{
    std::vector<std::pair<double, double>> points;
    for (size_t i = 0; i < x.size() && i < y.size(); ++ i)
        points.emplace_back(x[i], y[i]);
    std::sort(points.begin(), points.end());
    // Points with the same x averaged.
    for (size_t i = 0; i < points.size();) {
        size_t j = i;
        double sum = 0.;
        for (; j < points.size() && points[j].first == points[i].first; ++ j)
            sum += points[j].second;
        m_x.emplace_back(points[i].first);
        m_y.emplace_back(sum / double(j - i));
        i = j;
    }
    if (m_x.size() < 2)
        return;
    const size_t n = m_x.size() - 1;
    m_h.resize(n);
    m_delta.resize(n);
    m_d.resize(n + 1);
    for (size_t i = 0; i < n; ++ i) {
        m_h[i]     = m_x[i + 1] - m_x[i];
        m_delta[i] = (m_y[i + 1] - m_y[i]) / m_h[i];
    }
    m_d[0] = m_delta[0];
    m_d[n] = m_delta[n - 1];
    for (size_t i = 1; i < n; ++ i) {
        if (m_delta[i - 1] * m_delta[i] > 0) {
            const double w1 = 2 * m_h[i] + m_h[i - 1];
            const double w2 = m_h[i] + 2 * m_h[i - 1];
            m_d[i] = (w1 + w2) / (w1 / m_delta[i - 1] + w2 / m_delta[i]);
        } else
            m_d[i] = 0;
    }
}

double PchipInterpolator::operator()(double xi) const
{
    if (m_x.empty())
        return 0.;
    if (xi <= m_x.front())
        return m_y.front();
    if (xi >= m_x.back())
        return m_y.back();
    const size_t i   = size_t(std::lower_bound(m_x.begin(), m_x.end(), xi) - m_x.begin()) - 1;
    const double h   = m_h[i];
    const double t   = (xi - m_x[i]) / h;
    const double t2  = t * t;
    const double t3  = t2 * t;
    const double h00 = 2 * t3 - 3 * t2 + 1;
    const double h10 = t3 - 2 * t2 + t;
    const double h01 = -2 * t3 + 3 * t2;
    const double h11 = t3 - t2;
    return h00 * m_y[i] + h10 * h * m_d[i] + h01 * m_y[i + 1] + h11 * h * m_d[i + 1];
}

// ---------------------------------------------------------------------------------------------------------------------------
// The model.

static std::string_view trim(std::string_view s)
{
    while (! s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
        s.remove_prefix(1);
    while (! s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

static bool parse_number(std::string_view s, double &out)
{
    s = trim(s);
    if (s.empty())
        return false;
    // std::from_chars does not take a leading '+'.
    if (s.front() == '+')
        s.remove_prefix(1);
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && ptr == s.data() + s.size();
}

std::string AdaptivePAInterpolator::parse(const std::string &model)
{
    m_flow_curves.clear();
    std::map<double, std::pair<std::vector<double>, std::vector<double>>> per_accel;
    std::istringstream ss(model);
    std::string        line;
    for (int line_number = 1; std::getline(ss, line); ++ line_number) {
        const std::string_view l = trim(line);
        if (l.empty() || l.front() == ';')
            continue;
        double values[3];
        size_t start = 0, n = 0;
        bool   ok    = true;
        for (; n < 3 && ok; ++ n) {
            size_t comma = l.find(',', start);
            if (n < 2 && comma == std::string_view::npos) {
                ok = false;
                break;
            }
            if (n == 2)
                comma = l.size();
            ok    = parse_number(l.substr(start, comma - start), values[n]);
            start = comma + 1;
        }
        if (! ok || n != 3)
            return "line " + std::to_string(line_number) + " \"" + std::string(l) + "\": 3 numbers expected (PA,flow,acceleration)";
        if (values[0] == 0. && values[1] == 0. && values[2] == 0.)
            continue;
        if (values[0] < 0. || values[1] <= 0. || values[2] <= 0.)
            return "line " + std::to_string(line_number) + " \"" + std::string(l) + "\": the flow and the acceleration must be above 0";
        auto &[flows, pas] = per_accel[values[2]];
        flows.emplace_back(values[1]);
        pas.emplace_back(values[0]);
    }
    for (auto &[accel, data] : per_accel) {
        PchipInterpolator curve(data.first, data.second);
        // OrcaSlicer ignores an acceleration with a single point.
        if (curve.size() >= 2)
            m_flow_curves.emplace(accel, std::move(curve));
    }
    if (m_flow_curves.empty() && ! per_accel.empty())
        return "no acceleration with at least 2 flows";
    return {};
}

double AdaptivePAInterpolator::operator()(double flow, double acceleration) const
{
    if (m_flow_curves.empty())
        return -1.;
    std::vector<double> accels, pas;
    for (const auto &[accel, curve] : m_flow_curves) {
        accels.emplace_back(accel);
        pas.emplace_back(curve(flow));
    }
    const double pa = accels.size() == 1 ? pas.front() : PchipInterpolator(accels, pas)(acceleration);
    return std::round(pa * 1000.) / 1000.;
}

// ---------------------------------------------------------------------------------------------------------------------------
// The G-code.

static constexpr std::string_view PA_CHANGE_TAG = "; PA_CHANGE:";

static bool starts_with(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

// The line without its comment.
static std::string_view code(std::string_view line) { return line.substr(0, line.find(';')); }

static bool is_move(std::string_view line, bool with_arcs)
{
    return line.size() >= 3 && line[0] == 'G' && line[2] == ' ' &&
           (line[1] == '1' || line[1] == '0' || (with_arcs && (line[1] == '2' || line[1] == '3')));
}

static bool has_word(std::string_view code, char letter) { return code.find(letter) != std::string_view::npos; }

static bool is_extrusion(std::string_view line)
{
    // OrcaSlicer: G1 with X, Y and E. PrusaSlicer leaves out an unchanged coordinate and writes arcs (G2 / G3).
    const std::string_view c = code(line);
    return is_move(line, true) && line[1] != '0' && has_word(c, 'E') && (has_word(c, 'X') || has_word(c, 'Y'));
}

static bool is_travel(std::string_view line)
{
    const std::string_view c = code(line);
    return is_move(line, false) && ! has_word(c, 'E') && (has_word(c, 'X') || has_word(c, 'Y'));
}

// The feedrate (mm/s) of a "G1 F..." line.
static double g1_f(std::string_view line)
{
    double f = 0.;
    const std::string_view c = code(line);
    std::string_view       v = c.substr(4);
    v = v.substr(0, v.find(' '));
    return parse_number(v, f) ? f / 60. : 0.;
}

// The feedrate (mm/s) a G0 / G1 / G2 / G3 line sets with its F word, or 0 when it has none (OrcaSlicer's feedrate_word()).
static double feedrate_word(std::string_view line)
{
    if (! is_move(line, true))
        return 0.;
    const std::string_view c   = code(line);
    const size_t           pos = c.find(" F");
    if (pos == std::string_view::npos)
        return 0.;
    std::string_view v = c.substr(pos + 2);
    v = v.substr(0, v.find(' '));
    double f = 0.;
    return parse_number(v, f) ? f / 60. : 0.;
}

static std::string format_number(double value, int decimals)
{
    char buf[64];
    const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value, std::chars_format::fixed, decimals);
    std::string s(buf, ec == std::errc() ? ptr : buf);
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0')
            s.pop_back();
        if (s.back() == '.')
            s.pop_back();
    }
    return s;
}

std::string AdaptivePAProcessor::tag(unsigned int extruder_id, double mm3_per_mm, unsigned int acceleration, bool bridge, bool role_change, bool overhang)
{
    return std::string(PA_CHANGE_TAG) + "T" + std::to_string(extruder_id) + " MM3MM:" + format_number(mm3_per_mm, 6) +
           " ACCEL:" + std::to_string(acceleration) + " BR:" + (bridge ? "1" : "0") + " RC:" + (role_change ? "1" : "0") +
           " OV:" + (overhang ? "1" : "0") + "\n";
}

std::string AdaptivePAProcessor::set_pressure_advance(double value)
{
    return "SET_PRESSURE_ADVANCE ADVANCE=" + format_number(value, 4) + "\n";
}

struct PAChangeTag {
    unsigned int extruder     { 0 };
    double       mm3_per_mm   { 0. };
    double       acceleration { 0. };
    bool         bridge       { false };
    bool         role_change  { false };
    bool         overhang     { false };
};

static bool parse_tag(std::string_view line, PAChangeTag &out)
{
    line.remove_prefix(PA_CHANGE_TAG.size());
    auto field = [line](std::string_view key, double &value) {
        const size_t pos = line.find(key);
        if (pos == std::string_view::npos)
            return false;
        std::string_view v = line.substr(pos + key.size());
        v = v.substr(0, v.find(' '));
        return parse_number(v, value);
    };
    double t, mm3, accel, br, rc, ov;
    if (! field("T", t) || ! field("MM3MM:", mm3) || ! field("ACCEL:", accel) || ! field("BR:", br) || ! field("RC:", rc) || ! field("OV:", ov))
        return false;
    out = { unsigned(t), mm3, accel, br != 0, rc != 0, ov != 0 };
    return true;
}

// The ADVANCE value of a SET_PRESSURE_ADVANCE line, -1 when it has none.
static double set_pa_value(std::string_view line)
{
    const std::string_view c   = code(line);
    const size_t           pos = c.find("ADVANCE=");
    if (pos == std::string_view::npos)
        return -1.;
    std::string_view v = c.substr(pos + 8);
    v = v.substr(0, v.find(' '));
    double value = -1.;
    return parse_number(v, value) ? value : -1.;
}

static std::string num(double v) { return format_number(v, 6); }

std::string AdaptivePAProcessor::process_layer(std::string &&gcode)
{
    std::vector<std::string_view> lines;
    for (size_t start = 0; start < gcode.size();) {
        size_t eol = gcode.find('\n', start);
        if (eol == std::string::npos)
            eol = gcode.size();
        lines.emplace_back(std::string_view(gcode).substr(start, eol - start));
        start = eol + 1;
    }

    std::string out;
    out.reserve(gcode.size() + gcode.size() / 64);
    bool wipe = false;
    for (size_t i = 0; i < lines.size(); ++ i) {
        const std::string_view line = lines[i];

        // As OrcaSlicer: the speed changes of wipes are ignored.
        if (line.find("WIPE_START") != std::string_view::npos)
            wipe = true;
        if (starts_with(line, "G1 F") && ! wipe)
            m_current_feedrate = g1_f(line);
        if (const double f = feedrate_word(line); f > 0)
            m_modal_feedrate = f;
        if (line.find("WIPE_END") != std::string_view::npos)
            wipe = false;

        if (! starts_with(line, PA_CHANGE_TAG)) {
            if (starts_with(line, "SET_PRESSURE_ADVANCE"))
                if (const double v = set_pa_value(line); v >= 0.)
                    m_last_pa = v;
            out.append(line.data(), line.size());
            out += '\n';
            continue;
        }

        PAChangeTag tag;
        if (! parse_tag(line, tag))
            continue;
        const bool extruder_changed = int(tag.extruder) != m_last_extruder;
        m_last_extruder = int(tag.extruder);

        // Look ahead for the speed of the entity up to the first travel after an extrusion (OrcaSlicer).
        double next_feedrate = 0., max_feedrate = 0., modal = m_modal_feedrate;
        bool   extrusion_found = false;
        for (size_t j = i + 1, counter = 1; j < lines.size(); ++ j, ++ counter) {
            const std::string_view next = lines[j];
            if (! extrusion_found)
                if (const double f = feedrate_word(next); f > 0)
                    modal = f;
            if (! extrusion_found && is_extrusion(next)) {
                extrusion_found = true;
                // No G1 F before the extrusion: the cooling buffer dropped the F the travel already set.
                if (max_feedrate == 0. && modal > 0.) {
                    max_feedrate       = modal;
                    next_feedrate      = modal;
                    m_current_feedrate = modal;
                }
                continue;
            }
            if (extrusion_found && is_travel(next))
                break;
            // A wipe is usually faster than the feature.
            if (next.find("WIPE") != std::string_view::npos)
                break;
            if (starts_with(next, PA_CHANGE_TAG) && next.find("RC:1") != std::string_view::npos)
                break;
            if (starts_with(next, "G1 F")) {
                const double f = g1_f(next);
                if (counter == 1)
                    m_current_feedrate = f;
                max_feedrate = std::max(max_feedrate, f);
                if (next_feedrate < 1e-6)
                    next_feedrate = f;
            }
        }
        if (max_feedrate <= 0.)
            max_feedrate = m_current_feedrate;

        const Extruder *extruder  = tag.extruder < m_extruders.size() ? &m_extruders[tag.extruder] : nullptr;
        const double    static_pa = extruder ? extruder->static_pa : 0.;
        double          speed     = 0.;
        double          pa        = -1.;
        if (extruder != nullptr && extruder->adaptive) {
            // An overhang perimeter: the lower of the current and the next speed (the cooling may slow it down more), else the
            // fastest speed of the entity.
            speed = tag.overhang ? ((m_current_feedrate == 0. || next_feedrate == 0.) ? std::max(m_current_feedrate, next_feedrate)
                                                                                      : std::min(m_current_feedrate, next_feedrate))
                                 : std::max(max_feedrate, m_current_feedrate);
            pa = extruder->model(tag.mm3_per_mm * speed, tag.acceleration);
        }
        // No value, or 0: never 0 in the middle of a print (Klipper flushes its move queue when the smoothing switches between 0
        // and a value), the fallback instead.
        if (pa < 0.0005)
            pa = static_pa;
        if (m_comments) {
            out.append(line.data(), line.size());
            out += "\n; APA speed " + num(speed) + " mm/s, flow " + num(tag.mm3_per_mm * speed) + " mm3/s, acceleration " +
                   num(tag.acceleration) + ", PA " + num(pa) + " (was " + num(m_last_pa) + ")\n";
        }
        if (pa > 0. && (extruder_changed || m_last_pa < 0. || std::abs(pa - m_last_pa) > 1e-6)) {
            out += set_pressure_advance(pa);
            m_last_pa = pa;
        }
    }
    return out;
}

} // namespace Slic3r
