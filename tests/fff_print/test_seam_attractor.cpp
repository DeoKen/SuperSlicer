#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <string>
#include <vector>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Seam attractors ported from SuperSlicer: ModelVolumes SEAM_POSITION_CENTER (sphere), SEAM_POSITION_CENTER_Z (cylinder).

namespace {

// The start of the external perimeter of each layer (the seam) and the layer's print_z.
struct Seam { double z; Vec2d point; };

std::vector<Seam> external_perimeter_seams(const std::string &gcode)
{
    std::vector<Seam> seams;
    std::string type;
    double      z = 0.;
    bool        found = false;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        const std::string &raw = line.raw();
        if (boost::starts_with(raw, ";Z:")) {
            z     = std::stod(raw.substr(3));
            found = false;
        } else if (boost::starts_with(raw, ";TYPE:"))
            type = raw.substr(6);
        else if (! found && line.cmd_is("G1") && type == "External perimeter" && line.dist_E(self) > 0 && line.dist_XY(self) > 0) {
            seams.push_back({ z, Vec2d(self.x(), self.y()) });
            found = true;
        }
    });
    return seams;
}

DynamicPrintConfig config_with(const std::string &seam_position)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config_with({
        { "skirts",       0 },
        { "fill_density", "20%" },
        { "layer_height", 0.2 },
        { "perimeters",   2 },
    });
    config.set_deserialize_strict({ { "seam_position", seam_position } });
    return config;
}

// The 20 mm cube's bounding box on the bed and in the object's coordinates.
struct Boxes { BoundingBoxf3 world; BoundingBoxf3 object; };

Boxes boxes(const Model &model)
{
    const ModelObject *mo = model.objects.front();
    return { mo->instance_bounding_box(0), mo->volumes.front()->mesh().transformed_bounding_box(mo->volumes.front()->get_matrix()) };
}

ModelVolume& add_attractor(Model &model, ModelVolumeType type, const Vec3d &object_position, double height = 2.)
{
    ModelObject *mo     = model.objects.front();
    ModelVolume *volume = mo->add_volume(TriangleMesh(type == ModelVolumeType::SEAM_POSITION_CENTER ? its_make_sphere(1., PI / 18) :
                                                                                                     its_make_cylinder(1., height)), type);
    volume->set_offset(object_position);
    return *volume;
}

} // namespace

TEST_CASE("Seam attractor: the seams go to the point nearest to the sphere", "[SeamAttractor]")
{
    const std::string seam_position = GENERATE("aligned", "rear", "nearest", "random");
    INFO("seam_position = " << seam_position);
    DynamicPrintConfig config = config_with(seam_position);
    Print print;
    Model model;
    init_print({ TestMesh::cube_20x20x20 }, print, model, config);
    const Boxes bb = boxes(model);
    // Right of the right side, 5 mm in front of its middle, at half height.
    add_attractor(model, ModelVolumeType::SEAM_POSITION_CENTER, Vec3d(bb.object.max.x() + 3., bb.object.center().y() - 5., bb.object.center().z()));
    print.apply(model, config);

    const std::vector<Seam> seams = external_perimeter_seams(gcode(print));
    REQUIRE(seams.size() > 90);
    for (const Seam &seam : seams) {
        INFO("z = " << seam.z << " seam " << seam.point.x() << ", " << seam.point.y());
        CHECK(std::abs(seam.point.x() - bb.world.max.x()) < 0.5);
        CHECK(std::abs(seam.point.y() - (bb.world.center().y() - 5.)) < 0.3);
    }
}

TEST_CASE("Seam attractor: a cylinder only acts within its height", "[SeamAttractor]")
{
    DynamicPrintConfig config = config_with("rear");
    Print print;
    Model model;
    init_print({ TestMesh::cube_20x20x20 }, print, model, config);
    const Boxes bb = boxes(model);
    // Left of the left side, 4 mm behind its middle, from the bed to 10 mm.
    add_attractor(model, ModelVolumeType::SEAM_POSITION_CENTER_Z, Vec3d(bb.object.min.x() - 3., bb.object.center().y() + 4., bb.object.min.z() + 5.), 10.);
    // A sphere far above: the cylinder wins where it applies.
    add_attractor(model, ModelVolumeType::SEAM_POSITION_CENTER, Vec3d(bb.object.center().x(), bb.object.min.y() - 3., bb.object.max.z() + 30.));
    print.apply(model, config);

    const std::vector<Seam> seams = external_perimeter_seams(gcode(print));
    REQUIRE(seams.size() > 90);
    size_t below = 0, above = 0;
    for (const Seam &seam : seams) {
        INFO("z = " << seam.z << " seam " << seam.point.x() << ", " << seam.point.y());
        const double z = seam.z - bb.world.min.z();
        if (z < 9.9) {
            ++ below;
            CHECK(std::abs(seam.point.x() - bb.world.min.x()) < 0.5);
            CHECK(std::abs(seam.point.y() - (bb.world.center().y() + 4.)) < 0.3);
        } else if (z > 10.3) {
            ++ above;
            // The sphere: front side, middle.
            CHECK(std::abs(seam.point.y() - bb.world.min.y()) < 0.5);
            CHECK(std::abs(seam.point.x() - bb.world.center().x()) < 0.3);
        }
    }
    CHECK(below > 40);
    CHECK(above > 40);
}

