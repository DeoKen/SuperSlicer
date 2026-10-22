#include "libslic3r/GCode/SeamAttractor.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

namespace Slic3r::Seams::Attractor {

LayerAttractors get_layer_attractors(const PrintObject &print_object)
{
    std::vector<const ModelVolume*> volumes;
    for (const ModelVolume *volume : print_object.model_object()->volumes)
        if (volume->is_seam_attractor())
            volumes.push_back(volume);
    if (volumes.empty())
        return {};

    std::vector<float> zs;
    zs.reserve(print_object.layers().size());
    for (const Layer *layer : print_object.layers())
        zs.push_back(float(layer->slice_z));

    LayerAttractors     result(zs.size());
    std::vector<double> best_dz(zs.size(), std::numeric_limits<double>::max());
    // The nearest in Z wins, equally near ones are all kept.
    auto offer = [&result, &best_dz](size_t layer_idx, double dz, const Vec2d &point) {
        if (dz < best_dz[layer_idx] - EPSILON) {
            best_dz[layer_idx] = dz;
            result[layer_idx]  = { point };
        } else if (dz < best_dz[layer_idx] + EPSILON)
            result[layer_idx].push_back(point);
    };

    for (const ModelVolume *volume : volumes) {
        // The object's slicing coordinates (as PrintObjectSlice and the seam painting).
        const Transform3d trafo = print_object.trafo_centered() * volume->get_matrix();
        if (volume->type() == ModelVolumeType::SEAM_POSITION_CENTER) {
            const Vec3d center = volume->mesh().transformed_bounding_box(trafo).center();
            for (size_t i = 0; i < zs.size(); ++ i)
                offer(i, std::abs(double(zs[i]) - center.z()), center.head<2>());
        } else {
            // Cylinder: the center of its section, so also a tilted cylinder follows its axis.
            const std::vector<Polygons> sections = slice_mesh(volume->mesh().its, zs, MeshSlicingParams{ trafo });
            for (size_t i = 0; i < zs.size() && i < sections.size(); ++ i)
                if (! sections[i].empty())
                    offer(i, 0., unscaled<double>(get_extents(sections[i]).center()));
        }
    }
    return result;
}

// The point of the segment a-b nearest to p.
static Vec2d project(const Vec2d &p, const Vec2d &a, const Vec2d &b)
{
    const Vec2d  ab  = b - a;
    const double len = ab.squaredNorm();
    if (len <= 0.)
        return a;
    return a + std::clamp((p - a).dot(ab) / len, 0., 1.) * ab;
}

SeamChoice choose_seam_point(const Perimeters::Perimeter &perimeter, const std::vector<Vec2d> &attractors)
{
    using Perimeters::PointType;
    const std::vector<Vec2d> &positions = perimeter.positions;
    if (positions.empty() || attractors.empty())
        return SeamChoice{};

    // The point of the perimeter nearest to target among the points of the given type (the segments with both ends of
    // that type, and single points of that type).
    auto nearest = [&](const Vec2d &target, const PointType *type) {
        std::optional<SeamChoice> best;
        double                    best_dist = std::numeric_limits<double>::max();
        for (size_t i = 0; i < positions.size(); ++ i) {
            const size_t next = i + 1 == positions.size() ? 0 : i + 1;
            if (type != nullptr && perimeter.point_types[i] != *type)
                continue;
            if (type == nullptr || perimeter.point_types[next] == *type) {
                const Vec2d  point = project(target, positions[i], positions[next]);
                const double dist  = (point - target).squaredNorm();
                if (dist < best_dist) {
                    best_dist = dist;
                    best      = SeamChoice{ i, next, point };
                }
            } else if (const double dist = (positions[i] - target).squaredNorm(); dist < best_dist) {
                best_dist = dist;
                best      = SeamChoice{ i, i, positions[i] };
            }
        }
        return std::make_pair(best, best_dist);
    };

    // The attractor nearest to the perimeter.
    Vec2d  target    = attractors.front();
    double best_dist = std::numeric_limits<double>::max();
    for (const Vec2d &attractor : attractors)
        if (const double dist = nearest(attractor, nullptr).second; dist < best_dist) {
            best_dist = dist;
            target    = attractor;
        }

    // Painting wins: enforced points first, blocked points last.
    for (const PointType type : { PointType::enforcer, PointType::common, PointType::blocker })
        if (const std::optional<SeamChoice> choice = nearest(target, &type).first)
            return *choice;
    return SeamChoice{ 0, 0, positions.front() };
}

} // namespace Slic3r::Seams::Attractor
