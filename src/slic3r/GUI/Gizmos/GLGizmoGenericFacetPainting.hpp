///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GLGizmoGenericFacetPainting_hpp_
#define slic3r_GLGizmoGenericFacetPainting_hpp_

#include <map>
#include <string>

#include "libslic3r/Api/host/Orchestrator.hpp"

#include "GLGizmoPainterBase.hpp"

namespace Slic3r::GUI {

class GLGizmoGenericFacetPainting : public GLGizmoPainterBase
{
public:
    GLGizmoGenericFacetPainting(GLCanvas3D &parent,
                                const std::string &icon_filename,
                                unsigned int sprite_id,
                                GenericFacetsAnnotationDefinition definition,
                                int shortcut_key = GLGizmoBase::NO_SHORTCUT_KEY_VALUE);

    void render_painter_gizmo() override;

protected:
    void on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;
    PainterGizmoType get_painter_type() const override;

    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    std::string get_gizmo_entering_text() const override;
    std::string get_gizmo_leaving_text() const override;
    std::string get_action_snapshot_name() const override;

private:
    bool on_init() override;

    void update_model_object() const override;
    void update_from_model_object() override;

    void on_opening() override {}
    void on_shutdown() override;

    // m_definition is the stable identity and the labels for this painter. The
    // key is used to reach ModelVolume storage; labels are only used by the GUI.
    GenericFacetsAnnotationDefinition m_definition;
    int m_initial_shortcut_key;

    // This map holds translated or plugin-provided description texts, so they
    // can be reused both for drawing and for measuring the ImGui window.
    std::map<std::string, wxString> m_desc;
};

} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoGenericFacetPainting_hpp_
