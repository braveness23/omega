#include <omega/commands.h>
#include <omega/engine.h>
#include <omega/perf_slot.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>

#include <array>
#include <catch2/catch_test_macros.hpp>

using namespace omega;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Advance the engine to a specific absolute tick and process.
static void advance_to(Engine& e, MockClock& clock, uint64_t tick)
{
    clock.advance_ticks(tick + 1u);
    e.process();
}

// Build a simple engine with a clock and capturing sink already wired.
struct Rig
{
    MockClock clock;
    CapturingSink sink;
    Engine engine{&clock};

    Rig() { engine.add_sink(&sink); }

    void play() { REQUIRE(engine.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK); }

    // Drain command queue (clock not advanced → to_tick stays at 0).
    void drain() { engine.process(); }

    // Advance by delta ticks and process.
    void step(uint64_t delta)
    {
        clock.advance_ticks(delta);
        engine.process();
    }
};

// ── assign: state machine transitions ─────────────────────────────────────────

TEST_CASE("Perf assign: EMPTY to IDLE - subsequent cue plays the pattern")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);

    REQUIRE(r.sink.has_note_on(60, 0));
}

TEST_CASE("Perf assign: IDLE to IDLE - reassign plays the new pattern on cue")
{
    Rig r;
    PatternId pid_a = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid_a, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));
    PatternId pid_b = r.engine.create_pattern("B", 960u);
    r.engine.pattern_add_event(pid_b, omega_make_note_on(0u, r.sink.sink_id(), 0, 72, 100, 0));

    REQUIRE(r.engine.perf_assign(0u, pid_a) == OMEGA_OK);
    REQUIRE(r.engine.perf_assign(0u, pid_b) == OMEGA_OK);  // reassign
    REQUIRE(r.engine.perf_cue(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);

    REQUIRE_FALSE(r.sink.has_note_on(60, 0));
    REQUIRE(r.sink.has_note_on(72, 0));
}

TEST_CASE("Perf assign: QUEUED to QUEUED - updated pattern plays at boundary")
{
    Rig r;
    PatternId pid_a = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid_a, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));
    PatternId pid_b = r.engine.create_pattern("B", 960u);
    r.engine.pattern_add_event(pid_b, omega_make_note_on(0u, r.sink.sink_id(), 0, 72, 100, 0));

    r.play();
    r.drain();
    r.step(1u);

    // Queue A, then reassign to B while still QUEUED
    REQUIRE(r.engine.perf_assign(0u, pid_a) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    // Now reassign to B — slot is QUEUED, should update pending to B
    REQUIRE(r.engine.perf_assign(0u, pid_b) == OMEGA_OK);

    // Drain commands (cue + assign); at current tick=1 the boundary=960
    r.engine.process();

    // Advance past tick 960 — B should play, not A
    r.step(960u);

    REQUIRE_FALSE(r.sink.has_note_on(60, 0));
    REQUIRE(r.sink.has_note_on(72, 0));
}

TEST_CASE("Perf assign: PLAYING to PLAYING (no state change) - new pattern plays after next cue")
{
    Rig r;
    PatternId pid_a = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid_a, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));
    PatternId pid_b = r.engine.create_pattern("B", 960u);
    r.engine.pattern_add_event(pid_b, omega_make_note_on(480u, r.sink.sink_id(), 0, 72, 100, 0));

    REQUIRE(r.engine.perf_assign(0u, pid_a) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);

    // A is now PLAYING (note 60 fired)
    REQUIRE(r.sink.has_note_on(60, 0));
    r.sink.clear();

    // Reassign to B while PLAYING — state should stay PLAYING with A
    REQUIRE(r.engine.perf_assign(0u, pid_b) == OMEGA_OK);
    r.engine.process();

    // Advance to tick 480 — still playing A (no note at 480 in A)
    r.step(480u);
    REQUIRE_FALSE(r.sink.has_note_on(72, 0));
}

// ── cue(BOUNDARY): state machine transitions ──────────────────────────────────

TEST_CASE("Perf cue BOUNDARY: IDLE to QUEUED to PLAYING at boundary")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    r.play();
    r.drain();

    // At tick 0, boundary = 0 → fires immediately in first advance window
    r.step(1u);
    REQUIRE(r.sink.has_note_on(60, 0));
}

