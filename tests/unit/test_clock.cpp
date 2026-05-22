#include <omega/clock.h>
#include <omega/engine.h>
#include <omega/test/mock_clock.h>
#include <omega/types.h>

#include <catch2/catch_test_macros.hpp>

using namespace omega;

// ── InternalClock ─────────────────────────────────────────────────────────────

TEST_CASE("InternalClock is monotonic across 1M calls")
{
    InternalClock clock;
    uint64_t prev = clock.now_ns();
    for (int i = 0; i < 1'000'000; ++i)
    {
        uint64_t now = clock.now_ns();
        REQUIRE(now >= prev);
        prev = now;
    }
}

TEST_CASE("InternalClock returns non-zero time")
{
    InternalClock clock;
    REQUIRE(clock.now_ns() > 0u);
}

// ── MockClock ─────────────────────────────────────────────────────────────────

TEST_CASE("MockClock starts at zero")
{
    MockClock clock;
    REQUIRE(clock.now_ns() == 0u);
}

TEST_CASE("MockClock set_ns positions to exact value")
{
    MockClock clock;
    clock.set_ns(999'000'000u);
    REQUIRE(clock.now_ns() == 999'000'000u);
}

TEST_CASE("MockClock advance_beats at 120 BPM")
{
    MockClock clock;
    // At 120 BPM: 1 beat = 500,000,000 ns
    clock.advance_beats(1.0);
    REQUIRE(clock.now_ns() == 500'000'000u);
}

TEST_CASE("MockClock advance_ticks one full beat at 120 BPM")
{
    MockClock clock;
    // At 120 BPM, OMEGA_PPQN ticks = 1 beat = 500,000,000 ns
    clock.advance_ticks(OMEGA_PPQN);
    REQUIRE(clock.now_ns() == 500'000'000u);
}

TEST_CASE("MockClock does not drift over 10000 beat-sized advance_ticks calls")
{
    MockClock clock;
    for (int i = 0; i < 10'000; ++i)
    {
        clock.advance_ticks(OMEGA_PPQN);
    }
    // 10,000 beats * 500,000,000 ns/beat = 5,000,000,000,000 ns
    REQUIRE(clock.now_ns() == 5'000'000'000'000ULL);
}

TEST_CASE("MockClock set_bpm changes advance_ticks rate")
{
    MockClock clock;
    clock.set_bpm(240'000);  // 240 BPM: 1 beat = 250,000,000 ns
    clock.advance_ticks(OMEGA_PPQN);
    REQUIRE(clock.now_ns() == 250'000'000u);
}

TEST_CASE("MockClock advance is cumulative")
{
    MockClock clock;
    clock.advance_beats(1.0);
    clock.advance_beats(1.0);
    REQUIRE(clock.now_ns() == 1'000'000'000u);
}

// ── Engine + MockClock integration ────────────────────────────────────────────

TEST_CASE("Engine accepts MockClock at construction and starts stopped at zero")
{
    MockClock clock;
    Engine e{&clock};
    REQUIRE(e.transport_state() == TransportState::STOPPED);
    REQUIRE(e.transport_position_ns() == 0u);
}

TEST_CASE("Engine position advances exactly with MockClock")
{
    MockClock clock;
    Engine e{&clock};

    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);
    e.process();  // applies PLAY; session_start_ns_ = clock.now_ns() = 0

    clock.advance_beats(1.0);  // 500,000,000 ns
    e.process();

    REQUIRE(e.transport_position_ns() == 500'000'000u);
}

TEST_CASE("Engine position does not advance while stopped with MockClock")
{
    MockClock clock;
    Engine e{&clock};

    e.process();
    clock.advance_beats(10.0);
    e.process();

    REQUIRE(e.transport_position_ns() == 0u);
}

TEST_CASE("Engine LOCATE sets position correctly with MockClock")
{
    MockClock clock;
    Engine e{&clock};

    REQUIRE(e.enqueue(TransportCmd{TransportAction::LOCATE, OMEGA_PPQN}) == OMEGA_OK);
    e.process();

    // locate_tick=480 → at 120 BPM = 500,000,000 ns
    REQUIRE(e.transport_position_ns() == 500'000'000u);
}
