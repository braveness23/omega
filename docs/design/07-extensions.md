# Design: Extensions and Plugin Points

## Extension Philosophy

Omega has six designed extension points. Everything else is internal. Adding more extension points later is easy; removing them after they're published is not. We start conservative.

| Extension Point | How | Who Implements |
|---|---|---|
| Output sink | Subclass `OutputSink` | Application / plugin author |
| Clock source | Subclass `ClockSource` | Application / platform integrator |
| Event source | Subclass `EventSource` | Application / mode author |
| Event input | Subclass `EventInput` | Application / hardware integrator |
| Event payload types | Register with `PayloadRegistry` | Application / third-party library |
| Edit operations | Add `Command` subclass | Internal or application extension |

---

## Output Sinks

The primary extension point. Any time-tagged event delivery target is an `OutputSink`.

```cpp
class OutputSink {
public:
    virtual void send(const Event& e) = 0;
    virtual void flush() {}      // called after a batch of sends; optional
    virtual void panic() {}      // all-notes-off, reset; optional
    virtual ~OutputSink() = default;
};
```

**`send()`** is called from the timing thread. Must not block. Must not allocate. Must be re-entrant-safe (may be called for multiple events in sequence within one engine cycle).

**`flush()`** is called after the engine finishes processing all due events for a cycle. Useful for MIDI sinks that batch outgoing bytes — `flush()` is the signal to actually write to the hardware port.

**`panic()`** is called on transport stop or explicitly. MIDI sinks should send all-notes-off (CC 123) and reset controllers on all channels.

### Provided Sinks

| Sink | Description | Dependency |
|---|---|---|
| `MidiOutputSink` | Writes to a MIDI port via libremidi | libremidi (MIT) |
| `CapturingSink` | Records events to a vector (test/debug use) | None |
| `NullSink` | Discards all events (benchmarking) | None |
| `OscSink` | Sends OSC messages over UDP | liblo or similar (LGPL — optional) |

### Third-Party Sinks

The C API exposes sinks as `omega_sink_create(callback_fn, userdata)`. Any language that can call C can implement a sink by providing a callback function. No subclassing required across the C boundary.

---

## Event Sources

The event source is the input-side complement to the output sink. An `EventSource` produces events into the engine's dispatch pipeline each engine cycle. The three built-in sequencing modes (Timeline, Pattern, Performance) are implemented as `EventSource` subclasses. Applications can add custom sources for new sequencing paradigms.

```cpp
class EventSource {
public:
    // Called from the timing thread at the top of each engine cycle.
    // Must not block, allocate, or lock.
    // Emit all events due in the range (last_to_tick, to_tick].
    // ctx provides read/write access to ModulationBus, PerformanceContext, and InputBus.
    virtual void advance(uint64_t to_tick, EventDispatcher& out, ProcessContext& ctx) = 0;

    // Called when transport starts. Sources initialize playback state here.
    virtual void on_transport_start(uint64_t start_tick) {}

    // Called when transport stops. Sources silence active notes here.
    virtual void on_transport_stop() {}

    // Called when the transport locates to a new tick position.
    // Stateful sources reset local playback position here.
    // Sources that support chasing dispatch catch-up events via chase_out.
    virtual void on_locate(uint64_t tick, EventDispatcher& chase_out, ProcessContext& ctx) {}

    virtual ~EventSource() = default;
};

// Passed to advance() and on_locate() — the source calls out.dispatch() for each due event.
// Implemented by the engine; routes events to registered OutputSink instances.
class EventDispatcher {
public:
    virtual void dispatch(const Event& e) = 0;
};
```

`advance()` is called from the timing thread. All hot-path rules apply: no allocation, no blocking, no locking.

### Built-in Sources

| Source | Description |
|---|---|
| `TimelineSource` | Scans `Timeline` tracks, respects mute/solo |
| `SongArrangementSource` | Walks the song arrangement, resolves `PatternId` references |
| `PerformanceSource` | Manages the 64-slot state machine; applies transpose, velocity scale, and random bias |

All three are registered automatically on engine creation. They can be removed via `omega_engine_remove_source()` if a caller wants a custom-only engine, but this is not expected in normal use.

