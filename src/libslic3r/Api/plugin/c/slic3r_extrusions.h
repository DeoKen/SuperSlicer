///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef slic3r_extrusion_h_
#define slic3r_extrusion_h_

#include <stddef.h>
#include <stdint.h>

#include "slic3r_def.h"
#include "slic3r_geometry.h"
#include "slic3r_utils.h"

#ifdef __cplusplus
extern "C" {
#endif
    /* ========================= EXTRUSION ROLE ========================= */

/*
Extrusion role bitmask (mapped from ExtrusionRoleModifier / ExtrusionRole).
Multiple flags can be combined.
*/
typedef int32_t raw_extrusion_role;

/* No role */
#define RAW_EXTRUSION_ROLE_NONE                 0

/* Base types */

/* Perimeter (internal / external / overhang) */
#define RAW_EXTRUSION_ROLE_PERIMETER            (1 << 0)

/* Infill */
#define RAW_EXTRUSION_ROLE_INFILL               (1 << 1)

/* Support material */
#define RAW_EXTRUSION_ROLE_SUPPORT              (1 << 2)

/* Skirt / brim */
#define RAW_EXTRUSION_ROLE_SKIRT                (1 << 3)

/* Wipe tower */
#define RAW_EXTRUSION_ROLE_WIPE_TOWER           (1 << 4)

/* Milling */
#define RAW_EXTRUSION_ROLE_MILL                 (1 << 5)

/* Modifiers */

/* External / visible */
#define RAW_EXTRUSION_ROLE_EXTERNAL             (1 << 6)

/* Solid */
#define RAW_EXTRUSION_ROLE_SOLID                (1 << 7)

/* Ironing */
#define RAW_EXTRUSION_ROLE_IRONING              (1 << 8)

/* Bridge / overhang */
#define RAW_EXTRUSION_ROLE_BRIDGE               (1 << 9)

/* Thin / gap fill / thin wall */
#define RAW_EXTRUSION_ROLE_THIN                 (1 << 10)

/* Special */

/* Mixed role */
#define RAW_EXTRUSION_ROLE_MIXED                (1 << 11)

/* Travel */
#define RAW_EXTRUSION_ROLE_TRAVEL               (1 << 12)

/* ----- Exact named combined roles from ExtrusionRole ----- */

#define RAW_EXTRUSION_ROLE_INTERNAL_PERIMETER \
    (RAW_EXTRUSION_ROLE_PERIMETER)

#define RAW_EXTRUSION_ROLE_EXTERNAL_PERIMETER \
    (RAW_EXTRUSION_ROLE_PERIMETER | RAW_EXTRUSION_ROLE_EXTERNAL)

#define RAW_EXTRUSION_ROLE_OVERHANG_PERIMETER \
    (RAW_EXTRUSION_ROLE_PERIMETER | RAW_EXTRUSION_ROLE_BRIDGE)

#define RAW_EXTRUSION_ROLE_OVERHANG_EXTERNAL_PERIMETER \
    (RAW_EXTRUSION_ROLE_PERIMETER | RAW_EXTRUSION_ROLE_EXTERNAL | RAW_EXTRUSION_ROLE_BRIDGE)

#define RAW_EXTRUSION_ROLE_INTERNAL_INFILL \
    (RAW_EXTRUSION_ROLE_INFILL)

#define RAW_EXTRUSION_ROLE_SOLID_INFILL \
    (RAW_EXTRUSION_ROLE_INFILL | RAW_EXTRUSION_ROLE_SOLID)

#define RAW_EXTRUSION_ROLE_TOP_SOLID_INFILL \
    (RAW_EXTRUSION_ROLE_INFILL | RAW_EXTRUSION_ROLE_SOLID | RAW_EXTRUSION_ROLE_EXTERNAL)

#define RAW_EXTRUSION_ROLE_IRONING_INFILL \
    (RAW_EXTRUSION_ROLE_INFILL | RAW_EXTRUSION_ROLE_SOLID | RAW_EXTRUSION_ROLE_IRONING | RAW_EXTRUSION_ROLE_EXTERNAL)

#define RAW_EXTRUSION_ROLE_BRIDGE_INFILL \
    (RAW_EXTRUSION_ROLE_INFILL | RAW_EXTRUSION_ROLE_SOLID | RAW_EXTRUSION_ROLE_BRIDGE | RAW_EXTRUSION_ROLE_EXTERNAL)

#define RAW_EXTRUSION_ROLE_INTERNAL_BRIDGE_INFILL \
    (RAW_EXTRUSION_ROLE_INFILL | RAW_EXTRUSION_ROLE_SOLID | RAW_EXTRUSION_ROLE_BRIDGE)

#define RAW_EXTRUSION_ROLE_GAP_FILL \
    (RAW_EXTRUSION_ROLE_MIXED | RAW_EXTRUSION_ROLE_THIN)

#define RAW_EXTRUSION_ROLE_THIN_WALL \
    (RAW_EXTRUSION_ROLE_PERIMETER | RAW_EXTRUSION_ROLE_THIN | RAW_EXTRUSION_ROLE_EXTERNAL)

#define RAW_EXTRUSION_ROLE_SUPPORT_MATERIAL \
    (RAW_EXTRUSION_ROLE_SUPPORT)

#define RAW_EXTRUSION_ROLE_SUPPORT_MATERIAL_INTERFACE \
    (RAW_EXTRUSION_ROLE_SUPPORT | RAW_EXTRUSION_ROLE_EXTERNAL)

#define RAW_EXTRUSION_ROLE_MILLING \
    (RAW_EXTRUSION_ROLE_MILL)

#define RAW_EXTRUSION_ROLE_WIPE_TOWER_DEFAULT \
    (RAW_EXTRUSION_ROLE_WIPE_TOWER)

#define RAW_EXTRUSION_ROLE_WIPE_TOWER_RAMMING \
    (RAW_EXTRUSION_ROLE_WIPE_TOWER | RAW_EXTRUSION_ROLE_BRIDGE)

#define RAW_EXTRUSION_ROLE_WIPE_TOWER_WIPE \
    (RAW_EXTRUSION_ROLE_WIPE_TOWER | RAW_EXTRUSION_ROLE_SOLID)

/* Check if flag is set */
#define RAW_EXTRUSION_ROLE_HAS(role, flag) (((role) & (flag)) != 0)

/* Add flag */
#define RAW_EXTRUSION_ROLE_ADD(role, flag) ((role) |= (flag))

/* Remove flag */
#define RAW_EXTRUSION_ROLE_REMOVE(role, flag) ((role) &= ~(flag))

/* ----- Helpers matching ExtrusionRole semantics ----- */

#define RAW_EXTRUSION_ROLE_IS_PERIMETER(role) \
    RAW_EXTRUSION_ROLE_HAS((role), RAW_EXTRUSION_ROLE_PERIMETER)

#define RAW_EXTRUSION_ROLE_IS_EXTERNAL(role) \
    RAW_EXTRUSION_ROLE_HAS((role), RAW_EXTRUSION_ROLE_EXTERNAL)

#define RAW_EXTRUSION_ROLE_IS_BRIDGE(role) \
    RAW_EXTRUSION_ROLE_HAS((role), RAW_EXTRUSION_ROLE_BRIDGE)

#define RAW_EXTRUSION_ROLE_IS_EXTERNAL_PERIMETER(role) \
    (RAW_EXTRUSION_ROLE_IS_PERIMETER(role) && RAW_EXTRUSION_ROLE_IS_EXTERNAL(role))

#define RAW_EXTRUSION_ROLE_IS_INFILL(role) \
    RAW_EXTRUSION_ROLE_HAS((role), RAW_EXTRUSION_ROLE_INFILL)

#define RAW_EXTRUSION_ROLE_IS_SOLID_INFILL(role) \
    (RAW_EXTRUSION_ROLE_IS_INFILL(role) && RAW_EXTRUSION_ROLE_HAS((role), RAW_EXTRUSION_ROLE_SOLID))

#define RAW_EXTRUSION_ROLE_IS_SPARSE_INFILL(role) \
    (RAW_EXTRUSION_ROLE_IS_INFILL(role) && !RAW_EXTRUSION_ROLE_HAS((role), RAW_EXTRUSION_ROLE_SOLID))

#define RAW_EXTRUSION_ROLE_IS_SUPPORT(role) \
    RAW_EXTRUSION_ROLE_HAS((role), RAW_EXTRUSION_ROLE_SUPPORT)

#define RAW_EXTRUSION_ROLE_IS_SUPPORT_BASE(role) \
    (RAW_EXTRUSION_ROLE_IS_SUPPORT(role) && !RAW_EXTRUSION_ROLE_IS_EXTERNAL(role))

#define RAW_EXTRUSION_ROLE_IS_SUPPORT_INTERFACE(role) \
    (RAW_EXTRUSION_ROLE_IS_SUPPORT(role) && RAW_EXTRUSION_ROLE_IS_EXTERNAL(role))

#define RAW_EXTRUSION_ROLE_IS_SKIRT(role) \
    RAW_EXTRUSION_ROLE_HAS((role), RAW_EXTRUSION_ROLE_SKIRT)

#define RAW_EXTRUSION_ROLE_IS_MIXED(role) \
    RAW_EXTRUSION_ROLE_HAS((role), RAW_EXTRUSION_ROLE_MIXED)

#define RAW_EXTRUSION_ROLE_IS_TRAVEL(role) \
    RAW_EXTRUSION_ROLE_HAS((role), RAW_EXTRUSION_ROLE_TRAVEL)


/* ========================= HANDLES ========================= */

typedef struct extrusion_entity extrusion_entity;

/* ---- Flow ---- */
/*
contains the caracteristic of the flow, with some cached values for optimization.
*/
typedef struct c_flow
{
    coord_t width;
    coord_t spacing;
    coord_t height;
    coord_t nozzle_diameter;
    int32_t is_bridge;
    float spacing_ratio;
    double mm3_per_mm;
} c_flow;


#ifdef __cplusplus
}
#endif

#endif // slic3r_extrusion_h_
