#include <omega/commands.h>
#include <omega/engine.h>
#include <omega/omega.h>
#include <omega/perf_slot.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>

#include <catch2/catch_test_macros.hpp>

using namespace omega;

struct QFixture
{
    MockClock clock;
    Engine engine{&clock};
    CapturingSink sink;
    uint32_t sid{0};

    QFixture() : sid{sink.sink_id()} { engine.add_sink(&sink); }

    PatternId make_pattern(uint64_t length = 960u)
    {
        PatternId pat = engine.create_pattern("p", length);
        engine.pattern_add_event(pat, omega_make_note_on(0u, sid, 0, 60, 100, length));
        return pat;
    }
};

// ── Q1: Slot state query ─────────────────────────────────────────────────────

TEST_CASE("perf_slot_state: EMPTY for unassigned slot")
{
    QFixture f;
    REQUIRE(f.engine.perf_slot_state(0u) == SlotState::EMPTY);
}

TEST_CASE("perf_slot_state: IDLE after assign")
{
    QFixture f;
    PatternId pat = f.make_pattern();
    f.engine.perf_assign(0u, pat);
    f.engine.process();
    REQUIRE(f.engine.perf_slot_state(0u) == SlotState::IDLE);
}

TEST_CASE("perf_slot_state: PLAYING after IMMEDIATE cue and process")
{
    QFixture f;
    PatternId pat = f.make_pattern();
    f.engine.perf_assign(0u, pat);
    f.engine.perf_cue(0u, CueMode::IMMEDIATE);
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();
    REQUIRE(f.engine.perf_slot_state(0u) == SlotState::PLAYING);
}

TEST_CASE("perf_slot_state: QUEUED after AT_BOUNDARY cue")
{
    QFixture f;
    PatternId pat = f.make_pattern(960u);
    f.engine.perf_assign(0u, pat);
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();

    // Advance partway into the pattern so the next boundary is in the future.
    f.clock.advance_ticks(100u);
    f.engine.process();
    REQUIRE(f.engine.perf_slot_state(0u) == SlotState::IDLE);

    // Cue at next boundary — slot should be QUEUED, not yet PLAYING.
    f.engine.perf_cue(0u, CueMode::NEXT_BEAT);
    f.engine.process();
    REQUIRE(f.engine.perf_slot_state(0u) == SlotState::QUEUED);
}

TEST_CASE("perf_slot_state: STOPPING after stop command")
{
    QFixture f;
    PatternId pat = f.make_pattern();
    f.engine.perf_assign(0u, pat);
    f.engine.perf_cue(0u, CueMode::IMMEDIATE);
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();
    f.clock.advance_ticks(10u);
    f.engine.process();

    REQUIRE(f.engine.perf_slot_state(0u) == SlotState::PLAYING);

    f.engine.perf_stop(0u, CueMode::NEXT_BEAT);
    f.engine.process();
    REQUIRE(f.engine.perf_slot_state(0u) == SlotState::STOPPING);
}

TEST_CASE("perf_slot_state: IDLE after slot finishes stopping")
{
    QFixture f;
    PatternId pat = f.make_pattern(480u);
    f.engine.perf_assign(0u, pat);
    f.engine.perf_cue(0u, CueMode::IMMEDIATE);
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();
    f.clock.advance_ticks(10u);
    f.engine.process();

    f.engine.perf_stop(0u, CueMode::IMMEDIATE);
    f.engine.process();
    REQUIRE(f.engine.perf_slot_state(0u) == SlotState::STOPPING);

    f.clock.advance_ticks(1u);
    f.engine.process();
    REQUIRE(f.engine.perf_slot_state(0u) == SlotState::IDLE);
}

TEST_CASE("perf_slot_state: EMPTY after unassign")
{
    QFixture f;
    PatternId pat = f.make_pattern();
    f.engine.perf_assign(0u, pat);
    f.engine.process();
    REQUIRE(f.engine.perf_slot_state(0u) == SlotState::IDLE);

    f.engine.perf_assign(0u, 0u);
    f.engine.process();
    REQUIRE(f.engine.perf_slot_state(0u) == SlotState::EMPTY);
}

TEST_CASE("perf_slot_state: EMPTY for out-of-range slot")
{
    QFixture f;
    REQUIRE(f.engine.perf_slot_state(64u) == SlotState::EMPTY);
    REQUIRE(f.engine.perf_slot_state(999u) == SlotState::EMPTY);
}

// ── Q2: Mute/solo state query ────────────────────────────────────────────────

TEST_CASE("sink_is_muted: false before muting")
{
    QFixture f;
    REQUIRE_FALSE(f.engine.sink_is_muted(f.sid, 0u));
}

TEST_CASE("sink_is_muted: true after mute applied")
{
    QFixture f;
    f.engine.sink_set_mute(f.sid, 0u, true);
    f.engine.process();
    REQUIRE(f.engine.sink_is_muted(f.sid, 0u));
    REQUIRE_FALSE(f.engine.sink_is_muted(f.sid, 1u));
}

TEST_CASE("sink_is_muted: false after unmute applied")
{
    QFixture f;
    f.engine.sink_set_mute(f.sid, 0u, true);
    f.engine.process();
    REQUIRE(f.engine.sink_is_muted(f.sid, 0u));

    f.engine.sink_set_mute(f.sid, 0u, false);
    f.engine.process();
    REQUIRE_FALSE(f.engine.sink_is_muted(f.sid, 0u));
}

TEST_CASE("sink_is_soloed: false before soloing")
{
    QFixture f;
    REQUIRE_FALSE(f.engine.sink_is_soloed(f.sid, 0u));
}

TEST_CASE("sink_is_soloed: true after solo applied")
{
    QFixture f;
    f.engine.sink_set_solo(f.sid, 0u, true);
    f.engine.process();
    REQUIRE(f.engine.sink_is_soloed(f.sid, 0u));
    REQUIRE_FALSE(f.engine.sink_is_soloed(f.sid, 1u));
}

TEST_CASE("sink_is_muted: false for unknown sink_id")
{
    QFixture f;
    REQUIRE_FALSE(f.engine.sink_is_muted(999u, 0u));
}

TEST_CASE("sink_is_muted: false for channel > 15")
{
    QFixture f;
    f.engine.sink_set_mute(f.sid, 0xFFu, true);
    f.engine.process();
    REQUIRE_FALSE(f.engine.sink_is_muted(f.sid, 16u));
}

TEST_CASE("sink_is_soloed: false for unknown sink_id")
{
    QFixture f;
    REQUIRE_FALSE(f.engine.sink_is_soloed(999u, 0u));
}
