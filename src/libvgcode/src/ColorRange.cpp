///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/
///|/ libvgcode is released under the terms of the AGPLv3 or higher
///|/
#include "../include/ColorRange.hpp"

#include "Utils.hpp"

#include <algorithm>
#include <assert.h>
#include <cmath>

namespace libvgcode {

const ColorRange ColorRange::DUMMY_COLOR_RANGE = ColorRange();
// SuperSlicer's Too_Low_Value_Color (light purple) and Too_High_Value_Color
const Color ColorRange::TOO_LOW_COLOR{ 158, 137, 200 };
const Color ColorRange::TOO_HIGH_COLOR{ 200, 137, 170 };

static float get_step_size(const ColorRange& color_range)
{
    const std::array<float, 2> range = color_range.get_current_range();
    const Palette& palette = color_range.get_palette();
    switch (color_range.get_type())
    {
    default:
    case EColorRangeType::Linear:
    {
        return (range[1] - range[0]) / (static_cast<float>(palette.size()) - 1.0f);
    }
    case EColorRangeType::Logarithmic:
    {
        return (range[0] != 0.0f) ? std::log(range[1] / range[0]) / (static_cast<float>(palette.size()) - 1.0f) : 0.0f;
    }
    }
}

ColorRange::ColorRange(EColorRangeType type)
: m_type(type)
, m_palette(DEFAULT_RANGES_COLORS)
{
}

EColorRangeType ColorRange::get_type() const
{
    return m_type;
}

const Palette& ColorRange::get_palette() const
{
    return m_palette;
}

void ColorRange::set_palette(const Palette& palette)
{
    if (palette.size() > 1)
        m_palette = palette;
}

// Rounding of the distinct values of the discrete mode.
static float round_discrete(float value)
{
    return std::round(value * 100.0f) / 100.0f;
}

Color ColorRange::get_color_at(float value) const
{
    const std::array<float, 2> range = get_current_range();
    if (! m_whole_print) {
        // Selected layers: the values of the layers out of view (SuperSlicer).
        if (round_discrete(value) < round_discrete(range[0]))
            return TOO_LOW_COLOR;
        if (round_discrete(value) > round_discrete(range[1]))
            return TOO_HIGH_COLOR;
    }

    if (is_discrete()) {
        // The color of the nearest distinct value.
        const auto [first, last] = get_discrete_ids();
        const auto it = std::lower_bound(m_discrete_values.begin() + first, m_discrete_values.begin() + last, value);
        std::size_t id = std::distance(m_discrete_values.begin(), it);
        if (id == last || (id > first && value - m_discrete_values[id - 1] < *it - value))
            --id;
        return get_discrete_color(id - first, last - first);
    }

    // Input value scaled to the colors range
    float global_t = 0.0f;
    value = std::clamp(value, range[0], range[1]);
    const float step = get_step_size(*this);
    if (step > 0.0f) {
        if (m_type == EColorRangeType::Logarithmic) {
            if (range[0] != 0.0f)
                global_t = std::log(value / range[0]) / step;
        }
        else
            global_t = (value - range[0]) / step;
    }

    const size_t color_max_idx = m_palette.size() - 1;

    // Compute the two colors just below (low) and above (high) the input value
    const size_t color_low_idx = std::clamp<size_t>(static_cast<size_t>(global_t), 0, color_max_idx);
    const size_t color_high_idx = std::clamp<size_t>(color_low_idx + 1, 0, color_max_idx);

    // Interpolate between the low and high colors to find exactly which color the input value should get
    return lerp(m_palette[color_low_idx], m_palette[color_high_idx], global_t - static_cast<float>(color_low_idx));
}

const std::array<float, 2>& ColorRange::get_range() const
{
    return m_range;
}

std::array<float, 2> ColorRange::get_current_range() const
{
    if (m_whole_print || m_layers_range[0] > m_layers_range[1])
        return m_range;
    return { std::max(m_range[0], m_layers_range[0]), std::min(m_range[1], m_layers_range[1]) };
}

std::pair<std::size_t, std::size_t> ColorRange::get_discrete_ids() const
{
    if (m_whole_print || m_layers_range[0] > m_layers_range[1])
        return { 0, m_discrete_values.size() };
    const std::array<float, 2> range = get_current_range();
    const auto first = std::lower_bound(m_discrete_values.begin(), m_discrete_values.end(), round_discrete(range[0]));
    const auto last  = std::upper_bound(first, m_discrete_values.end(), round_discrete(range[1]));
    return { std::distance(m_discrete_values.begin(), first), std::distance(m_discrete_values.begin(), last) };
}

Color ColorRange::get_discrete_color(std::size_t value_id, std::size_t count) const
{
    // The palette and the colors halfway between its colors, the values spread over them, the last one at the last color
    // (SuperSlicer).
    const std::size_t details_count = get_max_discrete_count();
    const std::size_t detail_id = (value_id + 1 == count) ? details_count - 1 :
        std::min(details_count - 1, static_cast<std::size_t>(0.01f + static_cast<float>(value_id * details_count) / static_cast<float>(count)));
    const std::size_t palette_id = detail_id / 2;
    return (detail_id % 2 == 0) ? m_palette[palette_id] : lerp(m_palette[palette_id], m_palette[palette_id + 1], 0.5f);
}

bool ColorRange::is_discrete() const
{
    return m_discrete && can_be_discrete();
}

bool ColorRange::can_be_discrete() const
{
    const std::size_t count = get_discrete_count();
    return count > 2 && count <= get_max_discrete_count();
}

std::size_t ColorRange::get_discrete_count() const
{
    if (m_too_many_values)
        return 0;
    const auto [first, last] = get_discrete_ids();
    return last - first;
}

std::vector<float> ColorRange::get_values() const
{
    std::vector<float> ret;
    const std::array<float, 2> range = get_current_range();
    // Count of values; selected layers: the distinct values in view, unless the print has too many
    const std::size_t count = (m_whole_print || m_too_many_values) ? m_count : get_discrete_count();

    if (is_discrete()) {
        const auto [first, last] = get_discrete_ids();
        ret.assign(m_discrete_values.begin() + first, m_discrete_values.begin() + last);
    }
    else if (count == 1 || range[0] == range[1]) {
        // single item use case
        ret.emplace_back(range[0]);
    }
    else if (count == 2) {
        // two items use case
        ret.emplace_back(range[0]);
        ret.emplace_back(range[1]);
    }
    else {
        const float step_size = get_step_size(*this);
        for (size_t i = 0; i < m_palette.size(); ++i) {
            float value = 0.0f;
            switch (m_type)
            {
            default:
            case EColorRangeType::Linear:      { value = range[0] + static_cast<float>(i) * step_size; break; }
            case EColorRangeType::Logarithmic: { value = ::exp(::log(range[0]) + static_cast<float>(i) * step_size);  break; }
            }
            ret.emplace_back(value);
        }
    }

    return ret;
}

size_t ColorRange::size_in_bytes_cpu() const
{
    size_t ret = STDVEC_MEMSIZE(m_palette, Color);
    ret += STDVEC_MEMSIZE(m_discrete_values, float);
    return ret;
}

void ColorRange::update(float value, bool interpolated)
{
    if (value != m_range[0] && value != m_range[1])
        ++m_count;

    m_range[0] = std::min(m_range[0], value);
    m_range[1] = std::max(m_range[1], value);

    if (! m_too_many_values && ! interpolated) {
        const float rounded = round_discrete(value);
        const auto  it = std::lower_bound(m_discrete_values.begin(), m_discrete_values.end(), rounded);
        if (it == m_discrete_values.end() || *it != rounded) {
            if (m_discrete_values.size() + 1 >= 3 * get_max_discrete_count()) {
                m_too_many_values = true;
                m_discrete_values.clear();
                m_discrete_values.shrink_to_fit();
            }
            else
                m_discrete_values.insert(it, rounded);
        }
    }
}

void ColorRange::update_layers(float value)
{
    m_layers_range[0] = std::min(m_layers_range[0], value);
    m_layers_range[1] = std::max(m_layers_range[1], value);
}

void ColorRange::reset_layers()
{
    m_layers_range = { FLT_MAX, -FLT_MAX };
}

void ColorRange::reset()
{
    m_range = { FLT_MAX, -FLT_MAX };
    m_count = 0;
    m_discrete_values.clear();
    m_too_many_values = false;
    reset_layers();
}

} // namespace libvgcode

