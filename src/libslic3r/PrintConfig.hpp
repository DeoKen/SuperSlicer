///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas, Tomáš Mészáros @tamasmeszaros, Pavel Mikuš @Godrak, David Kocík @kocikdav, Oleksandra Iushchenko @YuSanka, Vojtěch Král @vojtechkral, Enrico Turri @enricoturri1966
///|/ Copyright (c) 2023 Pedro Lamas @PedroLamas
///|/ Copyright (c) 2020 Sergey Kovalev @RandoMan70
///|/ Copyright (c) 2021 Martin Budden
///|/ Copyright (c) 2021 Ilya @xorza
///|/ Copyright (c) 2020 Paul Arden @ardenpm
///|/ Copyright (c) 2019 Spencer Owen @spuder
///|/ Copyright (c) 2019 Stephan Reichhelm @stephanr
///|/ Copyright (c) 2018 Martin Loidl @LoidlM
///|/ Copyright (c) SuperSlicer 2018 Remi Durand @supermerill
///|/ Copyright (c) 2016 - 2017 Joseph Lenox @lordofhyphens
///|/ Copyright (c) Slic3r 2013 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2015 Maksim Derbasov @ntfshard
///|/ Copyright (c) 2015 Alexander Rössler @machinekoder
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_PrintConfig_hpp_
#define slic3r_PrintConfig_hpp_

// Configuration store of Slic3r.
//
// The configuration store is either static or dynamic.
// DynamicPrintConfig is used mainly at the user interface. while the StaticPrintConfig is used
// during the slicing and the g-code generation.
//
// The classes derived from StaticPrintConfig form a following hierarchy.
//
//  class ConfigBase
//    class StaticConfig : public virtual ConfigBase
//        class StaticPrintConfig : public StaticConfig
//            class PrintObjectConfig : public StaticPrintConfig
//            class PrintRegionConfig : public StaticPrintConfig
//            class MachineEnvelopeConfig : public StaticPrintConfig
//            class GCodeConfig : public StaticPrintConfig
//                  class  : public MachineEnvelopeConfig, public GCodeConfig
//                          class FullPrintConfig : PrintObjectConfig,PrintRegionConfig,PrintConfig
//            class SLAPrintObjectConfig : public StaticPrintConfig
//            class SLAMaterialConfig : public StaticPrintConfig
//            class SLAPrinterConfig : public StaticPrintConfig
//                  class SLAFullPrintConfig : public SLAPrinterConfig, public SLAPrintConfig, public SLAPrintObjectConfig, public SLAMaterialConfig
//    class DynamicConfig : public virtual ConfigBase
//        class DynamicPrintConfig : public DynamicConfig
//            class DynamicPrintAndCLIConfig : public DynamicPrintConfig
//
//
#include <unordered_map>

#include <boost/preprocessor/facilities/empty.hpp>
#include <boost/preprocessor/punctuation/comma_if.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/for_each_i.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>

#include "ConfigDef.hpp"
#include "libslic3r.h"

namespace Slic3r {

namespace sla {
enum class SupportTreeType;
enum class PillarConnectionMode;
}

enum PrintHostType {
    htPrusaLink,
    htPrusaConnect,
    htOctoPrint,
    htMoonraker,
    htDuet,
    htFlashAir,
    htAstroBox,
    htRepetier,
    htKlipper,
    htMPMDv2,
    htMKS,
    htMiniDeltaLCD,
};

enum AuthorizationType {
    atKeyPassword, atUserPassword
};

enum class SlicingMode
{
    // Regular, applying ClipperLib::pftNonZero rule when creating ExPolygons.
    Regular,
    // Compatible with 3DLabPrint models, applying ClipperLib::pftEvenOdd rule when creating ExPolygons.
    EvenOdd,
    // Orienting all contours CCW, thus closing all holes.
    CloseHoles,
};

enum SLAMaterial {
    slamTough,
    slamFlex,
    slamCasting,
    slamDental,
    slamHeatResistant,
};
enum ZLiftTop {
    zltAll,
    zltTop,
    zltNotTop
};

#define CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(NAME) \
    template<> const t_config_enum_names& ConfigOptionEnum<NAME>::get_enum_names(); \
    template<> const t_config_enum_values& ConfigOptionEnum<NAME>::get_enum_values();

CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(PrinterTechnology)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(ForwardCompatibilitySubstitutionRule)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(PrintHostType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(AuthorizationType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SlicingMode)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SLAMaterial)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(ZLiftTop)

#undef CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS

class DynamicPrintConfig;

// Defines each and every confiuration option of Slic3r, including the properties of the GUI dialogs.
// Does not store the actual values, but defines default values.
class PrintConfigDef : public ConfigDef
{
public:
    PrintConfigDef();

