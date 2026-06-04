// Tests for the graphical-editor C APIs: windowed bulk read (F8), stable event
// identity (F9), and edit-group / grouped undo (F10).

#include <omega/omega.h>
#include <omega/test/capturing_sink.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>

namespace
{

omega_sink_t* as_sink(omega::CapturingSink& s)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<omega_sink_t*>(&s);
}

// Creates an engine with one track holding NOTE_ON events at the given ticks
// (pitch 60, vel 100, duration 240). Applies them with a single process().
struct Fixture
{
    omega_engine_t* e{omega_engine_create()};
    omega::CapturingSink sink;
    omega_track_id_t track{0};

    explicit Fixture(std::initializer_list<omega_tick_t> ticks)
    {
        omega_engine_add_sink(e, as_sink(sink));
        omega_engine_add_track(e, "t", &track);
        omega_engine_set_track_sink(e, track, sink.sink_id());
        for (omega_tick_t t : ticks)
        {
            omega_event_t ev = omega_make_note_on(t, sink.sink_id(), 0, 60, 100, 240u);
            omega_engine_add_event(e, track, ev);
        }
        omega_engine_process(e);  // drain the AddEventCmds
    }
    ~Fixture() { omega_engine_destroy(e); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    Fixture(Fixture&&) = delete;
    Fixture& operator=(Fixture&&) = delete;
};

}  // namespace

// ── F8: omega_engine_track_copy_events ────────────────────────────────────────

TEST_CASE("F8: copy_events null engine is OMEGA_ERR_INVALID")
{
    std::array<omega_event_t, 4> buf{};
    REQUIRE(omega_engine_track_copy_events(
                nullptr, 1u, 0u, 1000u, 0xFFu, buf.data(), nullptr, 4u, nullptr) ==
            OMEGA_ERR_INVALID);
}

TEST_CASE("F8: copy_events unknown track is OMEGA_ERR_NOT_FOUND")
{
    Fixture fx{0u};
    std::array<omega_event_t, 4> buf{};
    REQUIRE(omega_engine_track_copy_events(
                fx.e, 999u, 0u, 1000u, 0xFFu, buf.data(), nullptr, 4u, nullptr) ==
            OMEGA_ERR_NOT_FOUND);
}

TEST_CASE("F8: copy_events returns only the events inside the lo..hi window")
{
    Fixture fx{0u, 480u, 960u};
    std::array<omega_event_t, 8> buf{};
    std::array<omega_event_id_t, 8> ids{};
    size_t total = 0;
    // Window [0, 700) covers ticks 0 and 480 but not 960.
    omega_status_t st = omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 700u, 0xFFu, buf.data(), ids.data(), 8u, &total);
    REQUIRE(st == OMEGA_OK);
    REQUIRE(total == 2u);
    REQUIRE(buf[0].tick == 0u);
    REQUIRE(buf[1].tick == 480u);
    // Ids are assigned, distinct, and never the invalid sentinel.
    REQUIRE(ids[0] != OMEGA_INVALID_EVENT_ID);
    REQUIRE(ids[1] != OMEGA_INVALID_EVENT_ID);
    REQUIRE(ids[0] != ids[1]);
}

TEST_CASE("F8: copy_events reports full total even when cap truncates")
{
    Fixture fx{0u, 480u, 960u};
    std::array<omega_event_t, 1> buf{};
    size_t total = 0;
    omega_status_t st = omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, buf.data(), nullptr, 1u, &total);
    REQUIRE(st == OMEGA_OK);
    REQUIRE(total == 3u);        // all three matched ...
    REQUIRE(buf[0].tick == 0u);  // ... but only one written (cap == 1)
}

TEST_CASE("F8: copy_events with cap 0 and null buffer just counts")
{
    Fixture fx{0u, 480u, 960u};
    size_t total = 0;
    omega_status_t st = omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, nullptr, nullptr, 0u, &total);
    REQUIRE(st == OMEGA_OK);
    REQUIRE(total == 3u);
}

