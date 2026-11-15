#include "SmallAreaFlowCompensation.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/trim.hpp>

#include "LocalesUtils.hpp"
#include "libslic3r/I18N.hpp"
#include "libslic3r/format.hpp"

namespace Slic3r {

// Decimal point whatever the user's locale.
static bool parse_number(const std::string &s, double &out)
{
    if (s.empty())
        return false;
    size_t pos = 0;
    out = string_to_double_decimal_point(s, &pos);
    return pos == s.size() && std::isfinite(out);
}

std::optional<SmallAreaFlowModel> SmallAreaFlowModel::parse(const std::string &text, std::string *error)
{
    auto fail = [error](const std::string &msg) -> std::optional<SmallAreaFlowModel> {
        if (error)
            *error = msg;
        return std::nullopt;
    };

    SmallAreaFlowModel model;
    std::vector<std::string> lines;
    boost::split(lines, text, boost::is_any_of("\n;"));
    for (std::string line : lines) {
        boost::trim(line);
        if (line.empty())
            continue;
        const size_t comma = line.find(',');
        std::string length_str = line.substr(0, comma);
        std::string factor_str = comma == std::string::npos ? std::string() : line.substr(comma + 1);
        boost::trim(length_str);
        boost::trim(factor_str);
        Point pt;
        if (comma == std::string::npos || ! parse_number(length_str, pt.length) || ! parse_number(factor_str, pt.factor))
            // TRN %1% is a line of the small area flow compensation model that could not be parsed.
            return fail(format(_u8L("Small area flow compensation: can't read the point \"%1%\" (expected \"length, factor\")."), line));
        model.m_points.emplace_back(pt);
    }

    const std::vector<Point> &p = model.m_points;
    if (p.size() < 2)
        return fail(_u8L("Small area flow compensation: the model needs at least 2 points."));
    if (std::abs(p.front().length) > 1e-9)
        return fail(_u8L("Small area flow compensation: the first length must be 0."));
    for (size_t i = 1; i < p.size(); ++ i) {
        if (p[i].length <= p[i - 1].length)
            return fail(_u8L("Small area flow compensation: the lengths must increase from one point to the next."));
        if (p[i].factor <= p[i - 1].factor)
            return fail(_u8L("Small area flow compensation: the flow factors must increase from one point to the next."));
    }
    if (p.front().factor < 0.)
        return fail(_u8L("Small area flow compensation: the flow factors can't be negative."));
    if (std::abs(p.back().factor - 1.) > 1e-9)
        return fail(_u8L("Small area flow compensation: the last flow factor must be 1."));

    // PCHIP derivatives, as OrcaSlicer's PchipInterpolatorHelper::computePCHIP(): end points take the slope of their
    // segment, interior points the weighted harmonic mean of the neighbouring slopes (0 at a local extreme).
    const size_t n = p.size() - 1;
    std::vector<double> h(n), delta(n);
    for (size_t i = 0; i < n; ++ i) {
        h[i]     = p[i + 1].length - p[i].length;
        delta[i] = (p[i + 1].factor - p[i].factor) / h[i];
    }
    model.m_slopes.assign(n + 1, 0.);
    model.m_slopes.front() = delta.front();
    model.m_slopes.back()  = delta.back();
    for (size_t i = 1; i < n; ++ i)
        if (delta[i - 1] * delta[i] > 0) {
            const double w1 = 2 * h[i] + h[i - 1];
            const double w2 = h[i] + 2 * h[i - 1];
            model.m_slopes[i] = (w1 + w2) / (w1 / delta[i - 1] + w2 / delta[i]);
        }
    return model;
}

double SmallAreaFlowModel::factor(double length) const
{
    if (m_points.size() < 2 || length >= m_points.back().length)
        return 1.;
    if (length <= m_points.front().length)
        return m_points.front().factor;
    const auto it = std::upper_bound(m_points.begin(), m_points.end(), length,
        [](double l, const Point &pt) { return l < pt.length; });
    const size_t i  = size_t(it - m_points.begin()) - 1;
    const double hi = m_points[i + 1].length - m_points[i].length;
    const double t  = (length - m_points[i].length) / hi;
    const double t2 = t * t;
    const double t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * m_points[i].factor + (t3 - 2 * t2 + t) * hi * m_slopes[i]
         + (-2 * t3 + 3 * t2) * m_points[i + 1].factor + (t3 - t2) * hi * m_slopes[i + 1];
}

std::string SmallAreaFlowModel::to_text(const std::vector<Point> &points)
{
    std::string out;
    for (size_t i = 0; i < points.size(); ++ i) {
        if (i > 0)
            out += "\n";
        out += float_to_string_decimal_point(points[i].length) + "," + float_to_string_decimal_point(points[i].factor);
    }
    return out;
}

std::string SmallAreaFlowModel::text_for_c(double c)
{
    std::string out = "0,0";
    for (double l : { 0.2, 0.4, 0.6, 0.8, 1., 1.5, 2., 3., 5., 10., 20., 30. })
        out += "\n" + float_to_string_decimal_point(l) + "," + float_to_string_decimal_point(l / (l + c), 4);
    return out + "\n50,1";
}

std::optional<double> SmallAreaFlowModel::c_of_text(const std::string &text)
{
    const std::optional<SmallAreaFlowModel> model = parse(text);
    if (! model)
        return std::nullopt;
    const std::vector<Point> &p = model->points();
    // Decimals written for each factor (as parse() splits the text), so a curve written to 2 decimals is compared to 2.
    std::vector<int> decimals;
    {
        std::vector<std::string> lines;
        boost::split(lines, text, boost::is_any_of("\n;"));
        for (std::string line : lines) {
            boost::trim(line);
            if (line.empty())
                continue;
            std::string factor_str = line.substr(line.find(',') + 1);
            boost::trim(factor_str);
            const size_t dot = factor_str.find('.');
            decimals.emplace_back(dot == std::string::npos ? 0 : int(factor_str.size() - dot - 1));
        }
    }
    if (decimals.size() != p.size() || p.size() < 3 || p.front().factor != 0.)
        return std::nullopt;
    // c from each point in between, the median as the estimate.
    std::vector<double> cs;
    for (size_t i = 1; i + 1 < p.size(); ++ i) {
        if (p[i].factor <= 0.)
            return std::nullopt;
        cs.emplace_back(p[i].length * (1. - p[i].factor) / p[i].factor);
    }
    std::sort(cs.begin(), cs.end());
    const double estimate = cs[cs.size() / 2];
    for (double step : { 0.01, 0.001, 0.0001 }) {
        const double c = std::round(estimate / step) * step;
        if (c < c_min - 1e-9 || c > c_max + 1e-9)
            continue;
        bool fits = true;
        for (size_t i = 1; fits && i + 1 < p.size(); ++ i)
            fits = std::abs(p[i].factor - p[i].length / (p[i].length + c)) <= 0.5 * std::pow(10., - decimals[i]) + 1e-9;
        if (fits)
            return c;
    }
    return std::nullopt;
}

std::vector<double> SmallAreaFlowModel::line_factors(const std::vector<Vec2d> &pts, const std::vector<double> &seg_len, const double width) const
{
    const size_t n = seg_len.size();
    assert(pts.size() == n + 1);
    std::vector<double> factors(n, 1.);
    if (n == 0)
        return factors;
    // Lines: [first segment, last segment], split where the direction turns by more than 60 degrees.
    std::vector<std::pair<size_t, size_t>> lines;
    std::vector<double>                    line_len;
    const double cos_sharp = 0.5;
    Vec2d  last_dir = Vec2d::Zero();
    size_t start    = 0;
    double len      = 0.;
    for (size_t i = 0; i < n; ++ i) {
        const Vec2d  d  = pts[i + 1] - pts[i];
        const double dn = d.norm();
        if (dn > 1e-9) {
            const Vec2d dir = d / dn;
            if (last_dir != Vec2d::Zero() && last_dir.dot(dir) < cos_sharp) {
                lines.emplace_back(start, i - 1);
                line_len.emplace_back(len);
                start = i;
                len   = 0.;
            }
            last_dir = dir;
        }
        len += seg_len[i];
    }
    lines.emplace_back(start, n - 1);
    line_len.emplace_back(len);

    std::vector<double> line_factor(lines.size());
    for (size_t l = 0; l < lines.size(); ++ l)
        line_factor[l] = this->factor(line_len[l]);
    // Connections: shorter than 3 widths and than the lines next to them -> mean factor of those lines.
    const double link_max = 3. * width;
    if (lines.size() > 1)
        for (size_t l = 0; l < lines.size(); ++ l) {
            if (line_len[l] >= link_max)
                continue;
            const bool has_prev = l > 0, has_next = l + 1 < lines.size();
            if ((has_prev && line_len[l - 1] <= line_len[l]) || (has_next && line_len[l + 1] <= line_len[l]))
                continue;
            const double f_prev = has_prev ? this->factor(line_len[l - 1]) : 0.;
            const double f_next = has_next ? this->factor(line_len[l + 1]) : 0.;
            line_factor[l] = has_prev && has_next ? 0.5 * (f_prev + f_next) : has_prev ? f_prev : f_next;
        }
    for (size_t l = 0; l < lines.size(); ++ l)
        for (size_t i = lines[l].first; i <= lines[l].second; ++ i)
            factors[i] = line_factor[l];
    return factors;
}

} // namespace Slic3r