    // Get print_config_def stored in the singleton
    static const PrintConfigDef& instance();
    static void handle_legacy_map(std::unordered_map<t_config_option_key, std::pair<t_config_option_key, std::string>> &dict, bool remove_unkown_keys = true);
    static void handle_legacy_pair(t_config_option_key &opt_key, std::string &value, bool remove_unkown_keys = true);
    static bool is_defined(const t_config_option_key& opt_key);
    static std::map<std::string, std::string> to_prusa(t_config_option_key& opt_key, std::string& value, const DynamicConfig& all_conf);
    static std::map<std::string, std::string> from_prusa(t_config_option_key& opt_key, std::string& value, const DynamicConfig& all_conf);
    static void handle_legacy_composite(DynamicPrintConfig &config, std::map<t_config_option_key, std::string> &opt_deleted);

    // Array options growing with the number of extruders
    const std::vector<std::string>& extruder_option_keys() const { return m_extruder_option_keys; }
    const std::set<std::string>&    filament_override_option_keys() const { return m_filament_override_option_keys; }
    // Options defining the extruder retract properties. These keys are sorted lexicographically.
    // The extruder retract keys could be overidden by the same values defined at the Filament level
    // (then the key is further prefixed with the "filament_" prefix).
    const std::vector<std::string>& extruder_retract_keys() const { return m_extruder_retract_keys; }
    // Array options growing with the number of milling cutters
    const std::vector<std::string>& milling_option_keys() const { return m_milling_option_keys; }
    const std::set<std::string>&    material_overrides_option_keys() const { return m_material_overrides_option_keys; }

private:
    void init_common_params();
    void init_fff_params();
    void init_extruder_option_keys();
    void init_sla_params();
    void init_sla_support_params(const std::string &method_prefix);
    void init_milling_params();

    std::vector<std::string>    m_extruder_option_keys;
    std::set<std::string>       m_filament_override_option_keys;
    std::vector<std::string>    m_extruder_retract_keys;
    std::vector<std::string>    m_milling_option_keys;
    std::set<std::string>       m_material_overrides_option_keys;

    // The one and only global definition of SLic3r configuration options is created as static in get_mutable()
    // This definition is constant (after initialisation & plugin loaded)
    // only use get_mutable() directly for initilisation, never after
    static PrintConfigDef& instance_mutable();
};

class StaticPrintConfig;

PrinterTechnology printer_technology(const ConfigBase &cfg); //TODO del
OutputFormat output_format(const ConfigBase &cfg);
// Minimum object distance for arrangement, based on printer technology
// double min_object_distance(const ConfigBase &cfg);

// Slic3r dynamic configuration, used to override the configuration
// per object, per modification volume or per printing material.
// The dynamic configuration is also used to store user modifications of the print global parameters,
// so the modified configuration values may be diffed against the active configuration
// to invalidate the proper slicing resp. g-code generation processing steps.
class DynamicPrintConfig : public DynamicConfig
{
public:
    DynamicPrintConfig() {}
    DynamicPrintConfig(const DynamicPrintConfig &rhs) : DynamicConfig(rhs) {}
    DynamicPrintConfig(DynamicPrintConfig &&rhs) noexcept : DynamicConfig(std::move(rhs)) {}
    explicit DynamicPrintConfig(const StaticPrintConfig &rhs);
    explicit DynamicPrintConfig(const ConfigBase &rhs) : DynamicConfig(rhs) {}

    DynamicPrintConfig& operator=(const DynamicPrintConfig &rhs) { DynamicConfig::operator=(rhs); return *this; }
    DynamicPrintConfig& operator=(DynamicPrintConfig &&rhs) noexcept { DynamicConfig::operator=(std::move(rhs)); return *this; }

    static DynamicPrintConfig  full_print_config();
    static DynamicPrintConfig  full_print_config_with(const t_config_option_key &opt_key, const std::string &str, bool append = false) {
        auto config = DynamicPrintConfig::full_print_config();
        config.set_deserialize_strict(opt_key, str, append);
        return config;
    }
    static DynamicPrintConfig  full_print_config_with(std::initializer_list<SetDeserializeItem> items) {
        auto config = DynamicPrintConfig::full_print_config();
        config.set_deserialize_strict(items);
        return config;
    }
    static DynamicPrintConfig  new_with(const t_config_option_key &opt_key, const std::string &str, bool append = false) {
        DynamicPrintConfig config;
        config.set_deserialize_strict(opt_key, str, append);
        return config;
    }
    static DynamicPrintConfig  new_with(std::initializer_list<SetDeserializeItem> items) {
        DynamicPrintConfig config;
        config.set_deserialize_strict(items);
        return config;
    }
    static DynamicPrintConfig* new_from_defaults_keys(const std::vector<std::string> &keys);

    // Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here.
    const ConfigDef*    def() const override { return &PrintConfigDef::instance(); }

    void                normalize_fdm();

