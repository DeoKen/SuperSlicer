#include <catch2/catch.hpp>

#include "perimeter_test_helpers.hpp"
#include "plugin_test_helpers.hpp"
#include "test_data.hpp"

#include <array>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_post_perimeter.h"
#include "libslic3r/Api/plugin/c/steps/slic3r_step_pre_perimeter.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintObject.hpp"
#include "libslic3r/Steps/StepPostPerimeterGeneration.hpp"
#include "libslic3r/Steps/StepPrepareForPeriemters.hpp"
#include "libslic3r/Steps/StepPipeline.hpp"

namespace {
using namespace Slic3r;
using namespace Slic3r::Test::PerimeterPluginTests;

struct RecordedEvent
{
    std::string plugin_id;
    std::string callback;
    slicing_step_t context_step = STEP_NONE;
    uint32_t run_count = 0;
    const print_handle *print = nullptr;
    const object_handle *object = nullptr;
    size_t object_idx = size_t(-1);
    size_t object_count = 0;
};

struct RecordingPluginState
{
    const char *id = nullptr;
    slicing_step_t step = STEP_NONE;
    int32_t priority = 0;
    const char *exclusive_group = "";
    const char *exclusive_group_label = "";
    const char *exclusive_group_tooltip = "";
    std::vector<RecordedEvent> *events = nullptr;
    std::mutex *mutex = nullptr;
};

RecordingPluginState g_pre_first  = {"test.pre_perimeter.first", STEP_PRE_PERIMETER, -10};
RecordingPluginState g_pre_second = {"test.pre_perimeter.second", STEP_PRE_PERIMETER, 20};
RecordingPluginState g_pre_inactive = {"test.pre_perimeter.inactive", STEP_PRE_PERIMETER, 0};
RecordingPluginState g_pre_group_first = {
    "test.pre_perimeter.group.first",
    STEP_PRE_PERIMETER,
    -5,
    "test.pre_perimeter.exclusive_group",
    "Test exclusive pre-perimeter group",
    "Choose one test pre-perimeter plugin."
};
RecordingPluginState g_pre_group_second = {
    "test.pre_perimeter.group.second",
    STEP_PRE_PERIMETER,
    5,
    "test.pre_perimeter.exclusive_group",
    "Second label should not win",
    "Second tooltip should not win."
};
RecordingPluginState g_post_first = {"test.post_perimeter.first", STEP_POST_PERIMETER, -10};
RecordingPluginState g_post_second = {"test.post_perimeter.second", STEP_POST_PERIMETER, 20};
RecordingPluginState g_post_inactive = {"test.post_perimeter.inactive", STEP_POST_PERIMETER, 0};

RecordingPluginState *const g_recording_plugins[] = {
    &g_pre_first,
    &g_pre_second,
    &g_pre_inactive,
    &g_pre_group_first,
    &g_pre_group_second,
    &g_post_first,
    &g_post_second,
    &g_post_inactive
};

const_strings_t recording_get_dependencies(void *)
{
    const_strings_t out = {};
    return out;
}

const char *recording_get_id(void *plugin_ctx)
{
    return static_cast<RecordingPluginState *>(plugin_ctx)->id;
}

const char *recording_get_name(void *plugin_ctx)
{
    return static_cast<RecordingPluginState *>(plugin_ctx)->id;
}

const char *recording_get_description(void *)
{
    return "";
}

const char *recording_get_exclusive_group(void *plugin_ctx)
{
    return static_cast<RecordingPluginState *>(plugin_ctx)->exclusive_group;
}

const char *recording_get_exclusive_group_label(void *plugin_ctx)
{
    return static_cast<RecordingPluginState *>(plugin_ctx)->exclusive_group_label;
}

const char *recording_get_exclusive_group_tooltip(void *plugin_ctx)
{
    return static_cast<RecordingPluginState *>(plugin_ctx)->exclusive_group_tooltip;
}

slicing_step_t recording_get_step(void *plugin_ctx)
{
    return static_cast<RecordingPluginState *>(plugin_ctx)->step;
}

int32_t recording_get_priority(void *plugin_ctx)
{
    return static_cast<RecordingPluginState *>(plugin_ctx)->priority;
}

int32_t recording_used_config_keys(void *, const char **)
{
    return 0;
}

int32_t recording_defined_config_keys(void *, const char **)
{
    return 0;
}

void recording_initialize(void *, storage_handle *) {}

void fill_payload_event(const plugin_run_context *run_ctx, RecordedEvent &event)
{
    if (run_ctx == nullptr)
        return;

    event.context_step = run_ctx->step;
    const plugin_host_context *host_context = static_cast<const plugin_host_context *>(run_ctx->host_context);
    if (host_context != nullptr) {
        event.object_idx = host_context->object_idx;
        event.object_count = host_context->object_count;
    }

    if (run_ctx->step == STEP_PRE_PERIMETER) {
        const run_ctx_prepare_for_perimeters *payload = plugin_ctx_as_prepare_for_perimeters(run_ctx);
        if (payload != nullptr) {
            event.print = payload->print;
            event.object = payload->object;
        }
    } else if (run_ctx->step == STEP_POST_PERIMETER) {
        const run_ctx_post_perimeter_generation *payload = plugin_ctx_as_post_perimeter_generation(run_ctx);
        if (payload != nullptr) {
            event.print = payload->print;
            event.object = payload->object;
        }
    }
}

void record_event(RecordingPluginState &state, RecordedEvent event)
{
    if (state.events == nullptr || state.mutex == nullptr)
        return;

    event.plugin_id = state.id;
    std::lock_guard<std::mutex> lock(*state.mutex);
    state.events->push_back(std::move(event));
}

void recording_setup(void *plugin_ctx, const plugin_run_context *run_ctx, uint32_t run_count)
{
    RecordingPluginState &state = *static_cast<RecordingPluginState *>(plugin_ctx);
    RecordedEvent event;
    event.callback = "setup";
    event.run_count = run_count;
    fill_payload_event(run_ctx, event);
    record_event(state, std::move(event));
}

void recording_setup_run(void *plugin_ctx, const plugin_run_context *run_ctx)
{
    RecordingPluginState &state = *static_cast<RecordingPluginState *>(plugin_ctx);
    RecordedEvent event;
    event.callback = "setup_run";
    fill_payload_event(run_ctx, event);
    record_event(state, std::move(event));
}

void recording_run(void *plugin_ctx, const plugin_run_context *run_ctx)
{
    RecordingPluginState &state = *static_cast<RecordingPluginState *>(plugin_ctx);
    RecordedEvent event;
    event.callback = "run";
    fill_payload_event(run_ctx, event);
    record_event(state, std::move(event));
}

const plugin_vtable *recording_vtable()
{
    static const plugin_vtable vt = {
        SLIC3R_PLUGIN_ABI_VERSION,
        &recording_get_id,
        &recording_get_name,
        &recording_get_description,
        &recording_get_exclusive_group,
        &recording_get_exclusive_group_label,
        &recording_get_exclusive_group_tooltip,
        &recording_get_step,
        &recording_get_dependencies,
        &recording_get_priority,
        &recording_used_config_keys,
        &recording_defined_config_keys,
        &recording_initialize,
        &recording_setup,
        &recording_setup_run,
        &recording_run
    };
    return &vt;
}

void register_recording_plugins()
{
    Orchestrator &orchestrator = Orchestrator::instance();
    for (RecordingPluginState *state : g_recording_plugins) {
        if (orchestrator.get_plugin(state->id) != nullptr)
            continue;

        plugin_instance instance = {};
        instance.ctx = state;
        instance.vt = recording_vtable();
        REQUIRE(orchestrator.register_plugin(instance));
    }
}

class ScopedActivePlugins
{
public:
    explicit ScopedActivePlugins(std::initializer_list<const char *> plugin_ids)
        : m_orchestrator(Orchestrator::instance())
    {
        m_previous_active_plugins.reserve(m_orchestrator.active_plugins().size());
        for (Plugin *plugin : m_orchestrator.active_plugins())
            m_previous_active_plugins.push_back(plugin);

        m_orchestrator.clear_active_plugins();
        for (const char *plugin_id : plugin_ids)
            REQUIRE(m_orchestrator.set_plugin_active(plugin_id, true));
    }

