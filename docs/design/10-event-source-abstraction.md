# Design Proposal: EventSource Abstraction

**Status**: Accepted. This decision supersedes the implicit mode model described in [06-session-container.md](06-session-container.md) and [07-extensions.md](07-extensions.md). Those documents have been updated to reflect this design.

---

## Problem

The current design has `OutputSink` and `ClockSource` as clean, pluggable abstractions — anything that consumes events or provides time can be added without touching the engine. The mode system is not symmetric.

The engine's `process()` loop currently hardcodes which data structures drive playback:

```
TIMELINE    → scans session.timeline.tracks[*].events
PATTERN     → scans session.song_arrangement → resolves to pattern events
PERFORMANCE → scans active slots → resolves to pattern events
```

Adding a new sequencing mode requires modifying the engine's hot path and adding new named fields to `Session`. There is no published interface a new mode can implement. The extension points table in [07-extensions.md](07-extensions.md) lists four extension points; pluggable playback modes are not among them.

This is identified in [07-extensions.md](07-extensions.md):
> "The engine's playback loop: not virtual, not overridable"

That constraint was intentional for the existing three modes. It becomes a liability as the mode space grows.

---

## Proposed Addition: `EventSource`

Add a fifth extension point: **EventSource**. An `EventSource` is anything that can produce events into the engine's dispatch pipeline during a given tick window.

```cpp
class EventSource {
public:
    // Called from the timing thread at the top of each engine cycle.
    // Must not block, allocate, or lock.
    // Implementations emit all events due in the range (last_tick, to_tick].
    virtual void advance(uint64_t to_tick, EventDispatcher& out) = 0;

    // Called when transport starts. Allows sources to initialize playback state.
    virtual void on_transport_start(uint64_t start_tick) {}

    // Called when transport stops. Allows sources to silence active notes.
    virtual void on_transport_stop() {}

    virtual ~EventSource() = default;
};

// Passed to advance() — avoids std::function allocation in the hot path.
class EventDispatcher {
public:
    virtual void dispatch(const Event& e) = 0;
};
```

The engine maintains a registry of active `EventSource` instances. Each call to `process()` calls `advance()` on every registered source. Sources are independent — they run in registration order, all within the same engine cycle.

---

## Mapping Existing Modes to EventSource

The three existing modes become `EventSource` implementations. Their behavior is unchanged; the interface just makes them interchangeable.

| Mode | EventSource Implementation |
|---|---|
| Timeline | `TimelineSource` — scans `Timeline::tracks[*].events`, respects mute/solo |
| Pattern | `SongArrangementSource` — walks `SongArrangement`, resolves to `PatternLibrary` events |
| Performance | `PerformanceSource` — manages the slot state machine (QUEUED/PLAYING/STOPPING), fires per-slot events with transpose and velocity scaling applied |

All three are registered by default when an engine is created. The coexistence property from the current design is preserved — all three sources run every cycle.

---

## New Modes as EventSource Implementations

The following modes are proposed as future `EventSource` implementations. They are included here to validate that the abstraction is sufficient, not to commit them to any particular release.

### Scene Launcher

A scene is a named preset of `{SlotId → PatternId}` assignments that fires atomically. Launching a scene enqueues a `LaunchSceneCmd` which calls `CuePatternCmd` for each slot in the scene. This does not require a new `EventSource` — it is a command on the existing `PerformanceSource`. It is listed here to confirm it fits without modification.

### Step Sequencer

A step sequencer operates on a fixed grid: N steps per pattern, each step has on/off, velocity, and per-step probability. This is a different authoring model from the tick-position event model, but the output is the same: events at tick positions.

`StepSequencerSource` generates events from step data during `advance()`. Internally it converts step index to ticks using the current pattern length and step count. From the engine's perspective it is just another source emitting events. Step patterns can coexist with the `PatternLibrary` or be stored separately; they do not need to be the same data structure as `Pattern`.

```
Step grid (8 steps, pattern length 960 ticks):
  step 0: note 60, vel 100   → tick 0
  step 2: note 60, vel 80    → tick 240
  step 4: note 60, vel 90    → tick 480
  step 6: note 60, vel 70    → tick 720
```

### Generative / Algorithmic Sequencer

Generative sources produce events procedurally rather than replaying stored data. Examples: Euclidean rhythm generators, Markov chain melodic generators, L-system phrase generators.

This mode cannot be implemented under the current hardcoded design — the engine assumes a sorted `vector<Event>` exists to scan. With `EventSource`, a generative source simply calls `out.dispatch()` for each generated event during `advance()`:

```cpp
class EuclideanRhythmSource : public EventSource {
    uint32_t steps_;       // total steps in the pattern
    uint32_t pulses_;      // active pulses (Bjorklund algorithm)
    uint32_t rotation_;    // offset
    // ...
public:
    void advance(uint64_t to_tick, EventDispatcher& out) override;
};
```

The generated sequence is deterministic given the parameters and the current tick. No event storage required. Parameters are mutated via the command queue (same rules as all other mutations from the mutation thread).

### Polyrhythmic / Per-Track Tempo

In polyrhythmic mode, each source maintains its own **local tick** that advances at a different rate from the global tick. The engine provides `to_tick` in global ticks; the source maps it to local ticks using its tempo multiplier.