    void                set_num_extruders(unsigned int num_extruders);

    void                set_num_milling(unsigned int num_milling);

    // Validate the PrintConfig. Returns an empty string on success, otherwise an error message is returned.
    std::string         validate();


#ifdef _DEBUGINFO
    // Verify whether the opt_key has not been obsoleted or renamed.
    // Both opt_key and value may be modified by handle_legacy().
    // If the opt_key is no more valid in this version of Slic3r, opt_key is cleared by handle_legacy().
    // handle_legacy() is called internally by set_deserialize().
    void                handle_legacy(t_config_option_key &opt_key, std::string &value) const override {
        PrintConfigDef::handle_legacy_pair(opt_key, value);
    }
#endif
    // Called after a config is loaded as a whole.
    // Perform composite conversions, for example merging multiple keys into one key.
    // For conversion of single options, the handle_legacy() method above is called.
    void                handle_legacy_composite(std::map<t_config_option_key, std::string> &opt_deleted) override
        { PrintConfigDef::handle_legacy_composite(*this, opt_deleted); }
    void                to_prusa(t_config_option_key& opt_key, std::string& value) const override
        { PrintConfigDef::to_prusa(opt_key, value, *this); }
    // utilities to help convert from prusa config.
    // if with_phony, then the phony settigns will be set to phony if needed.
    void                convert_from_prusa(bool with_phony);

    /// <summary>
    /// callback to changed other settings that are linked (like width & spacing)
    /// </summary>
    /// <param name="opt_key">name of the changed option</param>
    /// <return> configs that have at least a change</param>
    const DynamicPrintConfig* value_changed(const t_config_option_key& opt_key, const std::vector<const DynamicPrintConfig*> config_collection);
    const DynamicPrintConfig* update_phony(const std::vector<const DynamicPrintConfig*> config_collection, bool exclude_default_extrusion = false);
};

// An indirection to a bunch of Config
class MultiPtrPrintConfig : public virtual ConfigBase
{
public:
    MultiPtrPrintConfig() = default;
    
    // Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here.
    const ConfigDef*    def() const override { return &PrintConfigDef::instance(); }

    // Overrides ConfigResolver::optptr().
    const ConfigOption*     optptr(const t_config_option_key &opt_key) const override;
    // Overrides ConfigBase::optptr(). Find ando/or create a ConfigOption instance for a given name.
    ConfigOption*           optptr(const t_config_option_key &opt_key, bool create = false) override;
    // Overrides ConfigBase::keys(). Collect names of all configuration values maintained by this configuration store.
    t_config_option_keys    keys() const override;

    
    std::vector<ConfigBase*> storages;
private:
};

void handle_legacy_sla(DynamicPrintConfig& config);

class StaticPrintConfig : public StaticConfig
{
public:
    StaticPrintConfig() {}

    // Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here.
    const ConfigDef*    def() const override { return &PrintConfigDef::instance(); }
    // Reference to the cached list of keys.
    virtual const t_config_option_keys& keys_ref() const = 0;

protected:
#ifdef _DEBUGINFO
    // Verify whether the opt_key has not been obsoleted or renamed.
    // Both opt_key and value may be modified by handle_legacy().
    // If the opt_key is no more valid in this version of Slic3r, opt_key is cleared by handle_legacy().
    // handle_legacy() is called internally by set_deserialize().
    void                handle_legacy(t_config_option_key &opt_key, std::string &value) const override
        { PrintConfigDef::handle_legacy_pair(opt_key, value); }
#endif

    // Internal class for keeping a dynamic map to static options.
    class StaticCacheBase
    {
    public:
        // To be called during the StaticCache setup.
        // Add one ConfigOption into m_map_name_to_offset.
        template<typename T>
        void                opt_add(const std::string &name, const char *base_ptr, const T &opt)
        {
            assert(m_map_name_to_offset.find(name) == m_map_name_to_offset.end());
            m_map_name_to_offset[name] = (const char*)&opt - base_ptr;
        }

    protected:
        std::map<std::string, ptrdiff_t>    m_map_name_to_offset;
    };

    // Parametrized by the type of the topmost class owning the options.
    template<typename T>
    class StaticCache : public StaticCacheBase
    {
    public:
        // Calling the constructor of m_defaults with 0 forces m_defaults to not run the initialization.
        StaticCache() : m_defaults(nullptr) {}
        ~StaticCache() { delete m_defaults; m_defaults = nullptr; }

        bool                initialized() const { return ! m_keys.empty(); }

        ConfigOption*       optptr(const std::string &name, T *owner) const
        {
            const auto it = m_map_name_to_offset.find(name);
            return (it == m_map_name_to_offset.end()) ? nullptr : reinterpret_cast<ConfigOption*>((char*)owner + it->second);
        }

