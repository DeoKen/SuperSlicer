///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "UiLayoutMerger.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace Slic3r {
namespace {

enum class UiNodeKind
{
    Root,
    Page,
    Group,
    Line,
    Raw,
};

enum class UiInsertPosition
{
    Append,
    Before,
    After,
};

struct UiInsertion
{
    bool has_anchor = false;
    UiInsertPosition position = UiInsertPosition::Append;
    // Anchor kind defaults to the node kind being inserted. The explicit
    // insert$afterline$Name form can target a different sibling kind.
    UiNodeKind anchor_kind = UiNodeKind::Raw;
    std::string anchor_name;
};

struct UiNode
{
    UiNodeKind kind = UiNodeKind::Raw;
    // Normalized line used for generated fragment nodes.
    std::string line;
    // Original text from the base file. Keeping it avoids unrelated formatting
    // churn when a plugin adds one line to a large existing layout.
    std::string raw_line;
    // end_line is attached to its line node so inserted settings remain inside
    // the line block and rendering can keep legacy unmatched end_line entries.
    std::string close_line;
    std::string name;
    UiInsertion insertion;
    std::vector<UiNode> children;
    bool preserve_raw_line = false;
    bool has_close_line = false;
    bool preserve_close_line = false;
};

static std::string trim_copy(std::string str)
{
    const char *spaces = " \t\r\n";
    const size_t begin = str.find_first_not_of(spaces);
    if (begin == std::string::npos)
        return {};
    const size_t end = str.find_last_not_of(spaces);
    return str.substr(begin, end - begin + 1);
}

static void replace_all(std::string &str, const std::string &from, const std::string &to)
{
    if (from.empty())
        return;
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.size(), to);
        pos += to.size();
    }
}

static std::vector<std::string> split_char(const std::string &str, char delimiter)
{
    std::vector<std::string> out;
    std::string current;
    std::istringstream stream(str);
    while (std::getline(stream, current, delimiter))
        out.emplace_back(current);
    if (!str.empty() && str.back() == delimiter)
        out.emplace_back();
    return out;
}

static std::vector<std::string> split_ui_params(std::string line)
{
    // UI values may contain escaped colons. Hide them while splitting, then
    // restore them so insertion directives are parsed without corrupting labels.
    replace_all(line, "\\:", "\x1f");
    std::vector<std::string> params = split_char(line, ':');
    for (std::string &param : params) {
        param = trim_copy(param);
        replace_all(param, "\x1f", ":");
    }
    return params;
}

static std::string join_ui_params(const std::vector<std::string> &params)
{
    std::string out;
    for (const std::string &param : params) {
        if (!out.empty())
            out += ':';
        out += param;
    }
    return out;
}

static bool starts_with(const std::string &str, const std::string &prefix)
{
    return str.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), str.begin());
}

static UiNodeKind anchor_kind_from_name(const std::string &name, UiNodeKind default_kind)
{
    if (name == "page")
        return UiNodeKind::Page;
    if (name == "group")
        return UiNodeKind::Group;
    if (name == "line")
        return UiNodeKind::Line;
    if (name == "setting")
        return UiNodeKind::Raw;
    return default_kind;
}

