///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "PluginConfigDialog.hpp"

#include <algorithm>
#include <ios>
#include <string>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/Utils.hpp"

#include "format.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "slic3r/Utils/Process.hpp"

namespace Slic3r::GUI {

namespace {

const char *const PLUGIN_ACTIVATION_DIR = "plugin";
const char *const ACTIVATED_PLUGINS_FILENAME = "activated.ini";

boost::filesystem::path active_plugin_config_path()
{
    return boost::filesystem::path(data_dir()) / PLUGIN_ACTIVATION_DIR / ACTIVATED_PLUGINS_FILENAME;
}

wxString step_name(slicing_step_t step)
{
    switch (step) {
    case STEP_LAYER_HEIGHT:         return "Choose Layer Height";
    case STEP_SLICING:              return "Slice the 3d model";
    case STEP_POST_SLICING:         return "Post-process slices";
    case STEP_PRE_PERIMETER:        return "Prepare perimeter generation";
    case STEP_PERIMETER:            return "Perimeter generation";
    case STEP_POST_PERIMETER:       return "Post-process perimeters";
    case STEP_SURFACE_GENERATION:   return "Generate surfaces";
    case STEP_SURFACE_TYPE:         return "Detect solid surfaces";
    case STEP_PRE_INFILL:           return "Prepare filling";
    case STEP_INFILL:               return "Fill surfaces";
    case STEP_POST_INFILL:          return "Post-process infill";
    case STEP_SUPPORT_DEMAND:       return "Detect support areas";
    case STEP_SUPPORT:              return "Create support extrusions";
    case STEP_PRE_GCODE:            return "Prepare gcode creation";
    case STEP_CHECK_CONFLICT:       return "Check extrusions conflicts";
    case STEP_ORDERING:             return "Ordering iland extrusions";
    case STEP_WIPETOWER:            return "Create wipetower";
    case STEP_SUPPORT_SPOT:         return "Detect curling areas";
    case STEP_LAYER_EXTRUSION_EDIT: return "Edit extrusions";
    case STEP_EXTRUSION_SIMPLIFICATION: return "Create arcs";
    case STEP_GCODE:                return "Create output file";
    case STEP_NONE:                 return "Nothing";
    case STEP_ANY:                  return "Many steps";
    case BRIDGE_DETECTOR:           return "Detect bridges areas";
    case INFILL_PATTERN:            return "Fill a surface";
    default:                        return wxString::Format("STEP_%u", unsigned(step));
    }
}

} // namespace

PluginConfigDialog::PluginConfigDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("Plugin configuration"), wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER, "plugin_config")
{
    build();
}

