///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef ARRANGEKERNELUTILS_HPP
#define ARRANGEKERNELUTILS_HPP

#include <type_traits>

#include <arrange/NFP/NFPArrangeItemTraits.hpp>
#include <arrange/Beds.hpp>
#include <arrange/DataStoreTraits.hpp>

namespace Slic3r { namespace arr2 {

// Bed keep-out zone (bed_keep_out_zones): a fixed obstacle that must not attract the arranged items.
template<class ArrItem> bool is_keep_out(const ArrItem &itm)
{
    constexpr const char * Key = "is_keep_out";

    return has_key(itm, Key);
}

template<class Itm, class Bed, class Context>
bool find_initial_position(Itm &itm,
                           const Vec2crd &sink,
                           const Bed &bed,
                           const Context &packing_context)
{
    bool ret = false;

    if constexpr (std::is_convertible_v<Bed, RectangleBed> ||
                  std::is_convertible_v<Bed, InfiniteBed> ||
                  std::is_convertible_v<Bed, CircleBed>)
    {
        // Bed keep-out zones alone don't prevent the placement at the sink, as long as the item stays clear of them.
        bool only_keep_out = true;
        for (const auto &fixed : all_items_range(packing_context))
            only_keep_out = only_keep_out && is_keep_out(fixed);
        auto clear_of_keep_out = [&packing_context, &itm](const BoundingBox &bb) {
            for (const auto &fixed : all_items_range(packing_context)) {
                // A brim keeps its width from the zone (brim-aware arrange).
                BoundingBox zone = fixed_bounding_box(fixed);
                zone.offset(brim_gap(itm, fixed));
                if (bb.overlap(zone))
                    return false;
            }
            return true;
        };

        if (only_keep_out) {
            auto rotations = allowed_rotations(itm);
            set_rotation(itm, 0.);
            auto chull     = envelope_convex_hull(itm);

            for (double rot : rotations) {
                auto chullcpy = chull;
                chullcpy.rotate(rot);
                auto bbitm = bounding_box(chullcpy);

                Vec2crd cb = sink;
                Vec2crd ci = bbitm.center();

                Vec2crd d = cb - ci;
                bbitm.translate(d);

                if (bounding_box(bed).contains(bbitm) && clear_of_keep_out(bbitm)) {
                    rotate(itm, rot);
                    translate(itm, d);
                    ret = true;
                    break;
                }
            }
        }
    }

    return ret;
}

template<class ArrItem> std::optional<Vec2crd> get_gravity_sink(const ArrItem &itm)
{
    constexpr const char * SinkKey = "sink";

    std::optional<Vec2crd> ret;

    auto ptr = get_data<Vec2crd>(itm, SinkKey);

    if (ptr)
        ret = *ptr;

    return ret;
}

template<class ArrItem> bool is_wipe_tower(const ArrItem &itm)
{
    constexpr const char * Key = "is_wipe_tower";

    return has_key(itm, Key);
}

}} // namespace Slic3r::arr2

#endif // ARRANGEKERNELUTILS_HPP