static bool parse_insert_option(const std::string &param, UiNodeKind default_kind, UiInsertion &insertion)
{
    // Supported forms:
    //   append
    //   before$Anchor / after$Anchor
    //   insert$afterline$Anchor / insert$beforegroup$Anchor
    std::vector<std::string> parts = split_char(param, '$');
    if (parts.empty())
        return false;

    if (parts[0] == "append") {
        insertion.has_anchor = false;
        insertion.position = UiInsertPosition::Append;
        return true;
    }

    if (parts[0] == "before" || parts[0] == "after") {
        if (parts.size() < 2)
            return false;
        insertion.has_anchor = true;
        insertion.position = parts[0] == "before" ? UiInsertPosition::Before : UiInsertPosition::After;
        insertion.anchor_kind = default_kind;
        insertion.anchor_name = parts[1];
        return true;
    }

    if (parts[0] != "insert" || parts.size() < 3)
        return false;

    std::string position = parts[1];
    UiNodeKind anchor_kind = default_kind;
    for (const std::string &kind_name : { std::string("page"), std::string("group"), std::string("line"),
                                          std::string("setting") }) {
        const size_t suffix_pos = position.find(kind_name);
        if (suffix_pos != std::string::npos) {
            position.erase(suffix_pos, kind_name.size());
            anchor_kind = anchor_kind_from_name(kind_name, default_kind);
            break;
        }
    }

    if (position != "before" && position != "after")
        return false;

    insertion.has_anchor = true;
    insertion.position = position == "before" ? UiInsertPosition::Before : UiInsertPosition::After;
    insertion.anchor_kind = anchor_kind;
    insertion.anchor_name = parts[2];
    return true;
}

static UiNode make_structural_node(UiNodeKind kind, const std::string &line, const std::string &raw_line, bool preserve_raw_line)
{
    UiNode node;
    node.kind = kind;
    node.raw_line = raw_line;
    node.preserve_raw_line = preserve_raw_line;

    const std::vector<std::string> params = split_ui_params(line);
    if (kind == UiNodeKind::Page) {
        if (params.size() >= 3)
            node.name = params[params.size() - 2];
        else if (params.size() >= 2)
            node.name = params.back();
    } else if (params.size() >= 2)
        node.name = params.back();

    const size_t last_option = kind == UiNodeKind::Page && params.size() >= 3 ? params.size() - 2 : params.size() - 1;
    std::vector<std::string> display_params;
    display_params.reserve(params.size());
    if (!params.empty())
        display_params.emplace_back(params.front());
    // Insertion directives are merge metadata, not part of the final .ui file.
    // Remove the first one found and render the structural node normally.
    bool insertion_option_found = false;
    for (size_t i = 1; i < params.size(); ++i) {
        if (!insertion_option_found && i < last_option && parse_insert_option(params[i], kind, node.insertion)) {
            insertion_option_found = true;
        } else
            display_params.emplace_back(params[i]);
    }
    node.line = join_ui_params(display_params);

    return node;
}

static UiNode make_raw_node(const std::string &line, const std::string &raw_line, bool preserve_raw_line)
{
    UiNode node;
    node.kind = UiNodeKind::Raw;
    node.raw_line = raw_line;
    node.preserve_raw_line = preserve_raw_line;

    const std::vector<std::string> params = split_ui_params(line);
    if (params.empty()) {
        node.line = line;
        return node;
    }

    // Settings are raw .ui lines, not structural nodes. Giving them a name and
    // honoring one insertion directive lets plugin fragments place a setting
    // next to a neighboring setting without wrapping it in an artificial line.
    node.name = params.back();

    std::vector<std::string> display_params;
    display_params.reserve(params.size());
    display_params.emplace_back(params.front());

    bool insertion_option_found = false;
    for (size_t i = 1; i < params.size(); ++i) {
        if (!insertion_option_found && i + 1 < params.size() &&
            parse_insert_option(params[i], UiNodeKind::Raw, node.insertion)) {
            insertion_option_found = true;
        } else
            display_params.emplace_back(params[i]);
    }

    node.line = join_ui_params(display_params);
    return node;
}

static std::string line_without_trailing_cr(std::string line)
{
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    return line;
}

static std::string detect_eol(const std::string &content)
{
    const size_t lf_pos = content.find('\n');
    if (lf_pos != std::string::npos && lf_pos > 0 && content[lf_pos - 1] == '\r')
        return "\r\n";
    return "\n";
}

static void set_line_close(UiNode *line_node, const std::string &line, const std::string &raw_line, bool preserve_raw_line)
{
    if (line_node == nullptr)
        return;
    line_node->close_line = line;
    line_node->has_close_line = true;
    line_node->preserve_close_line = preserve_raw_line;
    if (preserve_raw_line)
        line_node->close_line = raw_line;
}

