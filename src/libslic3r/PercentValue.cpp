#include "PercentValue.hpp"

#include <boost/algorithm/string/predicate.hpp>

#include "PrintConfig.hpp"

namespace Slic3r {

// The value and percent flag of a setting taking a value or a percentage (the first value of a vector).
static std::optional<std::pair<double, bool>> value_or_percent(const ConfigOption *opt)
{
    if (opt == nullptr)
        return std::nullopt;
    if (opt->type() == coFloatOrPercent) {
        const auto *v = static_cast<const ConfigOptionFloatOrPercent*>(opt);
        return std::make_pair(v->value, v->percent);
    }
    if (opt->type() == coFloatsOrPercents && ! static_cast<const ConfigOptionFloatsOrPercents*>(opt)->empty()) {
        const FloatOrPercent &v = static_cast<const ConfigOptionFloatsOrPercents*>(opt)->get_at(0);
        return std::make_pair(v.value, v.percent);
    }
    return std::nullopt;
}

static std::optional<double> absolute_value(const DynamicPrintConfig &config, const std::string &key, int depth);

static std::optional<double> base_value(const DynamicPrintConfig &config, const std::string &key, int depth)
{
    std::string base_key;
    if (boost::ends_with(key, "extrusion_width"))
        // Flow::new_from_config_width(): a percentage of the layer height.
        base_key = key == "first_layer_extrusion_width" ? "first_layer_height" : "layer_height";
    else if (const ConfigOptionDef *def = print_config_def.get(key); def != nullptr)
        base_key = def->ratio_over;
    return base_key.empty() ? std::nullopt : absolute_value(config, base_key, depth + 1);
}

// Any number setting as an absolute value (a percentage resolved over its base), std::nullopt if unknown.
static std::optional<double> absolute_value(const DynamicPrintConfig &config, const std::string &key, int depth)
{
    if (depth > 10)
        return std::nullopt;
    const ConfigOption *opt = config.option(key);
    if (opt == nullptr)
        return std::nullopt;
    switch (opt->type()) {
    case coFloat:  return static_cast<const ConfigOptionFloat*>(opt)->value;
    case coInt:    return double(static_cast<const ConfigOptionInt*>(opt)->value);
    case coFloats: {
        const auto *v = static_cast<const ConfigOptionFloats*>(opt);
        return v->empty() ? std::nullopt : std::optional<double>(v->get_at(0));
    }
    default: break;
    }
    const std::optional<std::pair<double, bool>> v = value_or_percent(opt);
    if (! v)
        return std::nullopt;
    if (! v->second)
        return v->first;
    const std::optional<double> base = base_value(config, key, depth);
    return base ? std::optional<double>(v->first * *base / 100.) : std::nullopt;
}

std::optional<double> percent_base_value(const DynamicPrintConfig &config, const std::string &opt_key)
{
    return base_value(config, opt_key, 0);
}

std::optional<PercentValueHint> percent_value_hint(const DynamicPrintConfig &config, const std::string &opt_key)
{
    const std::optional<std::pair<double, bool>> v = value_or_percent(config.option(opt_key));
    if (! v || (! v->second && v->first == 0.))
        return std::nullopt;
    const std::optional<double> base = percent_base_value(config, opt_key);
    if (! base || *base <= 0.)
        return std::nullopt;
    return v->second ? PercentValueHint{ v->first * *base / 100., false } : PercentValueHint{ v->first / *base * 100., true };
}

std::optional<double> percent_absolute_value(const DynamicPrintConfig &config, const std::string &opt_key)
{
    const std::optional<PercentValueHint> hint = percent_value_hint(config, opt_key);
    return hint && ! hint->percent ? std::optional<double>(hint->value) : std::nullopt;
}

std::string percent_value_unit(const ConfigOptionDef &def)
{
    const size_t pos = def.sidetext.find(" or ");
    return pos == std::string::npos ? std::string() : def.sidetext.substr(0, pos);
}

} // namespace Slic3r
