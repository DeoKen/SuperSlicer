///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#pragma once

#ifdef _DEBUG

#include "libslic3r/Print.hpp"

#include <string>

namespace Slic3r::Steps {

/*
Debug-only helper used while replacing native slicing steps by plugins.

The class creates two independent Print instances from the same source Print:
- reference_print() is meant to run the original/native process;
- candidate_print() is meant to run the plugin-based process.

Model, ModelObject, ModelVolume and PrintObjectRegions are treated as read-only
inputs by the tested processes. The mutable output tree owned by Print/PrintObject
(layers, layer regions, layer islands and layer region islands) is duplicated by
running Print::apply() on both internal Print objects.

After each migrated step, call compare_tree() to verify that both mutable trees
still match. The comparison is intentionally strict and order-sensitive: if a
plugin produces equivalent geometry in a different order, this helper reports it
so we can decide explicitly whether that ordering difference is acceptable.
*/
class DebugPrintProcessComparator
{
public:
    explicit DebugPrintProcessComparator(const Print &source);

    Print &reference_print() { return m_reference_print; }
    Print &candidate_print() { return m_candidate_print; }
    const Print &reference_print() const { return m_reference_print; }
    const Print &candidate_print() const { return m_candidate_print; }

    // Compare the complete mutable print tree and append a precise path to
    // out_error on the first mismatch.
    bool compare_tree(std::string &out_error) const;

    // Same as compare_tree(), but prefixes the diagnostic with a step label.
    bool compare_tree_after(const char *label, std::string &out_error) const;

private:
    Print m_reference_print;
    Print m_candidate_print;
};

} // namespace Slic3r::Steps

#endif // _DEBUG

