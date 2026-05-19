# Design: Extensions and Plugin Points

## Extension Philosophy

Omega has five designed extension points. Everything else is internal. Adding more extension points later is easy; removing them after they're published is not. We start conservative.

| Extension Point | How | Who Implements |
|---|---|---|
| Output sink | Subclass `OutputSink` | Application / plugin author |
| Clock source | Subclass `ClockSource` | Application / platform integrator |
| Event source | Subclass `EventSource` | Application / mode author |
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
    virtual void advance(uint64_t to_tick, EventDispatcher& out) = 0;

    // Called when transport starts. Sources initialize playback state here.
    virtual void on_transport_start(uint64_t start_tick) {}

    // Called when transport stops. Sources silence active notes here.
    virtual void on_transport_stop() {}

    // Called when the transport locates to a new tick position.
    // Stateful sources (step sequencer, polyrhythmic) reset local state here.
    virtual void on_locate(uint64_t tick) {}

    virtual ~EventSource() = default;
};

// Passed to advance() — the source calls out.dispatch() for each due event.
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

```c
typedef void (*omega_advance_fn_t)(uint64_t to_tick, omega_dispatcher_t dispatcher,
                                    void* userdata);
typedef void (*omega_transport_start_fn_t)(uint64_t start_tick, void* userdata);
typedef void (*omega_transport_stop_fn_t)(void* userdata);
typedef void (*omega_locate_fn_t)(uint64_t tick, void* userdata);

typedef struct {
    omega_advance_fn_t         advance;
    omega_transport_start_fn_t on_start;   /* nullable */
    omega_transport_stop_fn_t  on_stop;    /* nullable */
    omega_locate_fn_t          on_locate;  /* nullable */
    void*                      userdata;
} omega_source_desc_t;

omega_source_t omega_source_create(const omega_source_desc_t* desc);
void           omega_source_destroy(omega_source_t source);
omega_status_t omega_engine_add_source(omega_engine_t engine, omega_source_t source);
omega_status_t omega_engine_remove_source(omega_engine_t engine, omega_source_t source);

/* Dispatch an event from within an advance callback */
void omega_dispatch(omega_dispatcher_t dispatcher, const omega_event_t* event);
```

### Source Ordering

Sources are called in registration order within each engine cycle. Built-in sources are registered first (timeline → pattern → performance), then application sources in the order `omega_engine_add_source()` was called. When multiple sources emit events at the same tick, dispatch order follows source registration order. This is deterministic but may not always be musically correct — a future `priority` field is reserved for v2.

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
