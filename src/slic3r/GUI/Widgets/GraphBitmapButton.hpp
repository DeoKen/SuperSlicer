///|/ Ported from SuperSlicer.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_GraphBitmapButton_hpp_
#define slic3r_GUI_GraphBitmapButton_hpp_

#include <wx/bmpbuttn.h>

namespace Slic3r {
    struct GraphData;
    struct GraphSettings;
} // namespace Slic3r

// Button showing the curve of a graph option, a click opens the graph dialog.
class GraphBitmapButton : public wxBitmapButton
{
public:
    GraphBitmapButton(wxWindow *parent, const wxSize &size);

    void update_bitmap(const Slic3r::GraphSettings &graph_settings, const Slic3r::GraphData &data);
    // New size of the bitmap (rescale), the curve is redrawn by the next update_bitmap().
    void set_bitmap_size(const wxSize &size);

private:
    void draw_bitmap(wxBitmap &bitmap, const wxColour &curve_colour, const wxColour &background, bool solid_background,
                     const Slic3r::GraphSettings &graph_settings, const Slic3r::GraphData &data) const;

    wxSize m_bitmap_size;
};

#endif // slic3r_GUI_GraphBitmapButton_hpp_
