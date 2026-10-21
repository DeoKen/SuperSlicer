#include "CalibrationDialogs.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include <wx/button.h>
#include <wx/combobox.h>
#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <boost/filesystem.hpp>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/FileReader.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include "format.hpp"

namespace Slic3r {
namespace GUI {

// ------------------------------------------------------------------------------------------------------------------------
// CalibrationDialog
// ------------------------------------------------------------------------------------------------------------------------

CalibrationDialog::CalibrationDialog(const wxString &title, const wxString &explanation)
    : DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetFont(wxGetApp().normal_font());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    auto *text  = new wxStaticText(this, wxID_ANY, explanation);
    text->Wrap(50 * em_unit());
    sizer->Add(text, 0, wxALL, em_unit());
    m_grid = new wxFlexGridSizer(2, em_unit() / 2, em_unit());
    m_grid->AddGrowableCol(1);
    sizer->Add(m_grid, 0, wxEXPAND | wxLEFT | wxRIGHT, em_unit());
    SetSizer(sizer);
}

wxComboBox* CalibrationDialog::add_combo(const wxString &label, const wxString &tooltip, const std::vector<wxString> &choices, const wxString &value)
{
    m_grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
    auto *combo = new wxComboBox(this, wxID_ANY, value, wxDefaultPosition, wxSize(10 * em_unit(), -1), int(choices.size()), choices.data(), wxCB_DROPDOWN);
    combo->SetToolTip(tooltip);
    m_grid->Add(combo, 0, wxALIGN_CENTER_VERTICAL);
    return combo;
}

wxTextCtrl* CalibrationDialog::add_text(const wxString &label, const wxString &tooltip, const wxString &value)
{
    m_grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
    auto *text = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition, wxSize(40 * em_unit(), -1));
    text->SetToolTip(tooltip);
    m_grid->Add(text, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);
    return text;
}

void CalibrationDialog::finish_layout()
{
    auto *buttons  = new wxBoxSizer(wxHORIZONTAL);
    auto *generate = new wxButton(this, wxID_OK, _L("Generate"));
    auto *close    = new wxButton(this, wxID_CANCEL, _L("Close"));
    buttons->AddStretchSpacer();
    buttons->Add(generate, 0, wxRIGHT, em_unit());
    buttons->Add(close);
    GetSizer()->Add(buttons, 0, wxEXPAND | wxALL, em_unit());
    generate->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        if (this->generate())
            EndModal(wxID_OK);
    });
    generate->SetDefault();
    wxGetApp().UpdateDlgDarkUI(this);
    GetSizer()->SetSizeHints(this);
}

bool CalibrationDialog::get_number(wxWindow *field, const wxString &label, double &value)
{
    wxString str;
    if (auto *combo = dynamic_cast<wxComboBox*>(field))
        str = combo->GetValue();
    else if (auto *text = dynamic_cast<wxTextCtrl*>(field))
        str = text->GetValue();
    str.Replace(",", ".");
    str.Trim(true).Trim(false);
    if (! str.ToCDouble(&value)) {
        show_error(this, format_wxstr(_L("%1%: \"%2%\" is not a number."), label, str));
        return false;
    }
    return true;
}

void CalibrationDialog::on_dpi_changed(const wxRect &)
{
    GetSizer()->SetSizeHints(this);
    Refresh();
}

std::string CalibrationDialog::resource(const std::string &dir, const std::string &file)
{
    return (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / dir / file).string();
}

