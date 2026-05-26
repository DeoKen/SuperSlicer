#/|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
#/|/
#/|/ SuperSlicer is released under the terms of the AGPLv3 or higher
#/|/

"""
Default STEP_SURFACE_GENERATION plugin implemented in Python.

This is intentionally close to the native default surface generator. It exists
mostly as an integration example: when both this plugin and the native one are
active, the GUI exposes one exclusive-group selector for STEP_SURFACE_GENERATION.
"""

from __future__ import annotations

from collections import defaultdict

from slic3r_api import (
    PluginBase,
    RAW_SURFACE_TYPE_DENS_SPARSE,
    RAW_SURFACE_TYPE_POS_INTERNAL,
    STEP_SURFACE_GENERATION,
)


PLUGIN_ID = "python.surface.generator.default"
SURFACE_GENERATION_GROUP = "step_surface_generation_plugin"
SPARSE_INTERNAL = RAW_SURFACE_TYPE_POS_INTERNAL | RAW_SURFACE_TYPE_DENS_SPARSE


def _region_infill_extruder_id(region) -> int:
    # SuperSlicer stores config extruders as 1-based values. LayerRegionIsland
    # stores the effective extruder as 0-based, matching the native data tree.
    return max(0, region.print_region().config().int("infill_extruder") - 1)


def _regions_by_infill_extruder(island) -> dict[int, list]:
    grouped = defaultdict(list)
    for region in island.regions():
        grouped[_region_infill_extruder_id(region)].append(region)
    return dict(grouped)


def _clip_infill_areas_to_regions(api, storage: int, island, regions: list):
    clip = api.clipper(storage)
    # When several infill extruders share one island, each LayerRegionIsland
    # receives only the part of the island infill areas covered by its regions.
    # Region raw slices are non-overlapping, so unioning them then intersecting
    # once is enough and keeps the plugin easy to read.
    with clip.empty() as region_paths:
        for region in regions:
            with clip(region.slices()) as region_operand:
                region_paths.concat_replace(region_operand)

        with clip.union(region_paths) as merged_regions:
            with clip(island.infill_areas()) as infill_operand:
                with clip.intersection(infill_operand, merged_regions) as clipped:
                    return clipped.to_expolygon_collection()


class PythonDefaultSurfaceGeneratorPlugin(PluginBase):
    def __init__(self, api):
        super().__init__(
            PLUGIN_ID,
            STEP_SURFACE_GENERATION,
            name="Python default surface generator",
            description="Python example that converts island fill areas into sparse internal infill surfaces.",
            priority=10,
            exclusive_group=SURFACE_GENERATION_GROUP,
            exclusive_group_label="Surface generation plugin",
            exclusive_group_tooltip="Choose which active plugin converts perimeter fill areas into infill surfaces.",
        )
        self.api = api

    def run(self, run_ctx_address: int) -> None:
        ctx = self.api.surface_generation(run_ctx_address)
        if ctx is None:
            return

        try:
            self._run_surface_generation(ctx)
        except Exception as exc:
            ctx.report_error(f"Python default surface generator failed: {exc}")

    def _run_surface_generation(self, ctx) -> None:
        storage = ctx.plugin_storage()
        for layer in ctx.object().layers():
            if ctx.is_cancelled():
                return
            for island in layer.islands():
                self._build_island_surfaces(ctx, storage, island)

    def _build_island_surfaces(self, ctx, storage: int, island) -> None:
        grouped_regions = _regions_by_infill_extruder(island)
        if not grouped_regions:
            return

        single_group = len(grouped_regions) == 1
        for regions in grouped_regions.values():
            region_island = ctx.get_or_create_region_island(island, regions)
            if region_island is None:
                continue

            if single_group:
                ctx.set_fill_surfaces(region_island, island.infill_areas(), SPARSE_INTERNAL)
                continue

            clipped_areas = _clip_infill_areas_to_regions(self.api, storage, island, regions)
            try:
                ctx.set_fill_surfaces(region_island, clipped_areas, SPARSE_INTERNAL)
            finally:
                clipped_areas.free_from_storage()


def register_plugin(api):
    return PythonDefaultSurfaceGeneratorPlugin(api)
