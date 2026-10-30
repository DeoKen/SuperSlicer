#include "GLGizmoBrimPaint.hpp"

#include "libslic3r/Model.hpp"

//#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/CameraUtils.hpp"
#include "slic3r/GUI/OpenGLManager.hpp"
#include "slic3r/GUI/GLShader.hpp"
#include "slic3r/GUI/MeshUtils.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include <GL/glew.h>


namespace Slic3r::GUI {



void GLGizmoBrimPaint::on_shutdown()
{
    // The clipper is shared with the other painting gizmos: don't leave them the first layer plane.
    if (m_first_layer_only)
        m_c->object_clipper()->set_position_by_ratio(0., false);
    m_hover_edge.clear();
    m_hover_edge_model.reset();
    m_outline.clear();
    m_outline_key.clear();
    m_parent.toggle_model_objects_visibility(true);
}



// The first layer height of the object (its own setting, else the print settings).
static double first_layer_height(const ModelObject &mo)
{
    auto option = [&mo](const char *key) {
        const ConfigOption *opt = mo.config.option(key);
        return opt != nullptr ? opt : wxGetApp().plater()->config()->option(key);
    };
    // A percentage is of the layer height.
    const auto         *first_layer_height = dynamic_cast<const ConfigOptionFloatOrPercent*>(option("first_layer_height"));
    const ConfigOption *layer_height       = option("layer_height");
    return first_layer_height != nullptr ? first_layer_height->get_abs_value(layer_height != nullptr ? layer_height->getFloat() : 0.2) : 0.2;
}

void GLGizmoBrimPaint::update_first_layer_clipping()
{
    const ModelObject *mo = m_c->selection_info()->model_object();
    if (! m_first_layer_only || mo == nullptr)
        return;
    // The facets of the first layer are the ones the brim uses (PrintObject::brim_paint_area()), so the plane is a hair
    // above it.
    const double        z = first_layer_height(*mo) + 0.001;
    const ClippingPlane plane(Vec3d::UnitZ(), z);
    // Set again if anything else moved it (Ctrl + mouse wheel, the selection changed).
    const ClippingPlane *current = m_c->object_clipper()->get_clipping_plane(true);
    if (current == nullptr || ! (*current == plane) || m_c->object_clipper()->get_position() == 0.)
        m_c->object_clipper()->set_range_and_pos(Vec3d::UnitZ(), z, 1.);
}



double GLGizmoBrimPaint::first_layer_mid_z() const
{
    const ModelObject *mo = m_c->selection_info()->model_object();
    return mo == nullptr ? 0.1 : 0.5 * first_layer_height(*mo);
}

const ExPolygons& GLGizmoBrimPaint::first_layer_outline()
{
    const ModelObject *mo = m_c->selection_info()->model_object();
    if (mo == nullptr) {
        m_outline.clear();
        m_outline_key.clear();
        return m_outline;
    }
    const int         instance_idx = m_parent.get_selection().get_instance_idx();
    const Transform3d instance     = mo->instances[instance_idx < 0 ? 0 : instance_idx]->get_transformation().get_matrix();
    const double      z            = first_layer_mid_z();
    // Sliced again when the object, its parts, their placement or the first layer height changed.
    std::vector<double> key{ double(mo->id().id), z };
    key.insert(key.end(), instance.data(), instance.data() + 16);
    for (const ModelVolume *mv : mo->volumes)
        if (mv->is_model_part()) {
            const Transform3d m = mv->get_matrix();
            key.emplace_back(double(mv->id().id));
            key.emplace_back(double(mv->mesh().its.indices.size()));
            key.insert(key.end(), m.data(), m.data() + 16);
        }
    if (key != m_outline_key) {
        ExPolygons outline;
        for (const ModelVolume *mv : mo->volumes)
            if (mv->is_model_part()) {
                MeshSlicingParamsEx params;
                params.trafo = instance * mv->get_matrix();
                append(outline, slice_mesh_ex(mv->mesh().its, { float(z) }, params).front());
            }
        m_outline     = union_ex(outline);
        m_outline_key = std::move(key);
    }
    return m_outline;
}

std::vector<Vec3d> GLGizmoBrimPaint::edge_under_mouse(const Vec2d &mouse_position)
{
    std::vector<Vec3d> out;
    const double z = first_layer_mid_z();
    // The point of the first layer under the mouse: the mouse ray at the middle of the first layer.
    Vec3d point, direction;
    CameraUtils::ray_from_screen_pos(wxGetApp().plater()->get_camera(), mouse_position, point, direction);
    if (std::abs(direction.z()) < EPSILON)
        return out;
    const double t = (z - point.z()) / direction.z();
    if (t < 0.)
        return out;
    const Point mouse = Point::new_scale(point.x() + t * direction.x(), point.y() + t * direction.y());

    // The nearest outline segment, within 2.5 mm.
    const Polygon *best_polygon = nullptr;
    size_t         best_idx     = 0;
    double         best_dist    = scaled<double>(2.5);
    for (const ExPolygon &expoly : first_layer_outline())
        for (const Polygon *polygon : { &expoly.contour })
            for (size_t i = 0; i < polygon->size(); ++ i)
                if (const double d = Line(polygon->points[i], polygon->points[(i + 1) % polygon->size()]).distance_to(mouse); d < best_dist) {
                    best_dist = d; best_polygon = polygon; best_idx = i;
                }
    for (const ExPolygon &expoly : first_layer_outline())
        for (const Polygon &hole : expoly.holes)
            for (size_t i = 0; i < hole.size(); ++ i)
                if (const double d = Line(hole.points[i], hole.points[(i + 1) % hole.size()]).distance_to(mouse); d < best_dist) {
                    best_dist = d; best_polygon = &hole; best_idx = i;
                }
    if (best_polygon == nullptr)
        return out;

    // From the segment both ways, up to the corners sharper than m_edge_angle (a turn of more than that between two
    // segments), or around the whole outline when it has none.
    const Points &pts   = best_polygon->points;
    const size_t  n     = pts.size();
    const double  limit = Geometry::deg2rad(double(m_edge_angle));
    auto turn = [&pts, n](size_t v) {
        const Vec2d a = (pts[v] - pts[(v + n - 1) % n]).cast<double>();
        const Vec2d b = (pts[(v + 1) % n] - pts[v]).cast<double>();
        return std::abs(std::atan2(a.x() * b.y() - a.y() * b.x(), a.dot(b)));
    };
    size_t first = best_idx, steps = 1;
    while (steps < n && turn(first) <= limit) {
        first = (first + n - 1) % n;
        ++ steps;
    }
    size_t last = (best_idx + 1) % n;
    while (steps < n && turn(last) <= limit) {
        last = (last + 1) % n;
        ++ steps;
    }
    const bool closed = steps >= n;
    Polyline   edge;
    for (size_t i = 0, v = first; i <= steps; ++ i, v = (v + 1) % n)
        edge.points.emplace_back(pts[v]);
    // Not past the corners: the brush is EdgeRadius wide, the next wall is not painted. An edge too short for that is
    // not offered (Polyline::clip_end() must not get to an empty polyline), the brush paints it.
    if (! closed) {
        if (edge.length() < 3. * scaled<double>(EdgeRadius))
            return out;
        edge.clip_start(scaled<double>(EdgeRadius));
        edge.clip_end(scaled<double>(EdgeRadius));
    }
    // Max length: the part of the edge of that length centered on the cursor, moved along so it stays within the edge
    // (an edge not longer is painted whole); around a closed outline it may go over the outline's start.
    if (const double max_length = scaled<double>(m_edge_max_length), length = edge.length(); max_length > 0. && max_length < length) {
        // Distance along the edge of the point nearest to the cursor.
        double along = 0., best = std::numeric_limits<double>::max(), at = 0.;
        for (size_t i = 0; i + 1 < edge.size(); ++ i) {
            const Line   line(edge.points[i], edge.points[i + 1]);
            const Vec2d  v   = (line.b - line.a).cast<double>();
            const double len = v.norm();
            const double t   = len > 0. ? std::clamp((mouse - line.a).cast<double>().dot(v) / (len * len), 0., 1.) : 0.;
            if (const double d = (line.a.cast<double>() + t * v - mouse.cast<double>()).norm(); d < best) {
                best = d;
                at   = along + t * len;
            }
            along += len;
        }
        double start = at - 0.5 * max_length;
        if (closed) {
            if (start < 0.)
                start += length;
            // Twice around, so that the part may go over the start.
            Points twice = edge.points;
            twice.insert(twice.end(), edge.points.begin() + 1, edge.points.end());
            edge.points = std::move(twice);
        } else
            start = std::clamp(start, 0., length - max_length);
        // The part from `start` to `start + max_length`.
        Polyline part;
        along = 0.;
        for (size_t i = 0; i + 1 < edge.size() && along < start + max_length; ++ i) {
            const Vec2d  a   = edge.points[i].cast<double>();
            const Vec2d  v   = edge.points[i + 1].cast<double>() - a;
            const double len = v.norm();
            if (len > 0. && along + len > start) {
                if (part.empty())
                    part.points.emplace_back((a + v * ((start - along) / len)).cast<coord_t>());
                if (along + len >= start + max_length)
                    part.points.emplace_back((a + v * ((start + max_length - along) / len)).cast<coord_t>());
                else
                    part.points.emplace_back(edge.points[i + 1]);
            }
            along += len;
        }
        edge = std::move(part);
    }
    for (const Point &p : edge.points)
        out.emplace_back(unscaled(p.x()), unscaled(p.y()), z);
    return out;
}

void GLGizmoBrimPaint::paint_edge(const std::vector<Vec3d> &edge, TriangleStateType state)
{
    const ModelObject *mo = m_c->selection_info()->model_object();
    if (mo == nullptr || edge.size() < 2)
        return;
    const ModelInstance *mi                   = mo->instances[m_parent.get_selection().get_instance_idx()];
    const Transform3d    instance             = mi->get_transformation().get_matrix();
    const Transform3d    instance_no_offset   = mi->get_transformation().get_matrix_no_offset();
    const Camera        &camera               = wxGetApp().plater()->get_camera();
    const std::vector<const MeshRaycaster*> raycasters = m_c->raycaster()->raycasters();
    int idx = -1;
    for (const ModelVolume *mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;
        ++ idx;
        if (idx >= int(m_triangle_selectors.size()) || idx >= int(raycasters.size()))
            break;
        const Transform3d trafo            = instance * mv->get_matrix();
        const Transform3d trafo_no_offset  = instance_no_offset * mv->get_matrix_no_offset();
        const Transform3d inverse          = trafo.inverse();
        const Vec3f       camera_pos       = (inverse * camera.get_position()).cast<float>();
        const TriangleSelector::ClippingPlane clp = this->get_clipping_plane_in_volume_coordinates(trafo);
        // A thin capsule along each segment of the edge, at the middle of the first layer: the wall band of the first
        // layer (and a strip of the bottom face) is painted, the facets are split at the capsule.
        for (size_t i = 0; i + 1 < edge.size(); ++ i) {
            const Vec3f a = (inverse * edge[i]).cast<float>();
            const Vec3f b = (inverse * edge[i + 1]).cast<float>();
            if ((a - b).norm() < EPSILON)
                continue;
            const int facet = raycasters[idx]->get_closest_facet(a);
            if (facet < 0)
                continue;
            m_triangle_selectors[idx]->select_patch(facet,
                TriangleSelector::DoublePointCursor::cursor_factory(a, b, camera_pos, EdgeRadius, TriangleSelector::CursorType::SPHERE, trafo, clp),
                state, trafo_no_offset, true);
        }
        m_triangle_selectors[idx]->request_update_render_data();
    }
}

bool GLGizmoBrimPaint::gizmo_event(SLAGizmoEventType action, const Vec2d &mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    if (! m_edge_tool)
        return GLGizmoPainterBase::gizmo_event(action, mouse_position, shift_down, alt_down, control_down);

    switch (action) {
    case SLAGizmoEventType::Moving: {
        std::vector<Vec3d> edge = edge_under_mouse(mouse_position);
        if (edge != m_hover_edge) {
            m_hover_edge = std::move(edge);
            m_hover_edge_model.reset();
            m_parent.set_as_dirty();
        }
        return false;
    }
    case SLAGizmoEventType::LeftDown:
    case SLAGizmoEventType::RightDown: {
        m_hover_edge = edge_under_mouse(mouse_position);
        m_hover_edge_model.reset();
        if (m_hover_edge.size() < 2 || m_triangle_selectors.empty())
            return false;
        const bool left = action == SLAGizmoEventType::LeftDown;
        paint_edge(m_hover_edge, shift_down ? TriangleStateType::NONE :
                                 left       ? this->get_left_button_state_type() : this->get_right_button_state_type());
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), handle_snapshot_action_name(shift_down, left ? Button::Left : Button::Right),
                                      UndoRedo::SnapshotType::GizmoAction);
        update_model_object();
        m_edge_clicked = true;
        m_parent.set_as_dirty();
        return true;
    }
    case SLAGizmoEventType::Dragging:
        // No scene rotation while the button that painted an edge is down.
        return m_edge_clicked;
    case SLAGizmoEventType::LeftUp:
    case SLAGizmoEventType::RightUp: {
        const bool clicked = m_edge_clicked;
        m_edge_clicked = false;
        return clicked;
    }
    default:
        return GLGizmoPainterBase::gizmo_event(action, mouse_position, shift_down, alt_down, control_down);
    }
}

