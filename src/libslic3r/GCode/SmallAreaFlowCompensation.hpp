#ifndef slic3r_GCode_SmallAreaFlowCompensation_hpp_
#define slic3r_GCode_SmallAreaFlowCompensation_hpp_

#include <optional>
#include <string>
#include <vector>

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

    // Text form of the points, one "length,factor" pair per line.
    static std::string to_text(const std::vector<Point> &points);

private:
    std::vector<Point>  m_points;
    std::vector<double> m_slopes;   // PCHIP derivative at each point
};

} // namespace Slic3r

#endif // slic3r_GCode_SmallAreaFlowCompensation_hpp_
