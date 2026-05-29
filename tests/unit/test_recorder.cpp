/*
 * Tests for omega::Recorder — live MIDI capture into a TimelineSource track.
 *
 * Verifies that:
 *  - NOTE_ON + NOTE_OFF pairs are captured as one NOTE_ON with correct duration.
 *  - Channel filter rejects events on non-matching channels.
 *  - CC and PROGRAM events are inserted immediately (no duration to resolve).
 *  - Notes still pending when stop_recording() is called are flushed.
 *  - No events are written when is_recording() is false.
 *  - stop_recording() returns the correct captured event count.
 *  - on_locate() clears pending notes so a loop wrap doesn't produce stuck notes.
 *  - Recorded notes play back via the timeline on subsequent advance() calls.
 */

#include <omega/commands.h>
#include <omega/engine.h>
#include <omega/omega.h>
#include <omega/recorder.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>
#include <omega/test/mock_event_input.h>
#include <omega/timeline.h>
#include <omega/types.h>

#include <catch2/catch_test_macros.hpp>

using namespace omega;

namespace
{

// Standard fixture: engine + sink + MockEventInput + Recorder wired up.
// The Recorder runs at MODULATOR priority so it captures before TimelineSource plays back.
struct Fixture
{
    MockClock clock;
    Engine engine{&clock};
    CapturingSink sink;
    uint32_t sid{sink.sink_id()};
    MockEventInput input;
    TrackId track{0};
    Recorder recorder{engine.timeline_source(), sid};

    Fixture()
    {
        engine.add_sink(&sink);
        engine.add_input(&input);
        engine.add_source(&recorder, OMEGA_SOURCE_PRIORITY_MODULATOR);
        track = engine.add_track("rec");
        engine.set_track_sink(track, sid);
    }

    // Start transport and drain initial command cycle.
    void start()
    {
        engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
        clock.advance_ticks(1u);
        engine.process();
    }

    // Advance N ticks and process.
    void tick(uint64_t n = 1u)
    {
        clock.advance_ticks(n);
        engine.process();
    }
};

// Build a raw NOTE_OFF event (velocity 0 NOTE_ON — the MIDI wire convention).
Event make_note_off(uint8_t channel, uint8_t note)
{
    Event e{};
    e.payload_tag = OMEGA_NOTE_OFF;
    e.channel = channel;
    e.data[0] = note;
    e.data[1] = 0u;
    return e;
}

}  // namespace

// ── Basic note capture ────────────────────────────────────────────────────────

TEST_CASE("Recorder: NOTE_ON + NOTE_OFF produces one timeline event with correct duration")
{
    Fixture f;
    f.recorder.start_recording(f.track);
    f.start();

    // Tick 1: note-on for note 60 on ch 0.
    f.input.prime(omega_make_note_on(0u, f.sid, 0, 60, 100, 0u));
    f.tick();  // to_tick ≈ 1; recorder captures note-on as pending

    CHECK(f.engine.timeline_source().tracks()[0].events.empty());  // not yet inserted

    // Tick 241: note-off arrives 240 ticks later → duration = 240.
    f.input.prime(make_note_off(0, 60));
    f.tick(240u);

    const auto& evts = f.engine.timeline_source().tracks()[0].events;
    REQUIRE(evts.size() == 1u);
    CHECK(evts[0].payload_tag == OMEGA_NOTE_ON);
    CHECK(evts[0].data[0] == 60u);
    CHECK(evts[0].data[1] == 100u);
    CHECK(omega_event_note_duration(&evts[0]) == 240u);
}

// ── Channel filter ────────────────────────────────────────────────────────────

TEST_CASE("Recorder: channel filter rejects events on non-matching channels")
{
    Fixture f;
    f.recorder.start_recording(f.track, /*channel_filter=*/3u);
    f.start();

    // Events on ch 0 (rejected) and ch 3 (accepted).
    f.input.prime(omega_make_note_on(0u, f.sid, 0, 60, 100, 0u));
    f.input.prime(omega_make_note_on(0u, f.sid, 3, 62, 90, 0u));
    f.tick();  // both arrive at same tick

    f.input.prime(make_note_off(0, 60));
    f.input.prime(make_note_off(3, 62));
    f.tick(240u);

    const auto& evts = f.engine.timeline_source().tracks()[0].events;
    REQUIRE(evts.size() == 1u);
    CHECK(evts[0].channel == 3u);
    CHECK(evts[0].data[0] == 62u);
}

