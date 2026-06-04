#include <omega/omega.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_event_input.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

// Cast helpers — integration tests may use C++ objects through C API handles.
static omega_sink_t* as_sink(omega::CapturingSink& s)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<omega_sink_t*>(&s);
}
static omega_input_t* as_input(omega::MockEventInput& i)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<omega_input_t*>(&i);
}

// omega_event_t and omega::Event share an ABI-identical layout; bridge the C
// event into the C++ Event that MockEventInput::prime() expects.
static const omega::Event& as_event(const omega_event_t& ev)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return *reinterpret_cast<const omega::Event*>(&ev);
}

// The C API has omega_make_note_on but no omega_make_note_off; construct manually.
static omega_event_t make_note_off(
    uint64_t tick, uint32_t sink_id, uint8_t ch, uint8_t note, uint8_t vel)
{
    omega_event_t ev{};
    ev.tick = tick;
    ev.sink_id = sink_id;
    ev.payload_tag = OMEGA_NOTE_OFF;
    ev.channel = ch;
    ev.data[0] = note;
    ev.data[1] = vel;
    return ev;
}

// ── NULL / invalid guards ─────────────────────────────────────────────────────

TEST_CASE("omega_recorder_create: null engine returns null")
{
    REQUIRE(omega_recorder_create(nullptr, 0u) == nullptr);
}

TEST_CASE("omega_recorder_destroy: null rec is a no-op")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    omega_recorder_destroy(e, nullptr);  // must not crash
    omega_engine_destroy(e);
}

TEST_CASE("omega_recorder_start: null rec returns OMEGA_ERR_INVALID")
{
    REQUIRE(omega_recorder_start(nullptr, 0u, 0xFFu) == OMEGA_ERR_INVALID);
}

TEST_CASE("omega_recorder_stop: null rec returns 0")
{
    REQUIRE(omega_recorder_stop(nullptr) == 0u);
}

TEST_CASE("omega_recorder_is_recording: null rec returns 0")
{
    REQUIRE(omega_recorder_is_recording(nullptr) == 0);
}

// ── Lifecycle: create + is_recording + destroy ────────────────────────────────

TEST_CASE("omega_recorder: created recorder starts disarmed")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega::CapturingSink sink;
    REQUIRE(omega_engine_add_sink(e, as_sink(sink)) == OMEGA_OK);

    omega_recorder_t* rec = omega_recorder_create(e, omega_sink_id(as_sink(sink)));
    REQUIRE(rec != nullptr);
    REQUIRE(omega_recorder_is_recording(rec) == 0);

    omega_recorder_destroy(e, rec);
    omega_engine_destroy(e);
}

TEST_CASE("omega_recorder: start arms, stop disarms")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega::CapturingSink sink;
    REQUIRE(omega_engine_add_sink(e, as_sink(sink)) == OMEGA_OK);

    omega_track_id_t track = 0u;
    REQUIRE(omega_engine_add_track(e, "rec", &track) == OMEGA_OK);

    omega_recorder_t* rec = omega_recorder_create(e, omega_sink_id(as_sink(sink)));
    REQUIRE(rec != nullptr);

    REQUIRE(omega_recorder_start(rec, track, 0xFFu) == OMEGA_OK);
    REQUIRE(omega_recorder_is_recording(rec) != 0);

    omega_recorder_stop(rec);
    REQUIRE(omega_recorder_is_recording(rec) == 0);

    omega_recorder_destroy(e, rec);
    omega_engine_destroy(e);
}

// ── Recording: NOTE_ON/NOTE_OFF pair produces one timeline event ──────────────
// omega_recorder_stop() returns the number of NOTE_ON events committed to the
// timeline track — that count is the primary correctness assertion here.

