#include "CalibrationDialogs.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>

#include <wx/arrstr.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/settings.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/wrapsizer.h>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/FileReader.hpp"
#include "libslic3r/GCode/BedKeepOut.hpp"
#include "libslic3r/GCode/PressureAdvancePattern.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/MultipleBeds.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PressureCalibration.hpp"
#include "libslic3r/Utils.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "GLCanvas3D.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include "format.hpp"
#include "../Utils/Process.hpp"

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
    m_explanation_text = explanation;
    m_explanation      = new wxStaticText(this, wxID_ANY, explanation);
    m_explanation->Wrap(40 * em_unit());
    sizer->Add(m_explanation, 0, wxALL, em_unit());
    m_grid = new wxFlexGridSizer(2, em_unit() / 2, em_unit());
    m_grid->AddGrowableCol(1);
    sizer->Add(m_grid, 0, wxEXPAND | wxLEFT | wxRIGHT, em_unit());
    SetSizer(sizer);
}

void CalibrationDialog::set_columns(int fields_per_row)
{
    m_grid->SetCols(2 * fields_per_row);
    m_grid->RemoveGrowableCol(1);
}

// A label in the grid, apart from the field before it on the same row.
void CalibrationDialog::add_label(const wxString &label)
{
    const bool first_column = m_grid->GetChildren().size() % size_t(m_grid->GetCols()) == 0;
    m_grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | (first_column ? 0 : wxLEFT), 2 * em_unit());
}

wxComboBox* CalibrationDialog::add_combo(const wxString &label, const wxString &tooltip, const std::vector<wxString> &choices, const wxString &value)
{
    add_label(label);
    auto *combo = new wxComboBox(this, wxID_ANY, value, wxDefaultPosition, wxSize(10 * em_unit(), -1), int(choices.size()), choices.data(), wxCB_DROPDOWN);
    combo->SetToolTip(tooltip);
    m_grid->Add(combo, 0, wxALIGN_CENTER_VERTICAL);
    return combo;
}

wxTextCtrl* CalibrationDialog::add_text(const wxString &label, const wxString &tooltip, const wxString &value, bool wide)
{
    add_label(label);
    auto *text = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition, wxSize((wide ? 40 : 8) * em_unit(), -1));
    text->SetToolTip(tooltip);
    m_grid->Add(text, wide ? 1 : 0, (wide ? wxEXPAND : 0) | wxALIGN_CENTER_VERTICAL);
    return text;
}

