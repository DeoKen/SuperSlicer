///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas, Filip Sykala @Jony01, Tomáš Mészáros @tamasmeszaros, Vojtěch Král @vojtechkral
///|/ Copyright (c) SuperSlicer 2019 Remi Durand @supermerill
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2016 Mark Walker
///|/
///|/ ported from lib/Slic3r/Point.pm:
///|/ Copyright (c) Prusa Research 2018 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2011 - 2015 Alessandro Ranellucci @alranel
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Point_hpp_
#define slic3r_Point_hpp_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include <Eigen/Core> 
#include <oneapi/tbb/scalable_allocator.h>

#include "libslic3r.h"
#include "NumericUtils.hpp"

namespace Slic3r {

class BoundingBox;
class BoundingBoxf;
class Point;
using Vector = Point;

// Base template for eigen derived vectors
template<int N, int M, class T>
using Mat = Eigen::Matrix<T, N, M, Eigen::DontAlign, N, M>;

template<int N, class T> using Vec = Mat<N, 1, T>;

template<typename NumberType>
using DynVec = Eigen::Matrix<NumberType, Eigen::Dynamic, 1>;

// Eigen types, to replace the Slic3r's own types in the future.
// Vector types with a fixed point coordinate base type.
using Vec2crd = Eigen::Matrix<coord_t,  2, 1, Eigen::DontAlign>;
using Vec3crd = Eigen::Matrix<coord_t,  3, 1, Eigen::DontAlign>;
//using Vec2i32   = Eigen::Matrix<int,      2, 1, Eigen::DontAlign>;
//using Vec3i   = Eigen::Matrix<int,      3, 1, Eigen::DontAlign>;
//using Vec4i   = Eigen::Matrix<int,      4, 1, Eigen::DontAlign>;
using Vec2i32 = Eigen::Matrix<int32_t,  2, 1, Eigen::DontAlign>;
using Vec2i64 = Eigen::Matrix<int64_t,  2, 1, Eigen::DontAlign>;
using Vec3i32 = Eigen::Matrix<int32_t,  3, 1, Eigen::DontAlign>;
using Vec3i64 = Eigen::Matrix<int64_t,  3, 1, Eigen::DontAlign>;
using Vec4i32 = Eigen::Matrix<int32_t,  4, 1, Eigen::DontAlign>;

// Vector types with a double coordinate base type.
using Vec2f   = Eigen::Matrix<float,    2, 1, Eigen::DontAlign>;
using Vec3f   = Eigen::Matrix<float,    3, 1, Eigen::DontAlign>;
using Vec4f   = Eigen::Matrix<float,    4, 1, Eigen::DontAlign>;
using Vec2d   = Eigen::Matrix<double,   2, 1, Eigen::DontAlign>;
using Vec3d   = Eigen::Matrix<double,   3, 1, Eigen::DontAlign>;
using Vec4d   = Eigen::Matrix<double,   4, 1, Eigen::DontAlign>;

template<typename BaseType>
using PointsAllocator = tbb::scalable_allocator<BaseType>;
//using PointsAllocator = std::allocator<BaseType>;
using Points         = std::vector<Point, PointsAllocator<Point>>;
using PointPtrs      = std::vector<Point*>;
using PointConstPtrs = std::vector<const Point*>;
using Points3        = std::vector<Vec3crd>;
using Pointfs        = std::vector<Vec2d>;
using Vec2ds         = std::vector<Vec2d>;
using Pointf3s       = std::vector<Vec3d>;
// for storing product
//using P2             = Eigen::Matrix<Coord2, 2, 1, Eigen::DontAlign>;

using VecOfPoints    = std::vector<Points, PointsAllocator<Points>>;

using Matrix2f       = Eigen::Matrix<float,  2, 2, Eigen::DontAlign>;
using Matrix2d       = Eigen::Matrix<double, 2, 2, Eigen::DontAlign>;
using Matrix3f       = Eigen::Matrix<float,  3, 3, Eigen::DontAlign>;
using Matrix3d       = Eigen::Matrix<double, 3, 3, Eigen::DontAlign>;
using Matrix4f       = Eigen::Matrix<float,  4, 4, Eigen::DontAlign>;
using Matrix4d       = Eigen::Matrix<double, 4, 4, Eigen::DontAlign>;

template<int N, class T>
using Transform = Eigen::Transform<float, N, Eigen::Affine, Eigen::DontAlign>;

using Transform2f    = Eigen::Transform<float,  2, Eigen::Affine, Eigen::DontAlign>;
using Transform2d    = Eigen::Transform<double, 2, Eigen::Affine, Eigen::DontAlign>;
using Transform3f    = Eigen::Transform<float,  3, Eigen::Affine, Eigen::DontAlign>;
using Transform3d    = Eigen::Transform<double, 3, Eigen::Affine, Eigen::DontAlign>;

inline coordf_t dot(const Vec2d &v1, const Vec2d &v2) { return v1.x() * v2.x() + v1.y() * v2.y(); }
inline coordf_t dot(const Vec2d &v) { return v.x() * v.x() + v.y() * v.y(); }

inline bool operator<(const Vec2d &lhs, const Vec2d &rhs) {
    return lhs.x() < rhs.x() || (lhs.x() == rhs.x() && lhs.y() < rhs.y());
}

inline bool operator<(const std::vector<Vec2d> &lhs, const std::vector<Vec2d> &rhs) {
    if (lhs.size() == rhs.size()) {
        for (size_t i = 0; i < lhs.size(); i++) {
            if (lhs[i] < rhs[i])
                return true;
            if (!(lhs[i] == rhs[i]))
                return false;
        }
    }
    return lhs.size() < rhs.size();
}
inline bool operator<(const Vec3d &lhs, const Vec3d &rhs) {
    return lhs.x() < rhs.x() ||
        (lhs.x() == rhs.x() && (lhs.y() < rhs.y() || (lhs.y() == rhs.y() && lhs.z() < rhs.z())));
}

inline distsqrf_t squared_norm(const Vec2crd &vec) {
    return vec.x()*coordf_t(vec.x()) + vec.y()*coordf_t(vec.y());
}
inline lengthsqr_t squared_int_norm(const Vec2crd &vec) {
    // note: minimum can be 2 if both x and y are negative (negative shifting to 0 still produce 1 as -1 is full of 1).
    // as we're computing the norm, we can use abs 
    lengthsqr_t x = std::abs(vec.x()) >> SLIC3R_SQUARE_BIT_REDUCTION;
    lengthsqr_t y = std::abs(vec.y()) >> SLIC3R_SQUARE_BIT_REDUCTION;
    // x2 = x*x don't overflow
    assert(x < std::numeric_limits<uint32_t>::max());
    // y2 = y*y don't overflow
    assert(y < std::numeric_limits<uint32_t>::max());
    // x2 + y2 don't overflow
    assert((x * x) / 2 + (y * y) / 2 < std::numeric_limits<uint64_t>::max() / 2);
    return x * x + y * y;
}

// not sure of this usefulness...
inline double dot_double(Vec2crd v1, Vec2crd v2) {
    assert(is_approx(double(v1.dot(v2)), double(v1.x()) * double(v2.x()) + double(v1.y()) * double(v2.y()), 100.));
    return double(v1.x()) * double(v2.x()) + double(v1.y()) * double(v2.y());
}
inline int64_t dot_int(Vec2crd v1, Vec2crd v2) {
    return (v1.x() >> SLIC3R_SQUARE_BIT_REDUCTION) * (v2.x() >> SLIC3R_SQUARE_BIT_REDUCTION) + (v1.y() >> SLIC3R_SQUARE_BIT_REDUCTION) * (v2.y() >> SLIC3R_SQUARE_BIT_REDUCTION);
}

// Cross product of two 2D vectors.
// None of the vectors may be of int32_t type as the result would overflow.
template<typename Derived, typename Derived2>
inline typename Derived::Scalar cross2(const Eigen::MatrixBase<Derived> &v1, const Eigen::MatrixBase<Derived2> &v2)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "cross2(): first parameter is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "cross2(): first parameter is not a 2D vector");
    static_assert(! std::is_same<typename Derived::Scalar, int32_t>::value, "cross2(): Scalar type must not be int32_t, otherwise the cross product would overflow.");
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value, "cross2(): Scalar types of 1st and 2nd operand must be equal.");
    return v1.x() * v2.y() - v1.y() * v2.x();
}