void GLGizmoBrimPaint::render_hover_edge()
{
    if (m_hover_edge.size() < 2)
        return;
    if (! m_hover_edge_model.is_initialized()) {
        GLModel::Geometry init_data;
        init_data.format = { GLModel::Geometry::EPrimitiveType::LineStrip, GLModel::Geometry::EVertexLayout::P3 };
        init_data.reserve_vertices(m_hover_edge.size());
        init_data.reserve_indices(m_hover_edge.size());
        for (const Vec3d &p : m_hover_edge) {
            // Drawn at the top of the first layer, so the see-through cut doesn't hide it.
            init_data.add_vertex(Vec3f(float(p.x()), float(p.y()), float(2. * p.z() + 0.01)));
            init_data.add_index(unsigned(init_data.vertices_count() - 1));
        }
        m_hover_edge_model.init_from(std::move(init_data));
    }
    GLShaderProgram *shader = OpenGLManager::get_gl_info().is_core_profile() ? wxGetApp().get_shader("dashed_thick_lines") : wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;
    const Camera &camera = wxGetApp().plater()->get_camera();
    shader->start_using();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix());
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    shader->set_uniform("width", 1.5f);
    m_hover_edge_model.set_color(ColorRGBA::YELLOW());
    glsafe(::glDisable(GL_DEPTH_TEST));
    m_hover_edge_model.render();
    glsafe(::glEnable(GL_DEPTH_TEST));
    shader->stop_using();
}