ModelVolume* CalibrationDialog::add_part(ModelObject *object, const std::string &file, const Vec3d &move, const Vec3d &scale)
{
    Model        model = FileReader::load_model(file);
    ModelVolume *added = nullptr;
    for (ModelObject *part_object : model.objects) {
        // The object was centered when it was loaded: move the part as the object was moved.
        Vec3d delta = Vec3d::Zero();
        if (object->origin_translation != Vec3d::Zero()) {
            part_object->center_around_origin();
            delta = object->origin_translation - part_object->origin_translation;
        }
        for (ModelVolume *volume : part_object->volumes) {
            volume->translate(delta + move);
            if (scale != Vec3d(1., 1., 1.))
                volume->scale(scale);
            added = object->add_volume(*volume, ModelVolumeType::MODEL_PART);
            added->name = boost::filesystem::path(file).filename().string();
        }
    }
    object->invalidate_bounding_box();
    return added;
}

// The current print, filament and printer settings (first extruder).
static DynamicPrintConfig current_config() { return wxGetApp().preset_bundle->full_config(); }

static double nozzle_diameter(const DynamicPrintConfig &config) { return config.opt<ConfigOptionFloats>("nozzle_diameter")->get_at(0); }

// New project (asks to save the current one). False if cancelled.
static bool start_calibration_project()
{
    if (wxGetApp().plater()->printer_technology() != ptFFF) {
        show_error(nullptr, _L("The calibration prints are for FFF printers."));
        return false;
    }
    return wxGetApp().plater()->new_project();
}

// Updates the plater after the objects were changed and slices.
static void finish_calibration_project(const std::vector<size_t> &objects)
{
    Plater *plater = wxGetApp().plater();
    for (size_t idx : objects)
        plater->model().objects[idx]->ensure_on_bed();
    plater->changed_objects(objects);
    wxGetApp().obj_list()->update_after_undo_redo();
    plater->reslice();
}

// Loads the settings into a tab as unsaved changes.
static void load_tab_config(Preset::Type type, const DynamicPrintConfig &config)
{
    wxGetApp().get_tab(type)->load_config(config);
    wxGetApp().plater()->on_config_change(config);
}

static std::vector<wxString> choices(std::initializer_list<const char*> values)
{
    std::vector<wxString> out;
    for (const char *value : values)
        out.emplace_back(value);
    return out;
}

// ------------------------------------------------------------------------------------------------------------------------
// Temperature tower
// ------------------------------------------------------------------------------------------------------------------------

