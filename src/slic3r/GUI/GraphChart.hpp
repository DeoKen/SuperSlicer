///|/ Ported from SuperSlicer (RammingChart.hpp, generalized to edit any graph).
///|/ Copyright (c) Prusa Research 2018 - 2019 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_GraphChart_hpp_
#define slic3r_GUI_GraphChart_hpp_

#include <functional>
#include <optional>
#include <vector>

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/geometry.h>

#include "libslic3r/Config.hpp"

namespace Slic3r { namespace GUI {

// Chart with draggable points, used by the graph dialog.
// Left click selects / drags a point, right click deletes a point or adds one, double click adds a point.
class GraphChart : public wxWindow
{
public:
    GraphChart(wxWindow *parent, wxRect rect, int scale_unit = 10);

    // Range of the axes. NaN keeps the current value.
    void set_xy_range(double min_x, double min_y, double max_x, double max_y);
    void set_x_label(const wxString &label, double incr);
    void set_y_label(const wxString &label, double incr);
    void set_no_point_label(const wxString &label) { m_no_point_legend = label; }
    void set_type(GraphData::GraphType type) { m_type = type; recalculate_line(); }
    GraphData::GraphType get_type() const { return m_type; }

    bool            has_selected_point() const { return m_clicked >= 0; }
    wxPoint2DDouble get_selected_point_position() const { return m_clicked >= 0 ? m_buttons[m_clicked].get_pos() : wxPoint2DDouble(); }
    void            set_selected_point_position(wxPoint2DDouble new_pos);
    // Called when the selected point changes or moves, with nullopt when no point is selected.
    void            set_selection_point_callback(std::function<void(std::optional<wxPoint2DDouble>)> func) { m_func_selected_point_moved = func; }

    std::vector<std::pair<double, double>> get_buttons() const;
    void                                   set_buttons(const std::vector<std::pair<double, double>> &new_buttons);

private:
    void draw();
    void mouse_clicked(wxMouseEvent &event);
    void mouse_right_button_clicked(wxMouseEvent &event);
    void mouse_moved(wxMouseEvent &event);
    void mouse_double_clicked(wxMouseEvent &event);
    void mouse_left_window(wxMouseEvent &event);
    void mouse_released(wxMouseEvent &event);
    void paint_event(wxPaintEvent &) { draw(); }

    static const int side = 10; // side of a draggable button

    class ButtonToDrag {
    public:
        ButtonToDrag(wxPoint2DDouble pos, const GraphChart &chart) : m_pos(pos), m_pos_following(pos), m_chart(&chart) {}
        bool            operator<(const ButtonToDrag &a) const { return m_pos.m_x < a.m_pos.m_x; }
        wxPoint2DDouble get_pos() const { return m_pos; }
        // Moved inside the chart area, rounded to the steps of the axes.
        void            move(double x, double y);
    private:
        wxPoint2DDouble   m_pos;            // position in math coordinates
        wxPoint2DDouble   m_pos_following;  // position following the mouse, before rounding
        const GraphChart *m_chart;
    };

    wxPoint         math_to_screen(const wxPoint2DDouble &math) const;
    wxPoint2DDouble screen_to_math(const wxPoint &screen) const;
    int             which_button_is_clicked(const wxPoint &point) const;
    double          round_x(double x) const { return std::round(x / m_x_legend_incr) * m_x_legend_incr; }
    double          round_y(double y) const { return std::round(y / m_y_legend_incr) * m_y_legend_incr; }
    void            add_point(wxPoint2DDouble pos);
    void            recalculate_line();
    void            notify_selection();

    GraphData::GraphType m_type = GraphData::GraphType::LINEAR;
    const int            scale_unit;
    int                  legend_side;
    wxString             m_x_legend;
    wxString             m_y_legend;
    double               m_x_legend_incr = 1.;
    int                  m_x_precision   = 0;
    double               m_y_legend_incr = 1.;
    int                  m_y_precision   = 0;
    wxString             m_no_point_legend;

    wxRect                    m_rect; // rectangle on screen the chart is mapped into (screen coordinates)
    wxPoint                   m_previous_mouse;
    bool                      m_is_hover_point = false;
    wxPoint2DDouble           m_mouse_hover_point;
    std::vector<ButtonToDrag> m_buttons;
    std::vector<int>          m_line_to_draw;
    wxRect2DDouble            m_visible_area;
    wxRect2DDouble            m_moveable_area;
    // Indices into m_buttons, -1 for none. Reset when points are added or deleted.
    int                       m_dragged = -1;
    int                       m_clicked = -1;
    int                       m_hover   = -1;
    std::function<void(std::optional<wxPoint2DDouble>)> m_func_selected_point_moved;
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_GraphChart_hpp_