TEST_CASE("Seam attractor: painted enforcers win", "[SeamAttractor]")
{
    DynamicPrintConfig config = config_with("aligned");
    Print print;
    Model model;
    init_print({ TestMesh::cube_20x20x20 }, print, model, config);
    const Boxes bb = boxes(model);
    add_attractor(model, ModelVolumeType::SEAM_POSITION_CENTER, Vec3d(bb.object.max.x() + 3., bb.object.center().y(), bb.object.center().z()));
    // Paint the left side (all facets with the normal -X) as enforcer.
    ModelVolume     &part = *model.objects.front()->volumes.front();
    TriangleSelector selector(part.mesh());
    const indexed_triangle_set &its = part.mesh().its;
    for (size_t i = 0; i < its.indices.size(); ++ i) {
        const Vec3f n = its_face_normal(its, int(i));
        if (n.x() < -0.9f)
            selector.set_facet(int(i), TriangleStateType::ENFORCER);
    }
    part.seam_facets.set(selector);
    print.apply(model, config);

    const std::vector<Seam> seams = external_perimeter_seams(gcode(print));
    REQUIRE(seams.size() > 90);
    for (const Seam &seam : seams) {
        INFO("z = " << seam.z << " seam " << seam.point.x() << ", " << seam.point.y());
        // On the painted side, at the point nearest to the attractor: a corner of the left side.
        CHECK(std::abs(seam.point.x() - bb.world.min.x()) < 0.5);
    }
}

TEST_CASE("Seam attractor: moving it only exports the G-code again", "[SeamAttractor]")
{
    DynamicPrintConfig config = config_with("aligned");
    Print print;
    Model model;
    init_print({ TestMesh::cube_20x20x20 }, print, model, config);
    const Boxes bb = boxes(model);
    ModelVolume &attractor = add_attractor(model, ModelVolumeType::SEAM_POSITION_CENTER,
                                           Vec3d(bb.object.max.x() + 3., bb.object.center().y(), bb.object.center().z()));
    print.apply(model, config);
    gcode(print);
    REQUIRE(print.objects().front()->is_step_done(posPerimeters));
    const auto cache = print.seam_placer_cache();
    REQUIRE(cache);

    attractor.set_offset(Vec3d(bb.object.center().x(), bb.object.max.y() + 3., bb.object.center().z()));
    print.apply(model, config);
    CHECK(print.objects().front()->is_step_done(posPerimeters));
    CHECK(! print.is_step_done(psGCodeExport));
    const std::vector<Seam> seams = external_perimeter_seams(gcode(print));
    // The seam placer was not reused with the old attractor.
    CHECK(print.seam_placer_cache() != cache);
    REQUIRE(! seams.empty());
    for (const Seam &seam : seams)
        CHECK(std::abs(seam.point.y() - bb.world.max.y()) < 0.5);
}

TEST_CASE("Seam attractor: saved in the 3mf with the SuperSlicer type names", "[SeamAttractor]")
{
    CHECK(ModelVolume::type_to_string(ModelVolumeType::SEAM_POSITION_CENTER) == "SeamPositionCenter");
    CHECK(ModelVolume::type_to_string(ModelVolumeType::SEAM_POSITION_CENTER_Z) == "SeamPositionCenterZ");
    CHECK(ModelVolume::type_from_string("SeamPosition") == ModelVolumeType::SEAM_POSITION_CENTER);

    DynamicPrintConfig config = config_with("aligned");
    Print print;
    Model model;
    init_print({ TestMesh::cube_20x20x20 }, print, model, config);
    add_attractor(model, ModelVolumeType::SEAM_POSITION_CENTER, Vec3d(15., 0., 10.));
    add_attractor(model, ModelVolumeType::SEAM_POSITION_CENTER_Z, Vec3d(-15., 0., 5.), 10.);
    const auto path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("seam-attractor-%%%%-%%%%.3mf");
    REQUIRE(store_3mf(path.string().c_str(), &model, &config, false));
    DynamicPrintConfig        loaded_config;
    ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Disable };
    Model                     loaded;
    boost::optional<Semver>   version;
    REQUIRE(load_3mf(path.string().c_str(), loaded_config, ctxt, &loaded, false, version));
    boost::filesystem::remove(path);
    REQUIRE(loaded.objects.size() == 1);
    REQUIRE(loaded.objects.front()->volumes.size() == 3);
    CHECK(loaded.objects.front()->volumes[1]->type() == ModelVolumeType::SEAM_POSITION_CENTER);
    CHECK(loaded.objects.front()->volumes[2]->type() == ModelVolumeType::SEAM_POSITION_CENTER_Z);
}
