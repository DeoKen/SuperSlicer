#ifndef slic3r_GLGizmoBrimPaint_hpp_
#define slic3r_GLGizmoBrimPaint_hpp_

#include "GLGizmoPainterBase.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "libslic3r/ExPolygon.hpp"

namespace Slic3r::GUI {

// Paint-on brim (fork): brim enforcers / blockers painted on the model, like the seam painting.
class GLGizmoBrimPaint : public GLGizmoPainterBase
{
public:
    GLGizmoBrimPaint(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
        : GLGizmoPainterBase(parent, icon_filename, sprite_id) {}

    void render_painter_gizmo() override;
    // Edge tool: the mouse events go to it, else to the brush (GLGizmoPainterBase).
    bool gizmo_event(SLAGizmoEventType action, const Vec2d &mouse_position, bool shift_down, bool alt_down, bool control_down) override;

protected:
    void on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;
    PainterGizmoType get_painter_type() const override;

    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    std::string get_gizmo_entering_text() const override { return _u8L("Entering Brim painting"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving Brim painting"); }
    std::string get_action_snapshot_name() const override { return _u8L("Paint-on brim editing"); }

private:
    bool on_init() override;

    void update_model_object() const override;
    void update_from_model_object() override;

    void on_opening() override {}
    void on_shutdown() override;

    // First layer only: the model above the first layer height is hidden and can't be painted (a horizontal clipping plane).
    void update_first_layer_clipping();
    bool m_first_layer_only{ true };

    // Edge tool: a click paints the bottom edge under the cursor, up to the corners sharper than m_edge_angle (degrees).
    double first_layer_mid_z() const;
    // The first layer outline of the object (world coordinates), sliced at the middle of the first layer; cached.
    const ExPolygons& first_layer_outline();
    // The edge of the outline under the mouse (world coordinates), empty if none.
    std::vector<Vec3d> edge_under_mouse(const Vec2d &mouse_position);
    void paint_edge(const std::vector<Vec3d> &edge, TriangleStateType state);
    void render_hover_edge();

    // Radius of the capsules painting an edge (mm): the wall band of the first layer, not past the corners.
    static constexpr float EdgeRadius = 0.3f;
    bool                m_edge_tool{ true };
    float               m_edge_angle{ 30.f };
    std::vector<Vec3d>  m_hover_edge;
    GLModel             m_hover_edge_model;
    bool                m_edge_clicked{ false };
    ExPolygons          m_outline;
    std::vector<double> m_outline_key;

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, std::string> m_desc;
};

} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoBrimPaint_hpp_
