#include <omega/tempo_map.h>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

// bench.tempo_lookup -- TempoMap::ns_to_ticks() with a 16-point map.
// Measures the binary-search cost across a realistic multi-tempo session.
TEST_CASE("bench_tempo_lookup", "[benchmark]")
{
    omega::TempoMap map;
    for (uint32_t i = 0; i < 16u; ++i)
    {
        map.insert(static_cast<uint64_t>(i) * 1920u, 100000u + i * 5000u);
    }
    const uint64_t ns = 7'000'000'000u;

    BENCHMARK("tempo_lookup")
    {
        return map.ns_to_ticks(ns);
    };
}