### Provided Extension Sources

| Source | Description |
|---|---|
| `StepSequencerSource` | Fixed-grid step sequencer; generates tick-position events from a step grid |
| `EuclideanRhythmSource` | Generative Euclidean rhythm patterns (Bjorklund algorithm) |
| `PolyrhythmicSource` | Wraps any source with a per-source tempo multiplier |
| `ReactiveSource` | Transforms incoming MIDI into outgoing events (arpeggiator, chord map) |

### C API

The `advance` and `on_locate` callbacks receive a `const omega_process_ctx_t*` providing access to `ModulationBus`, `PerformanceContext`, and the `InputBus`. Full type definitions are in [04-c-api-design.md](04-c-api-design.md).

```c
omega_source_t omega_source_create(const omega_source_desc_t* desc);
void           omega_source_destroy(omega_source_t source);
omega_status_t omega_engine_add_source(omega_engine_t engine, omega_source_t source);
omega_status_t omega_engine_remove_source(omega_engine_t engine, omega_source_t source);

/* Dispatch an event from within an advance or on_locate callback */
void omega_dispatch(omega_dispatcher_t dispatcher, const omega_event_t* event);
```

### Source Priority Convention

Sources are called in registration order within each engine cycle. The convention for registration order is:

1. **MODULATOR** — `LfoSource`, `EnvelopeSource`, `StepModSource`, `ExpressionSource`. These write to the `ModulationBus` and must run before any source that reads from it.
2. **CONTEXT** — `ChordDetectorSource`, `KeyDetectorSource`. These write to `PerformanceContext` and must run before playback sources that read it.
3. **PLAYBACK** — `TimelineSource`, `SongArrangementSource`, `PerformanceSource`, custom playback sources and transform chains. These read from `ModulationBus` and `PerformanceContext` and dispatch events.

This ordering is documented convention, not code-enforced. A `SourcePriority` enum (`MODULATOR=0`, `CONTEXT=1`, `PLAYBACK=2`) is reserved for v2 if the convention proves insufficient.

When multiple sources emit events at the same tick, dispatch order follows source registration order within the same priority tier. This is deterministic.

### TransformSource — Composition-Based Routing

`TransformSource` is a provided base class for composing `EventSource` instances in a pipeline. A `TransformSource` wraps an upstream source, intercepts its output, and re-dispatches transformed events.

```cpp
class TransformSource : public EventSource {
public:
    explicit TransformSource(std::shared_ptr<EventSource> upstream);

    void advance(uint64_t to_tick, EventDispatcher& out, ProcessContext& ctx) override;
    void on_locate(uint64_t tick, EventDispatcher& chase_out, ProcessContext& ctx) override;
    void on_transport_start(uint64_t start_tick) override;
    void on_transport_stop() override;

protected:
    // Subclass implements transformation logic here.
    // May emit zero, one, or many output events per input event.
    virtual void transform(const Event& in, EventDispatcher& out, ProcessContext& ctx) = 0;

    std::shared_ptr<EventSource> upstream_;
};
```

Composition chains are built by passing one source to another's constructor. The engine's source registry contains only the outermost (root) source. No separate graph registry is needed.

```cpp
auto step_seq  = std::make_shared<StepSequencerSource>(pattern);
auto quantized = std::make_shared<ScaleQuantizerSource>(step_seq);
auto humanized = std::make_shared<HumanizerSource>(quantized, /*jitter_ticks=*/10);
engine.add_source(humanized);  // only the root is registered
```

| Transform | Description |
|---|---|
| `ScaleQuantizerSource` | Snaps note numbers to the active scale in `PerformanceContext` |
| `TransposeSource` | Shifts pitch by a fixed offset or a `ModulationBus` channel value |
| `VelocityCurveSource` | Applies a velocity curve (linear, exponential, S-curve, fixed table) |
| `HumanizerSource` | Adds bounded timing jitter and velocity scatter |
| `ChordSpreadSource` | Expands single notes into chords per a voicing table |
| `FilterSource` | Gates events matching a predicate (note range, CC range, type mask) |
| `DelaySource` | Delays all events by a fixed tick offset |

