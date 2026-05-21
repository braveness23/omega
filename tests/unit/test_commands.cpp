#include <omega/commands.h>
#include <omega/detail/spsc_queue.h>

#include <catch2/catch_test_macros.hpp>
#include <thread>

using namespace omega;

TEST_CASE("Command fits in 64 bytes")
{
    static_assert(sizeof(Command) <= 64, "Command must fit within 64 bytes");
    REQUIRE(sizeof(Command) <= 64U);
}

TEST_CASE("SpscQueue<Command> basic push and pop")
{
    detail::SpscQueue<Command, 4096> que;

    Command cmd = SetTempoCmd{120'000U};
    REQUIRE(que.push(cmd));

    Command out;
    REQUIRE(que.pop(out));
    REQUIRE(std::holds_alternative<SetTempoCmd>(out));
    REQUIRE(std::get<SetTempoCmd>(out).bpm_milli == 120'000U);
}

TEST_CASE("SpscQueue<Command> preserves all variant types")
{
    detail::SpscQueue<Command, 16> que;

    REQUIRE(que.push(SetTempoCmd{240'000U}));
    REQUIRE(que.push(TransportCmd{TransportAction::PLAY, 0U}));
    REQUIRE(que.push(SetLoopCmd{0U, 480U, true}));

    Command out;
    REQUIRE(que.pop(out));
    REQUIRE(std::holds_alternative<SetTempoCmd>(out));

    REQUIRE(que.pop(out));
    REQUIRE(std::holds_alternative<TransportCmd>(out));
    REQUIRE(std::get<TransportCmd>(out).action == TransportAction::PLAY);

    REQUIRE(que.pop(out));
    REQUIRE(std::holds_alternative<SetLoopCmd>(out));
    REQUIRE(std::get<SetLoopCmd>(out).enabled == true);
}

TEST_CASE("SpscQueue<Command> two-thread stress test", "[tsan]")
{
    constexpr uint32_t kCount = 1'000'000U;
    detail::SpscQueue<Command, 4096> que;

    uint64_t sum_produced = 0;
    uint64_t sum_consumed = 0;

    std::thread producer([&]() {
        for (uint32_t i = 0; i < kCount; ++i)
        {
            Command cmd = SetTempoCmd{i};
            while (!que.push(cmd))
            {
                cmd = SetTempoCmd{i};
            }
            sum_produced += i;
        }
    });

    std::thread consumer([&]() {
        uint32_t received = 0;
        Command val;
        while (received < kCount)
        {
            if (que.pop(val))
            {
                sum_consumed += std::get<SetTempoCmd>(val).bpm_milli;
                ++received;
            }
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(sum_produced == sum_consumed);
}
