///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "PrintObjectRegion.hpp"

#include "ContainerUtils.hpp"

namespace Slic3r {

bool PrintObjectRegions::LayerRangeRegions::has_volume(const ObjectID id) const {
    std::vector<VolumeExtents>::const_iterator it = lower_bound_by_predicate(this->volumes.begin(),
                                                                             this->volumes.end(),
                                                                             [id](const VolumeExtents &l) {
                                                                                 return l.volume_id < id;
                                                                             });
    return it != this->volumes.end() && it->volume_id == id;
}

void PrintObjectRegions::clear() {
    all_regions.clear();
    layer_ranges.clear();
    cached_volume_ids.clear();
}

} // namespace Slic3r