void CalibrationDialog::finish_layout(const wxString &ok_label)
{
    auto *buttons  = new wxBoxSizer(wxHORIZONTAL);
    auto *generate = new wxButton(this, wxID_OK, ok_label.empty() ? _L("Generate") : ok_label);
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
    // The explanation as wide as the fields under it (no empty space beside it).
    const int width = GetSizer()->CalcMin().x - 2 * em_unit();
    if (width > 40 * em_unit()) {
        m_explanation->SetLabel(m_explanation_text);
        m_explanation->Wrap(width);
    }
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
    return calibration_add_part(object, file, move, scale);
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

// Updates the plater after the objects were changed, arranges (if asked) and slices (unless asked not to).
static void finish_calibration_project(const std::vector<size_t> &objects, bool arrange = false, bool slice = true)
{
    Plater *plater = wxGetApp().plater();
    for (size_t idx : objects)
        plater->model().objects[idx]->ensure_on_bed();
    plater->changed_objects(objects);
    wxGetApp().obj_list()->update_after_undo_redo();
    if (arrange) {
        plater->arrange(false);
        plater->get_ui_job_worker().wait_for_current_job(20000);
    }
    if (slice)
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

// The cone of a floor is a shell of its own: a part of its own, round (scale s), while the floor is at half thickness. Squashed
// with the floor its tip is thinner than a line and prints as dots in the air.
static void round_cone(ModelObject *object, ModelVolume *floor, double s)
{
    std::vector<TriangleMesh> shells = floor->mesh().split();
    if (shells.size() != 2)
        return;
    if (shells[0].volume() < shells[1].volume())
        std::swap(shells[0], shells[1]);
    ModelVolume *cone = object->add_volume(*floor, std::move(shells[1]));
    cone->set_scaling_factor(Y, s);
    cone->name = "cone";
    floor->set_mesh(std::move(shells[0]));
    floor->calculate_convex_hull();
    object->invalidate_bounding_box();
}

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
        ModelVolume *first = object->volumes.front();
        add_label(first, hottest);
        round_cone(object, first, s);
        for (int i = 1; i < nb_floors; ++ i) {
            ModelVolume *floor = add_part(object, item, Vec3d(0., 0., i * 10. * s), Vec3d(s, s * 0.5, s));
            if (floor != nullptr) {
                add_label(floor, hottest - i * step_t);
                round_cone(object, floor, s);
            }
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
// Pressure advance pattern (Ellis)
// ------------------------------------------------------------------------------------------------------------------------

// The fan of an external perimeter on a long layer (CoolingBuffer): its own fan speed, the perimeter one, the default fan
// speed, else min_fan_speed if the fan is always on.
static int external_perimeter_fan(const DynamicPrintConfig &config)
{
    for (const char *key : { "external_perimeter_fan_speed", "perimeter_fan_speed", "default_fan_speed" })
        if (const int fan = config.opt<ConfigOptionInts>(key)->get_at(0); fan >= 0)
            return fan;
    return config.opt<ConfigOptionBools>("fan_always_on")->get_at(0) ? config.opt<ConfigOptionInts>("min_fan_speed")->get_at(0) : 0;
}

class CalibrationPressureAdvanceDialog : public CalibrationDialog
{
public:
    CalibrationPressureAdvanceDialog()
        : CalibrationDialog(_L("Pressure advance calibration"),
            _L("Ellis' pressure advance pattern: chevrons of a few walls, one per pressure advance value, with the values printed "
               "next to them. The G-code is written directly (not sliced) from the printer, filament and print settings: bed "
               "(centered), temperatures, retraction, layer heights, start / end G-code and Klipper speed mode. It is saved and "
               "opened in the G-code viewer. Klipper only.\n"
               "Print it and pick the pattern with the sharpest corners and no gaps or bulges at the ends of the walls; set that "
               "value with SET_PRESSURE_ADVANCE in your Klipper config or the filament start G-code."))
    {
        const DynamicPrintConfig config = current_config();
        const int    fan          = external_perimeter_fan(config);
        const bool   fan_off      = config.opt<ConfigOptionInts>("disable_fan_first_layers")->get_at(0) > 0;
        const auto   number       = [](double v) { return wxString::FromCDouble(v); };

        m_pa_start   = add_text(_L("Start value"), _L("Pressure advance of the first pattern."), "0");
        m_pa_end     = add_text(_L("End value"), _L("Pressure advance of the last pattern."), "0.08");
        m_pa_step    = add_text(_L("Step"), _L("Pressure advance difference between two patterns."), "0.005");
        m_speed      = add_text(_L("Print speed (mm/s)"), _L("Speed of the pattern walls."), "100");
        m_first_speed = add_text(_L("First layer speed (mm/s)"), _L("Speed of the first layer, the anchor and the numbers. From the print settings."),
                                 number(config.get_abs_value("first_layer_speed", 30.)));
        m_accel      = add_text(_L("Acceleration (mm/s²)"), _L("SET_VELOCITY_LIMIT ACCEL= before the pattern. 0: no command (the Klipper "
                                                              "config or the speed mode applies). Calibrate at the acceleration you print with."), "0");
        m_fan        = add_text(_L("Fan (%)"), _L("From the second layer. From the filament settings: the fan of an external perimeter on a long layer."),
                                number(fan));
        m_first_fan  = add_text(_L("First layer fan (%)"), _L("0 when the filament disables the fan on the first layers."), number(fan_off ? 0 : fan));
        m_layers     = add_text(_L("Layers"), _L("Number of layers of the pattern."), "4");
        m_walls      = add_text(_L("Walls"), _L("Number of walls of each chevron."), "3");
        m_side       = add_text(_L("Side length (mm)"), _L("Length of one side of a chevron."), "30");
        m_spacing    = add_text(_L("Spacing (mm)"), _L("Gap between two patterns."), "2");
        m_angle      = add_text(_L("Corner angle (°)"), _L("Angle of the chevron corners."), "90");
        m_direction  = add_combo(_L("Print direction (°)"), _L("Rotation of the whole print (clockwise) around the bed center."),
                                 choices({ "0", "45", "90", "135", "180", "225", "270", "315" }), "0");
        m_anchor     = add_combo(_L("Anchor"), _L("Frame: perimeters around the patterns on the first layer. Layer: a full first layer "
                                                  "under the patterns. None: the patterns start on the bed."),
                                 { _L("Frame"), _L("Layer"), _L("None") }, _L("Frame"));
        m_anchor->SetEditable(false);
        m_numbers    = add_combo(_L("Numbers"), _L("Print the value next to every other pattern."), { _L("Yes"), _L("No") }, _L("Yes"));
        m_numbers->SetEditable(false);
        finish_layout();
    }

protected:
    bool generate() override
    {
        PressureAdvancePatternParams p;
        double layers, walls, fan, first_fan;
        if (! get_number(m_pa_start, _L("Start value"), p.pa_start) || ! get_number(m_pa_end, _L("End value"), p.pa_end) ||
            ! get_number(m_pa_step, _L("Step"), p.pa_step) || ! get_number(m_speed, _L("Print speed (mm/s)"), p.speed) ||
            ! get_number(m_first_speed, _L("First layer speed (mm/s)"), p.first_layer_speed) ||
            ! get_number(m_accel, _L("Acceleration (mm/s²)"), p.acceleration) || ! get_number(m_fan, _L("Fan (%)"), fan) ||
            ! get_number(m_first_fan, _L("First layer fan (%)"), first_fan) || ! get_number(m_layers, _L("Layers"), layers) ||
            ! get_number(m_walls, _L("Walls"), walls) || ! get_number(m_side, _L("Side length (mm)"), p.side_length) ||
            ! get_number(m_spacing, _L("Spacing (mm)"), p.spacing) || ! get_number(m_angle, _L("Corner angle (°)"), p.corner_angle) ||
            ! get_number(m_direction, _L("Print direction (°)"), p.print_dir))
            return false;
        p.layers          = int(layers);
        p.wall_count      = int(walls);
        p.fan             = int(fan);
        p.first_layer_fan = int(first_fan);
        p.anchor          = m_anchor->GetSelection() == 1 ? PressureAdvancePatternParams::Anchor::Layer :
                            m_anchor->GetSelection() == 2 ? PressureAdvancePatternParams::Anchor::None : PressureAdvancePatternParams::Anchor::Frame;
        p.numbers         = m_numbers->GetSelection() != 1;

        std::string gcode;
        try {
            gcode = pressure_advance_pattern_gcode(current_config(), p);
        } catch (const std::exception &ex) {
            show_error(this, from_u8(ex.what()));
            return false;
        }

        AppConfig  &app_config = *wxGetApp().app_config;
        wxFileDialog dlg(this, _L("Save the pressure advance pattern as:"), from_u8(app_config.get_last_output_dir("", false)),
                         "pa_pattern.gcode", file_wildcards(FT_GCODE), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() != wxID_OK)
            return false;
        const wxString path = dlg.GetPath();
        {
            boost::nowide::ofstream f(into_u8(path), std::ios::binary);
            f << gcode;
            if (! f.good()) {
                show_error(this, format_wxstr(_L("Failed to write %1%."), path));
                return false;
            }
        }
        app_config.update_last_output_dir(boost::filesystem::path(into_u8(path)).parent_path().string(), false);
        start_new_gcodeviewer(&path);
        return true;
    }

private:
    wxTextCtrl *m_pa_start { nullptr }, *m_pa_end { nullptr }, *m_pa_step { nullptr }, *m_speed { nullptr }, *m_first_speed { nullptr };
    wxTextCtrl *m_accel { nullptr }, *m_fan { nullptr }, *m_first_fan { nullptr }, *m_layers { nullptr }, *m_walls { nullptr };
    wxTextCtrl *m_side { nullptr }, *m_spacing { nullptr }, *m_angle { nullptr };
    wxComboBox *m_direction { nullptr }, *m_anchor { nullptr }, *m_numbers { nullptr };
};

// ------------------------------------------------------------------------------------------------------------------------
// Pressure calibration (SuperSlicer's CalibrationPressureAdvDialog): 90° bends, each a part with its own value as per region
// G-code, sliced in the plater.
// ------------------------------------------------------------------------------------------------------------------------

// Loads the tests in a new project (asks to save the current one), with the settings in the print and filament settings
// (unsaved changes), and slices. edit_filament: more changes to the filament settings; ids: the number of each test on its frame
// (empty: 0, 1, 2...); placements: where each frame goes (calibration_layout(), the beds of the plater) instead of arranging them.
// False if nothing was done.
static bool load_pressure_calibration(wxWindow *parent, const std::vector<PressureCalibrationTest> &tests, const PressureCalibrationSettings &settings,
                                      const std::function<void(DynamicPrintConfig&)> &edit_filament = {}, const std::vector<size_t> &ids = {},
                                      const std::vector<CalibrationPlacement> *placements = nullptr)
{
    const auto id_of = [&ids](size_t i) { return ids.empty() ? i : ids[i]; };
    // The settings into the print and filament settings (unsaved changes), the print ones also as SuperSlicer changes them.
    DynamicPrintConfig print    = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    DynamicPrintConfig filament = wxGetApp().preset_bundle->filaments.get_edited_preset().config;
    DynamicPrintConfig config   = current_config();
    try {
        pressure_calibration_apply_settings(print, settings);
        pressure_calibration_apply_settings(filament, settings);
        pressure_calibration_apply_settings(config, settings);
    } catch (const std::exception &ex) {
        show_error(parent, from_u8(ex.what()));
        return false;
    }
    pressure_calibration_print_config(print);
    pressure_calibration_print_config(config);
    if (edit_filament)
        edit_filament(filament);
    // The tests set their own pressure advance (per region G-code): no pressure advance of the filament (doc section 46).
    for (const char *key : { "enable_pressure_advance", "adaptive_pressure_advance" }) {
        auto *opt = filament.option<ConfigOptionBools>(key, true);
        opt->values.assign(std::max<size_t>(1, opt->values.size()), 0);
    }
    const double nozzle = nozzle_diameter(config);
    const bool   bends  = std::any_of(tests.begin(), tests.end(), [&settings](const PressureCalibrationTest &t) {
        return pressure_calibration_wall_objects(settings, t) == 0; });
    if (bends && pressure_calibration_bend_file(resources_dir(), nozzle).empty()) {
        show_error(parent, format_wxstr(_L("There is no 90° bend model for a %1% mm nozzle (0.1 to 2.0 mm by 0.1 mm)."), nozzle));
        return false;
    }
    if (! start_calibration_project())
        return false;

    Plater *plater = wxGetApp().plater();
    Model  &model  = plater->model();
    // Per test: the base (frame, numbers) and the objects of the walls of Ellis' chevrons, all from the base plate.
    size_t nb_objects = 0;
    for (const PressureCalibrationTest &t : tests)
        nb_objects += 1 + pressure_calibration_wall_objects(settings, t);
    std::vector<size_t> objs = plater->load_files(std::vector<std::string>(nb_objects, pressure_calibration_base_file(resources_dir())), true, false);
    if (objs.size() != nb_objects)
        return false;
    bool slice = true;
    std::vector<ModelObject*>              frames;
    std::vector<std::vector<ModelObject*>> walls;
    for (size_t id_item = 0, i = 0; id_item < tests.size(); ++ id_item) {
        frames.emplace_back(model.objects[objs[i ++]]);
        walls.emplace_back();
        for (size_t k = 0; k < pressure_calibration_wall_objects(settings, tests[id_item]); ++ k)
            walls.back().emplace_back(model.objects[objs[i ++]]);
        if (! pressure_calibration_build(*frames.back(), config, settings, tests[id_item], id_of(id_item), resources_dir()))
            slice = false;
    }
    const bool placed = placements != nullptr && placements->size() == tests.size();
    if (placed) {
        // Each frame on its bed, turned if so, its outline's minimum corner at its place (user 2026-11-05: as many per plate as
        // possible), then the walls on their frame, all before the plater hears of any change: it must never slice the objects
        // of the walls while they are only the base plate (an object without layers crashed the overhangs cut, 2026-11-05).
        const BoundingBoxf bed(config.opt<ConfigOptionPoints>("bed_shape")->values);
        for (size_t id_item = 0; id_item < tests.size(); ++ id_item) {
            const CalibrationPlacement &place = (*placements)[id_item];
            ModelInstance              *inst  = frames[id_item]->instances.front();
            Geometry::Transformation    trafo = inst->get_transformation();
            trafo.set_rotation(Vec3d(0., 0., place.turned ? 0.5 * PI : 0.));
            inst->set_transformation(trafo);
            frames[id_item]->invalidate_bounding_box();
            const BoundingBoxf3 bb     = frames[id_item]->instance_bounding_box(0);
            const Vec2d         target = bed.min + place.position + to_2d(s_multiple_beds.get_bed_translation(place.bed));
            inst->set_offset(inst->get_offset() + Vec3d(target.x() - bb.min.x(), target.y() - bb.min.y(), 0.));
            frames[id_item]->invalidate_bounding_box();
            for (ModelObject *wall : walls[id_item])
                wall->instances.front()->set_transformation(inst->get_transformation());
            if (! walls[id_item].empty())
                pressure_calibration_build_walls(walls[id_item], config, settings, tests[id_item], id_of(id_item));
        }
    }
    // How the test was made, as comments at the start of the first layer (Ellis writes them at the top of his G-code).
    CustomGCode::Info &custom = model.custom_gcode_per_print_z();
    custom.mode = wxGetApp().extruders_edited_cnt() > 1 ? CustomGCode::MultiAsSingle : CustomGCode::SingleExtruder;
    custom.gcodes.push_back({ settings.first_layer_height, CustomGCode::Custom, -1, "", pressure_calibration_summary(config, settings, tests) });

    load_tab_config(Preset::TYPE_PRINT, print);
    load_tab_config(Preset::TYPE_FILAMENT, filament);

    if (placed) {
        Plater *p = wxGetApp().plater();
        finish_calibration_project(objs, false, false);
        // The beds of the plater from where the objects are.
        s_multiple_beds.update_shown_beds(p->model(), p->build_volume());
        p->canvas3D()->check_volumes_outside_state();
    } else {
        // Arrange the frames while the objects of the walls are still the empty base plate, then the walls on their frame (same
        // instance transformation, same coordinates). CheckAll: not sliced, so that the values can be typed in first.
        finish_calibration_project(objs, true, false);
        for (size_t id_item = 0; id_item < tests.size(); ++ id_item)
            if (! walls[id_item].empty()) {
                for (ModelObject *wall : walls[id_item])
                    wall->instances.front()->set_transformation(frames[id_item]->instances.front()->get_transformation());
                pressure_calibration_build_walls(walls[id_item], config, settings, tests[id_item], id_of(id_item));
            }
    }
    finish_calibration_project(objs, false, slice);
    return true;
}

class CalibrationPressureDialog : public CalibrationDialog
{
public:
    CalibrationPressureDialog()
        : CalibrationDialog(_L("Pressure calibration"),
            _L("SuperSlicer's pressure calibration with Ellis' chevrons: a row of chevrons in a frame, each with its own pressure "
               "(linear) advance as per region G-code, from the start to the end value; every second value is engraved next to its "
               "chevron. Each wall is an open line as in Ellis' pattern (one object per wall). The frame and numbers print with the "
               "first layer value. Generate loads the print in the plater and slices it, so you can "
               "check it in the preview (layers, moves, G-code).\n"
               "The settings below are Ellis' defaults (his pressure advance tool): Generate puts them in the print and filament "
               "settings (unsaved changes) and writes them as comments at the start of the first layer (preview: Custom G-codes). "
               "The printer and filament settings (temperatures, retraction, travel) are yours.\n"
               "\"CheckAll\": one bend per extrusion role, not sliced: type the value in the per region G-code of each bend first.\n"
               "Pick the sharpest bend without a bulge or a gap and set the value in the filament start G-code."))
    {
        const auto flavor = current_config().opt_enum<GCodeFlavor>("gcode_flavor");
        const PressureCalibrationSettings ellis;
        const auto number = [](double v) { return wxString::FromCDouble(v); };
        set_columns(4);
        // Ellis' tool: "Use your external perimeter speed from your slicer settings - if it's reasonably fast (>60mm/s)", "Match
        // your thickest perimeter" (line width). The speed of the selected print profile (user 2026-10-26: the speed they print
        // at), his 100 mm/s if it has none; line width 100 % (user, the widths of the user's profiles are the nozzle size).
        const double ext_speed = current_config().get_abs_value("external_perimeter_speed");
        m_speed       = add_text(_L("Print speed (mm/s)"), _L("Speed of the bends. Default: the external perimeter speed of the selected print "
                                                              "profile, as Ellis recommends (\"if it's reasonably fast (>60mm/s)\"; if the "
                                                              "results are hard to read, try a faster speed). His tool's default: 100."),
                                 number(ext_speed > 0. ? std::round(ext_speed) : ellis.speed));
        m_line_ratio  = add_text(_L("Line width (% of the nozzle)"), _L("Width of the bends. Ellis: match your thickest perimeter (internal or "
                                                                        "external). His tool's default: 112.5."), number(100.));
        m_layers      = add_text(_L("Layers"), _L("Height of the bends in layers, with the first one (Ellis: 4)."), number(ellis.layers));
        m_walls       = add_text(_L("Walls"), _L("Number of walls (open lines) of each chevron (Ellis: 3)."), number(ellis.wall_count));
        m_side        = add_text(_L("Side length (mm)"), _L("Length of one side of a chevron (Ellis: 30)."), number(ellis.side_length));
        m_spacing     = add_text(_L("Spacing (mm)"), _L("Gap between two chevrons (Ellis: 2)."), number(ellis.pattern_spacing));
        m_first_speed = add_text(_L("First layer speed (mm/s)"), _L("First layer, and the numbers (Ellis: 30). Print settings."), number(ellis.first_layer_speed));
        m_layer       = add_text(_L("Layer height (mm)"), _L("Ellis: 0.2. Print settings."), number(ellis.layer_height));
        m_first_layer = add_text(_L("First layer height (mm)"), _L("Ellis: 0.25. Print settings."), number(ellis.first_layer_height));
        m_fan         = add_text(_L("Fan (%)"), _L("From the second layer (Ellis: 100). Filament settings: default fan speed, no ramp, no "
                                                   "layer time cooling (no fan speed-up, no slowdown)."), number(ellis.fan));
        m_first_fan   = add_text(_L("First layer fan (%)"), _L("Ellis: 0. 0 (off on the first layer) or the same as the fan: PrusaSlicer has no "
                                                               "fan speed of its own for the first layer."), number(ellis.first_layer_fan));
        m_accel       = add_text(_L("Acceleration (mm/s²)"), _L("Every acceleration of the print settings. 0: no acceleration command, the "
                                                                "Klipper config or the speed mode applies (Ellis: disabled)."), number(ellis.acceleration));
        m_prefix = flavor == gcfMarlinFirmware ? " LA " : (flavor == gcfKlipper || flavor == gcfRepRapFirmware) ? " PA " : "";
        if (m_prefix.empty()) {
            auto *text = new wxStaticText(this, wxID_ANY, _L("unsupported firmware type"));
            text->SetForegroundColour(*wxRED);
            GetSizer()->Add(text, 0, wxALL, em_unit());
        } else {
            m_nb_tests = new wxComboBox(this, wxID_ANY, "1", wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY);
            for (int i = 1; i <= 10; ++ i)
                m_nb_tests->Append(std::to_string(i));
            m_nb_tests->SetSelection(0);
            m_nb_tests->SetToolTip(_L("Select the number of tests to generate, max 6 is recommended due to bed size limits"));
            auto *line = new wxBoxSizer(wxHORIZONTAL);
            line->Add(new wxStaticText(this, wxID_ANY, _L("Number of" + m_prefix + "tests: ")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em_unit() / 2);
            line->Add(m_nb_tests, 0, wxALIGN_CENTER_VERTICAL);
            GetSizer()->Add(line, 0, wxALL, em_unit());
            m_rows_sizer = new wxBoxSizer(wxVERTICAL);
            GetSizer()->Add(m_rows_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, em_unit());
            add_rows(1);
            m_nb_tests->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &) {
                const int count = m_nb_tests->GetSelection() + 1;
                if (count > int(m_rows.size()))
                    add_rows(count - int(m_rows.size()));
                while (int(m_rows.size()) > count) {
                    m_rows_sizer->Detach(m_rows.back().sizer);
                    m_rows.back().sizer->Clear(true);
                    delete m_rows.back().sizer;
                    m_rows.pop_back();
                }
                wxGetApp().UpdateDlgDarkUI(this);
                GetSizer()->SetSizeHints(this);
                Layout();
            });
        }
        finish_layout();
    }

protected:
    struct Row {
        wxComboBox *first       { nullptr };
        wxComboBox *start       { nullptr };
        wxComboBox *end         { nullptr };
        wxComboBox *increment   { nullptr };
        wxComboBox *role        { nullptr };
        wxCheckBox *smooth_time { nullptr };
        wxBoxSizer *sizer       { nullptr };
    };

    void add_rows(int count)
    {
        std::vector<wxString> roles;
        for (const std::string &role : pressure_calibration_roles())
            roles.emplace_back(role);
        roles.emplace_back("CheckAll");
        // Start at ExternalPerimeter, then the role after the one of the last row.
        int selection = 2;
        if (! m_rows.empty())
            selection = std::min(m_rows.back().role->GetSelection() + 1, int(roles.size()) - 1);
        const wxSize size(7 * em_unit(), -1);
        for (int i = 0; i < count; ++ i) {
            Row   row;
            auto *sizer = new wxBoxSizer(wxHORIZONTAL);
            const auto add = [this, sizer](const wxString &label, wxWindow *field) {
                sizer->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | wxALL, em_unit() / 2);
                sizer->Add(field, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em_unit());
            };
            const auto combo = [this, &size](const std::vector<wxString> &values, int selection, const wxString &tooltip) {
                auto *c = new wxComboBox(this, wxID_ANY, values[selection], wxDefaultPosition, size, int(values.size()), values.data());
                c->SetToolTip(tooltip);
                return c;
            };
            row.first     = combo(choices({ "0.0", "0.025", "0.030", "0.035", "0.040", "0.045", "0.050" }), 0,
                                  _L("Select the" + m_prefix + "value to be used for the first layer only (base, borders and numbers)."));
            row.start     = combo(choices({ "0.0", "0.010", "0.020", "0.030", "0.040", "0.050" }), 0,
                                  _L("Select the starting" + m_prefix + "value to be used.\n (you can manually type in values!)"));
            row.end       = combo(choices({ "0.02", "0.04", "0.06", "0.08", "0.10", "0.15", "0.20", "0.30", "0.40", "0.50" }), 3,
                                  _L("Select the ending" + m_prefix + "value to be used.\n (you can manually type in values!)"));
            row.increment = combo(choices({ "0.0010", "0.0025", "0.0035", "0.005", "0.006", "0.007", "0.01", "0.1" }), 3,
                                  _L("Select the incremental value.\n (you can manually type in values!)"));
            row.role      = new wxComboBox(this, wxID_ANY, roles[selection], wxDefaultPosition, wxSize(14 * em_unit(), -1), int(roles.size()), roles.data(), wxCB_READONLY);
            row.role->SetSelection(selection);
            row.role->SetToolTip(_L("Select the extrusion role you want to generate a calibration for"));
            add(_L("First Layers" + m_prefix + "value: "), row.first);
            add(_L("Start value: "), row.start);
            add(_L("End value: "), row.end);
            add(_L("Increment by: "), row.increment);
            add(_L("Extrusion role: "), row.role);
            if (m_prefix == " PA ") {
                row.smooth_time = new wxCheckBox(this, wxID_ANY, wxEmptyString);
                row.smooth_time->SetToolTip(_L("This parameter defines the duration over which extruder velocity changes are averaged, "
                    "helping to smooth out rapid changes in extrusion pressure. Shorter times (e.g., 0.01 seconds) are beneficial for "
                    "fast printing, while longer times (e.g., 0.4 seconds) are better for slower printing. The default value is 0.04 seconds."));
                add(_L("Smooth time: "), row.smooth_time);
            }
            row.sizer = sizer;
            m_rows_sizer->Add(sizer, 0, wxALL, 2);
            m_rows.emplace_back(row);
            if (++ selection >= int(roles.size()))
                selection = 0;
        }
    }

    bool generate() override
    {
        if (m_prefix.empty()) {
            show_error(this, _L("unsupported firmware type"));
            return false;
        }
        std::vector<PressureCalibrationTest> tests;
        for (const Row &row : m_rows) {
            double first, start, end, increment;
            if (! get_number(row.first, _L("First layer value"), first) || ! get_number(row.start, _L("Start value"), start) ||
                ! get_number(row.end, _L("End value"), end) || ! get_number(row.increment, _L("Increment"), increment))
                return false;
            if (increment <= 0. || end < start) {
                show_error(this, _L("The increment must be above 0 and the end value at least the start value."));
                return false;
            }
            PressureCalibrationTest test { first, pressure_calibration_values(start, end, increment), into_u8(row.role->GetValue()), row.smooth_time && row.smooth_time->GetValue() };
            if (test.values.size() > 100) {
                show_error(this, format_wxstr(_L("%1% values: too many. Use a larger increment."), test.values.size()));
                return false;
            }
            if (test.values.size() < 2 && test.role != "CheckAll") {
                show_error(this, _L("At least 2 values: the end value must be at least one increment above the start value."));
                return false;
            }
            tests.emplace_back(std::move(test));
        }

        PressureCalibrationSettings settings;
        double layers, fan, first_fan, wall_count;
        if (! get_number(m_speed, _L("Print speed (mm/s)"), settings.speed) || ! get_number(m_line_ratio, _L("Line width"), settings.line_ratio) ||
            ! get_number(m_layers, _L("Layers"), layers) || ! get_number(m_first_speed, _L("First layer speed (mm/s)"), settings.first_layer_speed) ||
            ! get_number(m_layer, _L("Layer height (mm)"), settings.layer_height) ||
            ! get_number(m_first_layer, _L("First layer height (mm)"), settings.first_layer_height) ||
            ! get_number(m_fan, _L("Fan (%)"), fan) || ! get_number(m_first_fan, _L("First layer fan (%)"), first_fan) ||
            ! get_number(m_accel, _L("Acceleration (mm/s²)"), settings.acceleration) || ! get_number(m_walls, _L("Walls"), wall_count) ||
            ! get_number(m_side, _L("Side length (mm)"), settings.side_length) || ! get_number(m_spacing, _L("Spacing (mm)"), settings.pattern_spacing))
            return false;
        settings.wall_count      = int(wall_count);
        settings.layers          = int(layers);
        settings.fan             = int(fan);
        settings.first_layer_fan = int(first_fan);
        return load_pressure_calibration(this, tests, settings);
    }

private:
    std::string      m_prefix;
    wxTextCtrl      *m_speed { nullptr }, *m_line_ratio { nullptr }, *m_layers { nullptr }, *m_first_speed { nullptr };
    wxTextCtrl      *m_layer { nullptr }, *m_first_layer { nullptr }, *m_fan { nullptr }, *m_first_fan { nullptr }, *m_accel { nullptr };
    wxTextCtrl      *m_walls { nullptr }, *m_side { nullptr }, *m_spacing { nullptr };
    wxComboBox      *m_nb_tests   { nullptr };
    wxBoxSizer      *m_rows_sizer { nullptr };
    std::vector<Row> m_rows;
};

// ------------------------------------------------------------------------------------------------------------------------
// Adaptive pressure advance (OrcaSlicer's adaptive PA model): Ellis' chevrons per speed and acceleration, then the best value of
// each test into the filament's adaptive_pressure_advance_model.
// ------------------------------------------------------------------------------------------------------------------------

// The tests of the calibrations (speed, acceleration, flow, nozzle, typed value), per filament (user 2026-11-05): section
// ADAPTIVE_PA_SECTION of PrusaSlicer.ini, one key per filament preset. Before: one filament only, in the two keys below
// (moved into the section the first time).
static const char *ADAPTIVE_PA_SECTION      = "adaptive_pa_calibrations";
static const char *ADAPTIVE_PA_TESTS_KEY    = "calibration_adaptive_pa_tests";
static const char *ADAPTIVE_PA_FILAMENT_KEY = "calibration_adaptive_pa_filament";

// The key of a filament in the section: its name, without what an ini key can't hold.
static std::string adaptive_pa_key(std::string filament)
{
    std::replace(filament.begin(), filament.end(), '=', '_');
    if (! filament.empty() && (filament.front() == '[' || filament.front() == ';' || filament.front() == '#'))
        filament.insert(filament.begin(), '_');
    return filament;
}

static std::vector<AdaptivePATest> adaptive_pa_kept(const std::string &filament)
{
    AppConfig *config = wxGetApp().app_config;
    if (const std::string old = config->get(ADAPTIVE_PA_TESTS_KEY); ! old.empty()) {
        const std::string old_filament = config->get(ADAPTIVE_PA_FILAMENT_KEY);
        if (! old_filament.empty() && config->get(ADAPTIVE_PA_SECTION, adaptive_pa_key(old_filament)).empty())
            config->set(ADAPTIVE_PA_SECTION, adaptive_pa_key(old_filament), old);
        config->set(ADAPTIVE_PA_TESTS_KEY, "");
        config->set(ADAPTIVE_PA_FILAMENT_KEY, "");
    }
    return adaptive_pa_tests_deserialize(config->get(ADAPTIVE_PA_SECTION, adaptive_pa_key(filament)));
}

static void adaptive_pa_keep(const std::string &filament, const std::vector<AdaptivePATest> &tests)
{
    wxGetApp().app_config->set(ADAPTIVE_PA_SECTION, adaptive_pa_key(filament), adaptive_pa_tests_serialize(tests));
}

static std::string selected_filament() { return wxGetApp().preset_bundle->filaments.get_edited_preset().name; }

static wxString number_list(const std::vector<double> &values)
{
    wxString out;
    for (double v : values)
        out += (out.empty() ? "" : ", ") + wxString::FromCDouble(v);
    return out;
}

// A list of numbers separated by commas, semicolons or spaces (a decimal point only). False and an error message if a value
// isn't a number above 0.
static bool parse_number_list(wxWindow *parent, const wxString &label, const wxString &text, std::vector<double> &values)
{
    values.clear();
    wxString str = text;
    str.Replace(";", " ");
    str.Replace(",", " ");
    for (wxString token : wxSplit(str, ' ', '\0')) {
        token.Trim(true).Trim(false);
        if (token.empty())
            continue;
        double value;
        if (! token.ToCDouble(&value) || value <= 0.) {
            show_error(parent, format_wxstr(_L("%1%: \"%2%\" is not a number above 0."), label, token));
            return false;
        }
        values.emplace_back(value);
    }
    if (values.empty()) {
        show_error(parent, format_wxstr(_L("%1%: at least one value."), label));
        return false;
    }
    return true;
}

// A number of a field without an error message (the live estimate of the calibration dialog).
static bool quiet_number(wxWindow *field, double &value)
{
    wxString str;
    if (auto *combo = dynamic_cast<wxComboBox*>(field))
        str = combo->GetValue();
    else if (auto *text = dynamic_cast<wxTextCtrl*>(field))
        str = text->GetValue();
    str.Replace(",", ".");
    str.Trim(true).Trim(false);
    return str.ToCDouble(&value);
}

// Numbers separated by commas, semicolons or spaces, all above 0; false if one isn't (empty: true, none).
static bool quiet_number_list(const wxString &text, std::vector<double> &values)
{
    values.clear();
    wxString str = text;
    str.Replace(";", " ");
    str.Replace(",", " ");
    for (wxString token : wxSplit(str, ' ', '\0')) {
        token.Trim(true).Trim(false);
        if (token.empty())
            continue;
        double value;
        if (! token.ToCDouble(&value) || value <= 0.)
            return false;
        values.emplace_back(value);
    }
    return true;
}

// The plates of the adaptive PA calibration: per bed its outline, the keep-out zones and the frames with their labels.
class CalibrationPlatesPreview : public wxPanel
{
public:
    // label: "test acceleration"; number: the test number alone, drawn when the label is wider than the frame.
    struct Frame { BoundingBoxf box; wxString label; wxString number; int color; double acceleration; };

    CalibrationPlatesPreview(wxWindow *parent) : wxPanel(parent, wxID_ANY)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, [this](wxPaintEvent &) { paint(); });
    }

    void set(const BoundingBoxf &bed, const std::vector<BoundingBoxf> &zones, std::vector<std::vector<Frame>> beds)
    {
        m_bed   = bed;
        m_zones = zones;
        m_beds  = std::move(beds);
        const int side = 20 * wxGetApp().em_unit();
        SetMinSize(wxSize(m_beds.empty() ? 1 : std::max(int(m_beds.size()) * (side + side / 10), 30 * wxGetApp().em_unit()),
                          m_beds.empty() ? 1 : side + 4 * wxGetApp().em_unit()));
        Refresh();
    }

