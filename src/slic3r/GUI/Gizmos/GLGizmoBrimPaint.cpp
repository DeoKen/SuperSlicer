#include "GLGizmoBrimPaint.hpp"

#include "libslic3r/Model.hpp"

//#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include <GL/glew.h>


namespace Slic3r::GUI {



void GLGizmoBrimPaint::on_shutdown()
{
    // The clipper is shared with the other painting gizmos: don't leave them the first layer plane.
    if (m_first_layer_only)
        m_c->object_clipper()->set_position_by_ratio(0., false);
    m_parent.toggle_model_objects_visibility(true);
}



void GLGizmoBrimPaint::update_first_layer_clipping()
{
    const ModelObject *mo = m_c->selection_info()->model_object();
    if (! m_first_layer_only || mo == nullptr)
        return;
    // The object's own setting, else the print settings. The facets of the first layer are the ones the brim uses
    // (PrintObject::brim_paint_area()), so the plane is a hair above it.
    auto option = [mo](const char *key) {
        const ConfigOption *opt = mo->config.option(key);
        return opt != nullptr ? opt : wxGetApp().plater()->config()->option(key);
    };
    // A percentage is of the layer height.
    const auto         *first_layer_height = dynamic_cast<const ConfigOptionFloatOrPercent*>(option("first_layer_height"));
    const ConfigOption *layer_height       = option("layer_height");
    const double        z = (first_layer_height != nullptr ? first_layer_height->get_abs_value(layer_height != nullptr ? layer_height->getFloat() : 0.2) : 0.2) + 0.001;
    const ClippingPlane plane(Vec3d::UnitZ(), z);
    // Set again if anything else moved it (Ctrl + mouse wheel, the selection changed).
    const ClippingPlane *current = m_c->object_clipper()->get_clipping_plane(true);
    if (current == nullptr || ! (*current == plane) || m_c->object_clipper()->get_position() == 0.)
        m_c->object_clipper()->set_range_and_pos(Vec3d::UnitZ(), z, 1.);
}



bool GLGizmoBrimPaint::on_init()
{
    m_desc["clipping_of_view"] = _u8L("Clipping of view") + ": ";
    m_desc["reset_direction"]  = _u8L("Reset direction");
    m_desc["cursor_size"]      = _u8L("Brush size") + ": ";
    m_desc["cursor_type"]      = _u8L("Brush shape") + ": ";
    m_desc["enforce_caption"]  = _u8L("Left mouse button") + ": ";
    m_desc["enforce"]          = _u8L("Enforce brim");
    m_desc["block_caption"]    = _u8L("Right mouse button") + ": ";
    m_desc["block"]            = _u8L("Block brim");
    m_desc["remove_caption"]   = _u8L("Shift + Left mouse button") + ": ";
    m_desc["remove"]           = _u8L("Remove selection");
    m_desc["remove_all"]       = _u8L("Remove all selection");
    m_desc["circle"]           = _u8L("Circle");
    m_desc["sphere"]           = _u8L("Sphere");
    m_desc["no_brim_width"]    = _u8L("Brim width is 0: the painted brim needs a brim width (Print Settings > Skirt and brim, "
                                      "or a per-object setting).");
    m_desc["raft"]             = _u8L("No brim is made on a raft.");
    m_desc["first_layer_only"] = _u8L("First layer only");
    m_desc["first_layer_only_tooltip"] = _u8L("Shows and paints only the first layer of the part, the only one the brim uses. "
                                              "From above, the Sphere brush paints the bottom face through the cut; "
                                              "the Circle brush paints only facets facing you (look from below).");
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

    const float cursor_type_radio_left   = ImGuiPureWrap::calc_text_size(m_desc["cursor_type"]).x + m_imgui->scaled(1.f);
    const float cursor_type_radio_sphere = ImGuiPureWrap::calc_text_size(m_desc["sphere"]).x + m_imgui->scaled(2.5f);
    const float cursor_type_radio_circle = ImGuiPureWrap::calc_text_size(m_desc["circle"]).x + m_imgui->scaled(2.5f);

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
    window_width = std::max(window_width, cursor_type_radio_left + cursor_type_radio_sphere + cursor_type_radio_circle);

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

    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(m_desc.at("cursor_size"));
    ImGui::SameLine(sliders_left_width);
    ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
    m_imgui->slider_float("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f", 1.0f, true, _L("Alt + Mouse wheel"));

    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(m_desc.at("cursor_type"));

    float cursor_type_offset = cursor_type_radio_left + (window_width - cursor_type_radio_left - cursor_type_radio_sphere - cursor_type_radio_circle + m_imgui->scaled(0.5f)) / 2.f;
    ImGui::SameLine(cursor_type_offset);
    ImGui::PushItemWidth(cursor_type_radio_sphere);
    if (ImGuiPureWrap::radio_button(m_desc["sphere"], m_cursor_type == TriangleSelector::CursorType::SPHERE))
        m_cursor_type = TriangleSelector::CursorType::SPHERE;

    if (ImGui::IsItemHovered())
        ImGuiPureWrap::tooltip(_u8L("Paints all facets inside, regardless of their orientation."), max_tooltip_width);

    ImGui::SameLine(cursor_type_offset + cursor_type_radio_sphere);
    ImGui::PushItemWidth(cursor_type_radio_circle);
    if (ImGuiPureWrap::radio_button(m_desc["circle"], m_cursor_type == TriangleSelector::CursorType::CIRCLE))
        m_cursor_type = TriangleSelector::CursorType::CIRCLE;

    if (ImGui::IsItemHovered())
        ImGuiPureWrap::tooltip(_u8L("Ignores facets facing away from the camera."), max_tooltip_width);

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
