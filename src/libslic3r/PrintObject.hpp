///|/ Copyright (c) Prusa Research 2016 - 2023 Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros, Pavel Mikuš @Godrak, Lukáš Hejl @hejllukas, Filip Sykala @Jony01, Oleksandra Iushchenko @YuSanka, Vojtěch Král @vojtechkral
///|/ Copyright (c) BambuStudio 2023 manch1n @manch1n
///|/ Copyright (c) SuperSlicer 2022 Remi Durand @supermerill
///|/ Copyright (c) 2019 Bryan Smith
///|/ Copyright (c) 2017 Eyal Soha @eyal0
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2017 Joseph Lenox @lordofhyphens
///|/
///|/ ported from lib/Slic3r/Print.pm:
///|/ Copyright (c) Prusa Research 2016 - 2018 Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros
///|/ Copyright (c) Slic3r 2011 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2012 - 2013 Mark Hindess
///|/ Copyright (c) 2013 Devin Grady
///|/ Copyright (c) 2012 - 2013 Mike Sheldrake @mesheldrake
///|/ Copyright (c) 2012 Henrik Brix Andersen @henrikbrixandersen
///|/ Copyright (c) 2012 Michael Moon
///|/ Copyright (c) 2011 Richard Goodwin
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_PrintObject_hpp_
#define slic3r_PrintObject_hpp_

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include <Eigen/Geometry>

#include "BoundingBox.hpp"
#include "ContainerUtils.hpp"
#include "DataTreeFwd.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "Fill/FillAdaptive.hpp"
#include "Fill/FillLightning.hpp"
#include "Flow.hpp"
#include "libslic3r.h"
#include "Point.hpp"
#include "Polygon.hpp"
#include "PrintBase.hpp"
#include "PrintRegion.hpp"
#include "Slicing.hpp"
#include "SupportSpotsGenerator.hpp"
#include "Surface.hpp"
#include "TriangleSelector.hpp"

namespace Slic3r {

class GCodeGenerator;
class Print;
class PrintObject;
namespace Steps { class StepPipeline; }
namespace ApiInternal { struct PrintObjectAccess; }

enum PrintObjectStep : uint8_t {
    posSlice,
    posPerimeters,
    posPrepareInfill,
    posInfill,
    posIroning,
    posSupportSpotsSearch,
    posSupportMaterial, 
    posEstimateCurledExtrusions,
    posCalculateOverhangingPerimeters,
    posSimplifyPath, // simplify &  arc fitting from BBS
    posCount,
};

/**
* order:
*            m_objects[idx]->make_perimeters();
*                   -> slice()
*                   -> make_perimeters()
*            m_objects[idx]->infill();
*            m_objects[idx]->ironing();
*            obj->generate_support_spots();
*            psAlertWhenSupportsNeeded
*            obj.generate_support_material();
*            obj.estimate_curled_extrusions();
*            obj.calculate_overhanging_perimeters();
*            _make_wipe_tower();
*            _make_skirt();
*            make_brim();
*            simplify_extrusion_path();
* 
*           then export_gcode();
* */

// step % for starting this step
inline std::map<PrintObjectStep, int> objectstep_2_percent = {{PrintObjectStep::posSlice, 0},
                                                       {PrintObjectStep::posPerimeters, 10},
                                                       {PrintObjectStep::posPrepareInfill, 20},
                                                       {PrintObjectStep::posInfill, 30},
                                                       {PrintObjectStep::posIroning, 40},
                                                       {PrintObjectStep::posSupportSpotsSearch, 45},
                                                       {PrintObjectStep::posSupportMaterial, 50},
                                                       {PrintObjectStep::posEstimateCurledExtrusions, 60},
                                                       {PrintObjectStep::posCalculateOverhangingPerimeters, 65},
                                                       {PrintObjectStep::posSimplifyPath, 80},
                                                       {PrintObjectStep::posCount, 85}};

// Single instance of a PrintObject.
// As multiple PrintObjects may be generated for a single ModelObject (their instances differ in rotation around Z),
// ModelObject's instancess will be distributed among these multiple PrintObjects.
struct PrintInstance
{
    // Parent PrintObject
    PrintObject 		*print_object;
    // Source ModelInstance of a ModelObject, for which this print_object was created.
	const ModelInstance *model_instance;
	// Shift of this instance's center into the world coordinates.
	Point 				 shift;
};

class PrintObjectRegions
{
public:
    // Bounding box of a ModelVolume transformed into the working space of a PrintObject, possibly
    // clipped by a layer range modifier.
    // Only Eigen types of Nx16 size are vectorized. This bounding box will not be vectorized.
    static_assert(sizeof(Eigen::AlignedBox<float, 3>) == 24, "Eigen::AlignedBox<float, 3> is not being vectorized, thus it does not need to be aligned");
    using BoundingAlignedBox3f = Eigen::AlignedBox<float, 3>;
    struct VolumeExtents {
        ObjectID             volume_id;
        BoundingAlignedBox3f          bbox;
    };