private:
    void paint()
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        dc.Clear();
        const int    side  = 20 * wxGetApp().em_unit();
        const double scale = side / std::max(m_bed.size().x(), m_bed.size().y());
        static const wxColour colors[] = { wxColour(237, 107, 33), wxColour(60, 141, 188), wxColour(90, 164, 105), wxColour(181, 101, 29),
                                           wxColour(150, 90, 190) };
        wxFont font = GetFont();
        font.SetPointSize(std::max(6, font.GetPointSize() - 2));
        dc.SetFont(font);
        for (size_t b = 0; b < m_beds.size(); ++ b) {
            const int x0 = int(b) * (side + side / 10), y0 = 0;
            // Bed coordinates: y up.
            const auto pt = [&](const Vec2d &p) { return wxPoint(x0 + int((p.x() - m_bed.min.x()) * scale), y0 + side - int((p.y() - m_bed.min.y()) * scale)); };
            dc.SetPen(wxPen(wxColour(70, 75, 82)));
            dc.SetBrush(wxBrush(wxColour(43, 47, 54)));
            dc.DrawRectangle(x0, y0, side, side);
            dc.SetBrush(wxBrush(wxColour(110, 110, 110), wxBRUSHSTYLE_BDIAGONAL_HATCH));
            for (const BoundingBoxf &z : m_zones) {
                const wxPoint a = pt(Vec2d(z.min.x(), z.max.y())), c = pt(Vec2d(z.max.x(), z.min.y()));
                dc.DrawRectangle(a.x, a.y, c.x - a.x, c.y - a.y);
            }
            for (const Frame &f : m_beds[b]) {
                const wxColour col = colors[f.color % 5];
                const wxPoint a = pt(Vec2d(f.box.min.x(), f.box.max.y())), c = pt(Vec2d(f.box.max.x(), f.box.min.y()));
                dc.SetPen(wxPen(col));
                dc.SetBrush(wxBrush(wxColour(col.Red() / 3, col.Green() / 3, col.Blue() / 3)));
                dc.DrawRectangle(a.x, a.y, std::max(1, c.x - a.x), std::max(1, c.y - a.y));
                dc.SetTextForeground(*wxWHITE);
                const wxString text = dc.GetTextExtent(f.label).x <= c.x - a.x - 2 ? f.label : f.number;
                const wxSize   ts   = dc.GetTextExtent(text);
                dc.DrawText(text, (a.x + c.x - ts.x) / 2, c.y - ts.y - 2);
            }
            dc.SetTextForeground(GetForegroundColour());
            dc.DrawText(format_wxstr(_L("Plate %1%: %2% frames"), b + 1, m_beds[b].size()), x0, y0 + side + 2);
        }
        // The colour of each acceleration.
        std::map<int, double> legend;
        for (const std::vector<Frame> &bed : m_beds)
            for (const Frame &f : bed)
                legend[f.color] = f.acceleration;
        int x = 0;
        const int y = side + 2 + dc.GetCharHeight() + 2;
        for (const auto &[color, accel] : legend) {
            const wxColour col = colors[color % 5];
            dc.SetPen(wxPen(col));
            dc.SetBrush(wxBrush(col));
            dc.DrawRectangle(x, y + 2, dc.GetCharHeight() - 4, dc.GetCharHeight() - 4);
            const wxString text = wxString::FromCDouble(accel) + from_u8(" mm/s²");
            dc.DrawText(text, x + dc.GetCharHeight(), y);
            x += dc.GetCharHeight() + dc.GetTextExtent(text).x + 2 * wxGetApp().em_unit();
        }
    }

    BoundingBoxf                     m_bed;
    std::vector<BoundingBoxf>        m_zones;
    std::vector<std::vector<Frame>>  m_beds;
};