bool GLGizmoBrimPaint::on_init()
{
    m_desc["clipping_of_view"] = _u8L("Clipping of view") + ": ";
    m_desc["reset_direction"]  = _u8L("Reset direction");
    m_desc["cursor_size"]      = _u8L("Brush size") + ": ";
    m_desc["enforce_caption"]  = _u8L("Left mouse button") + ": ";
    m_desc["enforce"]          = _u8L("Enforce brim");
    m_desc["tool_type"]        = _u8L("Tool type") + ": ";
    m_desc["edge"]             = _u8L("Edge");
    m_desc["brush"]            = _u8L("Brush");
    m_desc["edge_angle"]       = _u8L("Corner angle") + ": ";
    m_desc["edge_tooltip"]     = _u8L("A click paints the bottom edge under the cursor (highlighted), up to the "
                                      "corners sharper than the corner angle: a round outline is painted all around. "
                                      "With a max length, only that much of it, centered on the cursor.");
    m_desc["edge_max_length"]  = _u8L("Max length") + ": ";
    m_desc["edge_max_length_tooltip"] = _u8L("Paints at most this length of the edge, centered on the cursor; near a corner "
                                             "it stays within the edge. 0 = the whole edge.");
    m_desc["edge_whole"]       = _u8L("whole edge");
    m_desc["block_caption"]    = _u8L("Right mouse button") + ": ";
    m_desc["block"]            = _u8L("Block brim");
    m_desc["remove_caption"]   = _u8L("Shift + Left mouse button") + ": ";
    m_desc["remove"]           = _u8L("Remove selection");
    m_desc["remove_all"]       = _u8L("Remove all selection");
    m_desc["no_brim_width"]    = _u8L("Brim width is 0: the painted brim needs a brim width (Print Settings > Skirt and brim, "
                                      "or a per-object setting).");
    m_desc["raft"]             = _u8L("No brim is made on a raft.");
    m_desc["first_layer_only"] = _u8L("First layer only");
    m_desc["first_layer_only_tooltip"] = _u8L("Shows and paints only the first layer of the part, the only one the brim uses. "
                                              "The brush paints only facets facing you: look from below to paint the bottom face.");
    m_desc["hint"]             = _u8L("Paint the bottom edge of the part (the side walls near the bed or the bottom face). "
                                      "Enforced: a brim along the painted edge, also with the brim type \"No brim\". "
                                      "Blocked: no brim there. The brim width and separation come from the print settings.");

    return true;
}



