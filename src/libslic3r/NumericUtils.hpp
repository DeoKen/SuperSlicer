///|/ Copyright (c) Prusa Research 2016 - 2023
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_NumericUtils_hpp_
#define slic3r_NumericUtils_hpp_

#include <cassert>
#include <cmath>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

#include "libslic3r.h"

namespace Slic3r {

constexpr inline double sqr(double x)
{
    return x * x;
}

constexpr inline float sqr(float x)
{
    return x * x;
}

template <typename T, typename Number>
constexpr inline T lerp(const T& a, const T& b, Number t)
{
    assert((t >= Number(-EPSILON)) && (t <= Number(1) + Number(EPSILON)));
    return (Number(1) - t) * a + t * b;
}

template <typename Number>
constexpr inline bool is_approx(Number value, Number test_value, Number precision = EPSILON)
{
    return std::fabs(double(value) - double(test_value)) < double(precision);
}

template<typename Number>
constexpr inline bool is_approx(const std::optional<Number> &value,
                                const std::optional<Number> &test_value)
{
    return (!value.has_value() && !test_value.has_value()) ||
        (value.has_value() && test_value.has_value() && is_approx<Number>(*value, *test_value));
}

template<class T, class = std::enable_if_t<std::is_floating_point<T>::value>>
constexpr T NaN = std::numeric_limits<T>::quiet_NaN();

constexpr float NaNf = NaN<float>;
constexpr double NaNd = NaN<double>;

// Rounding up.
// 1.5 is rounded to 2, 1.49 to 1, 0.5 to 1, 0.49 to 0,
// -0.5 to 0, -0.51 to -1, -1.5 to -1, -1.51 to -2.
template<typename I>
inline std::enable_if_t<std::is_integral<I>::value, I> fast_round_up(double a)
{
    // Why does Java Math.round(0.49999999999999994) return 1?
    // https://stackoverflow.com/questions/9902968/why-does-math-round0-49999999999999994-return-1
    return a == 0.49999999999999994 ? I(0) : I(floor(a + 0.5));
}

template<class T> using SamePair = std::pair<T, T>;

} // namespace Slic3r

#endif // slic3r_NumericUtils_hpp_