    struct VolumeRegion
    {
        // ID of the associated ModelVolume.
        const ModelVolume   *model_volume { nullptr };
        // Index of a parent VolumeRegion.
        int                  parent { -1 };
        // Pointer to PrintObjectRegions::all_regions, null for a negative volume.
        PrintRegion         *region { nullptr };
        // Pointer to VolumeExtents::bbox.
        const BoundingAlignedBox3f   *bbox { nullptr };
        // To speed up merging of same regions.
        const VolumeRegion  *prev_same_region { nullptr };
    };

    struct PaintedRegion
    {
        // 1-based extruder identifier.
        unsigned int     extruder_id;
        // Index of a parent VolumeRegion.
        int              parent { -1 };
        // Pointer to PrintObjectRegions::all_regions.
        PrintRegion     *region { nullptr };
    };

    // One slice over the PrintObject (possibly the whole PrintObject) and a list of ModelVolumes and their bounding boxes
    // possibly clipped by the layer_height_range.
    struct LayerRangeRegions
    {
        std::pair<coord_t, coord_t> layer_height_range_;
        // Config of the layer range, null if there is just a single range with no config override.
        // Config is owned by the associated ModelObject.
        const DynamicPrintConfig*   config { nullptr };
        // Volumes sorted by ModelVolume::id().
        std::vector<VolumeExtents>  volumes;

        // Sorted in the order of their source ModelVolumes, thus reflecting the order of region clipping, modifier overrides etc.
        std::vector<VolumeRegion>   volume_regions;
        std::vector<PaintedRegion>  painted_regions;

        bool has_volume(const ObjectID id) const {
            auto it = lower_bound_by_predicate(this->volumes.begin(), this->volumes.end(), [id](const VolumeExtents &l) { return l.volume_id < id; });
            return it != this->volumes.end() && it->volume_id == id;
        }
    };

    struct GeneratedSupportPoints{
        Transform3d object_transform; // for frontend object mapping
        SupportSpotsGenerator::SupportPoints support_points;
        SupportSpotsGenerator::PartialObjects partial_objects;
    };

    std::vector<std::unique_ptr<PrintRegion>>   all_regions;
    std::vector<LayerRangeRegions>              layer_ranges;
    // Transformation of this ModelObject into one of the associated PrintObjects (all PrintObjects derived from a single modelObject differ by a Z rotation only).
    // This transformation is used to calculate VolumeExtents.
    Transform3d                                 trafo_bboxes;
    std::vector<ObjectID>                       cached_volume_ids;

    std::optional<GeneratedSupportPoints> generated_support_points;

    void clear() {
        all_regions.clear();
        layer_ranges.clear();
        cached_volume_ids.clear();
    }

private:
    friend class PrintObject;
    // Number of PrintObjects generated from the same ModelObject and sharing the regions.
    // ref_cnt could only be modified by the main thread, thus it does not need to be atomic.
    size_t                                      m_ref_cnt{ 0 };
};

class PrintObject : public PrintObjectBaseWithState<Print, PrintObjectStep, posCount>
{
private: // Prevents erroneous use by other classes.
    typedef PrintObjectBaseWithState<Print, PrintObjectStep, posCount> Inherited;

public:
    // Size of an object: XYZ in scaled coordinates. The size might not be quite snug in XY plane.
    const Vec3crd&               size() const           { return m_size; }
    const PrintObjectConfig&     config() const         { return m_config; }
    const PrintRegionConfig&     default_region_config(const PrintRegionConfig &from_print) const;
    const Transform3d&           trafo() const          { return m_trafo; }
    // Trafo with the center_offset() applied after the transformation, to center the object in XY before slicing.
    Transform3d                  trafo_centered() const 
        { Transform3d t = this->trafo(); t.pretranslate(Vec3d(- unscaled(m_center_offset.x()), - unscaled(m_center_offset.y()), 0)); return t; }
    const PrintInstances&        instances() const      { return m_instances; }

