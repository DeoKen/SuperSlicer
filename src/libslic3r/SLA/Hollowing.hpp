///|/ Copyright (c) Prusa Research 2019 - 2023 Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef SLA_HOLLOWING_HPP
#define SLA_HOLLOWING_HPP

#include <functional>
#include <memory>
#include <vector>

#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/Point.hpp>
#include <libslic3r/SLA/DrainHole.hpp>
#include <libslic3r/SLA/JobController.hpp>

struct indexed_triangle_set;

namespace Slic3r {

class ModelObject;
class TriangleMesh;
struct VoxelGrid;

namespace sla {

struct HollowingConfig
{
    double min_thickness    = 2.;
    double quality          = 0.5;
    double closing_distance = 0.5;
    bool enabled = true;
};

enum HollowingFlags { hfRemoveInsideTriangles = 0x1 };

// All data related to a generated mesh interior. Includes the 3D grid and mesh
// and various metadata. No need to manipulate from outside.
struct Interior;
struct InteriorDeleter { void operator()(Interior *p); };
using  InteriorPtr = std::unique_ptr<Interior, InteriorDeleter>;

indexed_triangle_set &      get_mesh(Interior &interior);
const indexed_triangle_set &get_mesh(const Interior &interior);

const VoxelGrid & get_grid(const Interior &interior);
VoxelGrid &get_grid(Interior &interior);

double get_voxel_scale(double mesh_volume, const HollowingConfig &hc);

InteriorPtr generate_interior(const VoxelGrid &mesh,
                              const HollowingConfig &  = {},
                              const JobController &ctl = {});

InteriorPtr generate_interior(const indexed_triangle_set &mesh,
                              const HollowingConfig &hc = {},
                              const JobController &ctl = {});

// Will do the hollowing
void hollow_mesh(TriangleMesh &mesh, const HollowingConfig &cfg, int flags = 0);

// Hollowing prepared in "interior", merge with original mesh
void hollow_mesh(TriangleMesh &mesh, const Interior &interior, int flags = 0);

// Will do the hollowing
void hollow_mesh(indexed_triangle_set &mesh, const HollowingConfig &cfg, int flags = 0);

// Hollowing prepared in "interior", merge with original mesh
void hollow_mesh(indexed_triangle_set &mesh, const Interior &interior, int flags = 0);

enum class HollowMeshResult {
    Ok = 0,
    FaultyMesh = 1,
    FaultyHoles = 2,
    DrillingFailed = 4
};

// Return HollowMeshResult codes OR-ed.
int hollow_mesh_and_drill(
    indexed_triangle_set &mesh,
    const Interior& interior,
    const DrainHoles &holes,
    std::function<void(size_t)> on_hole_fail = [](size_t){});

void remove_inside_triangles(TriangleMesh &mesh, const Interior &interior,
                             const std::vector<bool> &exclude_mask = {});

void remove_inside_triangles(indexed_triangle_set &mesh, const Interior &interior,
                             const std::vector<bool> &exclude_mask = {});

sla::DrainHoles transformed_drainhole_points(const ModelObject &mo,
                                             const Transform3d &trafo);

void cut_drainholes(std::vector<ExPolygons> & obj_slices,
                    const std::vector<float> &slicegrid,
                    float                     closing_radius,
                    const sla::DrainHoles &   holes,
                    std::function<void(void)> thr);

void swap_normals(indexed_triangle_set &its);

// Create exclude mask for triangle removal inside hollowed interiors.
// This is necessary when the interior is already part of the mesh which was
// drilled using CGAL mesh boolean operation. Excluded will be the triangles
// originally part of the interior mesh and triangles that make up the drilled
// hole walls.
std::vector<bool> create_exclude_mask(
    const indexed_triangle_set &its,
    const sla::Interior &interior,
    const std::vector<sla::DrainHole> &holes);

} // namespace sla
} // namespace Slic3r

#endif // HOLLOWINGFILTER_H