// cross2 that use double as intermediate values, to avoid overflow of int types.
template<typename Derived, typename Derived2>
inline double cross2_double(const Eigen::MatrixBase<Derived> &v1, const Eigen::MatrixBase<Derived2> &v2)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "cross2(): first parameter is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "cross2(): first parameter is not a 2D vector");
    static_assert(! std::is_same<typename Derived::Scalar, int32_t>::value, "cross2(): Scalar type must not be int32_t, otherwise the cross product would overflow.");
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value, "cross2(): Scalar types of 1st and 2nd operand must be equal.");
    return (double(v1.x()) * double(v2.y()) - double(v1.y()) * double(v2.x()));
}

// 2D vector perpendicular to the argument.
template<typename Derived>
inline Eigen::Matrix<typename Derived::Scalar, 2, 1, Eigen::DontAlign> perp(const Eigen::MatrixBase<Derived> &v)
{ 
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "perp(): parameter is not a 2D vector");
    return { - v.y(), v.x() };
}

#if _DEBUG
inline double ccw_angle_old_test(const Vec2crd &me, const Vec2crd &p1, const Vec2crd &p2)
{
    //FIXME this calculates an atan2 twice! Project one vector into the other!
    double angle = atan2(p1.x() - (me).x(), p1.y() - (me).y())
                 - atan2(p2.x() - (me).x(), p2.y() - (me).y());
    // we only want to return only positive angles
    return angle <= 0 ? angle + 2*PI : angle;
}
#endif

