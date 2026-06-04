#include <omega/drain_sink.h>
#include <omega/engine.h>
#include <omega/omega.h>
#include <omega/test/mock_clock.h>
#include <omega/test/mock_event_source.h>
#include <omega/types.h>

#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

using namespace omega;

// ── DrainSink unit tests ──────────────────────────────────────────────────────

TEST_CASE("DrainSink: initially empty")
{
    DrainSink ds;
    REQUIRE(ds.empty());
    REQUIRE(ds.size() == 0u);
    REQUIRE(ds.dropped() == 0u);
}

TEST_CASE("DrainSink: send then pop round-trip")
{
    DrainSink ds;
    Event ev = omega_make_note_on(100, 1u, 0, 60, 100, 0);
    ds.send(ev);

    REQUIRE_FALSE(ds.empty());
    REQUIRE(ds.size() == 1u);

    Event out{};
    REQUIRE(ds.pop(out));
    REQUIRE(out.data[0] == 60);    // note pitch
    REQUIRE(out.payload_tag == OMEGA_NOTE_ON);
    REQUIRE(ds.empty());
}

TEST_CASE("DrainSink: pop returns false when empty")
{
    DrainSink ds;
    Event out{};
    REQUIRE_FALSE(ds.pop(out));
}

TEST_CASE("DrainSink: multiple events, FIFO order")
{
    DrainSink ds;
    for (uint8_t n = 60u; n < 64u; ++n)
    {
        ds.send(omega_make_note_on(static_cast<uint64_t>(n), 1u, 0, n, 100, 0));
    }

    REQUIRE(ds.size() == 4u);

    for (uint8_t n = 60u; n < 64u; ++n)
    {
        Event out{};
        REQUIRE(ds.pop(out));
        REQUIRE(out.data[0] == n);
    }
    REQUIRE(ds.empty());
}

TEST_CASE("DrainSink: flush is a no-op")
{
    DrainSink ds;
    ds.send(omega_make_note_on(0, 1u, 0, 60, 100, 0));
    ds.flush();
    REQUIRE(ds.size() == 1u);  // still there
}

TEST_CASE("DrainSink: dropped counter increments when ring is full")
{
    DrainSink ds;
    // Fill the ring (capacity is CAPACITY; usable = CAPACITY-1).
    for (uint32_t i = 0u; i < DrainSink::CAPACITY; ++i)
    {
        ds.send(omega_make_note_on(0, 1u, 0, 60, 100, 0));
    }
    // Ring is full; one more drop.
    ds.send(omega_make_note_on(0, 1u, 0, 60, 100, 0));
    REQUIRE(ds.dropped() >= 1u);
}

// ── Engine integration ────────────────────────────────────────────────────────

TEST_CASE("DrainSink: receives events dispatched by engine")
{
    MockClock clock;
    Engine eng{&clock};

    DrainSink ds;
    eng.add_sink(&ds);

    MockEventSource src;
    src.prime(omega_make_note_on(10, ds.sink_id(), 0, 72, 100, 0));
    src.prime(omega_make_note_on(20, ds.sink_id(), 0, 74, 100, 0));

    eng.add_source(&src, OMEGA_SOURCE_PRIORITY_PLAYBACK);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();  // tick=0, no events yet

    clock.advance_ticks(25);
    eng.process();  // dispatches events at tick 10 and 20

    REQUIRE(ds.size() == 2u);

    Event out{};
    REQUIRE(ds.pop(out));
    REQUIRE(out.data[0] == 72u);
    REQUIRE(ds.pop(out));
    REQUIRE(out.data[0] == 74u);
    REQUIRE(ds.empty());
}

TEST_CASE("DrainSink: producer/consumer from separate threads (TSan)")
{
    DrainSink ds;
    constexpr int N = 10000;
    std::atomic<bool> running{true};

    // Producer thread: sends events
    std::thread producer([&] {
        for (int i = 0; i < N; ++i)
        {
            ds.send(omega_make_note_on(0, 1u, 0, 60, 100, 0));
        }
        running.store(false, std::memory_order_relaxed);
    });

    // Consumer thread: drains events
    int consumed = 0;
    while (running.load(std::memory_order_relaxed) || !ds.empty())
    {
        Event out{};
        if (ds.pop(out))
        {
            ++consumed;
        }
    }

    producer.join();
    // Drain any remaining
    Event out{};
    while (ds.pop(out))
    {
        ++consumed;
    }

    // consumed + dropped == N
    REQUIRE(consumed + static_cast<int>(ds.dropped()) == N);
}
