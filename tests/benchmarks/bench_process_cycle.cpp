#include <omega/commands.h>
#include <omega/engine.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>
#include <omega/types.h>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

// bench.process_cycle -- engine.process() overhead with one registered track
// and no events due this cycle. Measures the minimum per-cycle cost.
TEST_CASE("bench_process_cycle", "[benchmark]")
{
    omega::MockClock clock;
    omega::Engine engine(&clock);
    omega::CapturingSink sink;
    engine.add_sink(&sink);
    engine.add_track("track");
    engine.enqueue(omega::TransportCmd{omega::TransportAction::PLAY, 0u});
    engine.process();  // drain play command

    BENCHMARK("process_cycle")
    {
        clock.advance_ticks(1u);
        engine.process();
        return engine.transport_state();
    };
}