        const ConfigOption* optptr(const std::string &name, const T *owner) const
        {
            const auto it = m_map_name_to_offset.find(name);
            return (it == m_map_name_to_offset.end()) ? nullptr : reinterpret_cast<const ConfigOption*>((const char*)owner + it->second);
        }

        const std::vector<std::string>& keys()      const { return m_keys; }
        const T&                        defaults()  const { return *m_defaults; }

        // To be called during the StaticCache setup.
        // Collect option keys from m_map_name_to_offset,
        // assign default values to m_defaults.
        void                finalize(T *defaults, const ConfigDef *defs)
        {
            assert(defaults != nullptr);
            assert(defs != nullptr);
            m_defaults = defaults;
            m_keys.clear();
            m_keys.reserve(m_map_name_to_offset.size());
            for (const auto &kvp : defs->options) {
                // Find the option given the option name kvp.first by an offset from (char*)m_defaults.
                ConfigOption *opt = this->optptr(kvp.first, m_defaults);
                if (opt == nullptr)
                    // This option is not defined by the ConfigBase of type T.
                    continue;
                m_keys.emplace_back(kvp.first);
                const ConfigOptionDef *def = defs->get(kvp.first);
                assert(def != nullptr);
                if (def->default_value)
                    opt->set(*def->default_value);
            }
        }