    // Bounding box is used to align the object infill patterns, and to calculate attractor for the rear seam.
    // The bounding box may not be quite snug.
    BoundingBox                  bounding_box() const   { return BoundingBox(Point(- m_size.x() / 2, - m_size.y() / 2), Point(m_size.x() / 2, m_size.y() / 2)); }
    // Height is used for slicing, for sorting the objects by height for sequential printing and for checking vertical clearence in sequential print mode.
    // The height is snug.
    coord_t                     height() const         { return m_size.z(); }
    // Centering offset of the sliced mesh from the scaled and rotated mesh of the model.
    const Point&                center_offset() const  { return m_center_offset; }

    bool                         has_brim() const;
    Polygons                     get_brim_patch(ModelVolumeType brim_type, const PrintInstance *instance = nullptr) const;

    // Whoever will get a non-const pointer to PrintObject will be able to modify its layers.
    size_t          layer_count() const { return m_layers.size(); }
    void            clear_layers();
    const Layer&    layer(size_t idx) const { return *m_layers[idx]; }
    Layer&          layer(size_t idx) 		{ return *m_layers[idx]; }
    LayerCRefs      layers() const         { return make_ref_view<Layer>(m_layers); }
    LayerRefs       layers()               { return make_ref_view<Layer>(m_layers); }
    LayerUPtrs&     mutable_layers()          { return m_layers; }
    // Get a layer exactly at print_z.
    const Layer*    get_layer_at_printz(coord_t print_z) const;
    Layer*          get_layer_at_printz(coord_t print_z);
    // Get a layer approximately at print_z.
    const Layer*    get_layer_at_printz(double print_z_mm, double epsilon) const;
    Layer*          get_layer_at_printz(double print_z_mm, double epsilon);
    // Get the first layer approximately bellow print_z.
    const Layer*    get_first_layer_below_printz(coord_t print_z) const;
    const Layer*    get_first_layer_below_printz(double print_z_mm, double epsilon) const;
    // For sparse infill, get the max spasing avaialable in this object (avaialable after prepare_infill)
    coord_t         get_sparse_max_spacing() const { return m_max_sparse_spacing; }
    
    
    size_t                  support_layer_count() const { return m_support_layers.size(); }
    void                    clear_support_layers();
    const SupportLayer&     support_layer(size_t idx) const { return *m_support_layers[idx]; }
    SupportLayerCRefs       support_layers() const { return make_ref_view<SupportLayer>(m_support_layers); }
    SupportLayerUPtrs&      mutable_support_layers()  { return m_support_layers; }
    void                    add_support_layer(int id, int interface_id, coord_t height, coord_t print_z);
    SupportLayerUPtrs::iterator insert_support_layer(SupportLayerUPtrs::const_iterator pos, size_t id, size_t interface_id, coord_t height, coord_t print_z, double slice_z);

    // This is the *total* layer count (including support layers)
    // this value is not supposed to be compared with Layer::id
    // since they have different semantics.
    size_t          total_layer_count() const { return this->layer_count() + this->support_layer_count(); }

    // Initialize the layer_height_profile from the model_object's layer_height_profile, from model_object's layer height table, or from slicing parameters.
    // Returns true, if the layer_height_profile was changed.
    static bool     update_layer_height_profile(const ModelObject &model_object, const SlicingParameters &slicing_parameters, std::vector<coordf_t> &layer_height_profile);
    const std::vector<coord_t>& layer_profile() const { return m_layer_profile; }

