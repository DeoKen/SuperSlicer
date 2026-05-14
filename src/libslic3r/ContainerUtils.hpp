///|/ Copyright (c) Prusa Research 2016 - 2023
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_ContainerUtils_hpp_
#define slic3r_ContainerUtils_hpp_

#include <algorithm>
#include <cassert>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <set>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
// On MSVC, std::deque degenerates to a list of pointers, which defeats its purpose of reducing allocator load and memory fragmentation.
// https://github.com/microsoft/STL/issues/147#issuecomment-1090148740
// Thus it is recommended to use boost::container::deque instead.
#include <boost/container/deque.hpp>
#else
#include <queue>
#endif // _WIN32

namespace Slic3r {

// On MSVC, std::deque degenerates to a list of pointers, which defeats its purpose of reducing allocator load and memory fragmentation.
template<class T, class Allocator = std::allocator<T>>
using deque = 
#ifdef _WIN32
    // Use boost implementation, which allocates blocks of 512 bytes instead of blocks of 8 bytes.
    boost::container::deque<T, Allocator>;
#else // _WIN32
    std::deque<T, Allocator>;
#endif // _WIN32

template <typename T, typename Alloc, typename Alloc2>
inline void append(std::vector<T, Alloc> &dest, const std::vector<T, Alloc2> &src)
{
    if (dest.empty())
        dest = src;
    else
        dest.insert(dest.end(), src.begin(), src.end());
}

template <typename T, typename Alloc>
inline void append(std::set<T, Alloc>& dest, const std::set<T, Alloc>& src)
{
    if (dest.empty())
        dest = src;
    else
        dest.insert(src.begin(), src.end());
}

template <typename T, typename Alloc>
inline void append(std::vector<T, Alloc>& dest, std::vector<T, Alloc>&& src)
{
    if (dest.empty())
        dest = std::move(src);
    else {
        dest.insert(dest.end(),
            std::make_move_iterator(src.begin()),
            std::make_move_iterator(src.end()));
        src.clear();
        src.shrink_to_fit();
    }
}

template<class T, class... Args>
void clear_and_shrink(std::vector<T, Args...>& vec)
{
    std::vector<T, Args...> tmp;
    vec.swap(tmp);
    assert(vec.capacity() == 0);
}

template <typename T>
inline void append_reversed(std::vector<T>& dest, const std::vector<T>& src)
{
    if (dest.empty())
        dest = {src.rbegin(), src.rend()};
    else
        dest.insert(dest.end(), src.rbegin(), src.rend());
}

template <typename T>
inline void append_reversed(std::vector<T>& dest, std::vector<T>&& src)
{
    if (dest.empty())
        dest = {std::make_move_iterator(src.rbegin()),
                std::make_move_iterator(src.rend())};
    else
        dest.insert(dest.end(),
            std::make_move_iterator(src.rbegin()),
            std::make_move_iterator(src.rend()));
    src.clear();
    src.shrink_to_fit();
}

template<typename T_TO, typename T_FROM>
std::vector<T_TO> cast(const std::vector<T_FROM> &src)
{
    std::vector<T_TO> dst;
    dst.reserve(src.size());
    for (const T_FROM &a : src)
        dst.emplace_back((T_TO)a);
    return dst;
}

template <typename T>
inline void remove_nulls(std::vector<T*> &vec)
{
    vec.erase(
        std::remove_if(vec.begin(), vec.end(), [](const T *ptr) { return ptr == nullptr; }),
        vec.end());
}

template <typename T>
inline void sort_remove_duplicates(std::vector<T> &vec)
{
    std::sort(vec.begin(), vec.end());
    vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
}

template<typename T, typename... Args>
inline std::unique_ptr<T> make_unique(Args&&... args)
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

template<class ForwardIt, class LowerThanKeyPredicate>
ForwardIt lower_bound_by_predicate(ForwardIt first, ForwardIt last, LowerThanKeyPredicate lower_than_key)
{
    ForwardIt it;
    typename std::iterator_traits<ForwardIt>::difference_type count, step;
    count = std::distance(first, last);

    while (count > 0) {
        it = first;
        step = count / 2;
        std::advance(it, step);
        if (lower_than_key(*it)) {
            first = ++it;
            count -= step + 1;
        }
        else
            count = step;
    }
    return first;
}

template<class ForwardIt, class T, class Compare = std::less<>>
ForwardIt binary_find(ForwardIt first, ForwardIt last, const T& value, Compare comp = {})
{
    first = std::lower_bound(first, last, value, comp);
    return first != last && !comp(value, *first) ? first : last;
}

template<class ForwardIt, class LowerThanKeyPredicate, class EqualToKeyPredicate>
ForwardIt binary_find_by_predicate(ForwardIt first, ForwardIt last, LowerThanKeyPredicate lower_thank_key, EqualToKeyPredicate equal_to_key)
{
    first = lower_bound_by_predicate(first, last, lower_thank_key);
    return first != last && equal_to_key(*first) ? first : last;
}

template<typename ContainerType, typename ValueType> inline bool contains(const ContainerType &c, const ValueType &v)
    { return std::find(c.begin(), c.end(), v) != c.end(); }
template<typename T> inline bool contains(const std::initializer_list<T> &il, const T &v)
    { return std::find(il.begin(), il.end(), v) != il.end(); }

template<typename ContainerType, typename ValueType> inline bool one_of(const ValueType &v, const ContainerType &c)
    { return contains(c, v); }
template<typename T> inline bool one_of(const T& v, const std::initializer_list<T>& il)
    { return contains(il, v); }

template<class T, class I, class... Args>
std::enable_if_t<std::is_integral<I>::value, std::vector<T, Args...>> reserve_vector(I capacity)
{
    std::vector<T, Args...> ret;
    if (capacity > I(0))
        ret.reserve(size_t(capacity));

    return ret;
}

template<class It> class Range
{
    It from, to;
public:
    It begin() const { return from; }
    It end() const { return to; }

    using iterator = It;
    using value_type = typename std::iterator_traits<It>::value_type;

    Range() = default;
    Range(It b, It e) : from(std::move(b)), to(std::move(e)) {}

    inline size_t size() const { return std::distance(from, to); }
    inline bool empty() const { return from == to; }
};

template<class Cont> auto range(Cont &&cont)
{
    return Range{std::begin(cont), std::end(cont)};
}

template<class Cont> auto crange(Cont &&cont)
{
    return Range{std::cbegin(cont), std::cend(cont)};
}

template<class IntType = int, class = std::enable_if_t<std::is_integral<IntType>::value, void>>
class IntIterator {
    IntType m_val;
public:
    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type   = std::ptrdiff_t;
    using value_type        = IntType;
    using pointer           = IntType*;
    using reference         = IntType&;

