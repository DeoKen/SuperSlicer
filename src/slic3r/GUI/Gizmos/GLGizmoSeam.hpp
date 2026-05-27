///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/ Copyright (c) Prusa Research 2019 - 2023 Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef slic3r_GLGizmoSeam_hpp_
#define slic3r_GLGizmoSeam_hpp_

#include "GLGizmoGenericFacetPainting.hpp"

namespace Slic3r::GUI {

class GLGizmoSeam : public GLGizmoGenericFacetPainting
{
public:
    GLGizmoSeam(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id);
};

} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoSeam_hpp_