---

## Clock Sources

```cpp
class ClockSource {
public:
    virtual uint64_t now_ns() = 0;   // monotonic nanoseconds
    virtual ~ClockSource() = default;
};
```

### Provided Clock Sources

| Clock | Description | Dependency |
|---|---|---|
| `SteadyClock` | `std::chrono::steady_clock` | None (default) |
| `MidiClockSource` | Slaves to incoming MIDI clock pulses | None |
| `LinkClockSource` | Ableton Link beat/tempo sync | Ableton Link (GPL v2+, optional) |
| `MockClock` | Manually advanced; for testing | None |
| `HostClock` | Driven by DAW sample position | None (host provides values) |

The C API clock extension point: `omega_clock_create_custom(fn, userdata)` where `fn` returns `uint64_t` nanoseconds. Any timing source that can produce a monotonically increasing nanosecond count can drive Omega.

---

## Event Inputs

`EventInput` is the symmetric counterpart to `OutputSink`. Where `OutputSink` receives events from the engine and delivers them to the outside world, `EventInput` accepts events from the outside world and delivers them into the engine each cycle.

```cpp
class EventInput {
public:
    // Called from the timing thread during engine.process(), before any advance() calls.
    // Drain all pending incoming events and deliver them via dispatcher.
    // Must not block or allocate.
    virtual void poll(InputDispatcher& dispatcher) = 0;
    virtual ~EventInput() = default;
};

class InputDispatcher {
public:
    // Deliver an event into the InputBus for this cycle.
    // If the engine is in record mode, also copies to the per-track staging buffer.
    virtual void deliver(const Event& e) = 0;
};
```

Delivered events are accumulated in the `InputBus` and made available to all `EventSource::advance()` calls this cycle via `ProcessContext::input_bus`. The `InputBus` is cleared at the start of each `process()` call.

### Provided EventInput Implementations

| Input | Description | Dependency |
|---|---|---|
| `MidiInputSource` | Drains incoming MIDI events from a libremidi port | libremidi (MIT) |
| `OscInputSource` | Receives OSC messages over UDP | optional |
| `MockEventInput` | Primed with events for testing; drains one at a time | None (test utility) |

### Recording Integration

`EventInput` supersedes the ad-hoc MIDI-input-to-recording-buffer path. When the engine is in record mode, `InputDispatcher::deliver()` copies events to the per-track staging ring buffer as described in [03-memory-storage.md](03-memory-storage.md). The staging-and-commit model is unchanged.

### C API

```c
typedef struct omega_input_s*     omega_input_t;
typedef struct omega_input_disp_s* omega_input_dispatcher_t;

typedef void (*omega_input_poll_fn_t)(omega_input_dispatcher_t dispatcher, void* userdata);

typedef struct {
    omega_input_poll_fn_t poll;
    void*                 userdata;
} omega_input_desc_t;

omega_input_t  omega_input_create(const omega_input_desc_t* desc);
void           omega_input_destroy(omega_input_t input);
omega_status_t omega_engine_add_input(omega_engine_t engine, omega_input_t input);
omega_status_t omega_engine_remove_input(omega_engine_t engine, omega_input_t input);

/* Call from within poll callback to deliver an event into the InputBus */
void omega_deliver(omega_input_dispatcher_t dispatcher, const omega_event_t* event);

/* Query how many input events were dropped this cycle (InputBus overflow) */
uint32_t omega_input_overflow_count(omega_engine_t engine);
```

---

## Custom Event Payload Types

Built-in payload types cover MIDI 1.0, basic OSC, and parameter changes. Applications that need custom payload types (e.g., lighting cues, video timecode, custom hardware protocols) can register types.

### Registration

```cpp
class PayloadRegistry {
public:
    // Returns a type_id in range 0x0B-0xFE
    uint8_t register_type(std::string_view name,
                           PayloadSerializer* serializer);
    uint8_t find_type(std::string_view name) const;
};
```

`PayloadSerializer` handles serialization to/from the session file format:

