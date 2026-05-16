///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "PrintHelpers.hpp"

#include <algorithm>

namespace slic3r_api {

coord_t check_z_step(coord_t val, coord_t z_step)
{
    if (z_step <= SCALED_EPSILON)
        return val;
    return ((((val * 2) + z_step) / (2 * z_step)) * z_step);
}

void collect_object_printing_extruders(const Print &print,
                                       const Object &object,
                                       const PrintRegion &region,
                                       std::set<uint16_t> &object_extruders)
{
    const ConfigOption nozzle_diameter = print.config().get("nozzle_diameter");
    const int num_extruders = static_cast<int>(nozzle_diameter.size());

    auto emplace_extruder = [num_extruders, &object_extruders](int extruder_id) {
        const int idx = std::max(0, extruder_id - 1);
        object_extruders.insert(static_cast<uint16_t>((idx >= num_extruders) ? 0 : idx));
    };

    const Config object_config = object.config();
    const Config region_config = region.config();

    if (region_config.get("perimeters").get_int() > 0 ||
        object_config.get("brim_width").get_float() > 0.0 ||
        object_config.get("brim_width_interior").get_float() > 0.0)
        emplace_extruder(region_config.get("perimeter_extruder").get_int());

    if (region_config.get("fill_density").get_float() > 0.0)
        emplace_extruder(region_config.get("infill_extruder").get_int());

    if (region_config.get("top_solid_layers").get_int() > 0 ||
        region_config.get("bottom_solid_layers").get_int() > 0 ||
        (region_config.get("solid_infill_every_layers").get_int() > 0 &&
         region_config.get("fill_density").get_float() > 0.0))
        emplace_extruder(region_config.get("solid_infill_extruder").get_int());
}

std::set<uint16_t> object_extruders(const Print &print, const Object &object)
{
    std::set<uint16_t> extruders;
    for (uint32_t region_idx = 0; region_idx < object.print_region_count(); ++region_idx)
        collect_object_printing_extruders(print, object, object.print_region(region_idx), extruders);
    return extruders;
}

} // namespace slic3r_api
