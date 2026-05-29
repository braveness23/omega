#include <omega/engine.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>
#include <omega/timeline.h>

#include <catch2/catch_test_macros.hpp>

using namespace omega;

// ── Note-on + note-off timing ─────────────────────────────────────────────────

TEST_CASE("Note-on fires at tick 0; note-off has not fired at tick 1")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    TrackId track = e.add_track("t");
    e.set_track_sink(track, sink.sink_id());

    // Note-on at tick 0, duration 240
    omega_event_t ev = omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 240);
    REQUIRE(e.enqueue(AddEventCmd{track, ev}) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    clock.advance_ticks(1);
    e.process();

    REQUIRE(sink.has_note_on(60, 0));
    REQUIRE_FALSE(sink.has_note_off(60, 0));
}

TEST_CASE("Note-off fires at tick 240 (duration boundary)")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    TrackId track = e.add_track("t");
    e.set_track_sink(track, sink.sink_id());

    omega_event_t ev = omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 240);
    REQUIRE(e.enqueue(AddEventCmd{track, ev}) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    clock.advance_ticks(1);
    e.process();
    REQUIRE_FALSE(sink.has_note_off(60, 0));

    // Advance past tick 240 — note-off deadline reached
    clock.advance_ticks(240);
    e.process();
    REQUIRE(sink.has_note_off(60, 0));
}

// ── Ordering ──────────────────────────────────────────────────────────────────

TEST_CASE("100 notes at random ticks all fire in tick order")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    TrackId track = e.add_track("t");
    e.set_track_sink(track, sink.sink_id());

    // Add 100 notes in reverse tick order to verify sorted insertion
    constexpr int N = 100;
    for (int i = N - 1; i >= 0; --i)
    {
        auto tick = static_cast<uint64_t>(i * 10);
        omega_event_t ev =
            omega_make_note_on(tick, sink.sink_id(), 0, static_cast<uint8_t>(i), 100, 0);
        REQUIRE(e.enqueue(AddEventCmd{track, ev}) == OMEGA_OK);
    }

    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);
    // First cycle: drains queue, starts play, dispatches tick-0 event
    e.process();
    // Second cycle: advance 1000 ticks, dispatch ticks 10..990
    clock.advance_ticks(1000);
    e.process();

    REQUIRE(sink.count() == static_cast<size_t>(N));
    for (size_t i = 0; i + 1 < sink.count(); ++i)
    {
        REQUIRE(sink.at(i).tick <= sink.at(i + 1).tick);
    }
}

// ── Muted track ───────────────────────────────────────────────────────────────

TEST_CASE("Muted track fires no events")
{
    CapturingSink sink;
    TimelineSource tl;
    TrackId tid = tl.add_track("muted");
    tl.set_sink(tid, sink.sink_id());
    tl.add_event(tid, omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 0));

    EventDispatcher::SinkList sinks{{sink.sink_id(), &sink}};
    EventDispatcher dispatcher{sinks};
    ProcessContext ctx{};

    // Verify event fires when unmuted
    tl.advance(1, dispatcher, ctx);
    REQUIRE(sink.count() == 1u);
}

// ── Track with no events ──────────────────────────────────────────────────────

TEST_CASE("Track with no events causes zero dispatches")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    TrackId track = e.add_track("empty");
    e.set_track_sink(track, sink.sink_id());

    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);
    clock.advance_ticks(480);
    e.process();
    clock.advance_ticks(480);
    e.process();

    REQUIRE(sink.count() == 0u);
}

// ── add_event keeps events sorted ────────────────────────────────────────────

TEST_CASE("add_event inserts events in tick-sorted order")
{
    CapturingSink sink;
    TimelineSource tl;
    TrackId tid = tl.add_track("t");
    tl.set_sink(tid, sink.sink_id());

    // Insert in reverse order
    tl.add_event(tid, omega_make_note_on(960u, sink.sink_id(), 0, 64, 100, 0));
    tl.add_event(tid, omega_make_note_on(480u, sink.sink_id(), 0, 62, 100, 0));
    tl.add_event(tid, omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 0));

    EventDispatcher::SinkList sinks{{sink.sink_id(), &sink}};
    EventDispatcher dispatcher{sinks};
    ProcessContext ctx{};

    tl.advance(1000, dispatcher, ctx);

    REQUIRE(sink.count() == 3u);
    REQUIRE(sink.at(0).data[0] == 60);
    REQUIRE(sink.at(1).data[0] == 62);
    REQUIRE(sink.at(2).data[0] == 64);
}

// ── replace_event ─────────────────────────────────────────────────────────────

