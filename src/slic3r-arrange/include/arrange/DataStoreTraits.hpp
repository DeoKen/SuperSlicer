///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef DATASTORETRAITS_HPP
#define DATASTORETRAITS_HPP

#include <algorithm>
#include <string>
#include <string_view>

#include "libslic3r/libslic3r.h"

namespace Slic3r { namespace arr2 {

// Some items can be containers of arbitrary data stored under string keys.
template<class ArrItem, class En = void> struct DataStoreTraits_
{
    static constexpr bool Implemented = false;

    template<class T> static const T *get(const ArrItem &, const std::string &key)
    {
        return nullptr;
    }

    // Same as above just not const.
    template<class T> static T *get(ArrItem &, const std::string &key)
    {
        return nullptr;
    }

    static bool has_key(const ArrItem &itm, const std::string &key)
    {
        return false;
    }
};

template<class ArrItem, class En = void> struct WritableDataStoreTraits_
{
    static constexpr bool Implemented = false;

    template<class T> static void set(ArrItem &, const std::string &key, T &&data)
    {
    }
};

template<class T> using DataStoreTraits = DataStoreTraits_<StripCVRef<T>>;
template<class T> constexpr bool IsDataStore = DataStoreTraits<StripCVRef<T>>::Implemented;
template<class T, class TT = T> using DataStoreOnly = std::enable_if_t<IsDataStore<T>, TT>;

template<class T, class ArrItem>
const T *get_data(const ArrItem &itm, const std::string &key)
{
    return DataStoreTraits<ArrItem>::template get<T>(itm, key);
}

template<class ArrItem>
bool has_key(const ArrItem &itm, const std::string &key)
{
    return DataStoreTraits<ArrItem>::has_key(itm, key);
}

template<class T, class ArrItem>
T *get_data(ArrItem &itm, const std::string &key)
{
    return DataStoreTraits<ArrItem>::template get<T>(itm, key);
}

template<class T> using WritableDataStoreTraits = WritableDataStoreTraits_<StripCVRef<T>>;
template<class T> constexpr bool IsWritableDataStore = WritableDataStoreTraits<StripCVRef<T>>::Implemented;
template<class T, class TT = T> using WritableDataStoreOnly = std::enable_if_t<IsWritableDataStore<T>, TT>;

template<class T, class ArrItem>
void set_data(ArrItem &itm, const std::string &key, T &&data)
{
    WritableDataStoreTraits<ArrItem>::template set<>(itm, key, std::forward<T>(data));
}

// Brim-aware arrange (fork): the outer brim of an item (width + separation, scaled; "brim" key), 0 = no brim.
template<class ArrItem> coord_t item_brim(const ArrItem &itm)
{
    const coord_t *brim = get_data<const coord_t>(itm, "brim");
    return brim != nullptr ? *brim : 0;
}

// The gap two items need on top of the object distance: brims may merge, so two items with a brim (or two without) need
// none, but a brim must not reach an item without one (also a bed keep-out zone or the wipe tower): the wider brim.
template<class ArrItemA, class ArrItemB> coord_t brim_gap(const ArrItemA &a, const ArrItemB &b)
{
    const coord_t brim_a = item_brim(a);
    const coord_t brim_b = item_brim(b);
    return (brim_a > 0) == (brim_b > 0) ? 0 : std::max(brim_a, brim_b);
}

template<class T> constexpr bool IsReadWritableDataStore = IsDataStore<T> && IsWritableDataStore<T>;
template<class T, class TT = T> using ReadWritableDataStoreOnly = std::enable_if_t<IsReadWritableDataStore<T>, TT>;

}} // namespace Slic3r::arr2

#endif // DATASTORETRAITS_HPP
