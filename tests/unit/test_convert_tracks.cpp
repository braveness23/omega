#include <omega/engine.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>
#include <omega/types.h>

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace omega;

// Helper: build a NOTE_ON event for a given tick/sink/channel/note.
static Event make_note(uint64_t tick, uint32_t sink_id, uint8_t channel, uint8_t note)
{
    Event ev = omega_make_note_on(tick, sink_id, channel, note, 80, 0);
    return ev;
}

TEST_CASE("convert_tracks_to_patterns: empty timeline produces no patterns")
{
    Engine engine;
    uint32_t count = engine.convert_tracks_to_patterns(1u, 960u);
    REQUIRE(count == 0u);
    REQUIRE(engine.pattern_library().count() == 0u);
}

TEST_CASE("convert_tracks_to_patterns: one track becomes one pattern in slot 0")
{
    Engine engine;
    TrackId t = engine.add_track("bass");
    engine.add_track_event(t, make_note(0u, 1u, 0, 48));
    engine.add_track_event(t, make_note(240u, 1u, 0, 50));

    uint32_t count = engine.convert_tracks_to_patterns(1u, 960u);
    REQUIRE(count == 1u);
    REQUIRE(engine.pattern_library().count() == 1u);

    // Verify the pattern has the correct length and event count.
    PatternId pid = 0;
    engine.pattern_library().for_each([&](PatternId id, const Pattern& /*pat*/) { pid = id; });
    REQUIRE(pid != 0u);

    const Pattern* pat = engine.pattern_library().get(pid);
    REQUIRE(pat != nullptr);
    REQUIRE(pat->length_ticks == 960u);
    REQUIRE(pat->events.size() == 2u);
    REQUIRE(pat->name == "bass");
}

TEST_CASE("convert_tracks_to_patterns: events are re-routed to the requested sink_id")
{
    Engine engine;
    TrackId t = engine.add_track("lead");
    // Original events use sink_id 99; convert should rewrite to 7.
    Event ev = make_note(0u, 99u, 0, 60);
    engine.add_track_event(t, ev);

    engine.convert_tracks_to_patterns(7u, 480u);

    PatternId pid = 0;
    engine.pattern_library().for_each([&](PatternId id, const Pattern& /*pat*/) { pid = id; });

    const Pattern* pat = engine.pattern_library().get(pid);
    REQUIRE(pat != nullptr);
    REQUIRE(pat->events[0].sink_id == 7u);
}

TEST_CASE("convert_tracks_to_patterns: N tracks → N patterns in slots 0..N-1")
{
    Engine engine;
    std::vector<TrackId> tracks;
    for (int i = 0; i < 4; ++i)
    {
        TrackId tid = engine.add_track("t" + std::to_string(i));
        engine.add_track_event(tid, make_note(static_cast<uint64_t>(i) * 120u, 1u, 0, 60 + i));
        tracks.push_back(tid);
    }

    uint32_t count = engine.convert_tracks_to_patterns(1u, 1920u);
    REQUIRE(count == 4u);
    REQUIRE(engine.pattern_library().count() == 4u);
}

TEST_CASE("convert_tracks_to_patterns: loop_end_ticks sets pattern length")
{
    Engine engine;
    engine.add_track("melody");

    engine.convert_tracks_to_patterns(1u, 3840u);

    engine.pattern_library().for_each(
        [&](PatternId /*id*/, const Pattern& pat) { REQUIRE(pat.length_ticks == 3840u); });
}

TEST_CASE("convert_tracks_to_patterns: track with no events produces empty pattern")
{
    Engine engine;
    engine.add_track("empty");

    uint32_t count = engine.convert_tracks_to_patterns(1u, 480u);
    REQUIRE(count == 1u);

    engine.pattern_library().for_each(
        [&](PatternId /*id*/, const Pattern& pat) { REQUIRE(pat.events.empty()); });
}

TEST_CASE("convert_tracks_to_patterns: track names copied to patterns")
{
    Engine engine;
    engine.add_track("drums");
    engine.add_track("bass");

    engine.convert_tracks_to_patterns(1u, 960u);

    std::vector<std::string> names;
    engine.pattern_library().for_each(
        [&](PatternId /*id*/, const Pattern& pat) { names.push_back(pat.name); });

    REQUIRE(names.size() == 2u);
    // Both names must appear (order in unordered_map is unspecified).
    bool has_drums = std::find(names.begin(), names.end(), "drums") != names.end();
    bool has_bass = std::find(names.begin(), names.end(), "bass") != names.end();
    REQUIRE(has_drums);
    REQUIRE(has_bass);
}
