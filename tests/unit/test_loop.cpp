#include <omega/commands.h>
#include <omega/engine.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>

#include <catch2/catch_test_macros.hpp>

using namespace omega;

// ── Validation ────────────────────────────────────────────────────────────────

TEST_CASE("loop_set rejects end_tick <= start_tick")
{
    Engine e;
    REQUIRE(e.loop_set(100, 100) == OMEGA_ERR_INVALID);
    REQUIRE(e.loop_set(200, 100) == OMEGA_ERR_INVALID);
}

TEST_CASE("loop_set accepts valid region and loop_region reflects it after process")
{
    MockClock clock;
    Engine e{&clock};

    REQUIRE(e.loop_set(0, 960) == OMEGA_OK);
    clock.advance_ticks(1);
    e.process();

    auto region = e.loop_region();
    REQUIRE(region.start_tick == 0);
    REQUIRE(region.end_tick == 960);
    REQUIRE(region.enabled);
}

TEST_CASE("loop_clear disables the loop and zeroes the region")
{
    MockClock clock;
    Engine e{&clock};

    REQUIRE(e.loop_set(100, 960) == OMEGA_OK);
    REQUIRE(e.loop_clear() == OMEGA_OK);
    clock.advance_ticks(1);
    e.process();

    auto region = e.loop_region();
    REQUIRE(region.start_tick == 0);
    REQUIRE(region.end_tick == 0);
    REQUIRE_FALSE(region.enabled);
}

TEST_CASE("loop_enable toggles enabled flag without changing region")
{
    MockClock clock;
    Engine e{&clock};

    REQUIRE(e.loop_set(0, 960) == OMEGA_OK);
    clock.advance_ticks(1);
    e.process();
    REQUIRE(e.loop_region().enabled);

    REQUIRE(e.loop_enable(false) == OMEGA_OK);
    clock.advance_ticks(1);
    e.process();

    auto region = e.loop_region();
    REQUIRE(region.start_tick == 0);
    REQUIRE(region.end_tick == 960);
    REQUIRE_FALSE(region.enabled);

    REQUIRE(e.loop_enable(true) == OMEGA_OK);
    clock.advance_ticks(1);
    e.process();

    REQUIRE(e.loop_region().enabled);
    REQUIRE(e.loop_region().start_tick == 0);
    REQUIRE(e.loop_region().end_tick == 960);
}

// ── Loop wrap behaviour ───────────────────────────────────────────────────────

TEST_CASE("transport wraps at loop end and fires note at loop start again")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    TrackId track = e.add_track("t");
    e.set_track_sink(track, sink.sink_id());

    // Note at tick 0
    omega_event_t ev = omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 0);
    e.enqueue(AddEventCmd{track, ev});

    // Loop: ticks 0 → 960; start playback
    REQUIRE(e.loop_set(0, 960) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    // First cycle: commands applied (including PLAY at now=1 tick), then advance
    // to to_tick=0. Note at tick 0 fires.
    clock.advance_ticks(1);
    e.process();
    REQUIRE(sink.has_note_on(60, 0));

    sink.clear();

    // Advance clock to exactly the loop end (tick 960 from session start).
    // The engine computes to_tick = 960 >= loop_end_tick_ (960) → wraps to 0.
    // After wrap, sources see on_locate(0) then advance(0): note fires again.
    clock.advance_ticks(960);
    e.process();
    REQUIRE(sink.has_note_on(60, 0));
}

TEST_CASE("disabled loop does not wrap at end_tick")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    TrackId track = e.add_track("t");
    e.set_track_sink(track, sink.sink_id());

    omega_event_t ev = omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 0);
    e.enqueue(AddEventCmd{track, ev});

    // Set a loop region then immediately clear it
    REQUIRE(e.loop_set(0, 960) == OMEGA_OK);
    REQUIRE(e.loop_clear() == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    clock.advance_ticks(1);
    e.process();
    REQUIRE(sink.has_note_on(60, 0));
    sink.clear();

    // Advance past where the loop would have wrapped — no wrap should occur
    clock.advance_ticks(960);
    e.process();
    REQUIRE_FALSE(sink.has_note_on(60, 0));
}

TEST_CASE("loop_enable(false) stops wrapping; loop_enable(true) resumes it")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    TrackId track = e.add_track("t");
    e.set_track_sink(track, sink.sink_id());

    omega_event_t ev = omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 0);
    e.enqueue(AddEventCmd{track, ev});

    REQUIRE(e.loop_set(0, 960) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    // ── First cycle: note at tick 0 fires
    clock.advance_ticks(1);
    e.process();
    REQUIRE(sink.has_note_on(60, 0));
    sink.clear();

    // ── Disable looping mid-playback; advance past loop end
    REQUIRE(e.loop_enable(false) == OMEGA_OK);
    clock.advance_ticks(960);
    e.process();
    // Loop disabled — no wrap, note should not fire
    REQUIRE_FALSE(sink.has_note_on(60, 0));
    sink.clear();

    // ── Re-enable; next cycle past end_tick (now we're far past 960 already)
    // Locate back to 0 manually so the next wrap is observable
    REQUIRE(e.enqueue(TransportCmd{TransportAction::LOCATE, 0u}) == OMEGA_OK);
    REQUIRE(e.loop_enable(true) == OMEGA_OK);
    clock.advance_ticks(1);
    e.process();  // applies locate + loop_enable, advances to ~0 ticks
    sink.clear();

    // Now advance past the loop end again — should wrap and fire note
    clock.advance_ticks(960);
    e.process();
    REQUIRE(sink.has_note_on(60, 0));
}

TEST_CASE("non-zero loop start: transport wraps from end to start (not tick 0)")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    TrackId track = e.add_track("t");
    e.set_track_sink(track, sink.sink_id());

    // Note at tick 480 (one beat into the loop, which starts at 480)
    omega_event_t ev = omega_make_note_on(480u, sink.sink_id(), 0, 72, 100, 0);
    e.enqueue(AddEventCmd{track, ev});

    // Loop: ticks 480 → 1440
    REQUIRE(e.loop_set(480, 1440) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::LOCATE, 480u}) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    clock.advance_ticks(1);
    e.process();
    REQUIRE(sink.has_note_on(72, 0));
    sink.clear();

    // Advance 960 more ticks: current tick = 481 → 1441, which >= loop end (1440) → wrap
    clock.advance_ticks(960);
    e.process();
    // After wrap to tick 480, advance(480) fires the note again
    REQUIRE(sink.has_note_on(72, 0));
}
