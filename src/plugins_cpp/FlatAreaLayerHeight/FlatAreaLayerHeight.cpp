///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "FlatAreaLayerHeight.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "libslic3r/Api/plugin/c/slic3r_config_def.h"
#include "libslic3r/Api/plugin/c/slic3r_orchestrator.h"
#include "libslic3r/Api/plugin/cpp/Views.hpp"

namespace slic3r_api { namespace FlatAreaLayerHeightPlugin {

namespace {

const char *k_flat_area_layer_height_id = "flat_area_layer_height";
const char *k_no_dependencies[] = { nullptr };
const char *k_used_config_keys[] = { "layer_height_min_flat_area" };

#define MIN_LAYER_HEIGHT 0.005

enum ProgressPhase : uint32_t
{
    ProgressScanVolumes = 0
};

struct LayerHeightSlicingParameters
{
    bool first_object_layer_height_fixed = false;
    coord_t first_object_layer_height = 0;
    coord_t object_print_z_height = 0;
    coord_t z_step = 0;
    coord_t layer_height = 0;
    coord_t min_layer_height = 0;
    coord_t max_layer_height = 0;
};

struct FlatSurface
{
    coord_t z = 0;
    double area = 0.0;
};

coord_t check_z_step(coord_t val, coord_t z_step)
{
    if (z_step <= SCALED_EPSILON)
        return val;
    return ((((val * 2) + z_step) / (2 * z_step)) * z_step);
}

coord_t z_step_unit(coord_t z_step)
{
    return z_step > SCALED_EPSILON ? z_step : 1;
}

uint32_t config_extruder_idx(int extruder_id)
{
    return extruder_id > 0 ? uint32_t(extruder_id - 1) : 0;
}

void insert_config_extruder_idx(std::set<uint16_t> &extruders, const ConfigOption &nozzle_diameter, int extruder_id)
{
    const uint32_t count = nozzle_diameter.size();
    const uint32_t idx = std::min(config_extruder_idx(extruder_id), count > 0 ? count - 1 : 0);
    extruders.insert(uint16_t(idx));
}

std::set<uint16_t> object_extruders(Print print, Object object)
{
    std::set<uint16_t> extruders;
    const Config print_config = print.config();
    const Config object_config = object.config();
    const ConfigOption nozzle_diameter = print_config.get("nozzle_diameter");

    for (uint32_t region_idx = 0; region_idx < object.print_region_count(); ++region_idx) {
        const PrintRegion region = object.print_region(region_idx);
        const Config region_config = region.config();

        if (region_config.get("perimeters").get_int() > 0 ||
            object_config.get("brim_width").get_float() > 0.0 ||
            object_config.get("brim_width_interior").get_float() > 0.0)
            insert_config_extruder_idx(extruders, nozzle_diameter, region_config.get("perimeter_extruder").get_int());

        if (region_config.get("fill_density").get_float() > 0.0)
            insert_config_extruder_idx(extruders, nozzle_diameter, region_config.get("infill_extruder").get_int());

        if (region_config.get("top_solid_layers").get_int() > 0 ||
            region_config.get("bottom_solid_layers").get_int() > 0 ||
            (region_config.get("solid_infill_every_layers").get_int() > 0 &&
             region_config.get("fill_density").get_float() > 0.0))
            insert_config_extruder_idx(extruders, nozzle_diameter, region_config.get("solid_infill_extruder").get_int());
    }

    return extruders;
}

coord_t min_layer_height_from_nozzle(const Config &print_config, uint32_t extruder_idx)
{
    const ConfigOption nozzle_diameter_opt = print_config.get("nozzle_diameter");
    const uint32_t nozzle_idx = extruder_idx < nozzle_diameter_opt.size() ? extruder_idx : 0;
    const double nozzle_diameter = nozzle_diameter_opt.get_float(nozzle_idx);
    const ConfigOption min_layer_height_opt = print_config.get("min_layer_height");
    const uint32_t value_idx = extruder_idx < min_layer_height_opt.size() ? extruder_idx : 0;
    double value = min_layer_height_opt.get_effective_value(nozzle_diameter, value_idx);
    if (value == 0.0)
        value = nozzle_diameter / 2.0;
    return check_z_step(scale_i(std::max(MIN_LAYER_HEIGHT, value)), scale_i(print_config.get("z_step").get_float()));
}

coord_t max_layer_height_from_nozzle(const Config &print_config, uint32_t extruder_idx)
{
    const ConfigOption nozzle_diameter_opt = print_config.get("nozzle_diameter");
    const uint32_t nozzle_idx = extruder_idx < nozzle_diameter_opt.size() ? extruder_idx : 0;
    const double nozzle_diameter = nozzle_diameter_opt.get_float(nozzle_idx);
    const ConfigOption max_layer_height_opt = print_config.get("max_layer_height");
    const uint32_t value_idx = extruder_idx < max_layer_height_opt.size() ? extruder_idx : 0;
    double value = max_layer_height_opt.get_effective_value(nozzle_diameter, value_idx);
    if (value == 0.0 || !max_layer_height_opt.is_enabled(value_idx))
        value = 0.75 * nozzle_diameter;
    const coord_t min_layer_height = min_layer_height_from_nozzle(print_config, extruder_idx);
    return check_z_step(std::max(min_layer_height, scale_i(value)), scale_i(print_config.get("z_step").get_float()));
}

LayerHeightSlicingParameters make_layer_height_slicing_parameters(Print print, Object object, coord_t max_z)
{
    const Config print_config = print.config();
    const Config object_config = object.config();
    const std::set<uint16_t> extruders = object_extruders(print, object);

    LayerHeightSlicingParameters out;
    out.z_step = scale_i(print_config.get("z_step").get_float());
    out.layer_height = check_z_step(scale_i(object_config.get("layer_height").get_float()), out.z_step);

    const ConfigOption nozzle_diameter_opt = print_config.get("nozzle_diameter");
    const ConfigOption first_layer_height_opt = object_config.get("first_layer_height");
    coord_t first_layer_height = 0;
    if (first_layer_height_opt.is_percent()) {
        first_layer_height = std::numeric_limits<coord_t>::max();
        for (uint16_t extruder_idx : extruders) {
            if (extruder_idx >= nozzle_diameter_opt.size())
                continue;
            const double nozzle = nozzle_diameter_opt.get_float(extruder_idx);
            first_layer_height = std::min(first_layer_height, scale_i(first_layer_height_opt.get_effective_value(nozzle)));
        }
        if (first_layer_height == std::numeric_limits<coord_t>::max())
            first_layer_height = scale_i(first_layer_height_opt.get_effective_value(nozzle_diameter_opt.get_float(0)));
    } else {
        first_layer_height = scale_i(first_layer_height_opt.get_float());
    }
    first_layer_height = check_z_step(first_layer_height, out.z_step);
    if (first_layer_height <= SCALED_EPSILON)
        first_layer_height = out.layer_height;

    out.first_object_layer_height = first_layer_height;
    out.object_print_z_height = check_z_step(max_z, out.z_step);
    if (out.object_print_z_height + SCALED_EPSILON < max_z)
        out.object_print_z_height += out.z_step;

    coord_t min_layer_height = 0;
    coord_t max_layer_height = std::numeric_limits<coord_t>::max();

    const bool has_support = object_config.get("support_material").get_bool() ||
        object_config.get("raft_layers").get_int() > 0 ||
        object_config.get("support_material_enforce_layers").get_int() > 0;
    if (has_support) {
        const int support_extruder = object_config.get("support_material_extruder").get_int();
        const int support_interface_extruder = object_config.get("support_material_interface_extruder").get_int();
        if (support_extruder > 0) {
            const uint32_t idx = config_extruder_idx(support_extruder);
            min_layer_height = std::max(min_layer_height, min_layer_height_from_nozzle(print_config, idx));
            max_layer_height = std::min(max_layer_height, max_layer_height_from_nozzle(print_config, idx));
        }
        if (support_interface_extruder > 0) {
            const uint32_t idx = config_extruder_idx(support_interface_extruder);
            min_layer_height = std::max(min_layer_height, min_layer_height_from_nozzle(print_config, idx));
            max_layer_height = std::min(max_layer_height, max_layer_height_from_nozzle(print_config, idx));
        }
    }

    if (extruders.empty()) {
        min_layer_height = std::max(min_layer_height, min_layer_height_from_nozzle(print_config, 0));
        max_layer_height = std::min(max_layer_height, max_layer_height_from_nozzle(print_config, 0));
    } else {
        for (uint16_t extruder_idx : extruders) {
            min_layer_height = std::max(min_layer_height, min_layer_height_from_nozzle(print_config, extruder_idx));
            max_layer_height = std::min(max_layer_height, max_layer_height_from_nozzle(print_config, extruder_idx));
        }
    }

    if (max_layer_height == std::numeric_limits<coord_t>::max())
        max_layer_height = out.layer_height;
    if (min_layer_height == 0)
        min_layer_height = out.layer_height;
    if (max_layer_height < min_layer_height)
        max_layer_height = min_layer_height;

    out.min_layer_height = check_z_step(min_layer_height, out.z_step);
    out.max_layer_height = check_z_step(max_layer_height, out.z_step);
    out.layer_height = std::clamp(out.layer_height, out.min_layer_height, out.max_layer_height);

    const int raft_layers = object_config.get("raft_layers").get_int();
    if (raft_layers > 0)
        out.first_object_layer_height = out.layer_height;
    out.first_object_layer_height_fixed = raft_layers == 0;

    return out;
}

c_vec3f transform_point(c_matrix4d matrix, c_vec3f point)
{
    const double x = matrix.value[0] * point.x + matrix.value[1] * point.y + matrix.value[2] * point.z + matrix.value[3];
    const double y = matrix.value[4] * point.x + matrix.value[5] * point.y + matrix.value[6] * point.z + matrix.value[7];
    const double z = matrix.value[8] * point.x + matrix.value[9] * point.y + matrix.value[10] * point.z + matrix.value[11];
    const double w = matrix.value[12] * point.x + matrix.value[13] * point.y + matrix.value[14] * point.z + matrix.value[15];
    const double inv_w = std::abs(w) > 1e-12 ? 1.0 / w : 1.0;
    return c_vec3f{float(x * inv_w), float(y * inv_w), float(z * inv_w)};
}

double xy_triangle_area(c_vec3f a, c_vec3f b, c_vec3f c)
{
    const double abx = double(b.x) - double(a.x);
    const double aby = double(b.y) - double(a.y);
    const double acx = double(c.x) - double(a.x);
    const double acy = double(c.y) - double(a.y);
    return 0.5 * std::abs(abx * acy - aby * acx);
}

bool triangle_is_horizontal(c_vec3f a, c_vec3f b, c_vec3f c)
{
    const double min_z = std::min({ double(a.z), double(b.z), double(c.z) });
    const double max_z = std::max({ double(a.z), double(b.z), double(c.z) });
    return max_z - min_z <= EPSILON;
}

void collect_flat_surfaces_from_volume(const Volume &volume,
                                       c_matrix4d object_transform,
                                       const LayerHeightSlicingParameters &params,
                                       std::map<coord_t, double> &area_by_z)
{
    if (!volume.is_model_part())
        return;

    const TriangleMesh mesh = volume.mesh();
    if (mesh.empty())
        return;

    const c_matrix4d transform = matrix4d_mul(object_transform, volume.matrix());
    for (uint32_t triangle_idx = 0; triangle_idx < mesh.triangle_count(); ++triangle_idx) {
        const c_triangle_indices indices = mesh.triangle(triangle_idx);
        const c_vec3f a = transform_point(transform, mesh.vertex(indices.a));
        const c_vec3f b = transform_point(transform, mesh.vertex(indices.b));
        const c_vec3f c = transform_point(transform, mesh.vertex(indices.c));
        if (!triangle_is_horizontal(a, b, c))
            continue;

        const double area = xy_triangle_area(a, b, c);
        if (area <= EPSILON)
            continue;

        const double z_mm = (double(a.z) + double(b.z) + double(c.z)) / 3.0;
        const coord_t z = check_z_step(scale_to_layer_coord(z_mm), params.z_step);
        if (z <= SCALED_EPSILON || z >= params.object_print_z_height - SCALED_EPSILON)
            continue;
        if (params.first_object_layer_height_fixed && z <= params.first_object_layer_height + SCALED_EPSILON)
            continue;

        area_by_z[z] += area;
    }
}

std::vector<FlatSurface> collect_flat_surfaces(Object object,
                                               const LayerHeightSlicingParameters &params,
                                               double min_flat_area,
                                               PluginProgress &progress)
{
    std::map<coord_t, double> area_by_z;
    const c_matrix4d object_transform = object.transform_centered();
    for (uint32_t volume_idx = 0; volume_idx < object.volume_count(); ++volume_idx) {
        const Volume volume = object.volume(volume_idx);
        collect_flat_surfaces_from_volume(volume, object_transform, params, area_by_z);
        progress.increment(ProgressScanVolumes);
    }

    std::vector<FlatSurface> flat_surfaces;
    flat_surfaces.reserve(area_by_z.size());
    for (const std::pair<const coord_t, double> &entry : area_by_z) {
        if (entry.second >= min_flat_area)
            flat_surfaces.push_back(FlatSurface{entry.first, entry.second});
    }

    std::sort(flat_surfaces.begin(), flat_surfaces.end(), [](const FlatSurface &lhs, const FlatSurface &rhs) {
        if (lhs.area != rhs.area)
            return lhs.area > rhs.area;
        return lhs.z < rhs.z;
    });
    return flat_surfaces;
}

int64_t ceil_div(int64_t lhs, int64_t rhs)
{
    assert(rhs > 0);
    return (lhs + rhs - 1) / rhs;
}

bool interval_is_fixed_first_layer(coord_t lo, coord_t hi, const LayerHeightSlicingParameters &params)
{
    return params.first_object_layer_height_fixed && lo == 0 && hi == params.first_object_layer_height;
}

bool interval_can_be_layered(coord_t lo, coord_t hi, const LayerHeightSlicingParameters &params)
{
    if (hi <= lo)
        return true;
    if (interval_is_fixed_first_layer(lo, hi, params))
        return true;

    const coord_t unit = z_step_unit(params.z_step);
    const int64_t distance = int64_t((hi - lo) / unit);
    const int64_t min_height = std::max<int64_t>(1, int64_t(params.min_layer_height / unit));
    const int64_t max_height = std::max(min_height, int64_t(params.max_layer_height / unit));
    const int64_t min_layers = ceil_div(distance, max_height);
    const int64_t max_layers = distance / min_height;
    return min_layers <= max_layers;
}

bool anchors_can_be_layered(const std::vector<coord_t> &anchors, const LayerHeightSlicingParameters &params)
{
    for (size_t idx = 1; idx < anchors.size(); ++idx)
        if (!interval_can_be_layered(anchors[idx - 1], anchors[idx], params))
            return false;
    return true;
}

std::vector<coord_t> select_flat_surface_anchors(const std::vector<FlatSurface> &flat_surfaces,
                                                 const LayerHeightSlicingParameters &params)
{
    std::vector<coord_t> anchors;
    anchors.push_back(0);
    if (params.first_object_layer_height_fixed)
        anchors.push_back(params.first_object_layer_height);
    anchors.push_back(params.object_print_z_height);
    std::sort(anchors.begin(), anchors.end());
    anchors.erase(std::unique(anchors.begin(), anchors.end()), anchors.end());

    for (const FlatSurface &surface : flat_surfaces) {
        if (std::binary_search(anchors.begin(), anchors.end(), surface.z))
            continue;

        std::vector<coord_t> candidate = anchors;
        candidate.push_back(surface.z);
        std::sort(candidate.begin(), candidate.end());
        if (anchors_can_be_layered(candidate, params))
            anchors = std::move(candidate);
    }

    return anchors;
}

void append_interval_layers(std::vector<coord_t> &boundaries,
                            coord_t lo,
                            coord_t hi,
                            const LayerHeightSlicingParameters &params)
{
    if (hi <= lo)
        return;

    if (boundaries.empty())
        boundaries.push_back(lo);
    assert(boundaries.back() == lo);

    if (interval_is_fixed_first_layer(lo, hi, params)) {
        boundaries.push_back(hi);
        return;
    }

    const coord_t unit = z_step_unit(params.z_step);
    const int64_t distance = int64_t((hi - lo) / unit);
    const int64_t min_height = std::max<int64_t>(1, int64_t(params.min_layer_height / unit));
    const int64_t max_height = std::max(min_height, int64_t(params.max_layer_height / unit));
    const int64_t preferred_height = std::clamp<int64_t>(int64_t(params.layer_height / unit), min_height, max_height);
    const int64_t min_layers = ceil_div(distance, max_height);
    const int64_t max_layers = distance / min_height;
    if (min_layers > max_layers) {
        boundaries.push_back(hi);
        return;
    }

    const int64_t preferred_layers = std::max<int64_t>(1, int64_t(std::llround(double(distance) / double(preferred_height))));
    const int64_t layer_count = std::clamp(preferred_layers, min_layers, max_layers);
    const int64_t base_height = distance / layer_count;
    const int64_t remainder = distance % layer_count;

    coord_t current = lo;
    for (int64_t layer_idx = 0; layer_idx < layer_count; ++layer_idx) {
        const int64_t height_units = base_height + (layer_idx < remainder ? 1 : 0);
        current += coord_t(height_units * unit);
        boundaries.push_back(current);
    }
    boundaries.back() = hi;
}

std::vector<coord_t> make_layer_boundaries(const std::vector<coord_t> &anchors,
                                           const LayerHeightSlicingParameters &params)
{
    std::vector<coord_t> boundaries;
    boundaries.reserve(anchors.size() * 2);
    boundaries.push_back(anchors.front());
    for (size_t idx = 1; idx < anchors.size(); ++idx)
        append_interval_layers(boundaries, anchors[idx - 1], anchors[idx], params);
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    return boundaries;
}

void layer_height_profile_append(std::vector<coord_t> &profile, coord_t z, coord_t layer_height)
{
    if (profile.size() > 1) {
        const bool last_z_matches = (*(profile.end() - 2) == z);
        const bool last_h_matches = (profile.back() == layer_height);
        if (last_h_matches) {
            if (last_z_matches)
                return;
            if (profile.size() >= 4 && (*(profile.end() - 3) == layer_height)) {
                *(profile.end() - 2) = z;
                return;
            }
        }
    }
    profile.push_back(z);
    profile.push_back(layer_height);
}

std::vector<coord_t> layer_height_profile_from_boundaries(const std::vector<coord_t> &boundaries,
                                                          const LayerHeightSlicingParameters &params)
{
    std::vector<coord_t> profile;
    if (boundaries.size() < 2) {
        layer_height_profile_append(profile, 0, params.layer_height);
        layer_height_profile_append(profile, params.object_print_z_height, params.layer_height);
        return profile;
    }

    for (size_t idx = 1; idx < boundaries.size(); ++idx) {
        const coord_t lo = boundaries[idx - 1];
        const coord_t hi = boundaries[idx];
        const coord_t height = hi - lo;
        if (height <= 0)
            continue;
        layer_height_profile_append(profile, lo, height);
        layer_height_profile_append(profile, hi, height);
    }

    return profile;
}

std::vector<coord_t> make_flat_area_layer_height_profile(Object object,
                                                         const LayerHeightSlicingParameters &params,
                                                         double min_flat_area,
                                                         PluginProgress &progress)
{
    std::vector<FlatSurface> flat_surfaces = collect_flat_surfaces(object, params, min_flat_area, progress);
    std::vector<coord_t> anchors = select_flat_surface_anchors(flat_surfaces, params);
    std::vector<coord_t> boundaries = make_layer_boundaries(anchors, params);
    return layer_height_profile_from_boundaries(boundaries, params);
}

} // namespace

FlatAreaLayerHeight &FlatAreaLayerHeight::instance(orchestrator_handle *orch)
{
    static FlatAreaLayerHeight s_instance(orch);
    return s_instance;
}

const char *FlatAreaLayerHeight::id_impl() const noexcept
{
    return k_flat_area_layer_height_id;
}

slicing_step_t FlatAreaLayerHeight::step_impl() const noexcept
{
    return STEP_LAYER_HEIGHT;
}

const char *const *FlatAreaLayerHeight::dependencies_impl() const noexcept
{
    return k_no_dependencies;
}

int32_t FlatAreaLayerHeight::priority_impl() const noexcept
{
    return 10;
}

int32_t FlatAreaLayerHeight::used_config_keys(const char **keys) const noexcept
{
    if (keys != nullptr)
        keys[0] = k_used_config_keys[0];
    return 1;
}

const char *FlatAreaLayerHeight::progress_message_format_impl() const noexcept
{
    return "Scanning flat surfaces: %u / %u volumes";
}

const char *FlatAreaLayerHeight::print_ui_fragment() noexcept
{
    return "page:Slicing\n"
           "group:Layer height\n"
           "line:Flat area layer matching\n"
           "setting:layer_height_min_flat_area\n"
           "end_line\n";
}

void FlatAreaLayerHeight::inilialize_impl(storage_handle *) const
{
    raw_config_option_def def = raw_config_option_def_init();
    def.opt_key = "layer_height_min_flat_area";
    def.type = RAW_CO_FLOAT;
    def.container_type = RAW_CONTAINER_TYPE_PROJECT;
    def.option_preset_type = RAW_PRESET_TYPE_FFF_PRINT;
    def.printer_technology = RAW_PT_FFF;
    def.label = "Min flat area";
    def.full_label = "Minimum flat area for layer matching";
    def.category = RAW_OPTION_CATEGORY_SLICING;
    def.level = RAW_OPTION_LEVEL_ADVANCED;
    def.invalidates_step = STEP_LAYER_HEIGHT;
    def.tooltip = "Horizontal mesh surfaces whose total area at a Z is below this value are ignored by the flat-area layer height plugin.";
    def.sidetext = "mm2";
    def.has_min = 1;
    def.min_value = 0.0;
    def.precision = 3;
    def.mode = RAW_CONFIG_OPTION_MODE_ADV_EXP | RAW_CONFIG_OPTION_MODE_SUSI;
    def.default_serialized_value = "1";
    orchestrator_create_option_def(m_orchestrator, &def);

    orchestrator_add_ui_fragment(m_orchestrator,
                                 "print.ui",
                                 k_flat_area_layer_height_id,
                                 FlatAreaLayerHeight::print_ui_fragment(),
                                 0);
}

void FlatAreaLayerHeight::setup_run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_layer_height_generation *ctx = plugin_ctx_as_layer_height_generation(run_ctx);
    if (ctx == nullptr || ctx->object == nullptr)
        return;
    progress().add_max(ProgressScanVolumes, Object(ctx->object).volume_count());
}

void FlatAreaLayerHeight::run_impl(const plugin_run_context *run_ctx) const
{
    const run_ctx_layer_height_generation *ctx = plugin_ctx_as_layer_height_generation(run_ctx);
    if (ctx == nullptr || ctx->print == nullptr || ctx->object == nullptr || ctx->set_layer_height_profile == nullptr)
        return;

    if (ctx->enforce_layer_zs != nullptr && ctx->enforce_layer_zs_size > 0) {
        ctx->set_layer_height_profile(ctx->object, ctx->enforce_layer_zs, ctx->enforce_layer_zs_size);
        progress().finish_run(ProgressScanVolumes);
        return;
    }

    const Print print(ctx->print);
    const Object object(ctx->object);
    const Config print_config = print.config();
    const double min_flat_area = print_config.get("layer_height_min_flat_area").get_float();
    const LayerHeightSlicingParameters params = make_layer_height_slicing_parameters(print, object, ctx->max_z);
    std::vector<coord_t> profile = make_flat_area_layer_height_profile(object, params, min_flat_area, progress());

    if (!profile.empty())
        ctx->set_layer_height_profile(ctx->object, profile.data(), uint32_t(profile.size()));
    else
        ctx->set_layer_height_profile(ctx->object, nullptr, 0);

    progress().finish_run(ProgressScanVolumes);
}

void register_flat_area_layer_height_plugin(orchestrator_handle *orch)
{
    orchestrator_register_plugin(orch, FlatAreaLayerHeight::instance(orch).c_instance());
}

}} // namespace slic3r_api::FlatAreaLayerHeightPlugin

#ifdef FLAT_AREA_LAYER_HEIGHT_PLUGIN_DLL
extern "C" SLIC3R_PLUGIN_API void register_plugin(orchestrator_handle *orch)
{
    slic3r_api::FlatAreaLayerHeightPlugin::register_flat_area_layer_height_plugin(orch);
}
#endif // FLAT_AREA_LAYER_HEIGHT_PLUGIN_DLL
