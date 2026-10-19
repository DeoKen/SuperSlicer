///|/ Ported from SuperSlicer.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_GraphDialog_hpp_
#define slic3r_GUI_GraphDialog_hpp_

#include <wx/dialog.h>
#include <wx/panel.h>

#include "libslic3r/Config.hpp"

class SpinInputDouble;

namespace Slic3r { namespace GUI {

class GraphChart;

// Dialog editing the points of a graph option (coGraph / coGraphs).
class GraphDialog : public wxDialog
{
public:
    // reset_value: the graph set by the "Reset" button (the default value of the option).
    GraphDialog(wxWindow *parent, const GraphData &value, const GraphData &reset_value, const GraphSettings &settings);
    GraphData get_data() const { return m_output_data; }

private:
    GraphData         get_chart_data() const;
    void              set_chart_data(const GraphData &data);

    GraphChart       *m_chart    = nullptr;
    ::SpinInputDouble *m_widget_x = nullptr;
    ::SpinInputDouble *m_widget_y = nullptr;
    GraphData         m_output_data;
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_GraphDialog_hpp_