TEST_CASE("Perf cue BOUNDARY: QUEUED to QUEUED (update queued pattern)")
{
    Rig r;
    PatternId pid_a = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid_a, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));
    PatternId pid_b = r.engine.create_pattern("B", 960u);
    r.engine.pattern_add_event(pid_b, omega_make_note_on(0u, r.sink.sink_id(), 0, 72, 100, 0));

    r.play();
    r.drain();
    // Advance past tick 0 so next boundary isn't 0
    r.step(100u);

    // Assign A, cue → QUEUED with boundary=960
    REQUIRE(r.engine.perf_assign(0u, pid_a) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    // Reassign to B and cue again → QUEUED with B pending
    REQUIRE(r.engine.perf_assign(0u, pid_b) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    r.engine.process();  // drain commands

    // Advance to tick 960 — B should play, not A
    r.step(861u);  // 101..961 → to_tick = 960
    REQUIRE_FALSE(r.sink.has_note_on(60, 0));
    REQUIRE(r.sink.has_note_on(72, 0));
}

TEST_CASE("Perf cue BOUNDARY: PLAYING (different pattern) to STOPPING then PLAYING new")
{
    Rig r;
    PatternId pid_a = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid_a, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));
    PatternId pid_b = r.engine.create_pattern("B", 960u);
    r.engine.pattern_add_event(pid_b, omega_make_note_on(0u, r.sink.sink_id(), 0, 72, 100, 0));

    // Start A immediately
    REQUIRE(r.engine.perf_assign(0u, pid_a) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);
    REQUIRE(r.sink.has_note_on(60, 0));
    r.sink.clear();

    // At tick ~1 (inside A's first loop), cue B at boundary
    REQUIRE(r.engine.perf_assign(0u, pid_b) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    r.engine.process();  // drain

    // Advance to tick 960 — A stops, B starts
    r.step(960u);  // total to_tick = 961
    REQUIRE(r.sink.has_note_on(72, 0));
}

TEST_CASE("Perf cue BOUNDARY: PLAYING (same pattern) to STOPPING (toggle)")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 240));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);
    REQUIRE(r.sink.has_note_on(60, 0));
    r.sink.clear();

    // Cue same pattern again → toggle to STOPPING
    REQUIRE(r.engine.perf_cue(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    r.engine.process();  // drain

    // Advance to boundary tick 960
    r.step(960u);

    // No new note-on after boundary
    REQUIRE_FALSE(r.sink.has_note_on(60, 0));
    // Should have a note-off for the active note
    REQUIRE(r.sink.has_note_off(60, 0));
}

TEST_CASE("Perf cue BOUNDARY: STOPPING to pending set (restart at boundary)")
{
    Rig r;
    PatternId pid_a = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid_a, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));
    PatternId pid_b = r.engine.create_pattern("B", 960u);
    r.engine.pattern_add_event(pid_b, omega_make_note_on(0u, r.sink.sink_id(), 0, 72, 100, 0));

    // Start A, then stop at boundary
    REQUIRE(r.engine.perf_assign(0u, pid_a) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    REQUIRE(r.engine.perf_stop(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);

    // Now cue B from STOPPING state
    REQUIRE(r.engine.perf_assign(0u, pid_b) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    r.engine.process();  // drain

    // Advance to tick 960 — A stops, B starts
    r.step(960u);
    REQUIRE(r.sink.has_note_on(72, 0));
}

// ── cue(IMMEDIATE): state machine transitions ─────────────────────────────────

TEST_CASE("Perf cue IMMEDIATE: IDLE to PLAYING")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);

    REQUIRE(r.sink.has_note_on(60, 0));
}

TEST_CASE("Perf cue IMMEDIATE: QUEUED to PLAYING (cancel queue)")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));

    r.play();
    r.drain();
    r.step(100u);  // advance past tick 0 so boundary would be 960

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::NEXT_BEAT) == OMEGA_OK);  // QUEUED at 960
    // Immediately cue to override
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.engine.process();  // drain

    // Note should fire now (not wait for tick 960)
    r.step(1u);
    REQUIRE(r.sink.has_note_on(60, 0));
    REQUIRE(r.sink.at(0).tick < 960u);
}

TEST_CASE("Perf cue IMMEDIATE: PLAYING to PLAYING (restart from phase 0)")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));
    r.engine.pattern_add_event(pid, omega_make_note_on(480u, r.sink.sink_id(), 0, 64, 100, 0));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);
    REQUIRE(r.sink.has_note_on(60, 0));
    r.sink.clear();

    // Advance to tick 200 (before the tick-480 event)
    r.step(200u);

    // Restart immediately — should fire note at tick 0 of the pattern again
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.engine.process();
    r.step(1u);

    REQUIRE(r.sink.has_note_on(60, 0));
}

