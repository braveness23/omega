#include <omega/engine.h>
#include <omega/song_arrangement.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>

#include <catch2/catch_test_macros.hpp>

using namespace omega;

// ── Helpers ───────────────────────────────────────────────────────────────────

static void run_to_tick(Engine& e, MockClock& clock, uint64_t target_tick)
{
    // advance_ticks is additive; set_ns is absolute — use set_ns in terms of
    // the default 120 BPM: 1 tick = 500'000'000 ns / 480 ticks ≈ 1'041'666 ns
    // Instead, we just advance in a loop of 1-tick steps to keep it simple.
    // For tests we advance 1 tick past the target so the engine has a positive window.
    clock.advance_ticks(target_tick + 1);
    e.process();
}

// ── Basic playback ────────────────────────────────────────────────────────────

TEST_CASE("SongArrangement: empty arrangement dispatches nothing")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);
    clock.advance_ticks(9600);
    e.process();

    REQUIRE(sink.count() == 0u);
}

TEST_CASE("SongArrangement: single pattern, repeat 1 - note fires at correct tick")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    PatternId pid = e.create_pattern("A", 960u);
    e.pattern_add_event(pid, omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 0));

    REQUIRE(e.song_append(pid, 1) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    // First process drains queue (PLAY + SongAppend).
    e.process();
    // Second process: clock at tick 1 → note at tick 0 fires.
    clock.advance_ticks(1);
    e.process();

    REQUIRE(sink.count() == 1u);
    REQUIRE(sink.has_note_on(60, 0));
    REQUIRE(sink.at(0).tick == 0u);
}

TEST_CASE("SongArrangement: Ax2, Bx1 - four notes at correct absolute ticks")
{
    // Pattern A: length 960, one note (note 60) at tick 0
    // Pattern B: length 1920, note 61 at tick 0, note 62 at tick 960
    // Arrangement: A×2, B×1
    // Expected absolute ticks:
    //   tick 0    — A repeat 0, note 60
    //   tick 960  — A repeat 1, note 60
    //   tick 1920 — B repeat 0, note 61
    //   tick 2880 — B repeat 0, note 62
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    PatternId a = e.create_pattern("A", 960u);
    e.pattern_add_event(a, omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 0));

    PatternId b = e.create_pattern("B", 1920u);
    e.pattern_add_event(b, omega_make_note_on(0u, sink.sink_id(), 0, 61, 100, 0));
    e.pattern_add_event(b, omega_make_note_on(960u, sink.sink_id(), 0, 62, 100, 0));

    REQUIRE(e.song_append(a, 2) == OMEGA_OK);
    REQUIRE(e.song_append(b, 1) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    // First process: drains commands, starts playback, dispatches tick-0 events.
    e.process();
    // Advance through the rest of the arrangement (ticks 1..3840).
    clock.advance_ticks(3841);
    e.process();

    REQUIRE(sink.count() == 4u);

    // Verify each note fired at the correct absolute tick.
    bool found_a0 = false;
    bool found_a1 = false;
    bool found_b0 = false;
    bool found_b1 = false;
    for (size_t i = 0; i < sink.count(); ++i)
    {
        const Event& ev = sink.at(i);
        if (ev.payload_tag == OMEGA_NOTE_ON && ev.data[0] == 60 && ev.tick == 0u)
        {
            found_a0 = true;
        }
        if (ev.payload_tag == OMEGA_NOTE_ON && ev.data[0] == 60 && ev.tick == 960u)
        {
            found_a1 = true;
        }
        if (ev.payload_tag == OMEGA_NOTE_ON && ev.data[0] == 61 && ev.tick == 1920u)
        {
            found_b0 = true;
        }
        if (ev.payload_tag == OMEGA_NOTE_ON && ev.data[0] == 62 && ev.tick == 2880u)
        {
            found_b1 = true;
        }
    }
    REQUIRE(found_a0);
    REQUIRE(found_a1);
    REQUIRE(found_b0);
    REQUIRE(found_b1);
}

TEST_CASE("SongArrangement: events fire in tick order across repetitions")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    PatternId pid = e.create_pattern("P", 960u);
    e.pattern_add_event(pid, omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 0));
    e.pattern_add_event(pid, omega_make_note_on(480u, sink.sink_id(), 0, 64, 100, 0));

    REQUIRE(e.song_append(pid, 3) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    // First process: drains commands, starts playback, dispatches tick-0 events.
    e.process();
    // Advance through the rest of the arrangement (ticks 1..2880).
    clock.advance_ticks(2881);
    e.process();

    REQUIRE(sink.count() == 6u);
    for (size_t i = 0; i + 1 < sink.count(); ++i)
    {
        REQUIRE(sink.at(i).tick <= sink.at(i + 1).tick);
    }

    // Verify absolute ticks: 0, 480, 960, 1440, 1920, 2400
    REQUIRE(sink.at(0).tick == 0u);
    REQUIRE(sink.at(1).tick == 480u);
    REQUIRE(sink.at(2).tick == 960u);
    REQUIRE(sink.at(3).tick == 1440u);
    REQUIRE(sink.at(4).tick == 1920u);
    REQUIRE(sink.at(5).tick == 2400u);
}

