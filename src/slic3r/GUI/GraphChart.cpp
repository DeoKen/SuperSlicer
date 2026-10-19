///|/ Ported from SuperSlicer (RammingChart.cpp, generalized to edit any graph).
///|/ Copyright (c) Prusa Research 2018 - 2021 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "GraphChart.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <wx/dcbuffer.h>
#include <wx/graphics.h>

#include "GUI_App.hpp"
#include "libslic3r/Utils.hpp"

namespace Slic3r { namespace GUI {

static int precision_of(double incr)
{
    for (int i = 0; i < 6; ++ i)
        if (std::round(incr * std::pow(10., i)) > 0)
            return i;
    return 6;
}

static wxString format_value(double value, int precision)
{
    return precision == 0 ? wxString::Format("%d", int(std::round(value))) : wxString::Format("%.*f", precision, value);
}

GraphChart::GraphChart(wxWindow *parent, wxRect rect, int scale_unit)
    : wxWindow(parent, wxID_ANY, rect.GetTopLeft(), rect.GetSize()), scale_unit(scale_unit), legend_side(5 * scale_unit)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_rect = wxRect(wxPoint(2 * legend_side, 0), rect.GetSize() - wxSize(2 * legend_side, legend_side));
    m_moveable_area = wxRect2DDouble(0., 0., 100., 100.);
    set_xy_range(0., 0., 100., 100.);

