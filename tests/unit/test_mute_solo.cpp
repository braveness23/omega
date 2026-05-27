/*
 * Tests for built-in mute/solo with correct note-off handling (issue #31).
 *
 * Verifies that:
 *  - Muted channels do not receive note-on events.
 *  - Muting a channel mid-note sends immediate note-off before suppression.
 *  - Unmuting a channel restores normal event delivery.
 *  - Solo-exclusive mode: only soloed channels produce output.
 *  - Engaging solo mid-note flushes active notes on non-soloed channels.
 *  - channel 0xFF targets all channels.
 *  - Invalid channel values return OMEGA_ERR_INVALID.
 *  - Mute/solo commands return OMEGA_OK when enqueued.
 */

#include <omega/commands.h>
#include <omega/engine.h>
#include <omega/omega.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>

#include <catch2/catch_test_macros.hpp>

using namespace omega;

namespace
{

// Standard fixture: engine with a MockClock, one CapturingSink.
struct Fixture
{
    MockClock clock;
    Engine engine{&clock};
    CapturingSink sink;
    uint32_t sid{0};

    Fixture() : sid{sink.sink_id()} { engine.add_sink(&sink); }

    // Convenience: play one pattern on perf slot 0 and advance past the first tick.
    PatternId play_pattern_with_note(uint8_t note = 60,
                                     uint8_t channel = 0,
                                     uint64_t length_ticks = 480u)
    {
        PatternId pat = engine.create_pattern("p", length_ticks);
        engine.pattern_add_event(pat,
                                 omega_make_note_on(0u, sid, channel, note, 100, length_ticks));
        engine.perf_assign(0u, pat);
        engine.perf_cue(0u, CueMode::IMMEDIATE);
        engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
        engine.process();  // apply commands, start playing
        clock.advance_ticks(1u);
        engine.process();  // dispatch note-on at tick 0
        return pat;
    }
};

}  // namespace

// ── Command validation ────────────────────────────────────────────────────────

TEST_CASE("sink_set_mute: OMEGA_ERR_INVALID for channel out of range")
{
    Engine e;
    REQUIRE(e.sink_set_mute(1u, 16u, true) == OMEGA_ERR_INVALID);  // ch 16 is invalid
    REQUIRE(e.sink_set_mute(1u, 0xFEu, true) == OMEGA_ERR_INVALID);
    REQUIRE(e.sink_set_mute(1u, 0u, true) == OMEGA_OK);     // ch 0 valid
    REQUIRE(e.sink_set_mute(1u, 15u, true) == OMEGA_OK);    // ch 15 valid
    REQUIRE(e.sink_set_mute(1u, 0xFFu, true) == OMEGA_OK);  // 0xFF = all valid
}

TEST_CASE("sink_set_solo: OMEGA_ERR_INVALID for channel out of range")
{
    Engine e;
    REQUIRE(e.sink_set_solo(1u, 16u, true) == OMEGA_ERR_INVALID);
    REQUIRE(e.sink_set_solo(1u, 0xFEu, true) == OMEGA_ERR_INVALID);
    REQUIRE(e.sink_set_solo(1u, 0u, true) == OMEGA_OK);
    REQUIRE(e.sink_set_solo(1u, 0xFFu, true) == OMEGA_OK);
}

// ── Mute: suppresses note-ons on the muted channel ───────────────────────────

TEST_CASE("mute channel 0: note-on on ch 0 is suppressed")
{
    Fixture f;
    PatternId pat = f.engine.create_pattern("p", 480u);
    f.engine.pattern_add_event(pat, omega_make_note_on(0u, f.sid, 0, 60, 100, 480u));
    f.engine.perf_assign(0u, pat);
    f.engine.perf_cue(0u, CueMode::IMMEDIATE);
    f.engine.sink_set_mute(f.sid, 0u, true);  // mute ch 0
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();

    f.clock.advance_ticks(10u);
    f.engine.process();

    // Note should be suppressed.
    REQUIRE_FALSE(f.sink.has_note_on(60u));
}

