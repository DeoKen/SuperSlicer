///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_UiLayoutMerger_hpp_
#define slic3r_UiLayoutMerger_hpp_

#include <cstdint>
#include <string>
#include <vector>

namespace Slic3r {

// Builds the effective .ui layout consumed by Tab from a base layout file and
// small plugin-provided fragments. The base file is preserved as closely as
// possible, while fragments may add pages, groups, lines, or settings at named
// insertion points.
class UiLayoutMerger
{
public:
    struct Fragment
    {
        // Stable fragment id. Orchestrator uses it to avoid registering the
        // same UI contribution twice.
        std::string id;
        std::string content;
        // Lower priority fragments are merged first. The order value is the
        // registration order and keeps equal-priority fragments deterministic.
        int32_t priority = 0;
        uint64_t order = 0;
    };

    explicit UiLayoutMerger(std::string target_file = {});

    // Base content is the full .ui file shipped with the application.
    void set_base(std::string base);
    // Fragment content uses the same page/group/line/setting syntax as a .ui
    // file, with optional insert directives in structural nodes.
    void add_fragment(std::string id, std::string content, int32_t priority, uint64_t order);
    // Returns the complete .ui text to feed into the existing Tab parser.
    std::string merged() const;

private:
    std::string m_target_file;
    std::string m_base;
    std::vector<Fragment> m_fragments;
};

} // namespace Slic3r

#endif // slic3r_UiLayoutMerger_hpp_
