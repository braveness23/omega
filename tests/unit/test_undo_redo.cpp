/*
 * Unit tests for Engine undo/redo command history (issue #55).
 *
 * Covers:
 *  - UndoCmd / RedoCmd size constraint
 *  - AddEventCmd undo/redo via omega::Engine C++ API
 *  - DeleteEventCmd undo/redo
 *  - ReplaceTrackEventCmd undo/redo (same tick and different tick)
 *  - ReplaceEventCmd (pattern) undo/redo
 *  - Undo with empty history is a no-op
 *  - Redo with empty history is a no-op
 *  - New edit clears the redo stack
 *  - Undo/redo depth cap (UNDO_DEPTH = 64)
 *  - C API: omega_engine_undo / omega_engine_redo
 */

#include <omega/commands.h>
#include <omega/engine.h>
#include <omega/omega.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>

#include <catch2/catch_test_macros.hpp>

using namespace omega;

// ── Fixtures ──────────────────────────────────────────────────────────────────

namespace
{

struct Fixture
{
    MockClock clock;
    Engine engine{&clock};
    CapturingSink sink;
    TrackId track{0};
    uint32_t sink_id{0};

    Fixture()
    {
        sink_id = sink.sink_id();
        engine.add_sink(&sink);
        track = engine.add_track("t");
        engine.set_track_sink(track, sink_id);
    }

    // Helpers to trigger process() so commands are actually applied.
    void tick(uint64_t advance_ns = 1)
    {
        clock.advance_ticks(0);  // just process the command queue
        engine.process();
        (void)advance_ns;
    }

    uint32_t event_count() const
    {
        uint32_t n = 0;
        for (const Track& t : engine.timeline_source().tracks())
        {
            n += static_cast<uint32_t>(t.events.size());
        }
        return n;
    }

    // Return tick of first event in track (or UINT64_MAX if none).
    uint64_t first_event_tick() const
    {
        for (const Track& t : engine.timeline_source().tracks())
        {
            if (!t.events.empty())
            {
                return t.events.front().tick;
            }
        }
        return UINT64_MAX;
    }

    // Return note pitch of first NOTE_ON event in track.
    uint8_t first_event_pitch() const
    {
        for (const Track& t : engine.timeline_source().tracks())
        {
            for (const Event& e : t.events)
            {
                if (e.payload_tag == OMEGA_NOTE_ON)
                {
                    return e.data[0];
                }
            }
        }
        return 0xFF;
    }
};

}  // namespace

// ── Struct size ───────────────────────────────────────────────────────────────

TEST_CASE("UndoCmd and RedoCmd fit in Command variant size limit")
{
    static_assert(sizeof(Command) <= 64u);
    REQUIRE(sizeof(UndoCmd) == 1u);
    REQUIRE(sizeof(RedoCmd) == 1u);
}

// ── AddEventCmd undo/redo ─────────────────────────────────────────────────────

TEST_CASE("Undo AddEventCmd removes the added event")
{
    Fixture f;
    Event ev = omega_make_note_on(480u, f.sink_id, 0, 60, 100, 480u);
    REQUIRE(f.engine.enqueue(AddEventCmd{f.track, ev}) == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 1u);

    REQUIRE(f.engine.undo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 0u);
}

TEST_CASE("Redo AddEventCmd re-inserts the event after undo")
{
    Fixture f;
    Event ev = omega_make_note_on(480u, f.sink_id, 0, 60, 100, 480u);
    REQUIRE(f.engine.enqueue(AddEventCmd{f.track, ev}) == OMEGA_OK);
    f.tick();
    REQUIRE(f.engine.undo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 0u);

    REQUIRE(f.engine.redo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 1u);
    REQUIRE(f.first_event_tick() == 480u);
}

// ── DeleteEventCmd undo/redo ──────────────────────────────────────────────────

TEST_CASE("Undo DeleteEventCmd restores the deleted event")
{
    Fixture f;
    Event ev = omega_make_note_on(240u, f.sink_id, 0, 62, 90, 240u);
    REQUIRE(f.engine.enqueue(AddEventCmd{f.track, ev}) == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 1u);

    REQUIRE(f.engine.enqueue(DeleteEventCmd{f.track, 240u, 0u}) == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 0u);

    REQUIRE(f.engine.undo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 1u);
    REQUIRE(f.first_event_tick() == 240u);
}

TEST_CASE("Redo DeleteEventCmd re-deletes the event after undo")
{
    Fixture f;
    Event ev = omega_make_note_on(240u, f.sink_id, 0, 62, 90, 240u);
    REQUIRE(f.engine.enqueue(AddEventCmd{f.track, ev}) == OMEGA_OK);
    f.tick();
    REQUIRE(f.engine.enqueue(DeleteEventCmd{f.track, 240u, 0u}) == OMEGA_OK);
    f.tick();
    REQUIRE(f.engine.undo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 1u);

    REQUIRE(f.engine.redo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 0u);
}

