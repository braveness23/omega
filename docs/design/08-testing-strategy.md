# Design: Testing Strategy

## Principle

A timing library that can only be tested against a real clock is not reliably testable. Every time-dependent behavior must be exercisable with deterministic, manually-advanced time. This requires the `MockClock` and `CapturingSink` to be first-class library components, not afterthoughts.

The test suite must run in CI in under 30 seconds. No sleeps, no real MIDI ports required.

---

## Test Framework

**Catch2** (BSL-1.0 license — compatible with MIT). Header-optional, BDD-style (`SCENARIO`/`GIVEN`/`WHEN`/`THEN`) or traditional (`TEST_CASE`/`SECTION`). Widely used for C++ libraries.

Catch2 is fetched via CMake `FetchContent` so contributors don't need to install it separately.

---

## The Three Test Utilities

These live in `include/omega/test/` and are compiled into a separate `omega_test` target. They are part of the public library — not hidden in the test directory — because application authors may want to use them to test their own sinks, clocks, sources, and inputs.

### MockClock

```cpp
class MockClock : public ClockSource {
    uint64_t current_ns_ = 0;
public:
    uint64_t now_ns() override { return current_ns_; }

    void advance_ns(uint64_t delta)    { current_ns_ += delta; }
    void advance_ticks(uint64_t ticks, uint32_t bpm_milli = 120000);
    void advance_beats(double beats, uint32_t bpm_milli = 120000);
    void set_ns(uint64_t ns)           { current_ns_ = ns; }
};
```

`advance_beats(1.0)` at 120 BPM advances by 500,000,000 ns (0.5 seconds). Internally uses the same integer arithmetic as the engine's timing model.

### MockEventSource

An `EventSource` that can be primed with events to emit at specific ticks. Allows tests to inject events into the engine pipeline without needing a full `TimelineSource` or `PerformanceSource` setup.

```cpp
class MockEventSource : public EventSource {
public:
    // Pre-load an event to be emitted when advance() reaches its tick.
    void add_event(const Event& e);
    void clear();

    void advance(uint64_t to_tick, EventDispatcher& out, ProcessContext& ctx) override;
    void on_transport_start(uint64_t start_tick) override { playhead_ = start_tick; }
    void on_transport_stop() override {}
    void on_locate(uint64_t tick, EventDispatcher& chase_out, ProcessContext& ctx) override {
        playhead_ = tick;
    }

    // Number of advance() calls received — useful for verifying call cadence.
    uint32_t advance_count() const;

private:
    std::vector<Event> events_;   // sorted by tick; emitted once
    uint64_t playhead_ = 0;
    uint32_t advance_count_ = 0;
};
```

`MockEventSource` complements `CapturingSink`: the mock source injects events at the input side; the capturing sink verifies events at the output side.

### MockEventInput

An `EventInput` that can be primed with events to deliver during `poll()`. Allows tests to simulate incoming MIDI or OSC events without needing a real hardware port.

```cpp
class MockEventInput : public EventInput {
public:
    // Pre-load an event to be delivered on the next poll() call.
    void push(const Event& e);
    void clear();

    void poll(InputDispatcher& dispatcher) override;

    // Number of poll() calls received — useful for verifying call cadence.
    uint32_t poll_count() const;

private:
    std::queue<Event> pending_;
    uint32_t poll_count_ = 0;
};
```

Use `MockEventInput` alongside `MockEventSource` and `CapturingSink` to test full input→source→sink pipelines deterministically.

### CapturingSink

```cpp
class CapturingSink : public OutputSink {
    std::vector<Event> events_;
    uint32_t           id_;
public:
    void send(const Event& e) override { events_.push_back(e); }
    void panic() override { /* record a panic event */ }

    const std::vector<Event>& events() const { return events_; }
    void clear() { events_.clear(); }

    // Convenience queries
    size_t  count(uint8_t type) const;
    bool    has_note_on(uint8_t note) const;
    bool    has_note_off(uint8_t note) const;
    Event   first(uint8_t type) const;
    SinkId  id() const { return id_; }
};
```

---

## Test Structure

```
tests/
├── unit/
│   ├── test_timing_model.cpp        # tick↔ns conversions, tempo map
│   ├── test_spsc_queue.cpp          # queue push/pop, full/empty conditions
│   ├── test_event_dispatch.cpp      # engine fires events at correct ticks
│   ├── test_note_off_tracking.cpp   # duration-based note-off
│   ├── test_pattern_state.cpp       # all slot state machine transitions
│   ├── test_performance_params.cpp  # transpose, velocity scale, random bias
│   ├── test_tempo_change.cpp        # mid-playback tempo change
│   ├── test_loop_region.cpp         # loop start/end, loop boundary behavior
│   ├── test_record_staging.cpp      # record, commit, merge into track
│   ├── test_smf_import.cpp          # roundtrip SMF type 0 and type 1
│   ├── test_session_save_load.cpp   # roundtrip native JSON format (incl. ModBus,
│   │                                # PerfCtx, GrooveLibrary keys)
│   ├── test_event_source.cpp        # MockEventSource, source registration,
│   │                                # multi-source ordering, locate/stop callbacks
│   ├── test_event_input.cpp         # MockEventInput, InputBus fill/overflow,
│   │                                # recording integration
│   ├── test_modulation_bus.cpp      # register, get/set, snapshot, ordering with
│   │                                # LfoSource writing and source reading
│   ├── test_performance_context.cpp # scale/chord/groove/chaos field writes,
│   │                                # ChordDetectorSource, random_seed advance
│   ├── test_transform_source.cpp    # ScaleQuantizerSource, HumanizerSource,
│   │                                # multi-wrap composition chain
│   └── test_chasing.cpp             # note/CC/program chasing on locate;
│                                    # PerformanceSource phase resume
├── integration/
│   ├── test_timeline_playback.cpp   # multi-track playback end-to-end
│   ├── test_pattern_playback.cpp    # song arrangement end-to-end
│   ├── test_performance_live.cpp    # cue/stop/transpose in sequence
│   ├── test_multi_source.cpp        # multiple sources active simultaneously;
│   │                                # verify ordering and note-off isolation
│   ├── test_orchestration.cpp       # MockEventInput → ProcessContext → CapturingSink;
│   │                                # LfoSource writes ModBus; TransposeSource reads it
│   └── test_c_api.cpp              # exercise the full C API surface incl. sources,
│                                    # inputs, ModBus, PerfCtx
└── benchmarks/
    ├── bench_process_loop.cpp       # throughput of engine.process() with N sources
    └── bench_event_insert.cpp       # cost of adding events to a large track
```