std::string GLGizmoBrimPaint::on_get_name() const
{
    return _u8L("Brim painting");
}

void GLGizmoBrimPaint::render_painter_gizmo()
{
    const Selection& selection = m_parent.get_selection();

    update_first_layer_clipping();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    render_triangles(selection);

    // First layer only: a see-through cut, so the painting of the bottom face shows from above.
    m_c->object_clipper()->render_cut(nullptr, m_first_layer_only ? 0.4f : 1.f);
    m_c->instances_hider()->render_cut();
    if (m_edge_tool)
        render_hover_edge();
    else
        render_cursor();

    glsafe(::glDisable(GL_BLEND));
}



void GLGizmoBrimPaint::on_render_input_window(float x, float y, float bottom_limit)
{
    if (! m_c->selection_info()->model_object())
        return;

    const float approx_height = m_imgui->scaled(21.f);
    y = std::min(y, bottom_limit - approx_height);
    ImGuiPureWrap::set_next_window_pos(x, y, ImGuiCond_Always);
    ImGuiPureWrap::begin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    // First calculate width of all the texts that are could possibly be shown. We will decide set the dialog width based on that:
    const float clipping_slider_left = std::max(ImGuiPureWrap::calc_text_size(m_desc.at("clipping_of_view")).x,
                                                ImGuiPureWrap::calc_text_size(m_desc.at("reset_direction")).x)
                                           + m_imgui->scaled(1.5f);
    const float cursor_size_slider_left = ImGuiPureWrap::calc_text_size(m_desc.at("cursor_size")).x + m_imgui->scaled(1.f);

    const float button_width = ImGuiPureWrap::calc_text_size(m_desc.at("remove_all")).x + m_imgui->scaled(1.f);
    const float minimal_slider_width = m_imgui->scaled(4.f);

    float caption_max    = 0.f;
    float total_text_max = 0.f;
    for (const auto &t : std::array<std::string, 3>{"enforce", "block", "remove"}) {
        caption_max    = std::max(caption_max, ImGuiPureWrap::calc_text_size(m_desc[t + "_caption"]).x);
        total_text_max = std::max(total_text_max, ImGuiPureWrap::calc_text_size(m_desc[t]).x);
    }
    total_text_max += caption_max + m_imgui->scaled(1.f);
    caption_max    += m_imgui->scaled(1.f);

    const float sliders_left_width = std::max(cursor_size_slider_left, clipping_slider_left);
    const float slider_icon_width  = ImGuiPureWrap::get_slider_icon_size().x;
    float       window_width       = minimal_slider_width + sliders_left_width + slider_icon_width;
    window_width = std::max(window_width, total_text_max);
    window_width = std::max(window_width, button_width);

    auto draw_text_with_caption = [&caption_max](const std::string& caption, const std::string& text) {
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, caption);
        ImGui::SameLine(caption_max);
        ImGuiPureWrap::text(text);
    };

    for (const auto &t : std::array<std::string, 3>{"enforce", "block", "remove"})
        draw_text_with_caption(m_desc.at(t + "_caption"), m_desc.at(t));

    ImGui::Separator();
    ImGuiPureWrap::text_wrapped(m_desc.at("hint"), window_width);
    // The painting has no effect without a brim width or with a raft (PrintObject::has_brim()): say so.
    {
        const ModelObject        *mo     = m_c->selection_info()->model_object();
        const DynamicPrintConfig &config = *wxGetApp().plater()->config();
        // The object's own setting, else the print settings.
        auto option = [mo, &config](const char *key) {
            const ConfigOption *opt = mo->config.option(key);
            return opt != nullptr ? opt : config.option(key);
        };
        const ConfigOption *brim_width  = option("brim_width");
        const ConfigOption *raft_layers = option("raft_layers");
        for (const auto &[problem, text] : { std::make_pair(brim_width == nullptr || brim_width->getFloat() <= 0., "no_brim_width"),
                                             std::make_pair(raft_layers != nullptr && raft_layers->getInt() > 0, "raft") })
            if (problem) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGuiPureWrap::COL_ORANGE_LIGHT);
                ImGuiPureWrap::text_wrapped(m_desc.at(text), window_width);
                ImGui::PopStyleColor();
            }
    }
    ImGui::Separator();

    const float max_tooltip_width = ImGui::GetFontSize() * 20.0f;

    // Tool type: Edge (fork, default) or the stock brush.
    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(m_desc.at("tool_type"));
    ImGui::SameLine(sliders_left_width);
    if (ImGuiPureWrap::radio_button(m_desc.at("edge"), m_edge_tool)) {
        m_edge_tool = true;
        m_parent.set_as_dirty();
    }
    if (ImGui::IsItemHovered())
        ImGuiPureWrap::tooltip(m_desc.at("edge_tooltip"), max_tooltip_width);
    ImGui::SameLine();
    if (ImGuiPureWrap::radio_button(m_desc.at("brush"), ! m_edge_tool)) {
        m_edge_tool = false;
        m_hover_edge.clear();
        m_hover_edge_model.reset();
        m_parent.set_as_dirty();
    }

    if (m_edge_tool) {
        ImGui::AlignTextToFramePadding();
        ImGuiPureWrap::text(m_desc.at("edge_angle"));
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
        if (m_imgui->slider_float("##edge_angle", &m_edge_angle, 5.f, 90.f, "%.0f°", 1.0f, true))
            m_hover_edge.clear();
        ImGui::AlignTextToFramePadding();
        ImGuiPureWrap::text(m_desc.at("edge_max_length"));
        if (ImGui::IsItemHovered())
            ImGuiPureWrap::tooltip(m_desc.at("edge_max_length_tooltip"), max_tooltip_width);
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
        // 0 shown as "0 (whole edge)".
        const std::string whole = "%.0f (" + m_desc.at("edge_whole") + ")";
        if (m_imgui->slider_float("##edge_max_length", &m_edge_max_length, 0.f, 100.f,
                                  m_edge_max_length > 0.f ? "%.1f mm" : whole.c_str(), 1.0f, true,
                                  from_u8(m_desc.at("edge_max_length_tooltip")))) {
            m_edge_max_length = std::max(0.f, m_edge_max_length);
            m_hover_edge.clear();
        }
    } else {
        ImGui::AlignTextToFramePadding();
        ImGuiPureWrap::text(m_desc.at("cursor_size"));
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
        m_imgui->slider_float("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f", 1.0f, true, _L("Alt + Mouse wheel"));
    }

    ImGui::Separator();
    if (ImGuiPureWrap::checkbox(m_desc.at("first_layer_only"), m_first_layer_only)) {
        if (! m_first_layer_only)
            wxGetApp().CallAfter([this]() { m_c->object_clipper()->set_position_by_ratio(0., false); });
        m_parent.set_as_dirty();
    }
    if (ImGui::IsItemHovered())
        ImGuiPureWrap::tooltip(m_desc.at("first_layer_only_tooltip"), max_tooltip_width);

    if (! m_first_layer_only) {
        if (m_c->object_clipper()->get_position() == 0.f) {
            ImGui::AlignTextToFramePadding();
            ImGuiPureWrap::text(m_desc.at("clipping_of_view"));
        }
        else {
            if (ImGuiPureWrap::button(m_desc.at("reset_direction"))) {
                wxGetApp().CallAfter([this](){
                        m_c->object_clipper()->set_position_by_ratio(-1., false);
                    });
            }
        }

        auto clp_dist = float(m_c->object_clipper()->get_position());
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
        if (m_imgui->slider_float("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f", 1.0f, true, from_u8(GUI::shortkey_ctrl_prefix()) + _L("Mouse wheel")))
            m_c->object_clipper()->set_position_by_ratio(clp_dist, true);
    }

    ImGui::Separator();
    if (ImGuiPureWrap::button(m_desc.at("remove_all"))) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Reset selection"), UndoRedo::SnapshotType::GizmoAction);
        ModelObject         *mo  = m_c->selection_info()->model_object();
        int                  idx = -1;
        for (ModelVolume *mv : mo->volumes)
            if (mv->is_model_part()) {
                ++idx;
                m_triangle_selectors[idx]->reset();
                m_triangle_selectors[idx]->request_update_render_data();
            }

        update_model_object();
        m_parent.set_as_dirty();
    }

    ImGuiPureWrap::end();
}