```cpp
class PolyrhythmicSource : public EventSource {
    double tempo_multiplier_ = 1.0;  // 2.0 = double speed, 0.5 = half speed
    uint64_t local_tick_ = 0;
public:
    void advance(uint64_t to_tick, EventDispatcher& out) override {
        uint64_t local_to = static_cast<uint64_t>(to_tick * tempo_multiplier_);
        // scan events from local_tick_ to local_to
        local_tick_ = local_to;
    }
};
```

The global tempo map still governs wall-clock-to-tick conversion for the engine as a whole. Per-source tempo multipliers are applied on top, in tick space. This is sufficient for classic polyrhythm use cases (3-against-4, etc.) without modifying the tempo map or requiring fractional ticks.

### Reactive / Trigger Source

Reactive sources transform incoming events into outgoing events in real time. Examples: arpeggiators, chord voicing maps, harmonizers, note-to-trigger routers.

Incoming MIDI arrives via the recording path (currently used for capture). A reactive source holds a staging buffer (same design as the recording staging buffer in [03-memory-storage.md](03-memory-storage.md)) that is filled by the input callback and drained during `advance()`. The source processes each input event and calls `out.dispatch()` with the transformed output.

```
Input (MIDI in):   note-on C4
Arpeggiator:       dispatches C4, E4, G4, C5 across next N ticks
```

The reactive source may look ahead into future tick windows to schedule arpeggiated notes, or it may operate purely on-demand each cycle. Either approach fits within `advance()`.

---

## Impact on Existing Design Documents

| Document | Change Required |
|---|---|
| [06-session-container.md](06-session-container.md) | Session no longer owns `Timeline`, `SongArrangement`, and `PerformanceConfig` as fixed named fields. Each `EventSource` owns its own data. Session owns a registry of `EventSource` instances. |
| [07-extensions.md](07-extensions.md) | Add `EventSource` as a fifth extension point. Remove the statement that the playback loop is not overridable. |
| [04-c-api-design.md](04-c-api-design.md) | Add C API surface for registering custom event sources: `omega_source_create(advance_fn, userdata)`. Existing transport/track/pattern/performance API functions target specific built-in sources by name. |
| [08-testing-strategy.md](08-testing-strategy.md) | Add `RecordingSource` (captures all dispatched events per source) as a test utility alongside `MockClock` and `CapturingSink`. |

---

## What Does Not Change

- The SPSC command queue and mutation model are unchanged. All mutations to source state still go through the queue.
- The `OutputSink` and `ClockSource` abstractions are unchanged.
- The `OutputDispatcher` passed to `advance()` is implemented by the engine and routes to registered `OutputSink` instances — sinks are still unaware of which source produced an event.
- The active notes table and note-off tracking remain engine-level responsibilities, not source responsibilities. Sources emit note-on events; the engine handles note-off timing.
- All hot-path rules apply: `advance()` must not allocate, block, or lock.

---

## C API Sketch

```c
/* Register a custom event source */
typedef void (*omega_advance_fn_t)(uint64_t to_tick, omega_dispatcher_t dispatcher,
                                    void* userdata);
typedef void (*omega_transport_start_fn_t)(uint64_t start_tick, void* userdata);
typedef void (*omega_transport_stop_fn_t)(void* userdata);

typedef struct {
    omega_advance_fn_t         advance;
    omega_transport_start_fn_t on_start;   /* nullable */
    omega_transport_stop_fn_t  on_stop;    /* nullable */
    void*                      userdata;
} omega_source_desc_t;

omega_source_t omega_source_create(const omega_source_desc_t* desc);
void           omega_source_destroy(omega_source_t source);
omega_status_t omega_engine_add_source(omega_engine_t engine, omega_source_t source);
omega_status_t omega_engine_remove_source(omega_engine_t engine, omega_source_t source);

/* Dispatch an event from within an advance callback */
void omega_dispatch(omega_dispatcher_t dispatcher, const omega_event_t* event);
```

The three built-in sources (timeline, pattern, performance) are added automatically on engine creation. They can be removed if a caller wants a custom-only engine.

---

## Cost of Deferring

The current design is in the pre-implementation phase. Adding `EventSource` now costs one interface definition and a refactor of three internal classes. The Session ownership model shifts slightly (sources own their data rather than Session owning fixed fields), but no implementation exists to migrate.

If deferred until after implementation:
- The engine's process loop, the Session struct, and the C API all require breaking changes.
- Callers of the C API (once published) would face an ABI break if mode-related functions change signatures.
- The three existing mode implementations would need to be extracted from the engine and wrapped.

The abstraction is symmetric with `OutputSink` and `ClockSource`, which are already accepted design decisions. There is no architectural reason to treat the input side of the engine differently from the output side.

---

## Open Issues

- **Source ordering**: When multiple sources emit events at the same tick, in what order are they dispatched? Registration order is deterministic but may not be musically correct. Consider a priority field on `EventSource`.
- **Source-to-sink routing**: Currently all sources dispatch to all sinks (filtered by `sink_id` on the event). Should sources declare which sinks they target, or is per-event routing sufficient?
- **Stateful sources and locate**: When `omega_transport_locate()` jumps to a new tick, stateful sources (step sequencer, polyrhythmic) must reset their local state. Add `on_locate(uint64_t tick)` to the interface.
- **Built-in source access via C API**: The existing track/pattern/performance C API functions target the built-in sources. If a caller removes a built-in source, those functions become no-ops or errors. Document clearly; consider making built-in sources non-removable in v1.
- **Session serialization**: Each source must be serializable to the `.omega` format. Custom sources cannot be serialized by the library. Define a serialization protocol or document that custom sources are session-ephemeral.
