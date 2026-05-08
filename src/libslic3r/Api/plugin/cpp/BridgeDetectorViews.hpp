///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#pragma once

#include "libslic3r/Api/plugin/c/slic3r_bridge_detector.h"
#include "libslic3r/Api/plugin/cpp/GeometryViews.hpp"

namespace slic3r_api {

/* ========================= bridge detector views ========================= */
/*
Owning C++ view over a bridge_detector_instance.

This is intentionally separate from DataTreeViews.hpp: BridgeDetector is not a
view over the slicer data tree. It is a view over a plugin-provided service
instance created through the orchestrator, and its lifetime is controlled by
the bridge detector ABI vtable.
*/

class BridgeDetector
{
public:
    BridgeDetector() = default;
    explicit BridgeDetector(bridge_detector_instance instance) : m_instance(instance) {}
    BridgeDetector(const BridgeDetector &) = delete;
    BridgeDetector &operator=(const BridgeDetector &) = delete;

    BridgeDetector(BridgeDetector &&other) noexcept : m_instance(other.m_instance) { other.m_instance = {}; }
    BridgeDetector &operator=(BridgeDetector &&other) noexcept {
        if (this != &other) {
            reset();
            m_instance = other.m_instance;
            other.m_instance = {};
        }
        return *this;
    }

    ~BridgeDetector() { reset(); }

    bool valid() const { return m_instance.ctx != nullptr && m_instance.vt != nullptr; }
    explicit operator bool() const { return valid(); }

    bool detect_angle(double bridge_direction_override = -1.) {
        return bridge_detector_detect_angle(&m_instance, bridge_direction_override) != 0;
    }

    uint32_t coverage(StoredPolygonCollection &out_polygons, double angle = -1.) {
        return bridge_detector_coverage(&m_instance, angle, out_polygons.mutable_handle());
    }

    uint32_t unsupported_edges(StoredPolylineCollection &out_polylines, double angle = -1.) {
        return bridge_detector_unsupported_edges(&m_instance, angle, out_polylines.mutable_handle());
    }

    double angle() { return bridge_detector_get_angle(&m_instance); }
    void set_max_bridge_length(double max_bridge_length) {
        bridge_detector_set_max_bridge_length(&m_instance, max_bridge_length);
    }
    double max_bridge_length() { return bridge_detector_get_max_bridge_length(&m_instance); }
    void set_layer_id(int32_t layer_id) { bridge_detector_set_layer_id(&m_instance, layer_id); }
    int32_t layer_id() { return bridge_detector_get_layer_id(&m_instance); }

    void reset() {
        bridge_detector_destroy(&m_instance);
        m_instance = {};
    }

private:
    bridge_detector_instance m_instance = {};
};

} // namespace slic3r_api
