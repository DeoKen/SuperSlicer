///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "SupportDemandStep.hpp"

#include <algorithm>
#include <memory>
#include <utility>

#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"

namespace Slic3r::ApiHost::Steps {
namespace {

SupportDemandSet *to_demand(support_demand_handle *demand)
{
    return reinterpret_cast<SupportDemandSet *>(demand);
}

const SupportDemandSet *to_demand(const support_demand_handle *demand)
{
    return reinterpret_cast<const SupportDemandSet *>(demand);
}

const LayerSliceIsland *to_island(const layer_island_handle *island)
{
    return reinterpret_cast<const LayerSliceIsland *>(island);
}

ExPolygons *to_expolygons(expolygon_collection_handle *polygons)
{
    return reinterpret_cast<ExPolygons *>(polygons);
}

uint32_t support_demand_entry_count(const support_demand_handle *demand)
{
    return demand == nullptr ? 0u : to_demand(demand)->entry_count();
}

const layer_island_handle *support_demand_entry_island(const support_demand_handle *demand, uint32_t idx)
{
    if (demand == nullptr)
        return nullptr;
    const LayerSliceIsland *island = to_demand(demand)->entry_island(idx);
    return reinterpret_cast<const layer_island_handle *>(island);
}

expolygon_collection_handle *support_demand_get(support_demand_handle *demand, const layer_island_handle *island)
{
    if (demand == nullptr || island == nullptr)
        return nullptr;
    ExPolygons *polygons = to_demand(demand)->get(to_island(island));
    return reinterpret_cast<expolygon_collection_handle *>(polygons);
}

int32_t support_demand_set(support_demand_handle *demand,
                           const layer_island_handle *island,
                           expolygon_collection_handle *polygons)
{
    if (demand == nullptr || island == nullptr)
        return 0;
    return to_demand(demand)->set(to_island(island), to_expolygons(polygons));
}

void support_demand_clear(support_demand_handle *demand)
{
    if (demand != nullptr)
        to_demand(demand)->clear();
}

} // namespace

uint32_t SupportDemandSet::entry_count() const
{
    return uint32_t(m_entries.size());
}

const LayerSliceIsland *SupportDemandSet::entry_island(uint32_t idx) const
{
    return idx < m_entries.size() ? m_entries[idx].island : nullptr;
}

ExPolygons *SupportDemandSet::get(const LayerSliceIsland *island)
{
    if (island == nullptr)
        return nullptr;

    for (Entry &entry : m_entries)
        if (entry.island == island)
            return &entry.polygons;
    return nullptr;
}

int32_t SupportDemandSet::set(const LayerSliceIsland *island, ExPolygons *polygons)
{
    if (island == nullptr)
        return 0;

    std::vector<Entry>::iterator it = std::find_if(m_entries.begin(), m_entries.end(),
        [island](const Entry &entry) { return entry.island == island; });

    if (polygons == nullptr || polygons->empty()) {
        if (it != m_entries.end())
            m_entries.erase(it);
        return 1;
    }

    if (it != m_entries.end() && polygons == &it->polygons)
        return 1;

    if (it == m_entries.end()) {
        Entry entry;
        entry.island = island;
        entry.polygons = std::move(*polygons);
        m_entries.emplace_back(std::move(entry));
    } else {
        it->polygons = std::move(*polygons);
    }

    polygons->clear();
    return 1;
}

void SupportDemandSet::clear()
{
    m_entries.clear();
}

std::unique_ptr<SupportDemandRunContext> make_support_demand_run_context(Print &print,
                                                                         size_t object_idx,
                                                                         SupportDemandSet &demand)
{
    std::unique_ptr<SupportDemandRunContext> out = std::make_unique<SupportDemandRunContext>();
    out->context_step.print = reinterpret_cast<const print_handle *>(&print);
    out->context_step.object = reinterpret_cast<const object_handle *>(&print.object(object_idx));
    out->context_step.demand = reinterpret_cast<support_demand_handle *>(&demand);
    out->context_step.entry_count = support_demand_entry_count;
    out->context_step.entry_island = support_demand_entry_island;
    out->context_step.get = support_demand_get;
    out->context_step.set = support_demand_set;
    out->context_step.clear = support_demand_clear;
    return out;
}

} // namespace Slic3r::ApiHost::Steps
