///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Api_host_steps_SupportDemandStep_hpp_
#define slic3r_Api_host_steps_SupportDemandStep_hpp_

#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <vector>

#include "libslic3r/Api/plugin/c/steps/slic3r_step_support_demand.h"
#include "libslic3r/ExPolygon.hpp"

namespace Slic3r {

class LayerSliceIsland;
class Print;

namespace ApiHost::Steps {

class SupportDemandSet
{
public:
    uint32_t entry_count() const;
    const LayerSliceIsland *entry_island(uint32_t idx) const;
    ExPolygons *get(const LayerSliceIsland *island);
    int32_t set(const LayerSliceIsland *island, ExPolygons *polygons);
    void clear();

private:
    struct Entry
    {
        const LayerSliceIsland *island = nullptr;
        ExPolygons polygons;
    };

    std::vector<Entry> m_entries;
};

struct SupportDemandRunContext
{
    run_ctx_support_demand context_step = {};
};

std::unique_ptr<SupportDemandRunContext> make_support_demand_run_context(Print &print,
                                                                         size_t object_idx,
                                                                         SupportDemandSet &demand);

} // namespace ApiHost::Steps
} // namespace Slic3r

#endif // slic3r_Api_host_steps_SupportDemandStep_hpp_
