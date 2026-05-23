#/|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
#/|/
#/|/ SuperSlicer is released under the terms of the AGPLv3 or higher
#/|/

"""
Polyholes plugin written against the high-level Python helpers.

This file intentionally avoids raw api.host calls and raw geometry handles. It
is meant as a readable example of how a Python plugin can walk the data tree,
inspect configs, edit mutable raw slices and keep host caches consistent through
the view layer.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

from slic3r_api import (
    CPoint,
    PluginBase,
    RAW_CO_BOOL,
    RAW_CO_FLOAT_OR_PERCENT,
    RAW_CONFIG_OPTION_MODE_ADV_EXP,
    RAW_CONFIG_OPTION_MODE_EXPERT,
    RAW_CONFIG_OPTION_MODE_SUSI,
    RAW_CONTAINER_TYPE_REGION,
    RAW_GUI_RULE_ACTION_ENABLE,
    RAW_GUI_RULE_CONDITION_BOOL_TRUE,
    RAW_OPTION_CATEGORY_SLICING,
    RAW_PRESET_TYPE_FFF_PRINT,
    RAW_PT_FFF,
    SCALED_EPSILON,
    STEP_POST_SLICING,
    STEP_SLICING,
    StoredPolygon,
    make_point,
    scale_i,
    unscaled,
)


POLYHOLES_KEY = "python_high_level_hole_to_polyhole"
POLYHOLES_THRESHOLD_KEY = "python_high_level_hole_to_polyhole_threshold"
POLYHOLES_TWISTED_KEY = "python_high_level_hole_to_polyhole_twisted"


@dataclass
class HoleData:
    center: CPoint
    max_diameter: float
    extruder_id: int
    max_deviation: int
    twist: bool
    points: list[CPoint]
    layer_region_idx: int


@dataclass
class LayerHole:
    points: list[CPoint]
    layer_idx: int
    layer_region_idx: int


@dataclass
class ThroughHole:
    hole_data: HoleData
    layers: list[LayerHole]


def _point_distance(lhs: CPoint, rhs: CPoint) -> float:
    return math.hypot(float(lhs.x - rhs.x), float(lhs.y - rhs.y))


def _point_mid(lhs: CPoint, rhs: CPoint) -> CPoint:
    return make_point((lhs.x + rhs.x) // 2, (lhs.y + rhs.y) // 2)


def _points_equal(lhs: list[CPoint], rhs: list[CPoint]) -> bool:
    if len(lhs) != len(rhs):
        return False
    return all(a.x == b.x and a.y == b.y for a, b in zip(lhs, rhs))


def _create_polyholes(api, storage: int, center: CPoint, radius: float, nozzle_diameter: int, twist: bool) -> list[StoredPolygon]:
    if nozzle_diameter <= 0:
        nozzle_diameter = 1
    edge_count = max(3, round(4.0 * unscaled(radius) * 0.4 / unscaled(nozzle_diameter)))
    polyhole_count = 5 if twist else 1
    rotation = 2.0 * math.pi / (edge_count * polyhole_count) if twist else 0.0
    new_radius = radius / math.cos(math.pi / edge_count)

    polygons = [api.new_polygon(storage) for _ in range(polyhole_count)]
    for poly_idx in range(polyhole_count):
        polygon_idx = poly_idx // 2 if poly_idx % 2 == 0 else (polyhole_count + 1) // 2 + poly_idx // 2
        polygon = polygons[polygon_idx]
        for edge_idx in range(edge_count):
            angle = rotation * poly_idx + 2.0 * math.pi * edge_idx / edge_count
            polygon.push_back(make_point(
                int(center.x + new_radius * math.cos(angle)),
                int(center.y + new_radius * math.sin(angle)),
            ))
        polygon.make_clockwise()
    return polygons


def _replace_matching_hole(expolygon, points_to_replace: list[CPoint], replacement: StoredPolygon) -> bool:
    for hole in expolygon.holes_mutable():
        if _points_equal(hole.points(), points_to_replace):
            hole.copy_from(replacement)
            return True
    return False


class PythonPolyholesHighLevelPlugin(PluginBase):
    def __init__(self, api):
        super().__init__("python.polyholes.high_level", STEP_POST_SLICING, priority=0)
        self.api = api

    def initialize(self, storage_address: int) -> None:
        self.api.create_option_def(
            opt_key=POLYHOLES_KEY,
            type=RAW_CO_BOOL,
            container_type=RAW_CONTAINER_TYPE_REGION,
            option_preset_type=RAW_PRESET_TYPE_FFF_PRINT,
            printer_technology=RAW_PT_FFF,
            label="Python high level: Convert round holes to polyholes",
            full_label="Python high level: Convert round holes to polyholes",
            category=RAW_OPTION_CATEGORY_SLICING,
            invalidates_step=STEP_SLICING,
            tooltip=(
                "Search for almost-circular holes that span more than one layer and convert the geometry "
                "to polyholes. This version uses the high-level Python helpers."
            ),
            mode=RAW_CONFIG_OPTION_MODE_ADV_EXP | RAW_CONFIG_OPTION_MODE_SUSI,
            default_serialized_value="0",
        )
        self.api.create_option_def(
            opt_key=POLYHOLES_THRESHOLD_KEY,
            type=RAW_CO_FLOAT_OR_PERCENT,
            container_type=RAW_CONTAINER_TYPE_REGION,
            option_preset_type=RAW_PRESET_TYPE_FFF_PRINT,
            printer_technology=RAW_PT_FFF,
            label="Python high level roundness margin",
            full_label="Python high level polyhole detection margin",
            category=RAW_OPTION_CATEGORY_SLICING,
            invalidates_step=STEP_SLICING,
            tooltip=(
                "Maximum deflection of a point to the estimated radius of the circle.\n"
                "In mm or in % of the radius."
            ),
            sidetext="mm or %",
            has_max_literal=1,
            max_literal_value=10.0,
            max_literal_is_percent=0,
            mode=RAW_CONFIG_OPTION_MODE_EXPERT | RAW_CONFIG_OPTION_MODE_SUSI,
            default_serialized_value="0.01",
        )
        self.api.create_option_def(
            opt_key=POLYHOLES_TWISTED_KEY,
            type=RAW_CO_BOOL,
            container_type=RAW_CONTAINER_TYPE_REGION,
            option_preset_type=RAW_PRESET_TYPE_FFF_PRINT,
            printer_technology=RAW_PT_FFF,
            label="Python high level twisting",
            full_label="Python high level polyhole twist",
            category=RAW_OPTION_CATEGORY_SLICING,
            invalidates_step=STEP_SLICING,
            tooltip="Rotate the Python-view-generated polyhole every layer.",
            mode=RAW_CONFIG_OPTION_MODE_EXPERT | RAW_CONFIG_OPTION_MODE_SUSI,
            default_serialized_value="1",
        )

        self.api.add_ui_fragment(
            "print.ui",
            "python_polyholes_high_level",
            "page:Slicing\n"
            "group:Modifying slices\n"
            "line:insert$afterline$Convert round vertical holes to polyholes:Python polyholes high level\n"
            f"setting:label$_:{POLYHOLES_KEY}\n"
            f"setting:sidetext_width$5:{POLYHOLES_THRESHOLD_KEY}\n"
            f"setting:{POLYHOLES_TWISTED_KEY}\n"
            "end_line\n",
            priority=11,
        )
        self.api.add_gui_rule(
            target_key=POLYHOLES_THRESHOLD_KEY,
            condition_key=POLYHOLES_KEY,
            action=RAW_GUI_RULE_ACTION_ENABLE,
            condition=RAW_GUI_RULE_CONDITION_BOOL_TRUE,
        )
        self.api.add_gui_rule(
            target_key=POLYHOLES_TWISTED_KEY,
            condition_key=POLYHOLES_KEY,
            action=RAW_GUI_RULE_ACTION_ENABLE,
            condition=RAW_GUI_RULE_CONDITION_BOOL_TRUE,
        )

    def run(self, run_ctx_address: int) -> None:
        ctx = self.api.post_slicing(run_ctx_address)
        if ctx is None:
            return

        try:
            self._run_polyholes(ctx)
        except Exception as exc:
            ctx.report_error(f"Python Polyholes high level failed: {exc}")

    def _run_polyholes(self, ctx) -> None:
        obj = ctx.object()
        layer_count = obj.layer_count()
        layer_holes: list[list[HoleData]] = [[] for _ in range(layer_count)]

        for layer_idx, layer in enumerate(obj.layers()):
            if ctx.is_cancelled():
                return
            for region_idx, layer_region in enumerate(layer.regions()):
                region_config = layer_region.print_region().config()
                if not region_config.bool(POLYHOLES_KEY):
                    continue

                twist = region_config.bool(POLYHOLES_TWISTED_KEY)
                perimeter_extruder = region_config.int("perimeter_extruder") - 1
                threshold = region_config.option(POLYHOLES_THRESHOLD_KEY)

                for expolygon in layer_region.slices():
                    for hole in expolygon.holes():
                        points = hole.points()
                        if len(points) <= 8:
                            continue
                        if hole.convex_points_idx(0.0, math.pi):
                            continue

                        center = hole.centroid()
                        min_radius = float("inf")
                        max_radius = 0.0
                        radius_sum = 0.0
                        for point in points:
                            distance = _point_distance(point, center)
                            min_radius = min(min_radius, distance)
                            max_radius = max(max_radius, distance)
                            radius_sum += distance

                        min_line_radius = float("inf")
                        max_line_radius = 0.0
                        previous = points[-1]
                        for point in points:
                            midline = _point_mid(previous, point)
                            distance = _point_distance(center, midline)
                            min_line_radius = min(min_line_radius, distance)
                            max_line_radius = max(max_line_radius, distance)
                            previous = point

                        reference_radius = unscaled(radius_sum / len(points))
                        max_variation = scale_i(threshold.get_effective_value(reference_radius))
                        max_variation = max(SCALED_EPSILON, max_variation)
                        if max_radius - min_radius < max_variation * 2 and max_line_radius - min_line_radius < max_variation * 2:
                            layer_holes[layer_idx].append(HoleData(
                                center=center,
                                max_diameter=max_radius,
                                extruder_id=perimeter_extruder,
                                max_deviation=max_variation,
                                twist=twist,
                                points=points,
                                layer_region_idx=region_idx,
                            ))
            ctx.report_progress((layer_idx + 1) / max(1, layer_count), "Python Polyholes high level: searching holes")

        through_holes = self._group_holes(obj, layer_holes)
        print_config = ctx.print().config()
        modified_layers: set[int] = set()

        for hole_idx, through_hole in enumerate(through_holes):
            nozzle_diameter = scale_i(print_config.float(
                "nozzle_diameter",
                max(0, through_hole.hole_data.extruder_id),
            ))
            replacements = _create_polyholes(
                self.api,
                ctx.plugin_storage(),
                through_hole.hole_data.center,
                through_hole.hole_data.max_diameter,
                nozzle_diameter,
                through_hole.hole_data.twist,
            )
            for layer_hole in through_hole.layers:
                mutable_layer = obj.layer_mutable(layer_hole.layer_idx)
                mutable_region = mutable_layer.region_mutable(layer_hole.layer_region_idx)
                mutable_slices = ctx.borrow_layer_region_slices(mutable_region)
                replacement = replacements[layer_hole.layer_idx % len(replacements)]
                modified = 0
                for expolygon in mutable_slices.mutable_items():
                    if _replace_matching_hole(expolygon, layer_hole.points, replacement):
                        modified += 1
                if modified:
                    modified_layers.add(layer_hole.layer_idx)
            for polygon in replacements:
                polygon.free_from_storage()
            ctx.report_progress(
                (hole_idx + 1) / max(1, len(through_holes)),
                "Python Polyholes high level: converting holes",
            )

        for layer_idx in sorted(modified_layers):
            ctx.recompute_slices_and_islands_from_layer_region(obj.layer_mutable(layer_idx))

        if ctx.storage_size() != 0:
            ctx.clear_storage()

    def _group_holes(self, obj, layer_holes: list[list[HoleData]]) -> list[ThroughHole]:
        through_holes: list[ThroughHole] = []
        min_layer_count = 2
        layer_count = len(layer_holes)

        for layer_idx in range(layer_count):
            for main_hole in list(layer_holes[layer_idx]):
                max_z = obj.layer(layer_idx).print_z()
                holes = [LayerHole(main_hole.points, layer_idx, main_hole.layer_region_idx)]
                for search_layer_idx in range(layer_idx + 1, layer_count):
                    search_layer = obj.layer(search_layer_idx)
                    if search_layer.print_z() - search_layer.height() - max_z > 0:
                        break

                    candidates = layer_holes[search_layer_idx]
                    for search_hole_idx, search_hole in enumerate(candidates):
                        if (
                            main_hole.extruder_id == search_hole.extruder_id
                            and _point_distance(main_hole.center, search_hole.center) < main_hole.max_deviation
                            and abs(main_hole.max_diameter - search_hole.max_diameter) < main_hole.max_deviation
                        ):
                            max_z = search_layer.print_z()
                            holes.append(LayerHole(search_hole.points, search_layer_idx, search_hole.layer_region_idx))
                            del candidates[search_hole_idx]
                            break

                if len(holes) >= min_layer_count or (len(holes) == 1 and holes[0].layer_idx == 0):
                    through_holes.append(ThroughHole(main_hole, holes))

        return through_holes


def register_plugin(api):
    return PythonPolyholesHighLevelPlugin(api)
