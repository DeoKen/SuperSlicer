///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas, Tomáš Mészáros @tamasmeszaros, Oleksandra Iushchenko @YuSanka, Pavel Mikuš @Godrak, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Filip Sykala @Jony01, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2023 Pedro Lamas @PedroLamas
///|/ Copyright (c) 2023 Mimoja @Mimoja
///|/ Copyright (c) 2020 - 2021 Sergey Kovalev @RandoMan70
///|/ Copyright (c) 2021 Niall Sheridan @nsheridan
///|/ Copyright (c) 2021 Martin Budden
///|/ Copyright (c) 2021 Ilya @xorza
///|/ Copyright (c) 2020 Paul Arden @ardenpm
///|/ Copyright (c) 2020 rongith
///|/ Copyright (c) 2019 Spencer Owen @spuder
///|/ Copyright (c) 2019 Stephan Reichhelm @stephanr
///|/ Copyright (c) 2018 Martin Loidl @LoidlM
///|/ Copyright (c) SuperSlicer 2018 Remi Durand @supermerill
///|/ Copyright (c) 2016 - 2017 Joseph Lenox @lordofhyphens
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2016 Vanessa Ezekowitz @VanessaE
///|/ Copyright (c) 2015 Alexander Rössler @machinekoder
///|/ Copyright (c) 2014 Petr Ledvina @ledvinap
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "FFFPrintConfig.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/nowide/iostream.hpp>

#include "Flow.hpp"
#include "PointUtils.hpp"

