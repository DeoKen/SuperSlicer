///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#pragma once

#include <cstdint>
#include <set>

#include "Views.hpp"

namespace slic3r_api {

void collect_object_printing_extruders(const Print &print,
                                       const Object &object,
                                       const PrintRegion &region,
                                       std::set<uint16_t> &object_extruders);

std::set<uint16_t> object_extruders(const Print &print, const Object &object);

coord_t check_z_step(coord_t val, coord_t z_step);

} // namespace slic3r_api
