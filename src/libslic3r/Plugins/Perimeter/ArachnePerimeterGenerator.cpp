///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "ArachnePerimeterGenerator.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

#include "libslic3r/Arachne/WallToolPaths.hpp"
#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_perimeter.h"
#include "libslic3r/Api/plugin/cpp/ExtrusionViews.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry/MedialAxis.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintRegion.hpp"
#include "libslic3r/SurfaceCollection.hpp"

namespace slic3r_api { namespace Perimeter { namespace ArachnePerimeterGeneratorPlugin {

namespace {

const char *k_arachne_perimeter_generator_id = "perimeter.generator.arachne";
const char *k_no_dependencies[] = { nullptr };

const Slic3r::LayerSliceIsland *to_layer_island(const layer_island_handle *handle)
{
    return reinterpret_cast<const Slic3r::LayerSliceIsland *>(handle);
}

const Slic3r::Layer *to_layer(const layer_handle *handle)
{
    return reinterpret_cast<const Slic3r::Layer *>(handle);
}

const Slic3r::Print *to_print(const print_handle *handle)
{
    return reinterpret_cast<const Slic3r::Print *>(handle);
}

layer_region_island_handle *get_or_create_single_region_island(const run_ctx_generate_perimeter &ctx,
                                                               const Slic3r::LayerSliceIsland &island,
                                                               std::vector<const layer_region_handle *> &region_handles)
{
    if (ctx.get_or_create_region_island == nullptr)
        return nullptr;

    region_handles.clear();
    region_handles.reserve(island.regions().size());
    for (const Slic3r::LayerRegion *region : island.regions())
        region_handles.push_back(reinterpret_cast<const layer_region_handle *>(region));

    return ctx.get_or_create_region_island(ctx.island,
                                           region_handles.empty() ? nullptr : region_handles.data(),
                                           uint32_t(region_handles.size()));
}

const Slic3r::LayerRegion *first_region(const Slic3r::LayerSliceIsland &island)
{
    return island.regions().empty() ? nullptr : *island.regions().begin();
}

Slic3r::Flow perimeter_flow(const Slic3r::LayerRegion &region, const bool external)
{
    return region.flow(external ? Slic3r::frExternalPerimeter : Slic3r::frPerimeter);
}

size_t max_inset_idx(const std::vector<Slic3r::Arachne::VariableWidthLines> &perimeters)
{
    size_t out = 0;
    for (const Slic3r::Arachne::VariableWidthLines &perimeter : perimeters)
        for (const Slic3r::Arachne::ExtrusionLine &line : perimeter)
            out = std::max(out, line.inset_idx);
    return out;
}

Slic3r::ExtrusionLoopRole loop_role_for_line(const Slic3r::Arachne::ExtrusionLine &line,
                                             const size_t biggest_inset_idx)
{
    Slic3r::ExtrusionLoopRole loop_role = Slic3r::elrDefault;
    if (line.inset_idx == biggest_inset_idx)
        loop_role = Slic3r::ExtrusionLoopRole(loop_role | Slic3r::elrInternal | Slic3r::elrFirstLoop);
    if (!line.is_contour())
        loop_role = Slic3r::ExtrusionLoopRole(loop_role | Slic3r::elrHole);
    return loop_role;
}

bool line_is_closed(const Slic3r::Arachne::ExtrusionLine &line)
{
    return line.is_closed ||
           (!line.junctions.empty() && line.junctions.front().p == line.junctions.back().p);
}

Slic3r::ExtrusionPaths variable_width_paths(const Slic3r::Arachne::ExtrusionLine &line,
                                            const Slic3r::ExtrusionRole role,
                                            const Slic3r::Flow &flow,
                                            const Slic3r::PrintConfig &print_config)
{
    if (line.size() < 2 || line.is_zero_length())
        return {};

    Slic3r::ThickPolyline thick_polyline = Slic3r::Arachne::to_thick_polyline(line);
    if (thick_polyline.points.size() < 2)
        return {};

    const Slic3r::coord_t resolution = std::max(flow.scaled_width() / 4, scale_i(print_config.resolution.value));
    const Slic3r::coord_t tolerance  = std::max<Slic3r::coord_t>(1, flow.scaled_width() / 10);
    return Slic3r::Geometry::unsafe_variable_width(thick_polyline, role, flow, resolution, tolerance);
}

void annotate_paths(Slic3r::ExtrusionPaths &paths,
                    const size_t inset_idx,
                    const Slic3r::ExtrusionLoopRole loop_role)
{
    const int16_t shell_idx = int16_t(std::min<size_t>(inset_idx, size_t(std::numeric_limits<int16_t>::max())));
    for (Slic3r::ExtrusionPath &path : paths) {
        path.get_or_add_property<EPropertyPerimeter>()
            .shell_count(shell_idx)
            .perimeter_role(int32_t(loop_role));
    }
}

void append_open_paths(Slic3r::ExtrusionEntityCollection &dst, Slic3r::ExtrusionPaths &&paths)
{
    if (paths.empty())
        return;

    Slic3r::ExtrusionMultiPath multi_path;
    multi_path.set_can_reverse(true);
    for (size_t idx = 0; idx < paths.size(); ++idx) {
        if (idx > 0)
            paths[idx].set_can_reverse(false);
        multi_path.paths().push_back(std::move(paths[idx]));
    }
    dst.append(std::move(multi_path));
}

void append_arachne_line(Slic3r::ExtrusionEntityCollection &dst,
                         const Slic3r::Arachne::ExtrusionLine &line,
                         const size_t biggest_inset_idx,
                         const Slic3r::Layer &layer,
                         const Slic3r::PrintRegionConfig &region_config,
                         const Slic3r::PrintConfig &print_config,
                         const Slic3r::Flow &external_flow,
                         const Slic3r::Flow &internal_flow)
{
    if (line.size() < 2 || line.is_zero_length())
        return;

    const bool is_external = line.inset_idx == 0;
    const bool is_contour = line.is_contour();
    const bool is_closed = line_is_closed(line);
    const Slic3r::ExtrusionRole role = is_external ?
        Slic3r::ExtrusionRole::ExternalPerimeter :
        Slic3r::ExtrusionRole::Perimeter;
    const Slic3r::Flow &flow = is_external ? external_flow : internal_flow;
    Slic3r::ExtrusionLoopRole loop_role = loop_role_for_line(line, biggest_inset_idx);
    Slic3r::ExtrusionPaths paths = variable_width_paths(line, role, flow, print_config);
    if (paths.empty())
        return;

    annotate_paths(paths, line.inset_idx, loop_role);

    if (is_closed && paths.back().last_point().coincides_with_epsilon(paths.front().first_point())) {
        Slic3r::ExtrusionLoop loop(std::move(paths), loop_role);
        const bool ccw_contour = region_config.perimeter_direction.value == Slic3r::PerimeterDirection::pdCCW_CW ||
                                 region_config.perimeter_direction.value == Slic3r::PerimeterDirection::pdCCW_CCW;
        const bool ccw_hole = region_config.perimeter_direction.value == Slic3r::PerimeterDirection::pdCW_CCW ||
                              region_config.perimeter_direction.value == Slic3r::PerimeterDirection::pdCCW_CCW;
        const bool need_ccw = ((region_config.perimeter_reverse.value && layer.id() % 2 == 1) ==
                               (is_contour ? ccw_contour : ccw_hole));
        if (need_ccw != loop.is_clockwise())
            loop.reverse();

        // Arachne may close a loop with points that are epsilon-close but not
        // bit-identical. Normalize the stored endpoint before later code
        // relies on exact loop closure.
        if (!loop.paths().empty())
            loop.paths().front().polyline().set_front(loop.paths().back().last_point());
        dst.append(std::move(loop));
        return;
    }

    append_open_paths(dst, std::move(paths));
}

Slic3r::ExtrusionEntityCollection make_arachne_extrusions(
    const std::vector<Slic3r::Arachne::VariableWidthLines> &perimeters,
    const Slic3r::Layer &layer,
    const Slic3r::PrintRegionConfig &region_config,
    const Slic3r::PrintConfig &print_config,
    const Slic3r::Flow &external_flow,
    const Slic3r::Flow &internal_flow)
{
    Slic3r::ExtrusionEntityCollection extrusion;
    const size_t biggest_inset_idx = max_inset_idx(perimeters);
    for (const Slic3r::Arachne::VariableWidthLines &perimeter : perimeters)
        for (const Slic3r::Arachne::ExtrusionLine &line : perimeter)
            append_arachne_line(extrusion, line, biggest_inset_idx, layer, region_config, print_config,
                                external_flow, internal_flow);
    return extrusion;
}

void publish_extrusion(const run_ctx_generate_perimeter &ctx,
                       layer_region_island_handle *region_island,
                       Slic3r::ExtrusionEntityCollection &extrusion)
{
    if (ctx.set_region_island_extrusion == nullptr)
        return;

    ctx.set_region_island_extrusion(region_island,
                                    RAW_EXTRUSION_ROLE_EXTERNAL_PERIMETER,
                                    reinterpret_cast<extrusion_entity_handle *>(&extrusion));
}

void publish_fill_surfaces(perimeter_set_region_island_surfaces_fn setter,
                           layer_region_island_handle *region_island,
                           Slic3r::ExPolygons surfaces_area)
{
    if (setter == nullptr)
        return;

    Slic3r::SurfaceCollection surfaces;
    surfaces.append(std::move(surfaces_area), Slic3r::stPosInternal | Slic3r::stDensSparse);
    setter(region_island, reinterpret_cast<surface_collection_handle *>(&surfaces));
}

} // namespace

ArachnePerimeterGenerator &
ArachnePerimeterGenerator::instance(orchestrator_handle *orch)
{
    static ArachnePerimeterGenerator s_instance(orch);
    return s_instance;
}

const char *ArachnePerimeterGenerator::id_impl() const noexcept
{
    return k_arachne_perimeter_generator_id;
}

slicing_step_t ArachnePerimeterGenerator::step_impl() const noexcept
{
    return STEP_PERIMETER;
}

const char *const *ArachnePerimeterGenerator::dependencies_impl() const noexcept
{
    return k_no_dependencies;
}

int32_t ArachnePerimeterGenerator::priority_impl() const noexcept
{
    return 10;
}

const char *ArachnePerimeterGenerator::progress_message_format_impl() const noexcept
{
    return "Arachne perimeter generator: %u / %u islands";
}

void ArachnePerimeterGenerator::setup_run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_generate_perimeter *ctx = plugin_ctx_as_generate_perimeter(run_ctx);
    if (ctx != nullptr && ctx->island != nullptr)
        progress().add_max(1);
}

void ArachnePerimeterGenerator::run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_generate_perimeter *ctx = plugin_ctx_as_generate_perimeter(run_ctx);
    if (ctx == nullptr || ctx->island == nullptr || ctx->layer == nullptr || ctx->print == nullptr)
        return;

