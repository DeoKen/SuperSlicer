///|/ Copyright (c) Prusa Research 2017 - 2023 Oleksandra Iushchenko @YuSanka, Pavel Mikuš @Godrak, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv
///|/ Copyright (c) 2021 Ilya @xorza
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <cassert>

#include "libslic3r/Flow.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/libslic3r.h"

#include "PresetHints.hpp"

#include <wx/intl.h> 

#include "GUI.hpp"
#include "format.hpp"
#include "I18N.hpp"

namespace Slic3r {

#define MIN_BUF_LENGTH	4096

// Fork: fan speed of the first filament option, -1 if the option is not in the preset.
static int fan_option(const Preset &preset, const char *key)
{
    return preset.config.has(key) ? preset.config.opt_int(key, 0) : -1;
}

// Fork: the default fan speed is set, it replaces "Keep fan always on" + "Min fan speed" (CoolingBuffer::apply_layer_cooldown()).
static std::string cooling_description_default_fan(const Preset &preset, const int default_fan_speed)
{
    std::string out;
    const bool cooling                   = preset.config.opt_bool("cooling", 0);
    const int  fan_below_layer_time      = preset.config.opt_int("fan_below_layer_time", 0);
    const int  slowdown_below_layer_time = preset.config.opt_int("slowdown_below_layer_time", 0);
    const int  max_fan_speed             = preset.config.opt_int("max_fan_speed", 0);
    const int  min_print_speed           = int(preset.config.opt_float("min_print_speed", 0) + 0.5);
    const int  full_fan_speed_layer      = preset.config.opt_int("full_fan_speed_layer", 0);
    int        disable_fan_first_layers  = preset.config.opt_int("disable_fan_first_layers", 0);
    if (disable_fan_first_layers <= 0 && full_fan_speed_layer > 0)
        // As the G-code generator: with a ramp, the fan is off at least on the first layer.
        disable_fan_first_layers = 1;

    if (cooling) {
        out += GUI::format(_L("If estimated layer time is below ~%1%s, print speed will be reduced "
                              "so that no less than %1%s are spent on that layer "
                              "(however, speed will never be reduced below %2%mm/s)."),
                              slowdown_below_layer_time, min_print_speed);
        if (fan_below_layer_time > 0)
            out += " " + GUI::format(_L("Fan will run at least at %1%%% on such layers."), max_fan_speed);
        if (fan_below_layer_time > slowdown_below_layer_time && max_fan_speed > default_fan_speed)
            out += "\n" + GUI::format(_L("If estimated layer time is greater, but still below ~%1%s, "
                                         "fan will be raised proportionally towards %2%%%."),
                                         fan_below_layer_time, max_fan_speed);
        out += "\n";
    }
    if (default_fan_speed == 0)
        out += cooling ? _u8L("During the other layers, fan will be turned off.") : _u8L("Fan will be turned off.");
    else if (full_fan_speed_layer > disable_fan_first_layers + 1)
        out += GUI::format(_L("Fan speed will be ramped from zero at layer %1% to %2%%% at layer %3%."),
                           disable_fan_first_layers, default_fan_speed, full_fan_speed_layer);
    else {
        out += GUI::format(cooling ? _L("During the other layers, fan will always run at %1%%%") : _L("Fan will always run at %1%%%"), default_fan_speed) + " ";
        if (disable_fan_first_layers > 1)
            out += GUI::format(_L("except for the first %1% layers."), disable_fan_first_layers);
        else if (disable_fan_first_layers == 1)
            out += GUI::format(_L("except for the first layer."));
    }
    return out;
}

// Fork: the features with a fan speed of their own and the bridge fan speed (CoolingBuffer feature_fan_speeds()).
static std::string cooling_description_features(const Preset &preset, const int default_fan_speed)
{
    // Fan speed of their own, with the fallbacks of the G-code generator.
    const int perimeter         = fan_option(preset, "perimeter_fan_speed");
    const int external          = fan_option(preset, "external_perimeter_fan_speed") >= 0 ? fan_option(preset, "external_perimeter_fan_speed") : perimeter;
    const int solid             = fan_option(preset, "solid_infill_fan_speed");
    const int top               = fan_option(preset, "top_fan_speed") >= 0 ? fan_option(preset, "top_fan_speed") : solid;
    const int support           = fan_option(preset, "support_material_fan_speed");
    const int support_interface = fan_option(preset, "support_material_interface_fan_speed") >= 0 ? fan_option(preset, "support_material_interface_fan_speed") : support;
    const int overhangs         = fan_option(preset, "overhangs_fan_speed");
    const int internal_bridges  = fan_option(preset, "internal_bridge_fan_speed");
    const std::vector<std::pair<std::string, int>> features {
        { _u8L("internal perimeters"),       perimeter },
        { _u8L("external perimeters"),       external },
        { _u8L("overhangs"),                 overhangs },
        { _u8L("sparse infill"),             fan_option(preset, "infill_fan_speed") },
        { _u8L("solid infill"),              solid },
        { _u8L("top solid infill"),          top },
        { _u8L("gap fill"),                  fan_option(preset, "gap_fill_fan_speed") },
        { _u8L("support material"),          support },
        { _u8L("support interface"),         support_interface },
        { _u8L("internal bridges"),          internal_bridges },
    };
    std::string list;
    for (const auto &[name, fan_speed] : features)
        if (fan_speed >= 0)
            list += (list.empty() ? "" : ", ") + name + " " + std::to_string(fan_speed) + "%";

    std::string out;
    if (! list.empty())
        out += "\n" + GUI::format(_L("Fan speed of their own, used even if lower: %1%."), list);

    // Bridges, and overhangs / internal bridges without a fan speed of their own: the bridge fan only raises the fan.
    const int bridge_fan_speed = fan_option(preset, "bridge_fan_speed");
    if (bridge_fan_speed > 0) {
        std::string bridges = _u8L("Bridges");
        if (overhangs < 0)
            bridges += ", " + _u8L("overhangs");
        if (internal_bridges < 0)
            bridges += ", " + _u8L("internal bridges");
        const int base = default_fan_speed >= 0 ? default_fan_speed :
            preset.config.opt_bool("fan_always_on", 0) ? preset.config.opt_int("min_fan_speed", 0) : 0;
        out += "\n";
        if (bridge_fan_speed > base)
            out += GUI::format(_L("%1%: fan raised to %2%%%, kept if already higher."), bridges, bridge_fan_speed);
        else
            out += GUI::format(_L("%1%: no change, the bridges fan speed %2%%% only raises the fan and it is not above %3%%%."),
                               bridges, bridge_fan_speed, base);
    }
    return out;
}

std::string PresetHints::cooling_description(const Preset &preset)
{
    const int default_fan_speed = fan_option(preset, "default_fan_speed");
    if (default_fan_speed >= 0)
        return cooling_description_default_fan(preset, default_fan_speed) + cooling_description_features(preset, default_fan_speed);

	std::string out;

    bool cooling              = preset.config.opt_bool("cooling", 0);
    int  fan_below_layer_time = preset.config.opt_int("fan_below_layer_time", 0);
    int  full_fan_speed_layer = preset.config.opt_int("full_fan_speed_layer", 0);

    if (cooling) {
		int 	slowdown_below_layer_time 	= preset.config.opt_int("slowdown_below_layer_time", 0);
		int 	min_fan_speed 				= preset.config.opt_int("min_fan_speed", 0);
		int 	max_fan_speed 				= preset.config.opt_int("max_fan_speed", 0);
		int 	min_print_speed				= int(preset.config.opt_float("min_print_speed", 0) + 0.5);

        out += GUI::format(_L("If estimated layer time is below ~%1%s, "
                              "fan will run at %2%%% and print speed will be reduced "
                              "so that no less than %3%s are spent on that layer "
                              "(however, speed will never be reduced below %4%mm/s)."),
                              slowdown_below_layer_time, max_fan_speed, slowdown_below_layer_time, min_print_speed);
        if (fan_below_layer_time > slowdown_below_layer_time) {
            out += "\n";
            if (min_fan_speed != max_fan_speed)
                out += GUI::format(_L("If estimated layer time is greater, but still below ~%1%s, "
                               "fan will run at a proportionally decreasing speed between %2%%% and %3%%%."),
                               fan_below_layer_time, max_fan_speed, min_fan_speed);
            else
                out += GUI::format(_L("If estimated layer time is greater, but still below ~%1%s, "
                               "fan will run at %2%%%"),
                               fan_below_layer_time, min_fan_speed);
        }
        out += "\n";
    }
	if (preset.config.opt_bool("fan_always_on", 0)) {
		int 	disable_fan_first_layers 	= preset.config.opt_int("disable_fan_first_layers", 0);
		int 	min_fan_speed 				= preset.config.opt_int("min_fan_speed", 0);

        if (full_fan_speed_layer > disable_fan_first_layers + 1)
            out += GUI::format(_L("Fan speed will be ramped from zero at layer %1% to %2%%% at layer %3%."), disable_fan_first_layers, min_fan_speed, full_fan_speed_layer);
        else {
            out += GUI::format(cooling ? _L("During the other layers, fan will always run at %1%%%") : _L("Fan will always run at %1%%%"), min_fan_speed) + " ";
            if (disable_fan_first_layers > 1)
                out += GUI::format(_L("except for the first %1% layers."), disable_fan_first_layers);
            else if (disable_fan_first_layers == 1)
            	out += GUI::format(_L("except for the first layer."));
        }
    } else
       out += cooling ? _u8L("During the other layers, fan will be turned off.") : _u8L("Fan will be turned off.");

    return out + cooling_description_features(preset, -1);
}

static const ConfigOptionFloatOrPercent& first_positive(const ConfigOptionFloatOrPercent *v1, const ConfigOptionFloatOrPercent &v2, const ConfigOptionFloatOrPercent &v3)
{
    return (v1 != nullptr && v1->value > 0) ? *v1 : ((v2.value > 0) ? v2 : v3);
}

std::string PresetHints::maximum_volumetric_flow_description(const PresetBundle &preset_bundle)
{
    // Find out, to which nozzle index is the current filament profile assigned.
    int idx_extruder  = 0;
    int num_extruders = (int)preset_bundle.extruders_filaments.size();
    for (; idx_extruder < num_extruders; ++ idx_extruder)
        if (preset_bundle.extruders_filaments[idx_extruder].get_selected_preset_name() == preset_bundle.filaments.get_selected_preset_name())
            break;
    if (idx_extruder == num_extruders)
        // The current filament preset is not active for any extruder.
        idx_extruder = -1;

    const DynamicPrintConfig &print_config    = preset_bundle.prints   .get_edited_preset().config;
    const DynamicPrintConfig &filament_config = preset_bundle.filaments.get_edited_preset().config;
    const DynamicPrintConfig &printer_config  = preset_bundle.printers .get_edited_preset().config;

    // Current printer values.
    float  nozzle_diameter                  = (float)printer_config.opt_float("nozzle_diameter", idx_extruder);

    // Print config values
    double layer_height                     = print_config.opt_float("layer_height");
    assert(! print_config.option<ConfigOptionFloatOrPercent>("first_layer_height")->percent);
    double first_layer_height               = print_config.opt_float("first_layer_height");
    double support_material_speed           = print_config.opt_float("support_material_speed");
    double support_material_interface_speed = print_config.get_abs_value("support_material_interface_speed", support_material_speed);
    double bridge_speed                     = print_config.opt_float("bridge_speed");
    // Fork: internal bridge and brim / skirt speeds (GCodeGenerator::_extrude(), GCodeGenerator::brim_speed()).
    double internal_bridge_speed            = print_config.has("internal_bridge_speed") ? print_config.get_abs_value("internal_bridge_speed", bridge_speed) : bridge_speed;
    if (internal_bridge_speed <= 0.)
        internal_bridge_speed = bridge_speed;
    double brim_speed                       = print_config.has("brim_speed") ? print_config.get_abs_value("brim_speed", support_material_speed) : 0.;
    if (brim_speed <= 0.)
        brim_speed = support_material_speed;
    const bool has_skirt_brim               = print_config.opt_int("skirts") > 0 || print_config.opt_float("brim_width") > 0;
    const bool skirt_above_first_layer      = print_config.opt_int("skirts") > 0 &&
        (print_config.opt_int("skirt_height") > 1 || print_config.opt_serialize("draft_shield") != "disabled");
    double bridge_flow_ratio                = print_config.opt_float("bridge_flow_ratio");
    double perimeter_speed                  = print_config.opt_float("perimeter_speed");
    double external_perimeter_speed         = print_config.get_abs_value("external_perimeter_speed", perimeter_speed);
    // double gap_fill_speed                   = print_config.opt_bool("gap_fill_enabled") ? print_config.opt_float("gap_fill_speed") : 0.;
    double infill_speed                     = print_config.opt_float("infill_speed");
    double small_perimeter_speed            = print_config.get_abs_value("small_perimeter_speed", perimeter_speed);
    double solid_infill_speed               = print_config.get_abs_value("solid_infill_speed", infill_speed);
    double top_solid_infill_speed           = print_config.get_abs_value("top_solid_infill_speed", solid_infill_speed);
    // Maximum print speed when auto-speed is enabled by setting any of the above speed values to zero.
    double max_print_speed                  = print_config.opt_float("max_print_speed");
    // Maximum volumetric speed allowed for the print profile.
    double max_volumetric_speed             = print_config.opt_float("max_volumetric_speed");

    const auto &extrusion_width                     = *print_config.option<ConfigOptionFloatOrPercent>("extrusion_width");
    const auto &external_perimeter_extrusion_width  = *print_config.option<ConfigOptionFloatOrPercent>("external_perimeter_extrusion_width");
    const auto &first_layer_extrusion_width         = *print_config.option<ConfigOptionFloatOrPercent>("first_layer_extrusion_width");
    const auto &infill_extrusion_width              = *print_config.option<ConfigOptionFloatOrPercent>("infill_extrusion_width");
    const auto &perimeter_extrusion_width           = *print_config.option<ConfigOptionFloatOrPercent>("perimeter_extrusion_width");
    const auto &solid_infill_extrusion_width        = *print_config.option<ConfigOptionFloatOrPercent>("solid_infill_extrusion_width");
    const auto &support_material_extrusion_width    = *print_config.option<ConfigOptionFloatOrPercent>("support_material_extrusion_width");
    const auto &top_infill_extrusion_width          = *print_config.option<ConfigOptionFloatOrPercent>("top_infill_extrusion_width");
    const auto &first_layer_speed                   = *print_config.option<ConfigOptionFloatOrPercent>("first_layer_speed");

    // Index of an extruder assigned to a feature. If set to 0, an active extruder will be used for a multi-material print.
    // If different from idx_extruder, it will not be taken into account for this hint.
    auto feature_extruder_active = [idx_extruder, num_extruders](int i) {
        return i <= 0 || i > num_extruders || idx_extruder == -1 || idx_extruder == i - 1;
    };
    bool perimeter_extruder_active                  = feature_extruder_active(print_config.opt_int("perimeter_extruder"));
    bool infill_extruder_active                     = feature_extruder_active(print_config.opt_int("infill_extruder"));
    bool solid_infill_extruder_active               = feature_extruder_active(print_config.opt_int("solid_infill_extruder"));
    bool support_material_extruder_active           = feature_extruder_active(print_config.opt_int("support_material_extruder"));
    bool support_material_interface_extruder_active = feature_extruder_active(print_config.opt_int("support_material_interface_extruder"));

    // Current filament values
    double filament_diameter                = filament_config.opt_float("filament_diameter", 0);
    double filament_crossection             = M_PI * 0.25 * filament_diameter * filament_diameter;
    // double extrusion_multiplier             = filament_config.opt_float("extrusion_multiplier", 0);
    // The following value will be annotated by this hint, so it does not take part in the calculation.
//    double filament_max_volumetric_speed    = filament_config.opt_float("filament_max_volumetric_speed", 0);

    std::string out;
    for (size_t idx_type = (first_layer_extrusion_width.value == 0) ? 1 : 0; idx_type < 3; ++ idx_type) {
        // First test the maximum volumetric extrusion speed for non-bridging extrusions.
        bool first_layer = idx_type == 0;
        bool bridging    = idx_type == 2;
		const ConfigOptionFloatOrPercent *first_layer_extrusion_width_ptr = (first_layer && first_layer_extrusion_width.value > 0) ?
			&first_layer_extrusion_width : nullptr;
        const float                       lh  = float(first_layer ? first_layer_height : layer_height);
        double                            max_flow = 0.;
        std::string                       max_flow_extrusion_type;
        auto                              limit_by_first_layer_speed = [&first_layer_speed, first_layer](double speed_normal, double speed_max) {
            if (first_layer && first_layer_speed.value > 0)
                // Apply the first layer limit.
                speed_normal = first_layer_speed.get_abs_value(speed_normal);
            return (speed_normal > 0.) ? speed_normal : speed_max;
        };
        auto test_flow =
            [first_layer_extrusion_width_ptr, extrusion_width, nozzle_diameter, lh, bridging, bridge_flow_ratio, limit_by_first_layer_speed, max_print_speed, &max_flow, &max_flow_extrusion_type]
            (FlowRole flow_role, const ConfigOptionFloatOrPercent &this_extrusion_width, double speed, const char *err_msg) {
            // Fork: stock PrusaSlicer had the two flows swapped (bridging flow for the normal extrusions and the other way round).
            Flow flow = bridging ?
                Flow::bridging_flow(nozzle_diameter * bridge_flow_ratio, nozzle_diameter) :
                Flow::new_from_config_width(flow_role, first_positive(first_layer_extrusion_width_ptr, this_extrusion_width, extrusion_width), nozzle_diameter, lh);
            double volumetric_flow = flow.mm3_per_mm() * (bridging ? speed : limit_by_first_layer_speed(speed, max_print_speed));
            if (max_flow < volumetric_flow) {
                max_flow = volumetric_flow;
                max_flow_extrusion_type = GUI::into_u8(_(err_msg));
            }
        };
        if (bridging) {
            // Fork: the bridging flow is the same for all, only the bridge speeds differ (stock tested every feature at the bridge speed).
            if (solid_infill_extruder_active) {
                test_flow(frInfill, solid_infill_extrusion_width, bridge_speed, L("bridges"));
                test_flow(frInfill, solid_infill_extrusion_width, internal_bridge_speed, L("internal bridges"));
            }
        } else {
            if (perimeter_extruder_active) {
                test_flow(frExternalPerimeter, external_perimeter_extrusion_width, std::max(external_perimeter_speed, small_perimeter_speed), L("external perimeters"));
                test_flow(frPerimeter,         perimeter_extrusion_width,          std::max(perimeter_speed,          small_perimeter_speed), L("perimeters"));
            }
            if (infill_extruder_active)
                test_flow(frInfill, infill_extrusion_width, infill_speed, L("infill"));
            if (solid_infill_extruder_active) {
                test_flow(frInfill, solid_infill_extrusion_width, solid_infill_speed, L("solid infill"));
                test_flow(frInfill, top_infill_extrusion_width, top_solid_infill_speed, L("top solid infill"));
            }
            if (support_material_extruder_active)
                test_flow(frSupportMaterial, support_material_extrusion_width, support_material_speed, L("support"));
            if (support_material_interface_extruder_active)
                test_flow(frSupportMaterialInterface, support_material_extrusion_width, support_material_interface_speed, L("support interface"));
            // Fork: brim and skirt at brim_speed, the brim only on the first layer.
            if (first_layer ? has_skirt_brim : skirt_above_first_layer)
                test_flow(frPerimeter, extrusion_width, brim_speed, L("brim / skirt"));
        }
        //FIXME handle gap_fill_speed
        if (! out.empty())
            out += "\n";
        out += (first_layer ? _u8L("First layer volumetric") : (bridging ? _u8L("Bridging volumetric") : _u8L("Volumetric")));
        out += " " + _u8L("flow rate is maximized") + " ";
        bool limited_by_max_volumetric_speed = max_volumetric_speed > 0 && max_volumetric_speed < max_flow;
        out += (limited_by_max_volumetric_speed ? 
            _u8L("by the print profile maximum") :
            (_u8L("when printing")+ " " + max_flow_extrusion_type))
            + " " + _u8L("with a volumetric rate")+ " ";
        if (limited_by_max_volumetric_speed)
            max_flow = max_volumetric_speed;

        out += format(_u8L("%3.2f mm³/s at filament speed %3.2f mm/s."), max_flow, (max_flow / filament_crossection));
    }

 	return out;
}

std::string PresetHints::recommended_thin_wall_thickness(const PresetBundle &preset_bundle)
{
    const DynamicPrintConfig &print_config    = preset_bundle.prints   .get_edited_preset().config;
    const DynamicPrintConfig &printer_config  = preset_bundle.printers .get_edited_preset().config;

    float   layer_height                        = float(print_config.opt_float("layer_height"));
    int     num_perimeters                      = print_config.opt_int("perimeters");
    bool    thin_walls                          = print_config.opt_bool("thin_walls");
    float   nozzle_diameter                     = float(printer_config.opt_float("nozzle_diameter", 0));
    
    std::string out;
	if (layer_height <= 0.f) {
		out += _u8L("Recommended object thin wall thickness: Not available due to invalid layer height.");
		return out;
	}
    
    if (num_perimeters > 0) {
        int num_lines = std::min(num_perimeters * 2, 10);
        out += (boost::format(_u8L("Recommended object thin wall thickness for layer height %.2f and")) % layer_height).str() + " ";
        // Start with the width of two closely spaced 
        try {
            Flow external_perimeter_flow = Flow::new_from_config_width(
                frExternalPerimeter, 
                *print_config.opt<ConfigOptionFloatOrPercent>("external_perimeter_extrusion_width"), 
                nozzle_diameter, layer_height);
            Flow perimeter_flow          = Flow::new_from_config_width(
                frPerimeter, 
                *print_config.opt<ConfigOptionFloatOrPercent>("perimeter_extrusion_width"), 
                nozzle_diameter, layer_height);
	        double width = external_perimeter_flow.width() + external_perimeter_flow.spacing();
	        for (int i = 2; i <= num_lines; thin_walls ? ++ i : i += 2) {
	            if (i > 2)
	                out += ", ";
	            out += (boost::format(_u8L("%d lines: %.2f mm")) % i %  width).str() + " ";
	            width += perimeter_flow.spacing() * (thin_walls ? 1.f : 2.f);
	        }
	    } catch (const FlowErrorNegativeSpacing &) {
            out = _u8L("Recommended object thin wall thickness: Not available due to excessively small extrusion width.");
        }
    }
    return out;
}


// Produce a textual explanation of the combined effects of the top/bottom_solid_layers
// versus top/bottom_min_shell_thickness. Which of the two values wins depends
// on the active layer height.
std::string PresetHints::top_bottom_shell_thickness_explanation(const PresetBundle &preset_bundle)
{
    const DynamicPrintConfig &print_config    = preset_bundle.prints   .get_edited_preset().config;
    const DynamicPrintConfig &printer_config  = preset_bundle.printers .get_edited_preset().config;

    std::string out;

    int 	top_solid_layers                = print_config.opt_int("top_solid_layers");
    int 	bottom_solid_layers             = print_config.opt_int("bottom_solid_layers");
    bool    has_top_layers 					= top_solid_layers > 0;
    bool    has_bottom_layers 				= bottom_solid_layers > 0;
    double  top_solid_min_thickness        	= print_config.opt_float("top_solid_min_thickness");
    double  bottom_solid_min_thickness  	= print_config.opt_float("bottom_solid_min_thickness");
    double  layer_height                    = print_config.opt_float("layer_height");
    bool    variable_layer_height			= printer_config.opt_bool("variable_layer_height");
    //FIXME the following line takes into account the 1st extruder only.
    double  min_layer_height				= variable_layer_height ? Slicing::min_layer_height_from_nozzle(printer_config, 1) : layer_height;

	if (layer_height <= 0.f) {
		out += _u8L("Top / bottom shell thickness hint: Not available due to invalid layer height.");
		return out;
	}

    if (has_top_layers) {
    	double top_shell_thickness = top_solid_layers * layer_height;
    	if (top_shell_thickness < top_solid_min_thickness) {
    		// top_solid_min_shell_thickness triggers even in case of normal layer height. Round the top_shell_thickness up
    		// to an integer multiply of layer_height.
    		double n = ceil(top_solid_min_thickness / layer_height);
    		top_shell_thickness = n * layer_height;
    	}
    	double top_shell_thickness_minimum = std::max(top_solid_min_thickness, top_solid_layers * min_layer_height);
        out += (boost::format(_u8L("Top shell is %1% mm thick for layer height %2% mm.")) % top_shell_thickness % layer_height).str();
        if (variable_layer_height && top_shell_thickness_minimum < top_shell_thickness) {
        	out += " ";
	        out += (boost::format(_u8L("Minimum top shell thickness is %1% mm.")) % top_shell_thickness_minimum).str();        	
        }
    } else
        out += _u8L("Top is open.");

    out += "\n";

    if (has_bottom_layers) {
    	double bottom_shell_thickness = bottom_solid_layers * layer_height;
    	if (bottom_shell_thickness < bottom_solid_min_thickness) {
    		// bottom_solid_min_shell_thickness triggers even in case of normal layer height. Round the bottom_shell_thickness up
    		// to an integer multiply of layer_height.
    		double n = ceil(bottom_solid_min_thickness / layer_height);
    		bottom_shell_thickness = n * layer_height;
    	}
    	double bottom_shell_thickness_minimum = std::max(bottom_solid_min_thickness, bottom_solid_layers * min_layer_height);
        out += (boost::format(_u8L("Bottom shell is %1% mm thick for layer height %2% mm.")) % bottom_shell_thickness % layer_height).str();
        if (variable_layer_height && bottom_shell_thickness_minimum < bottom_shell_thickness) {
        	out += " ";
	        out += (boost::format(_u8L("Minimum bottom shell thickness is %1% mm.")) % bottom_shell_thickness_minimum).str();        	
        }
    } else 
        out += _u8L("Bottom is open.");

    return out;
}

}; // namespace Slic3r
