#include <omega/commands.h>
#include <omega/engine.h>
#include <omega/omega.h>
#include <omega/perf_slot.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

using namespace omega;

struct CbRecord
{
    omega_engine_event_t event;
    uint32_t detail;
};

static void collect_cb(omega_engine_event_t event, uint32_t detail, void* userdata)
{
    auto* out = static_cast<std::vector<CbRecord>*>(userdata);
    out->push_back({event, detail});
}

// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
struct CbFixture
{
    MockClock clock;
    Engine engine{&clock};
    CapturingSink sink;
    uint32_t sid{0};
    std::vector<CbRecord> records;

    CbFixture() : sid{sink.sink_id()}
    {
        engine.add_sink(&sink);
        engine.set_event_callback(collect_cb, &records);
    }

    PatternId make_pattern(uint64_t length = 960u)
    {
        PatternId pat = engine.create_pattern("p", length);
        engine.pattern_add_event(pat, omega_make_note_on(0u, sid, 0, 60, 100, length));
        return pat;
    }
};

TEST_CASE("event callback: no callback registered does not crash")
{
    MockClock clock;
    Engine engine{&clock};
    CapturingSink sink;
    engine.add_sink(&sink);

    PatternId pat = engine.create_pattern("p", 480u);
    engine.pattern_add_event(pat, omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 480u));
    engine.perf_assign(0u, pat);
    engine.perf_cue(0u, CueMode::IMMEDIATE);
    engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    engine.process();
    clock.advance_ticks(10u);
    engine.process();
}

TEST_CASE("event callback: SLOT_STARTED on IMMEDIATE cue")
{
    CbFixture f;
    PatternId pat = f.make_pattern();
    f.engine.perf_assign(0u, pat);
    f.engine.perf_cue(0u, CueMode::IMMEDIATE);
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();
    f.clock.advance_ticks(1u);
    f.engine.process();

    bool found = false;
    for (const auto& r : f.records)
    {
        if (r.event == OMEGA_EVENT_SLOT_STARTED && r.detail == 0u)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("event callback: SLOT_STARTED on QUEUED to PLAYING")
{
    CbFixture f;
    PatternId pat = f.make_pattern(960u);
    f.engine.perf_assign(0u, pat);
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();

    f.clock.advance_ticks(100u);
    f.engine.process();

    f.engine.perf_cue(0u, CueMode::NEXT_BEAT);
    f.engine.process();
    REQUIRE(f.engine.perf_slot_state(0u) == SlotState::QUEUED);

    f.records.clear();
    f.clock.advance_ticks(960u);
    f.engine.process();

    bool found = false;
    for (const auto& r : f.records)
    {
        if (r.event == OMEGA_EVENT_SLOT_STARTED && r.detail == 0u)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("event callback: SLOT_STOPPED on STOPPING to IDLE")
{
    CbFixture f;
    PatternId pat = f.make_pattern(960u);
    f.engine.perf_assign(0u, pat);
    f.engine.perf_cue(0u, CueMode::IMMEDIATE);
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();
    f.clock.advance_ticks(10u);
    f.engine.process();

    f.engine.perf_stop(0u, CueMode::IMMEDIATE);
    f.engine.process();
    REQUIRE(f.engine.perf_slot_state(0u) == SlotState::STOPPING);

    f.records.clear();
    f.clock.advance_ticks(1u);
    f.engine.process();

    bool found = false;
    for (const auto& r : f.records)
    {
        if (r.event == OMEGA_EVENT_SLOT_STOPPED && r.detail == 0u)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("event callback: TRANSPORT_STOPPED on stop command")
{
    CbFixture f;
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();

    f.records.clear();
    f.engine.enqueue(TransportCmd{TransportAction::STOP, 0u});
    f.engine.process();

    bool found = false;
    for (const auto& r : f.records)
    {
        if (r.event == OMEGA_EVENT_TRANSPORT_STOPPED && r.detail == 0u)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("event callback: LOOP_WRAPPED with correct count")
{
    CbFixture f;
    PatternId pat = f.make_pattern(480u);
    f.engine.perf_assign(0u, pat);
    f.engine.perf_cue(0u, CueMode::IMMEDIATE);
    f.engine.loop_set(0u, 480u);
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();

    f.records.clear();
    f.clock.advance_ticks(490u);
    f.engine.process();

    bool found = false;
    for (const auto& r : f.records)
    {
        if (r.event == OMEGA_EVENT_LOOP_WRAPPED && r.detail == 1u)
        {
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("event callback: clearing callback prevents notifications")
{
    CbFixture f;
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();

    f.engine.set_event_callback(nullptr, nullptr);
    f.records.clear();

    f.engine.enqueue(TransportCmd{TransportAction::STOP, 0u});
    f.engine.process();

    REQUIRE(f.records.empty());
}

TEST_CASE("event callback: correct slot index in detail")
{
    CbFixture f;
    PatternId pat = f.make_pattern();
    f.engine.perf_assign(3u, pat);
    f.engine.perf_cue(3u, CueMode::IMMEDIATE);
    f.engine.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    f.engine.process();
    f.clock.advance_ticks(1u);
    f.engine.process();

    bool found = false;
    for (const auto& r : f.records)
    {
        if (r.event == OMEGA_EVENT_SLOT_STARTED && r.detail == 3u)
        {
            found = true;
        }
    }
    REQUIRE(found);
}
