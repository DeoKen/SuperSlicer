///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/ Copyright (c) Prusa Research 2021 Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_clipper_hpp
#define slic3r_clipper_hpp

// Hackish wrapper around the ClipperLib library to compile the Clipper library using Slic3r's own Point type.

#ifdef clipper_hpp
#error "You should include the libslic3r/clipper.hpp before clipper/clipper.hpp"
#endif

#ifdef CLIPPERLIB_USE_XYZ
#error "Something went wrong. Using clipper.hpp with Slic3r Point type, but CLIPPERLIB_USE_XYZ is defined."
#endif

#include "Point.hpp"

#define CLIPPERLIB_NAMESPACE_PREFIX		Slic3r
#define CLIPPERLIB_INTPOINT_TYPE    	Slic3r::Point

#include <clipper/clipper.hpp>

#undef clipper_hpp
#undef CLIPPERLIB_NAMESPACE_PREFIX
#undef CLIPPERLIB_INTPOINT_TYPE

#endif // slic3r_clipper_hpp
