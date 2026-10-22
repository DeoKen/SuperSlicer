///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef RECTANGLEOVERFITPACKINGSTRATEGY_HPP
#define RECTANGLEOVERFITPACKINGSTRATEGY_HPP

#include <arrange/Beds.hpp>

#include "Kernels/RectangleOverfitKernelWrapper.hpp"
#include "Kernels/KernelUtils.hpp"
#include "PackStrategyNFP.hpp"

namespace Slic3r { namespace arr2 {

using PostAlignmentFn = std::function<Vec2crd(const BoundingBox &bedbb,
                                              const BoundingBox &pilebb)>;

struct CenterAlignmentFn {
    Vec2crd operator() (const BoundingBox &bedbb,
                        const BoundingBox &pilebb)
    {
        return bedbb.center() - pilebb.center();
    }
};

template<class ArrItem>
struct RectangleOverfitPackingContext : public DefaultPackingContext<ArrItem>
{
    BoundingBox limits;
    int bed_index;
    PostAlignmentFn post_alignment_fn;

    explicit RectangleOverfitPackingContext(const BoundingBox limits,
                     int bedidx,
                     PostAlignmentFn alignfn = CenterAlignmentFn{})
        : limits{limits}, bed_index{bedidx}, post_alignment_fn{alignfn}
    {}

    void align_pile()
    {
        // Here, the post alignment can be safely done. No throwing
        // functions are called!
        if (fixed_items_range(*this).empty()) {
            auto itms = packed_items_range(*this);
            auto pilebb = bounding_box(itms);

            for (auto &itm : itms) {
                translate(itm, post_alignment_fn(limits, pilebb));
            }
        }
    }

    // Bed keep-out zones (bed_keep_out_zones) are fixed items, so align_pile() leaves the pile where the
    // inner-fit packing put it (off center). If the zones are the only fixed items, move the pile towards
    // the bed center as far as every item stays on the bed and clear of the zones (as the packing did).
    void align_pile_between_keep_out_zones()
    {
        auto fixed = fixed_items_range(*this);
        auto itms  = packed_items_range(*this);
        if (fixed.empty() || itms.empty() ||
            !std::all_of(fixed.begin(), fixed.end(), [](const auto &f) { return is_keep_out(f); }))
            return;

        const Vec2crd d = post_alignment_fn(limits, bounding_box(itms));
        auto fits = [&fixed, &itms, this](const Vec2crd &t) {
            for (const auto &itm : itms) {
                BoundingBox ebb = envelope_bounding_box(itm);
                ebb.translate(t);
                if (!limits.contains(ebb))
                    return false;
                for (const auto &zone : fixed) {
                    // A brim keeps its width from the zone (brim-aware arrange).
                    const coord_t gap = brim_gap(itm, zone);
                    BoundingBox   zbb = fixed_bounding_box(zone);
                    zbb.offset(gap);
                    if (ebb.overlap(zbb)) {
                        Polygons moved = envelope_outline(itm);
                        for (Polygon &p : moved)
                            p.translate(t);
                        if (!intersection(moved, gap > 0 ? offset(fixed_outline(zone), float(gap)) : fixed_outline(zone)).empty())
                            return false;
                    }
                }
            }
            return true;
        };

        // Fractions of the move per axis, nearest to the full move first; (0, 0) = stay.
        constexpr int Steps = 32;
        std::vector<std::pair<double, Vec2crd>> candidates;
        candidates.reserve((Steps + 1) * (Steps + 1));
        for (int ix = 0; ix <= Steps; ++ix)
            for (int iy = 0; iy <= Steps; ++iy) {
                const Vec2crd t(coord_t(int64_t(d.x()) * ix / Steps), coord_t(int64_t(d.y()) * iy / Steps));
                candidates.emplace_back((d - t).template cast<double>().squaredNorm(), t);
            }
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const auto &a, const auto &b) { return a.first < b.first; });
        for (const auto &c : candidates)
            if (c.second == Vec2crd::Zero())
                break;
            else if (fits(c.second)) {
                for (auto &itm : itms)
                    translate(itm, c.second);
                break;
            }
    }

    ~RectangleOverfitPackingContext()
    {
        align_pile();
        try {
            align_pile_between_keep_out_zones();
        } catch (...) {
            // Keep the packed (valid, only off center) positions.
        }
    }
};

// With rectange bed, and no fixed items, an infinite bed with
// RectangleOverfitKernelWrapper can produce better results than a pure
// RectangleBed with inner-fit polygon calculation.
template<class ...Args>
struct RectangleOverfitPackingStrategy {
    PackStrategyNFP<Args...> base_strategy;

    PostAlignmentFn post_alignment_fn = CenterAlignmentFn{};

    template<class ArrItem>
    using Context = RectangleOverfitPackingContext<ArrItem>;

    RectangleOverfitPackingStrategy(PackStrategyNFP<Args...> s,
                                    PostAlignmentFn post_align_fn)
        : base_strategy{std::move(s)}, post_alignment_fn{post_align_fn}
    {}

    RectangleOverfitPackingStrategy(PackStrategyNFP<Args...> s)
        : base_strategy{std::move(s)}
    {}
};

struct RectangleOverfitPackingStrategyTag {};

template<class... Args>
struct PackStrategyTag_<RectangleOverfitPackingStrategy<Args...>> {
    using Tag = RectangleOverfitPackingStrategyTag;
};

template<class... Args>
struct PackStrategyTraits_<RectangleOverfitPackingStrategy<Args...>> {
    template<class ArrItem>
    using Context = typename RectangleOverfitPackingStrategy<
        Args...>::template Context<StripCVRef<ArrItem>>;

    template<class ArrItem, class Bed>
    static Context<ArrItem> create_context(
        RectangleOverfitPackingStrategy<Args...> &ps,
        const Bed &bed,
        int bed_index)
    {
        return Context<ArrItem>{bounding_box(bed), bed_index,
                                ps.post_alignment_fn};
    }
};

template<class ArrItem>
struct PackingContextTraits_<RectangleOverfitPackingContext<ArrItem>>
    : public PackingContextTraits_<DefaultPackingContext<ArrItem>>
{
    static void add_packed_item(RectangleOverfitPackingContext<ArrItem> &ctx, ArrItem &itm)
    {
        ctx.add_packed_item(itm);

        // to prevent coords going out of range
        ctx.align_pile();
    }
};

template<class Strategy, class ArrItem, class Bed, class RemIt>
bool pack(Strategy &strategy,
          const Bed &bed,
          ArrItem &item,
          const PackStrategyContext<Strategy, ArrItem> &packing_context,
          const Range<RemIt> &remaining_items,
          const RectangleOverfitPackingStrategyTag &)
{
    bool ret = false;

    if (fixed_items_range(packing_context).empty()) {
        auto &base = strategy.base_strategy;
        PackStrategyNFP modded_strategy{
            base.solver,
            RectangleOverfitKernelWrapper{base.kernel, packing_context.limits},
            base.ep, base.accuracy};

        ret = pack(modded_strategy,
                   InfiniteBed{packing_context.limits.center()}, item,
                   packing_context, remaining_items, NFPPackingTag{});
    } else {
        ret = pack(strategy.base_strategy, bed, item, packing_context,
                   remaining_items, NFPPackingTag{});
    }

    return ret;
}

}} // namespace Slic3r::arr2

#endif // RECTANGLEOVERFITPACKINGSTRATEGY_HPP
