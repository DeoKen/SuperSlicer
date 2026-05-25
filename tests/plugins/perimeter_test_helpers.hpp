#ifndef slic3r_tests_plugins_perimeter_test_helpers_hpp_
#define slic3r_tests_plugins_perimeter_test_helpers_hpp_

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PrintRegion.hpp"
#include "libslic3r/SurfaceCollection.hpp"

namespace Slic3r::Test::PerimeterPluginTests {

extern const char *const SIMPLE_PERIMETER_GENERATOR;
extern const char *const PYTHON_SIMPLE_PERIMETER_GENERATOR;
extern const char *const ARACHNE_PERIMETER_GENERATOR;
extern const char *const EXTRA_PERIMETER_COUNT;
extern const char *const EXTRA_PERIMETER_BELOW_AREA;
extern const char *const EXTRA_PERIMETER_ODD_LAYER;
extern const char *const ONLY_ONE_PERIMETER_FIRST_LAYER;
extern const char *const ONLY_ONE_PERIMETER_ON_TOP;
extern const char *const SEPARATE_HOLE_CONTOUR;
extern const char *const REMOVE_GAP_FILL_ON_OVERHANGS;

struct PerimeterRunCapture
{
    ExtrusionEntityCollection external_perimeters;
    SurfaceCollection fill_surfaces;
    SurfaceCollection fill_no_overlap_surfaces;
};

struct VerticalSplitCounts
{
    size_t left_only = 0;
    size_t right_only = 0;
    size_t crossing = 0;
};

struct PreparedPerimeterPrint
{
    Model model;
    Print print;
    std::vector<std::unique_ptr<PrintRegion>> extra_regions;
};

ExPolygon rectangle_expolygon(double min_x, double min_y, double max_x, double max_y);
ExPolygon rectangle_with_hole_expolygon();
DynamicPrintConfig perimeter_config(std::initializer_list<std::pair<std::string, std::string>> overrides);
void prepare_cube_print(PreparedPerimeterPrint &prepared, const DynamicPrintConfig &config);
size_t layer_index_for_top(const PrintObject &object);
size_t layer_index_for_odd_layer(const PrintObject &object);

PerimeterRunCapture run_perimeter_case(
    const DynamicPrintConfig &config,
    std::initializer_list<const char *> active_plugins,
    const ExPolygon &surface,
    size_t layer_idx,
    std::initializer_list<std::pair<std::string, std::string>> overlap_overrides = {},
    const ExPolygon *overlap_surface = nullptr);

size_t external_perimeter_count(const PerimeterRunCapture &capture);
const ExtrusionEntityCollection &external_perimeters(const PerimeterRunCapture &capture);
double extrusion_length(const ExtrusionEntity &entity);
size_t count_loops_with_role(const ExtrusionEntity &entity, ExtrusionLoopRole role_mask);
VerticalSplitCounts vertical_split_counts(const ExtrusionEntity &entity, coord_t split_x);

size_t run_remove_gap_fill_module(const DynamicPrintConfig &config,
                                  bool use_overlap_region,
                                  double *length_out = nullptr);

} // namespace Slic3r::Test::PerimeterPluginTests

#endif // slic3r_tests_plugins_perimeter_test_helpers_hpp_