    // Collect the slicing parameters, to be used by variable layer thickness algorithm,
    // by the interactive layer height editor and by the printing process itself.
    // The slicing parameters are dependent on various configuration values
    // (layer height, first layer height, raft settings, print nozzle diameter etc).
    const SlicingParameters&                    slicing_parameters() const { return *m_slicing_params; }
    static std::shared_ptr<SlicingParameters>   slicing_parameters(const DynamicPrintConfig &full_config, const ModelObject &model_object, float object_max_z);

    size_t                      num_printing_regions()  const throw() { assert(m_shared_regions); return m_shared_regions->all_regions.size(); }
    const PrintRegion&          printing_region(size_t idx) const throw() { assert(m_shared_regions); return *(m_shared_regions->all_regions[idx].get()); }
    //FIXME returing all possible regions before slicing, thus some of the regions may not be slicing at the end.
    std::vector<std::reference_wrapper<const PrintRegion>> all_regions() const;
    const PrintObjectRegions*   shared_regions()        const throw() { assert(m_shared_regions); return m_shared_regions.get(); }

    bool                        has_support()           const { return m_config.support_material || m_config.support_material_enforce_layers > 0; }
    bool                        has_raft()              const { return m_config.raft_layers > 0; }
    bool                        has_support_material()  const { return this->has_support() || this->has_raft(); }
    // Checks if the model object is painted using the multi-material painting gizmo.
    bool                        is_mm_painted()         const { return this->model_object()->is_mm_painted(); }

    // returns 0-based indices of extruders used to print the object (without brim, support and other helper extrusions)
    std::set<uint16_t>   object_extruders() const;
    double               get_first_layer_height() const;

    // Called by make_perimeters()
    void slice();

    // Helpers to slice support enforcer / blocker meshes by the support generator.
    std::vector<ExPolygons>     slice_support_volumes(const ModelVolumeType model_volume_type) const;
    std::vector<ExPolygons>     slice_support_blockers() const { return this->slice_support_volumes(ModelVolumeType::SUPPORT_BLOCKER); }
    std::vector<ExPolygons>     slice_support_enforcers() const { return this->slice_support_volumes(ModelVolumeType::SUPPORT_ENFORCER); }

    // Helpers to project custom facets on slices
    std::vector<Polygons> project_and_append_custom_facets(bool seam, EnforcerBlockerType type) const;

    /// skirts if done per copy and not per platter
    const std::optional<ExtrusionEntityCollection>& skirt_first_layer() const { return m_skirt_first_layer; }
    const ExtrusionEntityCollection& skirt() const { return m_skirt; }
    const ExtrusionEntityCollection& brim() const { return m_brim; }

    // for unique_ptr
    ~PrintObject() override;
protected:
    // to be called from Print only.
    friend class Print;
    friend class PrintBaseWithState<PrintStep, psCount>;
    friend class Steps::StepPipeline;
    friend struct ApiInternal::PrintObjectAccess;

    PrintObject(Print* print, ModelObject* model_object, const Transform3d& trafo, PrintInstances&& instances);
    // as Layers are linked to us via a pointer, we can't move ourselves, or the link is severed
    PrintObject(PrintObject&&) = delete;
    PrintObject& operator=(PrintObject&&) = delete;
    PrintObject(const PrintObject&) = delete;
    PrintObject& operator=(const PrintObject&) = delete;

    void                    config_apply(const ConfigBase &other, bool ignore_nonexistent = false) { m_config.apply(other, ignore_nonexistent); }
    void                    config_apply_only(const ConfigBase &other, const t_config_option_keys &keys, bool ignore_nonexistent = false) { m_config.apply_only(other, keys, ignore_nonexistent); }
    PrintBase::ApplyStatus  set_instances(PrintInstances &&instances);
    // Invalidates the step, and its depending steps in PrintObject and Print.
    bool                    invalidate_step(PrintObjectStep step);
    // Invalidates all PrintObject and Print steps.
    bool                    invalidate_all_steps();
    // Invalidate steps based on a set of parameters changed.
    // It may be called for both the PrintObjectConfig and PrintRegionConfig.
    bool                    invalidate_state_by_config_options(
        const ConfigOptionResolver &old_config, const ConfigOptionResolver &new_config, const std::vector<t_config_option_key> &opt_keys);
    // If ! m_slicing_params.valid, recalculate.
    void                    update_slicing_parameters();

