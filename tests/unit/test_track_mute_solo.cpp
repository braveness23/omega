/*
 * Tests for per-track mute/solo, rename, and channel on the TimelineSource
 * (distinct from the per-(sink, channel) mute/solo in test_mute_solo.cpp).
 *
 * Verifies that:
 *  - Muting a track suppresses only that track's new events.
 *  - Soloing a track silences all non-soloed tracks.
 *  - track_is_muted/soloed queries reflect applied state.
 *  - Muting mid-note still releases the note's scheduled note-off (no stuck note).
 *  - set_track_name / set_track_channel mutate metadata and validate the id.
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

// Two tracks on channels 0 and 1, each a single 480-tick note at tick 0,
// both routed to one CapturingSink.
struct Fixture
{
    MockClock clock;
    Engine engine{&clock};
    CapturingSink sink;
    uint32_t sid{sink.sink_id()};
    TrackId ta{0};
    TrackId tb{0};

    Fixture()
    {
        engine.add_sink(&sink);
        ta = engine.add_track("A");
        tb = engine.add_track("B");
        engine.set_track_sink(ta, sid);
        engine.set_track_sink(tb, sid);
        engine.add_track_event(ta, omega_make_note_on(0u, sid, 0, 60, 100, 480u));
        engine.add_track_event(tb, omega_make_note_on(0u, sid, 1, 67, 100, 480u));
    }

    void play_first_tick()
    {
        engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
        engine.process();  // apply commands, start playing
        clock.advance_ticks(1u);
        engine.process();  // dispatch tick-0 note-ons
    }
};

}  // namespace

TEST_CASE("per-track mute suppresses only that track")
{
    Fixture f;
    REQUIRE(f.engine.set_track_mute(f.ta, true) == OMEGA_OK);
    f.play_first_tick();
    CHECK_FALSE(f.sink.has_note_on(60, 0));  // muted track
    CHECK(f.sink.has_note_on(67, 1));        // other track plays
}

TEST_CASE("per-track solo silences non-soloed tracks")
{
    Fixture f;
    REQUIRE(f.engine.set_track_solo(f.ta, true) == OMEGA_OK);
    f.play_first_tick();
    CHECK(f.sink.has_note_on(60, 0));        // soloed track
    CHECK_FALSE(f.sink.has_note_on(67, 1));  // non-soloed track silenced
}

TEST_CASE("track mute/solo queries reflect applied state")
{
    Fixture f;
    CHECK_FALSE(f.engine.track_is_muted(f.ta));
    CHECK_FALSE(f.engine.track_is_soloed(f.ta));

    REQUIRE(f.engine.set_track_mute(f.ta, true) == OMEGA_OK);
    REQUIRE(f.engine.set_track_solo(f.ta, true) == OMEGA_OK);
    f.engine.process();  // drains the queue even while stopped

    CHECK(f.engine.track_is_muted(f.ta));
    CHECK(f.engine.track_is_soloed(f.ta));
    CHECK_FALSE(f.engine.track_is_muted(f.tb));
}

TEST_CASE("muting a track mid-note still releases the scheduled note-off")
{
    Fixture f;
    f.play_first_tick();
    REQUIRE(f.sink.has_note_on(60, 0));
    f.sink.clear();

    REQUIRE(f.engine.set_track_mute(f.ta, true) == OMEGA_OK);
    f.clock.advance_ticks(480u);  // reach the note's scheduled off-tick
    f.engine.process();

    CHECK(f.sink.has_note_off(60, 0));  // no stuck note
}

TEST_CASE("set_track_name renames a track and validates the id")
{
    Fixture f;
    REQUIRE(f.engine.set_track_name(f.ta, "Bass") == OMEGA_OK);
    CHECK(f.engine.timeline_source().tracks()[0].name == "Bass");
    CHECK(f.engine.set_track_name(9999u, "x") == OMEGA_ERR_NOT_FOUND);
}

TEST_CASE("set_track_channel sets the channel and validates the id")
{
    Fixture f;
    REQUIRE(f.engine.set_track_channel(f.ta, 9u) == OMEGA_OK);
    CHECK(f.engine.timeline_source().tracks()[0].channel == 9u);
    CHECK(f.engine.set_track_channel(9999u, 0u) == OMEGA_ERR_NOT_FOUND);
}