    ~ScopedActivePlugins()
    {
        m_orchestrator.clear_active_plugins();
        for (Plugin *plugin : m_previous_active_plugins)
            m_orchestrator.set_plugin_active(plugin, true);
    }

private:
    Orchestrator &m_orchestrator;
    std::vector<Plugin *> m_previous_active_plugins;
};

class ScopedRecordingEvents
{
public:
    explicit ScopedRecordingEvents(std::vector<RecordedEvent> &events)
    {
        for (RecordingPluginState *state : g_recording_plugins) {
            state->events = &events;
            state->mutex = &m_mutex;
        }
    }

    ~ScopedRecordingEvents()
    {
        for (RecordingPluginState *state : g_recording_plugins) {
            state->events = nullptr;
            state->mutex = nullptr;
        }
    }

private:
    std::mutex m_mutex;
};

using StepRunFn = void (*)(Orchestrator &, Print &);

std::vector<std::string> plugin_ids_for_callback(const std::vector<RecordedEvent> &events,
                                                 const char *callback)
{
    std::vector<std::string> out;
    for (const RecordedEvent &event : events)
        if (event.callback == callback)
            out.push_back(event.plugin_id);
    return out;
}

void require_no_event_for_plugin(const std::vector<RecordedEvent> &events, const char *plugin_id)
{
    for (const RecordedEvent &event : events)
        CHECK(event.plugin_id != plugin_id);
}

void require_setup_counts(const std::vector<RecordedEvent> &events, uint32_t expected_run_count)
{
    size_t setup_count = 0;
    for (const RecordedEvent &event : events)
        if (event.callback == "setup") {
            ++setup_count;
            CHECK(event.run_count == expected_run_count);
        }
    CHECK(setup_count == 2);
}

void require_object_payloads(const std::vector<RecordedEvent> &events,
                             const Print &print,
                             slicing_step_t step)
{
    const print_handle *expected_print = reinterpret_cast<const print_handle *>(&print);
    const object_handle *objects[] = {
        reinterpret_cast<const object_handle *>(&print.object(0)),
        reinterpret_cast<const object_handle *>(&print.object(1))
    };
    std::array<size_t, 2> setup_run_seen = {{0, 0}};
    std::array<size_t, 2> run_seen = {{0, 0}};

    for (const RecordedEvent &event : events) {
        if (event.callback != "setup_run" && event.callback != "run")
            continue;

        CHECK(event.context_step == step);
        CHECK(event.print == expected_print);
        CHECK(event.object_count == 2);

        size_t object_idx = size_t(-1);
        if (event.object == objects[0])
            object_idx = 0;
        else if (event.object == objects[1])
            object_idx = 1;
        INFO("plugin " << event.plugin_id << ", callback " << event.callback);
        REQUIRE(object_idx < 2);
        CHECK(event.object_idx == object_idx);

        if (event.callback == "setup_run")
            ++setup_run_seen[object_idx];
        else
            ++run_seen[object_idx];
    }

    CHECK(setup_run_seen[0] == 2);
    CHECK(setup_run_seen[1] == 2);
    CHECK(run_seen[0] == 2);
    CHECK(run_seen[1] == 2);
}

const Steps::StepExclusivePluginGroup *find_exclusive_group(const std::vector<Steps::StepExclusivePluginGroup> &groups,
                                                            const char *group_id)
{
    for (const Steps::StepExclusivePluginGroup &group : groups)
        if (group.group.group_id == group_id)
            return &group;
    return nullptr;
}

void run_and_check_object_step(slicing_step_t step,
                               const char *first_plugin_id,
                               const char *second_plugin_id,
                               const char *inactive_plugin_id,
                               StepRunFn run_step)
{
    PreparedPerimeterPrint prepared;
    const DynamicPrintConfig config = perimeter_config({});
    Slic3r::Test::init_print({Slic3r::Test::TestMesh::cube_20x20x20,
                              Slic3r::Test::TestMesh::cube_20x20x20},
                             prepared.print,
                             prepared.model,
                             config);
    REQUIRE(prepared.print.objects().size() == 2);

    std::vector<RecordedEvent> events;
    ScopedRecordingEvents event_scope(events);
    ScopedActivePlugins active_scope({second_plugin_id, first_plugin_id});

    run_step(Orchestrator::instance(), prepared.print);

    require_no_event_for_plugin(events, inactive_plugin_id);
    require_setup_counts(events, 2);

    const std::vector<std::string> setup_ids = plugin_ids_for_callback(events, "setup");
    REQUIRE(setup_ids.size() == 2);
    CHECK(setup_ids[0] == first_plugin_id);
    CHECK(setup_ids[1] == second_plugin_id);

    const std::vector<std::string> setup_run_ids = plugin_ids_for_callback(events, "setup_run");
    REQUIRE(setup_run_ids.size() == 4);
    CHECK(setup_run_ids[0] == first_plugin_id);
    CHECK(setup_run_ids[1] == first_plugin_id);
    CHECK(setup_run_ids[2] == second_plugin_id);
    CHECK(setup_run_ids[3] == second_plugin_id);

    const std::vector<std::string> run_ids = plugin_ids_for_callback(events, "run");
    REQUIRE(run_ids.size() == 4);
    CHECK(run_ids[0] == first_plugin_id);
    CHECK(run_ids[1] == first_plugin_id);
    CHECK(run_ids[2] == second_plugin_id);
    CHECK(run_ids[3] == second_plugin_id);

    require_object_payloads(events, prepared.print, step);
}

} // namespace

