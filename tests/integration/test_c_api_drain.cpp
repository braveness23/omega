#include <omega/omega.h>

#include <catch2/catch_test_macros.hpp>

// ── DrainSink C API ───────────────────────────────────────────────────────────

TEST_CASE("C API: omega_drain_sink_create registers with engine")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    omega_drain_sink_t* ds = omega_drain_sink_create(eng);
    REQUIRE(ds != nullptr);

    // Destroy engine first (sink holds a raw reference in engine)
    omega_engine_destroy(eng);
    omega_drain_sink_destroy(nullptr, ds);
}

TEST_CASE("C API: omega_drain_pop receives events from engine")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    omega_drain_sink_t* ds = omega_drain_sink_create(eng);
    REQUIRE(ds != nullptr);

    // Get the sink_id so we can route events to it.
    // omega_drain_sink_t wraps DrainSink which is an OutputSink — use omega_sink_id.
    // The drain sink's OutputSink is the first field so the cast is layout-compatible.
    // Instead, we introspect via omega_drain_size which confirms events arrive.
    // For routing, use a custom source in C style.
    uint32_t sid = 0u;
    {
        // Cast: omega_drain_sink_t* → omega_sink_t* for omega_sink_id()
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        sid = omega_sink_id(reinterpret_cast<omega_sink_t*>(ds));
    }
    REQUIRE(sid != 0u);

    // Wire a custom source that dispatches a note on tick=0
    struct Ctx
    {
        uint32_t sid;
    } ctx{sid};

    omega_source_desc_t desc{};
    desc.advance_fn =
        [](uint64_t to_tick, omega_dispatcher_t* d, omega_process_context_t*, void* ud) {
            auto* c = static_cast<Ctx*>(ud);
            omega_event_t ev = omega_make_note_on(to_tick, c->sid, 0, 55, 100, 0);
            omega_dispatch(d, &ev);
        };
    desc.userdata = &ctx;
    desc.priority = OMEGA_SOURCE_PRIORITY_PLAYBACK;

    omega_source_t* src = omega_source_create(&desc);
    REQUIRE(omega_engine_add_source(eng, src) == OMEGA_OK);
    REQUIRE(omega_engine_play(eng) == OMEGA_OK);
    omega_engine_process(eng);  // tick=0; source dispatches note

    REQUIRE(omega_drain_size(ds) == 1u);

    omega_event_t out{};
    REQUIRE(omega_drain_pop(ds, &out) == 1);
    REQUIRE(out.payload_tag == OMEGA_NOTE_ON);
    REQUIRE(out.data[0] == 55u);
    REQUIRE(omega_drain_size(ds) == 0u);

    omega_source_destroy(src);
    omega_engine_destroy(eng);
    omega_drain_sink_destroy(nullptr, ds);
}

TEST_CASE("C API: omega_drain_pop returns 0 when empty")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    omega_drain_sink_t* ds = omega_drain_sink_create(eng);
    REQUIRE(ds != nullptr);

    omega_event_t out{};
    REQUIRE(omega_drain_pop(ds, &out) == 0);

    omega_engine_destroy(eng);
    omega_drain_sink_destroy(nullptr, ds);
}

TEST_CASE("C API: omega_drain null guards")
{
    REQUIRE(omega_drain_sink_create(nullptr) == nullptr);
    REQUIRE(omega_drain_pop(nullptr, nullptr) == 0);
    REQUIRE(omega_drain_size(nullptr) == 0u);
    REQUIRE(omega_drain_dropped(nullptr) == 0u);
    omega_drain_sink_destroy(nullptr, nullptr);  // must not crash
    REQUIRE(true);
}
