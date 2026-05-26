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

// Recording plugins are tiny C-ABI plugins used only by this test file. They
// let us observe the generic object-step runner without depending on real
// perimeter behavior.
struct RecordingPluginState
{
    const char *id = nullptr;
    slicing_step_t step = STEP_NONE;
    int32_t priority = 0;
    std::vector<RecordedEvent> *events = nullptr;
    std::mutex *mutex = nullptr;
};

// Two active plugins plus one inactive plugin per step. Active plugins are
// intentionally given different priorities so the test can prove orchestrator
// ordering is used instead of activation order.
RecordingPluginState g_pre_first  = {"test.pre_perimeter.first", STEP_PRE_PERIMETER, -10, nullptr, nullptr};
RecordingPluginState g_pre_second = {"test.pre_perimeter.second", STEP_PRE_PERIMETER, 20, nullptr, nullptr};
RecordingPluginState g_pre_inactive = {"test.pre_perimeter.inactive", STEP_PRE_PERIMETER, 0, nullptr, nullptr};
RecordingPluginState g_post_first = {"test.post_perimeter.first", STEP_POST_PERIMETER, -10, nullptr, nullptr};
RecordingPluginState g_post_second = {"test.post_perimeter.second", STEP_POST_PERIMETER, 20, nullptr, nullptr};
RecordingPluginState g_post_inactive = {"test.post_perimeter.inactive", STEP_POST_PERIMETER, 0, nullptr, nullptr};

RecordingPluginState *const g_recording_plugins[] = {
    &g_pre_first,
    &g_pre_second,
    &g_pre_inactive,
    &g_post_first,
    &g_post_second,
    &g_post_inactive
};

const_strings_t recording_get_dependencies(void *)
{
    const_strings_t out = {};
    return out;
}

// The following callbacks implement just enough of the plugin ABI to record
// setup/setup_run/run calls and their payloads.
const char *recording_get_id(void *plugin_ctx)
{
    return static_cast<RecordingPluginState *>(plugin_ctx)->id;
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
    // Boundary-step test plugins do not create settings.
    return 0;
}

void recording_initialize(void *, storage_handle *) {}

// Decode the step-specific payload into a common RecordedEvent. This verifies
// both pre and post perimeter steps pass the expected C payload type.
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

// setup_run() and run() may be called from worker threads, so all event writes
// go through the mutex installed by ScopedRecordingEvents.
void record_event(RecordingPluginState &state, RecordedEvent event)
{
    if (state.events == nullptr || state.mutex == nullptr)
        return;

    event.plugin_id = state.id;
    std::lock_guard<std::mutex> lock(*state.mutex);
    state.events->push_back(std::move(event));
}

// setup() is called once per plugin with the number of object runs to expect.
void recording_setup(void *plugin_ctx, const plugin_run_context *run_ctx, uint32_t run_count)
{
    RecordingPluginState &state = *static_cast<RecordingPluginState *>(plugin_ctx);
    RecordedEvent event;
    event.callback = "setup";
    event.run_count = run_count;
    fill_payload_event(run_ctx, event);
    record_event(state, std::move(event));
}

// setup_run() is called once per object before run() begins for this plugin.
void recording_setup_run(void *plugin_ctx, const plugin_run_context *run_ctx)
{
    RecordingPluginState &state = *static_cast<RecordingPluginState *>(plugin_ctx);
    RecordedEvent event;
    event.callback = "setup_run";
    fill_payload_event(run_ctx, event);
    record_event(state, std::move(event));
}

// run() is the actual object-level plugin callback.
void recording_run(void *plugin_ctx, const plugin_run_context *run_ctx)
{
    RecordingPluginState &state = *static_cast<RecordingPluginState *>(plugin_ctx);
    RecordedEvent event;
    event.callback = "run";
    fill_payload_event(run_ctx, event);
    record_event(state, std::move(event));
}

// All recording plugins share one vtable; RecordingPluginState provides the
// per-plugin id, step and priority.
const plugin_vtable *recording_vtable()
{
    static const plugin_vtable vt = {
        SLIC3R_PLUGIN_ABI_VERSION,
        &recording_get_id,
        &recording_get_step,
        &recording_get_dependencies,
        &recording_get_priority,
        &recording_used_config_keys,
        &recording_initialize,
        &recording_setup,
        &recording_setup_run,
        &recording_run
    };
    return &vt;
}

// Register once into the process-wide orchestrator. Re-registering would make
// tests order-dependent, so existing ids are skipped.
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

// Temporarily replace the active plugin set for one section and restore it when
// the section exits.
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

// Connect all recording plugin states to the event vector used by one test run.
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

// Extract callback order as plugin ids. The test uses this for readable checks
// on setup/setup_run/run priority order.
std::vector<std::string> plugin_ids_for_callback(const std::vector<RecordedEvent> &events,
                                                 const char *callback)
{
    std::vector<std::string> out;
    for (const RecordedEvent &event : events)
        if (event.callback == callback)
            out.push_back(event.plugin_id);
    return out;
}

// A registered but inactive plugin must receive no callback at all.
void require_no_event_for_plugin(const std::vector<RecordedEvent> &events, const char *plugin_id)
{
    for (const RecordedEvent &event : events)
        CHECK(event.plugin_id != plugin_id);
}

// Each active plugin gets exactly one setup() call and the setup run count must
// match the two PrintObjects in this fixture.
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

// setup_run() and run() should each receive one payload per object. The host
// context index and payload object handle must describe the same PrintObject.
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

// Shared test body for the two empty boundary steps. The active list is passed
// in reverse priority order to prove priority sorting is applied by the
// orchestrator before StepRunner executes plugins.
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