    Bind(wxEVT_MOTION,       &GraphChart::mouse_moved, this);
    Bind(wxEVT_LEFT_DOWN,    &GraphChart::mouse_clicked, this);
    Bind(wxEVT_LEFT_UP,      &GraphChart::mouse_released, this);
    Bind(wxEVT_LEFT_DCLICK,  &GraphChart::mouse_double_clicked, this);
    Bind(wxEVT_RIGHT_DOWN,   &GraphChart::mouse_right_button_clicked, this);
    Bind(wxEVT_LEAVE_WINDOW, &GraphChart::mouse_left_window, this);
    Bind(wxEVT_PAINT,        &GraphChart::paint_event, this);
}

void GraphChart::set_xy_range(double min_x, double min_y, double max_x, double max_y)
{
    if (! std::isnan(min_x))
        m_moveable_area.SetLeft(min_x);
    if (! std::isnan(max_x) && max_x > m_moveable_area.GetLeft())
        m_moveable_area.SetRight(max_x);
    if (! std::isnan(min_y))
        m_moveable_area.SetTop(min_y);
    if (! std::isnan(max_y) && max_y > m_moveable_area.GetTop())
        m_moveable_area.SetBottom(max_y);
    // The visible area is the moveable area + 5% on each side.
    m_visible_area.m_x      = m_moveable_area.m_x - m_moveable_area.m_width / 20.;
    m_visible_area.m_y      = m_moveable_area.m_y - m_moveable_area.m_height / 20.;
    m_visible_area.m_width  = m_moveable_area.m_width * 1.1;
    m_visible_area.m_height = m_moveable_area.m_height * 1.1;
    recalculate_line();
}

void GraphChart::set_x_label(const wxString &label, double incr)
{
    m_x_legend      = label;
    m_x_legend_incr = incr;
    m_x_precision   = precision_of(incr);
}

void GraphChart::set_y_label(const wxString &label, double incr)
{
    m_y_legend      = label;
    m_y_legend_incr = incr;
    m_y_precision   = precision_of(incr);
}

void GraphChart::ButtonToDrag::move(double x, double y)
{
    m_pos_following.m_x += x;
    m_pos_following.m_y += y;
    const wxRect2DDouble &area = m_chart->m_moveable_area;
    m_pos.m_x = m_chart->round_x(std::clamp(m_pos_following.m_x, area.GetLeft(), area.GetRight()));
    m_pos.m_y = m_chart->round_y(std::clamp(m_pos_following.m_y, area.GetTop(), area.GetBottom()));
}

wxPoint GraphChart::math_to_screen(const wxPoint2DDouble &math) const
{
    wxPoint screen;
    screen.x = int((math.m_x - m_visible_area.m_x) * (m_rect.GetWidth() / m_visible_area.m_width));
    screen.y = int((math.m_y - m_visible_area.m_y) * (m_rect.GetHeight() / m_visible_area.m_height));
    screen.y *= -1;
    screen += m_rect.GetLeftBottom();
    return screen;
}

wxPoint2DDouble GraphChart::screen_to_math(const wxPoint &screen) const
{
    wxPoint2DDouble math = screen;
    math -= m_rect.GetLeftBottom();
    math.m_y *= -1;
    math.m_x *= m_visible_area.m_width / m_rect.GetWidth();
    math.m_y *= m_visible_area.m_height / m_rect.GetHeight();
    return math + m_visible_area.GetLeftTop();
}

int GraphChart::which_button_is_clicked(const wxPoint &point) const
{
    if (! m_rect.Contains(point))
        return -1;
    int dist = std::numeric_limits<int>::max();
    int idx  = -1;
    for (size_t i = 0; i < m_buttons.size(); ++ i) {
        // Bounding rectangle of this button, twice its size to make it easier to grab.
        const wxRect rect(math_to_screen(m_buttons[i].get_pos()) - wxPoint(side, side), wxSize(side * 2, side * 2));
        if (rect.Contains(point)) {
            const int new_dist = std::abs(rect.x + rect.width / 2 - point.x) + std::abs(rect.y + rect.height / 2 - point.y);
            if (new_dist < dist) {
                dist = new_dist;
                idx  = int(i);
            }
        }
    }
    return idx;
}

void GraphChart::set_selected_point_position(wxPoint2DDouble new_pos)
{
    if (m_clicked < 0)
        return;
    ButtonToDrag &button = m_buttons[m_clicked];
    button.move(new_pos.m_x - button.get_pos().m_x, new_pos.m_y - button.get_pos().m_y);
    recalculate_line();
}

std::vector<std::pair<double, double>> GraphChart::get_buttons() const
{
    std::vector<std::pair<double, double>> out;
    for (const ButtonToDrag &button : m_buttons)
        out.emplace_back(button.get_pos().m_x, button.get_pos().m_y);
    std::stable_sort(out.begin(), out.end(), [](const auto &l, const auto &r) { return l.first < r.first; });
    return out;
}

void GraphChart::set_buttons(const std::vector<std::pair<double, double>> &new_buttons)
{
    m_buttons.clear();
    for (const auto &[x, y] : new_buttons)
        m_buttons.emplace_back(wxPoint2DDouble(x, y), *this);
    m_dragged = m_clicked = m_hover = -1;
    notify_selection();
    recalculate_line();
}

void GraphChart::add_point(wxPoint2DDouble pos)
{
    pos.m_x = round_x(std::clamp(pos.m_x, m_moveable_area.GetLeft(), m_moveable_area.GetRight()));
    pos.m_y = round_y(std::clamp(pos.m_y, m_moveable_area.GetTop(), m_moveable_area.GetBottom()));
    for (const ButtonToDrag &button : m_buttons)
        if (button.get_pos().m_x == pos.m_x)
            return;
    m_buttons.emplace_back(pos, *this);
    m_dragged = m_hover = -1;
    m_clicked = int(m_buttons.size()) - 1;
    notify_selection();
    recalculate_line();
}

void GraphChart::notify_selection()
{
    if (m_func_selected_point_moved)
        m_func_selected_point_moved(m_clicked >= 0 ? std::optional<wxPoint2DDouble>(m_buttons[m_clicked].get_pos()) : std::nullopt);
}

void GraphChart::recalculate_line()
{
    m_line_to_draw.clear();
    // Same interpolation as the slicer.
    Pointfs points;
    for (const auto &[x, y] : get_buttons())
        points.emplace_back(x, y);
    if (! points.empty()) {
        const GraphData graph(0, points.size(), m_type, points);
        for (int x = m_rect.GetLeft(); x <= m_rect.GetRight(); ++ x) {
            const double math_x = screen_to_math(wxPoint(x, 0)).m_x;
            const int    y      = math_to_screen(wxPoint2DDouble(math_x, graph.interpolate(math_x))).y;
            m_line_to_draw.push_back(std::clamp(y, m_rect.GetTop(), m_rect.GetBottom() - 1));
        }
    }
    Refresh();
}

void GraphChart::draw()
{
    wxAutoBufferedPaintDC dc(this); // unbuffered DC caused flickering on win

    dc.SetBrush(GetBackgroundColour());
    dc.SetPen(GetBackgroundColour());
    dc.DrawRectangle(GetClientRect()); // otherwise the background would end up black on windows

#ifdef _WIN32
    const wxPen line_pen(GetForegroundColour());
    dc.SetBrush(wxBrush(wxGetApp().get_highlight_default_clr()));
#else
    const wxPen line_pen(*wxBLACK_PEN);
    dc.SetBrush(*wxWHITE_BRUSH);
#endif
    dc.SetPen(line_pen);
    dc.DrawRectangle(m_rect);

    if (m_line_to_draw.empty()) {
        dc.DrawText(m_no_point_legend, wxPoint(m_rect.GetLeft() + m_rect.GetWidth() / 2 - 2 * legend_side, m_rect.GetBottom() - m_rect.GetHeight() / 2));
    } else {
        // Area under the curve, from green (low) to red (high).
        for (size_t i = 0; i < m_line_to_draw.size(); ++ i) {
            const int color = int(510 * ((m_rect.GetBottom() - m_line_to_draw[i]) / double(m_rect.GetHeight())));
            dc.SetPen(wxPen(wxColour(std::min(255, color), 255 - std::max(color - 255, 0), 0), 1));
            dc.DrawLine(m_rect.GetLeft() + int(i), m_line_to_draw[i], m_rect.GetLeft() + int(i), m_rect.GetBottom());
        }
        dc.SetPen(line_pen);
        for (size_t i = 0; i + 1 < m_line_to_draw.size(); ++ i)
            dc.DrawLine(m_rect.GetLeft() + int(i), m_line_to_draw[i], m_rect.GetLeft() + int(i) + 1, m_line_to_draw[i + 1]);
    }

    // Value of the dragged / hovered point, or of the mouse position, in the bottom right corner.
    {
        wxPoint2DDouble pos;
        if (m_dragged >= 0)
            pos = m_buttons[m_dragged].get_pos();
        else if (m_is_hover_point)
            pos = m_mouse_hover_point;
        else
            pos = screen_to_math(m_previous_mouse);
        const wxString text = "x: " + format_value(pos.m_x, m_x_precision) + "   y: " + format_value(pos.m_y, m_y_precision);
        wxSize text_size = dc.GetTextExtent(text);
        const wxPoint text_pos(m_rect.GetRight() - text_size.x - scale_unit / 2, m_rect.GetBottom() - text_size.y - scale_unit / 2);
        if (wxGraphicsContext *gc = wxGraphicsContext::Create(dc)) {
            // Semi-transparent background, so that the text stays readable over the curve.
            const wxColour bcol = GetBackgroundColour();
            gc->SetBrush(wxColour(bcol.Red(), bcol.Green(), bcol.Blue(), 160));
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->DrawRectangle(text_pos.x - 2, text_pos.y, text_size.x + 4, text_size.y);
            delete gc;
        }
        dc.DrawText(text, text_pos);
    }

    // Draggable buttons: blue, orange when hovered or dragged, yellow when selected.
    dc.SetPen(line_pen);
    for (size_t idx = 0; idx < m_buttons.size(); ++ idx) {
        if (int(idx) == m_dragged || int(idx) == m_hover)
            dc.SetBrush(wxBrush(wxColour(255, 150, 0)));
        else if (int(idx) == m_clicked)
            dc.SetBrush(wxBrush(wxColour(240, 240, 0)));
        else
            dc.SetBrush(*wxBLUE_BRUSH);
        dc.DrawCircle(math_to_screen(m_buttons[idx].get_pos()), side / 2);
    }

    // X axis.
    int last_mark = std::numeric_limits<int>::min() / 2;
    for (double math_x = m_moveable_area.GetLeft(); math_x <= m_moveable_area.GetRight() + EPSILON; math_x += m_x_legend_incr) {
        const int x = math_to_screen(wxPoint2DDouble(math_x, m_visible_area.m_y)).x;
        const int y = m_rect.GetBottom();
        if (x - last_mark < legend_side)
            continue;
        dc.DrawLine(x, y + 3, x, y - 3);
        dc.DrawText(format_value(math_x, m_x_precision), wxPoint(x - scale_unit, y + scale_unit / 2));
        last_mark = x;
    }
    // Y axis.
    last_mark = std::numeric_limits<int>::max() / 2;
    for (double math_y = m_moveable_area.GetTop(); math_y <= m_moveable_area.GetBottom() + EPSILON; math_y += m_y_legend_incr) {
        const int y = math_to_screen(wxPoint2DDouble(m_visible_area.m_x, math_y)).y;
        const int x = m_rect.GetLeft();
        if (last_mark - y < legend_side / 2)
            continue;
        dc.DrawLine(x - 3, y, x + 3, y);
        dc.DrawText(format_value(math_y, m_y_precision), wxPoint(x - 3 * scale_unit, y - scale_unit / 2));
        last_mark = y;
    }

    // Axis labels.
    wxSize text_size = dc.GetTextExtent(m_x_legend);
    dc.DrawText(m_x_legend, wxPoint(int(0.5 * (m_rect.GetRight() + m_rect.GetLeft()) - text_size.x / 2.), m_rect.GetBottom() + legend_side / 2));
    text_size = dc.GetTextExtent(m_y_legend);
    dc.DrawRotatedText(m_y_legend, wxPoint(0, int(0.5 * (m_rect.GetBottom() + m_rect.GetTop()) + text_size.x / 2.)), 90);
}

void GraphChart::mouse_right_button_clicked(wxMouseEvent &event)
{
    const wxPoint point = event.GetPosition();
    if (const int button_index = which_button_is_clicked(point); button_index != -1) {
        // Delete the point.
        m_buttons.erase(m_buttons.begin() + button_index);
        m_dragged = m_clicked = m_hover = -1;
        notify_selection();
        recalculate_line();
    } else if (m_rect.Contains(point)) {
        add_point(screen_to_math(point));
    }
}

void GraphChart::mouse_double_clicked(wxMouseEvent &event)
{
    const wxPoint point = event.GetPosition();
    if (m_rect.Contains(point) && which_button_is_clicked(point) == -1)
        add_point(screen_to_math(point));
}

void GraphChart::mouse_clicked(wxMouseEvent &event)
{
    const wxPoint point = event.GetPosition();
    if (const int button_index = which_button_is_clicked(point); button_index != -1) {
        m_dragged           = button_index;
        m_clicked           = button_index;
        m_previous_mouse    = point;
        m_mouse_hover_point = m_buttons[button_index].get_pos();
        m_is_hover_point    = true;
    } else {
        m_clicked = -1;
    }
    notify_selection();
    Refresh();
}

void GraphChart::mouse_moved(wxMouseEvent &event)
{
    const wxPoint pos  = event.GetPosition();
    wxRect        rect = m_rect;
    rect.Deflate(side / 2);
    m_hover = -1;
    if (! rect.Contains(pos)) {
        // The mouse left the chart area.
        mouse_left_window(event);
        return;
    }
    if (event.Dragging() && m_dragged >= 0) {
        const int delta_x = pos.x - m_previous_mouse.x;
        const int delta_y = pos.y - m_previous_mouse.y;
        m_buttons[m_dragged].move(double(delta_x) / m_rect.GetWidth() * m_visible_area.m_width,
                                  - double(delta_y) / m_rect.GetHeight() * m_visible_area.m_height);
        m_previous_mouse = pos;
        m_is_hover_point = false;
        if (m_dragged == m_clicked)
            notify_selection();
    } else if (const int idx = which_button_is_clicked(pos); idx < 0) {
        m_previous_mouse = pos;
        m_is_hover_point = false;
    } else {
        m_hover             = idx;
        m_previous_mouse    = math_to_screen(m_buttons[idx].get_pos());
        m_mouse_hover_point = m_buttons[idx].get_pos();
        m_is_hover_point    = true;
    }
    recalculate_line();
}

void GraphChart::mouse_left_window(wxMouseEvent &)
{
    m_hover   = -1;
    m_dragged = -1;
    Refresh();
}

void GraphChart::mouse_released(wxMouseEvent &event)
{
    if (m_dragged >= 0) {
        m_previous_mouse = math_to_screen(m_buttons[m_dragged].get_pos());
        m_dragged        = -1;
        // Hover the previously dragged button.
        mouse_moved(event);
    }
}

}} // namespace Slic3r::GUI
