#ifndef slic3r_GUI_SmallAreaFlowDialog_hpp_
#define slic3r_GUI_SmallAreaFlowDialog_hpp_

#include <functional>
#include <string>
#include <vector>

#include <wx/dialog.h>
#include <wx/panel.h>

#include "libslic3r/GCode/SmallAreaFlowCompensation.hpp"

class wxTextCtrl;
class wxStaticText;

namespace Slic3r { namespace GUI {

// Graph of the small area flow compensation model (flow factor per segment length). Points can be dragged,
// added with a double click and removed with a right click; the curve is the one used for slicing.
class SmallAreaFlowChart : public wxPanel
{
public:
    SmallAreaFlowChart(wxWindow *parent);
    void set_points(const std::vector<SmallAreaFlowModel::Point> &points);
    const std::vector<SmallAreaFlowModel::Point>& points() const { return m_points; }
    // Called after the user changed the points with the mouse.
    std::function<void()> on_change;

private:
    std::vector<SmallAreaFlowModel::Point> m_points;
    int    m_dragged { -1 };
    double m_max_length { 10. };

    wxRect  plot_rect() const;
    wxPoint to_screen(double length, double factor) const;
    void    to_model(const wxPoint &pt, double &length, double &factor) const;
    int     point_at(const wxPoint &pt) const;
    void    update_range();

    void on_paint(wxPaintEvent &);
    void on_left_down(wxMouseEvent &evt);
    void on_left_up(wxMouseEvent &evt);
    void on_motion(wxMouseEvent &evt);
    void on_double_click(wxMouseEvent &evt);
    void on_right_down(wxMouseEvent &evt);
};

class SmallAreaFlowDialog : public wxDialog
{
public:
    SmallAreaFlowDialog(wxWindow *parent, const std::string &model_text);
    std::string get_model() const { return m_model_text; }

private:
    SmallAreaFlowChart *m_chart { nullptr };
    wxTextCtrl         *m_text  { nullptr };
    wxStaticText       *m_error { nullptr };
    std::string         m_model_text;
    bool                m_updating { false };

    void text_changed();
    void chart_changed();
};

} } // namespace Slic3r::GUI

#endif // slic3r_GUI_SmallAreaFlowDialog_hpp_
