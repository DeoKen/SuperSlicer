///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_PointUtils_hpp_
#define slic3r_PointUtils_hpp_

#include <Eigen/Geometry>
#include <oneapi/tbb/scalable_allocator.h>

#include "BoundingBox.hpp"
#include "ContainerUtils.hpp"
#include "Line.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"
#include "TypeTraits.hpp"

// Forward declarations for the main data tree types.
// Include this file from headers that only store pointers, references or simple
// pointer containers to these types, so they do not pull the full model/print
// headers and their transitive dependencies.

namespace Slic3r {

// /////////////////////////////////////////////////////////////////////////////
// Type safe conversions to and from scaled and unscaled coordinates
// /////////////////////////////////////////////////////////////////////////////

// Semantics are the following:
// Upscaling (scale_XX()): only from floating point types (or Vec) to either
//                       floating point or integer 'scaled coord' coordinates.
// Downscaling (unscale_XX()): from arithmetic (or Vec) to floating point only

//for scalar, use scale_i or scale_d and unscaled from libslic3r.h
// these defined here are only to be used by templated complex convertion method defined below
namespace {
// Conversion definition from unscaled to floating point scaled.
template<class Tout, class Tin, class = FloatingOnly<Tin>>
inline constexpr FloatingOnly<Tout> scale_templated(const Tin &v) noexcept {
    return Tout(v / Tin(SCALING_FACTOR));
}

// Conversion definition from unscaled to integer 'scaled coord'.
// TODO: is the rounding necessary? Here it is commented  out to show that
// it can be different for integers but it does not have to be. Using
// std::round means loosing noexcept and constexpr modifiers.
template<class Tout = coord_t, class Tin, class = FloatingOnly<Tin>>
inline constexpr ScaledCoordOnly<Tout> scale_templated(const Tin &v) noexcept {
    // return static_cast<Tout>(std::round(v / SCALING_FACTOR));
    return Tout(v / Tin(SCALING_FACTOR));
}

// Conversion from arithmetic scaled type to floating point unscaled.
template<class Tout = double, class Tin, class = ArithmeticOnly<Tin>, class = FloatingOnly<Tout>>
inline constexpr Tout unscale_templated(const Tin &v) noexcept {
    return Tout(v) * Tout(SCALING_FACTOR);
}
}

inline Vec2d   unscale_p(coord_t x, coord_t y) { return Vec2d(unscaled(x), unscaled(y)); }
inline Vec2d   unscale_p(const Vec2crd &pt) { return Vec2d(unscaled(pt.x()), unscaled(pt.y())); }
inline Vec2d   unscale_p(const Vec2d   &pt) { return Vec2d(unscaled(pt.x()), unscaled(pt.y())); }
inline Vec3d   unscale_p(coord_t x, coord_t y, coord_t z) { return Vec3d(unscaled(x), unscaled(y), unscaled(z)); }
inline Vec3d   unscale_p(const Vec3crd &pt) { return Vec3d(unscaled(pt.x()), unscaled(pt.y()), unscaled(pt.z())); }
inline Vec3d   unscale_p(const Vec3d   &pt) { return Vec3d(unscaled(pt.x()), unscaled(pt.y()), unscaled(pt.z())); }

template<class Tin, int...EigenArgs>
inline Vec2d unscale_p(const Eigen::Matrix<Tin, 2, EigenArgs...> &pt) { return Vec2d(unscaled(pt.x()), unscaled(pt.y())); }

template<class Tin, int...EigenArgs>
inline Vec3d unscale_p(const Eigen::Matrix<Tin, 3, EigenArgs...> &pt) { return Vec3d(unscaled(pt.x()), unscaled(pt.y()), unscaled(pt.z())); }

inline Point   scale_p(double x, double y) { return Point::new_scale(x, y); }
inline Point   scale_p(const Vec2d &pt) { return Point::new_scale(pt); }
inline Point   scale_p(const Vec2f &pt) { return Point::new_scale(pt); }
inline Vec3crd scale_p(double x, double y, double z) { return Vec3crd(scale_i(x), scale_i(y), scale_i(z)); }
inline Vec3crd scale_p(const Vec3d &pt) { return Vec3crd(scale_i(pt.x()), scale_i(pt.y()), scale_i(pt.z())); }
inline Vec3crd scale_p(const Vec3f &pt) { return Vec3crd(scale_i(pt.x()), scale_i(pt.y()), scale_i(pt.z())); }

template<class T, class Tin, int...EigenArgs>
inline Eigen::Matrix<ArithmeticOnly<T>, 2, EigenArgs...> scale_p(const Eigen::Matrix<Tin, 2, EigenArgs...> &pt) { return (pt / SCALING_FACTOR).template cast<T>(); }

template<class T, class Tin, int...EigenArgs>
inline Eigen::Matrix<ArithmeticOnly<T>, 3, EigenArgs...> scale_p(const Eigen::Matrix<Tin, 3, EigenArgs...> &pt) { return (pt / SCALING_FACTOR).template cast<T>(); }

std::vector<Vec3f> transform(const std::vector<Vec3f>& points, const Transform3f& t);
Pointf3s transform(const Pointf3s& points, const Transform3d& t);

/// <summary>
/// Check whether transformation matrix contains odd number of mirroring.
/// NOTE: In code is sometime function named is_left_handed
/// </summary>
/// <param name="transform">Transformation to check</param>
/// <returns>Is positive determinant</returns>
inline bool has_reflection(const Transform3d &transform) { return transform.matrix().determinant() < 0; }

/// <summary>
/// Getter on base of transformation matrix
/// </summary>
/// <param name="index">column index</param>
/// <param name="transform">source transformation</param>
/// <returns>Base of transformation matrix</returns>
inline const Vec3d get_base(unsigned index, const Transform3d &transform) { return transform.linear().col(index); }
inline const Vec3d get_x_base(const Transform3d &transform) { return get_base(0, transform); }
inline const Vec3d get_y_base(const Transform3d &transform) { return get_base(1, transform); }
inline const Vec3d get_z_base(const Transform3d &transform) { return get_base(2, transform); }
inline const Vec3d get_base(unsigned index, const Transform3d::LinearPart &transform) { return transform.col(index); }
inline const Vec3d get_x_base(const Transform3d::LinearPart &transform) { return get_base(0, transform); }
inline const Vec3d get_y_base(const Transform3d::LinearPart &transform) { return get_base(1, transform); }
inline const Vec3d get_z_base(const Transform3d::LinearPart &transform) { return get_base(2, transform); }

// I don't know why Eigen::Transform::Identity() return a const object...
template<int N, class T> Transform<N, T> identity() { return Transform<N, T>::Identity(); }
inline const auto &identity3f = identity<3, float>;
inline const auto &identity3d = identity<3, double>;

/// <summary>
/// Define point laying on polygon
/// keep index of polygon line and point coordinate
/// </summary>
struct PolygonPoint
{
    // index of line inside of polygon
    // 0 .. from point polygon[0] to polygon[1]
    size_t index;

