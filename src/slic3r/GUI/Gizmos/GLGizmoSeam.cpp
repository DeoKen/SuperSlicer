///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/ Copyright (c) Prusa Research 2020 - 2022 Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "GLGizmoSeam.hpp"

#include <wx/defs.h>

#include "libslic3r/Api/host/Orchestrator.hpp"

#include "slic3r/GUI/I18N.hpp"

namespace Slic3r::GUI {

static GenericFacetsAnnotationDefinition translated_seam_definition()
{
    GenericFacetsAnnotationDefinition def = builtin_seam_facets_annotation_definition();
    def.label = _u8L("Seam painting");
    def.enforce_label = _u8L("Enforce seam");
    def.block_label = _u8L("Block seam");
    return def;
}

GLGizmoSeam::GLGizmoSeam(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id)
    : GLGizmoGenericFacetPainting(parent, icon_filename, sprite_id, translated_seam_definition(), WXK_CONTROL_P)
{
}

} // namespace Slic3r::GUI
