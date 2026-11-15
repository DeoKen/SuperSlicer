#ifndef slic3r_GCode_SmallAreaFlowCompensation_hpp_
#define slic3r_GCode_SmallAreaFlowCompensation_hpp_

#include <optional>
#include <string>
#include <vector>

#include "libslic3r/Point.hpp"

namespace Slic3r {

// Small area flow compensation (SuperSlicer / OrcaSlicer, after Alexander Moss' Small-Area-Flow-Comp script):
// the extrusion of a short solid infill segment is multiplied by a factor read from a curve of
// (segment length in mm, flow factor) points.
class SmallAreaFlowModel
{
public:
    struct Point { double length; double factor; };

    // Parses the small_area_infill_flow_compensation_model text: one "length, factor" pair per line
    // (a trailing ';' is allowed, empty lines are ignored). Returns nothing and sets error if the text
    // can't be parsed or breaks a rule (as OrcaSlicer): at least 2 points, the first length is 0,
    // lengths and factors strictly increase, factors are not negative, the last factor is 1.
    static std::optional<SmallAreaFlowModel> parse(const std::string &text, std::string *error = nullptr);

    // Flow factor for a segment of this length (mm): PCHIP interpolation of the points as in OrcaSlicer,
    // 1 at and beyond the last point.
    double factor(double length) const;

    const std::vector<Point>& points() const { return m_points; }

    // Flow factor per segment of a path, by LINE length instead of segment length: the path (points in mm) is split
    // into lines where it turns by more than 60 degrees, so a curved line made of many short segments counts as one
    // line, and each segment gets the factor of its line's length. A short connection between two lines (shorter than
    // 3 x width and than the lines next to it, e.g. the link along the wall between two zig-zag lines) gets the mean
    // factor of its neighbours. seg_len[i] is the length of segment pts[i] -> pts[i + 1] (an arc's length for arcs).
    std::vector<double> line_factors(const std::vector<Vec2d> &pts, const std::vector<double> &seg_len, double width) const;

    // Text form of the points, one "length,factor" pair per line.
    static std::string to_text(const std::vector<Point> &points);

    // The one-number model factor = L / (L + c) (c in mm: the excess of material per line, as a length of line).
    // Range of c accepted by the GUI field.
    static constexpr double c_min = 0.01;
    static constexpr double c_max = 10.;
    // Model text for c: 0,0, then L / (L + c) to 4 decimals at 0.2 0.4 0.6 0.8 1 1.5 2 3 5 10 20 30 mm, then 50,1.
    static std::string text_for_c(double c);
    // The c of a model text that follows the formula: every point between the first and the last one equals
    // L / (L + c) to the decimals written in the text, the first point is 0,0 (the last one is the cut-off to 1 and may
    // be anywhere). c is rounded to 2, 3 or 4 decimals (the coarsest that fits). Nothing if the text is not a valid
    // model, has no point in between, or any point drifts from the formula.
    static std::optional<double> c_of_text(const std::string &text);

private:
    std::vector<Point>  m_points;
    std::vector<double> m_slopes;   // PCHIP derivative at each point
};

} // namespace Slic3r

#endif // slic3r_GCode_SmallAreaFlowCompensation_hpp_