    IntIterator(IntType v): m_val{v} {}

    IntIterator & operator++() { ++m_val; return *this; }
    IntIterator operator++(int) { auto cpy = *this; ++m_val; return cpy; }
    IntIterator & operator--() { --m_val; return *this; }
    IntIterator operator--(int) { auto cpy = *this; --m_val; return cpy; }

    IntType operator*() const { return m_val; }
    IntType operator->() const { return m_val; }

    bool operator==(const IntIterator& other) const { return m_val == other.m_val; }
    bool operator!=(const IntIterator& other) const { return !(*this == other); }
};

template<class IntType, class = std::enable_if_t<std::is_integral<IntType>::value>>
auto range(IntType from, IntType to)
{
    return Range{IntIterator{from}, IntIterator{to}};
}

template<class Fn, class...Args>
Fn for_each_argument(Fn &&fn, Args&&...args)
{
    // see https://www.fluentcpp.com/2019/03/05/for_each_arg-applying-a-function-to-each-argument-of-a-function-in-cpp/
    (fn(std::forward<Args>(args)),...);

    return fn;
}

template<class Fn, class Tup>
Fn for_each_in_tuple(Fn fn, Tup &&tup)
{
    auto mpfn = [&fn](auto&...pack) {
        for_each_argument(fn, pack...);
    };

    std::apply(mpfn, tup);

    return fn;
}

} // namespace Slic3r

#endif // slic3r_ContainerUtils_hpp_

