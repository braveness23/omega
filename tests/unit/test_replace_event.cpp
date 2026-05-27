/*
 * Tests for ReplaceEventCmd (issue #30).
 *
 * Verifies that:
 *  - Replacing a pattern event at a valid index takes effect on the next process() cycle.
 *  - Replacing at an out-of-bounds index is a no-op (does not crash).
 *  - Replacing with a different tick re-sorts the event vector.
 *  - The new event is dispatched by the sequencer in place of the old one.
 *  - pattern_replace_event() returns OMEGA_OK when the command is enqueued.
 */

#include <omega/commands.h>
#include <omega/engine.h>
#include <omega/omega.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>

#include <catch2/catch_test_macros.hpp>

using namespace omega;

// Helper: engine with a clock, a CapturingSink, sink_id available for event construction.
struct Fixture
{
    MockClock clock;
    Engine engine{&clock};
    CapturingSink sink;
    PatternId pat{0};
    uint32_t sink_id{0};

    Fixture()
    {
        sink_id = sink.sink_id();
        engine.add_sink(&sink);
    }
};

// ── ReplaceEventCmd struct ────────────────────────────────────────────────────

TEST_CASE("ReplaceEventCmd fits in Command variant size limit")
{
    static_assert(sizeof(Command) <= 64, "Command must fit within 64 bytes");
    REQUIRE(sizeof(ReplaceEventCmd) <= 64u);
}

// ── Engine C++ API ────────────────────────────────────────────────────────────

TEST_CASE("pattern_replace_event: replaces event at given index")
{
    Fixture f;

    // Create a pattern with a single C4 note on tick 0.
    f.pat = f.engine.create_pattern("p", 960u);
    REQUIRE(f.engine.pattern_add_event(
                f.pat, omega_make_note_on(0u, f.sink_id, 0, 60, 100, 960u)) == OMEGA_OK);

    // Replace with D4 on the same tick.
    Event replacement = omega_make_note_on(0u, f.sink_id, 0, 62, 90, 960u);
    REQUIRE(f.engine.pattern_replace_event(f.pat, 0u, replacement) == OMEGA_OK);
    f.engine.process();  // drains command queue (still stopped)

    // The event at index 0 should now be D4.
    const Pattern* p = f.engine.pattern_library().get(f.pat);
    REQUIRE(p != nullptr);
    REQUIRE(p->events.size() == 1u);
    REQUIRE(p->events[0].data[0] == 62u);  // D4
    REQUIRE(p->events[0].data[1] == 90u);  // velocity
}

TEST_CASE("pattern_replace_event: out-of-bounds index is silently ignored")
{
    Fixture f;

    f.pat = f.engine.create_pattern("p", 960u);
    REQUIRE(f.engine.pattern_add_event(
                f.pat, omega_make_note_on(0u, f.sink_id, 0, 60, 100, 960u)) == OMEGA_OK);

    Event replacement = omega_make_note_on(0u, f.sink_id, 0, 62, 90, 960u);

    // Index 5 is out of bounds for a 1-event pattern — silently ignored.
    REQUIRE(f.engine.pattern_replace_event(f.pat, 5u, replacement) == OMEGA_OK);
    f.engine.process();

    // Original event should still be there.
    const Pattern* p = f.engine.pattern_library().get(f.pat);
    REQUIRE(p != nullptr);
    REQUIRE(p->events.size() == 1u);
    REQUIRE(p->events[0].data[0] == 60u);  // still C4
}

TEST_CASE("pattern_replace_event: changing tick re-sorts the event vector")
{
    Fixture f;

    // Two events: C4 at tick 0, E4 at tick 240.
    f.pat = f.engine.create_pattern("p", 960u);
    REQUIRE(f.engine.pattern_add_event(
                f.pat, omega_make_note_on(0u, f.sink_id, 0, 60, 100, 240u)) == OMEGA_OK);
    REQUIRE(f.engine.pattern_add_event(
                f.pat, omega_make_note_on(240u, f.sink_id, 0, 64, 100, 240u)) == OMEGA_OK);

    // Replace index 0 (C4@0) with G4@480 — tick moves past E4@240.
    Event replacement = omega_make_note_on(480u, f.sink_id, 0, 67, 100, 240u);
    REQUIRE(f.engine.pattern_replace_event(f.pat, 0u, replacement) == OMEGA_OK);
    f.engine.process();

    const Pattern* p = f.engine.pattern_library().get(f.pat);
    REQUIRE(p != nullptr);
    REQUIRE(p->events.size() == 2u);
    // After re-sort: E4@240 first, G4@480 second.
    REQUIRE(p->events[0].data[0] == 64u);  // E4
    REQUIRE(p->events[0].tick == 240u);
    REQUIRE(p->events[1].data[0] == 67u);  // G4
    REQUIRE(p->events[1].tick == 480u);
}

TEST_CASE("pattern_replace_event: replacement is dispatched during playback")
{
    MockClock clock;
    Engine engine{&clock};
    CapturingSink sink;
    engine.add_sink(&sink);
    uint32_t sid = sink.sink_id();

    // Pattern: C4 on tick 0.
    PatternId pat = engine.create_pattern("p", 480u);
    REQUIRE(engine.pattern_add_event(pat, omega_make_note_on(0u, sid, 0, 60, 100, 480u)) ==
            OMEGA_OK);

    // Replace C4 with D4 before starting playback (safe in stopped state).
    REQUIRE(engine.pattern_replace_event(pat, 0u, omega_make_note_on(0u, sid, 0, 62, 90, 480u)) ==
            OMEGA_OK);

    // Cue the pattern on slot 0 and start playing.
    REQUIRE(engine.perf_assign(0u, pat) == OMEGA_OK);
    REQUIRE(engine.perf_cue(0u, CueMode::IMMEDIATE) == OMEGA_OK);
    REQUIRE(engine.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);
    engine.process();

    // Advance past tick 0 so the note fires.
    clock.advance_ticks(10u);
    engine.process();

    // The sink should have received D4 (62), not C4 (60).
    REQUIRE(sink.has_note_on(62u));
    REQUIRE_FALSE(sink.has_note_on(60u));
}
