#ifndef libslic3r_SeamAttractor_hpp_
#define libslic3r_SeamAttractor_hpp_

#include <vector>

#include "libslic3r/GCode/SeamChoice.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r {
class PrintObject;
}

// Seam attractors (fork, from SuperSlicer): ModelVolumes of the types SEAM_POSITION_CENTER (sphere) and
// SEAM_POSITION_CENTER_Z (cylinder) move the seams of the object to the point of each perimeter nearest to them.
namespace Slic3r::Seams::Attractor {

// Per object layer (raft layers excluded, as the seam placer's layer index): the points the seams of the layer are
// drawn to, in the object's coordinates. Empty for a layer no attractor applies to. A cylinder applies to the layers
// within its height, at the center of its section; a sphere to every layer, unless a cylinder or a sphere nearer in Z
// applies. Several points when several attractors are equally near in Z: each perimeter takes the nearest one.
using LayerAttractors = std::vector<std::vector<Vec2d>>;

// Empty if the object has no seam attractor.
LayerAttractors get_layer_attractors(const PrintObject &print_object);

// The point of the perimeter nearest to the attractor point nearest to the perimeter. Painted enforcers win (the
// nearest enforced point), blocked points are taken only if all are blocked.
SeamChoice choose_seam_point(const Perimeters::Perimeter &perimeter, const std::vector<Vec2d> &attractors);

} // namespace Slic3r::Seams::Attractor

#endif // libslic3r_SeamAttractor_hpp_
