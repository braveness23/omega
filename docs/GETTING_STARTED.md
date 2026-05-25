# Getting Started with Omega

Omega is a C++ sequencer engine — a library, not an application. It handles timing,
event scheduling, multi-track and pattern sequencing, and live performance control.
Your code drives it; Omega handles the details.

---

## Contents

1. [Installation](#installation)
2. [Quick Start: Playback without hardware](#quick-start-playback-without-hardware)
3. [Quick Start: Real MIDI output](#quick-start-real-midi-output)
4. [Core Concepts](#core-concepts)
5. [Thread Model](#thread-model)
6. [Extension Points](#extension-points)
7. [Running the Tests](#running-the-tests)
8. [Next Steps](#next-steps)

---

## Installation

### CMake FetchContent (recommended)

```cmake
include(FetchContent)
FetchContent_Declare(
    omega
    GIT_REPOSITORY https://github.com/braveness23/omega.git
    GIT_TAG        v1.0.0
)
FetchContent_MakeAvailable(omega)

target_link_libraries(your_app PRIVATE omega)
```

### System requirements

| Platform | MIDI requirement |
|---|---|
| Linux | `libasound2-dev` (ALSA) |
| macOS | CoreMIDI (no extra install) |
| Windows | WinMM (no extra install) |

- C++17 compiler: GCC 10+, Clang 11+, MSVC 2019+
- CMake 3.16+

### Manual build

```bash
git clone https://github.com/braveness23/omega.git
cd omega
cmake -B build -DOMEGA_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

---

## Quick Start: Playback without hardware

This example uses `MockClock` and `CapturingSink` — test utilities that ship with the
library — to schedule and verify a note without any MIDI hardware.

```cpp
#include <omega/engine.h>
#include <omega/commands.h>
#include <omega/test/mock_clock.h>
#include <omega/test/capturing_sink.h>
#include <cassert>

int main()
{
    // Create a manually-advanced clock. Use InternalClock (the default) in production.
    omega::MockClock clock;
    omega::Engine engine(&clock);

    // Register an output sink. CapturingSink records every event sent to it.
    omega::CapturingSink sink;
    engine.add_sink(&sink);

    // Create a track and route it to the sink.
    omega::TrackId track = engine.add_track("lead");
    engine.set_track_sink(track, sink.sink_id());

    // Schedule a note: C4 (MIDI note 60), velocity 100, duration 1 beat (480 ticks).
    // omega_make_note_on: (tick, sink_id, channel, note, velocity, duration_ticks)
    omega::Event note = omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 480);
    engine.enqueue(omega::AddEventCmd{track, note});

    // Start playback. Enqueue() is non-blocking; process() applies the command.
    engine.enqueue(omega::TransportCmd{omega::TransportAction::PLAY, 0u});
    clock.advance_ticks(1u);   // move the clock past tick 0
    engine.process();          // timing thread: dispatches events due at tick 0

    assert(sink.has_note_on(60, 0));  // note fired on channel 0
    assert(sink.count() == 1);

    // Advance past the note-off tick (480 ticks = 1 beat at 120 BPM).
    clock.advance_ticks(480u);
    engine.process();

    assert(sink.has_note_off(60, 0)); // synthesised note-off arrived
    return 0;
}
```

---

## Quick Start: Real MIDI output

This example opens a MIDI port and plays a short ascending scale.

```cpp
#include <omega/engine.h>
#include <omega/timer.h>
#include <omega/midi_io.h>
#include <omega/commands.h>
#include <thread>
#include <chrono>

int main()
{
    // Default constructor uses the built-in InternalClock.
    omega::Engine engine;

    // Open the first available MIDI output port.
    // Pass a port name string to select a specific port; nullptr = first available.
    omega::LibremidiSink midi_out(nullptr);
    engine.add_sink(&midi_out);
    omega::sink_id_t out_id = midi_out.sink_id();

    // Create a track routed to the MIDI port.
    omega::TrackId track = engine.add_track("scale");
    engine.set_track_sink(track, out_id);

    // Schedule four quarter notes: C4, D4, E4, F4 — one beat (480 ticks) apart.
    const uint8_t notes[] = {60, 62, 64, 65};
    for (int i = 0; i < 4; ++i)
    {
        omega::Event ev = omega_make_note_on(
            static_cast<uint64_t>(i) * 480,  // tick position
            out_id,
            0,          // MIDI channel
            notes[i],   // note number
            100,        // velocity
            240         // duration: half a beat
        );
        engine.enqueue(omega::AddEventCmd{track, ev});
    }

    // Start playback. OmegaTimer calls engine.process() every 1 ms on a background thread.
    engine.enqueue(omega::TransportCmd{omega::TransportAction::PLAY, 0u});
    omega::OmegaTimer timer(engine);  // starts immediately

    // Let the scale play, then stop.
    std::this_thread::sleep_for(std::chrono::seconds(3));
    engine.enqueue(omega::TransportCmd{omega::TransportAction::STOP, 0u});

    return 0;  // OmegaTimer destructor joins the background thread cleanly.
}
```

---

## Core Concepts

### Engine and Session

`Engine` is the playback machine. It owns:
- The SPSC command queue (mutation thread → timing thread)
- References to all registered sinks, sources, and inputs
- Three built-in playback sources: `TimelineSource`, `SongArrangementSource`, `PerformanceSource`

### Ticks

All musical time is measured in **ticks**. Omega uses **480 PPQN** (pulses per quarter note) —
one beat at any tempo is exactly 480 ticks. The constant `OMEGA_PPQN` is always 480.

```
1 beat  = 480 ticks
1 bar (4/4) = 1920 ticks
```

Ticks are independent of tempo. Tempo only affects how fast ticks advance in wall-clock time.

### Events

Events are 24-byte structs:

```cpp
struct Event {
    uint64_t tick;        // absolute position in ticks from session start
    uint32_t sink_id;     // which OutputSink receives this event
    uint8_t  payload_tag; // OMEGA_NOTE_ON, OMEGA_NOTE_OFF, OMEGA_CC, OMEGA_PROGRAM, …
    uint8_t  channel;     // MIDI channel 0–15
    uint8_t  reserved[2];
    uint8_t  data[8];     // payload (note, velocity, duration, CC number/value, …)
};
```

Use the helper constructors (`omega_make_note_on`, `omega_make_cc`, `omega_make_program`) rather
than filling the struct manually — the data layout is internal.

### ClockSource

The engine asks a `ClockSource` for the current time in nanoseconds. Omega ships two:

| Clock | Use case |
|---|---|
| `InternalClock` (default) | Production — reads the system monotonic clock |
| `MockClock` | Tests — manually advanced via `advance_ticks()` or `set_ns()` |

### OutputSink

`OutputSink` receives dispatched events. Omega ships two:

| Sink | Use case |
|---|---|
| `LibremidiSink` | Sends MIDI to a hardware or virtual port via libremidi |
| `CapturingSink` | Records events in memory — for tests and inspection |

You can implement your own sink (OSC, CV, audio, logging) by subclassing `OutputSink`.

### The three playback modes

| Source | What it does |
|---|---|
| `TimelineSource` | Linear multi-track playback — the classic DAW model |
| `SongArrangementSource` | Chains named patterns with repeat counts |
| `PerformanceSource` | 64 slots; patterns are cued, looped, and transposed live |

All three run simultaneously every `process()` cycle. You can disable individual sources
via `omega_engine_remove_source()` if you only need one mode.

---

## Thread Model

Omega uses exactly **two threads**:

```
Mutation thread        Timing thread
─────────────────      ─────────────────────────────────────────
engine.enqueue(cmd)    engine.process()  ← called by OmegaTimer
     │                        │
     ▼                        ▼
  SPSC queue  ──────────► drain → advance sources → dispatch events → flush sinks
```

**Rules:**
- `engine.process()` is called from the **timing thread** only (usually by `OmegaTimer`).
- All mutations (add events, set tempo, cue patterns) go through `engine.enqueue()` from the
  **mutation thread** and take effect on the next `process()` call.
- `enqueue()` never blocks — if the queue is full (capacity 4096) it returns `OMEGA_ERR_QUEUE_FULL`.
- `OutputSink::send()` and `EventInput::poll()` fire from the timing thread. Implementations
  must not block or allocate memory.

### OmegaTimer

`OmegaTimer` is an RAII wrapper that runs `process()` on a background thread at a configurable
interval (default: 1 ms):

```cpp
omega::OmegaTimer timer(engine);        // default 1 ms interval
omega::OmegaTimer timer(engine, 500u);  // 500 µs interval
// destructor fires one final process() and joins the thread
```

For hosts that already have a real-time callback (audio thread, VST process block), call
`engine.process()` directly from that callback instead of using `OmegaTimer`.

---

## Extension Points

Omega has six extension points. All are registered through the engine's command queue and
are safe to add at runtime.

| Extension | Interface | Called from |
|---|---|---|
| Output destination | `OutputSink::send()` | Timing thread |
| Clock | `ClockSource::now_ns()` | Timing thread |
| Playback mode | `EventSource::advance()` | Timing thread |
| Event input | `EventInput::poll()` | Timing thread |
| Custom event types | `PayloadRegistry` | Registration only |
| Edit operations | `Command::execute()` / `undo()` | Mutation thread |

### Registering a custom EventSource

```cpp
class MyGenerativeSource : public omega::EventSource
{
public:
    void advance(uint64_t to_tick,
                 omega::EventDispatcher& dispatcher,
                 omega::ProcessContext& ctx) override
    {
        // Read from ctx.modulation_bus, ctx.perf_ctx, ctx.input_bus.
        // Dispatch events via dispatcher.dispatch(event).
    }
};

MyGenerativeSource src;
engine.enqueue(omega::AddSourceCmd{&src, 2u});  // priority 2 = PLAYBACK
```

Source priority order: **0 = MODULATOR** (runs first, writes ModulationBus) →
**1 = CONTEXT** (writes PerformanceContext) → **2 = PLAYBACK** (reads both, dispatches events).

---

## Running the Tests

```bash
# Development build with AddressSanitizer + UBSanitizer
cmake -B build_dev -DOMEGA_BUILD_TESTS=ON -DOMEGA_WITH_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build_dev
ctest --test-dir build_dev

# ThreadSanitizer (mutually exclusive with ASan)
cmake -B build_tsan -DOMEGA_BUILD_TESTS=ON -DOMEGA_WITH_TSAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build_tsan
ctest --test-dir build_tsan

# Run a single test by name pattern
ctest --test-dir build_dev -R test_performance_source
```

The test suite has 388 tests and runs in under 2 seconds. All tests are deterministic
(no real clocks or MIDI hardware required).

---

## Next Steps

- **Full API reference**: [`include/omega/omega.h`](../include/omega/omega.h) — every function
  documents its thread requirement, parameters, return value, and error codes.
- **Architecture overview**: [`docs/ARCHITECTURE.md`](ARCHITECTURE.md) — the three-layer design
  with ASCII diagram and links to all 14 design documents.
- **Diagrams**: [`docs/diagrams/`](diagrams/) — Mermaid class, flow, and state diagrams.
- **Design documents**: [`docs/design/`](design/) — full specifications for timing, threading,
  memory, the C API, pattern state machine, orchestration layer, and more.
- **Migration from v0.x**: [`docs/migration/v0-to-v1.md`](migration/v0-to-v1.md)
- **Examples**: the smoke test in [`cmake/smoke_test/main.cpp`](../cmake/smoke_test/main.cpp)
  is a minimal C API example that compiles and runs as part of the install CI job.
