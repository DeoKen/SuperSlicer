#ifndef slic3r_PointSerialize_hpp_
#define slic3r_PointSerialize_hpp_

#include <cereal/access.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>
#include <Eigen/Geometry>

#include "BoundingBox.hpp"
#include "ExPolygon.hpp"
#include "Point.hpp"

// External Cereal serialization for geometry point/vector types and the
// polygon containers built from them.
namespace cereal {

//    template<class Archive> void serialize(Archive& archive, Slic3r::Vec2crd &v) { archive(v.x(), v.y()); }
//    template<class Archive> void serialize(Archive& archive, Slic3r::Vec3crd &v) { archive(v.x(), v.y(), v.z()); }
template<class Archive> void serialize(Archive &archive, Slic3r::Vec2i32 &v) { archive(v.x(), v.y()); }
template<class Archive> void serialize(Archive &archive, Slic3r::Vec3i32 &v) { archive(v.x(), v.y(), v.z()); }
template<class Archive> void serialize(Archive &archive, Slic3r::Vec2i64 &v) { archive(v.x(), v.y()); }
template<class Archive> void serialize(Archive &archive, Slic3r::Vec3i64 &v) { archive(v.x(), v.y(), v.z()); }
template<class Archive> void serialize(Archive &archive, Slic3r::Vec2f &v) { archive(v.x(), v.y()); }
template<class Archive> void serialize(Archive &archive, Slic3r::Vec3f &v) { archive(v.x(), v.y(), v.z()); }
template<class Archive> void serialize(Archive &archive, Slic3r::Vec2d &v) { archive(v.x(), v.y()); }
template<class Archive> void serialize(Archive &archive, Slic3r::Vec3d &v) { archive(v.x(), v.y(), v.z()); }

template<class Archive> void serialize(Archive &archive, Slic3r::Matrix4d &m) {
    archive(binary_data(m.data(), 4 * 4 * sizeof(double)));
}
template<class Archive> void serialize(Archive &archive, Slic3r::Matrix2f &m) {
    archive(binary_data(m.data(), 2 * 2 * sizeof(float)));
}

template<class Archive, class T, int N>
inline void serialize(Archive &archive, Eigen::Transform<T, N, Eigen::Affine, Eigen::DontAlign> &t) {
    archive(t.matrix());
}

template<class Archive> void serialize(Archive &archive, Slic3r::BoundingBox &bb) {
    archive(bb.min, bb.max, bb.defined);
}
template<class Archive> void serialize(Archive &archive, Slic3r::BoundingBox3 &bb) {
    archive(bb.min, bb.max, bb.defined);
}
template<class Archive> void serialize(Archive &archive, Slic3r::BoundingBoxf &bb) {
    archive(bb.min, bb.max, bb.defined);
}
template<class Archive> void serialize(Archive &archive, Slic3r::BoundingBoxf3 &bb) {
    archive(bb.min, bb.max, bb.defined);
}

template<class Archive> void serialize(Archive &archive, Slic3r::Polygon &polygon) { archive(polygon.points); }

template<class Archive> void serialize(Archive &archive, Slic3r::ExPolygon &expoly) {
    archive(expoly.contour, expoly.holes);
}

} // namespace cereal

#endif // slic3r_PointSerialize_hpp_