inline double abs_angle(double rad) {
    return rad <= 0 ? rad + 2 * PI : rad;
}

// Angle from v1 to v2, returning double atan2(y, x) normalized to <-PI, PI>.
// By rotating v1 by this angle in the CCW direction, you get the direction of v2
// This rotation is CCW if the angle is >0.
template<typename Derived, typename Derived2>
inline double angle_ccw(const Eigen::MatrixBase<Derived> &v1, const Eigen::MatrixBase<Derived2> &v2) {
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "angle(): first parameter is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "angle(): second parameter is not a 2D vector");
    auto v1d = v1.template cast<double>();
    auto v2d = v2.template cast<double>();
    return atan2(cross2(v1d, v2d), v1d.dot(v2d));
}

template<typename Derived>
Eigen::Matrix<typename Derived::Scalar, 2, 1, Eigen::DontAlign> to_2d(const Eigen::MatrixBase<Derived> &ptN) {
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) >= 3, "to_2d(): first parameter is not a 3D or higher dimensional vector");
    return ptN.template head<2>();
}

template<typename Derived>
inline Eigen::Matrix<typename Derived::Scalar, 3, 1, Eigen::DontAlign> to_3d(const Eigen::MatrixBase<Derived> &pt, const typename Derived::Scalar z) {
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "to_3d(): first parameter is not a 2D vector");
    return { pt.x(), pt.y(), z };
}

std::string to_string(const Vec2crd &pt);
std::string to_string(const Vec2d   &pt);
std::string to_string(const Vec3crd &pt);
std::string to_string(const Vec3d   &pt);

class Point : public Vec2crd
{
public:
    using coord_type = coord_t;

    Point() : Vec2crd(0, 0) {}
    Point(int32_t x, int32_t y) : Vec2crd(coord_t(x), coord_t(y)) {}
    Point(int64_t x, int32_t y) : Vec2crd(coord_t(x), coord_t(y)) {}
    Point(int32_t x, int64_t y) : Vec2crd(coord_t(x), coord_t(y)) {}
    Point(int64_t x, int64_t y) : Vec2crd(coord_t(x), coord_t(y)) {}
    Point(coordf_t x, coordf_t y) : Vec2crd(coord_t(std::round(x)), coord_t(std::round(y))) {}
    Point(const Point &rhs) { *this = rhs; }
    // I don't know how to call it, as it call the implicit below
	explicit Point(const Vec2d& rhs) : Vec2crd(coord_t(std::round(rhs.x())), coord_t(std::round(rhs.y()))) {}
	// This constructor allows you to construct Point from Eigen expressions
    // This constructor has to be implicit (non-explicit) to allow implicit conversion from Eigen expressions.
    template<typename OtherDerived>
    Point(const Eigen::MatrixBase<OtherDerived> &other) : Vec2crd(other) {}
    static Point round(const Vec2d& rhs) { return Point(coord_t(std::round(rhs.x())), coord_t(std::round(rhs.y()))); }
    static Point new_scale(double x, double y) { return Point(scale_d(x), scale_d(y)); }
    // this one shouldn't exist.
    //static Point new_scale(const Point &p) { return Point(scale_i(p.x()), scale_i(p.y())); }
    template<typename OtherDerived>
    static Point new_scale(const Eigen::MatrixBase<OtherDerived> &v) { return Point(scale_i(v.x()), scale_i(v.y())); }