class CalibrationTemperatureDialog : public CalibrationDialog
{
public:
    CalibrationTemperatureDialog()
        : CalibrationDialog(_L("Temperature calibration"),
            _L("Prints a tower with one floor per temperature: the hottest at the bottom, the coldest at the top, around the "
               "temperature of the filament settings. Each floor sets its temperature with M104 at its height. The temperatures "
               "from 180 to 285 °C that are a multiple of 5 are engraved on the floors.\n"
               "Pick the floor with the best surface, bridge and overhang and set its temperature in the filament settings."))
    {
        m_down = add_combo(_L("Floors below"), _L("Number of floors colder than the filament temperature."),
                           choices({ "0", "1", "2", "3", "4", "5", "6", "7" }), "2");
        m_up   = add_combo(_L("Floors above"), _L("Number of floors hotter than the filament temperature."),
                           choices({ "0", "1", "2", "3", "4", "5", "6", "7" }), "2");
        m_step = add_combo(_L("Step (°C)"), _L("Temperature difference between two floors. Only multiples of 5 are engraved."),
                           choices({ "5", "10", "15", "20" }), "10");
        finish_layout();
    }

protected:
    bool generate() override
    {
        double down, up, step;
        if (! get_number(m_down, _L("Floors below"), down) || ! get_number(m_up, _L("Floors above"), up) || ! get_number(m_step, _L("Step (°C)"), step))
            return false;
        const int nb_down = std::clamp(int(down), 0, 20);
        const int nb_up   = std::clamp(int(up), 0, 20);
        const int step_t  = std::max(1, int(step));

        DynamicPrintConfig config = current_config();
        const int filament_temperature = config.opt<ConfigOptionInts>("temperature")->get_at(0);
        if (filament_temperature <= 0) {
            show_error(this, _L("Set the temperature of the filament first."));
            return false;
        }
        if (! start_calibration_project())
            return false;

        Plater *plater = wxGetApp().plater();
        Model  &model  = plater->model();
        const std::string   item = resource("filament_temp", "Smart_compact_temperature_calibration_item.amf");
        std::vector<size_t> objs = plater->load_files(std::vector<std::string>{ item }, true, false);
        if (objs.size() != 1)
            return false;
        ModelObject *object = model.objects[objs.front()];

        // The model is made for a 0.4 mm nozzle: scale it with the nozzle.
        const double nozzle = nozzle_diameter(config);
        double       s      = nozzle / 0.4;
        if (s > 0.9 && s < 1.1)
            s = 1.;
        object->scale(Vec3d(s, s * 0.5, s));

        // From the hottest floor at the bottom.
        const int    hottest   = 5 * (filament_temperature / 5) + step_t * nb_up;
        const int    nb_floors = 1 + nb_up + nb_down;
        const Vec3d  label_offset(-3.75 * s, -2.7 * s, -2.45 * s);
        const auto   add_label = [&](const ModelVolume *floor, int temperature) {
            if (temperature < 180 || temperature > 285 || temperature % 5 != 0)
                return;
            ModelVolume *label = add_part(object, resource("filament_temp", "t" + std::to_string(temperature) + ".amf"), Vec3d::Zero(), Vec3d(s, s, 0.43 * s));
            if (label != nullptr)
                label->set_offset(floor->get_offset() + label_offset);
        };
        add_label(object->volumes.front(), hottest);
        for (int i = 1; i < nb_floors; ++ i) {
            const ModelVolume *floor = add_part(object, item, Vec3d(0., 0., i * 10. * s), Vec3d(s, s * 0.5, s));
            if (floor != nullptr)
                add_label(floor, hottest - i * step_t);
        }

        // The temperature of each floor: M104 at its height. The first floor from the second layer, after the first layer
        // temperature.
        const double layer_height = nozzle / 2.;
        const double first_layer  = config.get_abs_value("first_layer_height", layer_height);
        CustomGCode::Info &custom = model.custom_gcode_per_print_z();
        custom.mode = wxGetApp().extruders_edited_cnt() > 1 ? CustomGCode::MultiAsSingle : CustomGCode::SingleExtruder;
        custom.gcodes.push_back({ first_layer + layer_height, CustomGCode::Custom, -1, "", "M104 S" + std::to_string(hottest) + " ; floor 0 of the temperature tower" });
        for (int i = 1; i < nb_floors; ++ i)
            custom.gcodes.push_back({ i * 10. * s, CustomGCode::Custom, -1, "",
                                      "M104 S" + std::to_string(hottest - i * step_t) + " ; floor " + std::to_string(i) + " of the temperature tower" });

        // Object settings, as SuperSlicer.
        ModelConfig &oc = object->config;
        if (config.opt_float("brim_width") < nozzle * 8)
            oc.set_key_value("brim_width", new ConfigOptionFloat(nozzle * 8));
        oc.set_key_value("brim_ears",                     new ConfigOptionBool(false));
        oc.set_key_value("perimeters",                    new ConfigOptionInt(1));
        oc.set_key_value("extra_perimeters_on_overhangs", new ConfigOptionBool(true));
        oc.set_key_value("bottom_solid_layers",           new ConfigOptionInt(2));
        oc.set_key_value("top_solid_layers",              new ConfigOptionInt(3));
        oc.set_key_value("gap_fill_enabled",              new ConfigOptionBool(false));
        oc.set_key_value("layer_height",                  new ConfigOptionFloat(layer_height));
        oc.set_key_value("fill_density",                  new ConfigOptionPercent(7));
        oc.set_key_value("top_fill_pattern",              new ConfigOptionEnum<InfillPattern>(ipRectilinear));
        oc.set_key_value("ironing",                       new ConfigOptionBool(false));

        finish_calibration_project(objs);
        return true;
    }

private:
    wxComboBox *m_down { nullptr };
    wxComboBox *m_up   { nullptr };
    wxComboBox *m_step { nullptr };
};