    private:
        T                                  *m_defaults;
        std::vector<std::string>            m_keys;
    };
};

#define STATIC_PRINT_CONFIG_CACHE_BASE(CLASS_NAME) \
public: \
    /* Overrides ConfigBase::optptr(). Find ando/or create a ConfigOption instance for a given name. */ \
    const ConfigOption*      optptr(const t_config_option_key &opt_key) const override \
        {   const ConfigOption* opt = s_cache_##CLASS_NAME.optptr(opt_key, this); \
            if (opt == nullptr && parent != nullptr) \
                /*if not find, try with the parent config.*/ \
                opt = parent->option(opt_key); \
            return opt; \
        } \
    /* Overrides ConfigBase::optptr(). Find ando/or create a ConfigOption instance for a given name. */ \
    ConfigOption*            optptr(const t_config_option_key &opt_key, bool create = false) override \
        { return s_cache_##CLASS_NAME.optptr(opt_key, this); } \
    /* Overrides ConfigBase::keys(). Collect names of all configuration values maintained by this configuration store. */ \
    t_config_option_keys     keys() const override { return s_cache_##CLASS_NAME.keys(); } \
    const t_config_option_keys& keys_ref() const override { return s_cache_##CLASS_NAME.keys(); } \
    static const CLASS_NAME& defaults() { assert(s_cache_##CLASS_NAME.initialized()); return s_cache_##CLASS_NAME.defaults(); } \
private: \
    friend int print_config_static_initializer(); \
    friend int fff_print_config_static_initializer(); \
    friend int sla_print_config_static_initializer(); \
    static void initialize_cache() \
    { \
        assert(! s_cache_##CLASS_NAME.initialized()); \
        if (! s_cache_##CLASS_NAME.initialized()) { \
            CLASS_NAME *inst = new CLASS_NAME(1); \
            inst->initialize(s_cache_##CLASS_NAME, (const char*)inst); \
            s_cache_##CLASS_NAME.finalize(inst, inst->def()); \
        } \
    } \
    /* Cache object holding a key/option map, a list of option keys and a copy of this static config initialized with the defaults. */ \
    static StaticPrintConfig::StaticCache<CLASS_NAME> s_cache_##CLASS_NAME;

#define STATIC_PRINT_CONFIG_CACHE(CLASS_NAME) \
    STATIC_PRINT_CONFIG_CACHE_BASE(CLASS_NAME) \
public: \
    /* Public default constructor will initialize the key/option cache and the default object copy if needed. */ \
    CLASS_NAME() { assert(s_cache_##CLASS_NAME.initialized()); *this = s_cache_##CLASS_NAME.defaults(); } \
protected: \
    /* Protected constructor to be called when compounded. */ \
    CLASS_NAME(int) {}

#define STATIC_PRINT_CONFIG_CACHE_DERIVED(CLASS_NAME) \
    STATIC_PRINT_CONFIG_CACHE_BASE(CLASS_NAME) \
public: \
    /* Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here. */ \
    const ConfigDef*    def() const override { return &PrintConfigDef::instance(); }

#define PRINT_CONFIG_CLASS_ELEMENT_DEFINITION(r, data, elem) BOOST_PP_TUPLE_ELEM(0, elem) BOOST_PP_TUPLE_ELEM(1, elem);
#define PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION2(KEY) cache.opt_add(BOOST_PP_STRINGIZE(KEY), base_ptr, this->KEY);
#define PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION(r, data, elem) PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION2(BOOST_PP_TUPLE_ELEM(1, elem))
#define PRINT_CONFIG_CLASS_ELEMENT_HASH(r, data, elem) boost::hash_combine(seed, BOOST_PP_TUPLE_ELEM(1, elem).hash());
#define PRINT_CONFIG_CLASS_ELEMENT_EQUAL(r, data, elem) if (! (BOOST_PP_TUPLE_ELEM(1, elem) == rhs.BOOST_PP_TUPLE_ELEM(1, elem))) return false;
#define PRINT_CONFIG_CLASS_ELEMENT_LOWER(r, data, elem) \
        if (BOOST_PP_TUPLE_ELEM(1, elem) < rhs.BOOST_PP_TUPLE_ELEM(1, elem)) return true; \
        if (! (BOOST_PP_TUPLE_ELEM(1, elem) == rhs.BOOST_PP_TUPLE_ELEM(1, elem))) return false;

#define PRINT_CONFIG_CLASS_DEFINE(CLASS_NAME, PARAMETER_DEFINITION_SEQ) \
class CLASS_NAME : public StaticPrintConfig { \
    STATIC_PRINT_CONFIG_CACHE(CLASS_NAME) \
public: \
    BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_DEFINITION, _, PARAMETER_DEFINITION_SEQ) \
    size_t hash() const throw() \
    { \
        size_t seed = 0; \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_HASH, _, PARAMETER_DEFINITION_SEQ) \
        return seed; \
    } \
    bool operator==(const CLASS_NAME &rhs) const throw() \
    { \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_EQUAL, _, PARAMETER_DEFINITION_SEQ) \
        return true; \
    } \
    bool operator!=(const CLASS_NAME &rhs) const throw() { return ! (*this == rhs); } \
    bool operator<(const CLASS_NAME &rhs) const throw() \
    { \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_LOWER, _, PARAMETER_DEFINITION_SEQ) \
        return false; \
    } \
protected: \
    void initialize(StaticCacheBase &cache, const char *base_ptr) \
    { \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION, _, PARAMETER_DEFINITION_SEQ) \
    } \
};

#define PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST_ITEM(r, data, i, elem) BOOST_PP_COMMA_IF(i) public elem
#define PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST(CLASSES_PARENTS_TUPLE) BOOST_PP_SEQ_FOR_EACH_I(PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST_ITEM, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE))
#define PRINT_CONFIG_CLASS_DERIVED_INITIALIZER_ITEM(r, VALUE, i, elem) BOOST_PP_COMMA_IF(i) elem(VALUE)
#define PRINT_CONFIG_CLASS_DERIVED_INITIALIZER(CLASSES_PARENTS_TUPLE, VALUE) BOOST_PP_SEQ_FOR_EACH_I(PRINT_CONFIG_CLASS_DERIVED_INITIALIZER_ITEM, VALUE, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE))
#define PRINT_CONFIG_CLASS_DERIVED_INITCACHE_ITEM(r, data, elem) this->elem::initialize(cache, base_ptr);
#define PRINT_CONFIG_CLASS_DERIVED_INITCACHE(CLASSES_PARENTS_TUPLE) BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_DERIVED_INITCACHE_ITEM, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE))
#define PRINT_CONFIG_CLASS_DERIVED_HASH(r, data, elem) boost::hash_combine(seed, static_cast<const elem*>(this)->hash());
#define PRINT_CONFIG_CLASS_DERIVED_EQUAL(r, data, elem) \
    if (! (*static_cast<const elem*>(this) == static_cast<const elem&>(rhs))) return false;

// Generic version, with or without new parameters. Don't use this directly.
#define PRINT_CONFIG_CLASS_DERIVED_DEFINE1(CLASS_NAME, CLASSES_PARENTS_TUPLE, PARAMETER_DEFINITION, PARAMETER_REGISTRATION, PARAMETER_HASHES, PARAMETER_EQUALS) \
class CLASS_NAME : PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST(CLASSES_PARENTS_TUPLE) { \
    STATIC_PRINT_CONFIG_CACHE_DERIVED(CLASS_NAME) \
    CLASS_NAME() : PRINT_CONFIG_CLASS_DERIVED_INITIALIZER(CLASSES_PARENTS_TUPLE, 0) { assert(s_cache_##CLASS_NAME.initialized()); *this = s_cache_##CLASS_NAME.defaults(); } \
public: \
    PARAMETER_DEFINITION \
    size_t hash() const throw() \
    { \
        size_t seed = 0; \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_DERIVED_HASH, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE)) \
        PARAMETER_HASHES \
        return seed; \
    } \
    bool operator==(const CLASS_NAME &rhs) const throw() \
    { \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_DERIVED_EQUAL, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE)) \
        PARAMETER_EQUALS \
        return true; \
    } \
    bool operator!=(const CLASS_NAME &rhs) const throw() { return ! (*this == rhs); } \
protected: \
    CLASS_NAME(int) : PRINT_CONFIG_CLASS_DERIVED_INITIALIZER(CLASSES_PARENTS_TUPLE, 1) {} \
    void initialize(StaticCacheBase &cache, const char* base_ptr) { \
        PRINT_CONFIG_CLASS_DERIVED_INITCACHE(CLASSES_PARENTS_TUPLE) \
        PARAMETER_REGISTRATION \
    } \
};
// Variant without adding new parameters.
#define PRINT_CONFIG_CLASS_DERIVED_DEFINE0(CLASS_NAME, CLASSES_PARENTS_TUPLE) \
    PRINT_CONFIG_CLASS_DERIVED_DEFINE1(CLASS_NAME, CLASSES_PARENTS_TUPLE, BOOST_PP_EMPTY(), BOOST_PP_EMPTY(), BOOST_PP_EMPTY(), BOOST_PP_EMPTY())
// Variant with adding new parameters.
#define PRINT_CONFIG_CLASS_DERIVED_DEFINE(CLASS_NAME, CLASSES_PARENTS_TUPLE, PARAMETER_DEFINITION_SEQ) \
    PRINT_CONFIG_CLASS_DERIVED_DEFINE1(CLASS_NAME, CLASSES_PARENTS_TUPLE, \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_DEFINITION, _, PARAMETER_DEFINITION_SEQ), \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION, _, PARAMETER_DEFINITION_SEQ), \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_HASH, _, PARAMETER_DEFINITION_SEQ), \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_EQUAL, _, PARAMETER_DEFINITION_SEQ))