namespace Slic3r {

static t_config_enum_names enum_names_from_keys_map(const t_config_enum_values &enum_keys_map)
{
    t_config_enum_names names;
    int cnt = 0;
    for (const auto& kvp : enum_keys_map)
        cnt = std::max(cnt, kvp.second);
    cnt += 1;
    names.assign(cnt, "");
    for (const auto& kvp : enum_keys_map)
        names[kvp.second] = kvp.first;
    return names;
}

#define CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(NAME, ...) \
    static const t_config_enum_values& enum_keys_map_##NAME() { \
        static const t_config_enum_values keys_map __VA_ARGS__; \
        return keys_map; \
    } \
    template<> const t_config_enum_values& ConfigOptionEnum<NAME>::get_enum_values() { return enum_keys_map_##NAME(); } \
    template<> const t_config_enum_names& ConfigOptionEnum<NAME>::get_enum_names() { \
        static const t_config_enum_names keys_names = enum_names_from_keys_map(enum_keys_map_##NAME()); \
        return keys_names; \
    }

CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(ArcFittingType, {
    { "disabled",       int(ArcFittingType::Disabled) },
    { "bambu",          int(ArcFittingType::Bambu) },
    { "emit_center",    int(ArcFittingType::ArcWelder) } // arcwelder
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(CompleteObjectSort, {
    {"nearest", cosNearest},
    {"object", cosObject},
    {"lowy", cosY},
    {"lowz", cosZ},
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(WipeAlgo, {
    {"linear", waLinear},
    {"quadra", waQuadra},
    {"expo", waHyper},
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(GCodeFlavor, {
    {"reprapfirmware",  gcfRepRap},
    {"repetier",        gcfRepetier},
    {"teacup",          gcfTeacup},
    {"makerware",       gcfMakerWare},
    {"marlin",          gcfMarlinLegacy },
    {"marlin2",         gcfMarlinFirmware },
    {"klipper",         gcfKlipper},
    {"sailfish",        gcfSailfish},
    {"smoothie",        gcfSmoothie},
    {"sprinter",        gcfSprinter},
    {"mach3",           gcfMach3},
    {"machinekit",      gcfMachinekit},
    {"no-extrusion",    gcfNoExtrusion},
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(MachineLimitsUsage, {
    {"emit_to_gcode",       int(MachineLimitsUsage::EmitToGCode)},
    {"time_estimate_only",  int(MachineLimitsUsage::TimeEstimateOnly)},
    {"limits",              int(MachineLimitsUsage::Limits)},
    {"ignore",              int(MachineLimitsUsage::Ignore)},
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(BridgeType, {
    {"nozzle",  uint8_t(BridgeType::btFromNozzle)},
    {"height",  uint8_t(BridgeType::btFromHeight)},
    {"flow",    uint8_t(BridgeType::btFromFlow)},
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(FuzzySkinType, {
    { "none",           int(FuzzySkinType::None) },
    { "external",       int(FuzzySkinType::External) },
    { "shell",          int(FuzzySkinType::Shell) },
    { "all",            int(FuzzySkinType::All) }
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(InfillPattern, {
    {"rectilinear",         ipRectilinear},
    {"alignedrectilinear",  ipAlignedRectilinear},
    {"monotonic",           ipMonotonic},
    {"grid",                ipGrid},
    {"triangles",           ipTriangles},
    {"stars",               ipStars},
    {"cubic",               ipCubic},
    {"line",                ipLine},
    {"monotoniclines",      ipMonotonicLines },
    {"concentric",          ipConcentric},
    {"honeycomb",           ipHoneycomb},
    {"3dhoneycomb",         ip3DHoneycomb},
    {"gyroid",              ipGyroid},
    {"hilbertcurve",        ipHilbertCurve},
    {"archimedeanchords",   ipArchimedeanChords},
    {"octagramspiral",      ipOctagramSpiral},
    {"smooth",              ipSmooth},
    {"smoothtriple",        ipSmoothTriple},
    {"smoothhilbert",       ipSmoothHilbert},
    {"rectiwithperimeter",  ipRectiWithPerimeter},
    {"scatteredrectilinear", ipScatteredRectilinear},
    {"sawtooth",            ipSawtooth},
    {"adaptivecubic",       ipAdaptiveCubic},
    {"supportcubic",        ipSupportCubic},
    {"lightning",           ipLightning},
    {"ensuring",            ipEnsuring},
    {"auto",                ipAuto}
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(IroningType, {
    { "top",            int(IroningType::TopSurfaces) },
    { "topmost",        int(IroningType::TopmostOnly) },
    { "solid",          int(IroningType::AllSolid) }
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PerimeterDirection, {
    {"ccw_cw",  pdCCW_CW},
    {"ccw_ccw", pdCCW_CCW},
    {"cw_ccw",  pdCW_CCW},
    {"cw_cw",   pdCW_CW},
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SupportMaterialPattern, {
    { "rectilinear",        smpRectilinear },
    { "rectilinear-grid",   smpRectilinearGrid },
    { "honeycomb",          smpHoneycomb }
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SupportMaterialStyle, {
    { "grid",           smsGrid },
    { "snug",           smsSnug },
    { "tree",           smsTree },
    { "organic",        smsOrganic }
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SeamPosition, {
        {"random",    spRandom},
        {"allrandom", spAllRandom},
        {"nearest",   spNearest}, // unused, replaced by cost
        {"cost",      spCost},
        {"aligned", spAligned},
        {"contiguous", spExtremlyAligned},
        {"rear", spRear},
        {"custom", spCustom}, // for seam object
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SeamScarfType, {
    { "none",           int(SeamScarfType::None) },
    { "external",       int(SeamScarfType::External) },
    { "all",            int(SeamScarfType::All) },
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(DenseInfillAlgo, {
        { "automatic", dfaAutomatic },
        { "autonotfull", dfaAutoNotFull },
        { "autoenlarged", dfaAutoOrEnlarged },
        { "autosmall",  dfaAutoOrNothing},
        { "enlarged", dfaEnlarged },
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(NoPerimeterUnsupportedAlgo, {
        { "none", npuaNone },
        { "noperi", npuaNoPeri },
        { "bridges", npuaBridges },
        { "bridgesoverhangs", npuaBridgesOverhangs },
        { "filled", npuaFilled },
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(InfillConnection, {
        { "connected", icConnected },
        { "holes", icHoles },
        { "outershell", icOuterShell },
        { "notconnected", icNotConnected },
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(RemainingTimeType, {
    { "m117", rtM117 },
    { "m73", rtM73 },
    { "m73q", rtM73_Quiet },
    { "m73m117", rtM73_M117 },
    { "none", rtNone },
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SupportZDistanceType, {
    { "filament", zdFilament },
    { "plane", zdPlane },
    { "none", zdNone },
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(BrimType, {
    {"no_brim",         btNoBrim},
    {"outer_only",      btOuterOnly},
    {"inner_only",      btInnerOnly},
    {"outer_and_inner", btOuterAndInner}
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(DraftShield, {
    { "disabled", dsDisabled },
    { "limited",  dsLimited  },
    { "enabled",  dsEnabled  }
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(LabelObjectsStyle, {
    { "disabled",  int(LabelObjectsStyle::Disabled)  },
    { "octoprint", int(LabelObjectsStyle::Octoprint) },
    { "firmware",  int(LabelObjectsStyle::Firmware)  },
    { "both",      int(LabelObjectsStyle::Both)},
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(GCodeThumbnailsFormat, {
    { "PNG", int(GCodeThumbnailsFormat::PNG) },
    { "JPG", int(GCodeThumbnailsFormat::JPG) },
    { "QOI", int(GCodeThumbnailsFormat::QOI) },
    { "BIQU",int(GCodeThumbnailsFormat::BIQU) },
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PerimeterGeneratorType, {
    { "classic", int(PerimeterGeneratorType::Classic) },
    { "arachne", int(PerimeterGeneratorType::Arachne) }
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(EnsureVerticalShellThickness, {
    { "disabled", int(EnsureVerticalShellThickness::Disabled) },
    { "partial",  int(EnsureVerticalShellThickness::Partial)  },
    { "enabled",  int(EnsureVerticalShellThickness::Enabled)  },
    { "enabled_old",  int(EnsureVerticalShellThickness::Enabled_old)  },
})

/*
double min_object_distance(const ConfigBase &cfg)
{
    const ConfigOptionEnum<PrinterTechnology> *opt_printer_technology = cfg.option<ConfigOptionEnum<PrinterTechnology>>("printer_technology");
    auto printer_technology = opt_printer_technology ? opt_printer_technology->value : ptUnknown;
    double ret = 0.;

    if (printer_technology == ptSLA)
        ret = 6.;
    else {
        auto ecr_opt = cfg.option<ConfigOptionFloat>("extruder_clearance_radius");
        auto dd_opt  = cfg.option<ConfigOptionFloat>("duplicate_distance");
        auto co_opt  = cfg.option<ConfigOptionBool>("complete_objects");

        if (!ecr_opt || !dd_opt || !co_opt) ret = 0.;
        else {
            // min object distance is max(duplicate_distance, clearance_radius)
            ret = (co_opt->value && ecr_opt->value > dd_opt->value) ?
                      ecr_opt->value : dd_opt->value;
        }
    }

    return ret;
}*/

double min_object_distance(const PrintConfig& config)
{
    return min_object_distance(static_cast<const ConfigBase*>(&config));
}

double min_object_distance(const ConfigBase *config, double ref_height /* = 0*/)
{
    if (printer_technology(*config) == ptSLA) return 6.;

    const ConfigOptionFloat* dd_opt = config->option<ConfigOptionFloat>("duplicate_distance");
    //test if called from prusaslicer::l240 where it's called on an empty config...
    if (dd_opt == nullptr) return 0;

    double base_dist = 0;
    //std::cout << "START min_object_distance =>" << base_dist << "\n";
    const ConfigOptionBool* opt_complete_object = config->option<ConfigOptionBool>("complete_objects");
    const ConfigOption* opt_parallel_objects_step = config->option("parallel_objects_step");
    if ((opt_parallel_objects_step && opt_parallel_objects_step->get_float() > 0) || (opt_complete_object && opt_complete_object->value)) {
        double skirt_dist = 0;
        double brim_dist = 0;
        try {
            std::vector<double> vals = dynamic_cast<const ConfigOptionFloats*>(config->option("nozzle_diameter"))->get_values();
            double max_nozzle_diam = 0;
            for (double val : vals) max_nozzle_diam = std::fmax(max_nozzle_diam, val);

            // min object distance is max(duplicate_distance, clearance_radius)
            // add 1 as safety offset.
            const double extruder_clearance_radius = config->option("extruder_clearance_radius")->get_float();
            if (extruder_clearance_radius > base_dist) {
                base_dist = extruder_clearance_radius;
            }

            // Add aso the skirt dist if per object, as the arrange & check method don't use it yet.
            // we use the max nozzle, just to be on the safe side
            //ideally, we should use print::first_layer_height()
            const double first_layer_height =
                dynamic_cast<const ConfigOptionFloatOrPercent *>(config->option("first_layer_height"))
                    ->get_effective_value(max_nozzle_diam);
            //add the skirt
            int skirts = config->option("skirts")->get_int();
            if (skirts > 0 && ref_height == 0)
                skirts += config->option("skirt_brim")->get_int();
            if (skirts > 0 && config->option("skirt_height")->get_int() >= 1 &&
                !config->option("complete_objects_one_skirt")->get_bool()) {
                float overlap_ratio = 1;
                //can't know the extruder, so we settle on the worst: 100%
                //if (config->option<ConfigOptionPercents>("filament_max_overlap")) overlap_ratio = config->get_computed_value("filament_max_overlap");
                if (ref_height == 0) {
                    skirt_dist = config->option("skirt_distance")->get_float();
                    Flow skirt_flow = Flow::new_from_config_width(
                        frPerimeter,
                        *Flow::extrusion_width_option("skirt", *config),
                        *Flow::extrusion_spacing_option("skirt", *config),
                        (float)max_nozzle_diam,
                        (float)first_layer_height,
                        overlap_ratio,
                        0
                    );
                    skirt_dist += skirt_flow.width() + (skirt_flow.spacing() * ((double)skirts - 1));
                } else {
                    double skirt_height = ((double)config->option("skirt_height")->get_int() - 1) * config->get_computed_value("layer_height") + first_layer_height;
                    if (ref_height <= skirt_height) {
                        skirt_dist = config->option("skirt_distance")->get_float();
                        Flow skirt_flow = Flow::new_from_config_width(
                            frPerimeter,
                            *Flow::extrusion_width_option("skirt", *config),
                            *Flow::extrusion_spacing_option("skirt", *config),
                            (float)max_nozzle_diam,
                            (float)first_layer_height,
                            overlap_ratio,
                            0
                        );
                        skirt_dist += skirt_flow.width() + (skirt_flow.spacing() * ((double)skirts - 1));
                    }
                }
                // send a warning in print.validate if oneskirt, the skirt height is > 1mm and the skirt distance (from brim) is < extruder_clearance_radius
                // send a warning in print.validate if not oneskirt and skirt height > 1mm (you might collide the skirt while printing another one)
            }
            // Add also the biggest object brim, as the arrange & check method don't use it yet.
            // mm we don't have access to each object config... then send a warning in print.validate.
            const ConfigOption *opt_brim_per_object = config->option("brim_per_object");
            const ConfigOption *opt_skirt_distance_from_brim = config->option("skirt_distance_from_brim");
            const bool has_brim = (ref_height == 0 && opt_brim_per_object && opt_brim_per_object->get_bool());
            const bool skirt_is_pushed = skirt_dist > 0 && opt_skirt_distance_from_brim && opt_skirt_distance_from_brim->get_bool();
            if ( has_brim || skirt_is_pushed) {
                double max_brim = config->option("brim_width")->get_float();
                max_brim = std::max(max_brim, config->option("brim_width_interior")->get_float());
            }

            // if skirt_distance_from_brim, then push it further back
            if (skirt_is_pushed) {
                skirt_dist += brim_dist;
                brim_dist = 0;
            }
        }
        catch (const std::exception & ex) {
            boost::nowide::cerr << ex.what() << std::endl;
        }

        return base_dist + std::max(skirt_dist, brim_dist);
    }
    // else (not cmplete object/step)
    return base_dist;
}

//FIXME localize this function.
//note: seems only called for config export & command line. Most of the validation work for the gui is done elsewhere... So this function may be a bit out-of-sync
std::string validate(const FullPrintConfig& cfg)
{
    // --layer-height
    if (cfg.get_computed_value("layer_height") <= 0)
        return "Invalid value for --layer-height";
    if (fabs(fmod(cfg.get_computed_value("layer_height"), SCALING_FACTOR)) > 1e-4)
        return "--layer-height must be a multiple of print resolution";

    // --first-layer-height
    //if (cfg.get_effective_value("first_layer_height") <= 0) //can't do that, as the extruder isn't defined
    if(cfg.first_layer_height.value <= 0)
        return "Invalid value for --first-layer-height";

    // --filament-diameter
    for (double fd : cfg.filament_diameter.get_values())
        if (fd < 1)
            return "Invalid value for --filament-diameter";

    // --nozzle-diameter
    for (double nd : cfg.nozzle_diameter.get_values())
        if (nd < 0.005)
            return "Invalid value for --nozzle-diameter";

    // --perimeters
    if (cfg.perimeters.value < 0)
        return "Invalid value for --perimeters";

    // --solid-layers
    if (cfg.top_solid_layers < 0)
        return "Invalid value for --top-solid-layers";
    if (cfg.bottom_solid_layers < 0)
        return "Invalid value for --bottom-solid-layers";

    if (cfg.use_firmware_retraction.value &&
        cfg.gcode_flavor.value != gcfSmoothie &&
        cfg.gcode_flavor.value != gcfSprinter &&
        cfg.gcode_flavor.value != gcfRepRap &&
        cfg.gcode_flavor.value != gcfMarlinLegacy &&
        cfg.gcode_flavor.value != gcfMarlinFirmware &&
        cfg.gcode_flavor.value != gcfMachinekit &&
        cfg.gcode_flavor.value != gcfRepetier &&
        cfg.gcode_flavor.value != gcfKlipper)
        return "--use-firmware-retraction is only supported by Marlin 1&2, Smoothie, Sprinter, Reprap, Repetier, Machinekit, Repetier, Klipper? and Lerdge firmware";

    if (cfg.use_firmware_retraction.value)
        for (unsigned char wipe : cfg.wipe.get_values())
             if (wipe)
                return "--use-firmware-retraction is not compatible with --wipe";

    // --gcode-flavor
    if (! PrintConfigDef::instance().get("gcode_flavor")->has_enum_value(cfg.gcode_flavor.serialize()))
        return "Invalid value for --gcode-flavor";

    // --fill-pattern
    if (! PrintConfigDef::instance().get("fill_pattern")->has_enum_value(cfg.fill_pattern.serialize()))
        return "Invalid value for --fill-pattern";

    // --top-fill-pattern
    if (!PrintConfigDef::instance().get("top_fill_pattern")->has_enum_value(cfg.top_fill_pattern.serialize()))
        return "Invalid value for --top-fill-pattern";

    // --bottom-fill-pattern
    if (! PrintConfigDef::instance().get("bottom_fill_pattern")->has_enum_value(cfg.bottom_fill_pattern.serialize()))
        return "Invalid value for --bottom-fill-pattern";

    // --solid-fill-pattern
    if (!PrintConfigDef::instance().get("solid_fill_pattern")->has_enum_value(cfg.solid_fill_pattern.serialize()))
        return "Invalid value for --solid-fill-pattern";

    // --brim-ears-pattern
    if (!PrintConfigDef::instance().get("brim_ears_pattern")->has_enum_value(cfg.brim_ears_pattern.serialize()))
        return "Invalid value for --brim-ears-pattern";

    // --fill-density
    if (fabs(cfg.fill_density.value - 100.) < EPSILON &&
        (! PrintConfigDef::instance().get("top_fill_pattern")->has_enum_value(cfg.fill_pattern.serialize())
        && ! PrintConfigDef::instance().get("bottom_fill_pattern")->has_enum_value(cfg.fill_pattern.serialize())
        ))
        return "The selected fill pattern is not supposed to work at 100% density";

    // --infill-every-layers
    if (cfg.infill_every_layers < 1)
        return "Invalid value for --infill-every-layers";

    // --skirt-height
    if (cfg.skirt_height < 0)
        return "Invalid value for --skirt-height";

    // extruder clearance
    if (cfg.extruder_clearance_radius < 0)
        return "Invalid value for --extruder-clearance-radius";
    if (cfg.extruder_clearance_height < 0)
        return "Invalid value for --extruder-clearance-height";

    // --extrusion-multiplier
    for (double em : cfg.extrusion_multiplier.get_values())
        if (em <= 0)
            return "Invalid value for --extrusion-multiplier";

    // --spiral-vase
    if (cfg.spiral_vase) {
        // Note that we might want to have more than one perimeter on the bottom
        // solid layers.
        if (cfg.perimeters > 1)
            return "Can't make more than one perimeter when spiral vase mode is enabled";
        else if (cfg.perimeters < 1)
            return "Can't make less than one perimeter when spiral vase mode is enabled";
        if (cfg.fill_density > 0)
            return "Spiral vase mode can only print hollow objects, so you need to set Fill density to 0";
        if (cfg.top_solid_layers > 0)
            return "Spiral vase mode is not compatible with top solid layers";
        if (cfg.support_material || cfg.support_material_enforce_layers > 0)
            return "Spiral vase mode is not compatible with support material";
        if (cfg.infill_dense)
            return "Spiral vase mode can only print hollow objects and have no top surface, so you don't need any dense infill";
        if (cfg.extra_perimeters || cfg.extra_perimeters_below_area.value > 0 || cfg.extra_perimeters_count.value > 0 || cfg.extra_perimeters_on_overhangs || cfg.extra_perimeters_odd_layers)
            return "Can't make more than one perimeter when spiral vase mode is enabled";
        if (cfg.overhangs_reverse)
            return "Can't reverse the direction of the overhangs every layer when spiral vase mode is enabled";
        if (cfg.perimeter_reverse)
            return "Can't reverse the direction of the perimeters every layer when spiral vase mode is enabled";
    }

    // extrusion widths
    {
        double max_nozzle_diameter = 0.;
        for (double dmr : cfg.nozzle_diameter.get_values())
            max_nozzle_diameter = std::max(max_nozzle_diameter, dmr);
        const char *widths[] = { "", "external_perimeter_", "perimeter_", "infill_", "solid_infill_", "top_infill_", "support_material_", "first_layer_", "first_layer_infill_", "skirt_" };
        for (size_t i = 0; i < sizeof(widths) / sizeof(widths[i]); ++ i) {
            std::string key(widths[i]);
            key += "extrusion_width";
            if (cfg.option(key)->get_effective_value(max_nozzle_diameter) > 10. * max_nozzle_diameter)
                return std::string("Invalid extrusion width (too large): ") + key;
        }
    }

    // Out of range validation of numeric values.
    for (const std::string &opt_key : cfg.keys()) {
        const ConfigOption      *opt    = cfg.optptr(opt_key);
        assert(opt != nullptr);
        const ConfigOptionDef   *optdef = PrintConfigDef::instance().get(opt_key);
        assert(optdef != nullptr);

        if (!opt->is_enabled()) {
            // Do not check disabled values
            continue;
        }

        bool out_of_range = false;
        switch (opt->type()) {
        case coFloat:
        case coPercent:
        {
            auto *fopt = static_cast<const ConfigOptionFloat*>(opt);
            out_of_range = fopt->value < optdef->min || fopt->value > optdef->max;
            break;
        }
        case coFloatOrPercent:
        {
            auto *fopt = static_cast<const ConfigOptionFloatOrPercent*>(opt);
            out_of_range = fopt->get_effective_value(1) < optdef->min || fopt->get_effective_value(1) > optdef->max;
            break;
        }
        case coPercents:
        case coFloats:
        {
            const auto* vec = static_cast<const ConfigOptionVector<double>*>(opt);
            for (size_t i = 0; i < vec->size(); ++i) {
                if (!vec->is_enabled(i))
                    continue;
                double v = vec->get_at(i);
                if (v < optdef->min || v > optdef->max) {
                    out_of_range = true;
                    break;
                }
            }
            break;
        }
        case coFloatsOrPercents:
        {
            const auto* vec = static_cast<const ConfigOptionVector<FloatOrPercent>*>(opt);
            for (size_t i = 0; i < vec->size(); ++i) {
                if (!vec->is_enabled(i))
                    continue;
                const FloatOrPercent &v = vec->get_at(i);
                if (v.value < optdef->min || v.value > optdef->max) {
                    out_of_range = true;
                    break;
                }
            }
            break;
        }
        case coInt:
        {
            auto *iopt = static_cast<const ConfigOptionInt*>(opt);
            out_of_range = iopt->value < optdef->min || iopt->value > optdef->max;
            break;
        }
        case coInts:
        {
            const auto* vec = static_cast<const ConfigOptionVector<int32_t>*>(opt);
            for (size_t i = 0; i < vec->size(); ++i) {
                if (!vec->is_enabled(i))
                    continue;
                int v = vec->get_at(i);
                if (v < optdef->min || v > optdef->max) {
                    out_of_range = true;
                    break;
                }
            }
            break;
        }
        default:;
        }
        if (out_of_range)
            return std::string("Value out of range: " + opt_key);
    }

    // The configuration is valid.
    return "";
}

// Declare and initialize static caches of StaticPrintConfig derived classes.
#define PRINT_CONFIG_CACHE_ELEMENT_DEFINITION(r, data, CLASS_NAME) StaticPrintConfig::StaticCache<class Slic3r::CLASS_NAME> BOOST_PP_CAT(CLASS_NAME::s_cache_, CLASS_NAME);
#define PRINT_CONFIG_CACHE_ELEMENT_INITIALIZATION(r, data, CLASS_NAME) Slic3r::CLASS_NAME::initialize_cache();
#define PRINT_CONFIG_CACHE_INITIALIZE(CLASSES_SEQ) \
    BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CACHE_ELEMENT_DEFINITION, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_SEQ)) \
    int fff_print_config_static_initializer() { \
        /* Putting a trace here to avoid the compiler to optimize out this function. */ \
        /*BOOST_LOG_TRIVIAL(trace) << "Initializing StaticPrintConfigs";*/ \
        /* Tamas: alternative solution through a static volatile int. Boost log pollutes stdout and prevents tests from generating clean output */ \
        static volatile int ret = 1; \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CACHE_ELEMENT_INITIALIZATION, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_SEQ)) \
        return ret; \
    }
PRINT_CONFIG_CACHE_INITIALIZE((
    PrintObjectConfig, PrintRegionConfig, MachineEnvelopeConfig, GCodeConfig, PrintConfig, FullPrintConfig))
static int fff_print_config_static_initialized = fff_print_config_static_initializer();

static Points to_points(const std::vector<Vec2d> &dpts)
{
    Points pts;
    pts.reserve(dpts.size());
    for (const Vec2d &v : dpts)
        pts.emplace_back(scale_i(v.x()), scale_i(v.y()));
    return pts;
}

Points get_bed_shape(const PrintConfig &cfg)
{
    return to_points(cfg.bed_shape.get_values());
}

static bool is_XL_printer_notes(const std::string &printer_notes)
{
    return boost::algorithm::contains(printer_notes, "PRINTER_VENDOR_PRUSA3D")
        && boost::algorithm::contains(printer_notes, "PRINTER_MODEL_XL");
}

bool is_XL_printer(const PrintConfig &cfg)
{
    return is_XL_printer_notes(cfg.printer_notes.value);
}



} // namespace Slic3r