// "Empty perimeter boundary steps" are the pre/post perimeter extension points.
// They currently do not transform perimeter geometry themselves; their host-side
// job is to run object-level plugins with the right lifecycle and C payload.
//
// This test therefore uses tiny recording plugins instead of real perimeter
// algorithms. It proves the step runner filters inactive plugins, sorts active
// plugins by priority, calls setup/setup_run/run at the expected granularity,
// and passes print/object handles plus object indexes that match the Print.
// Geometry regressions are covered by the generator/module tests, not here.
TEST_CASE("Empty perimeter boundary steps run object plugins", "[plugins][perimeter][steps]")
{
    Slic3r::Test::Plugins::ensure_plugin_test_runtime_initialized();
    register_recording_plugins();

    SECTION("pre-perimeter step")
    {
        run_and_check_object_step(STEP_PRE_PERIMETER,
                                  g_pre_first.id,
                                  g_pre_second.id,
                                  g_pre_inactive.id,
                                  &Steps::StepPrepareForPeriemters::run_step);
    }

    SECTION("post-perimeter step")
    {
        run_and_check_object_step(STEP_POST_PERIMETER,
                                  g_post_first.id,
                                  g_post_second.id,
                                  g_post_inactive.id,
                                  &Steps::StepPostPerimeterGeneration::run_step);
    }
}

