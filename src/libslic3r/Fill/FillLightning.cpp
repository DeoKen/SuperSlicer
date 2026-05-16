///|/ Copyright (c) SuperSlicer 2026 Durand R?mi @supermerill
///|/ Copyright (c) Prusa Research 2021 - 2023 Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "FillLightning.hpp"

#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/ShortestPath.hpp"

#include "Lightning/Generator.hpp"

namespace Slic3r::FillLightning {

void Filler::_fill_surface_single(
    const FillParams              &params,
    unsigned int                   thickness_layers,
    const std::pair<float, Point> &direction,
    ExPolygon                      expolygon,
    Polylines                     &polylines_out) const
{
    const LightningLayer &layer = generator->getTreesForLayer(this->layer_id);
    Polylines    fill_lines = layer.convertToLines(to_polygons(expolygon), scale_i(0.5 * this->get_spacing() - this->overlap));

    if (params.dont_connect() || fill_lines.size() <= 1) {
        append(polylines_out, chain_polylines(std::move(fill_lines)));
    } else
        connect_infill(std::move(fill_lines), expolygon, polylines_out, scale_i(this->get_spacing()), params);
}

void GeneratorDeleter::operator()(Generator *p) {
    delete p;
}

GeneratorPtr build_generator(const PrintObject &print_object, const coordf_t fill_density, const std::function<void()> &throw_on_cancel_callback)
{
    return GeneratorPtr(new Generator(print_object, fill_density, throw_on_cancel_callback));
}

} // namespace Slic3r::FillAdaptive