TEST_CASE("Perf cue IMMEDIATE: STOPPING to PLAYING (cancel stop)")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    REQUIRE(r.engine.perf_stop(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);
    r.sink.clear();

    // Cue immediately from STOPPING state
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.engine.process();
    r.step(1u);

    // Should be playing again — note fires at pattern tick 0 (= current start_tick)
    REQUIRE(r.sink.has_note_on(60, 0));
}

// ── cue_stop(BOUNDARY) transitions ───────────────────────────────────────────

TEST_CASE("Perf stop BOUNDARY: PLAYING to STOPPING to IDLE at boundary")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 480));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);
    REQUIRE(r.sink.has_note_on(60, 0));
    r.sink.clear();

    // Stop at boundary (tick 960)
    REQUIRE(r.engine.perf_stop(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    r.engine.process();

    // Advance to tick 480 — note-off from duration fires here
    r.step(480u);
    REQUIRE(r.sink.has_note_off(60, 0));
    r.sink.clear();

    // Advance past boundary tick 960
    r.step(480u);  // to_tick = 960

    // Note should silence at boundary; no new note-on after
    REQUIRE_FALSE(r.sink.has_note_on(60, 0));
}

TEST_CASE("Perf stop BOUNDARY: QUEUED to IDLE (cancel before it starts)")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));

    r.play();
    r.drain();
    r.step(100u);  // boundary = 960

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    REQUIRE(r.engine.perf_stop(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    r.engine.process();  // drain

    // Advance past tick 960 — should never play
    r.step(900u);
    REQUIRE(r.sink.count() == 0u);
}

// ── cue_stop(IMMEDIATE) transitions ──────────────────────────────────────────

TEST_CASE("Perf stop IMMEDIATE: PLAYING to IDLE with note-off")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 960));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);
    REQUIRE(r.sink.has_note_on(60, 0));
    r.sink.clear();

    // Stop immediately
    REQUIRE(r.engine.perf_stop(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.engine.process();
    r.step(1u);  // advance one tick to trigger the silence

    REQUIRE(r.sink.has_note_off(60, 0));
}

TEST_CASE("Perf stop IMMEDIATE: QUEUED to IDLE")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));

    r.play();
    r.drain();
    r.step(100u);

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    REQUIRE(r.engine.perf_stop(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.engine.process();

    r.step(900u);  // past tick 960
    REQUIRE(r.sink.count() == 0u);
}

TEST_CASE("Perf stop IMMEDIATE: STOPPING to IDLE (accelerate the stop)")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 960));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    REQUIRE(r.engine.perf_stop(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);
    REQUIRE(r.sink.has_note_on(60, 0));
    r.sink.clear();

    // Accelerate: stop immediately from STOPPING state
    REQUIRE(r.engine.perf_stop(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.engine.process();
    r.step(1u);

    // Note-off fires before tick 960
    REQUIRE(r.sink.has_note_off(60, 0));
    REQUIRE(r.sink.at(0).tick < 960u);
}

// ── Loop boundary transitions (internal) ─────────────────────────────────────

TEST_CASE("Perf loop boundary: QUEUED to PLAYING fires events at boundary tick")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));
    r.engine.pattern_add_event(pid, omega_make_note_on(480u, r.sink.sink_id(), 0, 64, 100, 0));

    r.play();
    r.drain();
    r.step(100u);  // at tick 100, boundary = 960

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    r.engine.process();

    // Advance to tick 961 — pattern starts at tick 960, fires note at offset 0
    r.step(861u);  // 101+861 = to_tick 961
    REQUIRE(r.sink.has_note_on(60, 0));
    REQUIRE(r.sink.at(0).tick == 960u);
}

