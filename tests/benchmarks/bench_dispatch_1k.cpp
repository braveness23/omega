#include <omega/commands.h>
#include <omega/engine.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>
#include <omega/types.h>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

// bench.dispatch_1k -- engine.process() with 1000 events due in one cycle.
// Measures event dispatch throughput: iterate sorted vector + sink delivery.
TEST_CASE("bench_dispatch_1k", "[benchmark]")
{
    omega::MockClock clock;
    omega::Engine engine(&clock);
    omega::CapturingSink sink;
    engine.add_sink(&sink);
    omega::TrackId tid = engine.add_track("track");
    engine.enqueue(omega::TransportCmd{omega::TransportAction::PLAY, 0u});

    for (uint32_t i = 0; i < 1000u; ++i)
    {
        omega::Event ev = omega_make_note_on(
            static_cast<uint64_t>(i), sink.sink_id(), 0, static_cast<uint8_t>(i % 128u), 100, 0);
        engine.enqueue(omega::AddEventCmd{tid, ev});
    }
    engine.process();  // drain all setup commands

    clock.advance_ticks(1001u);

    BENCHMARK("dispatch_1k")
    {
        sink.clear();
        clock.advance_ticks(1001u);
        engine.process();
        return sink.count();
    };
}