class CalibrationAdaptivePADialog : public CalibrationDialog
{
public:
    CalibrationAdaptivePADialog()
        : CalibrationDialog(_L("Adaptive pressure advance calibration"),
            _L("Calibration for OrcaSlicer's adaptive pressure advance: the pressure calibration with Ellis' chevrons once per "
               "speed and acceleration. Each frame shows its test number (1, 2, 3... in the order of the speeds) and its "
               "acceleration, e.g. \"1 6000\". The frames are spread over as few plates as possible (the plater's beds): print "
               "the plates one after another, in any order. Each acceleration needs at least 2 speeds. Each wall prints at its "
               "test's speed with SET_VELOCITY_LIMIT ACCEL= before it (the print settings write no acceleration); the filament "
               "end G-code sets the acceleration back to the Klipper max accel of the filament's speed mode.\n"
               "The sides of the chevrons of each test are long enough for half of each to print at the full speed (Klipper "
               "minimum_cruise_ratio, Printer > Machine limits), at least the minimum side length.\n"
               "Defaults: the small perimeter speed, the fastest speed of the print settings and halfway between.\n"
               "After printing: Calibration > Adaptive pressure advance results, type the acceleration and the best value of "
               "tests 1, 2, 3 (it keeps the tests of every acceleration printed for this filament and nozzle).\n"
               "Klipper only. Generate puts Ellis' settings in the print and filament settings (unsaved changes)."))
    {
        const DynamicPrintConfig config = current_config();
        const PressureCalibrationSettings ellis;
        const auto number = [](double v) { return wxString::FromCDouble(v); };
        // The layer heights of the print settings (user 2026-11-01: the flow at the layer height the nozzle prints at), not Ellis'.
        PressureCalibrationSettings defaults = ellis;
        defaults.line_ratio         = 100.;
        defaults.layer_height       = config.opt_float("layer_height");
        defaults.first_layer_height = config.get_abs_value("first_layer_height", defaults.layer_height);
        set_columns(4);
        // Around the user's 0.02 to 0.03 (2026-11-01), by 0.001 as OrcaSlicer recommends (user 2026-11-01).
        m_start       = add_text(_L("Start value"), _L("Pressure advance of the first chevron (Ellis: 0)."), "0.01");
        m_end         = add_text(_L("End value"), _L("Pressure advance of the last chevron (Ellis: 0.08). OrcaSlicer: high enough to show "
                                                     "gaps at the corners of the slowest test."), "0.05");
        m_step        = add_text(_L("Increment"), _L("Pressure advance difference between two chevrons (Ellis: 0.005, 0.001 to 0.002 for a "
                                                     "fine pass; OrcaSlicer: 0.001)."), "0.001");
        m_line_ratio  = add_text(_L("Line width (% of the nozzle)"), _L("Width of the chevrons. Ellis: match your thickest perimeter. The "
                                                                        "flow of each test is computed with it."), number(100.));
        m_layers      = add_text(_L("Layers"), _L("Height of the chevrons in layers, with the first one (Ellis: 4)."), number(ellis.layers));
        m_walls       = add_text(_L("Walls"), _L("Number of walls (open lines) of each chevron (Ellis: 3)."), number(ellis.wall_count));
        m_side        = add_text(_L("Minimum side length (mm)"), _L("The shortest side of a chevron (Ellis: 30). The sides of a test are "
                                                                    "longer when its speed needs it at the acceleration."), "20");
        m_spacing     = add_text(_L("Spacing (mm)"), _L("Gap between two chevrons (Ellis: 2)."), number(ellis.pattern_spacing));
        m_first_speed = add_text(_L("First layer speed (mm/s)"), _L("First layer, and the numbers (Ellis: 30). Print settings."), number(ellis.first_layer_speed));
        m_layer       = add_text(_L("Layer height (mm)"), _L("Default: the layer height of the print settings (Ellis: 0.2). The flow of each "
                                                             "test is computed with it."), number(defaults.layer_height));
        m_first_layer = add_text(_L("First layer height (mm)"), _L("Default: the first layer height of the print settings (Ellis: 0.25)."),
                                 number(defaults.first_layer_height));
        m_fan         = add_text(_L("Fan (%)"), _L("From the second layer (Ellis: 100). Filament settings."), number(ellis.fan));
        m_first_fan   = add_text(_L("First layer fan (%)"), _L("Ellis: 0. 0 (off on the first layer) or the same as the fan."), number(ellis.first_layer_fan));

        const int  em   = em_unit();
        const auto line = [this, em](const wxString &label, wxSizer *content) {
            auto *sizer = new wxBoxSizer(wxHORIZONTAL);
            auto *text  = new wxStaticText(this, wxID_ANY, label);
            text->SetMinSize(wxSize(14 * em, -1));
            sizer->Add(text, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em);
            sizer->Add(content, 1, wxALIGN_CENTER_VERTICAL);
            GetSizer()->Add(sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, em);
        };
        // The default speeds with the dialog's defaults: the max volumetric speed caps the fastest one.
        DynamicPrintConfig defaults_config = config;
        pressure_calibration_apply_settings(defaults_config, defaults);
        m_speeds = new wxTextCtrl(this, wxID_ANY, number_list(adaptive_pa_default_speeds(defaults_config, defaults)), wxDefaultPosition, wxSize(30 * em, -1));
        m_speeds->SetToolTip(_L("Comma-separated list of print speeds, one test each per acceleration (OrcaSlicer: at least 3). Default: the "
                                "small perimeter speed, the fastest speed of the print settings (limited by the max volumetric speed) and halfway between."));
        {
            auto *s = new wxBoxSizer(wxHORIZONTAL);
            s->Add(m_speeds, 1, wxALIGN_CENTER_VERTICAL);
            line(_L("Speeds (mm/s)"), s);
        }
        // The accelerations: the defaults (25 % of the slow speed mode's, the filament's, the fast speed mode's) and others typed.
        {
            const std::vector<double> accels = adaptive_pa_default_accelerations(config);
            const double slow = 0.25 * config.get_abs_value("klipper_max_accel_slow", config.opt_float("klipper_max_accel"));
            const double printed = config.get_abs_value("default_acceleration", config.opt_float("klipper_max_accel"));
            const double own = printed > 0. ? printed : adaptive_pa_speed_mode_accel(config);
            auto *s = new wxWrapSizer(wxHORIZONTAL);
            for (size_t i = 0; i < accels.size(); ++ i) {
                const double a = accels[i];
                wxString what = std::abs(a - own) < 0.5 ? _L("the filament's") : std::abs(a - slow) < 0.5 ? _L("25 % of slow") : _L("fast mode");
                auto *check = new wxCheckBox(this, wxID_ANY, number(a) + " (" + what + ")");
                // The filament's acceleration ticked.
                check->SetValue(std::abs(a - own) < 0.5);
                check->SetToolTip(_L("Calibrate at this acceleration: one test per speed."));
                s->Add(check, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2 * em);
                m_accel_checks.emplace_back(check, a);
            }
            m_other_check = new wxCheckBox(this, wxID_ANY, _L("Other"));
            s->Add(m_other_check, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
            m_other = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(14 * em, -1));
            m_other->SetToolTip(_L("More accelerations, comma-separated (mm/s²), e.g. 28000, 12000."));
            m_other->Enable(false);
            s->Add(m_other, 0, wxALIGN_CENTER_VERTICAL);
            line(_L("Accelerations (mm/s²)"), s);
            m_other_check->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { m_other->Enable(m_other_check->GetValue()); update_estimate(); });
        }
        {
            auto *s = new wxBoxSizer(wxHORIZONTAL);
            m_turn = new wxCheckBox(this, wxID_ANY, _L("Turn frames 90° to fit more on a plate"));
            m_turn->SetValue(true);
            m_turn->SetToolTip(_L("The arms of the chevrons stay at ±45° (one motor each on a CoreXY): the same test. Off: all upright."));
            s->Add(m_turn, 0, wxALIGN_CENTER_VERTICAL);
            line(_L("Layout"), s);
        }
        // The live estimate: tests, plates, large frames, and the plates drawn.
        m_estimate = new wxStaticText(this, wxID_ANY, wxEmptyString);
        GetSizer()->Add(m_estimate, 0, wxLEFT | wxRIGHT | wxTOP, em);
        m_preview = new CalibrationPlatesPreview(this);
        GetSizer()->Add(m_preview, 0, wxLEFT | wxRIGHT | wxTOP, em);
        // The tests of each acceleration already kept for the results.
        m_kept = new wxStaticText(this, wxID_ANY, kept_text());
        GetSizer()->Add(m_kept, 0, wxLEFT | wxRIGHT | wxTOP, em);
        if (config.opt_enum<GCodeFlavor>("gcode_flavor") != gcfKlipper) {
            auto *text = new wxStaticText(this, wxID_ANY, _L("Klipper only: select a Klipper printer."));
            text->SetForegroundColour(*wxRED);
            GetSizer()->Add(text, 0, wxALL, em);
        }
        for (wxTextCtrl *t : { m_start, m_end, m_step, m_line_ratio, m_layers, m_walls, m_side, m_spacing, m_first_speed, m_layer, m_first_layer,
                               m_fan, m_first_fan, m_speeds, m_other })
            t->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { update_estimate(); });
        for (auto &[check, a] : m_accel_checks)
            check->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { update_estimate(); });
        m_turn->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { update_estimate(); });
        update_estimate(false);
        finish_layout();
    }

