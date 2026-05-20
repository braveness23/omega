#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

// Placeholder benchmarks. Replace with real implementations once engine types exist.
// See docs/design/12-library-foundation.md §4.5 for the required benchmark list.

TEST_CASE("bench_process_cycle", "[benchmark]")
{
    BENCHMARK("process_cycle placeholder")
    {
        return 1 + 1;
        // Replace with: engine.process(dispatcher, clock.now_ns());
    };
}

TEST_CASE("bench_command_enqueue", "[benchmark]")
{
    BENCHMARK("command_enqueue placeholder")
    {
        return 1 + 1;
        // Replace with: enqueue and drain 1000 commands through the SPSC queue
    };
}

TEST_CASE("bench_tempo_lookup", "[benchmark]")
{
    BENCHMARK("tempo_lookup placeholder")
    {
        return 1 + 1;
        // Replace with: TempoMap::ns_to_tick() on a map with 100 tempo points
    };
}
