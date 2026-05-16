///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_slicing_step_h_
#define slic3r_slicing_step_h_

#include <stdint.h>

/* ========================= SLICING STEP ========================= */

/*
Defines the execution stage of a plugin in the slicing pipeline.

Values intentionally leave gaps so the host can add built-in stages and plugin
extensions without renumbering the existing API values.
*/
typedef enum slicing_step_t : uint16_t
{
    STEP_LAYER_HEIGHT              = 100,
    STEP_SLICING                   = 200,
    STEP_POST_SLICING              = 300,
    STEP_ALERT_SUPPORTS_NEEDED     = 400,
    STEP_PRE_PERIMETER             = 500,
    STEP_PERIMETER                 = 600,
    STEP_POST_PERIMETER            = 700,
    STEP_SURFACE_GENERATION        = 750,
    STEP_SURFACE_TYPE              = 800,
    STEP_PRE_INFILL                = 900,
    STEP_INFILL_GROUP              = 950,
    STEP_INFILL                    = 1000,
    STEP_POST_INFILL               = 1100,
    STEP_SUPPORT_SPOT              = 1200,
    STEP_SUPPORT                   = 1300,
    STEP_PRE_GCODE                 = 1400,
    STEP_CHECK_CONFLICT            = 1450,
    STEP_ORDERING                  = 1500,
    STEP_WIPETOWER                 = 1600,
    STEP_LAYER_EXTRUSION_EDIT      = 1650,
    STEP_LAYER_STICHING            = 1700,
    STEP_EXTRUSION_EDIT            = 1800,
    STEP_EXTRUSION_SIMPLIFICATION  = 1900,
    STEP_GCODE                     = 2000,

    /*
    Service plugin type used to create infill extrusion for a surface.
    */
    INFILL_PATTERN                 = 10000,

    /*
    Service plugin used to create bridge detector instances on demand.
    */
    BRIDGE_DETECTOR                = 10100

} slicing_step_t;

#endif // slic3r_slicing_step_h_