protected:
    // "Results kept: 1500, 6000 mm/s²" for this filament and nozzle.
    static wxString kept_text()
    {
        const std::vector<double> accels = adaptive_pa_calibration_accelerations(adaptive_pa_kept(selected_filament()), nozzle_diameter(current_config()));
        if (accels.empty())
            return _L("No calibration kept yet for this filament and nozzle.");
        return format_wxstr(_L("Calibrations kept for this filament and nozzle: %1% mm/s² (generating one again replaces it)."), number_list(accels));
    }

    // Everything Generate makes: the settings, the tests (per acceleration one per speed, numbered 1, 2, 3...), the kept tests of
    // the print, the frame sizes and their layout on the beds.
    struct Plan
    {
        PressureCalibrationSettings          settings;
        DynamicPrintConfig                   config;       // with the settings applied, as the frames are built
        std::vector<double>                  accels;
        std::vector<PressureCalibrationTest> tests;
        std::vector<size_t>                  ids;
        std::vector<AdaptivePATest>          print;
        std::vector<Vec2d>                   sizes;        // without the brim
        std::vector<CalibrationPlacement>    layout;
        BoundingBoxf                         bed;
        std::vector<BoundingBoxf>            zones;
        wxString                             capped;
    };

    // False with error set (the first wrong field) if the fields don't make a calibration.
    bool make_plan(Plan &plan, wxString &error) const
    {
        const DynamicPrintConfig config = current_config();
        double start, end, increment, min_side, layers, fan, first_fan, wall_count;
        PressureCalibrationSettings &settings = plan.settings;
        const std::pair<wxWindow*, double*> fields[] = { { m_start, &start }, { m_end, &end }, { m_step, &increment }, { m_side, &min_side },
            { m_line_ratio, &settings.line_ratio }, { m_layers, &layers }, { m_first_speed, &settings.first_layer_speed },
            { m_layer, &settings.layer_height }, { m_first_layer, &settings.first_layer_height }, { m_fan, &fan }, { m_first_fan, &first_fan },
            { m_walls, &wall_count }, { m_spacing, &settings.pattern_spacing } };
        for (const auto &[field, value] : fields)
            if (! quiet_number(field, *value)) {
                error = _L("A field above is not a number.");
                return false;
            }
        if (increment <= 0. || end <= start) {
            error = _L("The increment must be above 0 and the end value above the start value.");
            return false;
        }
        const std::vector<double> values = pressure_calibration_values(start, end, increment);
        if (values.size() > 100) {
            error = format_wxstr(_L("%1% values: too many. Use a larger increment."), values.size());
            return false;
        }
        std::vector<double> speeds;
        if (! quiet_number_list(m_speeds->GetValue(), speeds) || speeds.empty()) {
            error = _L("Speeds: numbers above 0, comma-separated.");
            return false;
        }
        if (speeds.size() > 9) {
            error = _L("At most 9 speeds (the frames are numbered 1 to 9).");
            return false;
        }
        // The accelerations ticked and the others typed, sorted, without duplicates.
        for (const auto &[check, a] : m_accel_checks)
            if (check->GetValue())
                plan.accels.emplace_back(a);
        if (m_other_check->GetValue()) {
            std::vector<double> other;
            if (! quiet_number_list(m_other->GetValue(), other)) {
                error = _L("Other accelerations: numbers above 0, comma-separated.");
                return false;
            }
            plan.accels.insert(plan.accels.end(), other.begin(), other.end());
        }
        std::sort(plan.accels.begin(), plan.accels.end());
        plan.accels.erase(std::unique(plan.accels.begin(), plan.accels.end(), [](double a, double b) { return std::abs(a - b) < 0.5; }), plan.accels.end());
        if (plan.accels.empty()) {
            error = _L("Tick at least one acceleration.");
            return false;
        }
        settings.wall_count      = int(wall_count);
        settings.layers          = int(layers);
        settings.fan             = int(fan);
        settings.first_layer_fan = int(first_fan);
        settings.side_length     = min_side;
        // Each test sets its own acceleration and speed; the print settings write no acceleration command.
        settings.speed           = speeds.front();
        settings.acceleration    = 0.;
        // The flow with the settings' layer height (the walls print at it above the first layer); the frames as they are built.
        plan.config = config;
        try {
            pressure_calibration_apply_settings(plan.config, settings);
        } catch (const std::exception &ex) {
            error = from_u8(ex.what());
            return false;
        }
        pressure_calibration_print_config(plan.config);
        for (double accel : plan.accels)
            for (size_t k = 0; k < speeds.size(); ++ k) {
                const double speed     = speeds[k];
                const double effective = adaptive_pa_effective_speed(plan.config, settings, speed);
                if (effective < speed - EPSILON)
                    plan.capped += format_wxstr(_L("%1% mm/s², test %2%: %3% mm/s prints at %4% mm/s\n"), accel, k + 1, speed, std::round(effective * 10.) / 10.);
                plan.tests.push_back({ start, values, "ExternalPerimeter", false, speed, accel, adaptive_pa_side_length(plan.config, effective, accel, min_side) });
                plan.ids.emplace_back(k + 1);
                plan.print.push_back({ effective, accel, adaptive_pa_flow(plan.config, settings, speed), nozzle_diameter(config), -1., int(k + 1) });
                plan.sizes.emplace_back(pressure_calibration_frame_size(plan.config, settings, plan.tests.back(), k + 1));
            }
        plan.bed   = BoundingBoxf(config.opt<ConfigOptionPoints>("bed_shape")->values);
        plan.zones = config.has("bed_keep_out_zones") ? BedKeepOut::parse(config.opt_string("bed_keep_out_zones")) : std::vector<BoundingBoxf>();
        std::vector<int> groups;
        for (const PressureCalibrationTest &t : plan.tests)
            groups.push_back(int(std::find_if(plan.accels.begin(), plan.accels.end(), [&t](double a) { return std::abs(a - t.acceleration) < 0.5; }) - plan.accels.begin()));
        plan.layout = calibration_layout(plan.sizes, plan.bed, plan.zones, PRESSURE_CALIBRATION_BRIM_WIDTH, m_turn->GetValue(), int(MAX_NUMBER_OF_BEDS), groups);
        if (plan.layout.empty()) {
            error = format_wxstr(_L("The frames don't fit on %1% beds, or a frame is larger than the bed: fewer tests, a lower speed or a "
                                    "higher acceleration."), MAX_NUMBER_OF_BEDS);
            return false;
        }
        return true;
    }

    // The estimate under the accelerations: the tests and plates, the large frames, the plates drawn.
    void update_estimate(bool layout = true)
    {
        Plan     plan;
        wxString error;
        if (! make_plan(plan, error)) {
            m_estimate->SetLabel(error);
            m_estimate->SetForegroundColour(*wxRED);
            m_preview->set(BoundingBoxf(), {}, {});
        } else {
            int beds = 0;
            for (const CalibrationPlacement &p : plan.layout)
                beds = std::max(beds, p.bed + 1);
            const size_t speeds = plan.tests.size() / plan.accels.size();
            wxString text = format_wxstr(_L("%1% tests (%2% accelerations × %3% speeds) on %4% plate(s)."), plan.tests.size(), plan.accels.size(), speeds, beds);
            // The usual frame and the larger ones (fast tests at a low acceleration need longer sides).
            const Vec2d usual = *std::min_element(plan.sizes.begin(), plan.sizes.end(), [](const Vec2d &a, const Vec2d &b) { return a.x() * a.y() < b.x() * b.y(); });
            text += "\n" + format_wxstr(_L("Frames %1% × %2% mm (without the %3% mm brim)"), std::lround(usual.x()), std::lround(usual.y()), PRESSURE_CALIBRATION_BRIM_WIDTH);
            for (size_t i = 0; i < plan.tests.size(); ++ i)
                if (plan.sizes[i].x() > usual.x() + 1. || plan.sizes[i].y() > usual.y() + 1.)
                    text += ", " + format_wxstr(_L("%1% @ %2% mm/s: %3% × %4% mm"), plan.tests[i].acceleration, plan.tests[i].speed,
                                                 std::lround(plan.sizes[i].x()), std::lround(plan.sizes[i].y()));
            text += ".";
            if (! plan.capped.empty())
                text += "\n" + _L("Limited by the max volumetric speed:") + " " + plan.capped;
            m_estimate->SetLabel(text);
            m_estimate->SetForegroundColour(GetForegroundColour());
            std::vector<std::vector<CalibrationPlatesPreview::Frame>> frames(beds);
            for (size_t i = 0; i < plan.layout.size(); ++ i) {
                const CalibrationPlacement &p = plan.layout[i];
                const Vec2d size = p.turned ? Vec2d(plan.sizes[i].y(), plan.sizes[i].x()) : plan.sizes[i];
                const int color = int(std::find_if(plan.accels.begin(), plan.accels.end(), [&](double a) { return std::abs(a - plan.tests[i].acceleration) < 0.5; }) - plan.accels.begin());
                frames[p.bed].push_back({ BoundingBoxf(p.position, p.position + size), wxString::Format("%d %ld", int(plan.ids[i]), std::lround(plan.tests[i].acceleration)),
                                          wxString::Format("%d", int(plan.ids[i])), color, plan.tests[i].acceleration });
            }
            m_preview->set(plan.bed, plan.zones, std::move(frames));
        }
        if (layout) {
            Layout();
            GetSizer()->SetSizeHints(this);
            Fit();
        }
    }

    bool generate() override
    {
        DynamicPrintConfig config = current_config();
        if (config.opt_enum<GCodeFlavor>("gcode_flavor") != gcfKlipper) {
            show_error(this, _L("Klipper only: select a Klipper printer."));
            return false;
        }
        Plan     plan;
        wxString error;
        if (! make_plan(plan, error)) {
            show_error(this, error);
            return false;
        }
        if (! plan.capped.empty() &&
            MessageDialog(this, _L("The max volumetric speed (print or filament settings) limits these tests:") + "\n" + plan.capped + "\n" +
                                _L("Their flow is the limited one. Continue?"), _L("Adaptive pressure advance calibration"),
                          wxYES_NO | wxICON_WARNING).ShowModal() != wxID_YES)
            return false;

        // Back to the printer's acceleration at the end: the test's SET_VELOCITY_LIMIT would stay until Klipper restarts.
        const std::string restore = "SET_VELOCITY_LIMIT ACCEL=" + std::to_string(std::lround(adaptive_pa_speed_mode_accel(config))) +
                                    " ; adaptive pressure advance calibration: the printer's acceleration\n";
        const std::string filament_name = selected_filament();
        if (! load_pressure_calibration(this, plan.tests, plan.settings, [&restore](DynamicPrintConfig &filament) {
                if (auto *opt = filament.option<ConfigOptionStrings>("end_filament_gcode"); opt != nullptr && ! opt->values.empty())
                    for (std::string &gcode : opt->values)
                        gcode = restore + gcode;
            }, plan.ids, &plan.layout))
            return false;
        // Kept per filament, per acceleration; the other accelerations and nozzles stay (adaptive_pa_tests_merge()).
        std::vector<AdaptivePATest> kept = adaptive_pa_kept(filament_name);
        for (double accel : plan.accels) {
            std::vector<AdaptivePATest> of_accel;
            for (const AdaptivePATest &t : plan.print)
                if (std::abs(t.acceleration - accel) < 0.5)
                    of_accel.push_back(t);
            kept = adaptive_pa_tests_merge(kept, of_accel);
        }
        adaptive_pa_keep(filament_name, kept);
        return true;
    }