TEST_CASE("F8: copy_events honours the tag filter")
{
    Fixture fx{0u, 480u};
    // Add a CC event at tick 240.
    omega_event_t cc{};
    cc.tick = 240u;
    cc.sink_id = fx.sink.sink_id();
    cc.payload_tag = OMEGA_CC;
    cc.data[0] = 7;
    cc.data[1] = 100;
    omega_engine_add_event(fx.e, fx.track, cc);
    omega_engine_process(fx.e);

    std::array<omega_event_t, 8> buf{};
    size_t total = 0;
    REQUIRE(omega_engine_track_copy_events(
                fx.e, fx.track, 0u, 10000u, OMEGA_NOTE_ON, buf.data(), nullptr, 8u, &total) ==
            OMEGA_OK);
    REQUIRE(total == 2u);  // the CC is filtered out
}

// ── F9: stable event identity ─────────────────────────────────────────────────

TEST_CASE("F9: replace_event_by_id moves a note while keeping its id")
{
    Fixture fx{0u, 480u};
    std::array<omega_event_t, 8> buf{};
    std::array<omega_event_id_t, 8> ids{};
    size_t total = 0;
    omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, buf.data(), ids.data(), 8u, &total);
    REQUIRE(total == 2u);
    const omega_event_id_t id0 = ids[0];  // event at tick 0

    // Move it past the other event (tick 0 -> 960) — this re-sorts the vector.
    omega_event_t moved = buf[0];
    moved.tick = 960u;
    REQUIRE(omega_engine_replace_event_by_id(fx.e, fx.track, id0, moved) == OMEGA_OK);
    omega_engine_process(fx.e);

    omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, buf.data(), ids.data(), 8u, &total);
    REQUIRE(total == 2u);
    REQUIRE(buf[0].tick == 480u);
    REQUIRE(buf[1].tick == 960u);
    REQUIRE(ids[1] == id0);  // the moved event still carries its original id
}

TEST_CASE("F9: a stale id from a prior snapshot still resolves after others move")
{
    // The whole point of F9: cache two ids, move the first (re-sorting the
    // vector), then the second id must still name the same event.
    Fixture fx{0u, 480u};
    std::array<omega_event_t, 8> buf{};
    std::array<omega_event_id_t, 8> ids{};
    size_t total = 0;
    omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, buf.data(), ids.data(), 8u, &total);
    const omega_event_id_t id_first = ids[0];   // tick 0
    const omega_event_id_t id_second = ids[1];  // tick 480

    // Move the first far right; positional (tick,index) of id_second would now be
    // wrong, but the id is not.
    omega_event_t m = buf[0];
    m.tick = 2000u;
    omega_engine_replace_event_by_id(fx.e, fx.track, id_first, m);
    omega_engine_process(fx.e);

    // Now retune the second by id (transpose to pitch 72) — must still work.
    omega_event_t second = buf[1];
    second.data[0] = 72;
    REQUIRE(omega_engine_replace_event_by_id(fx.e, fx.track, id_second, second) == OMEGA_OK);
    omega_engine_process(fx.e);

    omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, buf.data(), ids.data(), 8u, &total);
    REQUIRE(total == 2u);
    // event at tick 480 is the second one, now pitch 72
    REQUIRE(buf[0].tick == 480u);
    REQUIRE(buf[0].data[0] == 72);
}

TEST_CASE("F9: delete_event_by_id removes the right event")
{
    Fixture fx{0u, 480u, 960u};
    std::array<omega_event_t, 8> buf{};
    std::array<omega_event_id_t, 8> ids{};
    size_t total = 0;
    omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, buf.data(), ids.data(), 8u, &total);
    const omega_event_id_t id_mid = ids[1];  // tick 480

    REQUIRE(omega_engine_delete_event_by_id(fx.e, fx.track, id_mid) == OMEGA_OK);
    omega_engine_process(fx.e);

    omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, buf.data(), ids.data(), 8u, &total);
    REQUIRE(total == 2u);
    REQUIRE(buf[0].tick == 0u);
    REQUIRE(buf[1].tick == 960u);
}