// ── CC passthrough ────────────────────────────────────────────────────────────

TEST_CASE("Recorder: CC events are inserted immediately without a pending phase")
{
    Fixture f;
    f.recorder.start_recording(f.track);
    f.start();

    Event cc{};
    cc.payload_tag = OMEGA_CC;
    cc.channel = 0u;
    cc.data[0] = 7u;  // volume controller
    cc.data[1] = 100u;
    f.input.prime(cc);
    f.tick();

    const auto& evts = f.engine.timeline_source().tracks()[0].events;
    REQUIRE(evts.size() == 1u);
    CHECK(evts[0].payload_tag == OMEGA_CC);
    CHECK(evts[0].data[0] == 7u);
}

// ── Pending-note flush on stop ────────────────────────────────────────────────

TEST_CASE("Recorder: stop_recording flushes pending notes with duration up to stop tick")
{
    Fixture f;
    f.recorder.start_recording(f.track);
    f.start();

    f.input.prime(omega_make_note_on(0u, f.sid, 0, 60, 100, 0u));
    f.tick();  // note-on at tick ~1

    // Advance 100 more ticks with no note-off.
    f.tick(100u);

    size_t n = f.recorder.stop_recording();
    CHECK(n == 1u);

    const auto& evts = f.engine.timeline_source().tracks()[0].events;
    REQUIRE(evts.size() == 1u);
    CHECK(evts[0].payload_tag == OMEGA_NOTE_ON);
    // Duration should be > 0 (stop_tick > on_tick).
    CHECK(omega_event_note_duration(&evts[0]) > 0u);
}

// ── Not recording ─────────────────────────────────────────────────────────────

TEST_CASE("Recorder: events are not captured when is_recording() is false")
{
    Fixture f;
    // Do NOT call start_recording.
    f.start();

    f.input.prime(omega_make_note_on(0u, f.sid, 0, 60, 100, 0u));
    f.tick();
    f.input.prime(make_note_off(0, 60));
    f.tick(240u);

    CHECK(f.engine.timeline_source().tracks()[0].events.empty());
    CHECK(!f.recorder.is_recording());
}

// ── Record count ──────────────────────────────────────────────────────────────

TEST_CASE("Recorder: stop_recording returns the number of NOTE_ON events inserted")
{
    Fixture f;
    f.recorder.start_recording(f.track);
    f.start();

    for (uint8_t note = 60u; note < 63u; ++note)
    {
        f.input.prime(omega_make_note_on(0u, f.sid, 0, note, 100, 0u));
        f.tick();
        f.input.prime(make_note_off(0, note));
        f.tick(240u);
    }

    size_t n = f.recorder.stop_recording();
    CHECK(n == 3u);
}

// ── on_locate clears pending ──────────────────────────────────────────────────

TEST_CASE("Recorder: on_locate clears pending notes — no stuck note after loop wrap")
{
    Fixture f;
    f.recorder.start_recording(f.track);
    f.start();

    // Note-on at tick ~1; then locate back to tick 0 (simulating a loop wrap).
    f.input.prime(omega_make_note_on(0u, f.sid, 0, 60, 100, 0u));
    f.tick();

    // Locate resets the pending table.
    f.engine.enqueue(TransportCmd{TransportAction::LOCATE, 0u});
    f.tick();

    // A note-off now arrives for the note that was started before the locate.
    // Because pending was cleared, this note-off should be a no-op (nothing inserted).
    f.input.prime(make_note_off(0, 60));
    f.tick();

    CHECK(f.engine.timeline_source().tracks()[0].events.empty());
}

// ── Recorded notes play back ──────────────────────────────────────────────────

TEST_CASE("Recorder: a recorded note plays back via TimelineSource on the next loop")
{
    Fixture f;
    f.recorder.start_recording(f.track);
    f.start();

    // Record one note at tick ~1.
    f.input.prime(omega_make_note_on(0u, f.sid, 0, 60, 100, 0u));
    f.tick();
    f.input.prime(make_note_off(0, 60));
    f.tick(240u);

    f.recorder.stop_recording();
    f.sink.clear();

    // Locate back to start and play; the recorded note should reach the sink.
    f.engine.enqueue(TransportCmd{TransportAction::LOCATE, 0u});
    f.tick();
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.tick(250u);

    CHECK(f.sink.has_note_on(60, 0));
}
