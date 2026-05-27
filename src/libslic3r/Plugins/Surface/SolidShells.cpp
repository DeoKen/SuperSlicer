///|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "SolidShells.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_surface_generation.h"
#include "libslic3r/Api/plugin/cpp/ClipperViews.hpp"
#include "libslic3r/Api/plugin/cpp/DataTreeViews.hpp"
#include "libslic3r/Api/plugin/cpp/RegionSettingsViews.hpp"

namespace slic3r_api { namespace SurfaceGeneration { namespace SolidShellsPlugin {
namespace {

// This module is intentionally a refinement pass, not the initial surface
// builder. It reads the LayerRegionIsland fill surfaces produced earlier in
// STEP_SURFACE_GENERATION, keeps already-special surfaces unchanged, and only
// reclassifies ordinary internal sparse/void areas as internal solid.
const char *k_solid_shells_id = "surface.solid_shells";
const char *k_no_dependencies[] = { nullptr };

const char *k_top_solid_layers_key = "top_solid_layers";
const char *k_top_solid_min_thickness_key = "top_solid_min_thickness";
const char *k_bottom_solid_layers_key = "bottom_solid_layers";
const char *k_bottom_solid_min_thickness_key = "bottom_solid_min_thickness";
const char *k_solid_over_perimeters_key = "solid_over_perimeters";

const raw_used_config_key k_used_config_keys[] = {
    { k_top_solid_layers_key, RAW_CO_INT, RAW_CONTAINER_TYPE_NONE, RAW_PRESET_TYPE_NONE },
    { k_top_solid_min_thickness_key, RAW_CO_FLOAT, RAW_CONTAINER_TYPE_NONE, RAW_PRESET_TYPE_NONE },
    { k_bottom_solid_layers_key, RAW_CO_INT, RAW_CONTAINER_TYPE_NONE, RAW_PRESET_TYPE_NONE },
    { k_bottom_solid_min_thickness_key, RAW_CO_FLOAT, RAW_CONTAINER_TYPE_NONE, RAW_PRESET_TYPE_NONE },
    { k_solid_over_perimeters_key, RAW_CO_INT, RAW_CONTAINER_TYPE_NONE, RAW_PRESET_TYPE_NONE }
};

constexpr raw_surface_type k_internal_solid = RAW_SURFACE_TYPE_POS_INTERNAL | RAW_SURFACE_TYPE_DENS_SOLID;
constexpr raw_surface_type k_internal_sparse = RAW_SURFACE_TYPE_POS_INTERNAL | RAW_SURFACE_TYPE_DENS_SPARSE;

struct SurfacePrerequisites
{
    bool has_surfaces = false;
    bool has_top = false;
    bool has_bottom = false;
};

bool has_flag(raw_surface_type type, raw_surface_type flag)
{
    return (type & flag) != 0;
}

bool processable_internal_surface(raw_surface_type type)
{
    // Bridges are refined by a dedicated bridge module. This plugin only
    // decides whether ordinary internal infill area should be sparse/void or
    // solid. Existing solid areas are left untouched to avoid re-splitting work
    // done by previous surface-generation plugins.
    return has_flag(type, RAW_SURFACE_TYPE_POS_INTERNAL) &&
           !has_flag(type, RAW_SURFACE_TYPE_MOD_BRIDGE) &&
           !has_flag(type, RAW_SURFACE_TYPE_DENS_SOLID);
}

coord_t scaled_bottom_z(const Layer &layer)
{
    // Bottom shells are measured from the lower side of each layer. print_z is
    // the top of the layer in scaled coordinates, so subtract the layer height.
    return layer.print_z() - layer.height();
}

StoredExPolygonCollection collection_from_expolygon(storage_handle *storage, const ExPolygon &expolygon)
{
    StoredExPolygonCollection out(storage);
    out.push_back(expolygon);
    return out;
}

void scan_surface_prerequisites(SurfacePrerequisites &out, const SurfaceCollection &surfaces)
{
    // SolidShells runs after the initial surface classifier. If that classifier
    // did not run, every surface would look like an untyped internal area and
    // this plugin would silently do the wrong thing. The scan is deliberately
    // object-wide: an individual layer may have only top, only bottom, or only
    // internal surfaces, but the object must contain the typed anchors used for
    // shell projection.
    for (const Surface surface : surfaces) {
        if (surface.expolygon().contour().empty())
            continue;

        out.has_surfaces = true;
        const raw_surface_type type = surface.type();
        out.has_top = out.has_top || has_flag(type, RAW_SURFACE_TYPE_POS_TOP);
        out.has_bottom = out.has_bottom || has_flag(type, RAW_SURFACE_TYPE_POS_BOTTOM);
    }
}

SurfacePrerequisites scan_object_surface_prerequisites(const Object &object)
{
    SurfacePrerequisites out;
    for (uint32_t layer_idx = 0; layer_idx < object.layer_count(); ++layer_idx) {
        const Layer layer = object.layer(layer_idx);
        for (uint32_t island_idx = 0; island_idx < layer.island_count(); ++island_idx) {
            const LayerIsland island = layer.island(island_idx);
            for (uint32_t region_island_idx = 0; region_island_idx < island.region_island_count(); ++region_island_idx)
                scan_surface_prerequisites(out, island.region_island(region_island_idx).fill_surfaces_collection());
        }
    }
    return out;
}

bool validate_surface_prerequisites(const plugin_run_context *run_ctx, const Object &object)
{
    const SurfacePrerequisites prerequisites = scan_object_surface_prerequisites(object);
    if (prerequisites.has_surfaces && prerequisites.has_top && prerequisites.has_bottom)
        return true;

    std::string message =
        "Solid shell surfaces requires typed fill surfaces from the initial surface builder before it can run.";
    if (!prerequisites.has_surfaces)
        message += " No LayerRegionIsland fill surfaces were found.";
    else {
        if (!prerequisites.has_top)
            message += " No top surfaces were found.";
        if (!prerequisites.has_bottom)
            message += " No bottom surfaces were found.";
    }
    report_error(run_ctx, message.c_str());
    return false;
}

void append_surface(StoredSurfaceCollection &surfaces,
                    storage_handle *storage,
                    const ExPolygon &area,
                    raw_surface_type type)
{
    StoredExPolygonCollection single(storage);
    single.push_back(area);
    surfaces.append(single.readonly(), type);
}

void append_surface_group(StoredSurfaceCollection &surfaces,
                          const ExPolygonCollection &areas,
                          raw_surface_type type)
{
    if (!areas.empty())
        surfaces.append(areas, type);
}

StoredExPolygonCollection union_collection(storage_handle *storage, const ExPolygonCollection &areas)
{
    if (areas.empty())
        return StoredExPolygonCollection(storage);

    ClipperContext clip(storage);
    return clipper_union(clip(areas)).to_expolygon_collection();
}

StoredExPolygonCollection linked_island_slices(storage_handle *storage, const std::vector<LayerIsland> &linked_islands)
{
    StoredExPolygonCollection slices(storage);
    for (const LayerIsland &linked_island : linked_islands)
        slices.push_back(linked_island.slice());
    return slices;
}

StoredExPolygonCollection exposed_island_area(storage_handle *storage,
                                              const LayerIsland &island,
                                              const bool top_side)
{
    // A top area is the part of this island not covered by islands above it.
    // A bottom area is the same test against islands below it. These exposed
    // areas are projected through neighboring layers to request solid shells.
    const std::vector<LayerIsland> linked_islands = top_side ? island.upper_islands() : island.lower_islands();
    if (linked_islands.empty())
        return collection_from_expolygon(storage, island.slice());

    StoredExPolygonCollection linked_slices = linked_island_slices(storage, linked_islands);
    ClipperContext clip(storage);
    return clipper_diff(clip(island.slice()), clip(linked_slices.readonly())).to_expolygon_collection();
}

StoredExPolygonCollection exposed_layer_areas(storage_handle *storage, const Layer &layer, const bool top_side)
{
    // Each island computes its own exposed area against the island overlap
    // graph. The final union gives a layer-wide projection target, which lets a
    // shell on one island solidify matching areas on a neighboring lower/upper
    // island when geometry overlaps after slicing.
    StoredExPolygonCollection exposed(storage);
    for (uint32_t island_idx = 0; island_idx < layer.island_count(); ++island_idx) {
        StoredExPolygonCollection island_exposed = exposed_island_area(storage, layer.island(island_idx), top_side);
        exposed.append_move_from(std::move(island_exposed));
    }
    return union_collection(storage, exposed.readonly());
}

StoredExPolygonCollection island_perimeter_area(storage_handle *storage, const LayerIsland &island)
{
    // Perimeter-owned area is what remains between the full island slice and
    // the strict free infill area. If perimeters consumed the whole island,
    // infill_no_overlap_areas() is empty and the whole slice becomes perimeter.
    ClipperContext clip(storage);
    return clipper_diff(clip(island.slice()), clip(island.infill_no_overlap_areas())).to_expolygon_collection();
}

StoredExPolygonCollection layer_perimeter_area(storage_handle *storage, const Layer &layer)
{
    StoredExPolygonCollection perimeters(storage);
    for (uint32_t island_idx = 0; island_idx < layer.island_count(); ++island_idx) {
        StoredExPolygonCollection island_perimeters = island_perimeter_area(storage, layer.island(island_idx));
        perimeters.append_move_from(std::move(island_perimeters));
    }
    return union_collection(storage, perimeters.readonly());
}

bool include_top_layer(const Layer &current_layer,
                       const Layer &candidate_layer,
                       const uint32_t distance,
                       const int32_t top_solid_layers,
                       const coord_t min_thickness)
{
    // A candidate layer may be included by layer count or by physical shell
    // thickness. The first upper layer has distance 1, so top_solid_layers=2
    // means "the top layer plus the layer directly below it".
    return (top_solid_layers > 0 && int32_t(distance) < top_solid_layers) ||
           (min_thickness > 0 && candidate_layer.print_z() - current_layer.print_z() < min_thickness);
}

bool include_bottom_layer(const Layer &current_layer,
                          const Layer &candidate_layer,
                          const uint32_t distance,
                          const int32_t bottom_solid_layers,
                          const coord_t min_thickness)
{
    // Bottom thickness is measured between lower layer boundaries. This mirrors
    // the top shell test while keeping asymmetric layer heights correct.
    return (bottom_solid_layers > 0 && int32_t(distance) < bottom_solid_layers) ||
           (min_thickness > 0 && scaled_bottom_z(current_layer) - scaled_bottom_z(candidate_layer) < min_thickness);
}

StoredExPolygonCollection projected_top_shell(storage_handle *storage,
                                              const Object &object,
                                              const uint32_t layer_idx,
                                              const RegionSettingsValue &settings)
{
    // Build the area that would become unsupported from above if this layer
    // stayed sparse. We collect exposed areas from the upper layers requested
    // by top_solid_layers/top_solid_min_thickness and project them onto the
    // current layer; the actual clipping to this layer's surfaces happens later.
    const int32_t top_solid_layers = std::max<int32_t>(0, settings.get_int(k_top_solid_layers_key));
    const coord_t min_thickness = scale_to_layer_coord(std::max(0.0, settings.get_float(k_top_solid_min_thickness_key)));
    if (top_solid_layers == 0 && min_thickness == 0)
        return StoredExPolygonCollection(storage);

    const Layer current_layer = object.layer(layer_idx);
    StoredExPolygonCollection shell(storage);
    for (uint32_t upper_idx = layer_idx + 1; upper_idx < object.layer_count(); ++upper_idx) {
        const uint32_t distance = upper_idx - layer_idx;
        const Layer upper_layer = object.layer(upper_idx);
        if (!include_top_layer(current_layer, upper_layer, distance, top_solid_layers, min_thickness))
            break;

        StoredExPolygonCollection exposed = exposed_layer_areas(storage, upper_layer, true);
        shell.append_move_from(std::move(exposed));
    }
    return union_collection(storage, shell.readonly());
}

StoredExPolygonCollection projected_bottom_shell(storage_handle *storage,
                                                 const Object &object,
                                                 const uint32_t layer_idx,
                                                 const RegionSettingsValue &settings)
{
    // Same idea as projected_top_shell(), but looking downward. Exposed bottom
    // areas from lower layers request solid material above them until the
    // configured bottom shell count or thickness is satisfied.
    const int32_t bottom_solid_layers = std::max<int32_t>(0, settings.get_int(k_bottom_solid_layers_key));
    const coord_t min_thickness =
        scale_to_layer_coord(std::max(0.0, settings.get_float(k_bottom_solid_min_thickness_key)));
    if (bottom_solid_layers == 0 && min_thickness == 0)
        return StoredExPolygonCollection(storage);

    const Layer current_layer = object.layer(layer_idx);
    StoredExPolygonCollection shell(storage);
    for (int32_t lower_idx = int32_t(layer_idx) - 1; lower_idx >= 0; --lower_idx) {
        const uint32_t distance = layer_idx - uint32_t(lower_idx);
        const Layer lower_layer = object.layer(uint32_t(lower_idx));
        if (!include_bottom_layer(current_layer, lower_layer, distance, bottom_solid_layers, min_thickness))
            break;

        StoredExPolygonCollection exposed = exposed_layer_areas(storage, lower_layer, false);
        shell.append_move_from(std::move(exposed));
    }
    return union_collection(storage, shell.readonly());
}

StoredExPolygonCollection perimeter_stack_coverage(storage_handle *storage,
                                                   const Object &object,
                                                   const uint32_t layer_idx,
                                                   const bool top_side,
                                                   const int32_t solid_over_perimeters)
{
    // solid_over_perimeters prevents promoting a shell candidate when the whole
    // candidate is already backed by enough perimeter-owned material in the
    // neighboring layers. We intersect the perimeter areas of each required
    // adjacent layer; if any layer is missing or has no perimeter coverage, the
    // exemption cannot apply.
    if (solid_over_perimeters <= 0)
        return StoredExPolygonCollection(storage);

    StoredExPolygonCollection coverage(storage);
    bool has_coverage = false;
    for (int32_t step = 1; step <= solid_over_perimeters; ++step) {
        const int32_t adjacent_idx = top_side ? int32_t(layer_idx) + step : int32_t(layer_idx) - step;
        if (adjacent_idx < 0 || adjacent_idx >= int32_t(object.layer_count()))
            return StoredExPolygonCollection(storage);

        StoredExPolygonCollection layer_perimeters = layer_perimeter_area(storage, object.layer(uint32_t(adjacent_idx)));
        if (layer_perimeters.empty())
            return StoredExPolygonCollection(storage);

        if (!has_coverage) {
            coverage = std::move(layer_perimeters);
            has_coverage = true;
            continue;
        }

        ClipperContext clip(storage);
        coverage = clipper_intersection(clip(coverage.readonly()), clip(layer_perimeters.readonly())).to_expolygon_collection();
        if (coverage.empty())
            return coverage;
    }
    return coverage;
}

bool fully_covered_by(storage_handle *storage, const ExPolygon &area, const ExPolygonCollection &coverage)
{
    // The exemption is all-or-nothing. Partial perimeter coverage is not enough
    // because the uncovered part still needs solid infill to carry the shell.
    if (coverage.empty())
        return false;

    StoredExPolygonCollection single = collection_from_expolygon(storage, area);
    ClipperContext clip(storage);
    StoredExPolygonCollection uncovered =
        clipper_diff(clip(single.readonly()), clip(coverage)).to_expolygon_collection();
    return uncovered.empty();
}

StoredExPolygonCollection remove_fully_perimeter_covered_candidates(storage_handle *storage,
                                                                    const ExPolygonCollection &candidates,
                                                                    const ExPolygonCollection &perimeter_coverage)
{
    // Keep only candidates that still need solid infill. Fully perimeter-backed
    // candidates stay sparse so solid_over_perimeters behaves like the legacy
    // "do not waste solid infill under enough perimeters" rule.
    if (candidates.empty())
        return StoredExPolygonCollection(storage);
    if (perimeter_coverage.empty())
        return candidates.clone(storage);

    StoredExPolygonCollection out(storage);
    for (const ExPolygon candidate : candidates) {
        if (!fully_covered_by(storage, candidate, perimeter_coverage))
            out.push_back(candidate);
    }
    return out;
}

StoredExPolygonCollection solid_candidates_for_direction(storage_handle *storage,
                                                         const ExPolygonCollection &source,
                                                         const ExPolygonCollection &shell_zone,
                                                         const ExPolygonCollection &perimeter_coverage)
{
    // A top/bottom shell request only affects the part of the current sparse
    // source area that overlaps the projected exposed zone. The perimeter stack
    // filter is applied after clipping so the "fully covered" test is local to
    // each candidate polygon.
    if (source.empty() || shell_zone.empty())
        return StoredExPolygonCollection(storage);

    ClipperContext clip(storage);
    StoredExPolygonCollection candidates =
        clipper_intersection(clip(source), clip(shell_zone)).to_expolygon_collection();
    return remove_fully_perimeter_covered_candidates(storage, candidates.readonly(), perimeter_coverage);
}

StoredExPolygonCollection collect_processable_surfaces(storage_handle *storage,
                                                       const SurfaceCollection &surfaces,
                                                       const RegionSettingsClip &settings_clip)
{
    // RegionSettings may split an island into multiple clips, each with a
    // different combination of shell settings. Source surfaces are therefore
    // clipped to the active settings area before any top/bottom projection is
    // evaluated.
    StoredExPolygonCollection source(storage);
    for (const Surface surface : surfaces) {
        if (!processable_internal_surface(surface.type()))
            continue;

        StoredExPolygonCollection single = collection_from_expolygon(storage, surface.expolygon());
        if (settings_clip.is_accept_all())
            source.append_copy_from(single.readonly());
        else {
            StoredExPolygonCollection clipped = settings_clip.intersections(single.readonly());
            source.append_move_from(std::move(clipped));
        }
    }
    return source;
}

StoredExPolygonCollection subtract_areas(storage_handle *storage,
                                         const ExPolygonCollection &subject,
                                         const ExPolygonCollection &clip_areas)
{
    if (subject.empty())
        return StoredExPolygonCollection(storage);
    if (clip_areas.empty())
        return subject.clone(storage);

    ClipperContext clip(storage);
    return clipper_diff(clip(subject), clip(clip_areas)).to_expolygon_collection();
}

void append_unchanged_surfaces(StoredSurfaceCollection &out,
                               storage_handle *storage,
                               const SurfaceCollection &surfaces)
{
    // Preserve bridge, already-solid, and non-internal surfaces exactly as they
    // arrived. This keeps the plugin composable with other surface-generation
    // refinements that may run before or after it.
    for (const Surface surface : surfaces) {
        if (!processable_internal_surface(surface.type()))
            append_surface(out, storage, surface.expolygon(), surface.type());
    }
}

void append_solid_and_sparse_results(StoredSurfaceCollection &out,
                                     storage_handle *storage,
                                     StoredExPolygonCollection &&source,
                                     StoredExPolygonCollection &&top_solid,
                                     StoredExPolygonCollection &&bottom_solid)
{
    // Top and bottom requests produce the same final surface type, so they are
    // unioned before rebuilding the residual sparse area. This guarantees that
    // the output surfaces do not positively overlap.
    StoredExPolygonCollection solid(storage);
    solid.append_move_from(std::move(top_solid));
    solid.append_move_from(std::move(bottom_solid));
    solid = union_collection(storage, solid.readonly());

    StoredExPolygonCollection sparse = subtract_areas(storage, source.readonly(), solid.readonly());
    append_surface_group(out, solid.readonly(), k_internal_solid);
    append_surface_group(out, sparse.readonly(), k_internal_sparse);
}

void rebuild_region_island_surfaces(const run_ctx_surface_generation &ctx,
                                    storage_handle *storage,
                                    const Object &object,
                                    const LayerIsland &island,
                                    const uint32_t layer_idx,
                                    const LayerRegionIsland &region_island)
{
    assert(storage != nullptr);

    // The step payload gives read-only access to the existing surfaces and a
    // callback for replacing the whole collection. That keeps ownership in the
    // host while still letting plugin code use temporary storage-owned geometry.
    const SurfaceCollection input_surfaces = region_island.fill_surfaces_collection();
    if (input_surfaces.empty())
        return;

    // All five settings are grouped together so every clipped area carries a
    // complete, self-consistent shell policy. Sparse and void input surfaces are
    // treated the same here; density-specific cleanup is left to later plugins.
    RegionSettings settings(storage, island,
        {{ k_top_solid_layers_key,
           k_top_solid_min_thickness_key,
           k_bottom_solid_layers_key,
           k_bottom_solid_min_thickness_key,
           k_solid_over_perimeters_key }});
    settings.segregate(island.slice());

    StoredSurfaceCollection output(storage);
    append_unchanged_surfaces(output, storage, input_surfaces);

    const RegionSettings::AreaMap &areas = settings.get_areas(k_top_solid_layers_key);
    for (const std::pair<const RegionSettingsValue, RegionSettingsClip> &entry : areas) {
        // First collect only the source surfaces covered by this exact settings
        // combination. The same layer island can therefore have different solid
        // shell behavior in different regions without pre-splitting the island.
        StoredExPolygonCollection source = collect_processable_surfaces(storage, input_surfaces, entry.second);
        if (source.empty())
            continue;

        const int32_t solid_over_perimeters =
            std::max<int32_t>(0, entry.first.get_int(k_solid_over_perimeters_key));

        StoredExPolygonCollection top_shell = projected_top_shell(storage, object, layer_idx, entry.first);
        StoredExPolygonCollection top_perimeter_coverage =
            perimeter_stack_coverage(storage, object, layer_idx, true, solid_over_perimeters);
        StoredExPolygonCollection top_solid =
            solid_candidates_for_direction(storage, source.readonly(), top_shell.readonly(), top_perimeter_coverage.readonly());

        // Bottom shell detection works on the part not already made solid by
        // the top pass. Both outputs use the same final Surface type, but this
        // avoids duplicated solid areas when top and bottom ranges overlap.
        StoredExPolygonCollection source_without_top =
            subtract_areas(storage, source.readonly(), top_solid.readonly());
        StoredExPolygonCollection bottom_shell = projected_bottom_shell(storage, object, layer_idx, entry.first);
        StoredExPolygonCollection bottom_perimeter_coverage =
            perimeter_stack_coverage(storage, object, layer_idx, false, solid_over_perimeters);
        StoredExPolygonCollection bottom_solid =
            solid_candidates_for_direction(storage,
                                           source_without_top.readonly(),
                                           bottom_shell.readonly(),
                                           bottom_perimeter_coverage.readonly());

        append_solid_and_sparse_results(output,
                                        storage,
                                        std::move(source),
                                        std::move(top_solid),
                                        std::move(bottom_solid));
    }

    if (ctx.set_region_island_fill_surfaces != nullptr)
        ctx.set_region_island_fill_surfaces(
            const_cast<layer_region_island_handle *>(region_island.handle()),
            output.mutable_handle());
}

void process_layer(const run_ctx_surface_generation &ctx,
                   storage_handle *storage,
                   const Object &object,
                   const uint32_t layer_idx)
{
    // Each LayerRegionIsland owns a fill-surface collection for one island and
    // one compatible region set. Rebuilding them independently avoids creating
    // surfaces that mix incompatible regional settings.
    const Layer layer = object.layer(layer_idx);
    for (uint32_t island_idx = 0; island_idx < layer.island_count(); ++island_idx) {
        const LayerIsland island = layer.island(island_idx);
        for (uint32_t region_island_idx = 0; region_island_idx < island.region_island_count(); ++region_island_idx)
            rebuild_region_island_surfaces(ctx,
                                           storage,
                                           object,
                                           island,
                                           layer_idx,
                                           island.region_island(region_island_idx));
    }
}

} // namespace

SolidShells &SolidShells::instance(orchestrator_handle *orch)
{
    static SolidShells s_instance(orch);
    return s_instance;
}

const char *SolidShells::id_impl() const noexcept
{
    return k_solid_shells_id;
}

const char *SolidShells::name_impl() const noexcept
{
    return "Solid shell surfaces";
}

const char *SolidShells::description_impl() const noexcept
{
    return "Turns internal surfaces into solid infill where top or bottom shell thickness requires it.";
}

slicing_step_t SolidShells::step_impl() const noexcept
{
    return STEP_SURFACE_GENERATION;
}

const char *const *SolidShells::dependencies_impl() const noexcept
{
    return k_no_dependencies;
}

int32_t SolidShells::priority_impl() const noexcept
{
    return 10;
}

int32_t SolidShells::used_config_keys(raw_used_config_key *keys) const noexcept
{
    if (keys != nullptr)
        for (uint32_t idx = 0; idx < sizeof(k_used_config_keys) / sizeof(k_used_config_keys[0]); ++idx)
            keys[idx] = k_used_config_keys[idx];
    return int32_t(sizeof(k_used_config_keys) / sizeof(k_used_config_keys[0]));
}

const char *SolidShells::progress_message_format_impl() const noexcept
{
    return "Build solid shell surfaces: %u / %u layers";
}

void SolidShells::setup_run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_surface_generation *ctx = plugin_ctx_as_surface_generation(run_ctx);
    if (ctx != nullptr && ctx->object != nullptr) {
        const Object object(ctx->object);
        progress().add_max(object.layer_count());
    }
}

void SolidShells::run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_surface_generation *ctx = plugin_ctx_as_surface_generation(run_ctx);
    if (ctx == nullptr || ctx->object == nullptr || run_ctx == nullptr || run_ctx->plugin_storage == nullptr)
        return;

    const Object object(ctx->object);
    if (!validate_surface_prerequisites(run_ctx, object))
        return;

    for (uint32_t layer_idx = 0; layer_idx < object.layer_count(); ++layer_idx) {
        throw_if_cancelled(run_ctx);
        process_layer(*ctx, run_ctx->plugin_storage, object, layer_idx);
        progress().increment();
    }
}

void register_solid_shells_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, SolidShells::instance(orch).c_instance());
}

}}} // namespace slic3r_api::SurfaceGeneration::SolidShellsPlugin

#ifdef SOLID_SHELLS_PLUGIN_DLL
extern "C" void register_plugin(orchestrator_handle *orch)
{
    slic3r_api::SurfaceGeneration::SolidShellsPlugin::register_solid_shells_plugin(orch);
}
#endif // SOLID_SHELLS_PLUGIN_DLL
