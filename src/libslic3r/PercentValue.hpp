#ifndef slic3r_PercentValue_hpp_
#define slic3r_PercentValue_hpp_

// Settings given as a value or a percentage (coFloatOrPercent, "mm or %"): the other form of the value, as the slicer
// computes it, shown next to the field in the settings tabs: the absolute value of a percentage, the percentage of an
// absolute value.

#include <optional>
#include <string>

namespace Slic3r {

class ConfigOptionDef;
class DynamicPrintConfig;

// The value a percentage of opt_key is computed over (the first value of a vector), std::nullopt if unknown. Extrusion
// widths: the layer height (the first layer width: the first layer height), as Flow computes them; the others: their
// ratio_over, following chains of percentages; "nozzle_diameter": the first extruder.
std::optional<double> percent_base_value(const DynamicPrintConfig &config, const std::string &opt_key);

// The other form of the value of opt_key: for a percentage its absolute value (percent = false), for an absolute value its
// percentage of the base (percent = true). std::nullopt if the base is unknown or 0, or for an absolute 0 (auto / disabled).
struct PercentValueHint { double value; bool percent; };
std::optional<PercentValueHint> percent_value_hint(const DynamicPrintConfig &config, const std::string &opt_key);

// The absolute value of opt_key when it is a percentage, else std::nullopt.
std::optional<double> percent_absolute_value(const DynamicPrintConfig &config, const std::string &opt_key);

// The unit of a setting taking a value or a percentage, from its side text: "mm/s or %" -> "mm/s". Empty if there is none.
std::string percent_value_unit(const ConfigOptionDef &def);

} // namespace Slic3r

#endif // slic3r_PercentValue_hpp_