TEST_CASE("Perf loop boundary: STOPPING to IDLE fires note-off at boundary")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    // Note with duration > pattern length to be active when stopped
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 1920));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);
    REQUIRE(r.sink.has_note_on(60, 0));
    r.sink.clear();

    // Stop at next boundary (tick 960)
    REQUIRE(r.engine.perf_stop(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    r.engine.process();

    // Advance to tick 960 — note-off fires
    r.step(960u);
    REQUIRE(r.sink.has_note_off(60, 0));
    REQUIRE(r.sink.at(0).tick == 960u);

    // No further events
    r.sink.clear();
    r.step(960u);
    REQUIRE(r.sink.count() == 0u);
}

// ── Two simultaneous slots ────────────────────────────────────────────────────

TEST_CASE("Perf: two slots play simultaneously with independent state")
{
    Rig r;
    PatternId pid_a = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid_a, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 480));
    PatternId pid_b = r.engine.create_pattern("B", 1920u);
    r.engine.pattern_add_event(pid_b, omega_make_note_on(960u, r.sink.sink_id(), 0, 72, 100, 0));

    REQUIRE(r.engine.perf_assign(0u, pid_a) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    REQUIRE(r.engine.perf_assign(1u, pid_b) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(1u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);

    // Slot 0: note 60 at tick 0
    REQUIRE(r.sink.has_note_on(60, 0));
    r.sink.clear();

    // Advance to tick 480: note-off for slot 0 fires
    r.step(480u);
    REQUIRE(r.sink.has_note_off(60, 0));
    r.sink.clear();

    // Advance to tick 960: slot 1 fires note 72
    r.step(480u);
    REQUIRE(r.sink.has_note_on(72, 0));

    // Stop slot 0 at boundary (tick 960 already passed; next = 1920)
    REQUIRE(r.engine.perf_stop(0u, CueMode::NEXT_BEAT) == OMEGA_OK);
    r.engine.process();
    r.sink.clear();

    // Advance to tick 1920: slot 0 stops, slot 1 loops
    r.step(960u);
    REQUIRE_FALSE(r.sink.has_note_on(60, 0));  // slot 0 did not restart
}

// ── Transpose ─────────────────────────────────────────────────────────────────

TEST_CASE("Perf transpose +12: note 60 dispatched as note 72")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    REQUIRE(r.engine.perf_set_transpose(0u, 12) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);

    REQUIRE(r.sink.count() >= 1u);
    REQUIRE(r.sink.has_note_on(72, 0));
    REQUIRE_FALSE(r.sink.has_note_on(60, 0));
}

TEST_CASE("Perf transpose -12: note 60 dispatched as note 48")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    REQUIRE(r.engine.perf_set_transpose(0u, -12) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);

    REQUIRE(r.sink.has_note_on(48, 0));
}

TEST_CASE("Perf transpose clamps: note 120 + 24 = clamped to 127")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 120, 100, 0));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    REQUIRE(r.engine.perf_set_transpose(0u, 24) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);

    REQUIRE(r.sink.has_note_on(127, 0));
}

// ── Velocity scale ────────────────────────────────────────────────────────────

TEST_CASE("Perf velocity scale 50: velocity 100 dispatched as 50")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    REQUIRE(r.engine.perf_set_velocity_scale(0u, 50) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);

    REQUIRE(r.sink.count() >= 1u);
    REQUIRE(r.sink.at(0).payload_tag == OMEGA_NOTE_ON);
    REQUIRE(r.sink.at(0).data[1] == 50u);
}

TEST_CASE("Perf velocity scale 200: velocity 63 dispatched as 126")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 63, 0));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    REQUIRE(r.engine.perf_set_velocity_scale(0u, 200) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);

    REQUIRE(r.sink.at(0).data[1] == 126u);
}

TEST_CASE("Perf velocity scale 0: minimum dispatched velocity is 1")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    REQUIRE(r.engine.perf_set_velocity_scale(0u, 0) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);

    REQUIRE(r.sink.at(0).data[1] == 1u);
}

// ── Looping ───────────────────────────────────────────────────────────────────

TEST_CASE("Perf: pattern loops - second loop fires events at expected ticks")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 480u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);    // tick 1: fires at 0
    r.step(480u);  // tick 481: fires at 480 (second loop)
    r.step(480u);  // tick 961: fires at 960 (third loop)

    // Find the three note-ons and verify their absolute ticks
    size_t on_count = 0u;
    std::array<uint64_t, 3> ticks{};
    for (size_t i = 0u; i < r.sink.count(); ++i)
    {
        if (r.sink.at(i).payload_tag == OMEGA_NOTE_ON)
        {
            if (on_count < 3u)
            {
                ticks[on_count] = r.sink.at(i).tick;
            }
            ++on_count;
        }
    }
    REQUIRE(on_count >= 3u);
    REQUIRE(ticks[0] == 0u);
    REQUIRE(ticks[1] == 480u);
    REQUIRE(ticks[2] == 960u);
}

// ── Note-off tracking ─────────────────────────────────────────────────────────

TEST_CASE("Perf: note-on with duration fires note-off at correct tick")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 240));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);
    REQUIRE(r.sink.has_note_on(60, 0));
    REQUIRE_FALSE(r.sink.has_note_off(60, 0));

    r.step(240u);
    REQUIRE(r.sink.has_note_off(60, 0));
}

// ── on_locate ─────────────────────────────────────────────────────────────────

