///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "PerimeterGenerator2.hpp"

#include <vector>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/DataTreeFwd.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"

namespace Slic3r::PerimeterGenerator2 {
namespace {

struct PerimeterGenerationInput
{
    ExPolygon expolygon;
    coord_t spacing = 0;
    coord_t width = 0;
    bool external = false;
    bool hole = false;
};

struct PerimeterGenerationOutput
{
    ExPolygon inner;
    ExtrusionEntityCollection extrusions;
};

struct PerimeterPlan
{
    int contour_count = 0;
    int hole_count = 0;
};

struct SurfaceResult
{
    ExPolygons inner_perimeters;
    ExPolygons gap_surfaces;
    ExPolygons fill_surfaces;
    ExPolygons fill_no_overlap;
    ExtrusionEntityCollection perimeters;
    ExtrusionEntityCollection gap_fill;
};

std::vector<LayerRegionSetCPtrs> collect_region_groups(const LayerSliceIsland &island, const Layer &layer)
{
    // TODO: Move the compatibility grouping from Layer::make_perimeters().
    // This is the first split point between "which regions can share perimeter
    // generation" and the actual perimeter algorithm.
    (void) layer;

    LayerRegionSetCPtrs group;
    for (const LayerRegion *region : island.regions())
        group.insert(region);

    std::vector<LayerRegionSetCPtrs> groups;
    if (!group.empty())
        groups.push_back(std::move(group));
    return groups;
}

LayerRegionIsland &prepare_region_island(LayerSliceIsland &island, const LayerRegionSetCPtrs &regions)
{
    // TODO: Keep the current extruder selection rules from Layer::make_perimeters().
    // For now the new orchestrator only asks the LayerSliceIsland for the output
    // node matching this region group.
    return island.get_or_add_region_island(regions);
}

ExPolygons build_surface_inputs(const LayerSliceIsland &island, const LayerRegionIsland &region_island)
{
    // TODO: Reuse the old "whole island if all regions match, otherwise
    // intersect region raw slices with the island slice" logic. This isolates
    // the region split from the actual perimeter generation loop.
    if (region_island.regions() == island.regions())
        return ExPolygons{island.get_slice()};

    ExPolygons region_area;
    for (const LayerRegion *region : region_island.regions())
        append(region_area, region->get_raw_slices());

    region_area = union_safety_offset_ex(region_area);
    return intersection_ex(region_area, ExPolygons{island.get_slice()});
}

PerimeterPlan build_initial_plan(const Print &print,
                                 const PrintObject &object,
                                 const Layer &layer,
                                 const LayerSliceIsland &island,
                                 const LayerRegionIsland &region_island,
                                 const ExPolygon &surface)
{
    // TODO: Recreate the contour/hole count setup from process_classic().
    // This is where the base perimeter count, hole perimeter count, spiral
    // vase, first-layer special case and simple whole-region overrides belong.
    (void) print;
    (void) object;
    (void) layer;
    (void) island;
    (void) region_island;
    (void) surface;
    return {};
}

void apply_perimeter_count_settings(PerimeterPlan &plan,
                                    const Print &print,
                                    const PrintObject &object,
                                    const Layer &layer,
                                    const LayerSliceIsland &island,
                                    const LayerRegionIsland &region_island,
                                    const ExPolygon &surface)
{
    // TODO: Pull each setting rule into its own function:
    // - only_one_perimeter_top / only_one_perimeter_first_layer
    // - extra_perimeters_count
    // - extra_perimeters_odd_layers
    // - surface-provided extra perimeter counts
    // Each rule should edit PerimeterPlan, not touch the generated extrusions.
    (void) plan;
    (void) print;
    (void) object;
    (void) layer;
    (void) island;
    (void) region_island;
    (void) surface;
}

void apply_geometry_masks(PerimeterPlan &plan,
                          const Print &print,
                          const PrintObject &object,
                          const Layer &layer,
                          const LayerSliceIsland &island,
                          const LayerRegionIsland &region_island,
                          const ExPolygon &surface)
{
    // TODO: Create explicit masks/constraints for features that alter the
    // perimeter geometry:
    // - no perimeters on bridge / unsupported areas
    // - extra perimeters on overhangs
    // - bridgeable versus unbridgeable unsupported zones
    // These masks are consumed by the single-perimeter generation loop below.
    (void) plan;
    (void) print;
    (void) object;
    (void) layer;
    (void) island;
    (void) region_island;
    (void) surface;
}

bool should_generate_next_perimeter(const PerimeterPlan &plan, size_t perimeter_index)
{
    // TODO: Use the remaining contour/hole counts and any area-specific rules.
    // This function is deliberately separated so the loop condition is not
    // scattered through the future Classic/Arachne-compatible implementation.
    (void) plan;
    (void) perimeter_index;
    return false;
}

PerimeterGenerationInput make_generation_input(const PerimeterPlan &plan,
                                               const ExPolygon &current,
                                               size_t perimeter_index)
{
    // TODO: Convert the current plan state into the small black-box input:
    // expolygon + spacing + width + contour/hole/external metadata.
    // The black box must not read print config directly.
    (void) plan;
    (void) perimeter_index;
    PerimeterGenerationInput input;
    input.expolygon = current;
    return input;
}

PerimeterGenerationOutput perimeter_generation(const PerimeterGenerationInput &input)
{
    // TODO: Implement one-ring perimeter generation.
    // Classic can generate a fixed-width ring. Arachne can generate variable
    // width extrusion and temporarily encode the width profile in ArcPolyline
    // per-point extra data currently stored through z-offset.
    (void) input;
    return {};
}

void absorb_generation_output(SurfaceResult &result,
                              const PerimeterGenerationOutput &generated,
                              ExPolygon &current)
{
    // TODO: Move generated extrusions into result.perimeters and update the
    // current inner expolygon. This is the only place where the loop advances
    // from one perimeter ring to the next.
    (void) result;
    (void) generated;
    (void) current;
}

//void generate_gap_fill(SurfaceResult &result,
//                       const PerimeterPlan &plan,
//                       const ExPolygon &last_inner)
//{
//    // TODO: Move the old medial-axis gap-fill setup here. This should consume
//    // the final inner surface and the plan, then write result.gap_fill and
//    // result.gap_surfaces.
//    (void) result;
//    (void) plan;
//    (void) last_inner;
//}

void build_fill_surfaces(SurfaceResult &result,
                         const PerimeterPlan &plan,
                         const ExPolygon &last_inner)
{
    // TODO: Recreate inner_perimeter, fill_surfaces and fill_no_overlap.
    // This is intentionally after perimeter and gap-fill generation because
    // these polygons must stay coherent with what the generator actually made.
    (void) result;
    (void) plan;
    (void) last_inner;
}

void publish_surface_result(LayerSliceIsland &island,
                            LayerRegionIsland &region_island,
                            SurfaceResult &result)
{
    // TODO: Move result.perimeters/result.gap_fill to region_island, append
    // result.fill_surfaces/result.fill_no_overlap to the island-level caches,
    // and update the perimeter boundary used by avoid-crossing-perimeters.
    (void) island;
    (void) region_island;
    (void) result;
}

void ExtraPerimeter::init_perimeter_generation(
                          const Print &print,
                          const PrintObject &object,
                          const Layer &layer,
                          const LayerSliceIsland &island,
                          const LayerRegionIsland &region_island,
                          ExPolygon &surface,
                          std::vector<ExtrusionEntity> &extrusions) {
    while (this->need_extra_perimeter()) {
        plan.contour_count += 1;
        plan.hole_count += 1;
        input = make_generation_input(plan, surface, perimeter_index);
        PerimeterGenerationOutput generated = perimeter_generation(input);
        append(extrusions, generated.extrusions);
        surface = generated.inner
    }
}

void OnlyOnePerimeterOnTop::init_perimeter_generation(PerimeterPlan &plan,
                          const Print &print,
                          const PrintObject &object,
                          const Layer &layer,
                          const LayerSliceIsland &island,
                          const LayerRegionIsland &region_island,
                          ExPolygon &surface,
                          std::vector<ExtrusionEntity> &extrusions) {
    ExPolygons results;
    if(!should_generate_next_perimeter(plan, perimeter_index) ){
        return;
    }
    std::vector<TopAreas> top_surfaces = get_top_surfaces(surface);
    if(top_surfaces.empty()){
        return;
    }
    input = make_generation_input(plan, surface, perimeter_index);
    PerimeterGenerationOutput generated = perimeter_generation(input);
    for (TopArea top_surface = top_surfaces) {
        append(extrusions, generated.extrusions);
        if (!generated.inner.empty()) {
            append(results, intersection_ex(top_surface.get_top_area(),
                                                offset_ex(generated.inner, get_ext_perimeter_spacing() / 2));
        }
    }
}

void SeparateHoleContour::apply_geometry_masks(PerimeterPlan &plan,
                          const Print &print,
                          const PrintObject &object,
                          const Layer &layer,
                          const LayerSliceIsland &island,
                          const LayerRegionIsland &region_island,
                          ExPolygon &surface,
                          std::vector<ExtrusionEntity> &extrusions)
{

}

void SeparateHoleContour::absorb_generation_output(SurfaceResult &result,
                     PerimeterGenerationOutput &output,
                          ExPolygon &surface)
{

    if (hole_count != perimeter_count && (hole_count < perimeter_index || contour_count < perimeter_index)) {
        ExPolygons mask_area;
        if(hole_count < perimeter_index) {
            mask_area = grow_contour_only(surface);
        } else {
            mask_area = grow_hole_only(surface);
        }
        // remove extrusions that are entirely inside mask_area
        // note: extrusion are are part inside, part outside are perimeter that are both contour and holes, and they are kept, but considered contour afterwards.
        Extrusions deleted_extrusions = filter(output.extrusions, mask_area);
        output.inner = union_ex(output.inner, deleted_extrusions.as_polygon(width));
        // remove approximations
        output.inner = offset2_ex(output.inner , EPSILON, -EPSILON);
    }
}

void process_surface(Print &print,
                     PrintObject &object,
                     Layer &layer,
                     LayerSliceIsland &island,
                     LayerRegionIsland &region_island,
                     const ExPolygon &surface)
{
    // Build the initial high-level plan for this surface. This function should
    // own the contour/hole count rules copied from process_classic().
    PerimeterPlan plan = build_initial_plan(print, object, layer, island, region_island, surface);

    // Apply settings that alter only counts and high-level generation policy.
    // Each old setting branch should become a small function called from here.
    apply_perimeter_count_settings(plan, print, object, layer, island, region_island, surface);

    // Apply geometric masks that must be consumed while generating rings.
    // These are still pre-generation constraints, not post-processing edits.
    for (size_t idx_plugin = 0; idx_plugin < perimeter_mod_plugins.size(); ++idx_plugin) {
        PerimeterModPlugin &plugin = *perimeter_mod_plugins[idx_plugin];
        plugin.init_perimeter_generation(plan, print, object, layer, island, region_island, surface);
    }
    SurfaceResult result;
    ExPolygon current = surface;
    size_t perimeter_index = 0;

    while (should_generate_next_perimeter( perimeter_index)) {

        for (size_t idx_plugin = 0; idx_plugin < perimeter_mod_plugins.size(); ++idx_plugin) {
            PerimeterModPlugin &plugin = *perimeter_mod_plugins[idx_plugin];
            plugin.apply_geometry_masks( print, object, layer, island, region_island, surface);
        }

        // Convert the current plan state to the small black-box call. The
        // single-ring generator must remain independent from global config.
        PerimeterGenerationInput input = make_generation_input(plan, current, perimeter_index);

        // Generate exactly one perimeter ring and the next inner expolygon.
        // Classic and Arachne should both fit behind this contract.
        PerimeterGenerationOutput generated = perimeter_generation(input);

        // Accumulate extrusions and advance the current inner expolygon.
        for (size_t idx_plugin = perimeter_mod_plugins.size() - 1; idx_plugin < perimeter_mod_plugins.size(); --idx_plugin) {
            PerimeterModPlugin &plugin = *perimeter_mod_plugins[idx_plugin];
            plugin.absorb_generation_output(result, generated, current);
        }

        current = generated.inner;
        ++perimeter_index;
    }

    // Generate gap fill from the final inner geometry, once all requested
    // perimeter rings have been produced.
    // edit: nope, done in pot-proce or infill
    //generate_gap_fill(result, plan, current);

    // Build the fill surfaces that later infill steps will consume.
    build_fill_surfaces(result, plan, current);

    // Publish all generated data to the LayerSliceIsland / LayerRegionIsland
    // tree. Keeping this at the end makes the function easier to test.
    publish_surface_result(island, region_island, result);
}

} // namespace

void process(Print &print, PrintObject &object, Layer &layer, LayerSliceIsland &island)
{
    // First split the island into groups of compatible regions. This mirrors
    // the old Layer::make_perimeters responsibility, but keeps it outside the
    // actual perimeter generation algorithm.
    std::vector<LayerRegionSetCPtrs> region_groups = collect_region_groups(island, layer);

    for (const LayerRegionSetCPtrs &regions : region_groups) {
        // Create or retrieve the output node for this region group.
        LayerRegionIsland &region_island = prepare_region_island(island, regions);

        // Build the concrete expolygon surfaces that this region group will
        // process inside the current island.
        ExPolygons surfaces = build_surface_inputs(island, region_island);

        for (const ExPolygon &surface : surfaces) {
            // Process one surface independently. All setting-dependent logic is
            // routed through named helper functions inside process_surface().
            process_surface(print, object, layer, island, region_island, surface);
        }
    }
}

} // namespace Slic3r::PerimeterGenerator2
