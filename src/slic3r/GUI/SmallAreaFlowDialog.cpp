#include "SmallAreaFlowDialog.hpp"

#include <algorithm>
#include <cmath>

#include <wx/dcbuffer.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>

#include "GUI_App.hpp"
#include "I18N.hpp"

namespace Slic3r { namespace GUI {

static double round_to(double v, double step) { return std::round(v / step) * step; }

SmallAreaFlowChart::SmallAreaFlowChart(wxWindow *parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    const int em = wxGetApp().em_unit();
    SetMinSize(wxSize(55 * em, 32 * em));
    Bind(wxEVT_PAINT,        &SmallAreaFlowChart::on_paint,        this);
    Bind(wxEVT_LEFT_DOWN,    &SmallAreaFlowChart::on_left_down,    this);
    Bind(wxEVT_LEFT_UP,      &SmallAreaFlowChart::on_left_up,      this);
    Bind(wxEVT_MOTION,       &SmallAreaFlowChart::on_motion,       this);
    Bind(wxEVT_LEFT_DCLICK,  &SmallAreaFlowChart::on_double_click, this);
    Bind(wxEVT_RIGHT_DOWN,   &SmallAreaFlowChart::on_right_down,   this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent &) { m_dragged = -1; });
}

void SmallAreaFlowChart::set_points(const std::vector<SmallAreaFlowModel::Point> &points)
{
    m_points = points;
    update_range();
    Refresh();
}

void SmallAreaFlowChart::update_range()
{
    m_max_length = m_points.empty() ? 10. : std::max(1., m_points.back().length * 1.15);
}

wxRect SmallAreaFlowChart::plot_rect() const
{
    const int em = wxGetApp().em_unit();
    const wxSize size = GetClientSize();
    return wxRect(6 * em, em, std::max(1, size.x - 8 * em), std::max(1, size.y - 5 * em));
}

wxPoint SmallAreaFlowChart::to_screen(double length, double factor) const
{
    const wxRect r = plot_rect();
    return wxPoint(r.x + int(std::round(length / m_max_length * r.width)),
                   r.y + r.height - int(std::round(factor / 1.05 * r.height)));
}

void SmallAreaFlowChart::to_model(const wxPoint &pt, double &length, double &factor) const
{
    const wxRect r = plot_rect();
    length = double(pt.x - r.x) / r.width * m_max_length;
    factor = double(r.y + r.height - pt.y) / r.height * 1.05;
}

int SmallAreaFlowChart::point_at(const wxPoint &pt) const
{
    const int radius = wxGetApp().em_unit();
    for (size_t i = 0; i < m_points.size(); ++ i) {
        const wxPoint p = to_screen(m_points[i].length, m_points[i].factor);
        if (std::abs(p.x - pt.x) <= radius && std::abs(p.y - pt.y) <= radius)
            return int(i);
    }
    return -1;
}

void SmallAreaFlowChart::on_paint(wxPaintEvent &)
{
    wxAutoBufferedPaintDC dc(this);
    const wxColour bg   = wxGetApp().get_window_default_clr();
    const wxColour text = wxGetApp().get_label_clr_default();
    // Grid: a quarter of the way from the background to the text colour (works in dark mode too).
    const wxColour grid((bg.Red() * 3 + text.Red()) / 4, (bg.Green() * 3 + text.Green()) / 4, (bg.Blue() * 3 + text.Blue()) / 4);
    dc.SetBackground(wxBrush(bg));
    dc.Clear();
    dc.SetFont(wxGetApp().normal_font());
    dc.SetTextForeground(text);

    const wxRect r  = plot_rect();
    const int    em = wxGetApp().em_unit();

    // Grid and axis labels.
    double step = 0.5;
    for (double s : { 0.5, 1., 2., 5., 10., 20., 50. })
        if (m_max_length / s <= 10.) { step = s; break; }
    dc.SetPen(wxPen(grid, 1));
    for (double x = 0.; x <= m_max_length + 1e-9; x += step) {
        const wxPoint p = to_screen(x, 0.);
        dc.DrawLine(p.x, r.y, p.x, r.y + r.height);
        const wxString label = wxString::FromDouble(x);
        const wxSize   ext   = dc.GetTextExtent(label);
        dc.DrawText(label, p.x - ext.x / 2, r.y + r.height + em / 3);
    }
    for (int i = 0; i <= 5; ++ i) {
        const double  y = i * 0.2;
        const wxPoint p = to_screen(0., y);
        dc.DrawLine(r.x, p.y, r.x + r.width, p.y);
        const wxString label = wxString::FromDouble(y, 1);
        const wxSize   ext   = dc.GetTextExtent(label);
        dc.DrawText(label, r.x - ext.x - em / 2, p.y - ext.y / 2);
    }
    {
        const wxString xl = _L("Segment length (mm)");
        const wxSize   ext = dc.GetTextExtent(xl);
        dc.DrawText(xl, r.x + (r.width - ext.x) / 2, r.y + r.height + em * 2);
        const wxString yl = _L("Flow factor");
        const wxSize   eyt = dc.GetTextExtent(yl);
        dc.DrawRotatedText(yl, em / 3, r.y + (r.height + eyt.x) / 2, 90.);
    }
    dc.SetPen(wxPen(text, 1));
    dc.DrawLine(r.x, r.y, r.x, r.y + r.height);
    dc.DrawLine(r.x, r.y + r.height, r.x + r.width, r.y + r.height);

    if (m_points.size() < 2)
        return;

    // The curve as used for slicing; straight red lines if the points are not a valid model.
    const auto model = SmallAreaFlowModel::parse(SmallAreaFlowModel::to_text(m_points));
    if (model) {
        dc.SetPen(wxPen(wxColour(0x00, 0x96, 0x88), std::max(2, em / 5)));
        std::vector<wxPoint> curve;
        for (int px = 0; px <= r.width; ++ px) {
            const double l = double(px) / r.width * m_max_length;
            curve.emplace_back(to_screen(l, model->factor(l)));
        }
        dc.DrawLines(int(curve.size()), curve.data());
    } else {
        dc.SetPen(wxPen(*wxRED, std::max(2, em / 5)));
        for (size_t i = 1; i < m_points.size(); ++ i)
            dc.DrawLine(to_screen(m_points[i - 1].length, m_points[i - 1].factor), to_screen(m_points[i].length, m_points[i].factor));
    }

    dc.SetPen(wxPen(text, 1));
    dc.SetBrush(wxBrush(wxColour(0xED, 0x6B, 0x21)));
    for (const SmallAreaFlowModel::Point &pt : m_points)
        dc.DrawCircle(to_screen(pt.length, pt.factor), em / 2);
}

void SmallAreaFlowChart::on_left_down(wxMouseEvent &evt)
{
    m_dragged = point_at(evt.GetPosition());
    if (m_dragged >= 0 && ! HasCapture())
        CaptureMouse();
}

void SmallAreaFlowChart::on_left_up(wxMouseEvent &)
{
    if (HasCapture())
        ReleaseMouse();
    if (m_dragged >= 0) {
        m_dragged = -1;
        update_range();
        Refresh();
    }
}

void SmallAreaFlowChart::on_motion(wxMouseEvent &evt)
{
    if (m_dragged < 0 || ! evt.Dragging())
        return;
    double length, factor;
    to_model(evt.GetPosition(), length, factor);
    const size_t i = size_t(m_dragged);
    const double length_step = 0.01, factor_step = 0.001;
    SmallAreaFlowModel::Point &pt = m_points[i];
    // Keep the model valid: first length 0, last factor 1, lengths and factors strictly increasing.
    if (i == 0)
        length = 0.;
    else
        length = std::max(round_to(length, length_step), m_points[i - 1].length + length_step);
    if (i + 1 < m_points.size())
        length = std::min(length, m_points[i + 1].length - length_step);
    if (i + 1 == m_points.size())
        factor = 1.;
    else {
        factor = std::clamp(round_to(factor, factor_step), 0., m_points[i + 1].factor - factor_step);
        if (i > 0)
            factor = std::max(factor, m_points[i - 1].factor + factor_step);
    }
    if (length != pt.length || factor != pt.factor) {
        pt.length = length;
        pt.factor = factor;
        Refresh();
        if (on_change)
            on_change();
    }
}

void SmallAreaFlowChart::on_double_click(wxMouseEvent &evt)
{
    if (point_at(evt.GetPosition()) >= 0 || m_points.size() < 2)
        return;
    double length, factor;
    to_model(evt.GetPosition(), length, factor);
    length = round_to(length, 0.01);
    factor = round_to(factor, 0.001);
    // Insert between the neighbours in length, only where it keeps the factors increasing.
    for (size_t i = 1; i < m_points.size(); ++ i)
        if (length > m_points[i - 1].length && length < m_points[i].length) {
            if (factor <= m_points[i - 1].factor || factor >= m_points[i].factor)
                // Put it on the current curve instead.
                if (auto model = SmallAreaFlowModel::parse(SmallAreaFlowModel::to_text(m_points)); model)
                    factor = round_to(model->factor(length), 0.001);
            if (factor > m_points[i - 1].factor && factor < m_points[i].factor) {
                m_points.insert(m_points.begin() + i, SmallAreaFlowModel::Point{ length, factor });
                Refresh();
                if (on_change)
                    on_change();
            }
            return;
        }
}

void SmallAreaFlowChart::on_right_down(wxMouseEvent &evt)
{
    const int i = point_at(evt.GetPosition());
    // The first and the last point stay.
    if (i > 0 && i + 1 < int(m_points.size())) {
        m_points.erase(m_points.begin() + i);
        Refresh();
        if (on_change)
            on_change();
    }
}

SmallAreaFlowDialog::SmallAreaFlowDialog(wxWindow *parent, const std::string &model_text)
    : wxDialog(parent, wxID_ANY, _L("Small area flow compensation"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_model_text(model_text)
{
    SetFont(wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(this);
    const int em = wxGetApp().em_unit();

    m_chart = new SmallAreaFlowChart(this);
    m_chart->on_change = [this]() { chart_changed(); };

    m_text = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(model_text), wxDefaultPosition, wxSize(16 * em, -1), wxTE_MULTILINE);
    wxGetApp().UpdateDarkUI(m_text);
    m_text->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { text_changed(); });

    auto *help = new wxStaticText(this, wxID_ANY,
        _L("Flow factor applied to a solid infill segment of the given length. Drag a point to move it, "
           "double click to add one, right click to remove one. Segments longer than the last point are not changed."));
    help->Wrap(70 * em);
    m_error = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_error->SetForegroundColour(*wxRED);

    auto *text_sizer = new wxBoxSizer(wxVERTICAL);
    text_sizer->Add(new wxStaticText(this, wxID_ANY, _L("Length (mm), factor")), 0, wxBOTTOM, em / 2);
    text_sizer->Add(m_text, 1, wxEXPAND);

    auto *top = new wxBoxSizer(wxHORIZONTAL);
    top->Add(m_chart, 1, wxEXPAND | wxRIGHT, em);
    top->Add(text_sizer, 0, wxEXPAND);

    auto *main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(help, 0, wxEXPAND | wxALL, em);
    main_sizer->Add(top, 1, wxEXPAND | wxLEFT | wxRIGHT, em);
    main_sizer->Add(m_error, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, em);
    auto *buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    wxGetApp().SetWindowVariantForButton(buttons->GetAffirmativeButton());
    wxGetApp().SetWindowVariantForButton(buttons->GetCancelButton());
    wxGetApp().UpdateDarkUI(buttons->GetAffirmativeButton());
    wxGetApp().UpdateDarkUI(buttons->GetCancelButton());
    main_sizer->Add(buttons, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, em);
    SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);

    buttons->GetAffirmativeButton()->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        m_model_text = m_text->GetValue().ToUTF8().data();
        EndModal(wxID_OK);
    });

    text_changed();
}

void SmallAreaFlowDialog::text_changed()
{
    if (m_updating)
        return;
    std::string error;
    const auto model = SmallAreaFlowModel::parse(m_text->GetValue().ToUTF8().data(), &error);
    if (model)
        m_chart->set_points(model->points());
    m_error->SetLabel(model ? wxString() : wxString::FromUTF8(error));
    if (wxWindow *ok = FindWindowById(wxID_OK, this))
        ok->Enable(model.has_value());
    Layout();
}

void SmallAreaFlowDialog::chart_changed()
{
    m_updating = true;
    m_text->SetValue(wxString::FromUTF8(SmallAreaFlowModel::to_text(m_chart->points())));
    m_updating = false;
    m_error->SetLabel(wxString());
    if (wxWindow *ok = FindWindowById(wxID_OK, this))
        ok->Enable(true);
}

} } // namespace Slic3r::GUI
