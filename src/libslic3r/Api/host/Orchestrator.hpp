///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Orchestrator_hpp_
#define slic3r_Orchestrator_hpp_

#include <atomic>
#include <cassert>
#include <map>
#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <unordered_set>
#include <utility>
#include <vector>

#include "libslic3r/Api/plugin/c/slic3r_bridge_detector.h"
#include "libslic3r/Api/plugin/c/slic3r_config_def.h"
#include "libslic3r/MultiPoint.hpp"

#include "Plugin.hpp"

namespace Slic3r {

class Orchestrator;
class Print;
class MultiPoint;
class Polyline;
class Polygon;
class ExPolygon;
class PluginStorage;
namespace ApiClipper { class ClipperShapes; }

} // namespace Slic3r

// Host-side state passed back to callbacks through the opaque C ABI handle.
// It is intentionally not exposed in Api/plugin/c: plugins only see
// plugin_host_context* and must use the callbacks from plugin_run_context.
struct plugin_host_context
{
    Slic3r::Orchestrator *orchestrator = nullptr;
    Slic3r::Print *print = nullptr;
    Slic3r::Plugin *plugin = nullptr;
    slicing_step_t step = STEP_LAYER_HEIGHT;
    size_t object_idx = 0;
    size_t object_count = 0;
};

namespace Slic3r {

class Orchestrator
{
public:
    static Orchestrator &instance();

    std::vector<Plugin *> get_all_plugins_for_step(slicing_step_t step) const;
    std::vector<Plugin *> get_current_plugins_for_step(slicing_step_t step) const;
    const Plugin *get_plugin(const std::string &plugin_id) const;
    void add_plugin_to_step(Plugin *plugin, slicing_step_t step);


    //config def
    void create_new_print_config(const raw_config_option_def *def);

    bool register_plugin(plugin_instance plugin);

    bridge_detector_instance create_bridge_detector(const bridge_detector_create_input &input);
    void slice(Print &print_to_slice);

    Orchestrator(const Orchestrator&) = delete;
    Orchestrator& operator=(const Orchestrator&) = delete;
    Orchestrator(Orchestrator&&) = delete;
    Orchestrator& operator=(Orchestrator&&) = delete;

    std::map<Plugin *, PluginStorage> &plugin_storage() { return m_plugin_storage; }
    const std::map<Plugin *, PluginStorage> &plugin_storage() const { return m_plugin_storage; }
    plugin_host_context prepare_plugin_host_context(slicing_step_t step,
                                                    Plugin *plugin,
                                                    Print *print = nullptr);
    plugin_run_context prepare_plugin_run_context(slicing_step_t step,
                                                  Plugin *plugin,
                                                  plugin_host_context *host_context = nullptr);
    bool is_plugin_cancelled() const;
    void request_plugin_cancel();
    void reset_plugin_cancel();
    void initialize_plugins();

private:
    Orchestrator() = default;

    std::vector<std::unique_ptr<Plugin>> m_registered_plugins;
    std::map<slicing_step_t, std::vector<Plugin *>> m_plugins_by_step;
    std::map<Plugin *, PluginStorage> m_plugin_storage;
    std::atomic_bool m_plugin_cancel_requested { false };
};

// temporary storage for plugins
template<class T> class StableOwnedVector
{
public:
    using Storage = std::vector<std::unique_ptr<T>>;
    using iterator = typename Storage::iterator;
    using const_iterator = typename Storage::const_iterator;

    template<class... Args> T &emplace_back(Args&&... args) {
        m_items.emplace_back(std::make_unique<T>(std::forward<Args>(args)...));
        return *m_items.back();
    }

    T &push_back(std::unique_ptr<T> item) {
        assert(item != nullptr);
        m_items.push_back(std::move(item));
        return *m_items.back();
    }

    iterator begin() { return m_items.begin(); }
    iterator end() { return m_items.end(); }
    const_iterator begin() const { return m_items.begin(); }
    const_iterator end() const { return m_items.end(); }

    iterator erase(iterator it) { return m_items.erase(it); }
    void clear() { m_items.clear(); }

private:
    Storage m_items;
};

class PluginStorage
{
public:
    PluginStorage();
    ~PluginStorage();

    // Stored objects live behind unique_ptr so their addresses stay stable even
    // if the owning vector reallocates. Plugins may keep these handles until
    // storage_free() or clear().
    StableOwnedVector<Polyline> polylines;
    StableOwnedVector<Polygon> polygons;
    StableOwnedVector<ExPolygon> expolygons;
    StableOwnedVector<std::vector<Polyline>> polyline_collections;
    StableOwnedVector<std::vector<Polygon>> polygon_collections;
    StableOwnedVector<std::vector<ExPolygon>> expolygon_collections;
    std::vector<std::unique_ptr<ApiClipper::ClipperShapes>> clipper_shapes;
    std::unordered_set<void *> generic_storage;

    void clear();
    size_t size() const;
    bool contains(void *ptr) const;
    bool free(void *ptr);

};

} // namespace Slic3r


#endif // slic3r_Orchestrator_hpp_