class CLIActionsConfigDef : public ConfigDef
{
public:
    CLIActionsConfigDef();
};

class CLITransformConfigDef : public ConfigDef
{
public:
    CLITransformConfigDef();
};

class CLIMiscConfigDef : public ConfigDef
{
public:
    CLIMiscConfigDef();
};

typedef std::string t_custom_gcode_key;
// This map containes list of specific placeholders for each custom G-code, if any exist
const std::map<t_custom_gcode_key, t_config_option_keys>& custom_gcode_specific_placeholders();

// Next classes define placeholders used by GUI::EditGCodeDialog.

class ReadOnlySlicingStatesConfigDef : public ConfigDef
{
public:
    ReadOnlySlicingStatesConfigDef();
};

class ReadWriteSlicingStatesConfigDef : public ConfigDef
{
public:
    ReadWriteSlicingStatesConfigDef();
};

class OtherSlicingStatesConfigDef : public ConfigDef
{
public:
    OtherSlicingStatesConfigDef();
};

class PrintStatisticsConfigDef : public ConfigDef
{
public:
    PrintStatisticsConfigDef();
};

class ObjectsInfoConfigDef : public ConfigDef
{
public:
    ObjectsInfoConfigDef();
};

class DimensionsConfigDef : public ConfigDef
{
public:
    DimensionsConfigDef();
};

class TimestampsConfigDef : public ConfigDef
{
public:
    TimestampsConfigDef();
};

class OtherPresetsConfigDef : public ConfigDef
{
public:
    OtherPresetsConfigDef();
};

// This classes defines all custom G-code specific placeholders.
class CustomGcodeSpecificConfigDef : public ConfigDef
{
public:
    CustomGcodeSpecificConfigDef();
};
extern const CustomGcodeSpecificConfigDef    custom_gcode_specific_config_def;

// This class defines the command line options representing actions.
extern const CLIActionsConfigDef    cli_actions_config_def;

// This class defines the command line options representing transforms.
extern const CLITransformConfigDef  cli_transform_config_def;

// This class defines all command line options that are not actions or transforms.
extern const CLIMiscConfigDef       cli_misc_config_def;

class DynamicPrintAndCLIConfig : public DynamicPrintConfig
{
public:
    DynamicPrintAndCLIConfig() {}
    DynamicPrintAndCLIConfig(const DynamicPrintAndCLIConfig &other) : DynamicPrintConfig(other) {}

    // Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here.
    const ConfigDef*        def() const override { return &s_def; }

#ifdef _DEBUGINFO
    // Verify whether the opt_key has not been obsoleted or renamed.
    // Both opt_key and value may be modified by handle_legacy().
    // If the opt_key is no more valid in this version of Slic3r, opt_key is cleared by handle_legacy().
    // handle_legacy() is called internally by set_deserialize().
    void                    handle_legacy(t_config_option_key &opt_key, std::string &value) const override;
#endif
private:
    class PrintAndCLIConfigDef : public ConfigDef
    {
    public:
        PrintAndCLIConfigDef() {
            this->options.insert(PrintConfigDef::instance().options.begin(), PrintConfigDef::instance().options.end());
            this->options.insert(cli_actions_config_def.options.begin(), cli_actions_config_def.options.end());
            this->options.insert(cli_transform_config_def.options.begin(), cli_transform_config_def.options.end());
            this->options.insert(cli_misc_config_def.options.begin(), cli_misc_config_def.options.end());
            for (const auto &kvp : this->options)
                this->by_serialization_key_ordinal[kvp.second.serialization_key_ordinal] = &kvp.second;
        }
        // Do not release the default values, they are handled by print_config_def & cli_actions_config_def / cli_transform_config_def / cli_misc_config_def.
        ~PrintAndCLIConfigDef() { this->options.clear(); }
    };
    static PrintAndCLIConfigDef s_def;
};

