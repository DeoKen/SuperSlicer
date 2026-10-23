#include "GCodeSettingsLoader.hpp"

#include <cmath>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/path.hpp>

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "libslic3r/GCodeSettingsImport.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "Tab.hpp"
#include "../Utils/Process.hpp"

namespace Slic3r {
namespace GUI {

void open_gcode_settings_in_new_window(const wxString &gcode_path)
{
    start_new_slicer_with_gcode_settings(gcode_path);
}

namespace {

double nozzle_of(const DynamicPrintConfig &config)
{
    const auto *opt = config.option<ConfigOptionFloats>("nozzle_diameter");
    return opt && ! opt->values.empty() ? opt->values.front() : 0.;
}

// Name of the preset to use: the one the G-code was sliced with if usable, else the selected one if usable, else the first usable.
template<class Usable>
std::string pick_preset(const PresetCollection &presets, const std::string &wanted, Usable usable)
{
    if (const Preset *p = presets.find_preset(wanted); p && ! wanted.empty() && usable(*p))
        return p->name;
    if (usable(presets.get_selected_preset()))
        return presets.get_selected_preset_name();
    for (const Preset &p : presets)
        if (usable(p))
            return p.name;
    return {};
}

void show_report(const wxString &summary, const wxString &details)
{
    wxDialog dlg(wxGetApp().mainframe, wxID_ANY, _L("Settings from G-code"), wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    const int em = wxGetApp().em_unit();
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    auto *text  = new wxStaticText(&dlg, wxID_ANY, summary);
    sizer->Add(text, 0, wxALL, em);
    if (! details.empty()) {
        auto *box = new wxTextCtrl(&dlg, wxID_ANY, details, wxDefaultPosition, wxSize(70 * em, 30 * em),
                                   wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL);
        sizer->Add(box, 1, wxEXPAND | wxLEFT | wxRIGHT, em);
    }
    sizer->Add(dlg.CreateStdDialogButtonSizer(wxOK), 0, wxEXPAND | wxALL, em);
    dlg.SetSizerAndFit(sizer);
    wxGetApp().UpdateDlgDarkUI(&dlg);
    dlg.CenterOnParent();
    dlg.ShowModal();
}

} // namespace

void load_gcode_settings(const std::string &gcode_path)
{
    GCodeSettingsImport imp;
    try {
        imp = import_gcode_settings(gcode_path);
    } catch (const std::exception &ex) {
        show_error(wxGetApp().mainframe, ex.what());
        return;
    }
    PresetBundle &bundle = *wxGetApp().preset_bundle;
    const double  nozzle = nozzle_of(imp.config);
    wxString      problems;

    // Printer with the nozzle of the G-code.
    auto printer_usable = [nozzle](const Preset &p) {
        return p.is_visible && p.printer_technology() == ptFFF && nozzle > 0 && std::abs(nozzle_of(p.config) - nozzle) < 1e-6;
    };
    const std::string printer = pick_preset(bundle.printers, imp.printer_name, printer_usable);
    if (printer.empty()) {
        if (bundle.printers.get_selected_preset().printer_technology() != ptFFF) {
            show_error(wxGetApp().mainframe, format_wxstr(_L("No FFF printer preset with a %1% mm nozzle: the settings of %2% were not loaded."),
                                                          nozzle, from_u8(gcode_path)));
            return;
        }
        problems += format_wxstr(_L("No printer preset with a %1% mm nozzle: the selected printer is kept."), nozzle) + "\n";
    } else if (printer != bundle.printers.get_selected_preset_name())
        wxGetApp().get_tab(Preset::TYPE_PRINTER)->select_preset(printer);

    // Print and filament compatible with that printer.
    auto compatible = [](const Preset &p) { return p.is_visible && p.is_compatible; };
    for (auto [type, collection, wanted] : { std::make_tuple(Preset::TYPE_PRINT, &bundle.prints, imp.print_name),
                                             std::make_tuple(Preset::TYPE_FILAMENT, &bundle.filaments, imp.filament_name) }) {
        const std::string name = pick_preset(*collection, wanted, compatible);
        if (! name.empty() && name != collection->get_selected_preset_name())
            wxGetApp().get_tab(type)->select_preset(name);
    }

    // The values, as unsaved changes. The preset identity (names, compatibility conditions) stays the preset's.
    DynamicPrintConfig config = imp.config;
    for (const char *key : { "inherits", "compatible_printers", "compatible_printers_condition", "compatible_prints",
                             "compatible_prints_condition", "print_settings_id", "filament_settings_id", "printer_settings_id",
                             "physical_printer_settings_id", "printer_model", "printer_variant", "printer_vendor", "preset_names" })
        config.erase(key);
    size_t changed = 0;
    for (Tab *tab : wxGetApp().tabs_list) {
        if (! tab->supports_printer_technology(ptFFF))
            continue;
        DynamicPrintConfig *edited = tab->get_config();
        for (const std::string &key : config.keys()) {
            ConfigOption *dst = edited->option(key);
            if (dst == nullptr)
                continue;
            const ConfigOption *src = config.option(key);
            std::unique_ptr<ConfigOption> resized;
            if (src->is_vector() && dst->is_vector()) {
                // Same number of extruders as the preset (a multi extruder G-code gives its first extruder's values).
                const auto *dst_vec = static_cast<const ConfigOptionVectorBase*>(dst);
                const auto *src_vec = static_cast<const ConfigOptionVectorBase*>(src);
                if (src_vec->size() != dst_vec->size() && src_vec->size() > 0) {
                    resized.reset(src->clone());
                    static_cast<ConfigOptionVectorBase*>(resized.get())->resize(dst_vec->size(), dst);
                    src = resized.get();
                }
            }
            if (*dst != *src) {
                dst->set(src);
                ++ changed;
            }
        }
    }
    wxGetApp().load_current_presets();

    const std::string generator = imp.generator.empty() ? std::string("unknown slicer") : imp.generator + " " + imp.generator_version;
    wxString summary = format_wxstr(_L("Settings of %1% (%2%) loaded as unsaved changes over:"),
                                    from_u8(boost::filesystem::path(gcode_path).filename().string()), from_u8(generator)) + "\n";
    auto line = [](const wxString &label, const std::string &used, const std::string &in_gcode) {
        wxString s = "    " + label + ": " + from_u8(used);
        if (! in_gcode.empty() && in_gcode != used)
            s += "  " + format_wxstr(_L("(in the G-code: %1%)"), from_u8(in_gcode));
        return s + "\n";
    };
    summary += line(_L("Printer"), bundle.printers.get_selected_preset_name(), imp.printer_name);
    summary += line(_L("Print"), bundle.prints.get_selected_preset_name(), imp.print_name);
    summary += line(_L("Filament"), bundle.filaments.get_selected_preset_name(), imp.filament_name);
    summary += format_wxstr(_L("%1% settings differ from these presets. Add the model to slice it."), changed);
    if (! problems.empty())
        summary += "\n\n" + problems;

    wxString details;
    auto section = [&details](const wxString &title, const std::vector<std::string> &lines) {
        if (lines.empty())
            return;
        details += title + format_wxstr(" (%1%):", lines.size()) + "\n";
        for (const std::string &l : lines)
            details += "    " + from_u8(l) + "\n";
        details += "\n";
    };
    section(imp.from_superslicer ? _L("Not carried over: SuperSlicer settings SuperPSlicer does not have") :
                                   _L("Not carried over: settings SuperPSlicer does not have"), imp.not_imported);
    section(_L("Not carried over: values SuperPSlicer cannot read, kept from the preset"), imp.invalid);
    section(_L("Converted from SuperSlicer"), imp.notes);
    if (details.empty())
        summary += "\n\n" + _L("All the settings were carried over.");
    show_report(summary, details);
}

} // namespace GUI
} // namespace Slic3r
