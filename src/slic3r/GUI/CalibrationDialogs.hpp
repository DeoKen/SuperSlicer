#ifndef slic3r_GUI_CalibrationDialogs_hpp_
#define slic3r_GUI_CalibrationDialogs_hpp_

// Calibration prints (Calibration menu): the temperature tower ported from SuperSlicer and the CaliFlower (Vector 3D, the
// user's own model file).

#include "GUI_Utils.hpp"
#include "libslic3r/Point.hpp"

#include <string>
#include <vector>

class wxComboBox;
class wxTextCtrl;
class wxFlexGridSizer;

namespace Slic3r {

class ModelObject;
class ModelVolume;

namespace GUI {

enum class CalibrationType {
    Temperature,
    CaliFlower,
};

// Opens the dialog of a calibration print (modal).
void show_calibration_dialog(CalibrationType type);

// A dialog with a short explanation, a few fields and a Generate button. Generate starts a new project (asking to save the
// current one) and loads the calibration print with its own object / part settings; the dialog closes when it is done.
class CalibrationDialog : public DPIDialog
{
public:
    CalibrationDialog(const wxString &title, const wxString &explanation);

protected:
    // Fields, in a two column grid under the explanation.
    wxComboBox* add_combo(const wxString &label, const wxString &tooltip, const std::vector<wxString> &choices, const wxString &value);
    wxTextCtrl* add_text(const wxString &label, const wxString &tooltip, const wxString &value);
    // Call at the end of the derived constructor.
    void        finish_layout();
    // Value of a combo box or a text field, also with a decimal comma; false and an error message if it isn't a number.
    bool        get_number(wxWindow *field, const wxString &label, double &value);

    // Creates the calibration print. False if nothing was done (e.g. the new project was cancelled or invalid values).
    virtual bool generate() = 0;

    void on_dpi_changed(const wxRect &suggested_rect) override;

    // Adds the volumes of a model file to the object as parts, moved by move (in the coordinates of the file, as the
    // object was loaded) and scaled by scale.
    static ModelVolume* add_part(ModelObject *object, const std::string &file, const Vec3d &move, const Vec3d &scale);
    // resources/calibration/<dir>/<file>
    static std::string  resource(const std::string &dir, const std::string &file);

private:
    wxFlexGridSizer *m_grid { nullptr };
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_CalibrationDialogs_hpp_
