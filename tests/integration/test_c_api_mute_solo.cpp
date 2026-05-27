/*
 * C API integration tests for omega_sink_set_mute / omega_sink_set_solo (issue #31).
 *
 * Null-guard and parameter-validation tests are pure C API; functional tests
 * that require deterministic timing are in tests/unit/test_mute_solo.cpp.
 */

#include <omega/omega.h>
#include <omega/test/capturing_sink.h>

#include <catch2/catch_test_macros.hpp>
#include <thread>

// Cast CapturingSink (C++) to the opaque omega_sink_t* accepted by the C API.
static omega_sink_t* as_sink(omega::CapturingSink& s)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<omega_sink_t*>(&s);
}

// ── omega_sink_set_mute ───────────────────────────────────────────────────────

TEST_CASE("C API: omega_sink_set_mute null engine returns OMEGA_ERR_INVALID")
{
    REQUIRE(omega_sink_set_mute(nullptr, 1u, 0u, 1) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: omega_sink_set_mute invalid channel returns OMEGA_ERR_INVALID")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    REQUIRE(omega_sink_set_mute(e, 1u, 16u, 1) == OMEGA_ERR_INVALID);
    REQUIRE(omega_sink_set_mute(e, 1u, 0xFEu, 1) == OMEGA_ERR_INVALID);
    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_sink_set_mute valid channels return OMEGA_OK")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    REQUIRE(omega_sink_set_mute(e, 1u, 0u, 1) == OMEGA_OK);
    REQUIRE(omega_sink_set_mute(e, 1u, 15u, 0) == OMEGA_OK);
    REQUIRE(omega_sink_set_mute(e, 1u, 0xFFu, 1) == OMEGA_OK);
    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_sink_set_mute round-trip mute/unmute returns OMEGA_OK")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    REQUIRE(omega_sink_set_mute(e, 1u, 3u, 1) == OMEGA_OK);
    REQUIRE(omega_sink_set_mute(e, 1u, 3u, 0) == OMEGA_OK);
    omega_engine_destroy(e);
}

// ── omega_sink_set_solo ───────────────────────────────────────────────────────

TEST_CASE("C API: omega_sink_set_solo null engine returns OMEGA_ERR_INVALID")
{
    REQUIRE(omega_sink_set_solo(nullptr, 1u, 0u, 1) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: omega_sink_set_solo invalid channel returns OMEGA_ERR_INVALID")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    REQUIRE(omega_sink_set_solo(e, 1u, 16u, 1) == OMEGA_ERR_INVALID);
    REQUIRE(omega_sink_set_solo(e, 1u, 0xFEu, 1) == OMEGA_ERR_INVALID);
    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_sink_set_solo valid channels return OMEGA_OK")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    REQUIRE(omega_sink_set_solo(e, 1u, 0u, 1) == OMEGA_OK);
    REQUIRE(omega_sink_set_solo(e, 1u, 15u, 0) == OMEGA_OK);
    REQUIRE(omega_sink_set_solo(e, 1u, 0xFFu, 1) == OMEGA_OK);
    omega_engine_destroy(e);
}

// ── Functional: mute suppresses events ───────────────────────────────────────

TEST_CASE("C API: muting channel 0 suppresses note-on events on that channel")
{
    omega::CapturingSink sink;
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    REQUIRE(omega_engine_add_sink(e, as_sink(sink)) == OMEGA_OK);
    uint32_t sid = sink.sink_id();

    // Build a track with a note on channel 0.
    omega_track_id_t track{};
    REQUIRE(omega_engine_add_track(e, "t", &track) == OMEGA_OK);
    REQUIRE(omega_engine_set_track_sink(e, track, sid) == OMEGA_OK);
    omega_event_t ev = omega_make_note_on(0u, sid, 0, 60, 100, 0u);
    REQUIRE(omega_engine_add_event(e, track, ev) == OMEGA_OK);

    // Mute channel 0 before playback.
    REQUIRE(omega_sink_set_mute(e, sid, 0u, 1) == OMEGA_OK);
    REQUIRE(omega_engine_play(e) == OMEGA_OK);

    // Give the engine time to fire the note.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    omega_engine_process(e);

    // Note should be suppressed.
    REQUIRE_FALSE(sink.has_note_on(60u));

    omega_engine_stop(e);
    omega_engine_destroy(e);
}

TEST_CASE("C API: solo channel 0 suppresses note-on on non-soloed channel")
{
    omega::CapturingSink sink;
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    REQUIRE(omega_engine_add_sink(e, as_sink(sink)) == OMEGA_OK);
    uint32_t sid = sink.sink_id();

    omega_track_id_t track{};
    REQUIRE(omega_engine_add_track(e, "t", &track) == OMEGA_OK);
    REQUIRE(omega_engine_set_track_sink(e, track, sid) == OMEGA_OK);

    // Add two notes: C4 on channel 0, E4 on channel 1.
    omega_event_t ev0 = omega_make_note_on(0u, sid, 0, 60, 100, 0u);
    omega_event_t ev1 = omega_make_note_on(0u, sid, 1, 64, 100, 0u);
    // Note: timeline events don't support per-event channel routing; use
    // a channel field — this tests the mute/solo filtering only.
    // We set the channel on the event directly.
    REQUIRE(omega_engine_add_event(e, track, ev0) == OMEGA_OK);
    REQUIRE(omega_engine_add_event(e, track, ev1) == OMEGA_OK);

    // Solo ch 0 — ch 1 should be suppressed.
    REQUIRE(omega_sink_set_solo(e, sid, 0u, 1) == OMEGA_OK);
    REQUIRE(omega_engine_play(e) == OMEGA_OK);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    omega_engine_process(e);

    REQUIRE(sink.has_note_on(60u));            // ch 0 — soloed, passes
    REQUIRE_FALSE(sink.has_note_on(64u, 1u));  // ch 1 — not soloed, suppressed

    omega_engine_stop(e);
    omega_engine_destroy(e);
}
