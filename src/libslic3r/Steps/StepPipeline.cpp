///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "StepPipeline.hpp"

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/internal/LayerAccess.hpp"
#include "libslic3r/Api/internal/LayerRegionAccess.hpp"
#include "libslic3r/Api/internal/PrintObjectAccess.hpp"
#include "libslic3r/ConfigDef.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/Steps/StepDetectSupportSpots.hpp"
#include "libslic3r/Steps/StepDetectSurfaceType.hpp"
#include "libslic3r/Steps/StepExtrusionEdition.hpp"
#include "libslic3r/Steps/StepExtrusionOrdering.hpp"
#include "libslic3r/Steps/StepExtrusionSimplification.hpp"
#include "libslic3r/Steps/StepGenerateGcode.hpp"
#include "libslic3r/Steps/StepGenerateInfill.hpp"
#include "libslic3r/Steps/StepGeneratePerimeter.hpp"
#include "libslic3r/Steps/StepGenerateSupport.hpp"
#include "libslic3r/Steps/StepGenerateWipeTower.hpp"
#include "libslic3r/Steps/StepGroupInfillRegions.hpp"
#include "libslic3r/Steps/StepLayerExtrusionEdition.hpp"
#include "libslic3r/Steps/StepLayerHeightGeneration.hpp"
#include "libslic3r/Steps/StepLayerStiching.hpp"
#include "libslic3r/Steps/StepPostInfillGeneration.hpp"
#include "libslic3r/Steps/StepPostPerimeterGeneration.hpp"
#include "libslic3r/Steps/StepPostSlicing.hpp"
#include "libslic3r/Steps/StepPrepareForPeriemters.hpp"
#include "libslic3r/Steps/StepPrepareGcode.hpp"
#include "libslic3r/Steps/StepPrepareInfill.hpp"
#include "libslic3r/Steps/StepSlicing.hpp"
#include "libslic3r/Steps/StepSupportDemand.hpp"
#include "libslic3r/Steps/StepSurfaceGeneration.hpp"
#include "libslic3r/SurfaceCollection.hpp"
#include "libslic3r/Thread.hpp"

#ifdef _DEBUG
#include "libslic3r/Steps/DebugPrintProcessComparator.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>
#endif

#include <map>
#include <string>
#include <vector>

namespace Slic3r {
LayerUPtrs new_layers(PrintObject *print_object, const std::vector<double> &object_layers);
}