    // Called on main thread with stopped or paused background processing to let PrintObject release data for its milestones that were invalidated or canceled.
    void                    cleanup();

    static PrintObjectConfig object_config_from_model_object(const PrintObjectConfig &default_object_config, const ModelObject &object, size_t num_extruders);

private:
    void make_perimeters();
    void prepare_infill();
    void clear_fills();
    void infill();
    void ironing();
    void generate_support_spots();
    void generate_support_material();
    void estimate_curled_extrusions();
    void calculate_overhanging_perimeters();
    void simplify_extrusion_path();

    void slice_volumes();
    // Has any support (not counting the raft).
    ExPolygons _shrink_contour_holes(double contour_delta, double default_delta, double convex_delta, const ExPolygons& input) const;
    void _transform_hole_to_polyholes();
    void _max_overhang_threshold();
    ExPolygons _smooth_curves(const ExPolygons &input, const PrintRegionConfig &conf) const;
    void detect_surfaces_type();
    void apply_solid_infill_below_layer_area();
    void process_external_surfaces(bool old);
    void discover_vertical_shells();
    void bridge_over_infill();
    void replaceSurfaceType(SurfaceType st_to_replace, SurfaceType st_replacement, SurfaceType st_under_it);
    // void clip_fill_surfaces(); //infill_only_where_needed
    void tag_under_bridge();
    void discover_horizontal_shells();
    void clean_surfaces();
    void combine_infill();
    void _generate_support_material();
    void _compute_max_sparse_spacing();
    std::pair<FillAdaptive::OctreePtr, FillAdaptive::OctreePtr> prepare_adaptive_infill_data(
        const std::vector<std::pair<const Surface*, coord_t>>& surfaces_w_bottom_z) const;
    FillLightning::GeneratorPtr prepare_lightning_infill_data();

    // XYZ in scaled coordinates
    Vec3crd									m_size;
    PrintObjectConfig                       m_config;
    // Translation in Z + Rotation + Scaling / Mirroring.
    Transform3d                             m_trafo = Transform3d::Identity();
    // Slic3r::Point objects in scaled G-code coordinates
    std::vector<PrintInstance>              m_instances;
    // The mesh is being centered before thrown to Clipper, so that the Clipper's fixed coordinates require less bits.
    // This is the adjustment of the  the Object's coordinate system towards PrintObject's coordinate system.
    Point                                   m_center_offset;

    // Object split into layer ranges and regions with their associated configurations.
    // Shared among PrintObjects created for the same ModelObject.
    std::shared_ptr<PrintObjectRegions>     m_shared_regions;

    std::shared_ptr<SlicingParameters>      m_slicing_params;
    LayerUPtrs                               m_layers;
    SupportLayerUPtrs                        m_support_layers;

    // Ordered collections of extrusion paths to build skirt loops and brim.
    // have to be duplicated per copy
    std::optional<ExtrusionEntityCollection> m_skirt_first_layer;
    ExtrusionEntityCollection               m_skirt;
    ExtrusionEntityCollection               m_brim;

    // this is set to true when LayerRegion->slices is split in top/internal/bottom
    // so that next call to make_perimeters() performs a union() before computing loops
    bool                                  m_typed_slices = false;

    //this setting allow fill_aligned_z to get the max sparse spacing spacing.
    coord_t                                 m_max_sparse_spacing = 0;

    // pair < adaptive , support>, filled by prepare_adaptive_infill_data() (in bridge_over_infill() in prepare_infill()) and used in infill()
    std::pair<FillAdaptive::OctreePtr, FillAdaptive::OctreePtr> m_adaptive_fill_octrees;
    // filled by prepare_lightning_infill_data() (in bridge_over_infill() in prepare_infill()) and used in infill()
    FillLightning::GeneratorPtr m_lightning_generator;

    // Result of LayerHeightGeneration. It stores pairs of layer z / layer height, so its size is 2 * layer_count.
    std::vector<coord_t> m_layer_profile;

};




} /* namespace Slic3r */

#endif // slic3r_PrintObject_hpp_
