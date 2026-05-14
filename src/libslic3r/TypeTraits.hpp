///|/ Copyright (c) Prusa Research 2016 - 2023
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_TypeTraits_hpp_
#define slic3r_TypeTraits_hpp_

#include <functional>
#include <iterator>
#include <limits>
#include <type_traits>

#include "libslic3r.h"

namespace Slic3r {

// A meta-predicate which is true for integers wider than or equal to coord_t.
template<class I> struct is_scaled_coord
{
    static const constexpr bool value =
        std::is_integral<I>::value &&
        std::numeric_limits<I>::digits >=
            std::numeric_limits<coord_t>::digits;
};

// Meta predicates for floating, 'scaled coord' and generic arithmetic types.
template<class T, class O = T>
using FloatingOnly = std::enable_if_t<std::is_floating_point<T>::value, O>;

template<class T, class O = T>
using ScaledCoordOnly = std::enable_if_t<is_scaled_coord<T>::value, O>;

template<class T, class O = T>
using IntegerOnly = std::enable_if_t<std::is_integral<T>::value, O>;

template<class T, class O = T>
using ArithmeticOnly = std::enable_if_t<std::is_arithmetic<T>::value, O>;

template<class T, class O = T>
using IteratorOnly = std::enable_if_t<
    !std::is_same_v<typename std::iterator_traits<T>::value_type, void>, O
>;

// Borrowed from C++20.
template<class T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

namespace detail_strip_ref_wrappers {
template<class T> struct StripCVRef_ { using type = remove_cvref_t<T>; };
template<class T> struct StripCVRef_<std::reference_wrapper<T>>
{
    using type = std::remove_cv_t<T>;
};
} // namespace detail_strip_ref_wrappers

// Removes reference wrappers as well.
template<class T> using StripCVRef =
    typename detail_strip_ref_wrappers::StripCVRef_<remove_cvref_t<T>>::type;

// Helper to be used in static_assert.
template<class T> struct always_false { enum { value = false }; };

} // namespace Slic3r

#endif // slic3r_TypeTraits_hpp_