    // This method allows you to assign Eigen expressions to MyVectorType
    template<typename OtherDerived>
    Point& operator=(const Eigen::MatrixBase<OtherDerived> &other)
    {
        this->Vec2crd::operator=(other);
        return *this;
    }

    Point& operator+=(const Point& rhs) { this->x() += rhs.x(); this->y() += rhs.y(); return *this; }
    Point& operator-=(const Point& rhs) { this->x() -= rhs.x(); this->y() -= rhs.y(); return *this; }
    Point& operator*=(const double &rhs) {
        assert(coord_t(this->x() * rhs) == coord_t(std::clamp(this->x() * rhs, double(std::numeric_limits<coord_t>::min()), double(std::numeric_limits<coord_t>::max()))));
        assert(coord_t(this->y() * rhs) == coord_t(std::clamp(this->y() * rhs, double(std::numeric_limits<coord_t>::min()), double(std::numeric_limits<coord_t>::max()))));
        this->x() = coord_t(std::clamp(this->x() * rhs, double(std::numeric_limits<coord_t>::min()), double(std::numeric_limits<coord_t>::max()))); 
        this->y() = coord_t(std::clamp(this->y() * rhs, double(std::numeric_limits<coord_t>::min()), double(std::numeric_limits<coord_t>::max()))); 
        return *this;
    }
    //Point operator*(const double &rhs); //already exist outside

    void   rotate(double angle) { this->rotate(std::cos(angle), std::sin(angle)); }
    void   rotate(double cos_a, double sin_a) {
        double cur_x = (double)this->x();
        double cur_y = (double)this->y();
        this->x() = (coord_t)std::round(cos_a * cur_x - sin_a * cur_y);
        this->y() = (coord_t)std::round(cos_a * cur_y + sin_a * cur_x);
    }

    void   rotate(double angle, const Point &center);
    Point  rotated(double angle) const { Point res(*this); res.rotate(angle); return res; }
    Point  rotated(double cos_a, double sin_a) const { Point res(*this); res.rotate(cos_a, sin_a); return res; }
    Point  rotated(double angle, const Point &center) const { Point res(*this); res.rotate(angle, center); return res; }
    Point  projection_onto(const Point &line_pa, const Point &line_pb) const;
    Point  interpolate(const double percent, const Point &p) const;

    distf_t distance_to(const Point &point) const { return (point - *this).cast<distf_t>().norm(); }
    distsqrf_t distance_to_square(const Point &point) const {
        coordf_t dx = double(point.x() - this->x());
        coordf_t dy = double(point.y() - this->y());
        return dx*dx + dy*dy;
    }
    bool coincides_with(const Point &point) const { return this->x() == point.x() && this->y() == point.y(); }
    bool coincides_with_epsilon(const Point &point) const {
        return std::abs(this->x() - point.x()) < SCALED_EPSILON/2 && std::abs(this->y() - point.y()) < SCALED_EPSILON/2;
    }
};

inline bool operator<(const Point &l, const Point &r) 
{ 
    return l.x() < r.x() || (l.x() == r.x() && l.y() < r.y());
}

inline Point operator* (const Point& l, const double &r)
{
    assert(coord_t(l.x() * r) == coord_t(std::clamp(l.x() * r, double(std::numeric_limits<coord_t>::min()), double(std::numeric_limits<coord_t>::max()))));
    assert(coord_t(l.y() * r) == coord_t(std::clamp(l.y() * r, double(std::numeric_limits<coord_t>::min()), double(std::numeric_limits<coord_t>::max()))));
    return {
        coord_t(std::clamp(l.x() * r, double(std::numeric_limits<coord_t>::min()), double(std::numeric_limits<coord_t>::max()))),
        coord_t(std::clamp(l.y() * r, double(std::numeric_limits<coord_t>::min()), double(std::numeric_limits<coord_t>::max())))
    };
}

inline bool is_approx(const Point &p1, const Point &p2, coord_t epsilon = coord_t(SCALED_EPSILON))
{
	Point d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon;
}

inline bool is_approx(const Vec2f &p1, const Vec2f &p2, float epsilon = float(EPSILON))
{
	Vec2f d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon;
}

inline bool is_approx(const Vec2d &p1, const Vec2d &p2, double epsilon = EPSILON)
{
	Vec2d d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon;
}

