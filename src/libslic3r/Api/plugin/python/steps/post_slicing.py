#/|/ Copyright (c) SuperSlicer 2026 Durand Remi @supermerill
#/|/
#/|/ SuperSlicer is released under the terms of the AGPLv3 or higher
#/|/

"""
Python helper for the post-slicing plugin step.

The post-slicing step receives the current print and object, plus callbacks for
editing layer-region slices. Normal plugin code should create this context from
the run context address passed to PluginBase.run_impl(), then use the view
objects returned here instead of manually casting the raw payload.

Context contents
----------------

PostSlicingContext exposes:

* mutable print() and object() views because this step is allowed to edit the
  freshly produced layer slices;
* mutable_layer(), plus borrowed layer, LayerRegion, and LayerIsland polygon
  views for targeted edits;
* assign_islands_by_moving_contents(), for replacing a layer's islands from a
  storage-owned ExPolygon collection;
* recompute_slices_from_islands() and
  recompute_slices_and_islands_from_layer_region(), the host callbacks that
  rebuild derived caches after polygon edits;
* plugin_storage(), temporary storage cleanup, cancellation, progress, warning,
  and error helpers.

Typical use
-----------

    ctx = api.post_slicing(run_ctx_address)
    if ctx is None:
        return

    obj = ctx.object()
    for layer_idx in range(obj.layer_count()):
        layer = obj.layer_mutable(layer_idx)
        for region_idx in range(layer.region_count()):
            region = layer.region_mutable(region_idx)
            slices = ctx.borrow_layer_region_slices(region)
            # edit slices here
        ctx.recompute_slices_and_islands_from_layer_region(layer)

After editing a layer region's raw slices, call
recompute_slices_and_islands_from_layer_region() on the owning layer so the host
can rebuild its derived slice/island caches.
"""

from __future__ import annotations

import ctypes

from slic3r_api_generated import (
    PLUGIN_IS_CANCELLED,
    PLUGIN_REPORT,
    PLUGIN_REPORT_PROGRESS,
    PluginRunContext,
    RunCtxPostSlicing,
    STEP_POST_SLICING,
)
from slic3r_datatree_views import MutableLayer, MutableLayerIsland, MutableLayerRegion, MutableObject, MutablePrint
from slic3r_geometry_views import ExPolygonCollection, MutableExPolygonCollection, MutableExPolygon


def _address(handle) -> int:
    if handle is None:
        return 0
    if isinstance(handle, ctypes.c_void_p):
        return int(handle.value or 0)
    return int(handle)


def _void_p(handle) -> ctypes.c_void_p:
    return ctypes.c_void_p(_address(handle))


def _as_bytes(text: str) -> bytes:
    return text.encode("utf-8")


def _optional_bytes(text: str | None) -> bytes | None:
    return None if text is None else text.encode("utf-8")


# High-level wrapper around the STEP_POST_SLICING payload.
class PostSlicingContext:
    def __init__(self, api, common: PluginRunContext, payload: RunCtxPostSlicing) -> None:
        self.api = api
        self.common = common
        self.payload = payload

    @classmethod
    def from_run_context(cls, api, run_ctx_address: int) -> "PostSlicingContext | None":
        if not run_ctx_address:
            return None
        common = PluginRunContext.from_address(int(run_ctx_address))
        if common.step != STEP_POST_SLICING or not common.data:
            return None
        payload = ctypes.cast(common.data, ctypes.POINTER(RunCtxPostSlicing)).contents
        if not payload.print or not payload.object:
            return None
        return cls(api, common, payload)

    def plugin_storage(self) -> int:
        return _address(self.common.plugin_storage)

    def print(self) -> MutablePrint:
        return MutablePrint(self.api, self.payload.print)

    def object(self) -> MutableObject:
        return MutableObject(self.api, self.payload.object)

    def mutable_layer(self, idx: int) -> MutableLayer:
        """
        Borrow one mutable object layer through the step callback.

        The object handle in the payload is const, so this callback is the
        explicit permission granted by STEP_POST_SLICING to edit a layer.
        """
        if not self.payload.object_borrow_mutable_layer:
            raise RuntimeError("STEP_POST_SLICING did not expose mutable layer access.")
        return MutableLayer(self.api, self.payload.object_borrow_mutable_layer(self.payload.object, int(idx)))

    def borrow_layer_slices(self, layer: MutableLayer) -> MutableExPolygonCollection:
        handle = self.payload.layer_borrow_mutable_slices(layer.mutable_c_handle())
        return MutableExPolygonCollection(self.api, handle)

    def borrow_layer_region_slices(self, layer_region: MutableLayerRegion) -> MutableExPolygonCollection:
        handle = self.payload.layer_region_borrow_mutable_slices(layer_region.mutable_c_handle())
        return MutableExPolygonCollection(self.api, handle)

    def borrow_layer_island_slice(self, island: MutableLayerIsland) -> MutableExPolygon:
        handle = self.payload.layer_island_borrow_mutable_slice(island.mutable_c_handle())
        return MutableExPolygon(self.api, handle)

    def assign_islands_by_moving_contents(self, layer: MutableLayer, islands: ExPolygonCollection) -> None:
        """
        Replace a layer's islands from a mutable ExPolygonCollection.

        The host moves contents out of islands. Use a storage-owned collection
        when you want to transfer ownership, then consider the source empty.
        """
        self.payload.layer_assign_islands_by_moving_contents(layer.mutable_c_handle(), islands.c_handle())

    def recompute_slices_from_islands(self, layer: MutableLayer) -> None:
        self.payload.layer_recompute_slices_from_islands(layer.mutable_c_handle())

    def recompute_slices_and_islands_from_layer_region(self, layer: MutableLayer) -> None:
        self.payload.layer_recompute_slices_and_islands_from_layer_region(layer.mutable_c_handle())

    def storage_size(self) -> int:
        return int(self.api.host.storage_size(_void_p(self.plugin_storage())))

    def clear_storage(self) -> None:
        self.api.host.storage_clear(_void_p(self.plugin_storage()))

    def is_cancelled(self) -> bool:
        if not self.common.is_cancelled:
            return False
        return bool(PLUGIN_IS_CANCELLED(self.common.is_cancelled)(self.common.host_context))

    def report_warning(self, message: str) -> None:
        if self.common.report_warning:
            PLUGIN_REPORT(self.common.report_warning)(self.common.host_context, _as_bytes(message))

    def report_error(self, message: str) -> None:
        if self.common.report_error:
            PLUGIN_REPORT(self.common.report_error)(self.common.host_context, _as_bytes(message))

    def report_progress(self, progress: float, message: str | None = None) -> None:
        if self.common.report_progress:
            PLUGIN_REPORT_PROGRESS(self.common.report_progress)(
                self.common.host_context, float(progress), _optional_bytes(message)
            )


__all__ = ["PostSlicingContext"]