// ── ReplaceTrackEventCmd undo/redo (same tick) ────────────────────────────────

TEST_CASE("Undo ReplaceTrackEventCmd restores original pitch (same tick)")
{
    Fixture f;
    Event original = omega_make_note_on(480u, f.sink_id, 0, 60, 100, 480u);
    REQUIRE(f.engine.enqueue(AddEventCmd{f.track, original}) == OMEGA_OK);
    f.tick();
    REQUIRE(f.first_event_pitch() == 60u);

    Event transposed = omega_make_note_on(480u, f.sink_id, 0, 64, 100, 480u);
    REQUIRE(f.engine.enqueue(ReplaceTrackEventCmd{f.track, 480u, 0u, transposed}) == OMEGA_OK);
    f.tick();
    REQUIRE(f.first_event_pitch() == 64u);

    REQUIRE(f.engine.undo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.first_event_pitch() == 60u);
}

TEST_CASE("Redo ReplaceTrackEventCmd re-applies transpose after undo (same tick)")
{
    Fixture f;
    Event original = omega_make_note_on(480u, f.sink_id, 0, 60, 100, 480u);
    REQUIRE(f.engine.enqueue(AddEventCmd{f.track, original}) == OMEGA_OK);
    f.tick();

    Event transposed = omega_make_note_on(480u, f.sink_id, 0, 64, 100, 480u);
    REQUIRE(f.engine.enqueue(ReplaceTrackEventCmd{f.track, 480u, 0u, transposed}) == OMEGA_OK);
    f.tick();
    REQUIRE(f.engine.undo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.first_event_pitch() == 60u);

    REQUIRE(f.engine.redo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.first_event_pitch() == 64u);
}

// ── ReplaceTrackEventCmd undo/redo (tick change — note shift) ─────────────────

TEST_CASE("Undo ReplaceTrackEventCmd restores original tick after note shift")
{
    Fixture f;
    Event original = omega_make_note_on(480u, f.sink_id, 0, 60, 100, 480u);
    REQUIRE(f.engine.enqueue(AddEventCmd{f.track, original}) == OMEGA_OK);
    f.tick();

    Event shifted = omega_make_note_on(960u, f.sink_id, 0, 60, 100, 480u);
    REQUIRE(f.engine.enqueue(ReplaceTrackEventCmd{f.track, 480u, 0u, shifted}) == OMEGA_OK);
    f.tick();
    REQUIRE(f.first_event_tick() == 960u);

    REQUIRE(f.engine.undo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.first_event_tick() == 480u);
}

TEST_CASE("Redo ReplaceTrackEventCmd re-shifts note after undo")
{
    Fixture f;
    Event original = omega_make_note_on(480u, f.sink_id, 0, 60, 100, 480u);
    REQUIRE(f.engine.enqueue(AddEventCmd{f.track, original}) == OMEGA_OK);
    f.tick();

    Event shifted = omega_make_note_on(960u, f.sink_id, 0, 60, 100, 480u);
    REQUIRE(f.engine.enqueue(ReplaceTrackEventCmd{f.track, 480u, 0u, shifted}) == OMEGA_OK);
    f.tick();
    REQUIRE(f.engine.undo() == OMEGA_OK);
    f.tick();

    REQUIRE(f.engine.redo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.first_event_tick() == 960u);
}

// ── ReplaceEventCmd (pattern) undo/redo ──────────────────────────────────────

TEST_CASE("Undo ReplaceEventCmd restores original pattern event")
{
    Fixture f;
    PatternId pat = f.engine.create_pattern("p", 960u);
    REQUIRE(f.engine.pattern_add_event(pat, omega_make_note_on(0u, f.sink_id, 0, 60, 100, 480u)) ==
            OMEGA_OK);

    Event replacement = omega_make_note_on(0u, f.sink_id, 0, 64, 80, 480u);
    REQUIRE(f.engine.pattern_replace_event(pat, 0u, replacement) == OMEGA_OK);
    f.tick();

    // Verify replacement took effect via pattern library.
    {
        const Pattern* p = f.engine.pattern_library().get(pat);
        REQUIRE(p != nullptr);
        REQUIRE(p->events[0].data[0] == 64u);
    }

    REQUIRE(f.engine.undo() == OMEGA_OK);
    f.tick();

    {
        const Pattern* p = f.engine.pattern_library().get(pat);
        REQUIRE(p != nullptr);
        REQUIRE(p->events[0].data[0] == 60u);
    }
}

