#include <omega/engine.h>

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>

using namespace omega;

TEST_CASE("Engine constructs and destructs without leaking")
{
    Engine e;
}

TEST_CASE("Engine initial state is STOPPED and position is 0")
{
    Engine e;
    REQUIRE(e.transport_state() == TransportState::STOPPED);
    REQUIRE(e.transport_position_ns() == 0u);
    REQUIRE(e.transport_position_tick() == 0u);
}

TEST_CASE("Engine PLAY/STOP round-trip via command queue")
{
    Engine e;

    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);
    e.process();
    REQUIRE(e.transport_state() == TransportState::PLAYING);

    REQUIRE(e.enqueue(TransportCmd{TransportAction::STOP, 0u}) == OMEGA_OK);
    e.process();
    REQUIRE(e.transport_state() == TransportState::STOPPED);
}

TEST_CASE("Engine transport_position_ns advances while playing")
{
    Engine e;
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);
    e.process();

    uint64_t pos1 = e.transport_position_ns();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    e.process();
    uint64_t pos2 = e.transport_position_ns();

    REQUIRE(pos2 > pos1);
}

TEST_CASE("Engine position does not advance while stopped")
{
    Engine e;
    e.process();
    uint64_t pos1 = e.transport_position_ns();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    e.process();
    uint64_t pos2 = e.transport_position_ns();

    REQUIRE(pos1 == pos2);
    REQUIRE(pos1 == 0u);
}

// ── transport_position_tick (issue #26) ───────────────────────────────────────

TEST_CASE("Engine transport_position_tick advances while playing")
{
    Engine e;
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);
    e.process();

    uint64_t tick1 = e.transport_position_tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    e.process();
    uint64_t tick2 = e.transport_position_tick();

    REQUIRE(tick2 > tick1);
}

TEST_CASE("Engine transport_position_tick does not advance while stopped")
{
    Engine e;
    e.process();
    uint64_t t1 = e.transport_position_tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    e.process();
    uint64_t t2 = e.transport_position_tick();

    REQUIRE(t1 == t2);
    REQUIRE(t1 == 0u);
}

TEST_CASE("Engine SetTempoCmd is applied on next process() call")
{
    Engine e;
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);
    REQUIRE(e.enqueue(SetTempoCmd{240'000u}) == OMEGA_OK);
    e.process();
    e.process();
    REQUIRE(e.transport_state() == TransportState::PLAYING);
}

TEST_CASE("Engine enqueue returns OMEGA_ERR_QUEUE_FULL at capacity")
{
    Engine e;
    omega_status_t last = OMEGA_OK;
    for (int i = 0; i < 4096; ++i)
    {
        last = e.enqueue(SetTempoCmd{120'000u});
        if (last != OMEGA_OK)
        {
            break;
        }
    }
    REQUIRE(last == OMEGA_ERR_QUEUE_FULL);
}

TEST_CASE("Engine process() and enqueue() are TSan-clean from two threads", "[tsan]")
{
    Engine e;
    REQUIRE(e.enqueue(TransportCmd{TransportAction::PLAY, 0u}) == OMEGA_OK);
    e.process();

    constexpr int N_CYCLES = 200;
    std::atomic<bool> timing_done{false};

    std::thread timing_thread([&]() {
        for (int i = 0; i < N_CYCLES; ++i)
        {
            e.process();
        }
        timing_done.store(true, std::memory_order_release);
    });

    std::thread mutation_thread([&]() {
        uint32_t bpm = 120'000u;
        while (!timing_done.load(std::memory_order_acquire))
        {
            e.enqueue(SetTempoCmd{bpm});
            bpm = (bpm == 120'000u) ? 240'000u : 120'000u;
        }
    });

    timing_thread.join();
    mutation_thread.join();

    REQUIRE(e.transport_state() == TransportState::PLAYING);
}
