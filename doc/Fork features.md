# Fork features (PrusaSlicer 2.9.6 + SuperSlicer ports)

This branch (`claude/poc-per-feature-2.9.6`) is PrusaSlicer 2.9.6 with a small set of features ported from
SuperSlicer 2.7 and OrcaSlicer (and a few improvements). This document lists every feature added on top of stock PrusaSlicer:
what it does, its settings, how it works and how to test it. **Update it in the same commit as any new feature.**

General rules followed by every feature:

- **Defaults keep stock output.** With every new setting at its default, the G-code is byte for byte identical to stock
  PrusaSlicer 2.9.6 (only the new keys in the config block at the end of the file, and the `Internal bridge infill`
  type name, differ; with the Klipper flavor also the estimated times, see section 15). This is checked by the golden test, see [Testing](#testing).
  **One exception:** `internal_bridge_shortest_span` (section 20) is on by default (user's choice), so internal bridges
  over sparse infill can take a different direction than stock. Turn it off for stock output; the golden test does.
  **Second exception:** `overhangs_hysteresis` (section 39) is 15 % by default (user's choice, 2026-10-24), so perimeters
  along the overhang threshold don't flip between overhang and perimeter as in stock. 0 = stock; the golden test sets 0.
  Also different from stock, as a fix and not a setting: Arachne with "Single perimeter on top surfaces" = "All top
  surfaces" (section 26). That option is off by default, so the golden test is unaffected.
  Also different from stock when switched on: the dynamic overhang speeds and fan speeds now work as in SuperSlicer
  (section 27). Both are off by default, so stock output is unaffected.
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
15. [Klipper limits for the time estimate](#15-klipper-limits-for-the-time-estimate)
16. [Layer height variables for custom G-code](#16-layer-height-variables-for-custom-g-code)
17. [Small area flow compensation](#17-small-area-flow-compensation)
18. [No chamber temperature commands for Klipper](#18-no-chamber-temperature-commands-for-klipper)
19. [Thumbnails at the end of the file](#19-thumbnails-at-the-end-of-the-file)
20. [Internal bridge direction: shortest spans](#20-internal-bridge-direction-shortest-spans)
21. [Seam placer cache](#21-seam-placer-cache)
22. [Klipper speed modes](#22-klipper-speed-modes)
23. [Paint-on brim](#23-paint-on-brim)
24. [App name: SuperPSlicer](#24-app-name-superpslicer)
25. [Project copy at every G-code export](#25-project-copy-at-every-g-code-export)
26. [Arachne fix: thin walls with single perimeter on all top surfaces](#26-arachne-fix-thin-walls-with-single-perimeter-on-all-top-surfaces)
27. [Dynamic overhang speeds and fan speeds with graphs](#27-dynamic-overhang-speeds-and-fan-speeds-with-graphs)
28. [Overhang threshold](#28-overhang-threshold)
29. [G-code viewer: discrete values, selected layers](#29-g-code-viewer-discrete-values-selected-layers)
30. [Calibration prints](#30-calibration-prints)
31. [Own brim under the support](#31-own-brim-under-the-support)
32. [Slicing: no slicing while painting, delay after a change](#32-slicing-no-slicing-while-painting-delay-after-a-change)
33. [Delete all: new project or erase objects](#33-delete-all-new-project-or-erase-objects)
34. [Brim-aware arrange](#34-brim-aware-arrange)
35. [Seam attractors](#35-seam-attractors)
36. [Open the settings of a G-code](#36-open-the-settings-of-a-g-code)
37. [Stock bug fixes](#37-stock-bug-fixes)
38. [Per region G-code](#38-per-region-g-code)
40. [The other form of a value or percentage next to the field](#40-the-other-form-of-a-value-or-percentage-next-to-the-field)
25. [Testing](#testing)

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
- The dynamic overhang fan speeds (section 27), when on, replace `overhangs_fan_speed` and apply on top of any feature fan speed.
- When `default_fan_speed` is set, it replaces "Keep fan always on" + "Min fan speed" (greyed out in the GUI).
- "Max" fan speed is only the target of the short layer fan speed-up: greyed out unless auto cooling is on and "Enable fan
  if layer print time is below" is above 0 (without a default fan speed also when "Slow down if layer print time is below" is).
- The summary under Filament > Cooling > Enable (`GUI/PresetHints.cpp`) describes this logic: the default fan speed,
  the features with a fan speed of their own, and whether `bridge_fan_speed` has any effect (it doesn't when it is not
  above the base fan speed, e.g. bridges 45 % with a default of 60 % print at 60 %).

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
  then convex hull for objects near a zone), on every virtual bed. The hull is grown by the object's outer brim
  (separation + width) and the skirt (distance + loops x nozzle diameter), as slicing refuses those in a zone too;
  supports can't be known before slicing, so an object shown as fine can still be refused for its supports. The first 8
  zones are tinted on the objects (slicing checks all of them).
- Bed drawing (`GUI/3DBed.cpp`, `render_keep_out()`): every zone (as entered, without the clearance, cut to the bed)
  is drawn over the bed in the 3D view and the preview, on every virtual bed, over the plain bed, a custom texture or a
  system bed: a grey fill (42 % opacity), 45° hatch lines every 5 mm (0.35 mm wide) and a 0.7 mm outline. The
  widths are in mm (thin triangle strips, not GL lines), so they scale with the zoom like a texture does, and the overlay
  lies in the bed plane with a polygon offset, so it does not flicker against the bed when the view moves. Not drawn
  in G-code thumbnails or when looking at the bed from below. A bed texture no longer needs the zones painted in.
- Arrange and fill bed: the zones are fixed obstacles on every bed (`arrange-wrapper/SceneBuilder.cpp`). They are
  tagged `is_keep_out`, so the arrange kernel only avoids them: they don't pull objects towards them as other fixed
  items do, and the first object still goes to the bed center when it stays clear of them
  (`NFP/Kernels/KernelUtils.hpp`, `TMArrangeKernel.hpp`). Stock re-centers the arranged pile only on a bed without fixed
  items, so with zones the pile stayed where the packing put it (4 round parts ~40 mm left of center). When the zones are
  the only fixed items, the pile is now moved towards the bed center as far as every object stays on the bed and clear of
  the zones, so it is centered as without zones whenever that fits (`NFP/RectangleOverfitPackingStrategy.hpp`; default
  "Auto" arrange strategy, "Pull to center" is not re-centered, as stock).
- Limits: moves written inside custom G-code (purge lines, macros) are not checked or rerouted.

**How to test.** `fff_print_tests "[KeepOut]"` and `arrange_tests "[KeepOut]"`. Manually: set the zones, check the
tint on the bed, drag an object into a corner (outside color, slicing refused), press A with many objects (corners stay
empty), press A with one object (it goes to the bed center, not between the zones), press A with 4 copies of a round part (the group is centered on the bed), check the zones are drawn hatched on the bed
in the 3D view and the preview and follow edits of the setting, slice objects near a corner and check in the preview (travel moves visible) that travels go around.

---

## 4. Per-feature accelerations and brim / skirt speed

**What it does.** Own accelerations for gap fill, support, support interface, ironing, and brim + skirt, and an own
speed for the brim and the skirt. All in Print > Speed (rows "Support speed", "Support acceleration", "Other
acceleration"). On the first layer, the first layer acceleration and speed still take precedence.

| Setting | Default | Meaning (mm/s² or %) |
|---|---|---|
| `gap_fill_acceleration` | `0` | % of the default acceleration. 0 = default acceleration (stock). |
| `support_material_acceleration` | `0` | % of the default acceleration. 0 = default acceleration. |
| `support_material_interface_acceleration` | `0` | % of the default acceleration. 0 = support acceleration. |
| `ironing_acceleration` | `0` | % of the default acceleration. 0 = stock (solid infill acceleration). |
| `brim_acceleration` (brim and skirt) | `0` | % of the default acceleration. 0 = default acceleration. |
| `brim_speed` (brim and skirt) | `0` | mm/s or % of the support material speed. 0 = support material speed (stock). |

Note: in SuperSlicer, 0 for gap fill / ironing / brim acceleration means "same as perimeter / top solid / support", and
their percentages are of those parents. Since 2026-10-24 (user: every percentage of the default acceleration) a
percentage here is of the default acceleration for all of them (before: the SuperSlicer parents). The profile converter
and the settings import (section 36) write the parent's value for a SuperSlicer 0 (before: `100%`) and convert
SuperSlicer percentages to mm/s². A profile with `100%` from the old converter: 100 % of the default acceleration now
(the user's `TPU .2`: the same values, its parents are all its default acceleration).

**The stock accelerations in % (2026-10-24, user).** Perimeters, external perimeters, infill, solid infill, top solid
infill, bridge, internal bridge, first layer, first object layer over raft, wipe tower, travel and travel short distance
acceleration take mm/s² or a percentage of the default acceleration (they were mm/s² only), e.g. external perimeters 50 %
with a default acceleration of 5000 = 2500. 0 keeps its meaning (disabled / the parent value), so existing profiles and
the G-code at the defaults are unchanged. The fork's own five above too (same date). Code:
`GCodeGenerator::acceleration()` (`GCode.hpp`), used by the acceleration chain in `_extrude()`, `feature_acceleration()`
and the travel; the wipe tower (`WipeTowerIntegration.cpp`) and the support spots search (`PrintObject.cpp`) resolve it
the same way. Settings import from a G-code (section 36): a SuperSlicer first layer acceleration in % (of the feature's
acceleration there) is no longer passed through as a % of the default acceleration. A profile with a % value doesn't
load in stock PrusaSlicer.

**Default acceleration 0 = no acceleration at all (as SuperSlicer).** With `default_acceleration = 0` no acceleration
command is written, the travel acceleration included, so the firmware's own value (Klipper `max_accel`) is used for
everything. Stock PrusaSlicer already ignored the per-feature and first layer accelerations then, but still wrote
`travel_acceleration` / `travel_short_distance_acceleration` (and with Klipper `M204 S` is the print acceleration too).
Those two are now greyed out with the others. Stock output is unchanged at PrusaSlicer defaults (travel acceleration 0).

**Default acceleration in % of the Klipper max accel** (user, 2026-10-30). `default_acceleration` takes mm/s² or a
percentage of `klipper_max_accel` (Printer Settings > Machine limits, Klipper's `max_accel`; SuperSlicer takes the
percentage of the max X acceleration instead, its values are still converted to mm/s² on import). The accelerations given
as a percentage of the default one use its value in mm/s² (`default_acceleration_value()` in `PrintConfig.hpp`, used by
the G-code generator, the wipe tower and the overhang perimeter detection; custom G-code placeholders and the value hints
resolve the chain through `ratio_over`). The field shows the value in mm/s² next to it like the other % fields (section
40), e.g. `[50%] mm/s² or %  (14000 mm/s²)`. 0 is still "no acceleration at all"; changing the Klipper max accel only
regenerates the G-code. In the aligned rows of the acceleration page the hint has room for 5 digits (an acceleration
like `(18200 mm/s²)` wrapped onto two lines in the room of 4).

**How it works.** `GCodeGenerator::feature_acceleration()` and `brim_speed()` in `GCode.cpp`, checked before the stock
acceleration chain in `_extrude()`. Travel: `generate_travel_gcode()`.

**How to test.** `fff_print_tests "[FeatureAccel]"` (also "Stock feature accelerations in % of the default acceleration":
the same G-code as with the values in mm/s²; "Default acceleration in % of the Klipper max accel": 50 % of 10000 gives
the same G-code as 5000, the % of the default ones follow). Manually: set distinct values, slice a supported model with
ironing and a skirt, and check the `M204` values per `;TYPE:` block in the G-code. Set the default acceleration to 0 with
a travel acceleration set: the G-code has no `M204` at all.

---

## 5. Fan mover (fan startup delay)

**What it does.** Starts fan speed increases earlier, so the fan is already spun up when the feature that needs it
starts (ported from SuperSlicer). Slowing the fan down is never moved.

| Setting | Where | Default | Meaning |
|---|---|---|---|
| `fan_speedup_time` | Printer > General > Cooling fan > Speedup time | `0` | Seconds. 0 = off. |
| `fan_speedup_overhangs` | same row: *Only for overhangs* | on | Only move the fan increases of overhang perimeters (greyed out while the delay is 0). |
| `fan_slowdown_min_time` | same row: *Min. time at lower speed* | `0` s | No slow down when the fan would speed up again within this time (fork, 2026-10-28). 0 = off. |

**Minimum time at a lower speed** (`fan_slowdown_min_time`, fork). Short overhangs (the top of a horizontal hole) set
the overhang fan for each small segment, with the normal fan in between: without the mover 0.02 s pulses, with a 2 s
startup delay still dips of 0.3-0.6 s that the fan can't follow; they only make the cooling uneven next to the hole.
With this time the fan stays at the higher speed across a dip shorter than it; with a startup delay the dip is measured
to the moment the fan starts speeding up. Only dips are erased: a speed up from a lower speed (0 -> 40 % -> 100 %) is
kept, and a longer stretch at the lower speed slows down as before. The buffer holds startup delay + minimum time, the
speed up is inserted into it instead of written out at once (`FanMover::speed_up_in_buffer()`); with 0 the code path
is the one above. The mover runs when either time is above 0. Default 0 = stock output; 3 s is the suggested value (the user's
printer profile).

**How it works.** `GCode/FanMover.cpp`, a filter after the cooling buffer. It estimates the time of each move from its
feed rate (ignoring acceleration), keeps a buffer of `fan_speedup_time` seconds and writes a fan increase that much
earlier, splitting a move when needed. Rules as in SuperSlicer: a fan command is never moved into the previous layer,
slower fan commands within the delay are dropped while speeding up, and fan commands inside custom G-code are not moved
(custom G-code is tagged `; custom gcode: <name>` / `; custom gcode end: <name>` while the mover is on). Custom G-code
blocks without fan commands are looked through, and their moves are never split. SuperSlicer's fan kickstart is not
ported (Klipper `[fan] kick_start_time` covers it). Fix against SuperSlicer (2026-10-28): when a slow down in between was
dropped, the fan is already at the new speed, so the increase is not written again (SuperSlicer wrote one duplicate
`M106` per dropped slow down; 690 of them on a user's 4-part plate).

**How to test.** `fff_print_tests "[FanMover]"`. Manually: set 2 s, slice a model with overhangs, compare the `M106`
positions with the delay at 0: the increases come earlier, the extrusion is identical. Minimum time: a part with a
horizontal hole and an overhang fan above the normal fan; G-code viewer "Fan speed" view: with 0 the hole layers
flicker between the two speeds beside the hole, with 3 s they stay at the overhang speed.

---

## 6. Custom G-code at every extrusion type change (`feature_gcode`)

**What it does.** Custom G-code inserted at every extrusion type change, before the first extrusion of the new type
(ported from SuperSlicer).

| Setting | Where | Default |
|---|---|---|
| `feature_gcode` | Printer > Custom G-code > "Between extrusion role change G-code" | empty |

Variables: `extrusion_role` (= `next_extrusion_role`, the new type), `last_extrusion_role` (= `previous_extrusion_role`),
`layer_num`, `layer_z`, `max_layer_z`, `current_layer_height` (section 16), plus all settings. Type names are the `;TYPE:` names: `Perimeter`,
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
  The islands are taken from the layer the internal boundary was built from (`m_internal_layer`): stock builds the
  boundary lazily from the layer current at the first travel, which at a layer change is still the previous layer, and
  the order of `Layer::lslices` changes from run to run. Mixing the two layers made the output differ between runs
  (fixed 2026-10-13: 60 slices of the test plate identical).
- speed (2026-10-23, same output): stock calls `init_layer()` again for every instance of an object and rebuilt the
  boundaries each time; the boundaries built from the same layer are now kept (`m_init_layer`, `m_external_layer`).
  The island search limits the nearest-point radius to the gap that could still beat the best crossing found so far.
  Plate of 6 objects with the user's profile: G-code export 7.8 s -> 5.8 s.

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
- Filament: Cooling (fan speeds, disable fan / full speed layer, dynamic overhang fan on the overhang fan line, layer time thresholds),
  Advanced (max infill speeds, shrinkage, toolchange parameters, ramming, flush).
- Printer: General (Z, firmware supports, extruder clearance, fan speedup), Extruder (layer height limits, retraction
  speed, wipe, tool change retraction).
Supporting GUI changes (`OG_CustomCtrl`): `Line::force_sublabels` shows a short label even for a single field; the
lines with several fields of a group share columns (`OG_CustomCtrl::columns()`: each column is as wide as its widest
sub-label, field and side text in the group), so the fields are aligned; sub-labels are right aligned up to the lock
icon (over the slot of the search highlight arrow) and neither sub-labels nor side texts wrap. Only the fields followed
by another field set the width of their column, so a wide last field (a combo box) doesn't push the next columns away.
Lines with a single field in a group with such lines have their field in the first column
(`OG_CustomCtrl::aligned_single_offset()`), elsewhere they are laid out as stock. Not regrouped: the machine limits page, the filament overrides page and the custom G-code
pages.
---

## 15. Klipper limits for the time estimate

**What it does.** With the Klipper G-code flavor, the time estimate (and the actual speeds in the G-code viewer) follow
Klipper's motion planning and the limits from `printer.cfg` instead of the Marlin style machine limits (per axis
feedrates, accelerations and jerk). Nothing is ever written to the G-code: PrusaSlicer already refuses "Emit to G-code"
with Klipper. Active when the flavor is Klipper and Printer > Machine limits > "How to apply limits" is
"Use for time estimate"; "Ignore" keeps the stock estimate. Stock PrusaSlicer defaults (other flavor) are unaffected,
and the G-code itself never changes (only the estimated times in it).

| Setting (printer, Machine limits page with Klipper) | Default | Klipper option |
|---|---|---|
| `klipper_max_velocity` | `500` mm/s | `[printer] max_velocity` |
| `klipper_max_accel` | `1500` mm/s² | `[printer] max_accel` |
| `klipper_minimum_cruise_ratio` | `0.5` | `[printer] minimum_cruise_ratio` (0 = off) |
| `klipper_square_corner_velocity` | `5` mm/s | `[printer] square_corner_velocity` |
| `klipper_max_z_velocity` | `12` mm/s | `[printer] max_z_velocity` |
| `klipper_max_z_accel` | `500` mm/s² | `[printer] max_z_accel` |
| `klipper_max_extrude_only_velocity` | `0` | `[extruder] max_extrude_only_velocity`; 0 = Klipper's default (max_velocity × 4 × nozzle² / filament area) |
| `klipper_max_extrude_only_accel` | `0` | `[extruder] max_extrude_only_accel`; 0 = Klipper's default (max_accel × the same ratio) |

**How it works.** `GCode/GCodeProcessor.cpp`, a port of Klipper's planner (`klippy/toolhead.py`, checked against
Klipper master c7d5724, 2026-10-12):

- Per move (`Move`, `corexy.check_move()`, `extruder.check_move()`): speed capped by max_velocity (toolhead speed, not
  per axis); moves with Z limited by the Z velocity / acceleration scaled by the Z share of the move; retractions
  (extrude only moves, and wipes with a negative E) by the extrude only limits.
- Corners (`Move.calc_junction()`): junction deviation from square_corner_velocity, the centripetal limit, and the
  extruder's `instantaneous_corner_velocity` (Klipper default 1 mm/s, not a setting) when the extrusion ratio changes,
  e.g. between a travel and an extrusion.
- Look-ahead (`LookAheadQueue.flush()`) including minimum_cruise_ratio, with a stop at the end of the queue; the queue
  is processed in chunks like the stock planner, the speeds at the chunk boundary are carried over.
- G-code: `M204 S` (or the smaller of P and T) replaces the acceleration, as in Klipper (not limited by max_accel);
  `SET_VELOCITY_LIMIT VELOCITY= ACCEL= MINIMUM_CRUISE_RATIO= SQUARE_CORNER_VELOCITY=` changes the limits. Commands
  hidden inside macros (e.g. `PRINT_START`) are not seen.
- Not modelled: input shaper smoothing, pressure advance, `max_extrude_cross_section`, firmware retraction.
- A G-code file opened in the viewer is estimated with the limits from its config block; files without the
  `klipper_*` keys get the stock estimate.

Profile converter: the AWD Voron printer preset gets max_velocity 500, max_accel 20000, Z 20 mm/s and 300 mm/s²,
cruise ratio 0.5, square corner velocity 5, and "Use for time estimate".

**How to test.** `fff_print_tests "[KlipperLimits]"`: single moves, M204, SET_VELOCITY_LIMIT, Z moves, the cruise ratio
and a 90° corner against times computed from Klipper's formulas. Manually: slice with the Klipper flavor, change the
limits and check the estimate and the speeds in the preview ("Actual speed" view).

---

## 16. Layer height variables for custom G-code

**What it does.** Two new variables for custom G-code (fork only, not in SuperSlicer). Stock `layer_height` is the print
setting: it doesn't change with variable layer height, support layers or the first layer. These give the real values,
the same ones the G-code viewer shows.

| Variable | Available in | Value |
|---|---|---|
| `current_layer_height` | before / after layer change, `feature_gcode`, toolchange, start / end filament, end G-code | Z of the current layer minus Z of the layer printed before it (first layer: its Z). Same number as the `;HEIGHT:` line at the layer change. |
| `total_print_height` | every custom G-code, including the start G-code | Z of the top of the highest layer of the print (objects, supports, raft) = `layer_z` of the last layer. |

Layer numbers (stock variables, unchanged): `layer_num` (0 based) and `total_layer_count` count every distinct Z, support
only layers included, so `{layer_num+1}` / `[total_layer_count]` is the viewer's layer slider number. Variable layer
height just gives more layers where they are thin. Stock PrusaSlicer does not run the layer change G-code on the first
layer: for Klipper's layer display put `SET_PRINT_STATS_INFO CURRENT_LAYER=1` in the start G-code (after `TOTAL_LAYER`).
The extrusion thickness of a single line (the viewer's "Layer height" colours) can differ inside one layer (support
lines spanning several object layers), so it is not a per layer variable. In sequential printing, `total_print_height`
is the height of the tallest object.

Example (after layer change G-code):

```
M117 Layer {layer_num+1}/[total_layer_count] Z {layer_z}/{total_print_height} h {current_layer_height}
SET_PRINT_STATS_INFO CURRENT_LAYER={layer_num+1}
```

**Your own variables** (SuperSlicer `print_custom_variables` / `filament_custom_variables` / `printer_custom_variables`):
not ported, stock PrusaSlicer 2.9.6 does the same with "Custom parameters" (Expert mode): Print > Advanced, Filament >
Custom G-code, Printer > Custom G-code. Written as single level JSON, e.g. `{"soak": 10, "chamber": true}`, used with a
prefix: `{custom_parameter_print_soak}`, `{custom_parameter_printer_...}`, `{custom_parameter_filament_soak[current_extruder]}`
(one value per filament). Invalid JSON, or the same filament key with different types, refuses to slice.

**How it works.** `GCode.cpp`: `m_current_layer_height` is set in `process_layer()` with the `;HEIGHT:` value and passed
wherever `layer_z` is passed; `total_print_height` is set with `total_layer_count` before the start G-code. Both are
listed in the custom G-code editor. G-code with them unused is unchanged.

**How to test.** `fff_print_tests "[LayerPlaceholders]"`: a cube with a variable layer height profile and an overhang
with supports on their own heights; every layer change checks `current_layer_height` against `;HEIGHT:`, `layer_z`
against `;Z:`, `layer_num+1` against the count of `;LAYER_CHANGE`, and the totals against the last layer.

---

## 17. Small area flow compensation

**What it does.** Reduces the flow of short solid infill lines, which come out over-extruded in small areas (ported from
SuperSlicer 2.7.63 / OrcaSlicer, after Alexander Moss' Small-Area-Flow-Comp script). Every straight segment of solid
infill and top solid infill gets its extrusion multiplied by a flow factor read from a curve of segment length → factor.
Segments longer than the last point (10 mm by default) are not changed. Not applied on the first layer (adhesion,
as SuperSlicer), nor on top surfaces whose pattern is not rectilinear, aligned rectilinear, monotonic or monotonic lines
(curved patterns are made of many short segments, as OrcaSlicer). Internal solid infill is PrusaSlicer's "ensuring"
straight line pattern and is always included. The short connections between solid infill lines are reduced too (same
in SuperSlicer and OrcaSlicer).

| Setting | Where | Default |
|---|---|---|
| `small_area_infill_flow_compensation` | Print > Infill > Small area flow compensation (per object / modifier) | off |
| `small_area_infill_flow_compensation_model` | same, text, one `length, factor` pair per line (mm, 0-1) | SuperSlicer's curve: `0,0` `0.2,0.44` `0.4,0.61` `0.6,0.7` `0.8,0.76` `1.5,0.86` `2,0.89` `3,0.92` `5,0.95` `10,1` |

Rules (as OrcaSlicer; slicing refuses a model breaking them, with a message): at least 2 points, first length 0, lengths
and factors strictly increasing, factors not negative, last factor 1. The curve between the points is OrcaSlicer's
PCHIP (smooth, never overshoots between the points). **"Edit graph…"** opens a dialog with the curve as used for
slicing and the points as text: drag a point to move it, double click to add one, right click to remove one; the text
and the graph update each other, OK writes the text back into the setting.

G-code viewer: the "Width" view shows the planned width (`;WIDTH:` lines) and does not change; the "Volumetric flow
rate" and "Actual volumetric flow rate" views and the hover info show the reduced segments.

Profile converter: SuperSlicer's graph value (`[!]0:10:2:0x0:0.2x0.44:…`, `!` = disabled) becomes the switch (on unless
`!`) and the same points.

**How it works.** `GCode/SmallAreaFlowCompensation.cpp` parses and checks the text and interpolates (shared with the
dialog `GUI/SmallAreaFlowDialog.cpp`). `GCodeGenerator::_extrude()` multiplies the E of each line and arc segment by
the factor of its length (G-code coordinates); `small_area_flow_model()` decides per extrusion (role, first layer,
top pattern from the region config) and caches the parsed model. `Print::validate()` checks the model of every region
with the switch on.

**How to test.** `fff_print_tests "[SmallAreaFlow]"`: the curve (through the points, monotonic, 1 beyond the last
point, text round trip), invalid models refused, and a cube sliced with and without: same moves, every solid / top solid
segment after the first layer has E × factor(length), everything else unchanged, some segments reduced; a concentric
top surface is not changed. Manually: enable it, slice a part with small solid areas and look at the volumetric flow
rate view.

---

## 18. No chamber temperature commands for Klipper

**What it does.** With `gcode_flavor = klipper`, the chamber temperature (`chamber_temperature`,
`chamber_minimal_temperature`, Filament > Temperature) is no longer written as `M141` / `M191`: Klipper has no such
commands and answers `Unknown command:"M141"`. As SuperSlicer, which writes them only for Marlin 2, RepRap and
Machinekit. Other flavors are as stock. No setting.

Pass the value to the start macro instead, e.g. `PRINT_START ... CHAMBER={chamber_temperature[initial_extruder]}`
(the placeholder is unchanged). If the printer has its own `M141` macro, write it in the start G-code.

**How it works.** `GCodeWriter::set_chamber_temperature()` returns nothing for Klipper; the start G-code logic
(`_print_first_layer_chamber_temperature()`) is unchanged.

**How to test.** `fff_print_tests "[ChamberTemperature]"`: a cube with chamber temperatures set has no `M141` / `M191`
with Klipper, both with Marlin 2.

---

## 19. Thumbnails at the end of the file

**What it does.** From SuperSlicer. `thumbnails_end_file` (printer, Printer > General > Firmware, default off = stock)
writes the G-code thumbnails at the end of an ASCII G-code file, after the configuration block, instead of the front.
Klipper reports the print progress from the file position until the first `M73`; with the thumbnails at the front
(about 20 KB, 2% of a 1 MB file) Mainsail shows 2% before the printer has even homed. Moonraker / Mainsail find the
thumbnails at the end too; other printer screens may expect them at the front. Greyed out with binary G-code (the
thumbnails have their own blocks there). The bundle converter carries the SuperSlicer value over.

**How it works.** `GCodeGenerator::_do_export()` still checks the thumbnail list at the front (errors are reported the
same), skips writing them there and writes them after `; prusaslicer_config = end`. Loading the config from a G-code reads
back from the end up to that line, so the thumbnails after it don't disturb it.

**How to test.** `fff_print_tests "[ThumbnailsEndFile]"`: with a test thumbnail callback, the thumbnail is before the first
move by default, and once after the config block with the option on.

---

## 20. Internal bridge direction: shortest spans

**What it does.** Fix for PrusaSlicer issue #14957 ("Bridge over infill chose the worst possible angle").
`internal_bridge_shortest_span` (print, Print > Infill > Advanced, "Infill angle modifiers" line, **default on**, the only
fork option whose default changes stock output; off = stock)
chooses the direction of the internal bridges over sparse infill by the real unsupported spans between the infill lines
of the layer below. Stock PrusaSlicer looks at the nearest infill segment at points every 2 mm along the bridged area's
outline and bridges perpendicular to the most common direction. With wavy infill (gyroid) the local segment directions
don't show which way the lines run, and the bridge can end up along the infill lines; it can also go wrong with straight
infill on some shapes. Not changed: external bridges, the `bridge_angle` override (it applies to external bridges), internal
bridges without sparse infill lines below (e.g. lightning areas without lines, 0 % infill), which keep the stock fallback.

**How it works.** `PrintObject::bridge_over_infill()`, after the stock angle (`determine_bridging_angle`): for the stock
angle and for every 5° (0-175°), parallel scan lines (2x the bridge line spacing apart) are clipped to the bridged area and
split at every crossing with the anchor lines (the stock infill lines of the layer below, clipped to the expansion area).
The cost is the sum of the squared spans, i.e. the length-weighted mean span (the total length is the same for every angle).
The stock angle is kept unless another angle costs at least 10 % less, so straight-line infill usually keeps it exactly.
The chosen angle is then used like the stock one (also for the anchored expansion and for neighbouring bridged areas).

Measured with the issue's project (5 % gyroid, length-weighted mean span over the bridge layer): 29.0 mm stock, 12.7 mm
with the option (longest span 39.5 -> 26.5 mm). Same box with 15 % infill, stock -> option: rectilinear 5.0 -> 2.9 mm
(longest 37 -> 6.9 mm), grid 5.9 -> 5.1, stars 6.6 -> 6.1, cubic 6.3 -> 5.7, adaptive cubic 9.3 -> 7.3, triangles
6.0 -> 5.5, lightning 11.2 -> 8.6, 3D honeycomb 3.8 -> 3.0; line, honeycomb and aligned rectilinear unchanged. Never worse.
On a 20 mm cube with 15 % infill, the straight-line patterns give byte-identical G-code with the option on.

**How to test.** `fff_print_tests "[InfillDirection]"`: the option is on by default; on a 20 mm cube with 5 % gyroid the
mean span with the option is at least 15 % shorter (9.5 -> 7.6 mm); with rectilinear infill the internal bridge angle is
unchanged. The golden test turns it off (`with_stock_behaviour()` in `test_golden_gcode.cpp`; add any other option that is
on by default and changes the output there).

---

## 21. Seam placer cache

**What it does.** Faster G-code export after edits that don't change the geometry (fan, speed, acceleration, temperature,
custom G-code...): the seam placer (seam visibility ray casting, aligned seams) is kept from the last export and reused.
No setting, the G-code is unchanged. Measured on the box from PrusaSlicer issue #14957: export after a fan change
770 ms -> 510 ms. The first export after slicing takes as long as before.

**How it works.** `Print` keeps the last `Seams::Placer` (`Print::seam_placer_cache()`, cleared by `Print::clear()`).
`GCodeGenerator::_do_export()` builds a key (`seam_placer_cache_key()` in `GCode.cpp`) from everything
`Seams::Placer::init()` reads: per object the step timestamps (any re-slice or new perimeters changes them; the external
perimeter width it reads invalidates the perimeters), `seam_position`, the object transformation and the seam painting
timestamp of every volume (seam painting and `seam_position` only invalidate the G-code export), plus
`staggered_inner_seams`, `elefant_foot_compensation` and `raft_layers`. Same key: reuse, otherwise compute as stock.
Scarf seam and other region settings are read when a seam is placed, not cached.

**How to test.** `fff_print_tests "[SeamPlacerCache]"`: after a fan / speed change the placer is reused and the G-code
equals a fresh slice; changing `seam_position`, the seam painting, `staggered_inner_seams` or the external perimeter width
rebuilds it (and the G-code equals a fresh slice).

---

## 22. Klipper speed modes

**What it does.** For a printer with a speed mode macro (e.g. `SPEED_MODE TOGGLE=S|N|F` that changes max velocity and
max accel with `SET_VELOCITY_LIMIT`): the filament selects the mode (its fans and temperatures are tuned for it), the
fork writes the printer's macro into the G-code, and the time estimate (also the one Mainsail shows, read from
`; estimated printing time (normal mode)`) uses that mode's limits instead of always the normal ones. One printer
profile serves all the modes; one field per filament.

| Setting | Where | Default | Meaning |
|---|---|---|---|
| `klipper_max_velocity_slow` / `_fast` | Printer > Machine limits (Klipper), Slow / Fast columns | `75%` / `125%` | Absolute, or a percentage of the normal value (the existing `klipper_max_velocity`) |
| `klipper_max_accel_slow` / `_fast` | same | `75%` / `125%` | same, of `klipper_max_accel` |
| `klipper_speed_mode_gcode` | Printer > Machine limits (Klipper), below the limits | empty | The printer's macro, e.g. `SPEED_MODE TOGGLE={speed_mode}` |
| `klipper_speed_mode` | Filament > Cooling, "Klipper speed mode" (per filament) | Off | Off / Slow / Normal / Fast |

Only max velocity and max accel have slow / fast values (user's choice); minimum cruise ratio, square corner velocity,
the Z and the extrude only limits are the same in every mode. Placeholder `{speed_mode}`: `S`, `N` or `F`, empty when
Off; usable in every custom G-code (start G-code, filament G-code...). With several extruders the filament of the first
extruder decides. The "(normal mode)" in the estimate line is PrusaSlicer's name for its single estimate (its second,
"silent mode", exists only for Marlin printers with stealth mode); it is unchanged.

Off (default): nothing is written and the normal limits are used, as before. A mode with an empty macro: nothing is
written, but the estimate uses the selected mode. A filament start G-code that also calls the macro runs before the
fork's macro, so the selected mode wins; such calls can be removed once the filament selects the mode.

**How it works.** The macro is processed by the placeholder parser and written once in `GCodeGenerator::_do_export()`,
after the start G-code and the initial tool change (which runs the filament start G-code), before the first layer.
`GCodeProcessor::apply_klipper_limits()` replaces the normal max velocity and max accel by the selected mode's
(percentages of the normal ones); `SET_VELOCITY_LIMIT` lines in the G-code still change them afterwards. The settings
are in the G-code's config block, so a reloaded G-code gets the same estimate.

**How to test.** `fff_print_tests "[KlipperSpeedMode]"`: a single move against Klipper's formulas with the mode Off,
Normal, Slow (75 %), Fast (125 %) and absolute slow values; a corner move showing that square corner velocity and the
cruise ratio don't change; a `SET_VELOCITY_LIMIT` after it; the macro is written once with S / N / F after the start
G-code and before the first layer, nothing when Off or with an empty macro; `{speed_mode}` in the start G-code; a sliced
G-code with the slow mode has a longer estimate, and reloading it gives the same estimate. Manually: Printer > Machine
limits shows Slow | Normal | Fast for max velocity and max accel and the macro field; Filament > Cooling has the speed
mode; with Slow selected the sliced estimate goes up.

## 23. Paint-on brim

**What it does.** A brim only where you want it (PrusaSlicer request #8283), painted on the model like the support and
seam painting: a new left toolbar tool "Brim painting" (after Fuzzy skin painting).

- **Enforce brim** (left mouse button): a brim along the painted part of the bottom edge. Works also with the brim type
  "No brim", so a part can have a brim only at its corners or along one side.
- **Block brim** (right mouse button): no brim there, with any brim type (outer and inner brim).
- Shift + left mouse button removes the painting, "Remove all selection" clears it. Brush size as in the seam
  painting; the brush is always the circle (paints the facets facing you), the sphere shape was removed (2026-10-22).
- **Tool type: Edge** (default) or **Brush**. Edge: the bottom edge under the cursor is highlighted (yellow) from corner
  to corner; a click paints all of it (left enforce, right block, Shift remove). A corner is a turn sharper than the
  "Corner angle" (default 30 degrees), so curves and fillets count as one edge and a round outline is painted all around.
  Brush: the stock painting brush.
- "First layer only" (on by default): the part is cut at the first layer height (the only layer the brim uses), the
  rest is hidden and can't be painted; the cut is see-through; the brush paints only facets facing you, so look from
  below to paint the bottom face. Unticked: the whole part and the usual
  clipping of view slider.

Paint the bottom edge: the side walls near the bed or the bottom face. Painting higher up the walls does nothing (only
the facets within the first layer height count). The brim width, separation and the other brim settings (brim ears,
brim per object) come from the print settings as usual; the object list shows "Paint-on brim" (right click to remove,
double click to edit). The painting is saved in the 3mf project (attribute `slic3rpe:custom_brim`, ignored by stock
PrusaSlicer) and copied with the object.

The painting needs a brim width above 0 and no raft (as any brim): the tool's window says so when the part's brim
width is 0 or it has a raft. Brim width, brim separation and brim per object stay editable with the brim type
"No brim" (stock greys them out), so a painted-only brim can be set up without switching the brim type. Arrange leaves room for the brim of a part with brim enforcers, as for an outer brim.

No setting; nothing painted = stock brim (golden test unchanged).

**How it works.** `ModelVolume::brim_facets` holds the painting (like `seam_facets`), with undo / redo, 3mf and copy; cutting
the object or changing its mesh (repair, simplify...) removes it with the other paintings. `PrintObject::brim_paint_area()`
clips the painted facets at the top of the first layer and projects them to the bed (the walls as thin bands). In
`Brim.cpp`, `brim_paint_zones()` takes the parts of the first layer outline (contours offset, holes shrunk by the brim
separation) that lie in the painted area (grown by the elephant foot compensation + 0.1 mm) and shortens them at their
ends by a bit more, so the brim does not turn around the corner next to a painted wall. The zone is the brim nearer to
these parts than to the rest of the outline, up to the brim width + one line: ring by ring, one line apart, the band
around the painted parts reaching the ring minus what lies nearer to any outline than the previous ring. So a brim
between a painted and an unpainted wall is split halfway, and the other walls within the brim width keep their brim
(before 2026-10-21 the zone was a band of the brim width on both sides of the painted parts, and it also added or
removed the brim of nearby walls). The outer brim ring and the inner (hole) rings
are intersected with the enforcer zone where the brim type doesn't make them anyway, and the blocker zone is
subtracted; the one line wide "no brim" band around parts without a brim is opened where enforced. A hole that is
filled completely (the innermost hole) becomes a normal ring of the brim width when blocked, so the fill does not start
again from the blocked edge. A part with enforcers counts as having a brim (`PrintObject::has_brim()`). For the supports the brim area is the one of
the brim type plus the painted brim (`brim_paint_areas()`, Brim.hpp: the enforced rings) minus the blocked zones; see
section 31 (own brim under the support). The gizmo's first layer view is a horizontal clipping plane at the
first layer height (`GLGizmoBrimPaint::update_first_layer_clipping()`, set again whenever something else moved it, and
cleared when the gizmo closes since the clipper is shared with the other painting gizmos); `ObjectClipper::render_cut()`
takes the cut's alpha. `PrintObject::brim_paint_area()` keeps the edges (as thin bands) of the steep facets only and is cached on the object (key: first layer, placement, painting timestamps): a fine painting of 50 000 small facets took 12 s per call, 4 calls per slicing; now about 2 s once. The Edge tool (`GLGizmoBrimPaint::gizmo_event()`, `edge_under_mouse()`, `paint_edge()`) slices
the model parts at the middle of the first layer (cached outline), meets the mouse ray with that height, takes the
nearest outline segment within 2.5 mm and walks both ways while the turn at each vertex is under the corner angle; the
edge, shortened by 0.3 mm at its corners (not the next wall), is painted with 0.3 mm capsules (`select_patch()`, the
facets are split) along each segment, then one undo snapshot. Repainting only redoes the brim (and the supports of a part
that has supports), not the slicing.

**How to test.** `fff_print_tests "[BrimPaint]"`: nothing painted gives the stock brim; an enforcer on one side wall
of a cube with "No brim" gives a brim only in front of that wall, not longer than the wall and not wider than the brim;
an enforcer on the bottom face gives the brim all around; painting the top face does nothing; a blocker on one wall
with "Outer brim only" removes the brim there; a blocker on a wall facing another wall 4 mm away (same object) removes
only the half of the gap's brim nearest to it; with supports standing on the bed next to the part and the brim enforced
all around or with the outer brim, the supports have no first layer and every line of their second layer lies on brim
lines (without a brim they keep their first layer); blockers in a hole remove the inner brim; repainting keeps the slices
and only redoes the brim; the painting is saved and loaded with the 3mf. Edge tool, manually: hover a bottom edge, the
highlight stops at the corners (change the corner angle: a rounded rectangle becomes one edge at 90 degrees), click: only
that edge is painted (also from below and through the see-through cut), slice: brim only along it; undo. `arrange_tests "[BrimPaint]"`: arrange's brim offset counts the
enforcers. Manually: open Brim painting on a part,
paint two corners with "No brim", slice: brim only at those corners; set "Outer brim only", block one side: no brim
there; save and reopen the project.

## 24. App name: SuperPSlicer

**What it does.** The fork shows its own name, so it can't be mistaken for an official Prusa build: window title,
dialogs, menus, the About and System Information dialogs and the Windows file properties say "SuperPSlicer"
(`SuperPSlicer-2.9.6 based on Slic3r`; the full build ID `SuperPSlicer-2.9.6+fork` is in Help > System Info), the G-code viewer "SuperPSlicer G-code Viewer". The Windows programs
are `superpslicer.exe`, `superpslicer-console.exe` and `superpslicer-gcodeviewer.exe`. Online services (Prusa
Account, Printables, Prusa Connect, preset updates) see the fork as `SuperPSlicer/2.9.6` (HTTP user agent).
The app icon (2026-10-23) is SuperSlicer's glossy icon in PrusaSlicer's colours (its blue half recoloured to the orange
`#ed6b21`): `resources/icons/PrusaSlicer.ico` (exe, taskbar), `PrusaSlicer*.png` (About, wizard, window icon). The file
names stay, so no code changes; the G-code viewer keeps the stock icon.

Unchanged on purpose:

- **Settings folder** (`%APPDATA%\PrusaSlicer`) and translations: the fork shares profiles with stock PrusaSlicer.
- **G-code header** `; generated by PrusaSlicer 2.9.6`: hard-coded upstream, it is how Moonraker / Mainsail and the
  G-code viewer recognise the file (estimate, thumbnails, metadata). Golden test unchanged.
- The `prusaslicer://` link scheme (Prusa Account login, Printables downloads) and the Prusa Account client ID.
- Linux / macOS binary names; texts in the GUI that write "PrusaSlicer" literally (not through the app name).

Side effect: the Prusa Account login and physical printer passwords are saved in the Windows Credential Manager under
the app name (`SuperPSlicer/PrusaAccount/...`), so the fork asks to log in once and saved printer passwords must be
entered again; stock PrusaSlicer keeps its own.

**How it works.** `SLIC3R_APP_NAME` and `SLIC3R_BUILD_ID` in `version.inc` (`SLIC3R_APP_KEY` stays "PrusaSlicer": it
names the settings folder and translation catalog), `GCODEVIEWER_APP_NAME` in `libslic3r.h`. The exe names are the
`OUTPUT_NAME`s in `src/CMakeLists.txt` and the `OriginalFilename` in the `.rc` files; `MainFrame.cpp` (G-code viewer
icon) and `Utils/Process.cpp` (starting a new slicer or viewer) use the same names. `InstanceCheck.cpp` finds a
running instance by the app name in its window title (it was "PrusaSlicer"); this is what passes a `prusaslicer://`
link (login, Printables download) or a file to the open window.

**How to test.** Build; `build\src\Release` has the three `superpslicer*.exe`. Start `superpslicer.exe`: title
"SuperPSlicer-2.9.6 based on Slic3r", Help > About says SuperPSlicer. With it open, start it again with a
file and "Single instance" on in Preferences: the file opens in the running window. Window > G-code preview opens
`superpslicer-gcodeviewer.exe`. Log in to the Prusa Account / Printables tab. Sliced G-code still starts with
`; generated by PrusaSlicer 2.9.6`.

## 25. Project copy at every G-code export

**What it does.** Every Export G-code, Export all beds, Send to printer (print host) and Send to Prusa Connect also
saves the project as a 3mf into a local folder, so the exact objects and settings of every printed file can be
reopened later. Two kinds of copy, each with its own check box and folder, both can be on:

- **Backup** (on by default): named as the G-code file plus the date and time, `<G-code name>_YYYY-MM-DD_HH-MM-SS.3mf`.
  Only the newest backups are kept (10 by default).
- **Autosave** (off by default): named as the G-code file, `<G-code name>.3mf`, replacing the previous autosave with
  that name. Never deleted. Saved only once its folder is set; while the folder is empty, each export shows a warning
  notification instead.

For an upload the name is the file name entered in the send dialog. Export all beds / send all to Connect save one
copy per export, named after the first bed's file.

Opening a copy names the G-code as the original project did: the copy keeps the name the G-code was named after
(`{input_filename_base}`, normally the project's name). With the output file name
`{input_filename_base}_[filament_preset]_SuperSlicer`, project `Body1` exports `Body1_ABS_SuperSlicer.gcode` and the
autosave `Body1_ABS_SuperSlicer.3mf`; that autosave opened exports `Body1_ABS_SuperSlicer.gcode` again, not
`Body1_ABS_SuperSlicer_ABS_SuperSlicer.gcode` (stock names the G-code after the open project file). File > Save keeps that name, Save as with another name drops it
(the G-code is then named after the new project).
A name that already ends with what the file name template adds (copies saved before the name was kept, a project
saved under its G-code's name, the time stamp of a backup) is used without those additions, so the name never grows.

File > Recent projects shows the newest backups of the last 2 G-code names (one per name), so the backups don't push
your own projects out of the list: the two backup entries are two different projects, a backup of a third project
replaces the older of the two. Autosaves are normal entries: each one stays, exporting the same name again moves it
to the top. Up to 30 entries are remembered (stock: only the shown ones), so when an entry is removed (an older
backup replaced, a missing file removed) the next older one moves up and the menu stays full. The list now holds 10 projects (stock 9; the 10th has the
shortcut 0) and shows only the parent folder and the file name (`Voron\part.3mf`); backups show as
`Backup: <file name>`.

The copy is a background save: the open project keeps its name and its modified state, File > Save still writes your
own project file. SVG shapes added since the last real save are not stored in the copy (the copy skips the
"store original SVGs" question). If the copy can't be saved (folder not writable, disk full), a warning notification
says so and the G-code is exported anyway.

**Settings** (Preferences > General, app settings, not in any profile):

- `export_project_copy` "Save a project backup at every G-code export": on by default.
- `export_project_copy_keep` "Project backups kept": 10 by default (1-1000).
- `export_project_autosave` "Autosave the project at every G-code export": off by default.
- `export_project_copy_dir` "Project backups folder" (with a Browse button): empty = `exported_projects` in the
  configuration folder (`%APPDATA%\PrusaSlicer\exported_projects` on Windows).
- `export_project_autosave_dir` "Project autosave folder" (with a Browse button): no default, must be set.

The count and each folder row are greyed out while their check box is off. Older backups are deleted when there are
more than "Project backups kept", newest by the time stamp in the name; only files matching the backup name pattern
are ever deleted, so other 3mf files in a chosen folder (autosaves included) are safe. A backup counts as one only in the current backups folder: after changing the
folder, older backups show as normal entries.

**How it works.** `Plater::priv::save_export_project_copy()` (Plater.cpp), called from `Plater::priv::export_gcode()`
(single export and print host upload) and from `export_all_gcodes()` / `connect_gcode_all()`, before the export is
scheduled. It writes the model and `full_config_secure()` with `store_3mf()` and a thumbnail, as `export_3mf()` does,
without `set_project_filename()`; one thumbnail for both copies. `MainFrame::replace_in_recent_projects()` drops the
older backups from the recent list before adding the new one; autosaves go through `add_to_recent_projects()`. `RecentProjectsHistory` (MainFrame.hpp, a
`wxFileHistory` of 10) keeps all remembered entries (30) in its own list, rebuilds the 10 shown from it and relabels
the menu after every change; `[recent_projects]` in the app config stores all 30, the backup rule applies to all 30; `is_export_project_backup()` (Plater.cpp) tells the
backups. `AppConfig::get_recent_projects()` sorts by the key number (with 10 entries the `[recent_projects]` key "10"
sorted before "2"). The name the G-code was named after (`Plater::priv::output_filename_base()`: the kept one, else the
project's, else the first printable object's) is saved in the copies as `Model::output_filename_base` (3mf metadata
`slic3rpe:OutputFilenameBase`, written only when set; `strip_output_name_additions()` removes the template's
additions found by naming "A" and "B" with the current print); loading a project takes it, `output_project_path()` puts it in
place of the project's file name for the G-code name (`output_filepath_for_project()` callers), `export_3mf()` drops it
when saving under another name, a new project clears it. No change to slicing or G-code (golden test unchanged).

**How to test.** Export G-code: a `<name>_<date>_<time>.3mf` appears in the folder, Recent projects shows it, the title
bar still shows the open project (with its `*` if modified). Export 11 times: 10 copies left, the oldest deleted, a
non-matching 3mf in the folder kept. Set "Project backups kept" to 3 and export: 3 left. Turn on autosave without a
folder: warning notification, the backup is still saved. Set the autosave folder and export twice with the same name:
one `<name>.3mf` with the newer date; Recent projects shows it once, on top. Autosave A, B, C: all three in Recent
projects, C on top; export A again: A moves to the top, B and C stay. Export A, B, A, C: Recent
projects shows `Backup:` entries of C and A only, newest first. Open 11 projects: 10 entries, the order kept after a
restart. With 10 entries, export a backup of a third name: still 10 entries (the oldest hidden one moves up). Rename a
listed project on disk, click it, answer Yes: still 10 entries. Send to printer: copies named
after the upload file name. Set a folder to a read-only path: warning notification, G-code still exported. Turn both
check boxes off: no copy. Export with a template adding to `{input_filename_base}`, open the autosave, export again:
the same G-code and autosave name. `libslic3r_tests "[3mf]"`: the kept name is saved and loaded.

---

## 26. Arachne fix: thin walls with single perimeter on all top surfaces

**What it fixes.** With the Arachne perimeter generator and "Single perimeter on top surfaces"
(`top_one_perimeter_type`) = "All top surfaces", walls a little too thin for three lines (about 1.0-1.1 mm with a
0.4 mm nozzle at default Arachne settings) got two widened outer lines (up to about 0.55 mm instead of 0.40 mm) on
every layer that had a top surface anywhere on that island. The widened outer lines show as shiny/rough patches on the
side of the part. With "Disabled" or "Topmost surface only" the same walls got two normal outer lines and a thin middle
line, as stock. No setting: the fix is always on.

**Cause (stock PrusaSlicer 2.9.6).** For "All top surfaces", `PerimeterGenerator::process_arachne()` runs Arachne twice:
first for the outer wall only (one inset = at most 2 lines across a wall), then for the inner walls on the non-top part
of what is left inside. Arachne's `LimitedBeadingStrategy` caps the line count at 2 there, and between the point where
a third line would be added and the thickness of three full lines it widens the two lines to fill the whole wall. Nothing
is left for the inner pass, so the middle line is never added.

**How it works.** `LimitedBeadingStrategy` has a new `split_at_parent_transition` mode (constructor argument, default
off): the walled area ends already where the wrapped strategy would add the extra line, the capped lines keep their
optimal width and the rest goes to the inner contour, as it already did for thicker walls. It is passed through
`BeadingStrategyFactory::makeStrategy()` and a new `WallToolPaths` constructor argument `more_walls_inside`, which
`process_arachne()` sets for the first pass only when inner walls follow (`inner_loop_number >= 0`). The inner pass then
adds the middle line (on top surfaces the strip goes to top solid infill). Everything else (one perimeter only, "Topmost
surface only", first layer, classic generator, ensuring infill) is unchanged.

**How to test.** Unit test `[ArachneMoreWallsInside]` (test_arachne.cpp): a 1.04 mm wall with one inset gets 0.52 mm
lines and no inner contour without the mode, 0.357 mm lines and a 0.33 mm inner contour strip with it. Manual: a part
with ~1.06 mm walls and top surfaces, Arachne, "All top surfaces": in the G-code viewer (Width view) the outer wall of
those walls is 0.40 mm with a thin perimeter between, as with "Topmost surface only". Checked on a real part
(2026-10-18): wide outer wall (>0.43 mm) 4.56 m before, 3.11 m after (3.12 m with "Topmost surface only"); per-layer
extruded area within 0.07 % of before on every layer (no new gaps).

## 27. Dynamic overhang speeds and fan speeds with graphs

**What it does.** From SuperSlicer. Replaces PrusaSlicer's dynamic overhang speeds and fan speeds, which used four fixed
values at 0 / 25 / 50 / 75 % overlap, with a graph per setting that you edit with the mouse, plus SuperSlicer's overhangs
speed. The graphs' axis ("overlap", as SuperSlicer) counts down from 100 % at the overhang threshold (section 28: where
an extrusion starts to be an overhang) to 0 % at the threshold + the graph range; below the threshold an extrusion is
not an overhang and keeps its speed and fan speed.

**Settings.**
- Print > Speed > "Overhangs speed" line (as in SuperSlicer):
  - Maximum: `overhangs_speed`, mm/s or % of the bridges speed, default 100 %. Speed of the overhang perimeters (detected
    with "Detect bridging perimeters"); stock printed them at the bridges speed, so the default keeps stock output.
  - Range: `overhangs_graph_range`, mm or % of the nozzle diameter, default 50 %: how far past the overhang threshold the
    graphs end, measured as the threshold (the middle of the extrusion past the edge of the layer below). Example, 0.4 mm
    nozzle: threshold 55 % and range 50 % = the graphs go from 0.22 mm (100 %) to 0.42 mm (0 %). 0 = past the threshold
    is 0 % right away. Used by the dynamic speeds and the dynamic fan speeds. Replaces SuperSlicer's
    `overhangs_width_speed` (an end measured from the outer edge, which could end up before the threshold). A saved
    `overhangs_width_speed` is converted on load: range = old value - 100 % of the nozzle (the default threshold 50 % + half
    a line as wide as the nozzle), not below 0; a value in mm assumes a 0.4 mm nozzle; the old default 100 % gets the new
    default.
  - Dynamic: `enable_dynamic_overhang_speeds` (stock option, default off) and the graph `overhangs_dynamic_speed`: speed for
    each % of overlap, as a % of the way from the overhangs speed (0 %) to the perimeter / external perimeter speed (100 %).
    Default graph (SuperSlicer): 0 %: 0, 25 %: 10, 50 %: 40, 75 %: 70, 100 %: 100. 100 % overlap is always the perimeter
    speed, whatever the graph says (as SuperSlicer); without points the graph is linear.
- Filament > Cooling > "Overhang perimeter fan speed" line, per filament: Overhangs (`overhangs_fan_speed`, section 2),
  Dynamic: `enable_dynamic_fan_speeds` (stock option, default off) and the graph `overhangs_dynamic_fan_speed`: fan speed
  for each % of overlap. Default graph (SuperSlicer): 0 %: 100, 25 %: 80, 50 %: 60, 75 %: 40, 100 %: 20. While it is on,
  `overhangs_fan_speed` is greyed out and not used.
- Graph editor (click the curve): drag a point; left click selects it (yellow) and its values can be typed in the boxes
  at the bottom right; right click deletes a point; right click or double click an empty spot adds one. "Change type":
  linear, square (steps) or spline. "Reset": the default graph. The curve in the editor and on the button is drawn with
  the same interpolation the slicer uses.

**How it works.**
- New option types `coGraph` / `coGraphs` (`GraphData`, `ConfigOptionGraph(s)` in `Config.hpp/.cpp`), stored in
  SuperSlicer's format `begin:end:type:x0xy0:x1xy1...` (type 0 square, 1 linear, 2 spline), several graphs separated by
  commas. A SuperSlicer value with its `!` (disabled) mark is read as well; here the graphs are switched on and off by
  the stock `enable_dynamic_*` options. GUI: `GraphButton` field (`Field.cpp`), `GraphBitmapButton` widget, `GraphDialog`
  and `GraphChart` (ported from SuperSlicer's generalized ramming chart; PrusaSlicer's ramming chart is untouched).
- The overhanging perimeters are split by distance to the layer below as stock (`calculate_and_split_overhanging_extrusions()`).
  `ExtrusionProcessor::calculate_overhang_speed()` turns the distance into an overlap with the threshold and reads the
  graphs. Speed: the smallest graph value of the start, the end and the curled lines estimate of the path (as stock),
  between the overhangs speed and the perimeter speed (external perimeter speed for external overhangs); 0 speeds use
  the volumetric speed as stock. Fan: only for paths with at least one end past the overhang threshold (by more than 1 %
  of the nozzle diameter, against rounding; the overhang perimeters start right at the threshold); the others keep the
  fan speed of their feature.
- Graph start and end: the distances are measured from the outer edge, so the start is the overhang threshold + half
  the extrusion width (100 % overlap up to there) and the end that + the graph range (0 %). The slow down over curled
  lines is not affected (it grows over one nozzle diameter, as before).
- Fan in `CoolingBuffer` (`;_SET_FAN_SPEED` / `;_RESET_FAN_SPEED` as stock): the graph value is used as it is, also lower
  than the layer fan speed and without the `full_fan_speed_layer` ramp (stock only raised the fan, between the layer fan
  speed and the ramp), but 0 on the layers with the fan disabled by `disable_fan_first_layers`. The overhang perimeters
  feature fan (`overhangs_fan_speed`) and the stock bridge fan for overhang perimeters are ignored while the dynamic fan
  is on (as SuperSlicer): the overhang perimeter parts that don't stick out keep the layer fan speed.
- Old settings: `overhang_speed_0..3` and `overhang_fan_speed_0..3` are converted when a config is loaded
  (`PrintConfigDef::handle_legacy_dynamic_overhangs()`: presets, config bundles, 3MF projects incl. object / volume
  settings, G-code), then removed. Speeds, only when dynamic speeds were on (as SuperSlicer): the slowest of the four
  becomes the overhangs speed in mm/s (the fastest, if all are faster than the external perimeter speed), the graph gets
  the others as a % of the way to the external perimeter speed, 100 % at 100 % overlap. This is close to, not exactly,
  stock: stock used the extrusion width as threshold and the perimeter's own speed at 100 %. Fan speeds, per filament
  with the dynamic fan on: the four values, and at 100 % the external perimeter / perimeter / default fan speed if set,
  else the 75 % value. An object or volume setting is converted only when it also overrides the enable option,
  otherwise it is dropped. Stock spread its four values over one line width from where the line starts to stick out;
  here the graphs start at the overhang threshold (section 28), so the conversion sets the graph range to 100 % (the
  same width): the curve keeps its shape, the slow down and fan start at the threshold instead of the edge of the layer
  below (not on a filament preset alone, which has no print settings).

**How to test.** `[DynamicOverhang]` (test_dynamic_overhang.cpp): graph format and interpolation (linear, square,
spline), conversion of the old settings, overhangs speed (default = bridges speed, %), dynamic speeds (overhangs speed
when sticking out, perimeter speeds when supported, values in between on a sphere), dynamic fan (graph value used and
`overhangs_fan_speed` replaced, lower than the layer fan, 0 on the disabled first layers, off = overhangs fan speed).
Manual: Print > Speed, switch "Dynamic" on, click the curve, edit, slice an overhang (e.g. a 45 degree chamfer or a
sphere) and look at Speed / Fan speed in the G-code viewer.

---

## 28. Overhang threshold

**What it does.** From SuperSlicer 2019-2020 ("'As bridge' threshold", `overhangs_width`, before it was split into the
speed and flow thresholds). Sets how far a perimeter has to stick out past the layer below to be an overhang perimeter
(bridge flow, overhangs speed and fan, "Overhang perimeter" in the G-code viewer). Stock PrusaSlicer hard-codes half the
nozzle diameter. The dynamic overhang speeds and fan speeds (section 27) start at this threshold, so what is not an
overhang keeps its speed and fan speed.

**Settings.** Print > Layers and perimeters > Quality, "Detect bridging perimeters" line: Threshold
(`overhangs_threshold`), mm or % of the nozzle diameter, default 50 % (stock), down to -40 % of the nozzle diameter
(`Print::validate()`, as the option's minimum can't tell mm from %; further in, even vertical walls would be overhangs).
It measures where the **middle** of the perimeter lies past the edge of the layer below; with a line as wide as the
nozzle, 50 % means the whole line is past the edge, a negative value makes overhangs of lines whose middle is still a
little inside the edge (-5 % ≈ 42° for the example below). Below the line, a drawing shows the steepest wall that is not an overhang (a few external
perimeter lines, each sticking out as far as allowed, and the angle from vertical), for the layer height, nozzle and
external perimeter width of the current profiles:
angle = atan((threshold + external perimeter width / 2) / layer height). For a 0.4 mm nozzle, 0.4 mm lines and 0.2 mm
layers: 0 % = 45°, 25 % ≈ 56°, 50 % ≈ 63°. The same threshold is a steeper angle with lower layers or a smaller nozzle,
which print steeper overhangs. The name is not SuperSlicer's `overhangs_width`: current SuperSlicer uses that name for its
flow threshold, measured from the outer edge.

**How it works.**
- `PerimeterGenerator::process_classic()` / `process_arachne()`: the lower slices are grown by the threshold instead of
  half the nozzle diameter (the parts of the perimeters outside become overhang perimeters). "Extra perimeters on
  overhangs" keeps the stock half nozzle diameter (`extra_perimeters_lower_slices()`, computed once per layer region).
- `ExtrusionProcessor::calculate_overhang_speed()`: the overlap of the graphs is 100 % up to the threshold + half the
  extrusion width (the distance there is measured from the outer edge), see section 27.
- GUI: `PresetHints::overhang_threshold_geometry()`, drawn by `OverhangAnglePreview` (Tab.cpp), read again at every paint
  (a nozzle change in the printer tab shows when the print tab is shown again). The threshold is greyed out without
  perimeters, or when neither "Detect bridging perimeters", the dynamic overhang speeds nor the filament's dynamic fan
  speeds use it. The single option rows of a group with grouped rows have their field in the group's first column
  (`OG_CustomCtrl::aligned_single_offset()`), so all the fields of a group line up in columns.

**How to test.** `[DynamicOverhang]` (test_dynamic_overhang.cpp), "Overhang threshold": on a sphere at 0.2 mm layers the
steepest overhang perimeter is at the expected angle for 50 %, 0 % and 0.1 mm, and 50 % gives the same G-code as 0.2 mm;
"Dynamic overhang graphs start at the overhang threshold": with 0 % the dynamic speeds and fan speeds start at 45°;
"... end the graph range past the overhang threshold": 0 % + 25 % reaches the overhangs speed at 56°.
The golden test covers the default. Manual: slice a sphere or an overhang test, change the threshold, and look at
"Overhang perimeter" in the G-code viewer.

---

## 29. G-code viewer: discrete values, selected layers

**What it does.** From SuperSlicer ("Discrete Values" in its legend). When a range view (height, width, speed, fan
speed, temperature, volumetric flow, layer time) has few distinct values, each value gets its own color and its own
legend line, instead of the continuous scale where close values get almost the same color and the legend shows evenly
spaced values that may not exist in the G-code. On by default.
"Selected layers" (SuperSlicer, its "Selected layers" legend switch): the colors of the width, speed, volumetric flow
and layer time views span the values of the layers in view (vertical slider) instead of the whole print, so a few
layers get the whole palette, and can have few enough values for the discrete mode.

**Settings.** Preview legend: "Discrete values" checkbox, shown when the current view has more than 2 and at most 21
distinct values (rounded to 0.01). Remembered in the app config (`gcode_viewer_discrete_values`, default on), not in
the presets. With 1 or 2 values, or more than 21, the stock legend is used. The points the time estimator inserts
inside the moves for the actual speed view (`internal_only` moves, their fan speed, width, speed... blended between two
moves) don't count as values; they get the color of the nearest value.
"Selected layers" checkbox (Width, Speed, Volumetric flow rate, Layer time views, as SuperSlicer), shown when it is on
or when the vertical slider doesn't show all the layers. Off by default, per view, not remembered (as SuperSlicer).
When on, the legend shows the range of the layers in view, plus the whole print max on top (light pink) and min at the
bottom (light purple) when they are out of it; the discrete count is the print's distinct values within that range.

**How it works.**
- libvgcode `ColorRange`: besides the min / max, `update()` keeps the sorted distinct values (rounded to 0.01) until
  there are more than `get_max_discrete_count()` (2 × palette size − 1 = 21). In discrete mode `get_color_at()` returns
  the color of the nearest distinct value, and `get_values()` the distinct values. The colors are the palette and the
  colors halfway between its colors, the values spread over them and the last one at the last color (SuperSlicer's
  `compute_discrete_colors()` and `Range_Colors_Details`).
- `PathVertex::interpolated` (set from `internal_only` in `LibVGCodeWrapper.cpp`, in the padding of the struct): skipped
  for the distinct values by `ColorRange::update()`. Without it, a G-code with 3 fan speeds had 708 "values". The
  'phantom' vertex added at the start of each path copies the flag of its move: when that move was an estimator point,
  its blended value counted (214 fan "values" on a 3 fan speed print, no checkbox).
- Selected layers: `ColorRange` keeps up to 3 × 21 − 1 distinct values (SuperSlicer keeps 63), `m_whole_print` and the
  range of the layers in view (`update_layers()`); `get_current_range()` is the whole range clamped to it and is used
  by `get_color_at()` (values out of it: SuperSlicer's too low / too high colors), `get_values()` and the discrete
  count. `ViewerImpl::update_layers_color_range()` fills it for the current view in `update_colors()` (same vertex rules
  as `update_color_ranges()`, layer times for the layer time views); `set_layers_view_range()` recolors when the
  layers in view change and the current view is in selected layers mode. `Viewer::set_color_range_whole_print()`.
- `Viewer::set_color_ranges_discrete()` switches all the ranges and recolors. `GCodeViewer::load_as_gcode()` applies the
  app config setting; `render_legend()` lists one line per value and draws the checkbox.

**How to test.** Manual: slice the fork feature test part, Preview > Fan speed or Speed: one legend line per value, the
colors match the paths; untick "Discrete values": the stock scale. Restart: the setting is kept. Speed view, move the
top of the vertical slider down to a few layers, tick "Selected layers": the legend shows the speeds of those layers
(discrete if 3-21 values) with the whole print max/min in pink/purple, moving the slider updates it; untick: the
whole print scale.

---

## 30. Calibration prints

**What it does.** From SuperSlicer's Calibration menu, rewritten for PrusaSlicer 2.9.6: a Calibration menu (between
Configuration and Help) with a temperature tower, SuperSlicer's pressure calibration, the filament shrinkage calibration (CaliFlower) and Klipper's ringing tower. Each item opens a small dialog with an explanation, a few fields and Generate. Generate starts a new project
(it asks to save the current one first), loads the print with its own object / part settings and slices. SuperSlicer's
HTML help pages, its introduction page and its other calibrations (bed leveling, filament / extruder flow, retraction,
ironing, calibration cube) are not ported. The bridge flow and pressure advance calibrations were ported and removed
(2026-10-20): without SuperSlicer's overhang / thin wall / bridge margin settings they couldn't match it. Ellis' pattern
was added instead (2026-10-23), which writes its G-code directly and needs no slicing setting. SuperSlicer's pressure
calibration is back the same day (user: sliced in the plater, so the normal preview shows layers, moves and G-code),
without the per-part accelerations (see below).

**Prints.**
- Temperature tower: "Floors below" / "Floors above" the filament temperature (default 2 / 2), "Step" 5-20 °C
  (default 10). The hottest floor at the bottom; each floor sets its temperature with an `M104` custom G-code at its
  height (floor 0 at the second layer, after the first layer temperature). Temperatures from 180 to 285 °C that are a
  multiple of 5 are engraved. Model scaled with the nozzle (made for 0.4 mm). Object settings as SuperSlicer: 1
  perimeter, extra perimeters on overhangs, 2 bottom / 3 top layers, no gap fill, layer height half the nozzle, 7% infill,
  rectilinear top, no ironing, no brim ears, brim at least 8 x nozzle. SuperSlicer's per-object temperature
  (`print_temperature`) doesn't exist here: the custom G-code does it. The tower is half as thick as the model (Y scale
  0.5, as SuperSlicer), but the cone of each floor is a part of its own and stays round (2026-10-23): squashed, its tip was
  thinner than a line and printed as dots in the air.
- Filament shrinkage calibration (menu item and dialog named "CaliFlower" until 2026-10-26; Vector 3D's CaliFlower, the model is not included): the location of your model file (remembered in the app settings,
  `calibration_califlower_path`) and Browse. Loads it without object settings (the print profile applies; before
  2026-10-23 it forced 2 perimeters and 20% grid infill) and sets the filament shrinkage compensation XY and Z to 0% (no
  compensation, SuperSlicer's 100%) and adds `SET_SKEW CLEAR=1` (Klipper skew correction off, user 2026-10-23) at the
  end of the filament start G-code, which runs after the printer's start G-code (PRINT_START); both are unsaved changes
  of the filament settings, for every extruder. The line is not added twice.
- Ringing tower (Klipper, 2026-10-24; the model is not included, e.g. Klipper's `docs/prints/ringing_tower.stl`): the
  location of your model file and Browse, as the CaliFlower (`calibration_ringing_path`), and the values of Klipper's
  ringing test: velocity limit (default 100 mm/s), shaper type X / Y (zv, mzv, zvd, ei, 2hump_ei, 3hump_ei; default mzv),
  shaper frequency X / Y (default 0 = no input shaper on that axis), start acceleration (1500 mm/s²), step delta
  (500 mm/s²), step height (5 mm); all remembered (`calibration_ringing_*`). Loads the model without object settings,
  sets Klipper's recommended slicing (docs/Resonance_Compensation.md, user 2026-10-24) as unsaved changes of the print
  settings: vase mode (`spiral_vase`, perimeters 1, top solid layers 0, fill density 0, no supports / enforced support
  layers, thin walls off, as the vase mode check in the GUI asks), a 1 mm base (bottom solid layers = 1 mm / layer
  height, rounded up), layer height 0.2 mm (0.25 mm kept when the profile has it), external perimeter and perimeter speed
  = the velocity limit (the V of Klipper's frequency V x N / D); in the filament settings the minimum layer time
  (`slowdown_below_layer_time`) is capped at 3 s. Then it adds at the end of the filament start G-code (after the printer's start G-code), for every extruder:
  ```
  ; Ringing tower (Calibration menu)
  SET_VELOCITY_LIMIT VELOCITY=<velocity> MINIMUM_CRUISE_RATIO=0
  SET_PRESSURE_ADVANCE ADVANCE=0
  SET_INPUT_SHAPER SHAPER_TYPE_X=<type X> SHAPER_FREQ_X=<freq X> SHAPER_TYPE_Y=<type Y> SHAPER_FREQ_Y=<freq Y>   (SHAPER_TYPE_<axis> left out when its frequency is 0,
                    e.g. SET_INPUT_SHAPER SHAPER_FREQ_X=0 SHAPER_FREQ_Y=0)
  TUNING_TOWER COMMAND=SET_VELOCITY_LIMIT PARAMETER=ACCEL START=<start> STEP_DELTA=<delta> STEP_HEIGHT=<height>
  ; Ringing tower end
  ```
  Generated again, the block between the two comment lines is replaced. So that nothing overrides it, the print's
  default acceleration is set to 0 (no acceleration commands at all, section 4) and the filament's Klipper speed mode to
  Off (its macro runs after the filament start G-code, section 22). All unsaved changes of the print / filament settings.
- Pressure advance pattern (Klipper only; menu item disabled (greyed out) 2026-10-23, removed from the menu 2026-10-26 by
  the user, the code kept (`CalibrationType::PressureAdvance`, `[PAPattern]` tests): the pressure
  calibration below prints Ellis' chevrons sliced in the plater), ported from Ellis' Pressure Advance / Linear Advance Calibration Tool
  (https://github.com/AndrewEllis93/Pressure_Linear_Advance_Tool, GPL-3.0, original Marlin tool by Sineos): chevrons of a few
  walls, one per `SET_PRESSURE_ADVANCE ADVANCE=` value, on an anchor frame, with every other value printed next to them.
  Like the original, the G-code is written directly, not sliced. Fields (Ellis' defaults): start / end / step (0 / 0.08 /
  0.005), print speed 100 mm/s, first layer speed (print settings), acceleration (0 = no `SET_VELOCITY_LIMIT`), fan and
  first layer fan (filament: the fan of an external perimeter on a long layer, i.e. `external_perimeter_fan_speed` >
  `perimeter_fan_speed` > `default_fan_speed` > `min_fan_speed` if always on; first layer 0 if `disable_fan_first_layers`
  > 0), layers 4, walls 3, side 30 mm, spacing 2 mm, corner angle 90°, print direction, anchor frame / layer / none, numbers.
  Not in the dialog (Ellis' expert values): line width 112.5% / anchor 140% of the nozzle, 4 anchor perimeters, no
  retraction for travels under 2 mm, `M117 PA <value>` with every value. Not ported: Marlin / RRF, `SMOOTH_TIME`, extruder
  name / tool index, round beds, origin at the center. From the presets: bed (pattern at the center of `bed_shape`; refused
  if it doesn't fit or overlaps a `bed_keep_out_zones` zone), nozzle and filament diameter, extrusion multiplier, layer and
  first layer height, travel speed, retraction (length, speeds, firmware retraction, `retract_lift` as Z hop), first layer
  temperatures. The file is built as a print: PrusaSlicer header, `M190` / `M104` before and `M109` after the start
  G-code when it doesn't set them (`autoemit_temperature_commands`), start G-code and filament start G-code with the print
  placeholders (`total_layer_count`, `first_layer_print_min`, ... of the pattern), Klipper speed mode macro, `G21 G90 M83`,
  the pattern with `;TYPE:` (anchor Skirt/Brim, walls External perimeter, numbers Perimeter), `;WIDTH:`, `;LAYER_CHANGE` /
  `;Z:` / `;HEIGHT:`, then PA back to the start value, retract, Z +5, `M104 S0 M140 S0 M107`, filament end and end G-code,
  and the settings block, so the G-code viewer shows it as a sliced print (features, widths, layers, time estimate) and
  "Open settings" works. No thumbnails, no `EXCLUDE_OBJECT`. Generate asks where to save it (`pa_pattern.gcode`, last output
  folder) and opens it in the G-code viewer. The numbers on the first layer without an anchor use the first layer height
  (Ellis used the layer height).
- Pressure calibration (SuperSlicer's `CalibrationPressureAdvDialog`, menu "Pressure calibration (SuperSlicer)"): loaded
  as models and sliced in the plater, so the preview, the layer slider, the moves and the G-code panel work as for any
  print.
  - Dialog (2026-10-26): number fields 8 characters wide (file path fields stay wide), the settings 4 per row, the
    explanation as wide as the fields under it (every calibration dialog), the test rows narrower; End value
    choices 0.02 to 0.5 (0.02, 0.04, 0.06, 0.08 = default, 0.10, 0.15, 0.20, 0.30, 0.40, 0.50; any value can be typed).
    The "Bends" choice (Ellis' chevrons or SuperSlicer's 90° bends with the extrusion role's width and speed) was removed
    by the user: always Ellis' chevrons. SuperSlicer's bends stay in the code for CheckAll (one bend per extrusion role).
  - Settings, at the top of the dialog, filled in with Ellis' defaults (his tool's `DEFAULT_SETTINGS`,
    `pressure_linear_advance.js`; user 2026-10-23: "the recommended or default settings that Ellis uses"): print speed = the external perimeter speed of the
    selected print profile (2026-10-26, Ellis' tool: "Use your external perimeter speed from your slicer settings - if it's
    reasonably fast (>60mm/s)"; his default 100 mm/s when the profile has none), line width 100 % of the nozzle (2026-10-26,
    user; Ellis: "Match your thickest perimeter", his default 112.5 %), layers 4,
    first layer speed 30 mm/s, layer height 0.2, first layer height 0.25, fan 100 %, first layer fan 0 % (0 or the fan:
    PrusaSlicer has no fan of its own for the first layer), acceleration 0 = disabled. Generate writes them into the print
    and filament settings as unsaved changes (`pressure_calibration_apply_settings()`): layer heights, first layer speed,
    every acceleration = the default acceleration (0: no acceleration command), default fan speed = the fan with the feature
    fan speeds at -1, no `full_fan_speed_layer` ramp, no layer time cooling (`fan_below_layer_time` and
    `slowdown_below_layer_time` 0), `disable_fan_first_layers` 1 (first layer fan 0) or 0. The printer and filament settings
    (temperatures, retraction, travel speed) stay the user's, as Ellis asks for them.
  - How the user knows what was used: the same settings and the values of every test as comments (Ellis writes them at the
    top of his G-code), a custom G-code at the first layer height (`pressure_calibration_summary()`): in the G-code at the
    start of the first layer, a Custom G-code mark in the preview; the object is named after its test (`PA test 0
    ExternalPerimeter 0 - 0.08 by 0.005`, also its `EXCLUDE_OBJECT` name); the changed settings show as unsaved changes in
    the tabs and are in the settings block of the G-code.
  - Tests: "Number of PA tests" 1-10, one row per test: first layer value, start / end / increment (SuperSlicer's lists with
    Ellis' values added, values can be typed, defaults 0.0 / 0.0 / 0.08 / 0.005 as Ellis), extrusion role (14 roles +
    CheckAll, the first row ExternalPerimeter, each new row the next role) and Smooth time (`SET_PRESSURE_ADVANCE
    SMOOTH_TIME=` instead of `ADVANCE=`). Klipper `SET_PRESSURE_ADVANCE`, Marlin `M900 K`, RepRap `M572 S`, other firmwares
    refused.
  - "Ellis' settings" (default): Ellis' chevrons, `wall_count` open lines (Walls 3) of the line width at the print speed,
    `side_length` 30 mm arms at 90°, `pattern_spacing` 2 mm between chevrons (dialog fields Walls / Side length / Spacing,
    Ellis' defaults), walls `line spacing / sin 45°` apart (Ellis: the line spacing, width - layer height x (1 - pi/4)),
    "Layers" high, pointing down and stacked as SuperSlicer's bends. Each wall is a strip one line wide with square ends half
    a width beyond the end of the line and a round outer corner (the shape such a line leaves), sliced with Arachne into one
    open external perimeter from the end of one arm through the corner to the end of the other, 0.45 mm wide (wider only on
    a few moves within 0.15 mm of the corner, up to 0.51 mm: the inside of a 90° corner is thicker). Classic perimeters vary
    the width (0.32-0.57 mm). Ellis' walls overlap a little (spacing 0.407 mm for 0.45 mm lines) and overlapping parts of
    one object clip each other (`PrintObjectSlice.cpp`, the later part wins), so each wall is an object of its own: "PA test
    0 ... wall 1/2/3" holds wall k of every chevron, one part per chevron with its value as per region G-code. The objects of
    the walls use PrusaSlicer's default Arachne settings (`min_bead_width`, `min_feature_size`, `wall_transition_*`,
    `wall_distribution_count`): with the user's (min bead width 40 %, filter deviation 5 %) Arachne split the corner into
    two thin beads, the line stopped before the corner, travelled, and a blob printed at the corner on its own. The base
    object ("PA test 0 ... frame") holds the frame and the numbers. The dialog loads the base plate once per object, builds
    the frames, arranges (the objects of the walls still the empty base plate), gives the walls the instance transformation
    of their frame and builds them (`pressure_calibration_build_walls()`).
  - CheckAll (and "Extrusion role (SuperSlicer)" until 2026-10-26, now only in the code and tests): SuperSlicer's 90° bends (`90_bend_<nozzle>.3mf`, 0.1 to 2.0 mm nozzles),
    closed loops scaled with SuperSlicer's `magical_scaling()` to 2 loops of the role's width without gap fill, parts of the
    base object with the role's width (perimeter and external perimeter width), speed (perimeter, external perimeter and
    gap fill speed) and their value as per region G-code (section 38), 5 layers high (InternalInfill with
    `infill_every_layers` > 1 and FirstLayer get a layer range with that layer height).
  - Frame on the first layer, as Ellis' anchor frame: borders of SuperSlicer's 4 first layer lines, without SuperSlicer's
    extra (line width - spacing) on Ellis' frame: with it PrusaSlicer printed a 0.11 mm line of gap fill in the middle of every
    border on the user's profile (0.4 mm lines, 0.25 mm first layer; 2026-10-26). (A "Frame lines" field
    of 1-50 lines, 2026-10-25, was removed the same day: the wider borders printed inconsistently; the user adds a brim for
    adhesion). Sized from the chevrons / bends and the numbers, so it follows the number of values, the right side a solid
    tab under the numbers, with the test number, 2 mm between the numbers and both edges of the tab (SuperSlicer: a nozzle
    on the inner side, none on the outer one; user 2026-10-25: the tab was too small). Around Ellis' chevrons the frame is
    one solid part (`frame`, the outer rectangle without the inner one). On the first layer the left border and the tab
    reach half a line width into the chevrons, anchoring the ends of the lines: 0.13 mm overlap
    with the frame's lines on the user's profile, as Ellis' pattern (0.132 mm; a whole line width gave 0.36 mm, touching
    only left the lines 0.09 mm short; user 2026-10-25: "Maybe half a line width instead"). Above
    the first layer (no frame there) the left arm of every wall runs on over the whole left border to its outer edge, cut
    there square to it, printed on the frame; the right arms run as far (a border width) into the tab, over its perimeters,
    cut square there too, short of the numbers 2 mm from the chevrons (user 2026-10-26: the right side did not reach onto
    the frame like the left one). User 2026-10-25: "The first layer should only attach to the frame by 1 line width. The layers above
    will attach to the whole frame." Cutting the frame around full-length arms was tried the same day and rejected (the
    left border in islands, a jagged tab: "the frame should be a solid line"). The lines above the first layer are so 60
    mm + 2 x 1.4 x (the border width - half a line width) long (63.4 mm on the user's profile; the left border is as wide as the
    others: half a line wider, it printed a line of gap fill). The objects of the walls have `overhangs` and dynamic
    overhang speeds off: the frame under the arms is another object, so PrusaSlicer printed that piece as an overhang
    (bridge flow, overhang speed and fan, with the previous chevron's value). With SuperSlicer's bends: 4 borders (`pa_border.3mf`), the left
    one reaching a little into the bends. At least 2 values. SuperSlicer centred the sides on the middle bend: with an even number of values
    the frame was open (sides short at one end, the bottom border apart); here they are centred between the first and the
    last (2026-10-23). `base_plate.3mf` is a 0.3 mm placeholder under the first layer that doesn't print. Every second
    value engraved next to its chevron / bend with the digit / point models (6 decimals, as SuperSlicer; the point as wide
    as the strokes of the digits, so it prints as they do: SuperSlicer's 0.6 mm was too wide for a thin wall and too narrow
    for a loop, PrusaSlicer left it out, 2026-10-25); with Ellis'
    settings the numbers' second layer at the first layer speed (object perimeter speeds), as Ellis.
  - The object's per region G-code sets the first layer value for the base, borders and numbers on all their layers (the
    numbers are two layers high; SuperSlicer sets it on the first layer only, so their second layer printed with the last
    bend's value, Ellis prints the numbers with the start value). Object settings as SuperSlicer: monotonic bottom, thin
    walls, 1 bottom / 0 top layers, no brim, 0% infill, gap fill, no single perimeter on top / first layer, rear seam; and,
    not in SuperSlicer, classic perimeters (Arachne widened the inner loop and added a tiny loop with its own retract /
    travel 1 mm from the corner of every bend). Print settings as SuperSlicer: no avoid crossing perimeters, no sequential
    printing, no elephant foot / XY size compensation. Arranged, then sliced (CheckAll is not sliced: one bend per role, the
    value to type in the per region G-code of each bend). The models are SuperSlicer's (`resources/calibration/filament_pressure`).
  - Not as SuperSlicer: no per-part accelerations (one acceleration for all, as Ellis; per-part accelerations need them as
    region settings), the SuperSlicer-only widths / speeds (overhangs_width, thin walls, default_speed) fall back to the
    defaults (default_speed = perimeter speed), no `filament_pressure_advance` reset, no SuperSlicer help page.
  - Compared like for like with Ellis' pattern (2026-10-23), Ellis' defaults on both, the user's printer and filament
    (`.2 .4voron - slow`, `ABS-Sparta3D-slow`, Voron 0.4): the same 17 values; per chevron and layer 3 open external
    perimeters of 60 mm (179.8 mm, Ellis 180), 3 travels and 3 retractions as Ellis, 100 mm/s, 0.45 mm; 4 layers at the same
    heights (0.25, 0.2, 0.2, 0.2), 3057 mm of walls per layer (Ellis 3060); the numbers at 30 mm/s with the start value; no
    acceleration commands; the fan off on the first layer and 100 % from the second (the same layer change). Still different:
    the walls print wall by wall over all the chevrons (one object per wall), each with its `SET_PRESSURE_ADVANCE` (3 per
    chevron and layer, Ellis 1 per chevron, which prints its walls one after the other); the corner of each line is 4 short
    moves up to 0.51 mm wide over 0.3 mm (Ellis: a sharp turn at 0.45 mm); SuperSlicer's frame is 4 borders of about 4
    lines with a solid tab beside the chevrons, Ellis' 4 perimeters of 140 % with a filled tab above them; the numbers set
    the value on every island (Ellis once).

**How it works.** `CalibrationDialogs.cpp` (GUI): `CalibrationDialog` base (explanation, fields, Generate / Close,
`add_part()` adds the volumes of a model file to an object as SuperSlicer does), one class per print,
`show_calibration_dialog()` called by the menu (MainFrame.cpp). The pressure calibration's models and settings are built in
`libslic3r/PressureCalibration.cpp` (`pressure_calibration_build()`, also used by the tests); the dialog collects the rows,
loads the base plates, calls it and arranges. `Plater::new_project()` now returns false when cancelled.
Models in `resources/calibration/filament_temp` from SuperSlicer (CC BY-SA 3.0, see its LICENCE.txt). The pressure
advance pattern: `libslic3r/GCode/PressureAdvancePattern.cpp` (`pressure_advance_pattern_gcode()`, the geometry of Ellis'
`pressure_linear_advance.js` with its formulas), the dialog only collects the fields, saves and calls
`start_new_gcodeviewer()`.

**How to test.** Manual (GUI): Calibration > each item > Generate: the print is loaded and sliced; Preview: temperature
tower `M104` per floor (Legend > Custom G-codes), CaliFlower with the print profile's perimeters and infill (no object
settings in the object list), the filament shrinkage at 0% and `SET_SKEW CLEAR=1` after the start G-code; ringing tower:
the block after the start G-code with the values typed in, no `M204` / `ACCEL=` in the G-code, default acceleration
0, vase mode, layer height, speeds and speed mode Off as unsaved changes, minimum layer time 3 s at most, Generate twice gives one block; pressure
advance: saved and opened in the G-code viewer, anchor / walls / numbers as feature types, the values in the G-code panel;
pressure calibration (SuperSlicer): one object per test named after it with the bends, borders and digits as parts (each
bend's per region G-code in the object list), sliced, each bend's `SET_PRESSURE_ADVANCE ADVANCE=` before it on every layer
in the G-code panel, the first layer value for the base, borders and numbers, the settings summary at the start of the first
layer (Custom G-code), Ellis' settings as unsaved changes in the print and filament tabs. Automatic:
`fff_print_tests "[PressureCalibration]"` (the real models built as the dialog does, with Ellis' settings and the user's
Arachne settings: summary before the first extrusion, each chevron with its value on its 4 layers, base / borders / numbers
with the first layer value, per layer 3 walls of each chevron as open external perimeters of 60 mm + both arms over the frame at 100 mm/s, 0.45 mm
wide but on the few corner moves, whatever the generator, small perimeter or layer time settings, fan 0 then 100 %, no
acceleration command; with the extrusion role: 5 layers at the role's speed;
the settings written into a config, the first layer fan refused unless 0 or the fan; no overhang perimeters; the frame for 2 to 30 values: a
solid ring around the chevrons, on the first layer the ends of the lines half a line width into the left border and the tab,
above it the left arms to its outer edge and the right ones a border width into the tab, short of the numbers, one piece with the walls, the numbers on the tab 2 mm from both its edges (also with SuperSlicer's bends);
fewer than 2 values refused; no gap fill in the frame with 0.4 mm lines, a 0.2 / 0.25 / 0.3 mm first layer and 2 or 4
perimeters); the like for
like comparison with Ellis: `fff_print_tests "[.PACompare]"` with `PA_CMP_INI` (print|filament|printer .ini) and
`PA_CMP_OUT` (folder) writes both G-codes, `python Stuff/compare_pa.py <ellis> <superslicer>` compares them; `fff_print_tests "[PAPattern]"` (one pattern per value and reset, header / start G-code placeholders /
temperatures / speed mode / settings block, extrusions on the bed and E from the cross section, read by the G-code
processor with the feature types and a time estimate, refused for non-Klipper / keep-out zone / too small bed / bad
values, print direction rotates the print).

---

## 31. Own brim under the support

**What it does.** For the supports whose first layer touches the brim (every brim: brim types, paint-on brim of section
23, brim per object): their whole first layer is printed as brim (straight lines with a loop around them), and the brim
around the part stops at it, half a line away. The support stands on its own piece of brim, part of the brim. The
supports that don't touch the brim keep their own first layer on the bed.

Off (default, stock): the first layer of these supports is cut where it overlaps the brim (there the support starts on
the brim, on its second layer) and the rest of it is printed as support (raft first layer density).

**Settings.** `support_material_own_brim` "Own brim under the support" (Print Settings > Support material > Options
for support material and raft, per object too): off by default (stock output).

**How it works.**
- `generate_raft_base()` (Support/SupportCommon.cpp, used by the grid, snug and organic supports without a raft): the
  brim area is stock's (brim type, brim width + separation) plus the painted brim (`brim_paint_areas()`, Brim.hpp: the
  enforced rings) minus the blocked zones. Off: stock's cut of the support's first layer by it. On: the first-layer
  support islands (base, contacts, interfaces) that intersect it are removed from the first layer and kept in
  `PrintObject::support_brim_pads()` (cleared at each support generation).
- `make_brim()` (Brim.cpp): the pads of each instance, half a line wider, are taken out of the brim areas (outer, per
  object, ears; the inner brim loops go around them), and printed last, whole, with the rectilinear ears' fill (a loop
  and lines); a pad belongs to its object's brim with brim per object and counts in the first layer area (skirt). The
  supports change invalidates the brim, as stock.

**How to test.** `fff_print_tests "[BrimPaint]"` "Own brim under the support": a cube with a plate overhanging 10 mm
next to it (brim 5 mm). On, with the outer brim or the brim painted all around: no support on the first layer, every
line of the support's second layer lies on brim lines; without a brim the support keeps its first layer. Off: stock (the
support's first layer kept past the brim). Manually: a part with supports next to it and a brim, Preview layer 1 with
the option on and off.

---

## 32. Slicing: no slicing while painting, delay after a change

**What it does.**
- **No slicing while painting.** With background processing on, PrusaSlicer slices again after every stroke of a
  painting tool (support, seam, fuzzy skin, brim, multi-material painting), so painting stutters and a stroke can wait
  for the slicing. The fork doesn't slice while one of these tools is open; leaving the tool slices once.
- **Delay after a change.** With background processing on, while editing in the Plater's 3D view, the slicing starts a
  few seconds after the last change (every change restarts the count): a series of edits (part / modifier settings,
  moving parts) slices once. In the Preview and on the settings tabs the slicing starts right away, as stock;
  switching to the Preview slices right away too.

**Settings.** Preferences > General: "Slicing delay after a change" (`background_processing_delay`, app setting, 0-60 s,
default 3; 0 = right away as PrusaSlicer). Only with "Background processing" on; without it, the changes are applied
right away as stock (the Slice now button and the errors don't wait).

**How it works.** Plater's background process timer (`schedule_background_process()`, 0.1 s after a change in stock)
starts after the delay when the 3D view of the Plater tab is shown (`is_view3D_shown()`, the Plater page selected in the
main tabs or not in them), and does nothing while the current gizmo is one of the painting gizmos
(`GLGizmosManager::get_current_type()`); `GLGizmoPainterBase::on_set_state()` schedules the slicing when a painting gizmo
closes. Changes made meanwhile are applied then. No change to slicing or G-code (golden test unchanged).

**How to test.** Manual: background processing on. Open Brim (or Support) painting, paint several strokes: no slicing;
close the tool: it slices once. In the 3D view, change a part setting three times within 3 s: one slicing, 3 s after
the last change. On the Print Settings tab or in the Preview: slicing right away after a change.

---

## 33. Delete all: new project or erase objects

**What it does.** As SuperSlicer: "Delete all" (the garbage can in the 3D view toolbar, Ctrl+Del, Edit > Delete All)
asks what to do: **New Project** (default button), **Erase all objects** (keeps the project, its name and the presets;
undoable), or Cancel. With no objects on the plate it asks "Create a new project?". Stock PrusaSlicer erased the
objects without asking from the toolbar and Ctrl+Del, and asked "continue?" from the menu.

**Settings.** Preferences > General > "Ask for 'new project' on 'Delete all'" (`default_action_delete_all` in
PrusaSlicer.ini, default on). Off: every "Delete all" erases the objects without asking.

**How it works.** `EVT_GLTOOLBAR_DELETE_ALL` (toolbar and Ctrl+Del) now calls `Plater::reset_with_confirm()`, like the
menu. "New Project" calls `Plater::new_project()` (it still asks about an unsaved project and modified presets);
"Erase all objects" calls `delete_all_objects_from_model()`. No change to slicing or G-code.

**How to test.** Manual: load two objects, click the garbage can: the three-button dialog shows. "Erase all objects":
plate empty, project name kept, Ctrl+Z brings them back. "New Project": new untitled project. Same with Ctrl+Del and the
menu. Turn the preference off: the garbage can erases without a dialog.

---

## 34. Brim-aware arrange

**What it does.** Arrange (and fill bed) spaces the objects by their brims. Stock keeps every pair of objects the
arrange "Spacing" apart whatever their brims, so a brim could run into a neighbour that has no brim. Now:

- two objects with a brim, or two without: the spacing, as stock. Brims of neighbours may overlap and merge.
- an object with a brim next to one without: the spacing plus the brim (width + separation), so the brim edge stays the
  spacing away from the other object.
- the same for a bed keep-out zone (section 3) and the wipe tower: a brim keeps the spacing away from them.

A part counts as having a brim with the brim type "Outer brim only" / "Outer and inner brim", or with painted brim
enforcers (section 23), brim width above 0 and no raft (as `PrintObject::has_brim()`); its own settings override the
print settings. A painted brim counts as all around the part (simple and safe; it uses a little more room). Brim ears
use the brim width, so they are covered too.

No setting. Without brims arrange is unchanged (stock).

**How it works.** `arrange-wrapper/SceneBuilder.cpp` `model_object_brim()` computes the brim of each ModelObject from
its config over the print's (no PrintObject needed, so also for the objects of the other beds); the instance
arrangeables write it into the arrange item's data store (`"brim"`). In the kernel, `brim_gap()`
(`arrange/DataStoreTraits.hpp`) gives the extra gap of a pair: 0 when both or neither have a brim, else the brim. The
no-fit polygon of each fixed item is grown by that gap for the item being placed (`ArrangeItem.hpp`
`calculate_nfp_unnormalized`; the convex pieces stay convex). The keep-out start position check (`KernelUtils.hpp`) and
the keep-out re-centering (`RectangleOverfitPackingStrategy.hpp`) grow the zones the same way. The bed is still shrunk by
the widest brim, as stock.

**How to test.** `arrange_tests "[BrimArrange]"`: 8 cubes with an 8 mm brim and 8 without, 2 mm spacing, with keep-out
zones, two strategies and two geometry handlings: brim to no-brim gaps >= 10 mm, zone gaps >= brim + 1 mm, and two
brimmed cubes end up closer than the brim width (brims merge). Manual: per-object brim on a few parts (right click >
Add settings > Skirt and brim), arrange: parts without a brim stay clear of the brims, brimmed parts pack together.

---

## 35. Seam attractors

**What it does.** As SuperSlicer: a small part that pulls the seam towards it, for a straight seam at a chosen place
without painting every layer. Right click an object > **Add seam attractor** >

- **Seam attractor (sphere)**: on every layer, the seam of every loop (outer, inner perimeters and holes) goes to the
  point nearest to the sphere's center. So the seam is a straight vertical line where the object's side is straight.
  With several spheres, each layer uses the one nearest in height.
- **Seam attractor (cylinder)**: only on the layers within the cylinder's height, the seam goes to the point nearest to
  its axis (the center of its section, so a tilted cylinder works too). It starts as high as the object; scale it in Z
  to limit it to some layers. Layers outside every cylinder use the spheres, or else the normal `seam_position`.

A cylinder covering a layer wins over the spheres there. When several attractors are equally near (two cylinders on two
islands), each loop takes the one nearest to it. The attractor overrides `seam_position` (aligned, rear, nearest,
random) wherever it applies. **Seam painting still wins** (user's choice; SuperSlicer ignores the painting): on a loop
with painted enforcers the seam goes to the enforced point nearest to the attractor, and blocked points are used only if
the whole loop is blocked. Staggered inner seams, scarf seams and the seam notch work as usual from the chosen point.

The attractors are shown yellow (SuperSlicer's color), are not printed, and can be moved, scaled, deleted and changed
with "Change type" like support enforcers. Moving one only exports the G-code again (no re-slice). Saved in the 3mf with
SuperSlicer's type names (`SeamPositionCenter`, `SeamPositionCenterZ`), so SuperSlicer projects with attractors load
with them and SuperSlicer loads ours. SuperSlicer's "Enforce seam position (inside shape)" is not ported; its parts load
as attractors (`SeamPositionInside` as a cylinder, `SeamPositionInsideCenter` as a sphere). Stock PrusaSlicer loads the
attractors of a fork project as printed parts.

No setting; no attractor = stock seams (golden test unchanged).

**How it works.** `ModelVolumeType::SEAM_POSITION_CENTER` / `SEAM_POSITION_CENTER_Z` (`ModelVolume::is_seam_attractor()`).
`GCode/SeamAttractor.cpp`: `get_layer_attractors()` computes per object layer the attractor points in the object's
slicing coordinates (`trafo_centered()`, as the seam painting): sphere = center of its bounding box, cylinder = center of
its section sliced at the layer's `slice_z`, keeping the ones nearest in Z. `Seams::Placer::init()` stores them per
object; `place_seam()` replaces the seam choice of the perimeter by `Attractor::choose_seam_point()` (the nearest point
among the enforced points, else the common ones, else the blocked ones) before `finalize_seam_position()`, for every
`seam_position`. `PrintApply.cpp`: attractor changes invalidate only `psGCodeExport` and are synchronized like the
support enforcers; the seam placer cache key (section 21) includes their type, mesh size and matrix. GUI: menu in
`GUI_Factories.cpp`, shapes and placement (behind the middle of the rear side) in `GUI_ObjectList.cpp`
`load_generic_subobject()`, color in `3DScene.cpp`.

**How to test.** `fff_print_tests "[SeamAttractor]"`: sphere at the right side, every `seam_position`: every layer's seam
on the right side at the sphere's Y; a cylinder up to 10 mm and a sphere above: cylinder below 10 mm, sphere above;
painted left side wins over a sphere on the right; moving the sphere keeps the perimeters and rebuilds the seam placer;
3mf round trip with the SuperSlicer names. Manual: right click a part > Add seam attractor > sphere, move it around the
part, look at the seams in the preview (Legend > Seams); add a cylinder, scale it in Z, check the layers above it.

---

## 36. Open the settings of a G-code

**What it does.** Every G-code ends with the settings it was sliced with. This opens them in a **new SuperPSlicer
window**, ready to slice again (add the model yourself):

- G-code viewer: **File > Open settings in SuperPSlicer**, or the link in the notification shown after a G-code is loaded.
- Slicer: **File > Import > Open Settings from G-code in a New Window...** (pick the G-code file).

The new window picks presets for the **nozzle of the G-code**: the printer named in the G-code if it exists with that
nozzle, else the last used printer if its nozzle matches, else the first printer with that nozzle; then the print and
filament named in the G-code if they are compatible with that printer, else the last used ones if compatible, else the
first compatible ones. The G-code's values are loaded over these presets as **unsaved changes** (orange, nothing saved;
save them as new presets if you want). The preset names and compatibility conditions stay the presets' own. A dialog lists
the presets used (and the names in the G-code if different), how many settings differ, and what was **not carried over**:
settings this fork does not have, values it cannot read (the preset keeps its own value), and, for SuperSlicer G-code,
the conversions made. With no printer for that nozzle the selected printer is kept and the dialog says so. A G-code with
more extruders than the printer preset gives its first extruder's values.

Works with G-code from this fork, PrusaSlicer (ASCII 2.4+ and binary G-code) and **SuperSlicer** (`; SuperSlicer_config`
block). SuperSlicer values are converted with the rules of the bundle converter (`build/convert_ss_bundle.py`): widths,
speeds and accelerations in % of another option than here are resolved to mm or mm/s, disabled fan speeds (`!x`) become
-1, SuperSlicer-only options with an equivalent here are mapped (`first_layer_size_compensation`, `only_one_perimeter_top`,
interior brim, `filament_shrink`, dynamic overhang switches, infill direction, small area flow, thumbnails), G-code
templates are fixed (`extruder_temperature_offset`, per-filament variables without an index, also with spaces inside the
braces, `layer_num` in the before layer G-code). Widths set as a spacing in SuperSlicer (the width is written empty,
2026-10-27): width = spacing + layer height × (1 − π/4) × overlap (SuperSlicer `Flow::new_from_config()`: overlap =
`perimeter_overlap` / `solid_infill_overlap` / `external_perimeter_overlap`, 1 for infill and top infill, capped by
`filament_max_overlap`; the external perimeter spacing is a half spacing: overlap − 0.5; the first layer width uses the
first layer height; spacing 0 = auto width 0). A value whose type refuses it (a graph it cannot read) is listed as
unreadable instead of stopping the import. The G-code viewer loads SuperSlicer G-code with this same conversion
(section 37). Not converted: the converter's rules for the user's own printers (AWD keep-out zones from
`avoid_bed_notches`, Klipper limits): the printer preset keeps its own.

An empty list value where values are required (`cooling_slowdown_logic = `, written by G-code from the default config
before the fix in section 37) is listed as unreadable and the preset keeps its value.

**How it works.** `libslic3r/GCodeSettingsImport.cpp`: `read_gcode_settings()` reads the key / value pairs of the block
(tail of the file first, the whole file if needed; binary G-code: the slicer metadata block) and the generator from the
`; generated by` line; `import_gcode_settings()` converts SuperSlicer values (`SuperSlicerConverter`), then loads every
pair with `handle_legacy()` / `set_deserialize()` and the forward compatibility substitution, collecting the unknown
keys and the substituted values (which are removed). `GUI/GCodeSettingsLoader.cpp`: `open_gcode_settings_in_new_window()`
starts `superpslicer.exe --gcode-settings <file> --no-single-instance` (`start_new_slicer_with_gcode_settings()` in
`Utils/Process.cpp`); `GUI_App::post_init()` calls `load_gcode_settings()`, which selects the presets through the tabs,
applies the values to the tabs' edited configs (vectors resized to the preset's extruder count) and shows the report.

**How to test.** `fff_print_tests "[GCodeSettings]"`: a sliced G-code reads back with the values set; unknown and
unreadable values are listed and not loaded; a SuperSlicer block is converted (width in % of the nozzle -> mm, `!0` fan
-> -1, `first_layer_size_compensation`, `seam_position = cost`, thumbnails, vector variable in the start G-code);
"every SuperSlicer conversion" checks each rule of the converter (all fan keys, disabled values, enum maps, every
percentage option and its base, graphs, layer times, mapped options, spacing widths, brim, 0 accelerations, filament,
printer). Manual:
open a G-code in the viewer, click the notification link; check the presets and the orange values in the new window;
do the same with a SuperSlicer G-code and read the list.

---

## 37. Stock bug fixes

PrusaSlicer 2.9.6 bugs found while testing (2026-10-23, 2026-10-27):

- **Command line crash loading a print preset on its own.** `--load "<print preset>.ini"` (without the printer preset
  in the same file) crashed: `DynamicPrintConfig::normalize_fdm()` (`PrintConfig.cpp`) read `nozzle_diameter` to check
  `wipe_tower_extruder` and `bed_temperature_extruder`, which every 2.9 print preset has, without checking that the key
  exists. Now the check is skipped when there is no `nozzle_diameter` (it runs again on the full config). The GUI was not
  affected. Not in 2.9.0.
- **Empty `cooling_slowdown_logic` in the settings block.** `ConfigOptionEnums<T>::serialize()` (`Config.hpp`) wrote only
  the values after the first one, so a typed list of enums with one value serialized as an empty string. It showed in
  G-code and configs written from the default config (command line without presets, tests): `; cooling_slowdown_logic = `;
  loading that back gave an empty list, and the cooling read its first value (undefined). GUI exports were not affected
  (their options are the generic type, which was correct).
- **SuperSlicer G-code did not open in the G-code viewer** (2026-10-27). The viewer read the settings of a SuperSlicer
  G-code backwards from the last 64 kB and took the values as they are (stock `load_from_superslicer_gcode_file()`,
  `GCodeProcessor.cpp`). With SuperSlicer's `thumbnails_end_file` the thumbnails come after the settings, so it found
  none ("Suspiciously low number of configuration values extracted from ...: 0"); a SuperSlicer disabled value stopped it
  ("Invalid value provided for parameter support_material_bottom_interface_layers: !0"). Now the viewer reads and
  converts the settings as "Open settings in SuperPSlicer" does (section 36, `read_gcode_settings()` +
  `import_gcode_settings()`); values that still cannot be read keep the default. `read_gcode_settings()` also reads
  G-code without a `..._config = begin/end` block (older Slic3r / PrusaSlicer / SuperSlicer, at least 80 `; key = value`
  lines at the end, as stock), past thumbnails at the end of the file.

Output: the settings block of G-code from the default config now has `cooling_slowdown_logic = uniform_cooling` instead
of an empty value (a comment, the moves are unchanged). The golden test treats that line as equal to stock's empty one.

**How to test.** `libslic3r_tests "[Config]"`: "Typed enum vector serializes every value", "normalize_fdm without
nozzle_diameter". Manually: `superpslicer-console --export-gcode --load "<print preset>.ini" model.stl` no longer crashes.
`fff_print_tests "[GCodeSettings]"`: "G-code viewer: SuperSlicer G-code opens" (with / without the block delimiters,
with / without thumbnails at the end, disabled values); real files: `set SS_GCODE_FILES=a.gcode;b.gcode` then
`fff_print_tests "[.GCodeFiles]"` (each must open in the viewer, lists what was not converted). Manually: open a
SuperSlicer G-code sliced with "Thumbnails at the end of the file" in the G-code viewer.

---

## 38. Per region G-code

**What it does.** From SuperSlicer: `region_gcode` ("Per region G-code"), custom G-code inserted each time a region starts
to print its perimeters, infill or ironing on a layer (on every island), before its first extrusion. Mainly useful as an
object or part setting (right click > Add settings > Output options), e.g. a pressure advance value per part; the pressure
calibration (section 30) uses it. Empty by default: no change to the G-code.

**Settings.** Print Settings > Output options > Per region G-code (Expert), also an object / part setting. Placeholders: all
the settings, `[layer_num]` (0 = first layer) and `[layer_z]`. The code should not move the print head.

**How it works.** `PrintRegionConfig::region_gcode` (`PrintConfig.cpp/.hpp`). `GCodeGenerator::region_gcode()` (`GCode.cpp`)
runs it through the placeholder parser after the region config is applied in `extrude_perimeters()` and
`extrude_infill_ranges()` (infill and ironing). Regions with a different code are never merged: the perimeter merge
(`Layer.cpp`, `has_compatible_layer_regions`) and the infill merge (`Fill.cpp`, `SurfaceFillParams`) compare it, so each
part prints its own code. Changing it redoes the perimeters and infill, as the merge may change (`PrintObject.cpp`). Checked for unknown custom G-code
tags like the other custom G-codes. Differences from SuperSlicer: SuperSlicer queues the code with the object label start;
here it is written directly before the region's first travel. Ported and removed on 2026-10-20 with the first SuperSlicer
pressure calibration, back on 2026-10-23.

**How to test.** `fff_print_tests "[RegionGcode]"`: empty by default; as a print setting it is written on every layer before
the first `;TYPE:` with `[layer_num]` filled in; as part settings two parts of one object each write their own code on every
layer. The golden test keeps it empty.

---

## 39. Overhang hysteresis

**What it does.** Own feature (2026-10-24). A wall about as steep as the overhang threshold angle (section 28), e.g. a
45° chamfer with a 0 % threshold, has the middle of its perimeters right on the grown edge of the layer below; the facets
of the mesh and the slicing put it a little in front of or behind it, so stock PrusaSlicer cuts each loop into many short
pieces flipping between overhang perimeter and perimeter (speed, flow, fan changing every few mm; 27 pieces per layer
on the user's LeftUmbilicalMount2 chamfers, 80 on a test cone). With a hysteresis, each stretch of perimeter that stays
within the hysteresis of the threshold is an overhang or not as a whole, so a loop changes role once per real crossing.

**Settings.** Print > Layers and perimeters > Quality, "Detect bridging perimeters" line: Hysteresis
(`overhangs_hysteresis`), mm or % of the nozzle diameter, **default 15 %** (user, 2026-10-24: the smallest value that
works for the 0.25 mm nozzle too, see the tests below); 0 = off (stock), which the golden test sets. Greyed out without
perimeters or "Detect bridging perimeters".

**How it works.** `apply_overhang_hysteresis()` in PerimeterGenerator.cpp, for the classic and the Arachne generator,
between the clipping of a perimeter by the grown lower slices and the extrusion paths (Arachne keeps its widths: the
clipped pieces still carry them):
- Only when the perimeter has both supported and overhang pieces and the hysteresis is > 0.
- Each piece is sampled (every half hysteresis, at least 0.05 mm) for its distance to the edge of the grown lower slices
  (`AABBTreeLines`, the lower slices clipped to the perimeter's box grown by 2 x hysteresis + 1 mm, so that the cut
  along the box is not taken for an edge). A piece that gets further than the hysteresis from the edge is *strong* and
  keeps its role; the others are *weak*.
- The pieces are put back in order along the perimeter by their end points (the clipping keeps neither the order nor
  the direction); a closed loop is walked from a strong piece.
- Each run of weak pieces between strong ones gets one role: overhang if its average distance past the edge (length
  weighted, inside negative) is positive. A perimeter with no strong piece is one run.
- Neighbouring pieces of the same role are joined into one path.
Negative thresholds (section 28) use the same code.

**How to test.** `[OverhangHysteresis]` (test_overhang_hysteresis.cpp), both generators, 0.4 mm nozzle and lines,
0.2 mm layers, 0 % threshold (45°): a 180-facet cone at 46° flips (> 4 overhang runs in a layer) at 0 and has at most
one run per loop at 10 %; a straight 46° face (no facets) gives the same overhang as stock; a cylinder leaning 60°
keeps its overhang (±5 %); 40° walls stay walls. "Negative overhang threshold": -10 % makes a 42° face an overhang and
not a 36° one; -40 % is accepted, -45 % and -0.2 mm are refused. The golden test keeps the default 0.
Matrix run 2026-10-24 (scratchpad script, CLI): 5 shapes (cones of 48 and 180 facets, a hole narrowing like the user's,
a leaning cylinder, a straight face) x 11 angles (20-70° + the threshold angle -3/-1/0/+1/+3°) x nozzle/layer
0.25/0.08, 0.25/0.15, 0.4/0.1, 0.4/0.2, 0.4/0.3, 0.6/0.2, 0.6/0.4 x threshold -5/0/25/50 % x both generators, 3060
objects per hysteresis value. Objects with more than one overhang run per loop: 392 at 0, 90 at 5 %, 10 at 10 %,
3 at 15 % (the 3 = a seam inside the overhang of a 70° leaning cylinder, same as stock); overhang runs shorter than
1 mm: 144618 / 9163 / 2606 / 2480. No object that was clean in stock changed its overhang (> 5 % and > 2 mm).
The 0.25 mm nozzle needs 15 % (10 % = 0.025 mm is below the slicing noise there). Manual: the user's
LeftUmbilicalMount2 project, 0 % threshold, chamfer rings 39-49 pieces per layer -> 3 at 15 %.

---

## 40. The other form of a value or percentage next to the field

**What it does.** In the settings tabs and the object settings, a field taking a value or a percentage (unit "mm or %",
"mm/s or %", "mm/s² or %") shows after its unit the other form of its value, as the slicer computes it: for a percentage
its absolute value, `[225%] mm or %  (0.45 mm)`; for an absolute value its percentage, `[0.45] mm or %  (225 %)`. Nothing
for 0 (auto / disabled) or when the base is unknown or 0. Extrusion widths are a percentage of the layer height
in PrusaSlicer (the first layer width: of the first layer height), not of the nozzle as in SuperSlicer: 100 % at 0.2 mm
layers is 0.2 mm. The other settings follow their base, also chains (top solid infill speed % of solid infill speed % of
infill speed); the accelerations: % of the default acceleration (section 4). It changes when the value is entered (Enter /
leaving the field), when a setting it depends on is changed on the shown page, and when a page is shown or settings are
loaded. (2026-10-24, user.)

**How it works.** `percent_value_hint()` (`libslic3r/PercentValue.cpp`): the base of the setting's percentage
(`percent_base_value()`: its `ratio_over`, resolved through chains; widths: the layer height, as
`Flow::new_from_config_width()`; `nozzle_diameter` the first extruder), then value x base / 100 or value / base x 100.
`ConfigOptionsGroup::update_value_hints()` (`OptionsGroup.cpp`) computes the texts with the presets' full config plus the
group's config (object settings) from `reload_config()` and `on_change_OG()`; `Tab::on_value_change()` updates the groups
of the shown page; `OG_CustomCtrl` draws the text after the unit, its column wide enough for "(0000 unit)" ("(00000 mm/s²)" for accelerations) so the layout
doesn't move when a value becomes a percentage (room for the wider of "(0000 unit)" and "(000.0 %)").

**How to test.** `fff_print_tests "[PercentValue]"`. Manual (GUI): Print Settings > Advanced: extrusion width `225%` and
Tab: `(0.45 mm)` with 0.2 mm layers, `0.45`: `(225 %)`; change the layer height on its page and come back: the value follows.

---

## 41. Plain seam instead of the scarf joint near overhangs or corners

**What it does.** A normal seam instead of the scarf joint when an overhang is on the scarf joint or near it. The scarf
joint runs `scarf_seam_length` back from the seam and stops at the first overhang point. Next to the edge of a horizontal
hole the hole's overhanging top cuts it short at a different place on every layer, and the ramps leave streaks on the
wall beside the hole (seen on a user's part, 2026-10-28). On the layers below the top of the hole, where the hole splits the
wall, the scarf joint runs along the face and around the edge of the hole, which also moves on every layer: same streaks.
So the option also drops a scarf joint that goes around a sharp corner. Those loops get a normal seam; a scarf joint
along one flat face stays. A scarf joint around any other corner (an inside corner, a short face) is dropped too, even
where that corner is the same on every layer; the corner the seam sits on doesn't count.

| Setting | Where | Default | Meaning |
|---|---|---|---|
| `scarf_seam_not_near_overhangs` | Print > Layers and perimeters > Advanced > Plain seam instead of scarf: *Near overhangs or corners* (renamed 2026-10-28 from "Scarf joint near overhangs or corners: None") | off | Off = stock. |
| `scarf_seam_overhang_distance` | same row: *Overhang distance* | `5` mm | Along the perimeter: from the start of the scarf joint (scarf length + this from the seam), and from the seam on the other side. |

Both are greyed out without a scarf joint, the distance also while the option is off.

**How it works.** `finalize_seam_position()` (`GCode/SeamPlacer.cpp`) already drops the scarf when the seam point itself
is on an overhang with thick bridges; with the option it also drops it when `is_overhang_near()` finds a perimeter point
classified as overhang (the seam placer's own overhang detection) within scarf length + distance in the scarf direction
or within the distance in the other direction (the whole loop with `scarf_seam_entire_loop`), walking the perimeter
with `offset_along_perimeter()`. `is_corner_on_scarf()` walks the scarf joint the same way and stops at a point the seam
placer marks as not smooth (`angle_types`: convex over 10°, concave over 15°, 0.25 / 0.5 mm arms, so curves under about
3 mm radius count too). The seam's own corner is skipped: a run of corner points of one kind (convex or concave) that
starts within 1 mm of the seam, however long (rounded corners). Decided per loop at G-code export, so the seam placer cache (section 21) is unaffected.

**How to test.** `fff_print_tests "[Scarf]"` (`cube_with_hole` on its side, a horizontal hole: fewer scarf joints on the overhang layers, the other
layers fewer too, where the scarf joint would go around the hole edge; a 20 mm cube with a 10 mm scarf joint keeps all of
them). Manual: scarf joint "Everywhere", seam "Rear", a part with a horizontal hole in its rear wall; without
the option the scarf ramps (Z rising inside the external perimeter) start beside the hole on the hole layers, with it
those layers have a plain seam. On the user's plate (`WoodHolderSide_PLA-PolyMaker.3mf`, 2026-10-28) the tab with the
hole on Banister2StudSide has no scarf on the hole layers (34.3-39.5 mm), the faces above and below keep theirs.

**Seeing the scarf joints in the preview.** With the legend's *Seams* option on, the preview shows a seam marker at
both ends of each scarf joint ramp, besides the stock marker where the loop ends. `GCodeProcessor::process_G1()` stores
an `EMoveType::Seam` vertex at the start of the first and of the first-after-last move of a run of external (or
overhang) perimeter extrusions raising Z (not in spiral vase mode). Only the preview changes, not the G-code. Test:
`fff_print_tests "[Scarf]"` ("Seam markers at both ends of a scarf joint": two more markers per ramp).

---

## 42. Preview: no fade after an overhang piece (stock bug fix)

**What it was.** In the Speed, Height, Width and Volumetric flow views, a long wall move right after an overhang
perimeter piece was drawn fading from the overhang's values to the wall's over most of its length, although the G-code
has constant values on it (seen at the top of horizontal holes, 2026-10-28). Stock PrusaSlicer 2.9.6 does the same;
upstream master (3.0 alpha, `libpgcode/TimeMachine.cpp`) still has the code. No G-code change.

**Why.** For the Actual speed view the time estimator inserts points inside each move where the acceleration ends and
where the braking starts (`internal_only` vertices). Stock gave those points the feed rate, width, height, flow, fan and
temperature blended between the previous move and this one (`lerp(prev_move.X, curr_move.X, t)` when both are
extrusions). The preview colors each piece between two points from its ends and blends in between, so the long piece
between the two inserted points faded. Only visible where an extrusion follows another one without a travel and with
other values: an overhang piece inside a wall loop (every other feature change has a travel in between, which stops the
blend).

**Fix.** `TimeMachine::calculate_time()` (`GCode/GCodeProcessor.cpp`): the inserted points get the move's own values; only
their actual speed (the trapezoid's cruise feed rate) differs, as before. The position and the extruder value are
unchanged.

**How to test.** `fff_print_tests "[Preview]"`: a G-code snippet (overhang piece then a 130 mm wall move: both inserted
points have the wall's width, height and speed; fails with the stock code) and a sliced cube with a horizontal hole
(every inserted point matches its move). Models: `Stuff/hole_trail_test.stl` (60 x 10 x 30 wall, 5.5 and 8 mm holes,
`Stuff/hole_trail_test.py`) and `Stuff/overhang_test.stl` (159 x 10 x 40 wall: round holes 3 / 5.5 / 8 / 12 mm,
teardrop, diamond, 12 x 6 mm window, arch in the bottom edge, 60 degree slope, 45 degree slope and a 6 mm ledge;
`Stuff/overhang_test.py`). Slice with the user's profile and look at Speed / Height / Width / Volumetric flow at the
top of the holes: no fade along the wall; Actual speed still ramps.

---

## 43. Overhangs cut (SuperSlicer)

**What it does.** From SuperSlicer 2.5.60 ("Overhangs cut", `overhangs_max_slope`). Before perimeters and infill are made,
each layer is cut back where it would stick out past the layer below by more than the max slope. A round horizontal
hole gets steep sides and a short flat bridge on top instead of a top that closes with ever longer overhangs (left: model,
right: cut, as in the SuperSlicer release notes). Areas that can be bridged are not cut. The printed part is smaller than the model there (the cut only removes material: a hole can end a layer higher).

| Setting | Where | Default | Meaning |
|---|---|---|---|
| `overhangs_max_slope` | Print > Layers and perimeters > Quality > Overhangs cut: *Max slope* | `0` = off | mm or % of the (largest) nozzle diameter that a layer may stick out past the layer below. SuperSlicer's example: 50 %. |
| `overhangs_bridge_threshold` | same row: *Bridge max length* | `-1` = any length | Areas bridged with lines up to this long are kept; 0 = no bridges kept (everything cut). SuperSlicer: disabled = any length. |

Region options (per object / modifier). The row is right under "Detect bridging perimeters"; the bridge options are
greyed out while the max slope is 0, the upper layers also with a bridge max length of 0. Below the two rows, next to the
overhang threshold drawing (`OverhangAnglePreview`, both in one wrapping line), `OverhangCutPreview` draws the top half of
a 10 mm horizontal hole as cut: the layers, the material of the model cut away (light orange), the flat bridge (blue) and
the round hole of the model (dashed), from the layer height, the nozzle and these options (approximate: the slicer works
on the real outlines), with the slope per layer, the width of the flat top and how many layers the hole gets taller.

**How it works.** `PrintObject::apply_overhangs_cut()` (`PrintObjectSlice.cpp`), a port of SuperSlicer 2.7.63
`PrintObject::_max_overhang_threshold()`, called in `PrintObject::slice()` right after `slice_volumes()` (so after the XY
and hole size compensations). Layer by layer from the bottom (each layer uses the already cut layer below): supported
area = layer ∩ layer below; for each region with a max slope, the unsupported part wider than a nozzle goes through
PrusaSlicer's `BridgeDetector` (direction) and `overhangs_cut_bridge_coverage()` (lines at that direction, spaced by the
bridge spacing, anchored on two separate pieces of the layer below, not longer than the bridge max length): that part
is kept. Rules of our own, found on the user's screw hole block with their profile (25 %, 0.2 mm layers), 2026-10-28:
- The test lines are clipped half as far out as the anchors reach (their ends inside the anchors, not on the edge, where
  rounding rejected most of them) and to the material of the layer shrunk by as much, so a line passing over or grazing
  empty space of the layer (a screw hole, also when it splits the area in two) is not anchored: the edge of a bridged
  part next to a hole is one straight line, not kinked toward the hole (those kinks were in the perimeters along it).
- An unsupported area around a hole that closes in the layer above (any material of the next layer more than 0.02 mm
  inside it: a dome closes by only the slope per layer) is a slope, decided on the whole area before the nozzle-width
  filter (which breaks a thin ring into arcs): no bridge there. Chords across the ring had turned the dome's round steps
  into a lumpy polygon.
- Around a hole that stays open (a screw hole over a counterbore or a countersink, also one the cut itself left
  floating) the unsupported area is kept as the model has it: PrusaSlicer bridges it in one layer with overhang
  perimeters around the hole. The cut makes no bridges there (user, 2026-10-30: "with overhangs cut on and floating
  holes off, do not create any bridges on unsupported holes. let the checkbox handle that"): bridge layers under a
  floating hole are "Support floating holes" (section 44). Until 2026-10-30 the cut printed bridges beside the hole, then
  across the band over it, leaving a square for the round hole (rules for the band, the square's corners and the
  extension to the hole's tangents; the extension in `overhangs_cut_bridge_coverage()` is still there for other holes
  next to bridged areas). The floating holes found by section 44 are also marked on their ceiling layer
  (`Layer::floating_hole_zones`) and left as sliced, with the option on or off.
- A ring around an open hole that is near the support all around but touches it nowhere, with the layer below cut next
  to it (a dome closing faster than the slope follows it: the last ring around the hole) is left to the slope until it
  touches (then kept, above): printed as is it was a full loop of overhang perimeters, bridged its chords gave flat
  sides. "Touches": within a quarter of a line width (at half a line width the touch came at 0.22 mm, and the hole's
  outer perimeter, 0.2 mm off the edge, was overhang).

SuperSlicer's third option, `overhangs_bridge_upper_layers` ("Consider upper bridges": leave empty, on the layers under
a bridge, the area that bridge will cover, instead of growing overhangs into it), was ported, renamed "Look ahead for
bridges", fixed (it did nothing under a pocket closing with a flat ceiling) and then removed on 2026-10-30: on a test
pocket at 25 % it only kept the last layers under the bridge from narrowing by 0.1 mm a side and made the bridge
longer (12.4 -> 12.8 mm at its default), and the user saw no purpose in it. The key is ignored when a profile or a
SuperSlicer G-code has it (`PrintConfigDef_ignore`; also dropped by `build/convert_ss_bundle.py`).
The region slices are then cut to the supported area grown by the slope (limited
to 1.5 x the slope, at least 0.5 x, plus the bridges), with SuperSlicer's `smoothen()` against zig-zags, and the layer
outlines are rebuilt (`make_slices()`). The cut outlines are put in a fixed order (each starting at its lowest point,
holes and islands sorted: `overhangs_cut_canonical()`): the layers come out of the parallel slicing in an order that
changes from run to run, the cut followed it, and two exports of the same plate gave different G-code (found
2026-10-30: 2 outcomes in 10 slices of `Stuff/overhang_test.stl`; now 12 of 12 identical). Off (0) = nothing runs, stock
slices.

Differences to SuperSlicer: PrusaSlicer's bridge detector is used for the direction (SuperSlicer's was rewritten; ours
also drives the normal bridges, so it is left unchanged); SuperSlicer's bridge max length test is never reached
(`good_line` is still false in `BridgeDetector::detect_angle`), here the length is applied. SuperSlicer "!x" (disabled)
values are converted to -1 (G-code settings import, `build/convert_ss_bundle.py`); the user's SuperSlicer profile has
the defaults (0, disabled, 2).

**How to test.** `fff_print_tests "[OverhangsCut]"`: `tests/data/wall_round_hole.obj` (30 x 10 x 30 mm wall, 12 mm hole):
without the cut the top closes by more than 0.6 mm per layer, at 50 % (0.2 mm) by at most 0.4 mm (both sides) with a flat
bridge over 3 mm wide; bridge max length 0 cuts to the top; the options don't change anything while the slope is 0.
`tests/data/screw_holes.obj` (the screw hole block of `Stuff/overhang_test.py`, written by
`python overhang_test.py overhang_test.stl ../tests/data/screw_holes.obj`), 0.2 mm layers, 4 perimeters, 25 %, overhang
threshold 0: for the counterbores M2..M8, no overhang below the ceiling, the ceiling as sliced (overhang perimeters
around the hole), from there one hole of the model's size per layer and no overhang above the ceiling; for the domes,
never an overhang perimeter, one hole per layer narrowing by at most the slope, the model's size from 2.5 mm above the
dome (with the slope ring rule taken out, the domes fail).
Manual: `Stuff/overhang_test.stl` at 50 %: the 12 mm hole narrows by 0.40 mm per layer and closes with a 6.3 mm bridge
(stock: up to 2.66 mm per layer, 1.3 mm last gap); overhang perimeter moves 606 -> 286. The user's plate slices in the
same time (5.7 / 5.8 s). The model also has a 82.9 x 80 x 15 mm block (behind: the floating holes row of section 44 and a countersink) with screw holes M2 / M3 / M4 / M5 / M6 / M8
printed from below (clearance hole 2.4 / 3.4 / 4.5 / 5.5 / 6.6 / 9 mm; front row counterbores 4.4 / 6.5 / 8 / 10 / 11 / 15
mm, 4 mm deep with a flat ceiling; back row bores of the same sizes whose domes narrow into the holes): with the user's
profile (25 %) each counterbore ceiling is printed as sliced (one bridge layer); the domes are cut to round steps of the slope
(0.1 mm per layer, 72-point circles) without any overhang perimeter. Checked by rendering the sliced layers with the
user's own print / filament / printer presets, also rotated 30 and 65 degrees (2026-10-28, with the counterbore rules
since removed).

---

## 44. Support floating holes (bridge layers)

**What it does.** A hole floating over a void, with material all around the void on the layer below (a screw hole over a
counterbore printed from below, a hole in a ceiling over a cavity), is printed on sacrificial bridge layers: each layer
over the void has two parallel bridges, one on each side of the hole and touching it, and each layer is turned by
180 / N degrees from the one below (2 layers 90 degrees apart, 4 layers 45 degrees apart). After N layers the opening is a
regular polygon with 2N sides around the hole (a square, a hexagon, an octagon...) and the hole is printed on it, round.
Without it, the first layer over the void is bridged with a direction each piece picks for itself (the two sides were not
parallel, the square skewed) and the round hole's perimeters cross the corners, with bridge infill in the bigger corners
(user's screenshots 2026-10-30). The drawing `Stuff/Hole bridges drawing.png` / `.svg` (made with the user 2026-10-30)
shows the layers for M3 / M5 / M8 / 20 mm holes and N = 2 / 3 / 4 / 6, with the bridge lengths and corner gaps.

| Setting | Where | Default | Meaning |
|---|---|---|---|
| `floating_hole_bridges` | Print > Layers and perimeters > Quality, below Overhangs cut: *Support floating holes* (named "Floating holes: bridge layers" until 2026-10-30) | off | Region option (per object / modifier). On: the bridge layers above. Off: the layer over the void as sliced (one PrusaSlicer bridge layer; the overhangs cut leaves it too), and a warning (below). |

- **N** is chosen per hole (the user asked for automatic, 2026-10-30): the smallest N (2 to 16) whose polygon corners stay
  within half an external perimeter width of the hole. Round holes with 0.45 mm walls: M2 3 layers, M3 / M4 4, M5 / M6 5,
  M8 6. The limit is fixed for now (user: "lets try fixed for now"). A larger N does not shorten the first layer's bridges
  (they cross the void beside the hole); it shrinks the corners the hole's perimeters have to cross.
- **Direction of the first layer**: per direction (whole degrees), the layers it needs; the fewest layers first (a hexagon
  hole: 3 layers along its sides; across its corners the polygon never fits it), then the closest fit, then the shortest
  bridges (the longest bridge line of the first layer, lines laid from the hole's tangents outwards); X when that is as
  short (a round void). A 4 mm hole in a 16 x 8 mm cavity gets its first bridges across the 8 mm.
- **Any shape**: the strips touch the hole in each direction, so a slot, a square or a hexagon gets the polygon around it;
  the void can have any shape. Needed: the void is a hole of the layer below with the hole inside it and at least an
  external perimeter width of material around it, one hole in the void, nothing inside the hole or the void, the ceiling
  closed around the hole, and the same hole on the next layer (the steps of a countersink printed from below narrow on
  every layer: a slope, not a floating hole). The upper part of a stepped pocket (an 8 mm pocket over a 12 mm one) is
  one: it floats over the void for the layers it goes up. The layers go on while the layer above still has the same hole (same place, area within 10 %)
  and closes the void; a hole that ends earlier gets fewer layers. Two holes over one void, or a hole partly over open air
  (a ledge), are left as they are: the bridges need material at both ends.
- **Only removes material**: the bridge layers' opening is larger than the model's hole.
- **Warning** (option off): after slicing, one notification per object: "N floating hole(s) wider than 2 mm over a void
  (the widest X mm, at Z mm)...", with the option's name. Width = the largest width of the hole over the directions; the
  1.6 mm test hole gives none (user, 2026-10-30: warn above about 2 mm).

**How it works.** `PrintObject::apply_floating_hole_bridges()` (`PrintObjectSlice.cpp`), in `PrintObject::slice()` after
`slice_volumes()` and before the overhangs cut, and again after the cut (when it ran) for the holes the cut itself left
floating: the cut widens the last steps of a countersink printed from below, so its screw hole starts over a void of about
9 mm (user's screenshot 2026-10-30: that hole had no bridge layers). Holes found by the first pass are skipped; one warning
for both passes. Layer by layer from the bottom: finds the floating holes (above), picks
the direction and N, and cuts the region slices of the N layers to the void minus the strips of the layers so far
(`floating_hole_opening()`; outlines put in a fixed order as the overhangs cut does, for repeatable G-code). Per layer it
stores the bridge pieces with their direction in `Layer::forced_bridges` and the void in `Layer::floating_hole_zones` (also
on the layer with the hole on the polygon). `LayerRegion::process_external_surfaces()` gives each bridge surface over a
piece that direction instead of the detected one; a surface over the pieces of two holes with different directions
(PrusaSlicer merges bridges whose anchors touch) is split between them, each part to the nearest piece. The overhangs cut
keeps the zones as they are. Overhang perimeters along the slot edges are the usual "Detect bridging perimeters" ones.

**How to test.** `fff_print_tests "[FloatingHoles]"` on `tests/data/screw_holes.obj` (the screw hole block of
`Stuff/overhang_test.py`, now Y 20..100, its third row at Y 70: a 1.6 mm round hole, a 3 x 7 mm slot, a 5 mm square turned
20 degrees, a 4 mm hole off center in a 16 x 8 mm cavity, a 6 mm hexagon in a 12 mm hexagon cavity; each over a 4 mm
deep cavity; a fourth row at Y 90 has a 10 mm countersink printed from below into a 3.4 mm hole): off by default, no
bridge layers, the voids marked on the ceiling layer only (11), and the warning counts 10 holes (6 counterbores + 4; not
the 1.6 mm one); on
(overhangs cut off and at 25 %): consecutive bridge layers from 4.2 mm, each turned by 180 / N, round holes with the
expected N, first direction X for round voids and Y for the 16 x 8 mm one, 2 layers for the square and 3 for the hexagon,
the last opening around the hole with corners within the limit, the model's hole on the next layer, every bridge infill
line of a hole's pieces along its layer's direction; two slices give the same layers; with the overhangs cut at 25 % the
countersink's hole (left floating by the cut) gets bridge layers from 5.2 mm, and with the option off the warning counts
11 holes. With `FLOATING_HOLES_SVG=<folder>`
the test draws the layers 3.8 to 6 mm there (slices, extrusions by role). Manual: slice `Stuff/overhang_test.stl` with the
user's profile and the option on, look at the bridge layers in the preview (Feature type) above the counterbores and the
Y 70 row; with it off, the warning.

---

## Testing

- Build and run all tests: see CLAUDE.md. `ctest -C Release --output-on-failure` in `build\`, or a single test program,
  e.g. `build\tests\fff_print\Release\fff_print_tests.exe "[FanMover]"` (run it with `build\` as working directory).
- **Golden test** (`fff_print_tests "[Golden]"`): slices 4 models with PrusaSlicer defaults and with a Klipper config, and
  compares with the stock PrusaSlicer 2.9.6 G-code in `tests/data/golden`. It strips the header line and the keys added
  by this fork (list `added_config_keys` in `tests/fff_print/test_golden_gcode.cpp`, add new keys there), turns off
`internal_bridge_shortest_span` (on by default, section 20) and `overhangs_hysteresis` (15 % by default, section 39) and maps
  `Internal bridge infill` back to `Bridge infill`. The estimated time comments are not compared: with the Klipper
  config the Klipper limits (section 15) change them. Regenerate the reference only from an unmodified stock build:
  `PS_UPDATE_GOLDEN=1`. The Klipper references were regenerated on 2026-10-19 by a stock build without the dynamic overhang
  fan (this fork replaced it, section 27); the stock keys removed by this fork (`removed_config_keys`) are skipped in the
  references.
- Every feature has a tag listed in its section above.
