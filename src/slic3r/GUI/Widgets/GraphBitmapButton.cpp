///|/ Ported from SuperSlicer.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "GraphBitmapButton.hpp"

#include <algorithm>

#include <wx/dcmemory.h>

#include "UIColors.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "libslic3r/Config.hpp"

GraphBitmapButton::GraphBitmapButton(wxWindow *parent, const wxSize &size)
    : m_bitmap_size(size)
{
    wxBitmapButton::Create(parent, wxID_ANY, wxBitmap(size), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
#ifdef __WXMSW__
    if (parent) {
        SetBackgroundColour(parent->GetBackgroundColour());
        SetForegroundColour(parent->GetForegroundColour());
    }
#elif __WXGTK3__
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif
}

void GraphBitmapButton::set_bitmap_size(const wxSize &size)
{
    m_bitmap_size = size;
}

void GraphBitmapButton::draw_bitmap(wxBitmap &bitmap, const wxColour &curve_colour, const wxColour &background, bool solid_background,
                                    const Slic3r::GraphSettings &graph_settings, const Slic3r::GraphData &data) const
{
    wxMemoryDC dc(bitmap);
    const wxSize size = bitmap.GetSize();
    dc.SetBackground(wxBrush(background));
    dc.Clear();
    // Border.
    dc.SetPen(wxPen(wxColour(clr_border_normal)));
    dc.SetBrush(solid_background ? wxBrush(background) : *wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());

    const Slic3r::Pointfs points = data.data();
    if (points.empty()) {
        dc.SetTextForeground(curve_colour);
        dc.SetFont(Slic3r::GUI::wxGetApp().small_font());
        dc.DrawText(_(graph_settings.null_label), 2, 1);
        return;
    }
    double min_x = graph_settings.min_x, max_x = graph_settings.max_x;
    double min_y = graph_settings.min_y, max_y = graph_settings.max_y;
    for (const Slic3r::Vec2d &pt : points) {
        min_x = std::min(min_x, pt.x());
        max_x = std::max(max_x, pt.x());
        min_y = std::min(min_y, pt.y());
        max_y = std::max(max_y, pt.y());
    }
    if (max_x <= min_x || max_y <= min_y)
        return;
    dc.SetPen(wxPen(curve_colour, 2));
    const int    max_px_y = size.GetHeight() - 4;
    const double max_px_x = double(size.GetWidth() - 4);
    wxPoint      last;
    for (int px_x = 0; px_x <= int(max_px_x); ++ px_x) {
        const double  val_x = min_x + (max_x - min_x) * (px_x / max_px_x);
        const double  val_y = data.interpolate(val_x);
        const wxPoint pt(2 + px_x, 2 + max_px_y - int(max_px_y * (val_y - min_y) / (max_y - min_y)));
        if (px_x > 0)
            dc.DrawLine(last, pt);
        last = pt;
    }
}

void GraphBitmapButton::update_bitmap(const Slic3r::GraphSettings &graph_settings, const Slic3r::GraphData &data)
{
    using namespace Slic3r::GUI;
    const wxColour background = GetBackgroundColour();
    const wxColour background_disabled(wxGetApp().dark_mode() ? clr_background_disabled_dark : clr_background_disabled_light);

    wxBitmap image(m_bitmap_size), image_disabled(m_bitmap_size), image_focused(m_bitmap_size);
    draw_bitmap(image,          wxGetApp().get_label_clr_default(),   background,          false, graph_settings, data);
    draw_bitmap(image_disabled, wxColour(clr_foreground_disabled),    background_disabled, true,  graph_settings, data);
    draw_bitmap(image_focused,  wxGetApp().get_color_hovered_btn_label(), background,      false, graph_settings, data);
    SetBitmap(image);
    SetBitmapCurrent(image_focused);
    SetBitmapDisabled(image_disabled);
#ifdef __WXMSW__
    SetBitmapFocus(image_focused);
#endif
    SetMinSize(m_bitmap_size);
    SetSize(m_bitmap_size);
}
