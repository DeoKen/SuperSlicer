///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/
///|/ libvgcode is released under the terms of the AGPLv3 or higher
///|/
#ifndef VGCODE_COLORRANGE_HPP
#define VGCODE_COLORRANGE_HPP

#include "../include/Types.hpp"

#include <cfloat>
#include <utility>
#include <vector>

namespace libvgcode {

static const Palette DEFAULT_RANGES_COLORS{ {
    {  11,  44, 122 }, // bluish
    {  19,  89, 133 },
    {  28, 136, 145 },
    {   4, 214,  15 },
    { 170, 242,   0 },
    { 252, 249,   3 },
    { 245, 206,  10 },
    { 227, 136,  32 },
    { 209, 104,  48 },
    { 194,  82,  60 },
    { 148,  38,  22 }  // reddish
} };

class ColorRange
{
public:
    //
    // Constructor
    //
    explicit ColorRange(EColorRangeType type = EColorRangeType::Linear);
    //
    // Return the type of this ColorRange.
    //
    EColorRangeType get_type() const;
    //
    // Return the palette used by this ColorRange.
    // Default is DEFAULT_RANGES_COLORS
    //
    const Palette& get_palette() const;
    //
    // Set the palette to be used by this ColorRange.
    // The given palette must contain at least two colors.
    //
    void set_palette(const Palette& palette);
    //
    // Return the interpolated color at the given value.
    // Value is clamped to [get_current_range()[0]..get_current_range()[1]].
    // Selected layers mode: a value outside the range of the selected layers gets TOO_LOW_COLOR / TOO_HIGH_COLOR.
    //
    Color get_color_at(float value) const;
    //
    // Return the range of this ColorRange.
    // The range is detected during the call to Viewer::load().
    // [0] -> min
    // [1] -> max
    //
    const std::array<float, 2>& get_range() const;
    //
    // Selected layers (SuperSlicer): when not in whole print mode, the colors span only the values of the layers in view,
    // get_range() clamped to them. Whole print by default.
    //
    bool is_whole_print() const { return m_whole_print; }
    std::array<float, 2> get_current_range() const;
    //
    // Colors of the values outside the range of the selected layers (SuperSlicer).
    //
    static const Color TOO_LOW_COLOR;
    static const Color TOO_HIGH_COLOR;
    //
    // Return the values corresponding to the detected color bins of this ColorRange.
    // The size of the returned vector can be:
    // 1                    - If only one value was detected while setting up this ColorRange.
    // 2                    - If only two values were detected while setting up this ColorRange.
    // get_palette().size() - If more than two distinct values were detected while setting up this ColorRange.
    // The distinct values  - In discrete mode (see is_discrete()).
    //
    std::vector<float> get_values() const;
    //
    // Discrete values (SuperSlicer): with more than two and at most get_max_discrete_count() distinct values, each value
    // gets its own color, spread over the palette and the colors halfway between its colors, instead of the continuous scale.
    //
    bool is_discrete() const;
    //
    // Whether discrete mode applies to this ColorRange: more than two and at most get_max_discrete_count() distinct values
    // within get_current_range().
    //
    bool can_be_discrete() const;
    //
    // Count of distinct values (rounded to 0.01) within get_current_range(), 0 if the print has too many values.
    //
    std::size_t get_discrete_count() const;
    std::size_t get_max_discrete_count() const { return 2 * m_palette.size() - 1; }
    //
    // Return the size of the palette, in bytes
    //
    std::size_t size_in_bytes_cpu() const;

    static const ColorRange DUMMY_COLOR_RANGE;

private:
    EColorRangeType m_type{ EColorRangeType::Linear };
    //
    // The palette used by this ColorRange
    // 
    Palette m_palette;
    //
    // [0] = min
    // [1] = max
    //
    std::array<float, 2> m_range{ FLT_MAX, -FLT_MAX };
    //
    // Count of different values passed to update()
    // 
    std::size_t m_count{ 0 };
    //
    // Distinct values passed to update(), rounded to 0.01 and sorted; cleared when there are 3 * get_max_discrete_count() or
    // more (SuperSlicer keeps that many, so that the selected layers can still have few values).
    //
    std::vector<float> m_discrete_values;
    bool m_too_many_values{ false };
    bool m_discrete{ false };
    //
    // Selected layers mode: range of the values of the layers in view, see update_layers().
    //
    bool m_whole_print{ true };
    std::array<float, 2> m_layers_range{ FLT_MAX, -FLT_MAX };

    void set_discrete(bool discrete) { m_discrete = discrete; }
    void set_whole_print(bool whole_print) { m_whole_print = whole_print; }
    //
    // Ids in m_discrete_values of the distinct values within get_current_range(), [first, last).
    //
    std::pair<std::size_t, std::size_t> get_discrete_ids() const;
    //
    // Color of the value_id-th of count distinct values.
    //
    Color get_discrete_color(std::size_t value_id, std::size_t count) const;
    //
    // Selected layers mode: use the passed value (of a layer in view) to update the range of the selected layers.
    //
    void update_layers(float value);
    void reset_layers();

    //
    // Use the passed value to update the range.
    // A value blended between two others (interpolated vertex) updates the range but is not a value of the discrete mode.
    //
    void update(float value, bool interpolated = false);
    //
    // Reset the range
    // Call this method before reuse an instance of ColorRange.
    //
    void reset();

    friend class ViewerImpl;
};

} // namespace libvgcode

#endif // VGCODE_COLORRANGE_HPP