    // Point, which lay on line defined by index
    Point point;
};
using PolygonPoints = std::vector<PolygonPoint>;

// To replace reserve_vector where it's used for Polygons
template<class I> IntegerOnly<I, Polygons> reserve_polygons(I cap)
{
    return reserve_vector<Polygon, I, typename Polygons::allocator_type>(cap);
}

// A generic class to search for a closest Point in a given radius.
// It uses std::unordered_multimap to implement an efficient 2D spatial hashing.
// The PointAccessor has to return const Point*.
// If a nullptr is returned, it is ignored by the query.
template<typename ValueType, typename PointAccessor> class ClosestPointInRadiusLookup
{
public:
    ClosestPointInRadiusLookup(coord_t search_radius, PointAccessor point_accessor = PointAccessor()) : 
		m_search_radius(search_radius), m_point_accessor(point_accessor), m_grid_log2(0)
    {
        // Resolution of a grid, twice the search radius + some epsilon.
		coord_t gridres = 2 * m_search_radius + 4;
        m_grid_resolution = gridres;
        assert(m_grid_resolution > 0);
        assert(m_grid_resolution < (coord_t(1) << 30));
		// Compute m_grid_log2 = log2(m_grid_resolution)
		if (m_grid_resolution > 32767) {
			m_grid_resolution >>= 16;
			m_grid_log2 += 16;
		}
		if (m_grid_resolution > 127) {
			m_grid_resolution >>= 8;
			m_grid_log2 += 8;
		}
		if (m_grid_resolution > 7) {
			m_grid_resolution >>= 4;
			m_grid_log2 += 4;
		}
		if (m_grid_resolution > 1) {
			m_grid_resolution >>= 2;
			m_grid_log2 += 2;
		}
		if (m_grid_resolution > 0)
			++ m_grid_log2;
		m_grid_resolution = ((coord_t)1) << m_grid_log2;
		assert(m_grid_resolution >= gridres);
		assert(gridres > m_grid_resolution / 2);
    }

    void insert(const ValueType &value) {
        const Vec2crd *pt = m_point_accessor(value);
        if (pt != nullptr)
            m_map.emplace(std::make_pair(Vec2crd(pt->x()>>m_grid_log2, pt->y()>>m_grid_log2), value));
    }

    void insert(ValueType &&value) {
        const Vec2crd *pt = m_point_accessor(value);
        if (pt != nullptr)
            m_map.emplace(std::make_pair(Vec2crd(pt->x()>>m_grid_log2, pt->y()>>m_grid_log2), std::move(value)));
    }

    // Erase a data point equal to value. (ValueType has to declare the operator==).
    // Returns true if the data point equal to value was found and removed.
    bool erase(const ValueType &value) {
        const Point *pt = m_point_accessor(value);
        if (pt != nullptr) {
            // Range of fragment starts around grid_corner, close to pt.
            auto range = m_map.equal_range(Point((*pt).x()>>m_grid_log2, (*pt).y()>>m_grid_log2));
            // Remove the first item.
            for (auto it = range.first; it != range.second; ++ it) {
                if (it->second == value) {
                    m_map.erase(it);
                    return true;
                }
            }
        }
        return false;
    }

    // Return a pair of <ValueType*, distance_squared>
    std::pair<const ValueType*, double> find(const Vec2crd &pt) {
        // Iterate over 4 closest grid cells around pt,
        // find the closest start point inside these cells to pt.
        const ValueType *value_min = nullptr;
        double           dist_min = std::numeric_limits<double>::max();
        // Round pt to a closest grid_cell corner.
        Vec2crd            grid_corner((pt.x()+(m_grid_resolution>>1))>>m_grid_log2, (pt.y()+(m_grid_resolution>>1))>>m_grid_log2);
        // For four neighbors of grid_corner:
        for (coord_t neighbor_y = -1; neighbor_y < 1; ++ neighbor_y) {
            for (coord_t neighbor_x = -1; neighbor_x < 1; ++ neighbor_x) {
                // Range of fragment starts around grid_corner, close to pt.
                auto range = m_map.equal_range(Vec2crd(grid_corner.x() + neighbor_x, grid_corner.y() + neighbor_y));
                // Find the map entry closest to pt.
                for (auto it = range.first; it != range.second; ++it) {
                    const ValueType &value = it->second;
                    const Vec2crd *pt2 = m_point_accessor(value);
                    if (pt2 != nullptr) {
                        const double d2 = (pt - *pt2).cast<double>().squaredNorm();
                        if (d2 < dist_min) {
                            dist_min = d2;
                            value_min = &value;
                        }
                    }
                }
            }
        }
        return (value_min != nullptr && dist_min < coordf_t(m_search_radius) * coordf_t(m_search_radius)) ? 
            std::make_pair(value_min, dist_min) : 
            std::make_pair(nullptr, std::numeric_limits<double>::max());
    }

    // Returns all pairs of values and squared distances.
    std::vector<std::pair<const ValueType*, double>> find_all(const Vec2crd &pt) {
        // Iterate over 4 closest grid cells around pt,
        // Round pt to a closest grid_cell corner.
        Vec2crd      grid_corner((pt.x()+(m_grid_resolution>>1))>>m_grid_log2, (pt.y()+(m_grid_resolution>>1))>>m_grid_log2);
        // For four neighbors of grid_corner:
        std::vector<std::pair<const ValueType*, double>> out;
        const double r2 = double(m_search_radius) * m_search_radius;
        for (coord_t neighbor_y = -1; neighbor_y < 1; ++ neighbor_y) {
            for (coord_t neighbor_x = -1; neighbor_x < 1; ++ neighbor_x) {
                // Range of fragment starts around grid_corner, close to pt.
                auto range = m_map.equal_range(Vec2crd(grid_corner.x() + neighbor_x, grid_corner.y() + neighbor_y));
                // Find the map entry closest to pt.
                for (auto it = range.first; it != range.second; ++it) {
                    const ValueType &value = it->second;
                    const Vec2crd *pt2 = m_point_accessor(value);
                    if (pt2 != nullptr) {
                        const double d2 = (pt - *pt2).cast<double>().squaredNorm();
                        if (d2 <= r2)
                            out.emplace_back(&value, d2);
                    }
                }
            }
        }
        return out;
    }

private:
    using map_type = typename std::unordered_multimap<Vec2crd, ValueType, PointHash>;
    PointAccessor m_point_accessor;
    map_type m_map;
    coord_t  m_search_radius;
    coord_t  m_grid_resolution;
    coord_t  m_grid_log2;
};

// Conversion for Eigen vectors (N dimensional points)
template<class Tout = coord_t,
         class Tin,
         int N,
         class = FloatingOnly<Tin>,
         int...EigenArgs>
inline Eigen::Matrix<ArithmeticOnly<Tout>, N, EigenArgs...>
scale_templated(const Eigen::Matrix<Tin, N, EigenArgs...> &v)
{
    return (v / SCALING_FACTOR).template cast<Tout>();
}

// Unscaling for Eigen vectors. Input base type can be arithmetic, output base
// type can only be floating point.
template<class Tout = double,
         class Tin,
         int N,
         class = ArithmeticOnly<Tin>,
         class = FloatingOnly<Tout>,
         int...EigenArgs>
inline constexpr Eigen::Matrix<Tout, N, EigenArgs...>
unscale_templated(const Eigen::Matrix<Tin, N, EigenArgs...> &v) noexcept
{
    return v.template cast<Tout>() * Tout(SCALING_FACTOR);
}

inline BoundingBox scale_bb(const BoundingBoxf &bb) { return {Point::new_scale(bb.min), Point::new_scale(bb.max)}; }

template<class T = coord_t, class Tin>
BoundingBoxBase<Vec<2, T>> scale_bb(const BoundingBoxBase<Vec<2, Tin>> &bb) { return {scale_templated<T>(bb.min), scale_templated<T>(bb.max)}; }

template<class T = coord_t>
BoundingBoxBase<Vec<2, T>> scale_bb(const BoundingBox &bb) { return {scale_templated<T>(bb.min), scale_templated<T>(bb.max)}; }

template<class T = coord_t, class Tin>
BoundingBox3Base<Vec<3, T>> scale_bb(const BoundingBox3Base<Vec<3, Tin>> &bb) { return {scale_templated<T>(bb.min), scale_templated<T>(bb.max)}; }

template<class T = double, class Tin>
BoundingBoxBase<Vec<2, T>> unscale_bb(const BoundingBoxBase<Vec<2, Tin>> &bb) { return {unscale_templated<T>(bb.min), unscale_templated<T>(bb.max)}; }

template<class T = double>
BoundingBoxBase<Vec<2, T>> unscale_bb(const BoundingBox &bb) { return {unscale_templated<T>(bb.min), unscale_templated<T>(bb.max)}; }

template<class T = double, class Tin>
BoundingBox3Base<Vec<3, T>> unscale_bb(const BoundingBox3Base<Vec<3, Tin>> &bb) { return {unscale_templated<T>(bb.min), unscale_templated<T>(bb.max)}; }

// Align a coordinate to a grid. The coordinate may be negative,
// the aligned value will never be bigger than the original one.
inline coord_t align_to_grid(const coord_t coord, const coord_t spacing) {
    // Current C++ standard defines the result of integer division to be rounded to zero,
    // for both positive and negative numbers. Here we want to round down for negative
    // numbers as well.
    coord_t aligned = (coord < 0) ?
            ((coord - spacing + 1) / spacing) * spacing :
            (coord / spacing) * spacing;
    assert(aligned <= coord);
    return aligned;
}
inline Point   align_to_grid(Point   coord, Point   spacing) 
    { return Point(align_to_grid(coord.x(), spacing.x()), align_to_grid(coord.y(), spacing.y())); }
inline coord_t align_to_grid(coord_t coord, coord_t spacing, coord_t base) 
    { return base + align_to_grid(coord - base, spacing); }
inline Point   align_to_grid(Point   coord, Point   spacing, Point   base)
    { return Point(align_to_grid(coord.x(), spacing.x(), base.x()), align_to_grid(coord.y(), spacing.y(), base.y())); }

// MinMaxLimits
template<typename T> struct MinMax { T min; T max;};
template<typename T>
static bool apply(std::optional<T> &val, const MinMax<T> &limit) {
    if (!val.has_value()) return false;
    return apply<T>(*val, limit);
}
template<typename T>
static bool apply(T &val, const MinMax<T> &limit)
{
    if (val > limit.max) {
        val = limit.max;
        return true;
    }
    if (val < limit.min) {
        val = limit.min;
        return true;
    }
    return false;
}

/// ================ From Line ===================
Linef3 transform(const Linef3& line, const Transform3d& t);

} // namespace Slic3r

