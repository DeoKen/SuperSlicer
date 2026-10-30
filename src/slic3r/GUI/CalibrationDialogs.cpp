#include "CalibrationDialogs.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/FileReader.hpp"
#include "libslic3r/GCode/PressureAdvancePattern.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PressureCalibration.hpp"
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
        // The settings into the print and filament settings (unsaved changes), the print ones also as SuperSlicer changes them.
        DynamicPrintConfig print    = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        DynamicPrintConfig filament = wxGetApp().preset_bundle->filaments.get_edited_preset().config;
        DynamicPrintConfig config   = current_config();
        try {
            pressure_calibration_apply_settings(print, settings);
            pressure_calibration_apply_settings(filament, settings);
            pressure_calibration_apply_settings(config, settings);
        } catch (const std::exception &ex) {
            show_error(this, from_u8(ex.what()));
            return false;
        }
        pressure_calibration_print_config(print);
        pressure_calibration_print_config(config);
        const double nozzle = nozzle_diameter(config);
        const bool   bends  = std::any_of(tests.begin(), tests.end(), [&settings](const PressureCalibrationTest &t) {
            return pressure_calibration_wall_objects(settings, t) == 0; });
        if (bends && pressure_calibration_bend_file(resources_dir(), nozzle).empty()) {
            show_error(this, format_wxstr(_L("There is no 90° bend model for a %1% mm nozzle (0.1 to 2.0 mm by 0.1 mm)."), nozzle));
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
            if (! pressure_calibration_build(*frames.back(), config, settings, tests[id_item], id_item, resources_dir()))
                slice = false;
        }
        // How the test was made, as comments at the start of the first layer (Ellis writes them at the top of his G-code).
        CustomGCode::Info &custom = model.custom_gcode_per_print_z();
        custom.mode = wxGetApp().extruders_edited_cnt() > 1 ? CustomGCode::MultiAsSingle : CustomGCode::SingleExtruder;
        custom.gcodes.push_back({ settings.first_layer_height, CustomGCode::Custom, -1, "", pressure_calibration_summary(config, settings, tests) });

        load_tab_config(Preset::TYPE_PRINT, print);
        load_tab_config(Preset::TYPE_FILAMENT, filament);

        // Arrange the frames while the objects of the walls are still the empty base plate, then the walls on their frame (same
        // instance transformation, same coordinates). CheckAll: not sliced, so that the values can be typed in first.
        finish_calibration_project(objs, true, false);
        for (size_t id_item = 0; id_item < tests.size(); ++ id_item)
            if (! walls[id_item].empty()) {
                for (ModelObject *wall : walls[id_item])
                    wall->instances.front()->set_transformation(frames[id_item]->instances.front()->get_transformation());
                pressure_calibration_build_walls(walls[id_item], config, settings, tests[id_item], id_item);
            }
        finish_calibration_project(objs, false, slice);
        return true;
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
    case CalibrationType::Temperature:     dlg = std::make_unique<CalibrationTemperatureDialog>(); break;
    case CalibrationType::PressureAdvance: dlg = std::make_unique<CalibrationPressureAdvanceDialog>(); break;
    case CalibrationType::Pressure:        dlg = std::make_unique<CalibrationPressureDialog>(); break;
    case CalibrationType::CaliFlower:      dlg = std::make_unique<CalibrationCaliFlowerDialog>(); break;
    case CalibrationType::Ringing:         dlg = std::make_unique<CalibrationRingingDialog>(); break;
    }
    dlg->ShowModal();
}

} // namespace GUI
} // namespace Slic3r