bool is_XL_printer(const DynamicPrintConfig &cfg);

Points get_bed_shape(const DynamicPrintConfig &cfg);

// ModelConfig is a wrapper around DynamicPrintConfig with an addition of a timestamp.
// Each change of ModelConfig is tracked by assigning a new timestamp from a global counter.
// The counter is used for faster synchronization of the background slicing thread
// with the front end by skipping synchronization of equal config dictionaries.
// The global counter is also used for avoiding unnecessary serialization of config
// dictionaries when taking an Undo snapshot.
//
// The global counter is NOT thread safe, therefore it is recommended to use ModelConfig from
// the main thread only.
//
// As there is a global counter and it is being increased with each change to any ModelConfig,
// if two ModelConfig dictionaries differ, they should differ with their timestamp as well.
// Therefore copying the ModelConfig including its timestamp is safe as there is no harm
// in having multiple ModelConfig with equal timestamps as long as their dictionaries are equal.
//
// The timestamp is used by the Undo/Redo stack. As zero timestamp means invalid timestamp
// to the Undo/Redo stack (zero timestamp means the Undo/Redo stack needs to serialize and
// compare serialized data for differences), zero timestamp shall never be used.
// Timestamp==1 shall only be used for empty dictionaries.
class ModelConfig
{
public:
    // Following method clears the config and increases its timestamp, so the deleted
    // state is considered changed from perspective of the undo/redo stack.
    void         reset() { m_data.clear(); touch(); }

    void         assign_config(const ModelConfig &rhs) {
        if (m_timestamp != rhs.m_timestamp) {
            m_data      = rhs.m_data;
            m_timestamp = rhs.m_timestamp;
        }
    }
    void         assign_config(ModelConfig &&rhs) {
        if (m_timestamp != rhs.m_timestamp) {
            m_data      = std::move(rhs.m_data);
            m_timestamp = rhs.m_timestamp;
            rhs.reset();
        }
    }

    // Modification of the ModelConfig is not thread safe due to the global timestamp counter!
    // Don't call modification methods from the back-end!
    // Assign methods don't assign if src==dst to not having to bump the timestamp in case they are equal.
    void         assign_config(const DynamicPrintConfig &rhs)  { if (m_data != rhs) { m_data = rhs; this->touch(); } }
    void         assign_config(DynamicPrintConfig &&rhs)       { if (m_data != rhs) { m_data = std::move(rhs); this->touch(); } }
    void         apply(const ModelConfig &other, bool ignore_nonexistent = false) { this->apply(other.get(), ignore_nonexistent); }
    void         apply(const ConfigBase &other, bool ignore_nonexistent = false) { m_data.apply_only(other, other.keys(), ignore_nonexistent); this->touch(); }
    void         apply_only(const ModelConfig &other, const t_config_option_keys &keys, bool ignore_nonexistent = false) { this->apply_only(other.get(), keys, ignore_nonexistent); }
    void         apply_only(const ConfigBase &other, const t_config_option_keys &keys, bool ignore_nonexistent = false) { m_data.apply_only(other, keys, ignore_nonexistent); this->touch(); }
    bool         set_key_value(const std::string &opt_key, ConfigOption *opt) { bool out = m_data.set_key_value(opt_key, opt); this->touch(); return out; }
    template<typename T>
    void         set(const std::string &opt_key, T value) { m_data.set(opt_key, value, true); this->touch(); }
    void set_any(const std::string &opt_key, bool enable, boost::any value, int16_t extruder_id)
    {
        ConfigOption *opt = m_data.option(opt_key, true);
        assert(opt);
        if (opt) {
            opt->set_any(value, extruder_id);
            opt->set_enabled(enable, extruder_id);
            this->touch();
        }
    }
    void         set_deserialize(const t_config_option_key &opt_key, const std::string &str, ConfigSubstitutionContext &substitution_context, bool append = false)
        { m_data.set_deserialize(opt_key, str, substitution_context, append); this->touch(); }
    void         set_deserialize_strict(const t_config_option_key &opt_key, const std::string &str, bool append = false)
        { m_data.set_deserialize_strict(opt_key, str, append); this->touch(); }
    bool         erase(const t_config_option_key &opt_key) { bool out = m_data.erase(opt_key); if (out) this->touch(); return out; }