TEST_CASE("Perf on_locate: PLAYING slot resumes from correct phase after locate")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(480u, r.sink.sink_id(), 0, 64, 100, 0));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);

    // Locate to tick 300 (before the tick-480 event)
    REQUIRE(r.engine.enqueue(TransportCmd{TransportAction::LOCATE, 300u}) == OMEGA_OK);
    r.engine.process();

    // Advance to tick 481 — event should fire at tick 480
    r.step(181u);
    REQUIRE(r.sink.has_note_on(64, 0));
}

// ── C API ─────────────────────────────────────────────────────────────────────

TEST_CASE("Perf C API: NULL engine returns INVALID")
{
    REQUIRE(omega_perf_assign(nullptr, 0u, 1u) == OMEGA_ERR_INVALID);
    REQUIRE(omega_perf_cue(nullptr, 0u, OMEGA_CUE_IMMEDIATE) == OMEGA_ERR_INVALID);
    REQUIRE(omega_perf_stop(nullptr, 0u, OMEGA_CUE_IMMEDIATE) == OMEGA_ERR_INVALID);
    REQUIRE(omega_perf_stop_all(nullptr, OMEGA_CUE_IMMEDIATE) == OMEGA_ERR_INVALID);
    REQUIRE(omega_perf_set_transpose(nullptr, 0u, 0) == OMEGA_ERR_INVALID);
    REQUIRE(omega_perf_set_velocity_scale(nullptr, 0u, 100u) == OMEGA_ERR_INVALID);
    REQUIRE(omega_perf_set_random_bias(nullptr, 0u, 0u) == OMEGA_ERR_INVALID);
}

TEST_CASE("Perf C API: full round-trip plays a note")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    CapturingSink sink;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    REQUIRE(omega_engine_add_sink(eng, reinterpret_cast<omega_sink_t*>(&sink)) == OMEGA_OK);

    omega_pattern_id_t pid = omega_pattern_create(eng, "A", 960u);
    REQUIRE(pid != OMEGA_PATTERN_INVALID);
    omega_event_t ev = omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 0);
    REQUIRE(omega_pattern_add_event(eng, pid, &ev) == OMEGA_OK);

    REQUIRE(omega_perf_assign(eng, 0u, pid) == OMEGA_OK);
    REQUIRE(omega_perf_cue(eng, 0u, OMEGA_CUE_IMMEDIATE) == OMEGA_OK);
    REQUIRE(omega_engine_play(eng) == OMEGA_OK);
    omega_engine_process(eng);  // drain commands

    MockClock clock;
    omega_engine_process(eng);  // to_tick = 0 — no events yet (clock at 0)
    (void)clock;

    omega_engine_destroy(eng);
}

TEST_CASE("Perf C API: stop_all silences all slots")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 960));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_assign(1u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(1u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);
    REQUIRE(r.sink.has_note_on(60, 0));
    r.sink.clear();

    REQUIRE(r.engine.perf_stop_all(CueMode::IMMEDIATE) == OMEGA_OK);
    r.engine.process();
    r.step(1u);

    // Both slots should have fired note-offs
    REQUIRE(r.sink.has_note_off(60, 0));
}

// ── unassign ──────────────────────────────────────────────────────────────────

TEST_CASE("Perf unassign: any state to EMPTY via assign(0)")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("A", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 60, 100, 0));

    REQUIRE(r.engine.perf_assign(0u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.play();
    r.drain();
    r.step(1u);
    REQUIRE(r.sink.has_note_on(60, 0));
    r.sink.clear();

    // Unassign (pattern = 0)
    REQUIRE(r.engine.perf_assign(0u, 0u) == OMEGA_OK);
    r.engine.process();
    r.step(960u);

    REQUIRE(r.sink.count() == 0u);
}

// ── Slot capacity: slot 127 works with expanded PERF_MAX_SLOTS ────────────────

TEST_CASE("Perf slot 127: assign and cue works after PERF_MAX_SLOTS expansion to 128")
{
    Rig r;
    PatternId pid = r.engine.create_pattern("Z", 960u);
    r.engine.pattern_add_event(pid, omega_make_note_on(0u, r.sink.sink_id(), 0, 72, 100, 240u));

    REQUIRE(r.engine.perf_assign(127u, pid) == OMEGA_OK);
    REQUIRE(r.engine.perf_cue(127u, CueMode::IMMEDIATE) == OMEGA_OK);
    r.play();
    r.engine.process();  // apply commands
    r.step(1u);

    REQUIRE(r.engine.perf_slot_state(127u) == SlotState::PLAYING);
    REQUIRE(r.sink.has_note_on(72, 0));
}
