# Fork features (PrusaSlicer 2.9.6 + SuperSlicer ports)

This branch (`claude/poc-per-feature-2.9.6`) is PrusaSlicer 2.9.6 with a small set of features ported from
SuperSlicer 2.7 and OrcaSlicer (and a few improvements). This document lists every feature added on top of stock PrusaSlicer:
what it does, its settings, how it works and how to test it. **Update it in the same commit as any new feature.**

General rules followed by every feature:

- **Defaults keep stock output.** With every new setting at its default, the G-code is byte for byte identical to stock
  PrusaSlicer 2.9.6 (only the new keys in the config block at the end of the file, and the `Internal bridge infill`
  type name, differ). This is checked by the golden test, see [Testing](#testing).
- Settings marked "per filament" live in the filament preset, "printer" in the printer preset, all others in the
  print preset. Most are visible in **Expert** mode only.

Contents:

1. [Internal bridge infill](#1-internal-bridge-infill)
2. [Per-feature fan speeds and default fan speed](#2-per-feature-fan-speeds-and-default-fan-speed)
3. [Bed keep-out zones](#3-bed-keep-out-zones)
4. [Per-feature accelerations and brim / skirt speed](#4-per-feature-accelerations-and-brim--skirt-speed)
5. [Fan mover (fan startup delay)](#5-fan-mover-fan-startup-delay)
6. [Custom G-code at every extrusion type change](#6-custom-g-code-at-every-extrusion-type-change-feature_gcode)
7. [Small perimeter min / max length](#7-small-perimeter-min--max-length)
8. [Hole size compensation](#8-hole-size-compensation)
9. [Avoid crossing perimeters options](#9-avoid-crossing-perimeters-options)
10. [Seam notch](#10-seam-notch)
11. [Brim ears and brim per object](#11-brim-ears-and-brim-per-object)
12. [Auxiliary fan (from OrcaSlicer)](#12-auxiliary-fan-from-orcaslicer)
13. [Infill directions (from OrcaSlicer)](#13-infill-directions-from-orcaslicer)
14. [GUI layout](#14-gui-layout)
15. [Testing](#testing)

---

## 1. Internal bridge infill

**What it does.** Bridges printed over sparse infill (supporting the top solid layers) are their own extrusion type,
`Internal bridge infill`, with their own speed, acceleration and fan. Bridges over air keep the bridge settings.

| Setting | Where | Default | Meaning |
|---|---|---|---|
| `internal_bridge_speed` | Print > Speed > Bridge infill speed: *Internal* | `100%` | mm/s, or % of `bridge_speed`. 0 = auto speed (max volumetric speed). |
| `internal_bridge_acceleration` | Print > Speed > Bridge acceleration: *Internal* | `0` | mm/s². 0 = use `bridge_acceleration`. |
| `internal_bridge_fan_speed` | Filament > Cooling > Bridge infill fan speed: *Internal* (per filament) | `-1` | %. -1 = use `bridge_fan_speed`. See [section 2](#2-per-feature-fan-speeds-and-default-fan-speed) for the cooling rules. |

**How it works.** `ExtrusionRole::InternalBridgeInfill` is assigned in `Fill/Fill.cpp` to `stInternalBridge`
surfaces. The G-code type is `;TYPE:Internal bridge infill`; the G-code viewer shows it in light blue. Everything else
(support detection, pressure equalizer, line width estimate) treats it like a bridge, as stock did.

**How to test.** `fff_print_tests "[InternalBridge]"`. Manually: slice a box with 15% infill, check in the preview that
the first solid layer above the sparse infill is "Internal bridge infill", and that the G-code there has the set
`M204`, `F` and `M106`.

---

## 2. Per-feature fan speeds and default fan speed

**What it does.** Every feature can have its own part cooling fan speed, with the same rules and fallbacks as
SuperSlicer. Typical use: a low base fan for ABS with a high fan on bridges and overhangs.

All settings are per filament, in Filament > Cooling > Fan settings, in %. **-1 = disabled** (default).

| Setting | GUI row: label | Falls back to (when -1) | Raised by short layers | Ramped by "full fan speed at layer" |
|---|---|---|---|---|
| `default_fan_speed` | Default fan speed | stock: "Keep fan always on" + min fan speed | yes (towards max fan speed) | yes |
| `perimeter_fan_speed` | Perimeter fan speed: Internal | default fan speed | yes | yes |
| `external_perimeter_fan_speed` | Perimeter fan speed: External | perimeter fan speed | yes | yes |
| `infill_fan_speed` | Internal infill fan speed: Sparse | default fan speed | yes | yes |
| `solid_infill_fan_speed` | Solid infill fan speed: Solid | default fan speed | yes | yes |
| `top_fan_speed` | Solid infill fan speed: Top solid (also ironing) | solid infill fan speed (ironing: default) | no | yes |
| `support_material_fan_speed` | Support material fan speed: Default | default fan speed | no | yes |
| `support_material_interface_fan_speed` | Support material fan speed: Interface | support material fan speed | no | no |
| `bridge_fan_speed` (stock) | Bridge infill fan speed: External | - | stock: only raises the fan | yes (stock) |
| `internal_bridge_fan_speed` | Bridge infill fan speed: Internal | bridge fan speed (stock logic) | yes | no |
| `overhangs_fan_speed` | Overhang perimeter fan speed | bridge fan speed (stock logic) | yes | no |
| `gap_fill_fan_speed` | Gap fill fan speed | default fan speed | yes | yes |

Rules:

- A feature fan speed is applied even when it is lower than the current fan speed (it overrides layer time cooling),
  except that short layers raise the features marked above towards `max_fan_speed`, as SuperSlicer does.
- `disable_fan_first_layers` turns every fan off on the first layers.
- `bridge_fan_speed` keeps the stock PrusaSlicer behaviour (it only raises the fan).
- PrusaSlicer's dynamic overhang fan speeds (`enable_dynamic_fan_speeds`) still apply on top of a feature fan speed.
- When `default_fan_speed` is set, it replaces "Keep fan always on" + "Min fan speed" (greyed out in the GUI).

**How it works.** `GCode.cpp` wraps every extrusion in `;_FEATURE_FAN_START<role>` / `;_FEATURE_FAN_END<role>`
markers. `GCode/CoolingBuffer.cpp` builds a per role fan table each layer and changes the fan only at feature
boundaries where a value is set (no toggling between two features with their own fan). The markers are removed.

**How to test.** `fff_print_tests "[FeatureFan]"` and `"[InternalBridge]"`. Manually: set e.g. perimeter 20 % and
external perimeter 60 %, slice, and in the preview (view type "Fan speed") the perimeters alternate between the two.

---

## 3. Bed keep-out zones

**What it does.** Rectangles of the bed the toolhead cannot reach at any height, for example the front corners taken
by the stepper mounts of an AWD Voron. Travels are routed around them, slicing is refused when something has to be
printed inside one, the plater shows them and arrange keeps objects out of them. The bed itself stays a plain
rectangle (a concave bed shape makes the plater much slower).

| Setting | Where | Default | Meaning |
|---|---|---|---|
| `bed_keep_out_zones` | Printer > General > Size and coordinates | empty | `x0,y0,x1,y1` per zone in bed coordinates (mm), zones separated by `;`. Example (350 mm AWD Voron): `0,0,40,40;310,0,350,40`. |

**How it works.** `GCode/BedKeepOut.cpp`: the zones are grown by 1 mm of clearance.

- Travels: after avoid crossing perimeters and before the retraction decision, a travel that crosses a zone gets the
  shortest detour through the zone corners inside the bed. The first move after custom G-code (position unknown) is
  routed from the last XY position the custom G-code moved to (e.g. the end of a purge line).
- Export check: layer outlines, support, skirt, brim and the wipe tower are tested against the zones (exactly, not the
  convex hull); anything inside refuses the slice with a message naming it.
- Plater (`GLCanvas3D.cpp`): objects reaching into a zone are shown as outside of the print area (bounding box test,
  then convex hull for objects near a zone), on every virtual bed. The first 4 zones are tinted on the bed.
- Arrange and fill bed: the zones are fixed obstacles on every bed (`arrange-wrapper/SceneBuilder.cpp`).
- Limits: moves written inside custom G-code (purge lines, macros) are not checked or rerouted.

**How to test.** `fff_print_tests "[KeepOut]"` and `arrange_tests "[KeepOut]"`. Manually: set the zones, check the
tint on the bed, drag an object into a corner (outside color, slicing refused), press A with many objects (corners stay
empty), slice objects near a corner and check in the preview (travel moves visible) that travels go around.

---

## 4. Per-feature accelerations and brim / skirt speed

**What it does.** Own accelerations for gap fill, support, support interface, ironing, and brim + skirt, and an own
speed for the brim and the skirt. All in Print > Speed (rows "Support speed", "Support acceleration", "Other
acceleration"). On the first layer, the first layer acceleration and speed still take precedence.

| Setting | Default | Meaning (mm/s² or %) |
|---|---|---|
| `gap_fill_acceleration` | `0` | % of the perimeter acceleration. 0 = default acceleration (stock). |
| `support_material_acceleration` | `0` | % of the default acceleration. 0 = default acceleration. |
| `support_material_interface_acceleration` | `0` | % of the support acceleration. 0 = support acceleration. |
| `ironing_acceleration` | `0` | % of the top solid infill acceleration. 0 = stock (solid infill acceleration). |
| `brim_acceleration` (brim and skirt) | `0` | % of the support acceleration. 0 = default acceleration. |
| `brim_speed` (brim and skirt) | `0` | mm/s or % of the support material speed. 0 = support material speed (stock). |

Note: in SuperSlicer, 0 for gap fill / ironing / brim acceleration means "same as perimeter / top solid / support". The
profile converter writes `100%` for those to keep the SuperSlicer behaviour.

**How it works.** `GCodeGenerator::feature_acceleration()` and `brim_speed()` in `GCode.cpp`, checked before the stock
acceleration chain in `_extrude()`.

**How to test.** `fff_print_tests "[FeatureAccel]"`. Manually: set distinct values, slice a supported model with
ironing and a skirt, and check the `M204` values per `;TYPE:` block in the G-code.

---

## 5. Fan mover (fan startup delay)

**What it does.** Starts fan speed increases earlier, so the fan is already spun up when the feature that needs it
starts (ported from SuperSlicer). Slowing the fan down is never moved.

| Setting | Where | Default | Meaning |
|---|---|---|---|
| `fan_speedup_time` | Printer > General > Cooling fan > Speedup time | `0` | Seconds. 0 = off. |
| `fan_speedup_overhangs` | same row: *Only for overhangs* | on | Only move the fan increases of overhang perimeters (greyed out while the delay is 0). |

**How it works.** `GCode/FanMover.cpp`, a filter after the cooling buffer. It estimates the time of each move from its
feed rate (ignoring acceleration), keeps a buffer of `fan_speedup_time` seconds and writes a fan increase that much
earlier, splitting a move when needed. Rules as in SuperSlicer: a fan command is never moved into the previous layer,
slower fan commands within the delay are dropped while speeding up, and fan commands inside custom G-code are not moved
(custom G-code is tagged `; custom gcode: <name>` / `; custom gcode end: <name>` while the mover is on). Custom G-code
blocks without fan commands are looked through, and their moves are never split. SuperSlicer's fan kickstart is not
ported (Klipper `[fan] kick_start_time` covers it).

**How to test.** `fff_print_tests "[FanMover]"`. Manually: set 2 s, slice a model with overhangs, compare the `M106`
positions with the delay at 0: the increases come earlier, the extrusion is identical.

---

## 6. Custom G-code at every extrusion type change (`feature_gcode`)

**What it does.** Custom G-code inserted at every extrusion type change, before the first extrusion of the new type
(ported from SuperSlicer).

| Setting | Where | Default |
|---|---|---|
| `feature_gcode` | Printer > Custom G-code > "Between extrusion role change G-code" | empty |

Variables: `extrusion_role` (= `next_extrusion_role`, the new type), `last_extrusion_role` (= `previous_extrusion_role`),
`layer_num`, `layer_z`, `max_layer_z`, plus all settings. Type names are the `;TYPE:` names: `Perimeter`,
`External perimeter`, `Overhang perimeter`, `Internal infill`, `Solid infill`, `Top solid infill`, `Ironing`,
`Bridge infill`, `Internal bridge infill`, `Gap fill`, `Skirt/Brim`, `Support material`, `Support material interface`,
`Wipe tower` (`Unknown` before the first extrusion). Example:

```
{if extrusion_role == "External perimeter"}SET_VELOCITY_LIMIT ACCEL=5000{endif}
```

The code should not move the print head (moves inside custom G-code are not tracked). Wipe tower extrusions don't
trigger it.

**How it works.** In `GCodeGenerator::_extrude()`, after the travel and unretraction, before the `;TYPE:` line.

**How to test.** `fff_print_tests "[FeatureGcode]"`. Manually: put `; FEATURE {last_extrusion_role} -> {extrusion_role}`
in it and check the G-code: one line before every `;TYPE:` change.

---

## 7. Small perimeter min / max length

**What it does.** Small perimeters (usually holes) are slowed down; instead of the fixed stock threshold, a min length
and a speed ramp up to a max length (ported from SuperSlicer).

| Setting | Where | Default | Meaning |
|---|---|---|---|
| `small_perimeter_speed` (stock) | Print > Speed > Modifiers > Small perimeter speed: *Speed* | `15` | mm/s or % of the perimeter speed. |
| `small_perimeter_min_length` | same row: *Min length* | `0` | mm or % of the nozzle diameter. Perimeters up to it get the small perimeter speed. 0 = stock threshold, a circle of 6.5 mm radius (40.8 mm). |
| `small_perimeter_max_length` | same row: *Max length* | `0` | mm or % of the nozzle diameter. Between min and max, the speed ramps linearly from the small perimeter speed up to the perimeter's own speed. 0 = no ramp. |

Note: in SuperSlicer, a min length of 0 means "no small perimeters"; the profile converter writes `0.001` for that.

**How it works.** `GCodeGenerator::small_perimeter_speed()` in `GCode.cpp`, applied to each perimeter (loop or open
path) by its length, as stock does.

**How to test.** `fff_print_tests "[SmallPerimeter]"`. Manually: min 6 mm, max 20 mm, slice a plate with holes of
2-8 mm diameter, and check the hole perimeter speeds (preview "Speed" view) grow with the hole size.

---

## 8. Hole size compensation

**What it does.** Grows or shrinks the convex holes of the object in XY, to fine-tune hole sizes (holes usually print a
bit too small). The outer contour and the holes that are not convex are not changed (ported from SuperSlicer, only the
hole part: PrusaSlicer has no separate inner XY compensation).

| Setting | Where | Default | Meaning |
|---|---|---|---|
| `hole_size_compensation` | Print > Advanced > Slicing > XY holes compensation: *Size* | `0` | mm. Negative = the hole gets bigger, positive = smaller. 0 = off. |
| `hole_size_threshold` | same row: *Threshold* | `100` | mm². Full compensation for holes up to this area, fading out to none at four times this area. 0 = full compensation for all holes. |

**How it works.** In `PrintObject::slice_volumes()` (`PrintObjectSlice.cpp`), after the XY size and elephant foot
compensation of each layer: the holes of the whole layer are found, a hole is convex when all its corners are convex
seen from inside it (0.1 rad tolerance, as SuperSlicer), and the grown hole is cut out of every region (negative value) or
the ring is added to the region bordering the hole most (positive value). Not applied to multi-material painted objects,
like the XY size compensation.

**How to test.** `fff_print_tests "[HoleCompensation]"`. Manually: a test plate with round holes of a few sizes, print it
with 0 and with e.g. -0.05 mm and measure the holes with pins or calipers.

---

## 9. Avoid crossing perimeters options

**What it does.** Three options from SuperSlicer for "Avoid crossing perimeters" (Print > Layers and perimeters >
Advanced). They only act when "Avoid crossing perimeters" is on (off by default), so stock output is unaffected; they
default to on, as in SuperSlicer.

| Setting | GUI row: label | Default | Meaning |
|---|---|---|---|
| `avoid_crossing_not_first_layer` | Avoid crossing modifiers: *Not on first layer* | on | No avoid crossing perimeters on the first layer. |
| `avoid_crossing_top` | Avoid crossing modifiers: *Avoid top surfaces* | on | Don't travel over top surfaces (stock PrusaSlicer always avoids them). With perimeters and no ironing a narrow lane along the edge of the top surfaces stays usable for travels (SuperSlicer). Off: travels may cross top surfaces. |
| `avoid_travel_island` | Between islands: *Find smallest crossing* | on | Travelling between two islands of a layer, cross the gap where they are nearest. |
| `avoid_travel_island_weight` | Between islands: *Weight* | `0.4` | Weight of the travel inside the islands to reach the crossing. 0 = always the smallest crossing, higher = prefer a crossing nearer the straight travel. |

Difference from SuperSlicer: these are print wide settings (in SuperSlicer, the top and island options can also be set
in object modifiers).

**How it works.** `GCode/AvoidCrossingPerimeters.cpp`:
- not on first layer: `generate_travel_xy_path()` in `GCode.cpp` skips avoid crossing perimeters on the first layer.
- top surfaces: `get_boundary()` removes the top surfaces (shrunk by half a perimeter spacing) from the area the travels
  are planned in, and adds the lane along their edge.
- islands: `travel_between_islands()`: when start and end are in different islands, the crossing minimising
  `weight * (start to leave point) + gap + weight * (enter point to end)` is searched on the internal boundaries of both
  islands (sampled every 1 mm, nearest point of the other island from an edge grid). It is used when its cost is below
  `(1 + weight) * straight length` (as SuperSlicer); the parts inside the islands are planned as usual. This is a new
  implementation of SuperSlicer's idea on PrusaSlicer's travel planner, not a line by line port.

**How to test.** `fff_print_tests "[AvoidCrossing]"` (and PrusaSlicer's `"[AvoidCrossingPerimeters]"`). Manually: turn on
avoid crossing perimeters, slice a plate of small parts close together as one object (or a part with separate islands)
and look at the travel moves in the preview with "Find smallest crossing" on and off.

---

## 10. Seam notch

**What it does.** From SuperSlicer (Print > Layers and perimeters > Advanced, row "Seam notch"): the start and the end of
the external perimeter loops are moved a little inside the part, into a small cavity, so the bulge of the seam sinks
into the wall instead of sticking out. All sizes default to 0 (off), so stock output is unaffected.

| Setting | GUI row: label | Default | Meaning |
|---|---|---|---|
| `seam_notch_all` | Seam notch: *All* | `0` | Notch depth for every external perimeter, mm or % of the external perimeter width. |
| `seam_notch_inner` | Seam notch: *Round holes* | `0` | Depth for convex (round or oval) holes; takes precedence over *All* there. |
| `seam_notch_outer` | Seam notch: *Round perimeters* | `0` | Depth for convex (round or oval) outer perimeters; takes precedence over *All* there. |
| `seam_notch_angle` | Seam notch angle: *Max angle* | `250`° | No notch when the angle of the perimeter at the seam is above this (no room). 180 filters everything, 360 allows everything. |

Seam notch and the scarf seam both reshape the seam: setting both is refused (error when slicing), pick one.

**How it works.** `seam_notch()` in `GCode.cpp`, applied in `extrude_perimeters()` to external perimeter loops after the
seam is placed (and after the seam gap clipping). As SuperSlicer:
- round = more than 8 points and convex (SuperSlicer's roundness check is very loose: ellipses pass too);
- `notch_length = 2 * depth`; loops shorter than `4 * depth` are skipped; the depth is capped at half the width;
- the first and the last `notch_length` of the loop are cut off; the notch direction is the mean of the start and
  end directions, turned 90° towards the material; no notch when these directions differ too much (a seam in a sharp
  corner, e.g. a cube corner), when the angle exceeds `seam_notch_angle`, or when the notched points are not inside
  the material;
- if the cut part is straight enough it is replaced by three segments curving from the notched point back onto the
  loop (and the end symmetrically), with the flow reduced by the projected-length ratio times 0.5 / 0.75 / 0.9 at the
  start and 0.75 / 0.5 / 0.25 at the end, so a cavity is left.
Differences from SuperSlicer: it is written for PrusaSlicer's smooth paths; a loop with arcs (arc fitting on), an
overhang or bridge at the seam, or a scarf seam is left unchanged.

**How to test.** `fff_print_tests "[SeamNotch]"`: off by default; a cylinder gets a notch on every layer, starting and
ending 0.1-0.5 mm inside; angle 180 disables it; a cube's corner seams are not notched; round holes with *Round holes*;
refused with scarf seam. Manually: slice a cylinder with *Round perimeters* at 50% and zoom on the seam in the preview
(the start and end of the external perimeter dip inside); print it and compare the seam with notch off.

---

## 11. Brim ears and brim per object

**What it does.** From SuperSlicer (Print > Skirt and brim > Brim). All off by default, so stock output is unaffected.
They are object settings: they can also be set per object in the object list (right click > Add settings > Skirt and brim).

| Setting | GUI row: label | Default | Meaning |
|---|---|---|---|
| `brim_ears` | Brim ears: *Brim ears* | off | Only print the outer brim around the sharp corners of the model ("mouse ears"). |
| `brim_ears_max_angle` | Brim ears: *Max angle* | `125`° | Corners up to this angle get an ear. 0 = no brim at all, ~178 = everything but straight sections. |
| `brim_ears_detection_length` | Brim ears: *Detection radius* | `1` mm | The outline is simplified with this tolerance before looking for corners (so small details and round shapes don't count as corners). 0 = not simplified. |
| `brim_ears_pattern` | Brim ears: *Pattern* | concentric | Concentric: the brim loops cut to the ears. Rectilinear: a loop around each ear filled with lines. |
| `brim_per_object` | Brim per object | off | One brim per object (and per instance) instead of one brim for the plate: brims of objects close to each other are not merged, and each brim is printed with its object (with sequential printing, just before the object). Where two brims would overlap, the one made first wins, so a brim may be truncated if objects are too close. |

The brim width is PrusaSlicer's `brim_width` (measured from the brim separation gap; in SuperSlicer the width
includes the gap). Brim inside holes (`brim_inside_holes`) is not ported.

**How it works.** `Brim.cpp`:
- ears: `brim_ear_points()` simplifies the outer contour of the first layer (offset by the brim separation) and keeps
  the convex corners not wider than the max angle (SuperSlicer's `convex_points`); `top_level_outer_brim_area()` then
  keeps the brim area only inside discs of radius `brim_width - one line spacing` around them. Concentric: the usual
  brim loops are clipped to that area. Rectilinear: the area is filled by `emit_rectilinear_ears()` (one loop, then
  rectilinear lines at 100%).
- per object: `make_brim()` first makes the plate brim for the objects without `brim_per_object` (exactly as stock),
  then one brim per instance of each object with it, its loops grown from that instance only and clipped by what is
  already used. `Print::brim_owners()` records the object and instance of each brim entity; in `GCode.cpp`
  (`get_sorted_extrusions()`) the shared brim is printed with the first layer as stock, and an object's brim when its
  instance is printed for the first time.
- Limitation: the inner brim (`brim_type` = inner / outer and inner) of an object with `brim_per_object` stays with the
  plate brim, printed at the start.

**How to test.** `fff_print_tests "[BrimSS]"`: ears only near the corners of a cube for both patterns, all four corners,
less brim than a full brim; max angle 0 / 80° gives no brim on a cube and 100° gives ears; two close objects get
merged brim loops with the plate brim but not with brim per object; with sequential printing, the second object's
brim is printed after the first object; ears and per object together. Manually: slice a part with sharp corners with
ears on and look at the first layer in the preview; place two parts a few mm apart with a wide brim and compare brim
per object on and off.

---

## 12. Auxiliary fan (from OrcaSlicer)

**What it does.** Drives an auxiliary part cooling fan (for example a side or chamber blower) as OrcaSlicer does: off
for the layers with the fan disabled, then one speed per filament for the rest of the print, off at the end. The fan
command is set in the printer settings instead of OrcaSlicer's fixed `M106 P2`, so it works with Klipper as is.

| Setting | Where | Default | Meaning |
|---|---|---|---|
| `auxiliary_fan_gcode` | Printer > General > Cooling fan > Auxiliary fan G-code | empty | G-code setting the aux fan speed; `{aux_fan_speed}` = speed in % (0-100). Empty = no aux fan. |
| `additional_cooling_fan_speed` | Filament > Cooling > Auxiliary fan speed (per filament) | `0` | % (OrcaSlicer name). Greyed out while the printer has no aux fan G-code. |

Examples for the G-code:

```
SET_FAN_SPEED FAN=aux SPEED={aux_fan_speed/100.0}      ; Klipper [fan_generic aux]
M106 P2 S{int(aux_fan_speed*2.55+0.5)}                 ; OrcaSlicer / Bambu style
```

Write `100.0`, not `100`: in the macro language a division of two integers is an integer division (`60/100` = 0).
Custom G-code can also use `{additional_cooling_fan_speed[...]}` and `{max_additional_fan}` (highest aux fan speed of
the filaments used), for example in the start G-code.

**How it works.** Same rules as OrcaSlicer (checked against OrcaSlicer's source: `CoolingBuffer.cpp`
`change_extruder_set_fan`, `GCode.cpp` start / end): the cooling buffer sets the aux fan at the start of every layer and
at every filament change, and only writes it when the speed changes. The speed is 0 while the layer is below
`disable_fan_first_layers` (the value as set), otherwise the filament's `additional_cooling_fan_speed`. The layer time,
the `full_fan_speed_layer` ramp and the feature fan speeds don't change it. It is written again after custom tool change
G-code (which may have changed it), off before the start G-code when the first layers have the fan disabled, and off
after the print, before the end G-code. The G-code is rendered for every speed it may take before the layers are
processed (`GCodeGenerator::_do_export`). The fan mover never moves it: an `M106` / `M107` with a fan index (`P`) other
than 0 is not the part cooling fan for it. The G-code viewer only shows the part cooling fan (it ignores `M106 P...` and
`SET_FAN_SPEED`). Not ported: OrcaSlicer's aux fan at 100% while waiting for the chamber temperature, and the fan
direction used by its auto orientation.

**How to test.** `fff_print_tests "[AuxFan]"`. Manually: set the G-code, set a filament speed, slice and search the
G-code for the command: off at the start, on once at layer `disable_fan_first_layers` + 1, off at the end.

---

## 13. Infill directions (from OrcaSlicer)

**What it does.** A separate angle for the solid infill, an option to stop the solid infill turning 90° every layer,
and an option to turn the infill with the object when the object is rotated on the bed (OrcaSlicer's "Align directions
to model"), so the infill keeps its direction relative to the part.

| Setting | Where | Default | Meaning |
|---|---|---|---|
| `solid_infill_direction` | Print > Infill > Advanced | `-1` | Degrees. Angle of the solid, top, bottom infill and ironing. -1 = `fill_angle` (OrcaSlicer: 45, its fill angle default). |
| `rotate_solid_infill_direction` | same | on | Off: the solid infill (and ironing) keeps the same angle on every layer. |
| `align_infill_direction_to_model` | same | off | Add the object's rotation around Z on the bed to the infill angles, the ironing angle and the bridging angle override (`bridge_angle`). |

**How it works.** As in OrcaSlicer (checked against its source: `Fill.cpp` group_fills / make_ironing,
`LayerRegion.cpp` bridges): the sparse infill (`Internal infill`) uses `fill_angle`, everything else
`solid_infill_direction`. "Rotate" off sets a `fixed_angle` flag on the filler, which skips the 90° layer alternation in
`Fill::_infill_direction` (patterns that never alternate are unchanged). The object rotation is
`PrintObject::z_rotation()`, from the object's transformation (PrusaSlicer slices every rotation of an object as its own
print object). Automatically detected bridge directions are not changed (they follow the geometry), only the
`bridge_angle` override is turned. Not ported: OrcaSlicer's rotation templates (`*_rotate_template`), top / bottom
layer directions and its internal bridge angle setting.

**How to test.** `fff_print_tests "[InfillDirection]"` measures the main direction of the extrusions per type and layer.
Manually: rotate a part 30° on the bed, slice with and without "Align directions to model", compare the infill in the
preview.

---

## 14. GUI layout

Related fields share one row with short labels, as in SuperSlicer (`resources/ui_layout/default/*.ui` of SuperSlicer),
built with `append_labelled_line()` in `GUI/Tab.cpp`. PrusaSlicer's pages and groups are kept (options are not moved
between pages); only the rows inside a group are combined:
- Print: Layers and perimeters (extra perimeters, avoid crossing, seam, seam notch, scarf joint, fuzzy skin, only one
  perimeter), Infill (sparse infill, anchor, solid patterns, ironing, infill angles), Skirt and brim (brim width and
  gap, brim ears), Support material (raft, contact Z, pattern, interface, organic branches), Speed (all), Multiple
  extruders (wipe tower, segmented regions, interlocking), Advanced (extrusion widths, Arachne).
- Filament: Cooling (fan speeds, disable fan / full speed layer, dynamic fan speeds, layer time thresholds),
  Advanced (max infill speeds, shrinkage, toolchange parameters, ramming, flush).
- Printer: General (Z, firmware supports, extruder clearance, fan speedup), Extruder (layer height limits, retraction
  speed, wipe, tool change retraction).
Supporting GUI changes (`OG_CustomCtrl`): `Line::force_sublabels` shows a short label even for a single field; the
lines with several fields of a group share columns (`OG_CustomCtrl::columns()`: each column is as wide as its widest
sub-label, field and side text in the group), so the fields are aligned; sub-labels are right aligned up to the lock
icon (over the slot of the search highlight arrow) and neither sub-labels nor side texts wrap. Lines with a single
field are laid out as stock. Not regrouped: the machine limits page, the filament overrides page and the custom G-code
pages.
---

## Testing

- Build and run all tests: see CLAUDE.md. `ctest -C Release --output-on-failure` in `build\`, or a single test program,
  e.g. `build\tests\fff_print\Release\fff_print_tests.exe "[FanMover]"` (run it with `build\` as working directory, the
  slicing tests leave temporary G-code files in it).
- **Golden test** (`fff_print_tests "[Golden]"`): slices 4 models with PrusaSlicer defaults and with a Klipper config, and
  compares with the stock PrusaSlicer 2.9.6 G-code in `tests/data/golden`. It strips the header line and the keys added
  by this fork (list `added_config_keys` in `tests/fff_print/test_golden_gcode.cpp`, add new keys there) and maps
  `Internal bridge infill` back to `Bridge infill`. Regenerate the reference only from an unmodified stock build:
  `PS_UPDATE_GOLDEN=1`.
- Every feature has a tag listed in its section above.
