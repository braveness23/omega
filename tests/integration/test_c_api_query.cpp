#include <omega/omega.h>
#include <omega/test/capturing_sink.h>

#include <catch2/catch_test_macros.hpp>

static omega_sink_t* as_sink(omega::CapturingSink& s)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<omega_sink_t*>(&s);
}

// ── omega_perf_slot_state ────────────────────────────────────────────────────

TEST_CASE("C API: omega_perf_slot_state null engine returns OMEGA_SLOT_EMPTY")
{
    REQUIRE(omega_perf_slot_state(nullptr, 0u) == OMEGA_SLOT_EMPTY);
}

TEST_CASE("C API: omega_perf_slot_state returns OMEGA_SLOT_IDLE after assign")
{
    omega_engine_t* e = omega_engine_create();
    omega_pattern_id_t pat = omega_pattern_create(e, "p", 480u);
    omega_perf_assign(e, 0u, pat);
    omega_engine_process(e);

    REQUIRE(omega_perf_slot_state(e, 0u) == OMEGA_SLOT_IDLE);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_perf_slot_state returns OMEGA_SLOT_PLAYING after immediate cue")
{
    omega_engine_t* e = omega_engine_create();
    omega::CapturingSink sink;
    omega_engine_add_sink(e, as_sink(sink));

    omega_pattern_id_t pat = omega_pattern_create(e, "p", 480u);
    omega_event_t ev = omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 480u);
    omega_pattern_add_event(e, pat, &ev);
    omega_perf_assign(e, 0u, pat);
    omega_perf_cue(e, 0u, OMEGA_CUE_IMMEDIATE);
    omega_engine_play(e);
    omega_engine_process(e);

    REQUIRE(omega_perf_slot_state(e, 0u) == OMEGA_SLOT_PLAYING);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_perf_slot_state out of range returns OMEGA_SLOT_EMPTY")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(omega_perf_slot_state(e, 64u) == OMEGA_SLOT_EMPTY);
    omega_engine_destroy(e);
}

// ── omega_sink_is_muted / omega_sink_is_soloed ───────────────────────────────

TEST_CASE("C API: omega_sink_is_muted null engine returns 0")
{
    REQUIRE(omega_sink_is_muted(nullptr, 1u, 0u) == 0);
}

TEST_CASE("C API: omega_sink_is_muted returns 1 after mute + process")
{
    omega_engine_t* e = omega_engine_create();
    omega::CapturingSink sink;
    uint32_t sid = sink.sink_id();
    omega_engine_add_sink(e, as_sink(sink));

    omega_sink_set_mute(e, sid, 0u, 1);
    omega_engine_process(e);
    REQUIRE(omega_sink_is_muted(e, sid, 0u) == 1);
    REQUIRE(omega_sink_is_muted(e, sid, 1u) == 0);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_sink_is_soloed null engine returns 0")
{
    REQUIRE(omega_sink_is_soloed(nullptr, 1u, 0u) == 0);
}

TEST_CASE("C API: omega_sink_is_soloed returns 1 after solo + process")
{
    omega_engine_t* e = omega_engine_create();
    omega::CapturingSink sink;
    uint32_t sid = sink.sink_id();
    omega_engine_add_sink(e, as_sink(sink));

    omega_sink_set_solo(e, sid, 0u, 1);
    omega_engine_process(e);
    REQUIRE(omega_sink_is_soloed(e, sid, 0u) == 1);
    REQUIRE(omega_sink_is_soloed(e, sid, 1u) == 0);

    omega_engine_destroy(e);
}