void GLGizmoBrimPaint::update_model_object() const
{
    bool updated = false;
    ModelObject* mo = m_c->selection_info()->model_object();
    int idx = -1;
    for (ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;
        ++idx;
        updated |= mv->brim_facets.set(*m_triangle_selectors[idx]);
    }

    if (updated) {
        const ModelObjectPtrs& mos = wxGetApp().model().objects;
        wxGetApp().obj_list()->update_info_items(std::find(mos.begin(), mos.end(), mo) - mos.begin());

        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    }
}



void GLGizmoBrimPaint::update_from_model_object()
{
    wxBusyCursor wait;

    const ModelObject* mo = m_c->selection_info()->model_object();
    m_triangle_selectors.clear();

    int volume_id = -1;
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++volume_id;

        // This mesh does not account for the possible Z up SLA offset.
        const TriangleMesh* mesh = &mv->mesh();

        m_triangle_selectors.emplace_back(std::make_unique<TriangleSelectorGUI>(*mesh));
        // Reset of TriangleSelector is done inside TriangleSelectorGUI's constructor, so we don't need it to perform it again in deserialize().
        m_triangle_selectors.back()->deserialize(mv->brim_facets.get_data(), false);
        m_triangle_selectors.back()->request_update_render_data();
    }
}


PainterGizmoType GLGizmoBrimPaint::get_painter_type() const
{
    return PainterGizmoType::BRIM;
}

wxString GLGizmoBrimPaint::handle_snapshot_action_name(bool shift_down, GLGizmoPainterBase::Button button_down) const
{
    wxString action_name;
    if (shift_down)
        action_name = _L("Remove selection");
    else {
        if (button_down == Button::Left)
            action_name = _L("Enforce brim");
        else
            action_name = _L("Block brim");
    }
    return action_name;
}

} // namespace Slic3r::GUI
