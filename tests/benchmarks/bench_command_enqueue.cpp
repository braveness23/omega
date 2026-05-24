#include <omega/commands.h>
#include <omega/detail/spsc_queue.h>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

// bench.command_enqueue -- SpscQueue push/pop round-trip throughput.
// Measures the cost of one enqueue + one dequeue of a Command variant.
TEST_CASE("bench_command_enqueue", "[benchmark]")
{
    omega::detail::SpscQueue<omega::Command, 4096> queue;
    const omega::Command cmd = omega::SetTempoCmd{120000u};

    BENCHMARK("command_enqueue")
    {
        queue.push(omega::Command{cmd});
        omega::Command out;
        queue.pop(out);
        return out;
    };
}