```cpp
class PayloadSerializer {
public:
    virtual void serialize(const Event& e, std::ostream& out) = 0;
    virtual void deserialize(Event& e, std::istream& in) = 0;
    virtual ~PayloadSerializer() = default;
};
```

During event dispatch, the engine passes the event to the registered sink. The sink is responsible for interpreting the custom payload — the engine does not inspect it.

### Lifetime

The `PayloadRegistry` is owned by the `Session`. If a session is loaded that contains registered custom types, and the application has not registered those types, events with unknown type IDs are preserved in the session data but not dispatched (they pass through as no-ops with a warning flag).

### Inline vs. Blob

Custom types that fit in 8 bytes use the `Event::data` field directly. Custom types larger than 8 bytes use the blob store: `data[0-3]` = blob index, `data[4-7]` = custom type sub-ID. The blob store is type-agnostic — it stores raw bytes.

---

## Edit Operation Extensions

Edit operations (quantize, transpose, velocity scale, etc.) are implemented as `Command` objects following the Command pattern (credit: TSE3).

```cpp
class Command {
public:
    virtual void execute(Session& session) = 0;
    virtual void undo(Session& session) = 0;
    virtual std::string describe() const = 0;   // for undo history display
    virtual ~Command() = default;
};
```

Applications can add custom edit operations by implementing `Command` and submitting them to the `CommandHistory`. Custom commands participate in undo/redo automatically.

Built-in commands are in the `omega::commands` namespace. Third-party commands can be in any namespace.

This is the extension point for DAW-style features that are out of scope for the core library: humanize, chord expansion, arpeggiation, etc.

---

## What Is Not an Extension Point

- The SPSC queue implementation: internal detail
- The TempoMap: not replaceable (its semantics are fundamental)
- The session file format reader/writer: internal (expose via `omega_smf_import`/`export`)
- PPQN: compile-time constant in v1

If a use case requires overriding these, open an issue. The design will evolve.

---

## Ableton Link Integration

Link is an optional dependency. It is gated by a CMake option:

```cmake
option(OMEGA_WITH_LINK "Enable Ableton Link sync" OFF)
```

**License implication**: Ableton Link is licensed under GPL v2+. Enabling `OMEGA_WITH_LINK` makes the combined Omega+Link build subject to GPL v2+. The Omega core library itself remains MIT. This is documented prominently in the README and the CMakeLists.

When `OMEGA_WITH_LINK=ON`, the `LinkClockSource` class is compiled and available. When `OFF` (default), any reference to `LinkClockSource` produces a compile error.

Users who need Ableton Link in closed-source software must obtain a commercial Link license from Ableton directly.

---

## OSC Output (Future)

OSC via `OscSink` requires a UDP socket library. Candidates:

| Library | License | Notes |
|---|---|---|
| liblo | LGPL 2.1+ | Mature, widely used; LGPL allows dynamic linking |
| oscpack | MIT | Simpler; less maintained |
| Custom | MIT | Small enough to write; avoids the dependency |

Recommendation: implement basic OSC (bundle + message) natively (it's a simple binary protocol), avoiding the dependency entirely for v1. If full OSC feature support is needed later, add liblo as an optional dependency.

---

## Open Issues

- **Custom payload C API**: The `PayloadRegistry` is a C++ interface. How does a Python binding register a custom payload type with a serializer? Design a C wrapper for `PayloadSerializer`.
- **Sink thread affinity**: Should sinks be able to declare which thread they're safe to call from? Add `sink_thread_affinity()` returning `TIMING_THREAD_ONLY | ANY_THREAD` as a future hint.
- **Plugin format**: Should Omega support VST/AU/CLAP plugin loading to discover sinks? Out of scope for v1, but don't design against it.
- **EventInput and ModulationBus channel count**: Both `InputBus::MAX_EVENTS` (64) and `ModulationBus::MAX_CHANNELS` (256) are compile-time constants. Make them construction-time parameters (like command queue capacity) before v1 ships, so embedded targets can tune them.
- **TransformSource fan-out**: Composition handles fan-in (multiple sources feeding one transform) but not fan-out (one source feeding multiple transforms). If needed, a shared `EventBuffer` pattern is the mechanism. Deferred — no planned source requires it.