void PluginConfigDialog::build()
{
    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);

    wxStaticText *description = new wxStaticText(
        this, wxID_ANY,
        _L("Choose which loaded plugins will be active after the next restart."));
    main_sizer->Add(description, 0, wxEXPAND | wxALL, 10);

    wxScrolledWindow *scrolled = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                                      wxSize(70 * em_unit(), 24 * em_unit()),
                                                      wxVSCROLL);
    scrolled->SetScrollRate(0, em_unit());

    wxFlexGridSizer *grid = new wxFlexGridSizer(4, 8, 12);
    grid->AddGrowableCol(1, 1);

    grid->Add(new wxStaticText(scrolled, wxID_ANY, _L("Active")), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(scrolled, wxID_ANY, _L("Plugin")), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(scrolled, wxID_ANY, _L("Step")), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(scrolled, wxID_ANY, _L("Priority")), 0, wxALIGN_CENTER_VERTICAL);

    std::vector<Plugin *> plugins = Orchestrator::instance().registered_plugins();
    std::stable_sort(plugins.begin(), plugins.end(), [](const Plugin *lhs, const Plugin *rhs) {
        if (lhs->get_step() != rhs->get_step())
            return lhs->get_step() < rhs->get_step();
        return lhs->get_priority() < rhs->get_priority();
    });

    for (Plugin *plugin : plugins) {
        wxCheckBox *checkbox = new wxCheckBox(scrolled, wxID_ANY, wxEmptyString);
        checkbox->SetValue(Orchestrator::instance().is_plugin_active(plugin));

        grid->Add(checkbox, 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(new wxStaticText(scrolled, wxID_ANY, from_u8(plugin->get_id())), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(new wxStaticText(scrolled, wxID_ANY, step_name(plugin->get_step())), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(new wxStaticText(scrolled, wxID_ANY, wxString::Format("%d", plugin->get_priority())),
                  0, wxALIGN_CENTER_VERTICAL);

        m_rows.push_back({ plugin->get_id(), checkbox });
    }

    if (plugins.empty()) {
        grid->Add(new wxStaticText(scrolled, wxID_ANY, _L("No plugin is loaded.")), 0, wxALIGN_CENTER_VERTICAL);
        grid->AddSpacer(0);
        grid->AddSpacer(0);
        grid->AddSpacer(0);
    }

    scrolled->SetSizer(grid);
    main_sizer->Add(scrolled, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

    wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
    wxButton *save = new wxButton(this, wxID_OK, _L("Save and restart"));
    wxButton *cancel = new wxButton(this, wxID_CANCEL, _L("Cancel"));
    buttons->AddStretchSpacer();
    buttons->Add(save, 0, wxRIGHT, 6);
    buttons->Add(cancel, 0);
    main_sizer->Add(buttons, 0, wxEXPAND | wxALL, 10);

    save->Bind(wxEVT_BUTTON, &PluginConfigDialog::save_and_restart, this);
    cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_CANCEL); });

    SetSizerAndFit(main_sizer);
    CentreOnParent();
}

bool PluginConfigDialog::write_active_plugins(std::string &error_message) const
{
    if (!has_data_dir()) {
        error_message = "The configuration directory is not initialized.";
        return false;
    }

    const boost::filesystem::path config_path = active_plugin_config_path();
    try {
        boost::filesystem::create_directories(config_path.parent_path());
        boost::nowide::ofstream stream(config_path.string(), std::ios::out | std::ios::trunc);
        if (!stream) {
            error_message = "Cannot write " + config_path.string();
            return false;
        }

        stream << "[activated]\n";
        stream << "; Plugin ids enabled by the user.\n";
        for (const PluginRow &row : m_rows)
            if (row.checkbox != nullptr && row.checkbox->GetValue())
                stream << row.id << " = 1\n";
    } catch (const std::exception &error) {
        error_message = error.what();
        return false;
    }

    return true;
}

bool PluginConfigDialog::prepare_restart() const
{
    if (wxGetApp().plater() == nullptr)
        return true;

    const int saved_project = wxGetApp().plater()->save_project_if_dirty(
        format_wxstr(_L("Closing %1%. Current project is modified."), SLIC3R_APP_NAME));
    if (saved_project == wxID_CANCEL)
        return false;

    if (saved_project == wxID_NO && wxGetApp().plater()->is_presets_dirty())
        return wxGetApp().check_and_save_current_preset_changes(
            format_wxstr(_L("Closing %1%"), SLIC3R_APP_NAME),
            format_wxstr(_L("Closing %1% while some presets are modified."), SLIC3R_APP_NAME));

    return true;
}

void PluginConfigDialog::save_and_restart(wxCommandEvent &)
{
    if (!prepare_restart())
        return;

    std::string error_message;
    if (!write_active_plugins(error_message)) {
        show_error(this, error_message);
        return;
    }

    BOOST_LOG_TRIVIAL(info) << "Plugin activation configuration saved to '"
                            << active_plugin_config_path().string() << "'. Restarting.";
    EndModal(wxID_OK);
    start_new_slicer(nullptr, false);
    if (wxGetApp().mainframe != nullptr)
        wxGetApp().mainframe->Close(true);
}

void PluginConfigDialog::on_dpi_changed(const wxRect &)
{
    SetFont(wxGetApp().normal_font());
    msw_buttons_rescale(this, em_unit(), { wxID_OK, wxID_CANCEL });
    Fit();
    Refresh();
}

} // namespace Slic3r::GUI