// ── repeat_count == 0 ─────────────────────────────────────────────────────────

TEST_CASE("SongArrangement: entry with repeat_count 0 is skipped")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    PatternId skip = e.create_pattern("skip", 960u);
    e.pattern_add_event(skip, omega_make_note_on(0u, sink.sink_id(), 0, 99, 100, 0));

    PatternId play = e.create_pattern("play", 480u);
    e.pattern_add_event(play, omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 0));

    // skip×0 (no-op), play×1
    REQUIRE(e.song_append(skip, 0) == OMEGA_OK);
    REQUIRE(e.song_append(play, 1) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    clock.advance_ticks(481);
    e.process();
    e.process();

    REQUIRE(sink.count() == 1u);
    REQUIRE(sink.at(0).data[0] == 60);  // only the play pattern fires
    REQUIRE(sink.at(0).tick == 0u);     // zero-repeat entry contributes no ticks
}

// ── Note-on + note-off ────────────────────────────────────────────────────────

TEST_CASE("SongArrangement: note-on with duration fires note-off")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    PatternId pid = e.create_pattern("P", 960u);
    // Note-on at tick 0, duration 240
    e.pattern_add_event(pid, omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 240));

    REQUIRE(e.song_append(pid, 1) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    clock.advance_ticks(1);
    e.process();
    REQUIRE(sink.has_note_on(60, 0));
    REQUIRE_FALSE(sink.has_note_off(60, 0));

    clock.advance_ticks(240);
    e.process();
    REQUIRE(sink.has_note_off(60, 0));
}

// ── on_locate ─────────────────────────────────────────────────────────────────

TEST_CASE("SongArrangement: locate into middle of arrangement resumes correctly")
{
    // Pattern A: length 960, note at tick 0
    // Arrangement: A×3 (total 2880 ticks)
    // Locate to tick 1200 (inside second repeat of A, which runs from 960 to 1919)
    // Next note should be the third repeat's note at absolute tick 1920.
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    PatternId pid = e.create_pattern("A", 960u);
    e.pattern_add_event(pid, omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 0));

    REQUIRE(e.song_append(pid, 3) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::LOCATE, 1200u}) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    // Drain commands (nothing dispatched yet)
    e.process();

    // Advance to just past tick 1920 — the third repeat's note-on should fire.
    clock.advance_ticks(1921);
    e.process();

    REQUIRE(sink.count() == 1u);
    REQUIRE(sink.at(0).data[0] == 60);
    REQUIRE(sink.at(0).tick == 1920u);
}

TEST_CASE("SongArrangement: locate past end of arrangement - nothing fires")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    PatternId pid = e.create_pattern("A", 960u);
    e.pattern_add_event(pid, omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 0));

    REQUIRE(e.song_append(pid, 1) == OMEGA_OK);
    // Locate to tick 5000, past the end of A×1 (960 ticks total)
    REQUIRE(e.enqueue(TransportCmd{TransportAction::LOCATE, 5000u}) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    e.process();
    clock.advance_ticks(960);
    e.process();

    REQUIRE(sink.count() == 0u);
}

// ── omega_song_append / omega_song_clear C API ────────────────────────────────

TEST_CASE("SongArrangement: omega_song_append NULL engine returns INVALID")
{
    REQUIRE(omega_song_append(nullptr, 1u, 1u) == OMEGA_ERR_INVALID);
}

TEST_CASE("SongArrangement: omega_song_clear NULL engine returns INVALID")
{
    REQUIRE(omega_song_clear(nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("SongArrangement: omega_song_clear resets arrangement")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    PatternId pid = e.create_pattern("A", 960u);
    e.pattern_add_event(pid, omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 0));

    REQUIRE(e.song_append(pid, 2) == OMEGA_OK);
    REQUIRE(e.song_clear() == OMEGA_OK);  // clears before playback starts

    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    clock.advance_ticks(1921);
    e.process();
    e.process();

    REQUIRE(sink.count() == 0u);
}

TEST_CASE("SongArrangement: C API omega_song_append and omega_song_clear round-trip")
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

    REQUIRE(omega_song_append(eng, pid, 1u) == OMEGA_OK);
    REQUIRE(omega_song_clear(eng) == OMEGA_OK);
    REQUIRE(omega_song_append(eng, pid, 1u) == OMEGA_OK);

    omega_engine_destroy(eng);
}