static void add_child(UiNode *root, UiNode *page, UiNode *group, UiNode *line, UiNode &&child)
{
    if (line != nullptr)
        line->children.emplace_back(std::move(child));
    else if (group != nullptr)
        group->children.emplace_back(std::move(child));
    else if (page != nullptr)
        page->children.emplace_back(std::move(child));
    else
        root->children.emplace_back(std::move(child));
}

static UiNode parse_ui_tree(const std::string &content, bool preserve_raw_lines)
{
    // The base file is parsed with raw preservation enabled so merging a plugin
    // fragment can round-trip the existing layout. Fragments are parsed without
    // preservation because they are intentionally normalized when rendered.
    UiNode root;
    root.kind = UiNodeKind::Root;

    UiNode *current_page = nullptr;
    UiNode *current_group = nullptr;
    UiNode *current_line = nullptr;

    std::istringstream stream(content);
    std::string full_line;
    while (std::getline(stream, full_line)) {
        full_line = line_without_trailing_cr(std::move(full_line));
        const std::string line = trim_copy(full_line);
        if (line.size() < 4 || line.front() == '#') {
            if (preserve_raw_lines)
                add_child(&root, current_page, current_group, current_line, make_raw_node(line, full_line, true));
            continue;
        }

        if (starts_with(line, "page")) {
            root.children.emplace_back(make_structural_node(UiNodeKind::Page, line, full_line, preserve_raw_lines));
            current_page = &root.children.back();
            current_group = nullptr;
            current_line = nullptr;
        } else if (starts_with(line, "end_page")) {
            current_page = nullptr;
            current_group = nullptr;
            current_line = nullptr;
        } else if (starts_with(line, "group")) {
            UiNode group = make_structural_node(UiNodeKind::Group, line, full_line, preserve_raw_lines);
            add_child(&root, current_page, nullptr, nullptr, std::move(group));
            std::vector<UiNode> &siblings = current_page != nullptr ? current_page->children : root.children;
            current_group = &siblings.back();
            current_line = nullptr;
        } else if (starts_with(line, "end_group")) {
            current_group = nullptr;
            current_line = nullptr;
        } else if (starts_with(line, "line")) {
            UiNode line_node = make_structural_node(UiNodeKind::Line, line, full_line, preserve_raw_lines);
            add_child(&root, current_page, current_group, nullptr, std::move(line_node));
            std::vector<UiNode> &siblings = current_group != nullptr ? current_group->children :
                                            current_page != nullptr ? current_page->children :
                                            root.children;
            current_line = &siblings.back();
        } else if (starts_with(line, "end_line")) {
            if (current_line != nullptr)
                set_line_close(current_line, line, full_line, preserve_raw_lines);
            else if (preserve_raw_lines)
                add_child(&root, current_page, current_group, current_line, make_raw_node(line, full_line, true));
            current_line = nullptr;
        } else
            add_child(&root, current_page, current_group, current_line, make_raw_node(line, full_line, preserve_raw_lines));
    }

    return root;
}

static bool same_structural_node(const UiNode &lhs, const UiNode &rhs)
{
    return lhs.kind == rhs.kind && lhs.name == rhs.name;
}

static std::vector<UiNode>::iterator find_matching_node(std::vector<UiNode> &nodes, const UiNode &node)
{
    return std::find_if(nodes.begin(), nodes.end(), [&node](const UiNode &candidate) {
        return same_structural_node(candidate, node);
    });
}

static std::vector<UiNode>::iterator find_anchor_node(std::vector<UiNode> &nodes, const UiInsertion &insertion)
{
    return std::find_if(nodes.begin(), nodes.end(), [&insertion](const UiNode &candidate) {
        return candidate.kind == insertion.anchor_kind && candidate.name == insertion.anchor_name;
    });
}

