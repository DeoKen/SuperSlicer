// Adaptive pressure advance (doc section 46), ported from OrcaSlicer (AdaptivePAProcessor, AdaptivePAInterpolator,
// PchipInterpolatorHelper; OrcaSlicer b5517b6, AGPL-3.0 as PrusaSlicer). Klipper only.
//
// The G-code generator writes a "; PA_CHANGE:" tag before every extrusion entity (and at every role change inside one) with the
// cross section (mm³/mm) and the acceleration; AdaptivePAProcessor runs after the cooling buffer and the fan mover, finds the
// speed the entity really prints at (its fastest F up to the next travel, after the cooling slowdown), takes the pressure
// advance for flow = cross section x speed and the acceleration from the filament's measurements, and replaces the tag by
// SET_PRESSURE_ADVANCE when the value changes.
#ifndef slic3r_GCode_AdaptivePA_hpp_
#define slic3r_GCode_AdaptivePA_hpp_

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r {

// Piecewise cubic Hermite interpolation (PCHIP) as OrcaSlicer's: monotone between the points, the end values outside of them.
class PchipInterpolator
{
public:
    PchipInterpolator() = default;
    // At least 2 points with different x (points with the same x are averaged: OrcaSlicer would divide by 0).
    PchipInterpolator(std::vector<double> x, std::vector<double> y);
    double operator()(double x) const;
    size_t size() const { return m_x.size(); }

private:
    std::vector<double> m_x, m_y, m_h, m_delta, m_d;
};

// OrcaSlicer's model: per acceleration a PCHIP over the flow (accelerations with one point are ignored), then a PCHIP over the
// accelerations of the values at the flow.
class AdaptivePAInterpolator
{
public:
    // Lines "PA,flow,acceleration"; empty lines, "; ..." lines (the nozzle headers) and lines of zeros (OrcaSlicer's
    // placeholder "0,0,0") are skipped. Returns an error message for the first wrong line, else empty.
    std::string parse(const std::string &model);
    // At least one acceleration with 2 or more flows.
    bool        valid() const { return ! m_flow_curves.empty(); }
    // The pressure advance rounded to 3 decimals as OrcaSlicer, -1 without a model.
    double      operator()(double flow, double acceleration) const;

private:
    std::map<double, PchipInterpolator> m_flow_curves;
};

class AdaptivePAProcessor
{
public:
    struct Extruder {
        // Adaptive pressure advance on for this extruder with a valid model.
        bool                   adaptive  { false };
        // pressure_advance: the fallback.
        double                 static_pa { 0. };
        AdaptivePAInterpolator model;
    };

    AdaptivePAProcessor(std::vector<Extruder> extruders, bool gcode_comments) : m_extruders(std::move(extruders)), m_comments(gcode_comments) {}

    bool adaptive(unsigned int extruder_id) const { return extruder_id < m_extruders.size() && m_extruders[extruder_id].adaptive; }

    std::string process_layer(std::string &&gcode);

    // The tag the G-code generator writes (OrcaSlicer's format, BR and OV as OrcaSlicer's: a bridge, an overhang perimeter).
    static std::string tag(unsigned int extruder_id, double mm3_per_mm, unsigned int acceleration, bool bridge, bool role_change, bool overhang);
    // SET_PRESSURE_ADVANCE ADVANCE=<value>, the value with up to 4 decimals.
    static std::string set_pressure_advance(double value);

private:
    std::vector<Extruder> m_extruders;
    bool                  m_comments;
    // The pressure advance the printer has, -1: unknown. Also set by SET_PRESSURE_ADVANCE lines of the G-code (tool changes,
    // custom G-code), so OrcaSlicer's resetPreviousPA() is not needed.
    double                m_last_pa { -1. };
    int                   m_last_extruder { -1 };
    // As OrcaSlicer's: the last G1 F outside of wipes, and the feedrate of the last move with an F word (mm/s).
    double                m_current_feedrate { 0. };
    double                m_modal_feedrate { 0. };
};

} // namespace Slic3r

#endif // slic3r_GCode_AdaptivePA_hpp_
