#include <omega/commands.h>
#include <omega/engine.h>
#include <omega/omega.h>
#include <omega/test/mock_clock.h>

#include <catch2/catch_test_macros.hpp>

using namespace omega;

// ── Validation ────────────────────────────────────────────────────────────────

TEST_CASE("omega_engine_position returns OMEGA_ERR_INVALID for NULL engine")
{
    omega_position_t pos{};
    REQUIRE(omega_engine_position(nullptr, &pos) == OMEGA_ERR_INVALID);
}

TEST_CASE("omega_engine_position returns OMEGA_ERR_INVALID for NULL out pointer")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    REQUIRE(omega_engine_position(e, nullptr) == OMEGA_ERR_INVALID);
    omega_engine_destroy(e);
}

// ── Initial state ─────────────────────────────────────────────────────────────

TEST_CASE("position snapshot is zero before first process call")
{
    Engine e;
    omega_position_t pos = e.position();
    REQUIRE(pos.tick == 0u);
    REQUIRE(pos.bar == 0u);
    REQUIRE(pos.beat == 0u);
    REQUIRE(pos.subdivision == 0u);
    REQUIRE(pos.loop_count == 0u);
}

// ── Freeform mode ─────────────────────────────────────────────────────────────

TEST_CASE("bar/beat/sub are 0 in freeform mode; tick advances")
{
    MockClock clock;
    Engine e{&clock};

    // No time signature — freeform mode
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    // First cycle: PLAY is applied and session_start = clock.now(); to_tick = 0.
    clock.advance_ticks(1);
    e.process();

    // Second cycle: advance 480 ticks (one beat) → to_tick = 480.
    // 480 is a multiple of 3 so advance_ticks()/ns_to_ticks() round-trips exactly.
    clock.advance_ticks(480);
    e.process();

    omega_position_t pos = e.position();
    REQUIRE(pos.tick == 480u);
    REQUIRE(pos.bar == 0u);
    REQUIRE(pos.beat == 0u);
    REQUIRE(pos.subdivision == 0u);
    REQUIRE(pos.loop_count == 0u);
}

// ── Metered mode ──────────────────────────────────────────────────────────────

TEST_CASE("bar/beat/sub are computed correctly with a time signature")
{
    MockClock clock;
    Engine e{&clock};

    // 4/4 time signature at tick 0
    REQUIRE(e.timesig_set(0, 4, 4) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    // First process cycle: applies SetTimeSigCmd + PLAY; session_start is set to
    // clock.now() so that position = 0.  to_tick = 0 after this cycle.
    clock.advance_ticks(1);
    e.process();

    // Now advance exactly 1920 ticks (one full 4/4 bar) → to_tick = 1920 = bar 2, beat 1.
    clock.advance_ticks(1920);
    e.process();

    omega_position_t pos = e.position();
    REQUIRE(pos.tick == 1920u);
    REQUIRE(pos.bar == 2u);
    REQUIRE(pos.beat == 1u);
    REQUIRE(pos.subdivision == 0u);
}

TEST_CASE("position snapshot at beat boundary")
{
    MockClock clock;
    Engine e{&clock};

    // 4/4
    REQUIRE(e.timesig_set(0, 4, 4) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    // First cycle sets to_tick = 0 (PLAY applied here).
    clock.advance_ticks(1);
    e.process();

    // Advance exactly 480 ticks → to_tick = 480 = bar 1, beat 2, sub 0.
    clock.advance_ticks(480);
    e.process();

    omega_position_t pos = e.position();
    REQUIRE(pos.tick == 480u);
    REQUIRE(pos.bar == 1u);
    REQUIRE(pos.beat == 2u);
    REQUIRE(pos.subdivision == 0u);
}

TEST_CASE("switching from freeform to metered updates bar/beat/sub on next cycle")
{
    MockClock clock;
    Engine e{&clock};

    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);
    clock.advance_ticks(1);
    e.process();

    // Still freeform
    REQUIRE(e.position().bar == 0u);

    // Add a time signature — enqueued, applied on the next process()
    REQUIRE(e.timesig_set(0, 4, 4) == OMEGA_OK);
    clock.advance_ticks(1);
    e.process();

    omega_position_t pos = e.position();
    REQUIRE(pos.bar == 1u);
    REQUIRE(pos.beat == 1u);
}

// ── loop_count ────────────────────────────────────────────────────────────────

TEST_CASE("loop_count starts at 0 and increments each loop wrap")
{
    MockClock clock;
    Engine e{&clock};

    REQUIRE(e.loop_set(0, 960) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    clock.advance_ticks(1);
    e.process();
    REQUIRE(e.position().loop_count == 0u);

    // Advance to the loop end — wraps once
    clock.advance_ticks(960);
    e.process();
    REQUIRE(e.position().loop_count == 1u);

    // Advance another loop length — wraps again
    clock.advance_ticks(960);
    e.process();
    REQUIRE(e.position().loop_count == 2u);
}

TEST_CASE("loop_count resets to 0 when loop_set changes the region")
{
    MockClock clock;
    Engine e{&clock};

    REQUIRE(e.loop_set(0, 960) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    clock.advance_ticks(1);
    e.process();
    clock.advance_ticks(960);
    e.process();
    REQUIRE(e.position().loop_count == 1u);

    // Change the loop region — counter should reset
    REQUIRE(e.loop_set(0, 480) == OMEGA_OK);
    clock.advance_ticks(1);
    e.process();
    REQUIRE(e.position().loop_count == 0u);
}

TEST_CASE("loop_count does not reset when only loop_enable toggles")
{
    MockClock clock;
    Engine e{&clock};

    REQUIRE(e.loop_set(0, 960) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    clock.advance_ticks(1);
    e.process();
    clock.advance_ticks(960);
    e.process();
    REQUIRE(e.position().loop_count == 1u);

    // Disable then re-enable — counter should be preserved
    REQUIRE(e.loop_enable(false) == OMEGA_OK);
    clock.advance_ticks(1);
    e.process();
    REQUIRE(e.position().loop_count == 1u);

    REQUIRE(e.loop_enable(true) == OMEGA_OK);
    clock.advance_ticks(1);
    e.process();
    REQUIRE(e.position().loop_count == 1u);
}

TEST_CASE("loop_count resets to 0 after loop_clear")
{
    MockClock clock;
    Engine e{&clock};

    REQUIRE(e.loop_set(0, 960) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    clock.advance_ticks(1);
    e.process();
    clock.advance_ticks(960);
    e.process();
    REQUIRE(e.position().loop_count == 1u);

    REQUIRE(e.loop_clear() == OMEGA_OK);
    clock.advance_ticks(1);
    e.process();
    REQUIRE(e.position().loop_count == 0u);
}

// ── C API integration ─────────────────────────────────────────────────────────

TEST_CASE("omega_engine_position returns OMEGA_OK and fills struct")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_position_t pos{};
    REQUIRE(omega_engine_position(e, &pos) == OMEGA_OK);
    // Before any process(), all fields are 0
    REQUIRE(pos.tick == 0u);
    REQUIRE(pos.bar == 0u);
    REQUIRE(pos.loop_count == 0u);

    omega_engine_destroy(e);
}
