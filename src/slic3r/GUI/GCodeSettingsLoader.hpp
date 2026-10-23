#ifndef slic3r_GUI_GCodeSettingsLoader_hpp_
#define slic3r_GUI_GCodeSettingsLoader_hpp_

// Fork: open the settings stored in a G-code file in a new SuperPSlicer window (G-code viewer File menu and notification,
// slicer File > Import). The new window gets the startup option --gcode-settings.

#include <string>

#include <wx/string.h>

namespace Slic3r {
namespace GUI {

// Start a new SuperPSlicer window loading the settings of the G-code file.
void open_gcode_settings_in_new_window(const wxString &gcode_path);

// In the new window: select the presets for the nozzle of the G-code, load its settings over them as unsaved changes and
// list what could not be carried over.
void load_gcode_settings(const std::string &gcode_path);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_GCodeSettingsLoader_hpp_