// ------------------------------------------------------------------------------------------------------------------------
// CaliFlower (Vector 3D)
// ------------------------------------------------------------------------------------------------------------------------

static const char *CALIFLOWER_PATH_KEY = "calibration_califlower_path";

class CalibrationCaliFlowerDialog : public CalibrationDialog
{
public:
    CalibrationCaliFlowerDialog()
        : CalibrationDialog(_L("CaliFlower"),
            _L("Loads your CaliFlower model (Vector 3D, not included) with 2 perimeters and 20% grid infill as object settings, "
               "and switches the filament shrinkage compensation off (XY and Z 0%, an unsaved change of the filament settings), "
               "so that you measure the real shrinkage.\n"
               "Measure the print and enter the values in Vector 3D's calculator."))
    {
        m_path = add_text(_L("CaliFlower file"), _L("Location of your CaliFlower model file (STL, 3MF, OBJ or STEP). It is remembered."),
                          from_u8(wxGetApp().app_config->get(CALIFLOWER_PATH_KEY)));
        auto *browse = new wxButton(this, wxID_ANY, _L("Browse") + dots);
        GetSizer()->Add(browse, 0, wxALIGN_RIGHT | wxRIGHT | wxTOP, em_unit());
        browse->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
            wxFileDialog dlg(this, _L("Choose the CaliFlower model file"), wxEmptyString, wxEmptyString, file_wildcards(FT_MODEL),
                             wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if (dlg.ShowModal() == wxID_OK)
                m_path->SetValue(dlg.GetPath());
        });
        finish_layout();
    }

protected:
    bool generate() override
    {
        const std::string path = into_u8(m_path->GetValue().Trim(true).Trim(false));
        if (path.empty() || ! boost::filesystem::exists(path)) {
            show_error(this, _L("Choose your CaliFlower model file first."));
            return false;
        }
        wxGetApp().app_config->set(CALIFLOWER_PATH_KEY, path);
        if (! start_calibration_project())
            return false;

        Plater *plater = wxGetApp().plater();
        std::vector<size_t> objs = plater->load_files(std::vector<std::string>{ path }, true, false);
        if (objs.empty())
            return false;
        for (size_t idx : objs) {
            ModelConfig &oc = plater->model().objects[idx]->config;
            oc.set_key_value("perimeters",   new ConfigOptionInt(2));
            oc.set_key_value("fill_density", new ConfigOptionPercent(20));
            oc.set_key_value("fill_pattern", new ConfigOptionEnum<InfillPattern>(ipGrid));
        }

        // No shrinkage compensation (PrusaSlicer 0% = SuperSlicer 100%), for every filament.
        DynamicPrintConfig filament = wxGetApp().preset_bundle->filaments.get_edited_preset().config;
        for (const char *key : { "filament_shrinkage_compensation_xy", "filament_shrinkage_compensation_z" }) {
            auto *opt = filament.option<ConfigOptionPercents>(key, true);
            opt->values.assign(std::max<size_t>(1, opt->values.size()), 0.);
        }
        load_tab_config(Preset::TYPE_FILAMENT, filament);

        finish_calibration_project(objs);
        return true;
    }

private:
    wxTextCtrl *m_path { nullptr };
};

// ------------------------------------------------------------------------------------------------------------------------

void show_calibration_dialog(CalibrationType type)
{
    std::unique_ptr<CalibrationDialog> dlg;
    switch (type) {
    case CalibrationType::Temperature:     dlg = std::make_unique<CalibrationTemperatureDialog>(); break;
    case CalibrationType::CaliFlower:      dlg = std::make_unique<CalibrationCaliFlowerDialog>(); break;
    }
    dlg->ShowModal();
}

} // namespace GUI
} // namespace Slic3r
