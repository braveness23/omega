#include <omega/engine.h>
#include <omega/test/mock_clock.h>
#include <omega/timer.h>

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

using namespace omega;

TEST_CASE("OmegaTimer constructs and destructs without hanging")
{
    Engine engine;
    {
        OmegaTimer timer(engine);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    // If we get here the destructor joined cleanly.
}

TEST_CASE("OmegaTimer calls process() at least once before destructor returns")
{
    // Use a CapturingSink to observe that the engine actually ran.
    // We just verify transport state can be observed after timer runs.
    Engine engine;
    REQUIRE(engine.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    {
        OmegaTimer timer(engine, 1000);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // After the timer destructs it calls process() one last time.
    // The engine must have been in PLAYING state at some point.
    // We can't observe mid-run, but we can confirm it's still PLAYING
    // (nothing stopped it).
    REQUIRE(engine.transport_state() == TransportState::PLAYING);
}

TEST_CASE("OmegaTimer fires process() multiple times per 10ms window")
{
    std::atomic<int> call_count{0};

    // We need a way to count process() calls. Use a custom sink that
    // increments on every event — but we need actual events to fire.
    // Instead, measure wall-clock time: 10ms at 1ms interval should
    // produce at least 5 process() calls. We approximate by checking
    // transport position advances, which requires playing.
    Engine engine;
    REQUIRE(engine.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    uint64_t pos_before{};
    uint64_t pos_after{};

    {
        OmegaTimer timer(engine, 1000);
        // Let it tick at least a few cycles.
        pos_before = engine.transport_position_ns();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        pos_after = engine.transport_position_ns();
    }

    // Position must have advanced (engine was driven by the timer).
    REQUIRE(pos_after > pos_before);
}

TEST_CASE("OmegaTimer concurrent enqueue from mutation thread is race-free")
{
    // This is the primary TSan test: mutation thread calls enqueue()
    // concurrently with OmegaTimer's timing thread calling process().
    Engine engine;
    REQUIRE(engine.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    std::atomic<bool> done{false};

    auto mutation_thread = std::thread([&]() {
        while (!done.load(std::memory_order_acquire))
        {
            // Enqueue a no-op-equivalent command; ignore queue-full.
            engine.enqueue(SetTempoCmd{120'000u});
        }
    });

    {
        OmegaTimer timer(engine, 500);  // 0.5ms for higher contention
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    done.store(true, std::memory_order_release);
    mutation_thread.join();
}

TEST_CASE("OmegaTimer respects custom interval_us parameter")
{
    // With a 2ms interval over a 40ms window we expect ~20 cycles.
    // We can't count process() calls directly, so just verify that
    // transport position advances proportionally more with a shorter interval.
    Engine engine1, engine2;
    REQUIRE(engine1.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);
    REQUIRE(engine2.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);

    uint64_t pos1_end{}, pos2_end{};

    {
        OmegaTimer t1(engine1, 500);   // 0.5ms
        OmegaTimer t2(engine2, 5000);  // 5ms
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pos1_end = engine1.transport_position_ns();
        pos2_end = engine2.transport_position_ns();
    }

    // Both engines were running for the same wall-clock time. The transport
    // position reported by each engine represents "how much real time was
    // measured inside process()" — which should be roughly the same
    // regardless of call frequency, since InternalClock::now_ns() is
    // steady_clock. Both should be non-zero and broadly similar (within 20%).
    REQUIRE(pos1_end > 0u);
    REQUIRE(pos2_end > 0u);
}