---

## Example Tests

### Timing model correctness

```cpp
TEST_CASE("tick to ns conversion is exact at 120 BPM") {
    // At 120 BPM, 480 PPQN: one beat = 500ms = 500,000,000 ns
    uint64_t ns = ticks_to_ns(480, 120000);  // 480 ticks = one beat
    REQUIRE(ns == 500'000'000ULL);
}

TEST_CASE("accumulated timing does not drift over 10 minutes") {
    MockClock clock;
    // Simulate 10 minutes at 120 BPM: 120 beats/min * 10 min = 1200 beats
    // = 1200 * 480 = 576,000 ticks
    uint64_t expected_ns = 600'000'000'000ULL;  // 10 minutes
    uint64_t actual_ns = ticks_to_ns(576'000, 120000);
    REQUIRE(actual_ns == expected_ns);
}
```

### Event dispatch

```cpp
SCENARIO("engine fires note-on at correct tick") {
    GIVEN("an engine with a mock clock, capturing sink, and no event inputs") {
        MockClock clock;
        CapturingSink sink;
        Engine engine(clock, sink);
        omega_transport_play(engine);

        WHEN("a note-on is added at tick 480 (one beat)") {
            Event e = omega_make_note_on(480, sink.id(), 0, 60, 100, 240);
            omega_track_add_event(engine, track, &e);

            THEN("it does not fire before tick 480") {
                clock.advance_ticks(479);
                engine.process();
                REQUIRE(sink.count(OMEGA_EVENT_NOTE_ON) == 0);
            }
            AND_THEN("it fires at tick 480") {
                clock.advance_ticks(1);
                engine.process();
                REQUIRE(sink.count(OMEGA_EVENT_NOTE_ON) == 1);
                REQUIRE(sink.first(OMEGA_EVENT_NOTE_ON).data[0] == 60);
            }
        }
    }
}
```

### Note-off tracking

```cpp
TEST_CASE("note-off fires at note-on tick + duration") {
    MockClock clock;
    CapturingSink sink;
    Engine engine(clock, sink);
    // Add note-on at tick 0, duration 240 ticks (one eighth note)
    Event e = omega_make_note_on(0, sink.id(), 0, 60, 100, 240);
    // ... add, play
    clock.advance_ticks(240);
    engine.process();
    REQUIRE(sink.has_note_off(60));
}
```

### Pattern state machine

```cpp
TEST_CASE("cuing a slot transitions IDLE -> QUEUED -> PLAYING at boundary") {
    // ... set up engine with 1-beat pattern (480 ticks)
    omega_perf_cue(engine, slot_0, OMEGA_CUE_AT_BOUNDARY);
    REQUIRE(slot_state(engine, slot_0) == OMEGA_SLOT_QUEUED);

    // Advance to loop boundary
    clock.advance_ticks(480);
    engine.process();
    REQUIRE(slot_state(engine, slot_0) == OMEGA_SLOT_PLAYING);
}
```

---

## CI Pipeline

```yaml
# .github/workflows/ci.yml
jobs:
  build-and-test:
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest, windows-latest]
        compiler: [gcc, clang]   # MSVC on windows
    steps:
      - configure CMake with -DOMEGA_BUILD_TESTS=ON
      - build
      - run ctest
      - upload test results
```

Tests must pass on all three platforms before a PR can merge.

---

## What We Don't Test Automatically

- Real MIDI hardware I/O: requires physical hardware. Tested manually. `MidiOutputSink` is thin enough that if the unit tests pass and the library builds, the MIDI path is trustworthy.
- Ableton Link sync: requires multiple machines on a network. Tested manually.
- Real-time thread safety: data races are not reliably caught by unit tests. Use ThreadSanitizer (`-fsanitize=thread`) in a CI variant.

Add a `sanitizer` CI job that builds with `-fsanitize=address,undefined` on Linux.

---

## Coverage

Track coverage with gcov/lcov. Target: 80% line coverage of the C++ core. The C API wrapper is covered by `test_c_api.cpp`.

Coverage does not measure correctness of timing behavior — that's what the explicit tick-accurate tests are for.

---

## Benchmarks

Benchmarks use Catch2's `BENCHMARK` macro. They are not run in CI (results vary by machine) but are tracked manually before releases.

Key benchmarks:
- `omega_process()` with 48 tracks, each with 1000 events, nothing due: should complete in < 1μs
- `omega_process()` firing 100 events simultaneously: should complete in < 10μs
- Pattern mode with 64 simultaneous slots: should complete in < 5μs per cycle