namespace Slic3r::Steps {
namespace {

StepExclusiveGroup make_exclusive_group(slicing_step_t step,
                                        const char *key,
                                        const char *label,
                                        raw_option_category category,
                                        const char *line_label)
{
    StepExclusiveGroup group = {};
    group.step = step;

    raw_config_option_def def = raw_config_option_def_init();
    def.opt_key = key;
    def.type = RAW_CO_ENUM;
    def.gui_type = RAW_GUI_TYPE_SELECT_CLOSE;
    def.container_type = RAW_CONTAINER_TYPE_PROJECT;
    def.option_preset_type = RAW_PRESET_TYPE_FFF_PRINT;
    def.printer_technology = RAW_PT_FFF;
    def.label = label;
    def.full_label = label;
    def.category = category;
    def.invalidates_step = step;
    def.tooltip = "Choose which active plugin owns this exclusive slicing step.";
    def.mode = RAW_CONFIG_OPTION_MODE_ADV_EXP | RAW_CONFIG_OPTION_MODE_SUSI;
    group.option_def = def;

    group.ui_fragment = std::string("page:Notes\n") +
                        "group:Exclusive step plugins\n" +
                        "line:" + line_label + "\n" +
                        "setting:" + key + "\n" +
                        "end_line\n";
    return group;
}

inline std::map<slicing_step_t, int> slicingstep_2_percent = {
    {STEP_LAYER_HEIGHT, 0},
    {STEP_SLICING, 5},
    {STEP_POST_SLICING, 10},
    {STEP_SURFACE_GENERATION, 15},
    {STEP_PRE_PERIMETER, 20},
    {STEP_PERIMETER, 25},
    {STEP_POST_PERIMETER, 30},
    {STEP_SURFACE_TYPE, 35},
    {STEP_PRE_INFILL, 40},
    {STEP_INFILL_GROUP, 45},
    {STEP_INFILL, 50},
    {STEP_POST_INFILL, 55},
    {STEP_SUPPORT_DEMAND, 60},
    {STEP_SUPPORT, 65},
    {STEP_PRE_GCODE, 70},
    {STEP_ORDERING, 75},
    {STEP_WIPETOWER, 80},
    {STEP_SUPPORT_SPOT, 81},
    {STEP_LAYER_EXTRUSION_EDIT, 82},
    {STEP_LAYER_STICHING, 85},
    {STEP_EXTRUSION_EDIT, 90},
    {STEP_EXTRUSION_SIMPLIFICATION, 95},
    {STEP_GCODE, 100},
};

} // namespace

void StepExclusiveGroup::set_enum_plugins(const std::vector<std::string> &plugin_ids)
{
    enum_values = plugin_ids;
    enum_labels = plugin_ids;
    enum_pairs.clear();
    enum_pairs.reserve(plugin_ids.size());

    for (size_t i = 0; i < enum_values.size(); ++i) {
        key_value_string_pair_t pair = {};
        pair.value = enum_values[i].c_str();
        pair.label = enum_labels[i].c_str();
        enum_pairs.push_back(pair);
    }

    option_def.enum_def.value_label_pairs.items = enum_pairs.empty() ? nullptr : enum_pairs.data();
    option_def.enum_def.value_label_pairs.count = uint32_t(enum_pairs.size());
    option_def.default_serialized_value = enum_values.empty() ? "" : enum_values.front().c_str();
}

const std::map<slicing_step_t, StepExclusiveGroup> &get_exclusive_steps()
{
    static const std::map<slicing_step_t, StepExclusiveGroup> s_groups = {
        {STEP_LAYER_HEIGHT,       make_exclusive_group(STEP_LAYER_HEIGHT,       "step_layer_height_plugin",      "Layer height plugin",      RAW_OPTION_CATEGORY_SLICING,      "Layer height step plugin")},
        {STEP_SLICING,            make_exclusive_group(STEP_SLICING,            "step_slicing_plugin",           "Slicing plugin",           RAW_OPTION_CATEGORY_SLICING,      "Slicing step plugin")},
        {STEP_PERIMETER,          make_exclusive_group(STEP_PERIMETER,          "step_perimeter_plugin",         "Perimeter plugin",         RAW_OPTION_CATEGORY_PERIMETER,    "Perimeter step plugin")},
        {STEP_SURFACE_GENERATION, make_exclusive_group(STEP_SURFACE_GENERATION, "step_surface_generation_plugin","Surface generation plugin",RAW_OPTION_CATEGORY_SLICING,      "Surface generation step plugin")},
        {STEP_SURFACE_TYPE,       make_exclusive_group(STEP_SURFACE_TYPE,       "step_surface_type_plugin",      "Surface type plugin",      RAW_OPTION_CATEGORY_SLICING,      "Surface type step plugin")},
        {STEP_INFILL_GROUP,       make_exclusive_group(STEP_INFILL_GROUP,       "step_infill_group_plugin",      "Infill grouping plugin",   RAW_OPTION_CATEGORY_INFILL,       "Infill grouping step plugin")},
        {STEP_INFILL,             make_exclusive_group(STEP_INFILL,             "step_infill_plugin",            "Infill plugin",            RAW_OPTION_CATEGORY_INFILL,       "Infill step plugin")},
        {STEP_SUPPORT,            make_exclusive_group(STEP_SUPPORT,            "step_support_plugin",           "Support plugin",           RAW_OPTION_CATEGORY_SUPPORT,      "Support step plugin")},
        {STEP_ORDERING,           make_exclusive_group(STEP_ORDERING,           "step_ordering_plugin",          "Ordering plugin",          RAW_OPTION_CATEGORY_OUTPUT,       "Ordering step plugin")},
        {STEP_WIPETOWER,          make_exclusive_group(STEP_WIPETOWER,          "step_wipetower_plugin",         "Wipe tower plugin",        RAW_OPTION_CATEGORY_OUTPUT,       "Wipe tower step plugin")},
        {STEP_LAYER_STICHING,     make_exclusive_group(STEP_LAYER_STICHING,     "step_layer_stiching_plugin",    "Layer stitching plugin",   RAW_OPTION_CATEGORY_OUTPUT,       "Layer stitching step plugin")},
        {STEP_GCODE,              make_exclusive_group(STEP_GCODE,              "step_gcode_plugin",             "G-code plugin",            RAW_OPTION_CATEGORY_OUTPUT,       "G-code step plugin")}
    };
    return s_groups;
}

std::vector<Plugin *> selected_or_active_plugins_for_step(Orchestrator &orchestrator,
                                                          slicing_step_t step,
                                                          const ConfigBase *config)
{
    std::vector<Plugin *> active_plugins = orchestrator.get_active_plugins_for_step(step);
    if (active_plugins.size() <= 1)
        return active_plugins;

    const std::map<slicing_step_t, StepExclusiveGroup> &exclusive_steps = get_exclusive_steps();
    const std::map<slicing_step_t, StepExclusiveGroup>::const_iterator exclusive_it = exclusive_steps.find(step);
    if (exclusive_it == exclusive_steps.end())
        return active_plugins;

    if (config == nullptr)
        return { active_plugins.front() };

    const ConfigOption *option = config->option(exclusive_it->second.option_def.opt_key);
    if (option == nullptr)
        return { active_plugins.front() };

    const int32_t selected_idx = option->get_int();
    if (selected_idx < 0 || size_t(selected_idx) >= active_plugins.size())
        return { active_plugins.front() };

    return { active_plugins[size_t(selected_idx)] };
}

namespace {

void begin_step(Print &print, slicing_step_t step, const std::string &message, const std::string &path)
{
    print.set_status(slicingstep_2_percent[step], message, {path});
    print.secondary_status_counter_reset();
}

bool stop_after(slicing_step_t current, slicing_step_t until)
{
    return current == until;
}

void run_layer_height_generation(Orchestrator &orchestrator, Print &print, const std::string &path)
{
    begin_step(print, STEP_LAYER_HEIGHT, L("Creating the layer height"), path);
    StepLayerHeightGeneration::clean_and_prepare(print);
    StepLayerHeightGeneration::run_step(orchestrator, print);
}

void run_slicing(Orchestrator &orchestrator, Print &print, const std::string &path)
{
    begin_step(print, STEP_SLICING, L("StepSlicing"), path);
    StepSlicing::run_step(orchestrator, print);
}

void run_post_slicing(Orchestrator &orchestrator, Print &print, const std::string &path)
{
    begin_step(print, STEP_POST_SLICING, L("Post-processing slices"), path);
    StepPostSlicing::clean_and_prepare(print);
    StepPostSlicing::run_step(orchestrator, print);
}

void run_remaining_steps(Orchestrator &orchestrator, Print &print, const std::string &path, slicing_step_t until = STEP_GCODE)
{
// not yet implemented
    StepSupportDemand::State support_demand;

    begin_step(print, STEP_PRE_PERIMETER, L("Preparing perimeters"), path);
    StepPrepareForPeriemters::clean_and_prepare(print);
    StepPrepareForPeriemters::run_step(orchestrator, print);
    if (stop_after(STEP_PRE_PERIMETER, until)) return;
    
    begin_step(print, STEP_PERIMETER, L("Generating perimeters"), path);
    StepGeneratePerimeter::clean_and_prepare(print);
    StepGeneratePerimeter::run_step(orchestrator, print);
    if (stop_after(STEP_PERIMETER, until)) return;
    
    begin_step(print, STEP_POST_PERIMETER, L("Post-processing perimeters"), path);
    StepPostPerimeterGeneration::clean_and_prepare(print);
    StepPostPerimeterGeneration::run_step(orchestrator, print);
    if (stop_after(STEP_POST_PERIMETER, until)) return;

    begin_step(print, STEP_SURFACE_GENERATION, L("Generating surfaces"), path);
    StepSurfaceGeneration::clean_and_prepare(print);
    StepSurfaceGeneration::run_step(orchestrator, print);
    if (stop_after(STEP_SURFACE_GENERATION, until)) return;

    begin_step(print, STEP_SURFACE_TYPE, L("Detecting surface types"), path);
    StepDetectSurfaceType::clean_and_prepare(print);
    StepDetectSurfaceType::run_step(orchestrator, print);
    if (stop_after(STEP_SURFACE_TYPE, until)) return;

    begin_step(print, STEP_PRE_INFILL, L("Preparing infill"), path);
    StepPrepareInfill::clean_and_prepare(print);
    StepPrepareInfill::run_step(orchestrator, print);
    if (stop_after(STEP_PRE_INFILL, until)) return;

    begin_step(print, STEP_INFILL_GROUP, L("Grouping infill regions"), path);
    StepGroupInfillRegions::clean_and_prepare(print);
    StepGroupInfillRegions::run_step(orchestrator, print);
    if (stop_after(STEP_INFILL_GROUP, until)) return;

    begin_step(print, STEP_INFILL, L("Generating infill"), path);
    StepGenerateInfill::clean_and_prepare(print);
    StepGenerateInfill::run_step(orchestrator, print);
    if (stop_after(STEP_INFILL, until)) return;

    begin_step(print, STEP_POST_INFILL, L("Post-processing infill"), path);
    StepPostInfillGeneration::clean_and_prepare(print);
    StepPostInfillGeneration::run_step(orchestrator, print);
    if (stop_after(STEP_POST_INFILL, until)) return;

    begin_step(print, STEP_SUPPORT_DEMAND, L("Detecting support demand"), path);
    StepSupportDemand::clean_and_prepare(print);
    StepSupportDemand::run_step(orchestrator, print, support_demand);
    if (stop_after(STEP_SUPPORT_DEMAND, until)) return;

    begin_step(print, STEP_SUPPORT, L("Generating support material"), path);
    StepGenerateSupport::clean_and_prepare(print);
    StepGenerateSupport::run_step(orchestrator, print, support_demand);
    if (stop_after(STEP_SUPPORT, until)) return;

    begin_step(print, STEP_PRE_GCODE, L("Preparing G-code"), path);
    StepPrepareGcode::clean_and_prepare(print);
    StepPrepareGcode::run_step(orchestrator, print);
    if (stop_after(STEP_PRE_GCODE, until)) return;

    begin_step(print, STEP_ORDERING, L("Ordering extrusions"), path);
    StepExtrusionOrdering::clean_and_prepare(print);
    StepExtrusionOrdering::run_step(orchestrator, print);
    if (stop_after(STEP_ORDERING, until)) return;

    begin_step(print, STEP_WIPETOWER, L("Generating wipe tower"), path);
    StepGenerateWipeTower::clean_and_prepare(print);
    StepGenerateWipeTower::run_step(orchestrator, print);
    if (stop_after(STEP_WIPETOWER, until)) return;

    begin_step(print, STEP_SUPPORT_SPOT, L("Detecting support spots"), path);
    StepDetectSupportSpots::clean_and_prepare(print);
    StepDetectSupportSpots::run_step(orchestrator, print);
    if (stop_after(STEP_SUPPORT_SPOT, until)) return;

    begin_step(print, STEP_LAYER_EXTRUSION_EDIT, L("Editing layers extrusions"), path);
    StepLayerExtrusionEdition::clean_and_prepare(print);
    StepLayerExtrusionEdition::run_step(orchestrator, print);
    if (stop_after(STEP_LAYER_EXTRUSION_EDIT, until)) return;

    begin_step(print, STEP_LAYER_STICHING, L("Stitching layers"), path);
    StepLayerStiching::clean_and_prepare(print);
    StepLayerStiching::run_step(orchestrator, print);
    if (stop_after(STEP_LAYER_STICHING, until)) return;

    begin_step(print, STEP_EXTRUSION_EDIT, L("Editing extrusions"), path);
    StepExtrusionEdition::clean_and_prepare(print);
    StepExtrusionEdition::run_step(orchestrator, print);
    if (stop_after(STEP_EXTRUSION_EDIT, until)) return;

    begin_step(print, STEP_EXTRUSION_SIMPLIFICATION, L("Simplifying extrusions"), path);
    StepExtrusionSimplification::clean_and_prepare(print);
    StepExtrusionSimplification::run_step(orchestrator, print);
    if (stop_after(STEP_EXTRUSION_SIMPLIFICATION, until)) return;

    begin_step(print, STEP_GCODE, L("Generating G-code"), path);
    StepGenerateGcode::clean_and_prepare(print);
    StepGenerateGcode::run_step(orchestrator, print);
}

#ifdef _DEBUG

void assert_same(DebugPrintProcessComparator &comparator, const char *label)
{
    std::string error;
    if (comparator.compare_tree_after(label, error))
        return;

    std::cerr << error << std::endl;
    assert(false && "Debug step pipeline comparison failed");
    throw std::runtime_error(error);
}

std::vector<double> to_unscaled_layer_height_profile(const std::vector<coord_t> &layer_profile)
{
    std::vector<double> out;
    out.reserve(layer_profile.size());
    for (coord_t value : layer_profile)
        out.push_back(unscaled(value));
    return out;
}

void recompute_layer_slices_from_regions(PrintObject &object)
{
    for (Layer &layer : object.layers())
        ApiInternal::LayerAccess::recompute_slices_from_layer_regions(layer);
}

void add_debug_surfaces_from_raw_slices(PrintObject &object)
{
    for (Layer &layer : object.layers()) {
        for (LayerRegion &region : layer.regions()) {
            SurfaceCollection &surfaces = ApiInternal::LayerRegionAccess::surfaces_mutable(region);
            surfaces.clear();
            // Native slice_volumes() creates temporary internal/sparse surfaces
            // from raw slices. Recreate them only for debug comparisons while
            // the plugin slicing step intentionally owns just the raw slices.
            surfaces.append(region.get_raw_slices(), stPosInternal | stDensSparse);
        }
    }
}

void add_debug_surfaces_from_raw_slices(Print &print)
{
    parallel_for(size_t(0), print.objects().size(), [&print](const size_t idx) {
        add_debug_surfaces_from_raw_slices(print.object(idx));
    });
}

void clear_debug_surfaces(PrintObject &object)
{
    for (Layer &layer : object.layers())
        for (LayerRegion &region : layer.regions())
            ApiInternal::LayerRegionAccess::surfaces_mutable(region).clear();
}

void clear_debug_surfaces(Print &print)
{
    parallel_for(size_t(0), print.objects().size(), [&print](const size_t idx) {
        clear_debug_surfaces(print.object(idx));
    });
}

#endif

} // namespace

void StepPipeline::run(Orchestrator &orchestrator, Print &print)
{
    const std::string path;
    orchestrator.reset_plugin_cancel();

    run_layer_height_generation(orchestrator, print, path);
    run_slicing(orchestrator, print, path);
    run_post_slicing(orchestrator, print, path);
    run_remaining_steps(orchestrator, print, path);
}

#ifdef _DEBUG
void StepPipeline::run_native_layer_height_generation(Print &print)
{
    parallel_for(size_t(0), print.objects().size(), [&print](const size_t idx) {
        StepPipeline::run_native_layer_height_generation_object(print.object(idx));
    });
}

void StepPipeline::run_native_slicing(Print &print)
{
    parallel_for(size_t(0), print.objects().size(), [&print](const size_t idx) {
        StepPipeline::run_native_slicing_object(print.object(idx));
    });
}

void StepPipeline::run_native_post_slicing(Print &print)
{
    parallel_for(size_t(0), print.objects().size(), [&print](const size_t idx) {
        StepPipeline::run_native_post_slicing_object(print.object(idx));
    });
}

void StepPipeline::run_native_layer_height_generation_object(PrintObject &object)
{
    object.update_slicing_parameters();

    std::vector<coordf_t> layer_height_profile;
    PrintObject::update_layer_height_profile(*object.model_object(), *object.m_slicing_params, layer_height_profile);

    std::vector<coord_t> scaled_profile;
    scaled_profile.reserve(layer_height_profile.size());
    for (coordf_t value : layer_height_profile)
        scaled_profile.push_back(scale_i(value));
    ApiInternal::PrintObjectAccess::set_layer_profile(object, std::move(scaled_profile));
}

void StepPipeline::run_native_slicing_object(PrintObject &object)
{
    LayerUPtrs object_layers =
        new_layers(&object, generate_object_layers(object.slicing_parameters(),
                                                   to_unscaled_layer_height_profile(object.layer_profile())));
    for (std::unique_ptr<Layer> &layer : object_layers)
        ApiInternal::LayerAccess::init_regions_from_object(*layer);

    ApiInternal::PrintObjectAccess::replace_layers_by_moving_contents(object, std::move(object_layers));
    object.slice_volumes();
    recompute_layer_slices_from_regions(object);
}

void StepPipeline::run_native_post_slicing_object(PrintObject &object)
{
    object._transform_hole_to_polyholes();
    object._max_overhang_threshold();
    recompute_layer_slices_from_regions(object);
}

void StepPipeline::debug_run(Orchestrator &orchestrator, const Print &source, slicing_step_t until)
{
    const std::string path;
    DebugPrintProcessComparator comparator(source);
    orchestrator.reset_plugin_cancel();

    assert_same(comparator, "START");

    run_native_layer_height_generation(comparator.reference_print());
    run_layer_height_generation(orchestrator, comparator.candidate_print(), path);
    assert_same(comparator, "STEP_LAYER_HEIGHT");
    if (stop_after(STEP_LAYER_HEIGHT, until)) return;

    run_native_slicing(comparator.reference_print());
    run_slicing(orchestrator, comparator.candidate_print(), path);
    // temporairement generer des surface dans les regionlayer du plugin pour correspondre à ce que fait le natif
    add_debug_surfaces_from_raw_slices(comparator.candidate_print());
    assert_same(comparator, "STEP_SLICING");
    // on retire les surfaces
    clear_debug_surfaces(comparator.candidate_print());
    if (stop_after(STEP_SLICING, until)) return;

    run_native_post_slicing(comparator.reference_print());
    run_post_slicing(orchestrator, comparator.candidate_print(), path);
    // temporairement generer des surface dans les regionlayer du plugin pour correspondre à ce que fait le natif
    add_debug_surfaces_from_raw_slices(comparator.candidate_print());
    assert_same(comparator, "STEP_POST_SLICING");
    // on retire les surfaces
    clear_debug_surfaces(comparator.candidate_print());
    if (stop_after(STEP_POST_SLICING, until)) return;

    // The remaining step classes are still being filled one by one. Keep the
    // candidate path executable. Add the native side above as each step gets a
    // stable split point.
    run_remaining_steps(orchestrator, comparator.candidate_print(), path, until);
}
#endif


} // namespace Slic3r::Steps
