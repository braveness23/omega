#include <omega/detail/spsc_queue.h>

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>

using omega::detail::SpscQueue;

TEST_CASE("SpscQueue is empty on construction")
{
    SpscQueue<int, 16> q;
    REQUIRE(q.empty());
    REQUIRE(q.size() == 0u);  // NOLINT(readability-container-size-empty)
}

TEST_CASE("SpscQueue push and pop round-trip")
{
    SpscQueue<int, 16> q;
    REQUIRE(q.push(42));
    REQUIRE(!q.empty());
    REQUIRE(q.size() == 1u);

    int val = 0;
    REQUIRE(q.pop(val));
    REQUIRE(val == 42);
    REQUIRE(q.empty());
}

TEST_CASE("SpscQueue returns false when full")
{
    SpscQueue<int, 4> q;
    /* Usable capacity is Capacity-1 = 3 slots. */
    REQUIRE(q.push(1));
    REQUIRE(q.push(2));
    REQUIRE(q.push(3));
    REQUIRE(!q.push(4));
}

TEST_CASE("SpscQueue returns false when empty")
{
    SpscQueue<int, 4> q;
    int val = 0;
    REQUIRE(!q.pop(val));
}

TEST_CASE("SpscQueue preserves FIFO order")
{
    SpscQueue<int, 8> q;
    for (int i = 0; i < 5; ++i)
    {
        REQUIRE(q.push(i));
    }

    for (int i = 0; i < 5; ++i)
    {
        int val = -1;
        REQUIRE(q.pop(val));
        REQUIRE(val == i);
    }
}

TEST_CASE("SpscQueue can be reused after draining")
{
    SpscQueue<int, 4> q;
    REQUIRE(q.push(10));
    int v = 0;
    REQUIRE(q.pop(v));
    REQUIRE(v == 10);

    REQUIRE(q.push(20));
    REQUIRE(q.pop(v));
    REQUIRE(v == 20);
}

TEST_CASE("SpscQueue two-thread stress test", "[tsan]")
{
    constexpr uint32_t N = 1'000'000u;
    SpscQueue<uint32_t, 65536> q;

    uint64_t sum_produced = 0;
    uint64_t sum_consumed = 0;

    std::thread producer([&]() {
        for (uint32_t i = 0; i < N; ++i)
        {
            uint32_t item = i;
            while (!q.push(item))
            {
            }
            sum_produced += i;
        }
    });

    std::thread consumer([&]() {
        uint32_t received = 0;
        uint32_t val = 0;
        while (received < N)
        {
            if (q.pop(val))
            {
                sum_consumed += val;
                ++received;
            }
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(sum_produced == sum_consumed);
}
