///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "libslic3r/Api/plugin/c/slic3r_bridge_detector.h"

namespace {

bool valid(const bridge_detector_instance *detector)
{
    return detector != nullptr && detector->ctx != nullptr && detector->vt != nullptr;
}

} // namespace

extern "C" {

void bridge_detector_destroy(bridge_detector_instance *detector)
{
    if (!valid(detector) || detector->vt->destroy == nullptr)
        return;
    detector->vt->destroy(detector->ctx);
    detector->ctx = nullptr;
    detector->vt = nullptr;
}

int32_t bridge_detector_detect_angle(bridge_detector_instance *detector, double bridge_direction_override)
{
    return valid(detector) && detector->vt->detect_angle != nullptr ?
        detector->vt->detect_angle(detector->ctx, bridge_direction_override) :
        0;
}

uint32_t bridge_detector_coverage(bridge_detector_instance *detector,
                                  double angle,
                                  polygon_collection_handle *out_polygons)
{
    return valid(detector) && detector->vt->coverage != nullptr ?
        detector->vt->coverage(detector->ctx, angle, out_polygons) :
        0;
}

uint32_t bridge_detector_unsupported_edges(bridge_detector_instance *detector,
                                           double angle,
                                           polyline_collection_handle *out_polylines)
{
    return valid(detector) && detector->vt->unsupported_edges != nullptr ?
        detector->vt->unsupported_edges(detector->ctx, angle, out_polylines) :
        0;
}

double bridge_detector_get_angle(bridge_detector_instance *detector)
{
    return valid(detector) && detector->vt->get_angle != nullptr ? detector->vt->get_angle(detector->ctx) : -1.;
}

void bridge_detector_set_max_bridge_length(bridge_detector_instance *detector, double max_bridge_length)
{
    if (valid(detector) && detector->vt->set_max_bridge_length != nullptr)
        detector->vt->set_max_bridge_length(detector->ctx, max_bridge_length);
}

double bridge_detector_get_max_bridge_length(bridge_detector_instance *detector)
{
    return valid(detector) && detector->vt->get_max_bridge_length != nullptr ?
        detector->vt->get_max_bridge_length(detector->ctx) :
        -1.;
}

void bridge_detector_set_layer_id(bridge_detector_instance *detector, int32_t layer_id)
{
    if (valid(detector) && detector->vt->set_layer_id != nullptr)
        detector->vt->set_layer_id(detector->ctx, layer_id);
}

int32_t bridge_detector_get_layer_id(bridge_detector_instance *detector)
{
    return valid(detector) && detector->vt->get_layer_id != nullptr ? detector->vt->get_layer_id(detector->ctx) : -1;
}

} // extern "C"