    throw_if_cancelled(run_ctx);

    const Slic3r::LayerSliceIsland *island = to_layer_island(ctx->island);
    const Slic3r::Layer *layer = to_layer(ctx->layer);
    const Slic3r::Print *print = to_print(ctx->print);
    if (island == nullptr || layer == nullptr || print == nullptr)
        return;

    const Slic3r::LayerRegion *region = first_region(*island);
    if (region == nullptr)
        return;

    std::vector<const layer_region_handle *> region_handles;
    layer_region_island_handle *region_island = get_or_create_single_region_island(*ctx, *island, region_handles);
    if (region_island == nullptr)
        return;

    const Slic3r::PrintRegionConfig &region_config = region->region().config();
    const Slic3r::PrintConfig &print_config = print->config();
    const Slic3r::Flow external_flow = perimeter_flow(*region, true);
    const Slic3r::Flow internal_flow = perimeter_flow(*region, false);
    const size_t perimeter_count = region_config.perimeters.value <= 0 ?
        size_t(0) :
        size_t(region_config.perimeters.value);

    Slic3r::ExtrusionEntityCollection extrusion;
    Slic3r::ExPolygons fill_no_overlap = Slic3r::ExPolygons{ island->get_slice() };

    if (perimeter_count > 0) {
        Slic3r::Polygons outlines = Slic3r::to_polygons(island->get_slice());
        Slic3r::Arachne::WallToolPaths wall_tool_paths(outlines,
                                                       external_flow.scaled_spacing(),
                                                       external_flow.scaled_width(),
                                                       internal_flow.scaled_spacing(),
                                                       internal_flow.scaled_width(),
                                                       perimeter_count,
                                                       Slic3r::coord_t(0),
                                                       layer->unscaled_height(),
                                                       region_config,
                                                       print_config);
        const std::vector<Slic3r::Arachne::VariableWidthLines> &perimeters =
            wall_tool_paths.getToolPaths();
        extrusion = make_arachne_extrusions(perimeters, *layer, region_config, print_config,
                                            external_flow, internal_flow);
        fill_no_overlap = Slic3r::union_ex(wall_tool_paths.getInnerContour());
        if (fill_no_overlap.empty())
            fill_no_overlap = Slic3r::ExPolygons{ island->get_slice() };
    }

    Slic3r::ExPolygons fill_surfaces = Slic3r::ensure_valid(
        Slic3r::offset_ex(fill_no_overlap, 0.25 * double(internal_flow.scaled_spacing())));
    fill_no_overlap = Slic3r::ensure_valid(std::move(fill_no_overlap));

    publish_extrusion(*ctx, region_island, extrusion);
    publish_fill_surfaces(ctx->set_region_island_fill_surfaces, region_island, std::move(fill_surfaces));
    publish_fill_surfaces(ctx->set_region_island_fill_no_overlap_surfaces,
                          region_island,
                          std::move(fill_no_overlap));

    progress().increment();
}

void register_arachne_perimeter_generator_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, ArachnePerimeterGenerator::instance(orch).c_instance());
}

}}} // namespace slic3r_api::Perimeter::ArachnePerimeterGeneratorPlugin

#ifdef ARACHNE_PERIMETER_GENERATOR_PLUGIN_DLL
extern "C" void register_plugin(orchestrator_handle *orch)
{
    slic3r_api::Perimeter::ArachnePerimeterGeneratorPlugin::register_arachne_perimeter_generator_plugin(orch);
}
#endif // ARACHNE_PERIMETER_GENERATOR_PLUGIN_DLL