private:
    wxTextCtrl   *m_start { nullptr }, *m_end { nullptr }, *m_step { nullptr }, *m_line_ratio { nullptr }, *m_layers { nullptr };
    wxTextCtrl   *m_walls { nullptr }, *m_side { nullptr }, *m_spacing { nullptr }, *m_first_speed { nullptr }, *m_layer { nullptr };
    wxTextCtrl   *m_first_layer { nullptr }, *m_fan { nullptr }, *m_first_fan { nullptr }, *m_speeds { nullptr }, *m_other { nullptr };
    std::vector<std::pair<wxCheckBox*, double>> m_accel_checks;
    wxCheckBox   *m_other_check { nullptr }, *m_turn { nullptr };
    wxStaticText *m_estimate { nullptr }, *m_kept { nullptr };
    CalibrationPlatesPreview *m_preview { nullptr };
};

// The results (user 2026-11-05): per row the acceleration of a calibration print (on its frames) and the best value of its tests 1, 2,
// 3...; the flows come from the kept tests (adaptive_pa_results()). "Add acceleration" adds a row, each row can be removed.
class CalibrationAdaptivePAResultsDialog : public CalibrationDialog
{
public:
    CalibrationAdaptivePAResultsDialog()
        : CalibrationDialog(_L("Adaptive pressure advance results"),
            _L("Per calibration print: type its acceleration (on its frames, after the test number) and the best value of each "
               "test, numbered 1, 2, 3 on the frames: the sharpest corner without a bulge or a gap. Leave a test empty when no "
               "value is clearly better (usual at low speed and acceleration, where several values are good: OrcaSlicer then "
               "uses the value of the next faster test). Each acceleration needs values for at least 2 tests. The best value "
               "should get lower as the speed and the acceleration get higher. \"Add acceleration\" for another print.\n"
               "Save sorts the values with the flow of each test into the selected filament's \"Adaptive pressure advance "
               "measurements\" (Filament > Advanced), in the section of this nozzle (the other nozzles' sections stay), as an "
               "unsaved change: revert the calibration's unsaved changes (fan, layer time cooling, end G-code) first, then save "
               "the filament after."))
        , m_filament(selected_filament())
    {
        const int                em     = em_unit();
        const DynamicPrintConfig config = current_config();
        // The selected filament and the printer's nozzle (user 2026-11-05: the calibrations are kept per filament and nozzle).
        m_nozzle = nozzle_diameter(config);
        m_kept   = adaptive_pa_kept(m_filament);
        // The filament's measurements of accelerations without kept tests (e.g. calibrated on another computer), with their flows.
        {
            const auto       *opt   = wxGetApp().preset_bundle->filaments.get_edited_preset().config.option<ConfigOptionStrings>("adaptive_pressure_advance_model");
            const std::string model = opt != nullptr && ! opt->values.empty() ? opt->values.front() : std::string();
            const std::vector<AdaptivePATest> from_model = adaptive_pa_tests_from_model(m_kept, model, m_nozzle);
            m_kept.insert(m_kept.end(), from_model.begin(), from_model.end());
        }
        const std::vector<double> accels   = adaptive_pa_calibration_accelerations(m_kept, m_nozzle);
        const std::vector<double> measured = measured_accelerations();

        // A large header with the nozzle, above the explanation.
        auto *header = new wxStaticText(this, wxID_ANY, format_wxstr(_L("Nozzle %1% mm"), m_nozzle));
        wxFont font  = header->GetFont().Bold();
        font.SetPointSize(int(std::lround(font.GetPointSize() * 1.8)));
        header->SetFont(font);
        GetSizer()->Insert(0, header, 0, wxLEFT | wxRIGHT | wxTOP, em);
        auto *sub = new wxStaticText(this, wxID_ANY, format_wxstr(_L("Filament \"%1%\": %2%"), from_u8(m_filament), accels.empty() ?
            _L("no calibration printed yet with this nozzle (Calibration > Adaptive pressure advance calibration).") :
            format_wxstr(_L("printed at %1% mm/s²."), number_list(accels))) + "\n" +
            (measured.empty() ? _L("No measurements saved in the filament for this nozzle yet: nothing is calibrated.") :
                                format_wxstr(_L("Measurements saved in the filament for this nozzle (what slicing uses): %1% mm/s²."), number_list(measured))));
        GetSizer()->Insert(1, sub, 0, wxLEFT | wxRIGHT, em);

        m_rows = new wxBoxSizer(wxVERTICAL);
        GetSizer()->Add(m_rows, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, em);
        // One row per calibrated acceleration with the values typed before, else an empty row.
        for (double accel : accels) {
            std::vector<double> pa;
            for (size_t i : adaptive_pa_calibration_tests(m_kept, m_nozzle, accel))
                pa.emplace_back(m_kept[i].pa);
            add_row(accel, pa);
        }
        if (m_rows_data.empty())
            add_row(0., {});
        auto *add = new wxButton(this, wxID_ANY, _L("Add acceleration"));
        add->SetToolTip(_L("Another row for the results of another calibration print (another acceleration)."));
        add->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { add_row(0., {}); relayout(); });
        GetSizer()->Add(add, 0, wxLEFT | wxTOP, em);

        // The accelerations the print profiles print at with this printer and filament (user 2026-11-05), against the calibrated ones.
        auto *profiles_title = new wxStaticText(this, wxID_ANY, _L("Accelerations your print profiles use with this printer and filament"));
        profiles_title->SetFont(profiles_title->GetFont().Bold());
        GetSizer()->Add(profiles_title, 0, wxLEFT | wxRIGHT | wxTOP, em);
        auto *profiles = new wxStaticText(this, wxID_ANY, profiles_text(config, measured));
        GetSizer()->Add(profiles, 0, wxLEFT | wxRIGHT | wxTOP, em / 2);
        finish_layout(_L("Save to filament"));
    }

    // The accelerations of the selected filament's saved measurements for this nozzle, as the G-code reads them (its section, else
    // the lines before the first section).
    std::vector<double> measured_accelerations() const
    {
        const auto *opt = wxGetApp().preset_bundle->filaments.get_edited_preset().config.option<ConfigOptionStrings>("adaptive_pressure_advance_model");
        if (opt == nullptr || opt->values.empty())
            return {};
        const std::string lines = adaptive_pa_model_for_nozzle(opt->values.front(), m_nozzle);
        if (lines.empty())
            return {};
        return adaptive_pa_calibration_accelerations(adaptive_pa_tests_from_model({}, adaptive_pa_nozzle_header(m_nozzle) + "\n" + lines, m_nozzle), m_nozzle);
    }

    // Per print preset (visible, compatible with the printer, allowed by the filament's compatible_prints): its accelerations with
    // the features, and whether the calibrated accelerations cover them (outside: the nearest calibrated one's values).
    static wxString profiles_text(const DynamicPrintConfig &config, const std::vector<double> &calibrated)
    {
        const PresetCollection &prints  = wxGetApp().preset_bundle->prints;
        const auto             *allowed = wxGetApp().preset_bundle->filaments.get_edited_preset().config.option<ConfigOptionStrings>("compatible_prints");
        wxString                out;
        for (const Preset &preset : prints) {
            if (! preset.is_visible || ! preset.is_compatible || preset.is_external)
                continue;
            if (allowed != nullptr && ! allowed->values.empty() &&
                std::find(allowed->values.begin(), allowed->values.end(), preset.name) == allowed->values.end())
                continue;
            DynamicPrintConfig with_print = config;
            with_print.apply(preset.name == prints.get_edited_preset().name ? prints.get_edited_preset().config : preset.config);
            wxString line;
            for (const AdaptivePAPrintAcceleration &a : adaptive_pa_print_accelerations(with_print)) {
                wxString features;
                for (const std::string &f : a.features)
                    features += (features.empty() ? "" : ", ") + from_u8(f);
                wxString note;
                if (calibrated.empty())
                    note = _L("not calibrated");
                else if (a.acceleration < calibrated.front() - 0.5)
                    note = format_wxstr(_L("below the calibrated ones: uses %1%"), calibrated.front());
                else if (a.acceleration > calibrated.back() + 0.5)
                    note = format_wxstr(_L("above the calibrated ones: uses %1%"), calibrated.back());
                else
                    note = std::any_of(calibrated.begin(), calibrated.end(), [&a](double c) { return std::abs(c - a.acceleration) < 0.5; }) ?
                           _L("calibrated") : _L("between calibrated ones");
                line += "\n      " + wxString::FromCDouble(a.acceleration) + from_u8(" mm/s²: ") + features + "  [" + note + "]";
            }
            out += (out.empty() ? "" : "\n") + from_u8(preset.name) + ":" + line;
        }
        return out.empty() ? _L("No print profile for this printer and filament.") : out;
    }