// start Boost
#include <boost/version.hpp>
#include <boost/polygon/polygon.hpp>
namespace boost { namespace polygon {
    template <>
    struct geometry_concept<Slic3r::Polygon>{ typedef polygon_concept type; };

    template <>
    struct polygon_traits<Slic3r::Polygon> {
        typedef coord_t coordinate_type;
        typedef Slic3r::Points::const_iterator iterator_type;
        typedef Slic3r::Point point_type;

        // Get the begin iterator
        static inline iterator_type begin_points(const Slic3r::Polygon& t) {
            return t.points.begin();
        }

        // Get the end iterator
        static inline iterator_type end_points(const Slic3r::Polygon& t) {
            return t.points.end();
        }

        // Get the number of sides of the polygon
        static inline std::size_t size(const Slic3r::Polygon& t) {
            return t.points.size();
        }

        // Get the winding direction of the polygon
        static inline winding_direction winding(const Slic3r::Polygon& /* t */) {
            return unknown_winding;
        }
    };

    template <>
    struct polygon_mutable_traits<Slic3r::Polygon> {
        // expects stl style iterators
        template <typename iT>
        static inline Slic3r::Polygon& set_points(Slic3r::Polygon& polygon, iT input_begin, iT input_end) {
            polygon.points.clear();
            while (input_begin != input_end) {
                polygon.points.push_back(Slic3r::Point());
                boost::polygon::assign(polygon.points.back(), *input_begin);
                ++input_begin;
            }
            // skip last point since Boost will set last point = first point
            assert(polygon.points.front() == polygon.points.back());
            polygon.points.pop_back();
            return polygon;
        }
    };
    
    template <>
    struct geometry_concept<Slic3r::Polygons> { typedef polygon_set_concept type; };

    //next we map to the concept through traits
    template <>
    struct polygon_set_traits<Slic3r::Polygons> {
        typedef coord_t coordinate_type;
        typedef Slic3r::Polygons::const_iterator iterator_type;
        typedef Slic3r::Polygons operator_arg_type;

        static inline iterator_type begin(const Slic3r::Polygons& polygon_set) {
            return polygon_set.begin();
        }

        static inline iterator_type end(const Slic3r::Polygons& polygon_set) {
            return polygon_set.end();
        }

        //don't worry about these, just return false from them
        static inline bool clean(const Slic3r::Polygons& /* polygon_set */) { return false; }
        static inline bool sorted(const Slic3r::Polygons& /* polygon_set */) { return false; }
    };

    template <>
    struct polygon_set_mutable_traits<Slic3r::Polygons> {
        template <typename input_iterator_type>
        static inline void set(Slic3r::Polygons& polygons, input_iterator_type input_begin, input_iterator_type input_end) {
          polygons.assign(input_begin, input_end);
        }
    };
} }
// end Boost

#endif // slic3r_PointUtils_hpp_
