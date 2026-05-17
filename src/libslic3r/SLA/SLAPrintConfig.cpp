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

#include "SLA/SLAPrintConfig.hpp"

#include <algorithm>

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

CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SLADisplayOrientation, {
    { "landscape",      sladoLandscape},
    { "portrait",       sladoPortrait}
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SLAPillarConnectionMode, {
    {"zigzag",          int(SLAPillarConnectionMode::zigzag)},
    {"cross",           int(SLAPillarConnectionMode::cross)},
    {"dynamic",         int(SLAPillarConnectionMode::dynamic)}
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SLAMaterialSpeed, {
    {"slow",            slamsSlow},
    {"fast",            slamsFast},
    {"high_viscosity",  slamsHighViscosity}
})
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SLASupportTreeType, {
    {"default", int(sla::SupportTreeType::Default)},
    {"branching",   int(sla::SupportTreeType::Branching)},
    //TODO: {"organic", int(sla::SupportTreeType::Organic)}
})

// Declare and initialize static caches of StaticPrintConfig derived classes.
#define PRINT_CONFIG_CACHE_ELEMENT_DEFINITION(r, data, CLASS_NAME) StaticPrintConfig::StaticCache<class Slic3r::CLASS_NAME> BOOST_PP_CAT(CLASS_NAME::s_cache_, CLASS_NAME);
#define PRINT_CONFIG_CACHE_ELEMENT_INITIALIZATION(r, data, CLASS_NAME) Slic3r::CLASS_NAME::initialize_cache();
#define PRINT_CONFIG_CACHE_INITIALIZE(CLASSES_SEQ) \
    BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CACHE_ELEMENT_DEFINITION, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_SEQ)) \
    int sla_print_config_static_initializer() { \
        /* Putting a trace here to avoid the compiler to optimize out this function. */ \
        /*BOOST_LOG_TRIVIAL(trace) << "Initializing StaticPrintConfigs";*/ \
        /* Tamas: alternative solution through a static volatile int. Boost log pollutes stdout and prevents tests from generating clean output */ \
        static volatile int ret = 1; \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CACHE_ELEMENT_INITIALIZATION, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_SEQ)) \
        return ret; \
    }
PRINT_CONFIG_CACHE_INITIALIZE((
    SLAMaterialConfig, SLAPrintConfig, SLAPrintObjectConfig, SLAPrinterConfig, SLAFullPrintConfig))
static int sla_print_config_static_initialized = sla_print_config_static_initializer();

static Points to_points(const std::vector<Vec2d> &dpts)
{
    Points pts;
    pts.reserve(dpts.size());
    for (const Vec2d &v : dpts)
        pts.emplace_back(scale_i(v.x()), scale_i(v.y()));
    return pts;
}

Points get_bed_shape(const SLAPrinterConfig &cfg) { return to_points(cfg.bed_shape.get_values()); }

std::string get_sla_suptree_prefix(const DynamicPrintConfig &config)
{
    const auto *suptreetype = config.option<ConfigOptionEnum<sla::SupportTreeType>>("support_tree_type");
    std::string slatree = "";
    if (suptreetype) {
        auto ttype = static_cast<sla::SupportTreeType>(suptreetype->get_int());
        switch (ttype) {
        case sla::SupportTreeType::Branching: slatree = "branching"; break;
        case sla::SupportTreeType::Organic: slatree = "organic"; break;
        default:
            ;
        }
    }

    return slatree;
}

} // namespace Slic3r