protected:
    bool generate() override
    {
        std::vector<AdaptivePAResultRow> rows;
        for (const Row &row : m_rows_data) {
            const wxString accel_text = row.accel->GetValue().Strip(wxString::both);
            bool any_value = false;
            for (wxTextCtrl *v : row.values)
                any_value |= ! v->GetValue().Strip(wxString::both).empty();
            if (accel_text.empty()) {
                if (any_value) {
                    show_error(this, _L("A row with values has no acceleration."));
                    return false;
                }
                continue;
            }
            AdaptivePAResultRow r;
            if (! get_number(row.accel, _L("Acceleration (mm/s²)"), r.acceleration))
                return false;
            if (r.acceleration <= 0.) {
                show_error(this, _L("The acceleration must be above 0."));
                return false;
            }
            for (size_t k = 0; k < row.values.size(); ++ k) {
                double pa = -1.;
                if (! row.values[k]->GetValue().Strip(wxString::both).empty()) {
                    if (! get_number(row.values[k], format_wxstr(_L("%1% mm/s², test %2%"), r.acceleration, k + 1), pa))
                        return false;
                    if (pa < 0.) {
                        show_error(this, format_wxstr(_L("%1% mm/s², test %2%: the pressure advance must be from 0 to 2."), r.acceleration, k + 1));
                        return false;
                    }
                }
                r.pa.emplace_back(pa);
            }
            rows.emplace_back(std::move(r));
        }
        std::string                        error;
        m_kept_before = m_kept;
        const std::vector<AdaptivePAPoint> points = adaptive_pa_results(m_kept, m_nozzle, rows, error);
        if (points.empty()) {
            show_error(this, from_u8(error));
            return false;
        }
        // Values against OrcaSlicer's rule (lower with a higher speed or acceleration): probably a misread chevron.
        if (const std::vector<std::string> warnings = adaptive_pa_results_warnings(m_kept, m_nozzle); ! warnings.empty()) {
            wxString text;
            for (const std::string &w : warnings)
                text += "- " + from_u8(w) + "\n";
            if (MessageDialog(this, _L("The best value should get lower as the speed and the acceleration get higher (OrcaSlicer). "
                                       "These go the other way, check the chevrons:") + "\n\n" + text + "\n" + _L("Save anyway?"),
                              _L("Adaptive pressure advance results"), wxYES_NO | wxICON_WARNING).ShowModal() != wxID_YES) {
                // Back to the kept values: adaptive_pa_results() wrote the typed ones into m_kept.
                m_kept = m_kept_before;
                return false;
            }
        }
        // The typed values stay for the next time.
        adaptive_pa_keep(m_filament, m_kept);
        DynamicPrintConfig filament = wxGetApp().preset_bundle->filaments.get_edited_preset().config;
        auto              *opt      = filament.option<ConfigOptionStrings>("adaptive_pressure_advance_model", true);
        const std::string  model    = adaptive_pa_model_set_section(opt->values.empty() ? std::string() : opt->values.front(), m_nozzle,
                                                                    adaptive_pa_model(points));
        opt->values.assign(std::max<size_t>(1, opt->values.size()), model);
        load_tab_config(Preset::TYPE_FILAMENT, filament);
        MessageDialog(this, format_wxstr(_L("Written into the filament \"%1%\" (unsaved change):"),
                                         from_u8(wxGetApp().preset_bundle->filaments.get_edited_preset().name)) + "\n\n" + from_u8(model),
                      _L("Adaptive pressure advance results"), wxOK | wxICON_INFORMATION).ShowModal();
        return true;
    }