TEST_CASE("F9: replace_event_by_id then undo restores the original")
{
    Fixture fx{0u};
    std::array<omega_event_t, 4> buf{};
    std::array<omega_event_id_t, 4> ids{};
    size_t total = 0;
    omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, buf.data(), ids.data(), 4u, &total);
    const omega_event_id_t id0 = ids[0];

    omega_event_t m = buf[0];
    m.data[0] = 72;  // transpose
    omega_engine_replace_event_by_id(fx.e, fx.track, id0, m);
    omega_engine_process(fx.e);

    omega_engine_undo(fx.e);
    omega_engine_process(fx.e);

    omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, buf.data(), ids.data(), 4u, &total);
    REQUIRE(total == 1u);
    REQUIRE(buf[0].data[0] == 60);  // back to original pitch
}

TEST_CASE("F9: delete_event_by_id then undo restores the event under its id")
{
    Fixture fx{0u, 480u};
    std::array<omega_event_t, 8> buf{};
    std::array<omega_event_id_t, 8> ids{};
    size_t total = 0;
    omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, buf.data(), ids.data(), 8u, &total);
    const omega_event_id_t id0 = ids[0];

    omega_engine_delete_event_by_id(fx.e, fx.track, id0);
    omega_engine_process(fx.e);
    omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, buf.data(), ids.data(), 8u, &total);
    REQUIRE(total == 1u);

    omega_engine_undo(fx.e);
    omega_engine_process(fx.e);
    omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, buf.data(), ids.data(), 8u, &total);
    REQUIRE(total == 2u);
    // Restored under its original id, so it can still be addressed.
    bool found = (ids[0] == id0) || (ids[1] == id0);
    REQUIRE(found);
}

// ── F10: edit groups / grouped undo ───────────────────────────────────────────

TEST_CASE("F10: a grouped multi-edit is reverted by a single undo")
{
    Fixture fx{0u, 480u};
    std::array<omega_event_t, 8> buf{};
    std::array<omega_event_id_t, 8> ids{};
    size_t total = 0;
    omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, buf.data(), ids.data(), 8u, &total);
    const omega_event_id_t id0 = ids[0];
    const omega_event_id_t id1 = ids[1];

    // Transpose both notes up an octave as one gesture.
    omega_engine_begin_edit_group(fx.e, "transpose");
    omega_event_t a = buf[0];
    a.data[0] = 72;
    omega_engine_replace_event_by_id(fx.e, fx.track, id0, a);
    omega_event_t b = buf[1];
    b.data[0] = 72;
    omega_engine_replace_event_by_id(fx.e, fx.track, id1, b);
    omega_engine_end_edit_group(fx.e);
    omega_engine_process(fx.e);

    omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, buf.data(), ids.data(), 8u, &total);
    REQUIRE(buf[0].data[0] == 72);
    REQUIRE(buf[1].data[0] == 72);

    // ONE undo reverts the entire group.
    omega_engine_undo(fx.e);
    omega_engine_process(fx.e);
    omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, buf.data(), ids.data(), 8u, &total);
    REQUIRE(buf[0].data[0] == 60);
    REQUIRE(buf[1].data[0] == 60);

    // ONE redo re-applies the entire group.
    omega_engine_redo(fx.e);
    omega_engine_process(fx.e);
    omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, buf.data(), ids.data(), 8u, &total);
    REQUIRE(buf[0].data[0] == 72);
    REQUIRE(buf[1].data[0] == 72);
}

TEST_CASE("F10: ungrouped edits still undo one at a time")
{
    Fixture fx{0u, 480u};
    std::array<omega_event_t, 8> buf{};
    std::array<omega_event_id_t, 8> ids{};
    size_t total = 0;
    omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, buf.data(), ids.data(), 8u, &total);

    omega_event_t a = buf[0];
    a.data[0] = 72;
    omega_engine_replace_event_by_id(fx.e, fx.track, ids[0], a);
    omega_event_t b = buf[1];
    b.data[0] = 72;
    omega_engine_replace_event_by_id(fx.e, fx.track, ids[1], b);
    omega_engine_process(fx.e);

    // One undo reverts only the most recent edit.
    omega_engine_undo(fx.e);
    omega_engine_process(fx.e);
    omega_engine_track_copy_events(
        fx.e, fx.track, 0u, 10000u, 0xFFu, buf.data(), ids.data(), 8u, &total);
    REQUIRE(buf[0].data[0] == 72);  // first edit still applied
    REQUIRE(buf[1].data[0] == 60);  // second edit reverted
}