inline bool is_approx(const Vec3f &p1, const Vec3f &p2, float epsilon = float(EPSILON))
{
	Vec3f d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon && d.z() < epsilon;
}

inline bool is_approx(const Vec3d &p1, const Vec3d &p2, double epsilon = EPSILON)
{
	Vec3d d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon && d.z() < epsilon;
}

inline Point lerp(const Point &a, const Point &b, double t)
{
    assert((t >= -EPSILON) && (t <= 1. + EPSILON));
    return ((1. - t) * a.cast<double>() + t * b.cast<double>()).cast<coord_t>();
}

// if IncludeBoundary, then a bounding box is defined even for a single point.
// otherwise a bounding box is only defined if it has a positive area.
template<bool IncludeBoundary = false>
BoundingBox get_extents(const Points &pts);
extern template BoundingBox get_extents<false>(const Points &pts);
extern template BoundingBox get_extents<true>(const Points &pts);

// if IncludeBoundary, then a bounding box is defined even for a single point.
// otherwise a bounding box is only defined if it has a positive area.
template<bool IncludeBoundary = false>
BoundingBox get_extents(const VecOfPoints &pts);
extern template BoundingBox get_extents<false>(const VecOfPoints &pts);
extern template BoundingBox get_extents<true>(const VecOfPoints &pts);

BoundingBoxf get_extents(const std::vector<Vec2d> &pts);

int nearest_point_index(const Points &points, const Point &pt);

inline std::pair<Point, bool> nearest_point(const Points &points, const Point &pt)
{
    int idx = nearest_point_index(points, pt);
    return idx == -1 ? std::make_pair(Point(), false) : std::make_pair(points[idx], true);
}

// Test for duplicate points in a vector of points.
// The points are copied, sorted and checked for duplicates globally.
bool        has_duplicate_points(Points &&pts);
inline bool has_duplicate_points(const Points &pts)
{
    Points cpy = pts;
    return has_duplicate_points(std::move(cpy));
}

// Test for duplicate points in a vector of points.
// Only successive points are checked for equality.
inline bool has_duplicate_successive_points(const Points &pts)
{
    for (size_t i = 1; i < pts.size(); ++ i)
        if (pts[i - 1] == pts[i])
            return true;
    return false;
}

// Test for duplicate points in a vector of points.
// Only successive points are checked for equality. Additionally, first and last points are compared for equality.
inline bool has_duplicate_successive_points_closed(const Points &pts)
{
    return has_duplicate_successive_points(pts) || (pts.size() >= 2 && pts.front() == pts.back());
}

// Collect adjecent(duplicit points)
Points collect_duplicates(Points pts /* Copy */);

inline bool shorter_then(const Vec2crd& p0, const coord_t len)
{
    if (p0.x() > len || p0.x() < -len)
        return false;
    if (p0.y() > len || p0.y() < -len)
        return false;
    //return squared_int_norm(p0) <= Slic3r::coord_int_sqr(len); // should do the same
    return p0.cast<distsqrf_t>().squaredNorm() <= coord_sqr(len);
}

namespace int128 {
    // Exact orientation predicate,
    // returns +1: CCW, 0: collinear, -1: CW.
    int orient(const Vec2crd &p1, const Vec2crd &p2, const Vec2crd &p3);
    // Exact orientation predicate,
    // returns +1: CCW, 0: collinear, -1: CW.
    int cross(const Vec2crd &v1, const Vec2crd &v2);
}

// To be used by std::unordered_map, std::unordered_multimap and friends.
// >>6 because it's not useful to keep the epsilon part for a hash (/64).
struct PointHash {
    size_t operator()(const Vec2crd &pt) const noexcept {
        return coord_t((89 * 31 + (int64_t(pt.x()) >> 6)) * 31 + (pt.y() >> 6));
    }
};

} // namespace Slic3r

// To be able to use Vec<> and Mat<> in range based for loops:
namespace Eigen {
template<class T, int N, int M>
T* begin(Slic3r::Mat<N, M, T> &mat) { return mat.data(); }

template<class T, int N, int M>
T* end(Slic3r::Mat<N, M, T> &mat) { return mat.data() + N * M; }

template<class T, int N, int M>
const T* begin(const Slic3r::Mat<N, M, T> &mat) { return mat.data(); }

template<class T, int N, int M>
const T* end(const Slic3r::Mat<N, M, T> &mat) { return mat.data() + N * M; }
} // namespace Eigen

#endif
