# Fork features (PrusaSlicer 2.9.6 + SuperSlicer ports)

This branch (`claude/poc-per-feature-2.9.6`) is PrusaSlicer 2.9.6 with a small set of features ported from
SuperSlicer 2.7 (and a few improvements). This document lists every feature added on top of stock PrusaSlicer:
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
8. [GUI layout](#8-gui-layout)
9. [Testing](#testing)

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

## 8. GUI layout

Rows grouped as in SuperSlicer (several fields with short labels on one row) for Filament > Cooling > Fan settings and
Print > Speed. Supporting GUI changes: `Line::force_sublabels` (show a short label even for a single field),
`OG_CustomCtrl` places fields with the same label width it draws them with, and an options group keeps an explicitly set
side text width. Regrouping the other pages is a TODO (see CLAUDE.md).

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