TEST_CASE("mute channel 0: note-on on ch 1 still passes")
{
    Fixture f;
    PatternId pat = f.engine.create_pattern("p", 480u);
    f.engine.pattern_add_event(pat, omega_make_note_on(0u, f.sid, 0, 60, 100, 480u));  // ch 0
    f.engine.pattern_add_event(pat, omega_make_note_on(0u, f.sid, 1, 64, 100, 480u));  // ch 1
    f.engine.perf_assign(0u, pat);
    f.engine.perf_cue(0u, CueMode::IMMEDIATE);
    f.engine.sink_set_mute(f.sid, 0u, true);  // only ch 0 muted
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();

    f.clock.advance_ticks(10u);
    f.engine.process();

    REQUIRE_FALSE(f.sink.has_note_on(60u));  // ch 0 suppressed
    REQUIRE(f.sink.has_note_on(64u, 1u));    // ch 1 passes through
}

// ── Mute mid-note: immediate note-off ────────────────────────────────────────

TEST_CASE("muting mid-note sends immediate note-off")
{
    Fixture f;
    // Start playback and fire a note on ch 0.
    f.play_pattern_with_note(60u, 0u, 960u);

    // C4 should be playing.
    REQUIRE(f.sink.has_note_on(60u));
    f.sink.clear();

    // Now mute ch 0 while the note is still sustaining.
    f.engine.sink_set_mute(f.sid, 0u, true);
    f.engine.process();  // applies the mute command; should send immediate note-off

    REQUIRE(f.sink.has_note_off(60u));  // immediate note-off was sent
}

TEST_CASE("muting already-muted channel does not double-flush")
{
    Fixture f;
    f.play_pattern_with_note(60u, 0u, 960u);
    f.engine.sink_set_mute(f.sid, 0u, true);
    f.engine.process();  // first mute — sends note-off

    f.sink.clear();
    f.engine.sink_set_mute(f.sid, 0u, true);
    f.engine.process();  // already muted — nothing new to flush

    REQUIRE_FALSE(f.sink.has_note_off(60u));  // no second note-off
}

// ── Unmute: resumes normal event delivery ─────────────────────────────────────

TEST_CASE("unmuting a channel allows note-ons through again")
{
    Fixture f;
    PatternId pat = f.engine.create_pattern("p", 960u);
    f.engine.pattern_add_event(pat, omega_make_note_on(0u, f.sid, 0, 60, 100, 480u));
    f.engine.pattern_add_event(pat, omega_make_note_on(480u, f.sid, 0, 64, 100, 480u));
    f.engine.perf_assign(0u, pat);
    f.engine.perf_cue(0u, CueMode::IMMEDIATE);
    f.engine.sink_set_mute(f.sid, 0u, true);
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();

    // Advance past first note (tick 0) while muted.
    f.clock.advance_ticks(10u);
    f.engine.process();
    REQUIRE_FALSE(f.sink.has_note_on(60u));

    // Unmute, then advance to second note (tick 480).
    f.engine.sink_set_mute(f.sid, 0u, false);
    f.clock.advance_ticks(490u);
    f.engine.process();

    REQUIRE(f.sink.has_note_on(64u));  // E4 passes through after unmute
}

// ── channel 0xFF: all channels ────────────────────────────────────────────────

TEST_CASE("mute 0xFF mutes all channels")
{
    Fixture f;
    PatternId pat = f.engine.create_pattern("p", 480u);
    for (uint8_t ch = 0; ch < 4; ++ch)
    {
        f.engine.pattern_add_event(
            pat, omega_make_note_on(0u, f.sid, ch, static_cast<uint8_t>(60u + ch), 100, 480u));
    }
    f.engine.perf_assign(0u, pat);
    f.engine.perf_cue(0u, CueMode::IMMEDIATE);
    f.engine.sink_set_mute(f.sid, 0xFFu, true);
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();

    f.clock.advance_ticks(10u);
    f.engine.process();

    for (uint8_t note = 60u; note < 64u; ++note)
    {
        REQUIRE_FALSE(f.sink.has_note_on(note, static_cast<uint8_t>(note - 60u)));
    }
}