private:
    struct Row
    {
        wxPanel                 *panel { nullptr };
        wxTextCtrl              *accel { nullptr };
        std::vector<wxTextCtrl*> values;
        wxBoxSizer              *fields { nullptr };
        wxStaticText            *info { nullptr };
    };

    // A row: the acceleration, the values of tests 1, 2, 3 (more when its print had more tests), Remove, and under them the speed
    // and flow of each test of the print.
    void add_row(double accel, const std::vector<double> &pa)
    {
        const int em    = em_unit();
        auto     *panel = new wxPanel(this);
        panel->SetFont(GetFont());
        auto *sizer  = new wxBoxSizer(wxVERTICAL);
        auto *fields = new wxBoxSizer(wxHORIZONTAL);
        Row   row;
        row.panel  = panel;
        row.fields = fields;
        fields->Add(new wxStaticText(panel, wxID_ANY, _L("Acceleration (mm/s²)")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
        row.accel = new wxTextCtrl(panel, wxID_ANY, accel > 0. ? wxString::FromCDouble(accel) : wxString(), wxDefaultPosition, wxSize(8 * em, -1));
        row.accel->SetToolTip(_L("The acceleration of the calibration print, on its frames after the test number."));
        fields->Add(row.accel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em);
        auto *remove = new wxButton(panel, wxID_ANY, _L("Remove"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        remove->SetToolTip(_L("Remove this row: its values are not written (the calibration print stays kept)."));
        fields->Add(remove, 0, wxALIGN_CENTER_VERTICAL);
        row.info = new wxStaticText(panel, wxID_ANY, wxEmptyString);
        row.info->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        sizer->Add(fields, 0, wxTOP, em / 2);
        sizer->Add(row.info, 0, wxTOP, em / 4);
        panel->SetSizer(sizer);
        m_rows->Add(panel, 0, wxEXPAND);
        m_rows_data.emplace_back(std::move(row));
        Row &r = m_rows_data.back();
        ensure_fields(r, std::max<size_t>(3, pa.size()));
        for (size_t k = 0; k < pa.size(); ++ k)
            if (pa[k] >= 0.)
                r.values[k]->SetValue(wxString::FromCDouble(pa[k]));
        update_info(r);
        r.accel->Bind(wxEVT_TEXT, [this, panel](wxCommandEvent &) {
            if (Row *row = find_row(panel)) {
                update_info(*row);
                relayout();
            }
        });
        remove->Bind(wxEVT_BUTTON, [this, panel](wxCommandEvent &) {
            // After the button's event: the button is destroyed with its row.
            CallAfter([this, panel]() {
                m_rows_data.erase(std::remove_if(m_rows_data.begin(), m_rows_data.end(), [panel](const Row &r) { return r.panel == panel; }),
                                  m_rows_data.end());
                m_rows->Detach(panel);
                panel->Destroy();
                relayout();
            });
        });
        wxGetApp().UpdateDarkUI(panel);
    }

    // At least count value fields, added before Remove.
    void ensure_fields(Row &row, size_t count)
    {
        const int em = em_unit();
        while (row.values.size() < count) {
            const size_t k     = row.values.size();
            auto        *label = new wxStaticText(row.panel, wxID_ANY, wxString::Format("%d", int(k + 1)));
            auto        *value = new wxTextCtrl(row.panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(6 * em, -1));
            value->SetToolTip(format_wxstr(_L("The best pressure advance of test %1% (the number on its frame), empty to leave it out."), k + 1));
            // Before the Remove button (the last item).
            const size_t at = row.fields->GetItemCount() - 1;
            row.fields->Insert(at, label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, em / 2);
            row.fields->Insert(at + 1, value, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em);
            row.values.emplace_back(value);
        }
    }

    // Under the fields: the speed and flow of each test of the print of the typed acceleration; more fields if it had more tests.
    void update_info(Row &row)
    {
        double   accel = 0.;
        wxString text  = row.accel->GetValue();
        text.Replace(",", ".");
        text.Trim(true).Trim(false);
        if (text.empty() || ! text.ToCDouble(&accel) || accel <= 0.) {
            row.info->SetLabel(text.empty() ? wxString() : _L("Not a number."));
            return;
        }
        const std::vector<size_t> tests = adaptive_pa_calibration_tests(m_kept, m_nozzle, accel);
        if (tests.empty()) {
            row.info->SetLabel(format_wxstr(_L("No calibration printed at %1% mm/s² for this nozzle."), accel));
            return;
        }
        ensure_fields(row, tests.size());
        wxString info;
        for (size_t k = 0; k < tests.size(); ++ k) {
            const AdaptivePATest &t = m_kept[tests[k]];
            info += (info.empty() ? "" : "    ") + (t.speed > 0. ?
                format_wxstr(_L("%1%: %2% mm/s, %3% mm³/s"), k + 1, std::round(t.speed * 10.) / 10., std::round(t.flow * 100.) / 100.) :
                format_wxstr(_L("%1%: %2% mm³/s"), k + 1, std::round(t.flow * 100.) / 100.));
        }
        if (m_kept[tests.front()].speed <= 0.)
            info += "    " + _L("(from the filament's measurements: no calibration print kept)");
        row.info->SetLabel(info);
    }

    Row* find_row(wxPanel *panel)
    {
        for (Row &r : m_rows_data)
            if (r.panel == panel)
                return &r;
        return nullptr;
    }

    void relayout()
    {
        Layout();
        GetSizer()->SetSizeHints(this);
        Fit();
    }

    std::string                 m_filament;
    std::vector<AdaptivePATest> m_kept;
    std::vector<AdaptivePATest> m_kept_before;
    double                      m_nozzle { 0. };
    wxBoxSizer                 *m_rows { nullptr };
    // A deque: the bound handlers find their row by its panel, the rows don't move when one is added.
    std::deque<Row>             m_rows_data;
};

// ------------------------------------------------------------------------------------------------------------------------
// CaliFlower (Vector 3D)
// ------------------------------------------------------------------------------------------------------------------------

static const char *CALIFLOWER_PATH_KEY = "calibration_califlower_path";

class CalibrationCaliFlowerDialog : public CalibrationDialog
{
public:
    CalibrationCaliFlowerDialog()
        : CalibrationDialog(_L("Filament shrinkage calibration"),
            _L("Loads your CaliFlower model (Vector 3D, not included) with the print settings as they are, "
               "switches the filament shrinkage compensation off (XY and Z 0%) and adds SET_SKEW CLEAR=1 (Klipper: no skew "
               "correction) to the filament start G-code, after the printer's start G-code (unsaved changes of the filament settings), "
               "so that you measure the real shrinkage.\n"
               "Measure the print and enter the values in Vector 3D's calculator."))
    {
        m_path = add_text(_L("CaliFlower file"), _L("Location of your CaliFlower model file (STL, 3MF, OBJ or STEP). It is remembered."),
                          from_u8(wxGetApp().app_config->get(CALIFLOWER_PATH_KEY)), true);
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
        // No object settings: the print profile applies.
        std::vector<size_t> objs = plater->load_files(std::vector<std::string>{ path }, true, false);
        if (objs.empty())
            return false;

        // No shrinkage compensation (PrusaSlicer 0% = SuperSlicer 100%), for every filament.
        DynamicPrintConfig filament = wxGetApp().preset_bundle->filaments.get_edited_preset().config;
        for (const char *key : { "filament_shrinkage_compensation_xy", "filament_shrinkage_compensation_z" }) {
            auto *opt = filament.option<ConfigOptionPercents>(key, true);
            opt->values.assign(std::max<size_t>(1, opt->values.size()), 0.);
        }
        // No Klipper skew correction either: the filament start G-code runs after the printer's start G-code (PRINT_START).
        {
            auto *opt = filament.option<ConfigOptionStrings>("start_filament_gcode", true);
            if (opt->values.empty())
                opt->values.emplace_back();
            for (std::string &gcode : opt->values)
                if (gcode.find("SET_SKEW CLEAR=1") == std::string::npos)
                    gcode += (gcode.empty() || gcode.back() == '\n' ? "" : "\n") + std::string("SET_SKEW CLEAR=1\n");
        }
        load_tab_config(Preset::TYPE_FILAMENT, filament);

        finish_calibration_project(objs);
        return true;
    }

private:
    wxTextCtrl *m_path { nullptr };
};

// ------------------------------------------------------------------------------------------------------------------------
// Ringing tower (Klipper TUNING_TOWER)
// ------------------------------------------------------------------------------------------------------------------------

static const char *RINGING_PATH_KEY    = "calibration_ringing_path";
// The block written into the filament start G-code, between these lines (replaced when generated again).
static const char *RINGING_BLOCK_START = "; Ringing tower (Calibration menu)";
static const char *RINGING_BLOCK_END   = "; Ringing tower end";

class CalibrationRingingDialog : public CalibrationDialog
{
public:
    CalibrationRingingDialog()
        : CalibrationDialog(_L("Ringing tower"),
            _L("Loads your ringing test model (e.g. Klipper's ringing_tower.stl, not included; don't rotate it) with Klipper's "
               "recommended print settings: vase mode (one perimeter, no infill, no top layers) on a 1 mm base, layer height "
               "0.2 mm (0.25 mm kept), external perimeters at the velocity limit, minimum layer time 3 s at most. "
               "Adds Klipper's ringing test to the filament start G-code, after the printer's start G-code: velocity limit, "
               "no pressure advance, the input shaper below and TUNING_TOWER raising the acceleration with the height. "
               "So that nothing overrides it, the print writes no acceleration commands (default acceleration 0) and the "
               "filament's Klipper speed mode is Off. All are unsaved changes of the print and filament settings.\n"
               "Ringing frequency = velocity limit x oscillations / distance (mm).\n"
               "Shaper frequency 0 = no input shaper on that axis. Acceleration of a height: start + step delta x (height / step height)."))
    {
        const auto remembered = [](const std::string &key, const char *def) {
            const std::string value = wxGetApp().app_config->get("calibration_ringing_" + key);
            return from_u8(value.empty() ? std::string(def) : value);
        };
        const std::vector<wxString> shapers = choices({ "zv", "mzv", "zvd", "ei", "2hump_ei", "3hump_ei" });
        m_path        = add_text(_L("Model file"), _L("Location of your ringing test model file (STL, 3MF, OBJ or STEP). It is remembered."),
                                 from_u8(wxGetApp().app_config->get(RINGING_PATH_KEY)), true);
        m_velocity    = add_text(_L("Velocity limit (mm/s)"), "SET_VELOCITY_LIMIT VELOCITY=, also the external perimeter speed",
                                 remembered("velocity", "100"));
        m_shaper_x    = add_combo(_L("Shaper type X"), "SET_INPUT_SHAPER SHAPER_TYPE_X=", shapers, remembered("shaper_type_x", "mzv"));
        m_freq_x      = add_text(_L("Shaper frequency X (Hz)"), "SET_INPUT_SHAPER SHAPER_FREQ_X= (0 = off)", remembered("shaper_freq_x", "0"));
        m_shaper_y    = add_combo(_L("Shaper type Y"), "SET_INPUT_SHAPER SHAPER_TYPE_Y=", shapers, remembered("shaper_type_y", "mzv"));
        m_freq_y      = add_text(_L("Shaper frequency Y (Hz)"), "SET_INPUT_SHAPER SHAPER_FREQ_Y= (0 = off)", remembered("shaper_freq_y", "0"));
        m_start_accel = add_text(_L("Start acceleration (mm/s²)"), "TUNING_TOWER START=", remembered("start_accel", "1500"));
        m_step_delta  = add_text(_L("Step delta (mm/s²)"), "TUNING_TOWER STEP_DELTA=", remembered("step_delta", "500"));
        m_step_height = add_text(_L("Step height (mm)"), "TUNING_TOWER STEP_HEIGHT=", remembered("step_height", "5"));
        auto *browse = new wxButton(this, wxID_ANY, _L("Browse") + dots);
        GetSizer()->Add(browse, 0, wxALIGN_RIGHT | wxRIGHT | wxTOP, em_unit());
        browse->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
            wxFileDialog dlg(this, _L("Choose the ringing test model file"), wxEmptyString, wxEmptyString, file_wildcards(FT_MODEL),
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
            show_error(this, _L("Choose your ringing test model file first."));
            return false;
        }
        double velocity, freq_x, freq_y, start_accel, step_delta, step_height;
        if (! get_number(m_velocity, _L("Velocity limit (mm/s)"), velocity) ||
            ! get_number(m_freq_x, _L("Shaper frequency X (Hz)"), freq_x) || ! get_number(m_freq_y, _L("Shaper frequency Y (Hz)"), freq_y) ||
            ! get_number(m_start_accel, _L("Start acceleration (mm/s²)"), start_accel) ||
            ! get_number(m_step_delta, _L("Step delta (mm/s²)"), step_delta) || ! get_number(m_step_height, _L("Step height (mm)"), step_height))
            return false;
        if (velocity <= 0. || start_accel <= 0. || step_height <= 0. || freq_x < 0. || freq_y < 0.) {
            show_error(this, _L("The velocity limit, start acceleration and step height must be above 0, the shaper frequencies 0 or more."));
            return false;
        }
        const auto shaper = [](wxComboBox *combo) { return into_u8(combo->GetValue().Trim(true).Trim(false)); };
        const std::string shaper_x = shaper(m_shaper_x), shaper_y = shaper(m_shaper_y);
        if (shaper_x.empty() || shaper_y.empty() || shaper_x.find_first_of(" \t\r\n") != std::string::npos ||
            shaper_y.find_first_of(" \t\r\n") != std::string::npos) {
            show_error(this, _L("Choose a shaper type for X and Y."));
            return false;
        }
        // The numbers as typed (decimal point, no trailing zeros).
        const auto num = [](double v) { std::string s = float_to_string_decimal_point(v, 6);
            if (s.find('.') != std::string::npos) { s.erase(s.find_last_not_of('0') + 1); if (s.back() == '.') s.pop_back(); }
            return s; };
        AppConfig &app = *wxGetApp().app_config;
        app.set(RINGING_PATH_KEY, path);
        app.set("calibration_ringing_velocity", num(velocity));
        app.set("calibration_ringing_shaper_type_x", shaper_x);
        app.set("calibration_ringing_shaper_freq_x", num(freq_x));
        app.set("calibration_ringing_shaper_type_y", shaper_y);
        app.set("calibration_ringing_shaper_freq_y", num(freq_y));
        app.set("calibration_ringing_start_accel", num(start_accel));
        app.set("calibration_ringing_step_delta", num(step_delta));
        app.set("calibration_ringing_step_height", num(step_height));
        if (! start_calibration_project())
            return false;

        Plater *plater = wxGetApp().plater();
        // No object settings: the print profile applies.
        std::vector<size_t> objs = plater->load_files(std::vector<std::string>{ path }, true, false);
        if (objs.empty())
            return false;

        const std::string block = std::string(RINGING_BLOCK_START) + "\n" +
            "SET_VELOCITY_LIMIT VELOCITY=" + num(velocity) + " MINIMUM_CRUISE_RATIO=0\n"
            "SET_PRESSURE_ADVANCE ADVANCE=0\n"
            // Frequency 0 = no shaper on that axis: no shaper type for it.
            "SET_INPUT_SHAPER" + (freq_x > 0. ? " SHAPER_TYPE_X=" + shaper_x : std::string()) + " SHAPER_FREQ_X=" + num(freq_x) +
                (freq_y > 0. ? " SHAPER_TYPE_Y=" + shaper_y : std::string()) + " SHAPER_FREQ_Y=" + num(freq_y) + "\n"
            "TUNING_TOWER COMMAND=SET_VELOCITY_LIMIT PARAMETER=ACCEL START=" + num(start_accel) +
                " STEP_DELTA=" + num(step_delta) + " STEP_HEIGHT=" + num(step_height) + "\n" +
            RINGING_BLOCK_END + "\n";
        DynamicPrintConfig filament = wxGetApp().preset_bundle->filaments.get_edited_preset().config;
        {
            // At the end of the filament start G-code (after its own SET_PRESSURE_ADVANCE), for every filament; an earlier block replaced.
            auto *opt = filament.option<ConfigOptionStrings>("start_filament_gcode", true);
            if (opt->values.empty())
                opt->values.emplace_back();
            for (std::string &gcode : opt->values) {
                if (size_t a = gcode.find(RINGING_BLOCK_START); a != std::string::npos) {
                    size_t b = gcode.find(RINGING_BLOCK_END, a);
                    b = b == std::string::npos ? gcode.size() : std::min(gcode.size(), b + strlen(RINGING_BLOCK_END) + 1);
                    gcode.erase(a, b - a);
                }
                gcode += (gcode.empty() || gcode.back() == '\n' ? "" : "\n") + block;
            }
        }
        // The speed mode macro runs after the filament start G-code and would set its own velocity and acceleration.
        if (auto *opt = filament.option<ConfigOptionStrings>("klipper_speed_mode"); opt)
            opt->values.assign(std::max<size_t>(1, opt->values.size()), "Off");
        // Klipper: minimum layer time at most 3 s, else the cooling slowdown lowers the perimeter speed.
        if (auto *opt = filament.option<ConfigOptionInts>("slowdown_below_layer_time"); opt)
            for (int &t : opt->values)
                t = std::min(t, 3);
        load_tab_config(Preset::TYPE_FILAMENT, filament);
        // No acceleration commands (M204 / SET_VELOCITY_LIMIT ACCEL), which would override TUNING_TOWER's.
        DynamicPrintConfig print = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        print.set_key_value("default_acceleration", new ConfigOptionFloatOrPercent(0., false));
        // Klipper's recommended slicing: layer height 0.2 or 0.25 mm, vase mode with a 1-2 mm base (the settings vase
        // mode needs, as ConfigManipulation), external perimeters at the speed used for the frequency (V x N / D).
        const double layer_height = std::abs(print.opt_float("layer_height") - 0.25) < EPSILON ? 0.25 : 0.2;
        print.set_key_value("layer_height",                    new ConfigOptionFloat(layer_height));
        print.set_key_value("spiral_vase",                     new ConfigOptionBool(true));
        print.set_key_value("perimeters",                      new ConfigOptionInt(1));
        print.set_key_value("top_solid_layers",                new ConfigOptionInt(0));
        print.set_key_value("bottom_solid_layers",             new ConfigOptionInt(int(std::ceil(1. / layer_height - EPSILON))));
        print.set_key_value("fill_density",                    new ConfigOptionPercent(0));
        print.set_key_value("support_material",                new ConfigOptionBool(false));
        print.set_key_value("support_material_enforce_layers", new ConfigOptionInt(0));
        print.set_key_value("thin_walls",                      new ConfigOptionBool(false));
        print.set_key_value("external_perimeter_speed",        new ConfigOptionFloatOrPercent(velocity, false));
        print.set_key_value("perimeter_speed",                 new ConfigOptionFloat(velocity));
        load_tab_config(Preset::TYPE_PRINT, print);

        finish_calibration_project(objs);
        return true;
    }

private:
    wxTextCtrl *m_path { nullptr }, *m_velocity { nullptr }, *m_freq_x { nullptr }, *m_freq_y { nullptr };
    wxTextCtrl *m_start_accel { nullptr }, *m_step_delta { nullptr }, *m_step_height { nullptr };
    wxComboBox *m_shaper_x { nullptr }, *m_shaper_y { nullptr };
};

// ------------------------------------------------------------------------------------------------------------------------

void show_calibration_dialog(CalibrationType type)
{
    std::unique_ptr<CalibrationDialog> dlg;
    switch (type) {
    case CalibrationType::Temperature:       dlg = std::make_unique<CalibrationTemperatureDialog>(); break;
    case CalibrationType::PressureAdvance:   dlg = std::make_unique<CalibrationPressureAdvanceDialog>(); break;
    case CalibrationType::Pressure:          dlg = std::make_unique<CalibrationPressureDialog>(); break;
    case CalibrationType::AdaptivePA:        dlg = std::make_unique<CalibrationAdaptivePADialog>(); break;
    case CalibrationType::AdaptivePAResults: dlg = std::make_unique<CalibrationAdaptivePAResultsDialog>(); break;
    case CalibrationType::CaliFlower:        dlg = std::make_unique<CalibrationCaliFlowerDialog>(); break;
    case CalibrationType::Ringing:           dlg = std::make_unique<CalibrationRingingDialog>(); break;
    }
    dlg->ShowModal();
}

} // namespace GUI
} // namespace Slic3r
