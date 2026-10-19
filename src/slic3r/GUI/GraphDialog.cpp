///|/ Ported from SuperSlicer.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "GraphDialog.hpp"

#include <algorithm>

#include <wx/button.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include "GraphChart.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Widgets/SpinInput.hpp"

namespace Slic3r { namespace GUI {

static wxString graph_type_name(GraphData::GraphType type)
{
    switch (type) {
    case GraphData::GraphType::SQUARE: return _L("Square");
    case GraphData::GraphType::SPLINE: return _L("Spline");
    default:                           return _L("Linear");
    }
}

GraphDialog::GraphDialog(wxWindow *parent, const GraphData &value, const GraphData &reset_value, const GraphSettings &settings)
    : wxDialog(parent, wxID_ANY, _(settings.title), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    const int em = wxGetApp().em_unit();
#ifndef _WIN32
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_FRAMEBK));
#endif
    auto *main_sizer = new wxBoxSizer(wxVERTICAL);

    main_sizer->Add(new wxStaticText(this, wxID_ANY, _(settings.description)), 0, wxALL, em);

    m_chart = new GraphChart(this, wxRect(0, 0, 64 * em, 36 * em), em);
    m_chart->set_x_label(_(settings.x_label), settings.step_x);
    m_chart->set_y_label(_(settings.y_label), settings.step_y);
    m_chart->set_no_point_label(_(settings.null_label));
    m_chart->set_xy_range(settings.min_x, settings.min_y, settings.max_x, settings.max_y);
#ifdef _WIN32
    wxGetApp().UpdateDarkUI(m_chart);
#else
    m_chart->SetBackgroundColour(GetBackgroundColour());
#endif
    set_chart_data(value);
    main_sizer->Add(m_chart, 0, wxLEFT | wxRIGHT, em);

    auto *line = new wxBoxSizer(wxHORIZONTAL);
    auto *bt_reset = new wxButton(this, wxID_ANY, _L("Reset"));
    bt_reset->SetToolTip(_L("Set the points of the default value."));
    line->Add(bt_reset, 0, wxALIGN_CENTER_VERTICAL);
    std::vector<GraphData::GraphType> allowed_types = settings.allowed_types;
    if (allowed_types.empty())
        allowed_types = { GraphData::GraphType::LINEAR, GraphData::GraphType::SQUARE, GraphData::GraphType::SPLINE };
    auto *type_text = new wxStaticText(this, wxID_ANY, graph_type_name(m_chart->get_type()));
    if (allowed_types.size() > 1) {
        auto *bt_type = new wxButton(this, wxID_ANY, _L("Change type"));
        bt_type->SetToolTip(_L("Change the shape of the graph between the points: linear, square (steps) or spline (smooth curve)."));
        line->AddSpacer(em);
        line->Add(bt_type, 0, wxALIGN_CENTER_VERTICAL);
        bt_type->Bind(wxEVT_BUTTON, [this, allowed_types, type_text](wxCommandEvent &) {
            auto it = std::find(allowed_types.begin(), allowed_types.end(), m_chart->get_type());
            m_chart->set_type(it == allowed_types.end() || it + 1 == allowed_types.end() ? allowed_types.front() : *(it + 1));
            type_text->SetLabel(graph_type_name(m_chart->get_type()));
            Layout();
        });
        wxGetApp().UpdateDarkUI(bt_type);
    }
    line->AddSpacer(em);
    line->Add(type_text, 0, wxALIGN_CENTER_VERTICAL);
    line->AddSpacer(2 * em);
    auto *help_text = new wxStaticText(this, wxID_ANY, _L("Help"));
    help_text->SetToolTip(_L("Drag a point with the left mouse button. Left click a point to select it (yellow) and edit its values "
                             "in the boxes on the right.\nRight click a point to delete it.\nRight click or double click an empty "
                             "spot to add a point."));
    line->Add(help_text, 0, wxALIGN_CENTER_VERTICAL);
    line->AddStretchSpacer();

    // Values of the selected point.
    const long style = wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER;
    m_widget_x = new ::SpinInputDouble(this, "", wxEmptyString, wxDefaultPosition, wxSize(8 * em, -1), style,
                                       settings.min_x, settings.max_x, settings.min_x, settings.step_x);
    m_widget_y = new ::SpinInputDouble(this, "", wxEmptyString, wxDefaultPosition, wxSize(8 * em, -1), style,
                                       settings.min_y, settings.max_y, settings.min_y, settings.step_y);
    line->Add(new wxStaticText(this, wxID_ANY, _L("Selected point") + ":"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
    line->Add(m_widget_x, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
    line->Add(m_widget_y, 0, wxALIGN_CENTER_VERTICAL);
    main_sizer->Add(line, 0, wxEXPAND | wxALL, em);

    m_widget_x->Enable(false);
    m_widget_y->Enable(false);
    m_chart->set_selection_point_callback([this](std::optional<wxPoint2DDouble> pos) {
        m_widget_x->Enable(pos.has_value());
        m_widget_y->Enable(pos.has_value());
        if (pos) {
            m_widget_x->SetValue(pos->m_x);
            m_widget_y->SetValue(pos->m_y);
        }
    });
    m_widget_x->Bind(wxEVT_SPINCTRL, [this](wxCommandEvent &) {
        wxPoint2DDouble pos = m_chart->get_selected_point_position();
        pos.m_x = m_widget_x->GetValue();
        m_chart->set_selected_point_position(pos);
    });
    m_widget_y->Bind(wxEVT_SPINCTRL, [this](wxCommandEvent &) {
        wxPoint2DDouble pos = m_chart->get_selected_point_position();
        pos.m_y = m_widget_y->GetValue();
        m_chart->set_selected_point_position(pos);
    });
    bt_reset->Bind(wxEVT_BUTTON, [this, reset_value, type_text](wxCommandEvent &) {
        set_chart_data(reset_value);
        type_text->SetLabel(graph_type_name(m_chart->get_type()));
        Layout();
    });

    main_sizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, em);
    SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);

    Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        m_output_data = get_chart_data();
        EndModal(wxID_OK);
    }, wxID_OK);

    wxGetApp().UpdateDarkUI(bt_reset);
    wxGetApp().UpdateDlgDarkUI(this);
    CenterOnParent();
}

void GraphDialog::set_chart_data(const GraphData &data)
{
    std::vector<std::pair<double, double>> buttons;
    for (const Vec2d &pt : data.data())
        buttons.emplace_back(pt.x(), pt.y());
    m_chart->set_buttons(buttons);
    m_chart->set_type(data.type);
}

GraphData GraphDialog::get_chart_data() const
{
    Pointfs points;
    for (const auto &[x, y] : m_chart->get_buttons())
        points.emplace_back(x, y);
    const size_t size = points.size();
    return GraphData(0, size, m_chart->get_type(), std::move(points));
}

}} // namespace Slic3r::GUI