TEST_CASE("mute 0xFF flushes all active notes on all channels")
{
    Fixture f;
    PatternId pat = f.engine.create_pattern("p", 960u);
    f.engine.pattern_add_event(pat, omega_make_note_on(0u, f.sid, 0, 60, 100, 960u));
    f.engine.pattern_add_event(pat, omega_make_note_on(0u, f.sid, 1, 64, 100, 960u));
    f.engine.perf_assign(0u, pat);
    f.engine.perf_cue(0u, CueMode::IMMEDIATE);
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();

    f.clock.advance_ticks(10u);
    f.engine.process();

    // Both notes playing.
    REQUIRE(f.sink.has_note_on(60u));
    REQUIRE(f.sink.has_note_on(64u, 1u));
    f.sink.clear();

    // Mute all channels — should flush both notes.
    f.engine.sink_set_mute(f.sid, 0xFFu, true);
    f.engine.process();

    REQUIRE(f.sink.has_note_off(60u));
    REQUIRE(f.sink.has_note_off(64u, 1u));
}

// ── Solo ──────────────────────────────────────────────────────────────────────

TEST_CASE("solo channel 0: only ch 0 events pass")
{
    Fixture f;
    PatternId pat = f.engine.create_pattern("p", 480u);
    f.engine.pattern_add_event(pat, omega_make_note_on(0u, f.sid, 0, 60, 100, 480u));  // ch 0
    f.engine.pattern_add_event(pat, omega_make_note_on(0u, f.sid, 1, 64, 100, 480u));  // ch 1
    f.engine.perf_assign(0u, pat);
    f.engine.perf_cue(0u, CueMode::IMMEDIATE);
    f.engine.sink_set_solo(f.sid, 0u, true);  // solo ch 0
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();

    f.clock.advance_ticks(10u);
    f.engine.process();

    REQUIRE(f.sink.has_note_on(60u));            // ch 0 — soloed, passes
    REQUIRE_FALSE(f.sink.has_note_on(64u, 1u));  // ch 1 — not soloed, suppressed
}

TEST_CASE("solo mid-note: active notes on non-soloed channels get note-off")
{
    Fixture f;
    // Start with two channels playing.
    PatternId pat = f.engine.create_pattern("p", 960u);
    f.engine.pattern_add_event(pat, omega_make_note_on(0u, f.sid, 0, 60, 100, 960u));  // ch 0
    f.engine.pattern_add_event(pat, omega_make_note_on(0u, f.sid, 1, 64, 100, 960u));  // ch 1
    f.engine.perf_assign(0u, pat);
    f.engine.perf_cue(0u, CueMode::IMMEDIATE);
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();

    f.clock.advance_ticks(10u);
    f.engine.process();

    REQUIRE(f.sink.has_note_on(60u));
    REQUIRE(f.sink.has_note_on(64u, 1u));
    f.sink.clear();

    // Solo ch 0 — ch 1 should get an immediate note-off.
    f.engine.sink_set_solo(f.sid, 0u, true);
    f.engine.process();

    REQUIRE(f.sink.has_note_off(64u, 1u));    // E4 on ch 1 silenced immediately
    REQUIRE_FALSE(f.sink.has_note_off(60u));  // C4 on ch 0 keeps playing
}

TEST_CASE("clearing solo restores normal (mute-based) filtering")
{
    Fixture f;
    PatternId pat = f.engine.create_pattern("p", 960u);
    f.engine.pattern_add_event(pat, omega_make_note_on(0u, f.sid, 0, 60, 100, 480u));
    f.engine.pattern_add_event(pat, omega_make_note_on(480u, f.sid, 1, 64, 100, 480u));
    f.engine.perf_assign(0u, pat);
    f.engine.perf_cue(0u, CueMode::IMMEDIATE);
    f.engine.sink_set_solo(f.sid, 0u, true);  // solo ch 0
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();

    f.clock.advance_ticks(10u);
    f.engine.process();
    REQUIRE(f.sink.has_note_on(60u));  // ch 0 plays

    // Clear solo — ch 1 event at tick 480 should now pass through.
    f.engine.sink_set_solo(f.sid, 0u, false);
    f.clock.advance_ticks(490u);
    f.engine.process();

    REQUIRE(f.sink.has_note_on(64u, 1u));  // ch 1 plays after solo cleared
}

// ── SetSinkMuteCmd / SetSinkSoloCmd size guard ────────────────────────────────

TEST_CASE("SetSinkMuteCmd and SetSinkSoloCmd fit in Command variant limit")
{
    static_assert(sizeof(Command) <= 64, "Command must fit within 64 bytes");
    REQUIRE(sizeof(SetSinkMuteCmd) <= 64u);
    REQUIRE(sizeof(SetSinkSoloCmd) <= 64u);
}