TEST_CASE("omega_recorder: note-on + note-off produces one NOTE_ON in timeline")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega::CapturingSink sink;
    REQUIRE(omega_engine_add_sink(e, as_sink(sink)) == OMEGA_OK);
    const uint32_t sid = omega_sink_id(as_sink(sink));

    omega::MockEventInput midi_in;
    REQUIRE(omega_engine_add_input(e, as_input(midi_in)) == OMEGA_OK);

    omega_track_id_t track = 0u;
    REQUIRE(omega_engine_add_track(e, "rec", &track) == OMEGA_OK);
    REQUIRE(omega_engine_set_track_sink(e, track, sid) == OMEGA_OK);

    omega_recorder_t* rec = omega_recorder_create(e, sid);
    REQUIRE(rec != nullptr);

    REQUIRE(omega_engine_play(e) == OMEGA_OK);
    REQUIRE(omega_recorder_start(rec, track, 0xFFu) == OMEGA_OK);

    omega_event_t on = omega_make_note_on(0u, sid, 0u, 60u, 100u, 0u);
    midi_in.prime(as_event(on));
    omega_engine_process(e);  // NOTE_ON primed into InputBus, tick advances

    omega_event_t off = make_note_off(0u, sid, 0u, 60u, 0u);
    midi_in.prime(as_event(off));
    omega_engine_process(e);  // NOTE_OFF resolves duration; NOTE_ON committed to track

    const size_t n = omega_recorder_stop(rec);
    REQUIRE(n == 1u);

    omega_engine_stop(e);
    omega_recorder_destroy(e, rec);
    omega_engine_destroy(e);
}

// ── Channel filter: events on the wrong channel are ignored ──────────────────

TEST_CASE("omega_recorder: channel filter excludes events on other channels")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega::CapturingSink sink;
    REQUIRE(omega_engine_add_sink(e, as_sink(sink)) == OMEGA_OK);
    const uint32_t sid = omega_sink_id(as_sink(sink));

    omega::MockEventInput midi_in;
    REQUIRE(omega_engine_add_input(e, as_input(midi_in)) == OMEGA_OK);

    omega_track_id_t track = 0u;
    REQUIRE(omega_engine_add_track(e, "filtered", &track) == OMEGA_OK);

    omega_recorder_t* rec = omega_recorder_create(e, sid);
    REQUIRE(rec != nullptr);

    REQUIRE(omega_engine_play(e) == OMEGA_OK);
    REQUIRE(omega_recorder_start(rec, track, 0u) == OMEGA_OK);  // channel 0 only

    // NOTE_ON on ch1 (filtered out) and ch0 (captured), then both NOTE_OFFs.
    omega_event_t on_ch1 = omega_make_note_on(0u, sid, 1u, 64u, 100u, 0u);
    omega_event_t on_ch0 = omega_make_note_on(0u, sid, 0u, 60u, 100u, 0u);
    midi_in.prime(as_event(on_ch1));
    midi_in.prime(as_event(on_ch0));
    omega_engine_process(e);

    omega_event_t off_ch1 = make_note_off(0u, sid, 1u, 64u, 0u);
    omega_event_t off_ch0 = make_note_off(0u, sid, 0u, 60u, 0u);
    midi_in.prime(as_event(off_ch1));
    midi_in.prime(as_event(off_ch0));
    omega_engine_process(e);

    const size_t n = omega_recorder_stop(rec);
    REQUIRE(n == 1u);  // only the ch0 note was captured

    omega_engine_stop(e);
    omega_recorder_destroy(e, rec);
    omega_engine_destroy(e);
}

// ── Flush on stop: held note is inserted with truncated duration ──────────────

TEST_CASE("omega_recorder: held note flushed on stop_recording")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega::CapturingSink sink;
    REQUIRE(omega_engine_add_sink(e, as_sink(sink)) == OMEGA_OK);
    const uint32_t sid = omega_sink_id(as_sink(sink));

    omega::MockEventInput midi_in;
    REQUIRE(omega_engine_add_input(e, as_input(midi_in)) == OMEGA_OK);

    omega_track_id_t track = 0u;
    REQUIRE(omega_engine_add_track(e, "flush", &track) == OMEGA_OK);

    omega_recorder_t* rec = omega_recorder_create(e, sid);
    REQUIRE(rec != nullptr);

    REQUIRE(omega_engine_play(e) == OMEGA_OK);
    REQUIRE(omega_recorder_start(rec, track, 0xFFu) == OMEGA_OK);

    // Prime NOTE_ON but no NOTE_OFF — recorder should flush on stop.
    omega_event_t on = omega_make_note_on(0u, sid, 0u, 60u, 100u, 0u);
    midi_in.prime(as_event(on));
    omega_engine_process(e);
    omega_engine_process(e);  // extra cycle so last_tick_ > on_tick

    const size_t n = omega_recorder_stop(rec);
    REQUIRE(n == 1u);  // flushed the still-held note

    omega_engine_stop(e);
    omega_recorder_destroy(e, rec);
    omega_engine_destroy(e);
}