TEST_CASE("Explicit exclusive groups select one object-step plugin", "[plugins][perimeter][steps]")
{
    Slic3r::Test::Plugins::ensure_plugin_test_runtime_initialized();
    register_recording_plugins();

    PreparedPerimeterPrint prepared;
    const DynamicPrintConfig config = perimeter_config({});
    Slic3r::Test::init_print({Slic3r::Test::TestMesh::cube_20x20x20,
                              Slic3r::Test::TestMesh::cube_20x20x20},
                             prepared.print,
                             prepared.model,
                             config);

    std::vector<RecordedEvent> events;
    ScopedRecordingEvents event_scope(events);
    ScopedActivePlugins active_scope({
        g_pre_first.id,
        g_pre_group_first.id,
        g_pre_group_second.id
    });

    // The two grouped plugins are alternatives. The group text used by the GUI
    // selector comes from the first active plugin in execution order, while the
    // additive pre-perimeter plugin stays outside the selector.
    const std::vector<Steps::StepExclusivePluginGroup> groups =
        Steps::active_exclusive_plugin_groups(Orchestrator::instance());
    const Steps::StepExclusivePluginGroup *group =
        find_exclusive_group(groups, g_pre_group_first.exclusive_group);
    REQUIRE(group != nullptr);
    REQUIRE(group->plugins.size() == 2);
    CHECK(group->plugins[0]->get_id() == g_pre_group_first.id);
    CHECK(group->plugins[1]->get_id() == g_pre_group_second.id);
    CHECK(group->group.label_storage == g_pre_group_first.exclusive_group_label);
    CHECK(group->group.tooltip_storage == g_pre_group_first.exclusive_group_tooltip);

    // No selector option is injected into this synthetic test config, so the
    // runtime falls back to the first plugin in the exclusive group. The normal
    // additive plugin still runs alongside it.
    Steps::StepPrepareForPeriemters::run_step(Orchestrator::instance(), prepared.print);

    require_no_event_for_plugin(events, g_pre_group_second.id);
    require_setup_counts(events, 2);

    const std::vector<std::string> setup_ids = plugin_ids_for_callback(events, "setup");
    REQUIRE(setup_ids.size() == 2);
    CHECK(setup_ids[0] == g_pre_first.id);
    CHECK(setup_ids[1] == g_pre_group_first.id);

    require_object_payloads(events, prepared.print, STEP_PRE_PERIMETER);
}
