#include <omega/omega.h>
#include <omega/test/capturing_sink.h>

#include <catch2/catch_test_macros.hpp>

// Cast CapturingSink (C++) to the opaque omega_sink_t* accepted by the C API.
// Safe because omega_engine_add_sink() reinterpret_casts back to OutputSink*.
static omega_sink_t* as_sink_handle(omega::CapturingSink& s)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<omega_sink_t*>(&s);
}

TEST_CASE("C API: engine lifecycle creates and destroys cleanly")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    omega_engine_destroy(e);
}

TEST_CASE("C API: null handle safety — all functions tolerate NULL")
{
    omega_track_id_t tid{};
    REQUIRE(omega_engine_add_sink(nullptr, nullptr) == OMEGA_ERR_INVALID);
    REQUIRE(omega_engine_add_track(nullptr, "t", &tid) == OMEGA_ERR_INVALID);
    REQUIRE(omega_engine_set_track_sink(nullptr, 0, 0) == OMEGA_ERR_INVALID);
    REQUIRE(omega_engine_add_event(nullptr, 0, omega_make_note_on(0u, 0u, 0, 60, 100, 0)) ==
            OMEGA_ERR_INVALID);
    REQUIRE(omega_engine_play(nullptr) == OMEGA_ERR_INVALID);
    REQUIRE(omega_engine_stop(nullptr) == OMEGA_ERR_INVALID);
    omega_engine_process(nullptr);  // must not crash
    REQUIRE(omega_engine_transport_state(nullptr) == OMEGA_TRANSPORT_STOPPED);
    REQUIRE(omega_engine_position_ns(nullptr) == 0u);
}

TEST_CASE("C API: note-on at tick 0 fires on first process call")
{
    omega::CapturingSink sink;

    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    REQUIRE(omega_engine_add_sink(e, as_sink_handle(sink)) == OMEGA_OK);

    omega_track_id_t track{};
    REQUIRE(omega_engine_add_track(e, "main", &track) == OMEGA_OK);
    REQUIRE(omega_engine_set_track_sink(e, track, sink.sink_id()) == OMEGA_OK);

    // Schedule note-on at tick 0 (no duration — pure note-on without auto note-off)
    omega_event_t ev = omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 0);
    REQUIRE(omega_engine_add_event(e, track, ev) == OMEGA_OK);
    REQUIRE(omega_engine_play(e) == OMEGA_OK);

    // First process: drains queue (applies add-event + play), dispatches tick-0 event.
    omega_engine_process(e);

    REQUIRE(sink.has_note_on(60, 0));
    REQUIRE(omega_engine_transport_state(e) == OMEGA_TRANSPORT_PLAYING);

    REQUIRE(omega_engine_stop(e) == OMEGA_OK);
    omega_engine_process(e);
    REQUIRE(omega_engine_transport_state(e) == OMEGA_TRANSPORT_STOPPED);

    omega_engine_destroy(e);
}

TEST_CASE("C API: transport state reflects play/stop cycle")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    REQUIRE(omega_engine_transport_state(e) == OMEGA_TRANSPORT_STOPPED);

    REQUIRE(omega_engine_play(e) == OMEGA_OK);
    omega_engine_process(e);
    REQUIRE(omega_engine_transport_state(e) == OMEGA_TRANSPORT_PLAYING);

    REQUIRE(omega_engine_stop(e) == OMEGA_OK);
    omega_engine_process(e);
    REQUIRE(omega_engine_transport_state(e) == OMEGA_TRANSPORT_STOPPED);

    omega_engine_destroy(e);
}
