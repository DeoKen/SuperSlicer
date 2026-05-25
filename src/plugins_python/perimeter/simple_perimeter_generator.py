#/|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
#/|/
#/|/ SuperSlicer is released under the terms of the AGPLv3 or higher
#/|/

"""
Simple STEP_PERIMETER generator written with the high-level Python helpers.

This mirrors the temporary native SimplePerimeterGenerator: for each perimeter
node it offsets the current surface to create one closed perimeter loop, then
returns the inner surface used by the host to create the next node.
"""

from __future__ import annotations

from dataclasses import dataclass

from slic3r_api import (
    CFlow,
    EPropertyAttributes,
    EPropertyPerimeter,
    PluginBase,
    RAW_EXTRUSION_FLAG_CONTINUOUS,
    RAW_EXTRUSION_FLAG_REVERSIBLE,
    RAW_EXTRUSION_ROLE_EXTERNAL_PERIMETER,
    STEP_PERIMETER,
    StoredExtrusionEntity,
    unscaled,
)


PLUGIN_ID = "python.perimeter.generator.simple"
LOOP_ROLE_DEFAULT = 1 << 0
LOOP_ROLE_HOLE = 1 << 3


@dataclass
class SimpleGeneratorState:
    flow: CFlow


def _offset_surface(api, storage: int, surface, delta: float):
    clip = api.clipper(storage)
    result = clip.offset(clip(surface), delta).to_expolygon_collection()
    result.ensure_valid()
    return result


def _append_perimeter_loop(dst: StoredExtrusionEntity, polygon, flow: CFlow, loop_role: int) -> None:
    if not polygon.valid_polygon() or polygon.empty():
        return

    points = polygon.points()
    if not points:
        return
    points.append(points[0])

    path = StoredExtrusionEntity(dst.api, dst.storage(), points)
    attributes = path.property(EPropertyAttributes)
    attributes.role = RAW_EXTRUSION_ROLE_EXTERNAL_PERIMETER
    attributes.mm3_per_mm = flow.mm3_per_mm
    attributes.width = float(unscaled(flow.width))
    attributes.height = float(unscaled(flow.height))

    loop = StoredExtrusionEntity(dst.api, dst.storage())
    loop.set_flags(RAW_EXTRUSION_FLAG_CONTINUOUS | RAW_EXTRUSION_FLAG_REVERSIBLE)
    perimeter = loop.property(EPropertyPerimeter)
    perimeter.perimeter_idx = 0
    perimeter.loop_role = int(loop_role)
    loop.add_child_move(path)
    dst.add_child_move(loop)


def _make_perimeter_extrusion(api, storage: int, surface, flow: CFlow, line_offset: float) -> StoredExtrusionEntity:
    extrusion = StoredExtrusionEntity(api, storage)
    extrusion.disable_reverse().disable_sort()

    loops = _offset_surface(api, storage, surface, line_offset)
    try:
        for loop in loops:
            _append_perimeter_loop(extrusion, loop.contour(), flow, LOOP_ROLE_DEFAULT)
            for hole in loop.holes():
                _append_perimeter_loop(extrusion, hole, flow, LOOP_ROLE_HOLE)
    finally:
        loops.free_from_storage()
    return extrusion


class PythonSimplePerimeterGeneratorPlugin(PluginBase):
    def __init__(self, api):
        super().__init__(PLUGIN_ID, STEP_PERIMETER, priority=0)
        self.api = api

    def run(self, run_ctx_address: int) -> None:
        ctx = self.api.perimeter(run_ctx_address)
        if ctx is None:
            return

        try:
            self._run_perimeter(ctx)
        except Exception as exc:
            ctx.report_error(f"Python simple perimeter generator failed: {exc}")

    def _run_perimeter(self, ctx) -> None:
        island = ctx.island()
        regions = list(island.regions())
        if not regions:
            return

        state = SimpleGeneratorState(regions[0].flow(RAW_EXTRUSION_ROLE_EXTERNAL_PERIMETER))
        ctx.run_region_group(regions, island.slice(), state, self._generate_node)

    def _generate_node(self, state: SimpleGeneratorState, generation, node, inner_surfaces, inner_fill_surfaces) -> bool:
        storage = generation.plugin_storage()
        first_perimeter = node.perimeter_idx() == 0
        flow = state.flow

        line_offset = -0.5 * float(flow.width if first_perimeter else flow.spacing)
        extrusion = _make_perimeter_extrusion(self.api, storage, node.surface(), flow, line_offset)
        try:
            node.extrusions().move_from(extrusion)
        finally:
            extrusion.free_from_storage()

        inner_offset = -0.5 * float(flow.width + flow.spacing) if first_perimeter else -float(flow.spacing)
        inner = _offset_surface(self.api, storage, node.surface(), inner_offset)
        try:
            self.api.host.expolygons_move(inner_surfaces.mutable_c_handle(), inner.mutable_c_handle())
        finally:
            inner.free_from_storage()

        # Keep the fill/anchor area slightly larger than the next perimeter
        # surface, matching the native simple generator.
        fill_offset = inner_offset + 0.25 * float(flow.spacing)
        inner_fill = _offset_surface(self.api, storage, node.surface(), fill_offset)
        try:
            self.api.host.expolygons_move(inner_fill_surfaces.mutable_c_handle(), inner_fill.mutable_c_handle())
        finally:
            inner_fill.free_from_storage()

        return True


def register_plugin(api):
    return PythonSimplePerimeterGeneratorPlugin(api)