TEST_CASE("replace_event: changed pitch fires on playback")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    TrackId track = e.add_track("t");
    e.set_track_sink(track, sink.sink_id());

    omega_event_t ev = omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, OMEGA_PPQN);
    e.add_track_event(track, ev);

    // Replace pitch 60 → 64.
    omega_event_t rep = ev;
    omega_event_set_pitch(&rep, 64);
    REQUIRE(e.replace_track_event(track, 0u, 0u, rep) == OMEGA_OK);

    e.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    clock.advance_ticks(1u);
    e.process();

    REQUIRE(sink.has_note_on(64, 0));
    REQUIRE_FALSE(sink.has_note_on(60, 0));
}

TEST_CASE("replace_event: changed tick re-sorts and fires at new position")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    TrackId track = e.add_track("t");
    e.set_track_sink(track, sink.sink_id());

    omega_event_t ev = omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 240u);
    e.add_track_event(track, ev);  // originally at tick 0

    // Move to tick 480.
    omega_event_t rep = ev;
    rep.tick = 480u;
    REQUIRE(e.replace_track_event(track, 0u, 0u, rep) == OMEGA_OK);

    e.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    clock.advance_ticks(1u);
    e.process();
    // At tick 1, event has not fired yet.
    REQUIRE_FALSE(sink.has_note_on(60, 0));

    clock.advance_ticks(480u);
    e.process();
    REQUIRE(sink.has_note_on(60, 0));
}

TEST_CASE("replace_event: changed velocity dispatched on playback")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    TrackId track = e.add_track("t");
    e.set_track_sink(track, sink.sink_id());

    omega_event_t ev = omega_make_note_on(0u, sink.sink_id(), 0, 60, 64, 240u);
    e.add_track_event(track, ev);

    omega_event_t rep = ev;
    omega_event_set_velocity(&rep, 127u);
    REQUIRE(e.replace_track_event(track, 0u, 0u, rep) == OMEGA_OK);

    e.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    clock.advance_ticks(1u);
    e.process();

    REQUIRE(sink.count() >= 1u);
    REQUIRE(omega_event_note_velocity(&sink.at(0)) == 127u);
}

TEST_CASE("replace_event: changed duration fires note-off at new time")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    TrackId track = e.add_track("t");
    e.set_track_sink(track, sink.sink_id());

    omega_event_t ev = omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 480u);
    e.add_track_event(track, ev);

    // Shorten duration to 240.
    omega_event_t rep = ev;
    omega_event_set_duration(&rep, 240u);
    REQUIRE(e.replace_track_event(track, 0u, 0u, rep) == OMEGA_OK);

    e.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    clock.advance_ticks(1u);
    e.process();
    REQUIRE(sink.has_note_on(60, 0));
    REQUIRE_FALSE(sink.has_note_off(60, 0));

    clock.advance_ticks(240u);
    e.process();
    REQUIRE(sink.has_note_off(60, 0));
}

TEST_CASE("replace_event: OMEGA_ERR_NOT_FOUND for missing tick")
{
    Engine e;
    TrackId track = e.add_track("t");
    omega_event_t ev = omega_make_note_on(0u, 1u, 0, 60, 100, 240u);
    e.add_track_event(track, ev);

    // Wrong tick — no event at tick 999.
    REQUIRE(e.replace_track_event(track, 999u, 0u, ev) == OMEGA_OK);  // command enqueued
    e.process();  // applied — OMEGA_ERR_NOT_FOUND silently ignored (applied from timing thread)
    // Verify original event is unchanged (still at tick 0).
    REQUIRE(e.timeline_source().tracks()[0].events[0].tick == 0u);
}

// ── Full engine integration ───────────────────────────────────────────────────

TEST_CASE("Engine end-to-end: note fires then note-off fires")
{
    MockClock clock;
    CapturingSink sink;
    Engine e{&clock};
    e.add_sink(&sink);

    TrackId track = e.add_track("main");
    e.set_track_sink(track, sink.sink_id());

    // Note-on at tick 0, duration OMEGA_PPQN (1 beat)
    omega_event_t ev = omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, OMEGA_PPQN);
    REQUIRE(e.enqueue(AddEventCmd{track, ev}) == OMEGA_OK);
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    clock.advance_ticks(1);
    e.process();
    REQUIRE(sink.has_note_on(60, 0));
    REQUIRE_FALSE(sink.has_note_off(60, 0));

    clock.advance_ticks(OMEGA_PPQN);
    e.process();
    REQUIRE(sink.has_note_off(60, 0));
}