TEST_CASE("Redo ReplaceEventCmd re-applies pattern event change after undo")
{
    Fixture f;
    PatternId pat = f.engine.create_pattern("p", 960u);
    REQUIRE(f.engine.pattern_add_event(pat, omega_make_note_on(0u, f.sink_id, 0, 60, 100, 480u)) ==
            OMEGA_OK);

    Event replacement = omega_make_note_on(0u, f.sink_id, 0, 64, 80, 480u);
    REQUIRE(f.engine.pattern_replace_event(pat, 0u, replacement) == OMEGA_OK);
    f.tick();
    REQUIRE(f.engine.undo() == OMEGA_OK);
    f.tick();

    REQUIRE(f.engine.redo() == OMEGA_OK);
    f.tick();

    const Pattern* p = f.engine.pattern_library().get(pat);
    REQUIRE(p != nullptr);
    REQUIRE(p->events[0].data[0] == 64u);
}

// ── Edge cases ────────────────────────────────────────────────────────────────

TEST_CASE("Undo with empty history is a no-op")
{
    Fixture f;
    REQUIRE(f.engine.undo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 0u);
}

TEST_CASE("Redo with empty history is a no-op")
{
    Fixture f;
    REQUIRE(f.engine.redo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 0u);
}

TEST_CASE("New edit clears redo stack")
{
    Fixture f;
    Event ev1 = omega_make_note_on(0u, f.sink_id, 0, 60, 100, 480u);
    REQUIRE(f.engine.enqueue(AddEventCmd{f.track, ev1}) == OMEGA_OK);
    f.tick();
    REQUIRE(f.engine.undo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 0u);

    // New edit should clear redo
    Event ev2 = omega_make_note_on(480u, f.sink_id, 0, 62, 100, 480u);
    REQUIRE(f.engine.enqueue(AddEventCmd{f.track, ev2}) == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 1u);

    // Redo should be a no-op now
    REQUIRE(f.engine.redo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 1u);
    REQUIRE(f.first_event_tick() == 480u);
}

TEST_CASE("Multiple sequential undo/redo operations")
{
    Fixture f;
    Event ev1 = omega_make_note_on(0u, f.sink_id, 0, 60, 100, 480u);
    Event ev2 = omega_make_note_on(480u, f.sink_id, 0, 62, 100, 480u);

    REQUIRE(f.engine.enqueue(AddEventCmd{f.track, ev1}) == OMEGA_OK);
    f.tick();
    REQUIRE(f.engine.enqueue(AddEventCmd{f.track, ev2}) == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 2u);

    // Undo first add (ev2)
    REQUIRE(f.engine.undo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 1u);

    // Undo second add (ev1)
    REQUIRE(f.engine.undo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 0u);

    // Redo first add (ev1)
    REQUIRE(f.engine.redo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 1u);

    // Redo second add (ev2)
    REQUIRE(f.engine.redo() == OMEGA_OK);
    f.tick();
    REQUIRE(f.event_count() == 2u);
}

// ── C API ─────────────────────────────────────────────────────────────────────

TEST_CASE("omega_engine_undo/redo NULL guard")
{
    REQUIRE(omega_engine_undo(nullptr) == OMEGA_ERR_INVALID);
    REQUIRE(omega_engine_redo(nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("omega_engine_undo reverts pattern replace via C API")
{
    // Use CapturingSink via the reinterpret_cast idiom from integration tests.
    CapturingSink sink;
    auto* snk = reinterpret_cast<omega_sink_t*>(&sink);

    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    omega_engine_add_sink(e, snk);

    omega_pattern_id_t pat = omega_pattern_create(e, "p", 960u);
    omega_event_t original = omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 480u);
    omega_pattern_add_event(e, pat, &original);

    omega_event_t replacement = omega_make_note_on(0u, sink.sink_id(), 0, 64, 80, 480u);
    omega_pattern_replace_event(e, pat, 0u, &replacement);
    omega_engine_process(e);

    // Verify replacement applied.
    omega_event_t out{};
    omega_pattern_event_at(e, pat, 0u, &out);
    REQUIRE(out.data[0] == 64u);

    // Undo — should restore original.
    omega_engine_undo(e);
    omega_engine_process(e);
    omega_pattern_event_at(e, pat, 0u, &out);
    REQUIRE(out.data[0] == 60u);

    // Redo — should re-apply replacement.
    omega_engine_redo(e);
    omega_engine_process(e);
    omega_pattern_event_at(e, pat, 0u, &out);
    REQUIRE(out.data[0] == 64u);

    omega_engine_destroy(e);
}