    // Getters are thread safe.
    // The following implicit conversion breaks the Cereal serialization.
//    operator const DynamicPrintConfig&() const throw() { return this->get(); }
    const DynamicPrintConfig&   get() const throw() { return m_data; }
    bool                        empty() const throw() { return m_data.empty(); }
    size_t                      size() const throw() { return m_data.size(); }
    auto                        cbegin() const { return m_data.cbegin(); }
    auto                        cend() const { return m_data.cend(); }
    t_config_option_keys        keys() const { return m_data.keys(); }
    bool                        has(const t_config_option_key& opt_key) const { return m_data.has(opt_key); }
    const ConfigDef*            def() const { return m_data.def(); }
    bool                        operator==(const ModelConfig& other) const { return m_data.equals(other.m_data); }
    bool                        operator!=(const ModelConfig& other) const { return !this->operator==(other); }
    const ConfigOption*         option(const t_config_option_key &opt_key) const { return m_data.option(opt_key); }
    int                         opt_int(const t_config_option_key &opt_key) const { return m_data.opt_int(opt_key); }
    int                         extruder() const { return opt_int("extruder"); }
    int                         first_layer_extruder() const { return opt_int("first_layer_extruder"); }
    double                      opt_float(const t_config_option_key &opt_key) const { return m_data.opt_float(opt_key); }
    std::string                 opt_serialize(const t_config_option_key &opt_key) const { return m_data.opt_serialize(opt_key); }

    // Return an optional timestamp of this object.
    // If the timestamp returned is non-zero, then the serialization framework will
    // only save this object on the Undo/Redo stack if the timestamp is different
    // from the timestmap of the object at the top of the Undo / Redo stack.
    virtual uint64_t    timestamp() const throw() { return m_timestamp; }
    bool                timestamp_matches(const ModelConfig &rhs) const throw() { return m_timestamp == rhs.m_timestamp; }
    // Not thread safe! Should not be called from other than the main thread!
    void                touch() { m_timestamp = ++ s_last_timestamp; }


    // utilities to help convert from prusa config.
    // if with_phony, then the phony settings will be set to phony if needed.
    void convert_from_prusa(const DynamicPrintConfig& global_config, bool with_phony);
    void handle_legacy_composite(std::map<t_config_option_key, std::string> &opt_deleted)
        { PrintConfigDef::handle_legacy_composite(m_data, opt_deleted); }

private:
    friend class cereal::access;
    template<class Archive> void serialize(Archive& ar) { ar(m_timestamp); ar(m_data); }

    uint64_t                    m_timestamp { 1 };
    DynamicPrintConfig          m_data;

    static uint64_t             s_last_timestamp;
};

void deserialize_maybe_from_prusa(std::map<t_config_option_key, std::string> settings,
                                  ModelConfig &                              config,
                                  const DynamicPrintConfig &                 global_config,
                                  ConfigSubstitutionContext &                config_substitutions,
                                  bool                                       with_phony,
                                  bool                                       check_prusa);
void deserialize_maybe_from_prusa(std::map<t_config_option_key, std::string> settings,
                                  DynamicPrintConfig &                       config,
                                  ConfigSubstitutionContext &                config_substitutions,
                                  bool                                       with_phony,
                                  bool                                       check_prusa);


} // namespace Slic3r

// Serialization through the Cereal library
namespace cereal {
    // Let cereal know that there are load / save non-member functions declared for DynamicPrintConfig, ignore serialize / load / save from parent class DynamicConfig.
    template <class Archive> struct specialize<Archive, Slic3r::DynamicPrintConfig, cereal::specialization::non_member_load_save> {};

    template<class Archive> void load(Archive& archive, Slic3r::DynamicPrintConfig &config)
    {
        size_t cnt;
        archive(cnt);
        config.clear();
        for (size_t i = 0; i < cnt; ++ i) {
            size_t serialization_key_ordinal;
            archive(serialization_key_ordinal);
            assert(serialization_key_ordinal > 0);
            auto it = Slic3r::PrintConfigDef::instance().by_serialization_key_ordinal.find(serialization_key_ordinal);
            assert(it != Slic3r::PrintConfigDef::instance().by_serialization_key_ordinal.end());
            config.set_key_value(it->second->opt_key, it->second->load_option_from_archive(archive));
        }
    }

    template<class Archive> void save(Archive& archive, const Slic3r::DynamicPrintConfig &config)
    {
        size_t cnt = config.size();
        archive(cnt);
        for (auto it = config.cbegin(); it != config.cend(); ++it) {
            const Slic3r::ConfigOptionDef* optdef = Slic3r::PrintConfigDef::instance().get(it->first);
            assert(optdef != nullptr);
            assert(optdef->serialization_key_ordinal > 0);
            archive(optdef->serialization_key_ordinal);
            optdef->save_option_to_archive(archive, it->second.get());
        }
    }
}

#endif
