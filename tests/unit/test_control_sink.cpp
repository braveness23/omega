/*
 * Tests for omega::ControlSink — the timing-thread OutputSink that executes
 * control-sequence events (OMEGA_CTRL_*) as direct engine mutations.
 *
 * Verifies that:
 *  - CTRL_START_SLOT causes the target slot to start playing.
 *  - CTRL_STOP_SLOT stops a playing slot.
 *  - CTRL_SET_TEMPO changes the engine's tempo at the event's tick.
 *  - CTRL_TRANSPOSE applies a semitone offset to a slot.
 *  - Non-control events (NOTE_ON routed to ControlSink) are silently dropped.
 */

#include <omega/commands.h>
#include <omega/control_sink.h>
#include <omega/engine.h>
#include <omega/omega.h>
#include <omega/perf_slot.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>
#include <omega/types.h>

#include <catch2/catch_test_macros.hpp>

using namespace omega;

namespace
{

// Fixture: engine + a MIDI CapturingSink + a ControlSink; one pattern
// ready to assign to any slot.
struct Fixture  // NOLINT(clang-analyzer-optin.performance.Padding)
{
    MockClock clock;
    Engine engine{&clock};
    CapturingSink midi_sink;
    ControlSink ctrl_sink{engine.perf_source(), engine.tempo_map()};
    uint32_t msid{midi_sink.sink_id()};
    uint32_t csid{ctrl_sink.sink_id()};

    Fixture()
    {
        engine.add_sink(&midi_sink);
        engine.add_sink(&ctrl_sink);
    }

    // Create a pattern with one note on the MIDI sink and assign to slot.
    PatternId make_note_pattern(uint32_t slot, uint64_t length = 960u)
    {
        PatternId pid = engine.create_pattern("n", length);
        engine.pattern_add_event(pid, omega_make_note_on(0u, msid, 0, 60, 100, length));
        engine.perf_assign(static_cast<SlotId>(slot), pid);
        return pid;
    }

    // Create a control pattern with one ctrl event and assign to slot.
    PatternId make_ctrl_pattern(uint32_t slot, const Event& ctrl_ev, uint64_t length = 960u)
    {
        PatternId pid = engine.create_pattern("c", length);
        engine.pattern_add_event(pid, ctrl_ev);
        engine.perf_assign(static_cast<SlotId>(slot), pid);
        return pid;
    }

    void start()
    {
        engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
        clock.advance_ticks(1u);
        engine.process();
    }

    void tick(uint64_t n = 1u)
    {
        clock.advance_ticks(n);
        engine.process();
    }
};

}  // namespace

// ── CTRL_START_SLOT ──────────────────────────────────────────────────────────

TEST_CASE("ControlSink: CTRL_START_SLOT immediately starts the target slot")
{
    Fixture f;
    f.make_note_pattern(0);  // slot 0 = the sequence to be started

    // Control pattern in slot 1: at tick 0, start slot 0 immediately.
    f.make_ctrl_pattern(1, omega_make_ctrl_start_slot(0u, f.csid, 0u, OMEGA_CUE_IMMEDIATE));

    f.start();

    // Cue slot 1 (the control pattern).
    f.engine.perf_cue(1u, CueMode::IMMEDIATE);
    f.tick();

    // The ctrl event fires during this tick → slot 0 should be cued/playing.
    // (In practice one loop iteration may be needed before the started slot dispatches
    // for the first time; we check on the next process() call.)
    f.tick();

    const SlotState s = f.engine.perf_slot_state(0u);
    CHECK((s == SlotState::PLAYING || s == SlotState::QUEUED));
}

// ── CTRL_STOP_SLOT ───────────────────────────────────────────────────────────

TEST_CASE("ControlSink: CTRL_STOP_SLOT stops a playing slot")
{
    Fixture f;
    f.make_note_pattern(0);

    // Start slot 0 directly.
    f.start();
    f.engine.perf_cue(0u, CueMode::IMMEDIATE);
    f.tick();
    REQUIRE(f.engine.perf_slot_state(0u) == SlotState::PLAYING);

    // Control pattern in slot 1: at tick 0, stop slot 0 immediately.
    f.make_ctrl_pattern(1, omega_make_ctrl_stop_slot(0u, f.csid, 0u, OMEGA_CUE_IMMEDIATE));
    f.engine.perf_cue(1u, CueMode::IMMEDIATE);
    f.tick();

    const SlotState s = f.engine.perf_slot_state(0u);
    CHECK((s == SlotState::STOPPING || s == SlotState::IDLE));
}

// ── CTRL_SET_TEMPO ───────────────────────────────────────────────────────────

TEST_CASE("ControlSink: CTRL_SET_TEMPO changes the engine tempo")
{
    Fixture f;

    // Control pattern in slot 0: at tick 0, set tempo to 90 BPM.
    f.make_ctrl_pattern(0, omega_make_ctrl_set_tempo(0u, f.csid, 90'000u));

    f.start();
    f.engine.perf_cue(0u, CueMode::IMMEDIATE);
    f.tick();
    f.tick();  // one cycle for ctrl event to fire and tempo map to be updated

    CHECK(f.engine.tempo_map().bpm_milli_at(1u) == 90'000u);
}

// ── CTRL_TRANSPOSE ───────────────────────────────────────────────────────────

TEST_CASE("ControlSink: CTRL_TRANSPOSE applies semitone offset to target slot")
{
    Fixture f;
    f.make_note_pattern(0);  // note at pitch 60

    // Control pattern in slot 1: at tick 0, transpose slot 0 up 12 semitones.
    f.make_ctrl_pattern(1, omega_make_ctrl_transpose(0u, f.csid, 0u, 12));

    f.start();
    // Cue the ctrl pattern first; advance one tick so the ctrl event fires and
    // sets the transpose on slot 0 BEFORE slot 0 starts playing.
    f.engine.perf_cue(1u, CueMode::IMMEDIATE);
    f.tick();
    f.tick();  // ctrl event fires on first or second tick; transpose now set

    // Now cue the note pattern and let it dispatch.
    f.engine.perf_cue(0u, CueMode::IMMEDIATE);
    f.tick();
    f.tick();

    bool has_transposed = false;
    for (size_t i = 0; i < f.midi_sink.count(); ++i)
    {
        if (f.midi_sink.at(i).payload_tag == OMEGA_NOTE_ON &&
            omega_event_note_pitch(&f.midi_sink.at(i)) == 72u)
        {
            has_transposed = true;
        }
    }
    CHECK(has_transposed);
}

// ── Non-control events silently dropped ─────────────────────────────────────

TEST_CASE("ControlSink: non-control events (NOTE_ON) are silently dropped")
{
    Fixture f;

    // Build a pattern with a NOTE_ON event routed to the ControlSink (wrong sink).
    PatternId pid = f.engine.create_pattern("x", 960u);
    f.engine.pattern_add_event(pid, omega_make_note_on(0u, f.csid, 0, 60, 100, 240u));
    f.engine.perf_assign(0u, pid);

    f.start();
    f.engine.perf_cue(0u, CueMode::IMMEDIATE);
    f.tick();

    // The ControlSink should have received and dropped the NOTE_ON.
    // No MIDI output on the real MIDI sink.
    CHECK(f.midi_sink.count() == 0u);
}