static void merge_node(UiNode &target, UiNode &&patch);

static void insert_new_node(std::vector<UiNode> &nodes, UiNode &&node)
{
    // Missing anchors fall back to append. This keeps third-party fragments
    // usable when the base UI changed between releases.
    if (!node.insertion.has_anchor) {
        nodes.emplace_back(std::move(node));
        return;
    }

    std::vector<UiNode>::iterator anchor = find_anchor_node(nodes, node.insertion);
    if (anchor == nodes.end()) {
        nodes.emplace_back(std::move(node));
        return;
    }

    if (node.insertion.position == UiInsertPosition::After)
        ++anchor;
    nodes.insert(anchor, std::move(node));
}

static void merge_children(UiNode &target, UiNode &&patch)
{
    for (UiNode &child : patch.children) {
        if (child.kind == UiNodeKind::Page || child.kind == UiNodeKind::Group || child.kind == UiNodeKind::Line) {
            // Structural nodes are merged by their visible name. Raw nodes and
            // settings are appended because duplicate handling belongs to the
            // orchestrator/registration layer.
            std::vector<UiNode>::iterator existing = find_matching_node(target.children, child);
            if (existing != target.children.end())
                merge_node(*existing, std::move(child));
            else
                insert_new_node(target.children, std::move(child));
        } else
            insert_new_node(target.children, std::move(child));
    }
}

static void merge_node(UiNode &target, UiNode &&patch)
{
    merge_children(target, std::move(patch));
}

static void render_line(const UiNode &node, std::string &out, const std::string &indentation, const std::string &eol)
{
    if (node.preserve_raw_line)
        out += node.raw_line;
    else {
        out += indentation;
        out += node.line;
    }
    out += eol;
}

static void render_node(const UiNode &node, std::string &out, int indent, const std::string &eol)
{
    const std::string indentation(size_t(std::max(0, indent)), '\t');
    if (node.kind != UiNodeKind::Root)
        render_line(node, out, indentation, eol);

    const int child_indent = node.kind == UiNodeKind::Group || node.kind == UiNodeKind::Line ? indent + 1 : indent;
    for (const UiNode &child : node.children)
        render_node(child, out, child_indent, eol);

    if (node.kind == UiNodeKind::Line && node.has_close_line) {
        if (node.preserve_close_line)
            out += node.close_line;
        else {
            out += indentation;
            out += "end_line";
        }
        out += eol;
    }
}

} // namespace

UiLayoutMerger::UiLayoutMerger(std::string target_file) : m_target_file(std::move(target_file)) {}

void UiLayoutMerger::set_base(std::string base)
{
    m_base = std::move(base);
}

void UiLayoutMerger::add_fragment(std::string id, std::string content, int32_t priority, uint64_t order)
{
    Fragment fragment;
    fragment.id = std::move(id);
    fragment.content = std::move(content);
    fragment.priority = priority;
    fragment.order = order;
    m_fragments.emplace_back(std::move(fragment));
}

std::string UiLayoutMerger::merged() const
{
    UiNode root = parse_ui_tree(m_base, true);
    const std::string eol = detect_eol(m_base);

    std::vector<Fragment> fragments = m_fragments;
    // Deterministic ordering matters: the final text becomes the input to the
    // normal Tab parser, and tests compare it to a reference .ui file.
    std::sort(fragments.begin(), fragments.end(), [](const Fragment &lhs, const Fragment &rhs) {
        if (lhs.priority != rhs.priority)
            return lhs.priority < rhs.priority;
        if (lhs.order != rhs.order)
            return lhs.order < rhs.order;
        return lhs.id < rhs.id;
    });

    for (const Fragment &fragment : fragments) {
        UiNode patch = parse_ui_tree(fragment.content, false);
        merge_children(root, std::move(patch));
    }

    std::string out;
    render_node(root, out, 0, eol);
    return out;
}

} // namespace Slic3r
