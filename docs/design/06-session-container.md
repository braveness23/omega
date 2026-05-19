# Design: Session Container

## What Is a Session

A `Session` is the top-level save/load unit. It owns shared musical data, configuration, and the registry of active `EventSource` instances. The `Engine` holds a reference to the active `Session` and operates on it.

The separation of `Engine` (the playback machine) from `Session` (the data) makes it possible to:
- Load a new session without destroying the engine
- Compare two sessions
- Implement save-as without side effects
- Test session data independently of the playback engine

Mode-specific data (track lists, song arrangement, performance slots) is owned by each `EventSource` implementation, not by `Session` directly. `Session` owns only the data shared across multiple sources: `PatternLibrary`, `SinkRegistry`, `TempoMap`.

See [07-extensions.md](07-extensions.md) for the `EventSource` interface and the full list of built-in and extension sources.

---

## Ownership Hierarchy

```
Session
├── PatternLibrary           (shared; owns all Pattern objects, indexed by PatternId)
├── SinkRegistry             (named output sinks, registered by the host application)
├── TempoMap                 (list of TempoPoints; see timing model)
├── TimeSignature            (numerator, denominator — display/grid only, not timing)
├── LoopRegion               {start_tick, end_tick, enabled}
├── Metadata                 {name, author, created_at, modified_at}
├── ModulationBus            (256 float channels; written by modulator sources, read by any source)
├── PerformanceContext        (shared musical state: scale, chord, groove_id, chaos, transpose, etc.)
├── GrooveLibrary             (named GrooveTemplate entries; pre-loaded with 6 built-in templates)
├── EventSourceRegistry      (ordered list of registered EventSource instances)
│   ├── [MODULATOR priority]  LfoSource, EnvelopeSource, etc. (write ModulationBus)
│   ├── [CONTEXT priority]    ChordDetectorSource, etc. (write PerformanceContext)
│   ├── TimelineSource        (owns Timeline: tracks and their event vectors)
│   ├── SongArrangementSource (owns SongArrangement: ordered PatternRef list)
│   ├── PerformanceSource     (owns PerformanceConfig: 64 slots with state machine)
│   └── [application sources] (custom sources added via omega_engine_add_source)
└── EventInputRegistry       (ordered list of registered EventInput instances)
    ├── MidiInputSource       (drains incoming MIDI from a libremidi port)
    └── [application inputs]  (custom inputs added via omega_engine_add_input)
```

---

## EventSourceRegistry

```cpp
class EventSourceRegistry {
public:
    void   add(std::shared_ptr<EventSource> source);
    void   remove(EventSource* source);
    void   advance_all(uint64_t to_tick, EventDispatcher& out, ProcessContext& ctx);
    void   notify_transport_start(uint64_t start_tick);
    void   notify_transport_stop();
    void   notify_locate(uint64_t tick, EventDispatcher& chase_out, ProcessContext& ctx);
private:
    std::vector<std::shared_ptr<EventSource>> sources_;
};
```

`advance_all()` is called by the engine's `process()` each cycle. It iterates sources in registration order, calling `advance(to_tick, out, ctx)` on each. The `ProcessContext` carries the `ModulationBus`, `PerformanceContext`, and `InputBus` for this cycle. The registry itself is not modified during `advance_all()` — source addition and removal go through the command queue and take effect at the start of the next cycle.

`notify_locate()` calls `on_locate(tick, chase_out, ctx)` on every registered source. Sources that support chasing dispatch catch-up events via `chase_out`. See [11-orchestration-layer.md](11-orchestration-layer.md) for the full chasing design.

---

## PatternLibrary

```cpp
class PatternLibrary {
public:
    PatternId    create(std::string_view name, Ticks length);
    void         destroy(PatternId id);
    Pattern*     get(PatternId id);             // null if not found
    const Pattern* get(PatternId id) const;
    PatternId    find_by_name(std::string_view name) const;  // OMEGA_INVALID_ID if not found
    void         foreach(std::function<void(PatternId, const Pattern&)>) const;
};
```

`PatternId`s are never reused within a session. A destroyed pattern's ID is permanently retired. This prevents dangling references from pointing at a reallocated slot.

Internally: `std::pmr::vector<std::optional<Pattern>>`, indexed by ID. The optional is nullopt when a pattern has been destroyed. IDs are assigned sequentially.

---

## TimelineSource

`TimelineSource` is an `EventSource` that owns the `Timeline` — the multi-track linear recording data.

```cpp
class TimelineSource : public EventSource {
public:
    TrackId  create_track(std::string_view name);
    void     destroy_track(TrackId id);
    Track*   get_track(TrackId id);
    uint32_t track_count() const;

    void advance(uint64_t to_tick, EventDispatcher& out, ProcessContext& ctx) override;
    void on_transport_start(uint64_t start_tick) override;
    void on_transport_stop() override;
    void on_locate(uint64_t tick, EventDispatcher& chase_out, ProcessContext& ctx) override;
};

class Track {
public:
    std::string             name;
    SinkId                  sink_id;
    uint8_t                 channel;
    bool                    muted;
    bool                    soloed;
    std::pmr::vector<Event> events;   // sorted by tick
};
```

Solo logic: if any track is soloed, only soloed tracks emit events during `advance()`. Implemented inside `TimelineSource::advance()`.

---

## SongArrangementSource

`SongArrangementSource` is an `EventSource` that owns the `SongArrangement` — the ordered list of pattern references for linear song playback.

```cpp
struct PatternRef {
    PatternId  pattern_id;
    uint32_t   repeat_count;  // 0 = infinite loop (until manual stop)
};

class SongArrangementSource : public EventSource {
public:
    void append(PatternId id, uint32_t repeats = 1);
    void insert(size_t index, PatternId id, uint32_t repeats = 1);
    void remove(size_t index);
    Ticks total_length() const;

    void advance(uint64_t to_tick, EventDispatcher& out, ProcessContext& ctx) override;
    void on_transport_start(uint64_t start_tick) override;
    void on_transport_stop() override;
    void on_locate(uint64_t tick, EventDispatcher& chase_out, ProcessContext& ctx) override;

private:
    std::pmr::vector<PatternRef> refs_;
    size_t   current_ref_index_ = 0;
    uint32_t current_repeat_    = 0;
    uint64_t ref_start_tick_    = 0;
    const PatternLibrary* library_;  // non-owning reference to shared library
};
```

`SongArrangementSource` holds a non-owning reference to the `PatternLibrary` (owned by `Session`) to resolve `PatternId` references during `advance()`.

---

## PerformanceSource

`PerformanceSource` is an `EventSource` that owns the performance slot state and implements the slot state machine. See [05-pattern-state-machine.md](05-pattern-state-machine.md) for the full state machine specification.

```cpp
struct Slot {
    PatternId   pattern_id;           // OMEGA_INVALID_ID if empty
    SlotState   state;                // EMPTY, IDLE, QUEUED, PLAYING, STOPPING
    int8_t      transpose;            // -24 to +24 semitones
    uint8_t     velocity_scale;       // 0-200, 100=unity
    uint8_t     random_bias;          // 0-100%
    uint8_t     bias_range;           // max semitone offset for bias (default: 5)
    uint64_t    start_tick;           // tick when current loop began
    uint32_t    loop_count;           // completed loops since last start
};

class PerformanceSource : public EventSource {
public:
    void advance(uint64_t to_tick, EventDispatcher& out, ProcessContext& ctx) override;
    void on_transport_start(uint64_t start_tick) override;
    void on_transport_stop() override;
    void on_locate(uint64_t tick, EventDispatcher& chase_out, ProcessContext& ctx) override;

private:
    std::array<Slot, OMEGA_MAX_SLOTS> slots_;
    const PatternLibrary* library_;   // non-owning reference
};
```

`OMEGA_MAX_SLOTS = 64` for v1. Exposed as a constant in the C API so UIs can size their grids accordingly. `PerformanceSource` also holds a non-owning reference to the `PatternLibrary`.

---

## ModulationBus

`ModulationBus` is a flat array of 256 named `float` channels owned by `Session`. Modulator sources write to it during `advance()`; playback sources read from it. The bus is made available to all sources via `ProcessContext`.

```cpp
class ModulationBus {
public:
    static constexpr uint32_t MAX_CHANNELS = 256;

    uint32_t register_channel(const char* name, float initial_value = 0.0f);
    uint32_t find_channel(const char* name) const;
    float    get(uint32_t channel_id) const;
    void     set(uint32_t channel_id, float value);
    void     snapshot(float* out, uint32_t count) const;  // safe from UI thread
};
```

Channel registration happens from the mutation thread before playback. Read/write from the timing thread uses no locks — naturally-aligned `float` reads and writes are hardware-atomic on all target architectures. See [11-orchestration-layer.md](11-orchestration-layer.md) for the full design.

---

## PerformanceContext and GrooveLibrary

`PerformanceContext` is a small POD struct owned by `Session` holding shared musical state read by multiple sources each cycle. It is passed by reference through `ProcessContext`.

```cpp
struct PerformanceContext {
    Scale    scale;              // musical key/scale (root + semitone bitmask)
    Chord    chord;              // current chord (root, type, voices)
    int8_t   global_transpose;  // applied globally to all note-ons, -24 to +24
    uint8_t  global_velocity;   // 0–200, 100=unity; applied globally
    uint8_t  chaos;             // 0–100; global randomness influence
    uint8_t  groove_id;         // index into GrooveLibrary; 0 = straight timing
    float    swing;             // 0.0–1.0; applied to the active groove template
    uint32_t random_seed;       // advances deterministically each process() cycle
};
```

`GrooveLibrary` holds named timing/velocity templates applied by groove-aware sources. It is also owned by `Session`.

```cpp
struct GrooveTemplate {
    static constexpr uint32_t STEPS = 16;  // sixteenth-note resolution
    int8_t  tick_offset[STEPS];             // timing nudge per step, in ticks
    int8_t  velocity_offset[STEPS];         // velocity adjustment per step, signed
};

class GrooveLibrary {
public:
    uint8_t              add(std::string_view name, const GrooveTemplate& tmpl);
    const GrooveTemplate* get(uint8_t id) const;
    uint8_t              find_by_name(std::string_view name) const;
};
```

Pre-loaded templates: `STRAIGHT` (id=0), `MPC_SWING_54`, `MPC_SWING_58`, `808_SHUFFLE`, `BOSSA`, `JAZZ_SHUFFLE`. Mutations to `PerformanceContext` fields go through the command queue (`SetScaleCmd`, `SetChordCmd`, `SetGrooveCmd`, `SetChaosCmd`, etc.).

---

## EventInputRegistry

`EventInputRegistry` holds all registered `EventInput` instances. The engine's `process()` calls `poll()` on each registered input before calling `advance_all()`, filling the `InputBus` for that cycle.

```cpp
class EventInputRegistry {
public:
    void add(std::shared_ptr<EventInput> input);
    void remove(EventInput* input);
    void poll_all(InputDispatcher& dispatcher);  // called at top of process()
};
```

`EventInput` instances are session-ephemeral — they are not serialized to the `.omega` file. Hardware connections are re-established by the application on each load.

---

## SinkRegistry

The application registers output sinks with the session before playback. The session stores sink pointers by ID. Events reference sinks by `SinkId`, not by pointer.

```cpp
class SinkRegistry {
public:
    SinkId   register_sink(std::string_view name, std::unique_ptr<OutputSink> sink);
    void     unregister_sink(SinkId id);
    OutputSink* get(SinkId id);  // null if not found
    SinkId   find_by_name(std::string_view name) const;
};
```

The registry owns the `OutputSink` objects (via unique_ptr). This is the one place in the library where ownership of a sink transfers to the session.

For the C API: `omega_sink_t` handles are registered via `omega_engine_add_sink()`. Internally, the engine wraps the C callback in a `CSink : public OutputSink` adapter.

---

## Mode Coexistence

The three built-in sources run simultaneously every engine cycle. There is no mode switch — all registered sources are always active. Silencing a source is done by muting it (TimelineSource: mute all tracks; SongArrangementSource: no active arrangement; PerformanceSource: all slots IDLE) or by removing it from the engine entirely.

This design is an intentional consequence of the `EventSource` abstraction: sources are independent, and the engine does not privilege any one over another. A live performance setup naturally has `TimelineSource` playing a backing track while `PerformanceSource` handles live melodic improvisation — no special configuration required.

---

## Session Lifecycle

```
Application startup:
  engine = omega_engine_create(config)
  sink = omega_sink_create(my_callback, my_data)
  omega_engine_add_sink(engine, sink)

Load session:
  omega_smf_import(engine, "song.mid")    // or native format

Play:
  omega_transport_play(engine)
  // call omega_process(engine) in your timing loop

Modify:
  omega_track_add_event(engine, track_id, &event)  // enqueues command

Save:
  omega_smf_export(engine, "song.mid", 1)

Shutdown:
  omega_transport_stop(engine)
  omega_sink_destroy(sink)
  omega_engine_destroy(engine)
```

---

## Persistence Format

### Native Format (v1: JSON)

For v1, the native session format is JSON. It is human-readable, diffable, and trivially inspectable without special tools. Compact binary format is deferred to v2.

File extension: `.omega`

Top-level structure:
```json
{
  "omega_version": "0.1.0",
  "metadata": { "name": "...", "author": "..." },
  "tempo_map": [ { "tick": 0, "bpm_milli": 120000 } ],
  "time_signature": { "numerator": 4, "denominator": 4 },
  "pattern_library": [ { "id": 1, "name": "Groove A", "length_ticks": 1920, "events": [...] } ],
  "timeline": { "tracks": [...] },
  "song_arrangement": [ { "pattern_id": 1, "repeat_count": 4 } ],
  "performance": { "slots": [...] },
  "modulation_bus": {
    "channels": [
      { "id": 0, "name": "lfo_speed", "value": 0.5 }
    ]
  },
  "performance_context": {
    "scale": { "root": 0, "mask": 2741 },
    "chord": { "root": 0, "type": "MAJ", "voices": [60, 64, 67] },
    "global_transpose": 0,
    "global_velocity": 100,
    "chaos": 0,
    "groove_id": 0,
    "swing": 0.5,
    "random_seed": 42
  },
  "groove_library": [
    { "id": 1, "name": "MPC_SWING_54", "tick_offsets": [...], "velocity_offsets": [...] }
  ]
}
```

`EventInput` instances are not serialized — they are session-ephemeral (hardware connections re-established on each load). Custom `EventSource` implementations are also session-ephemeral.

Events are encoded as arrays for compactness: `[tick, type, channel, data...]`.

### SMF Import/Export

Standard MIDI File import reads type 0 and type 1 files into the Timeline. Tempo changes in the SMF populate the TempoMap. Export writes the Timeline as type 0 (merged) or type 1 (per-track).

SMF import is built on midifile (Stanford CCRMA, BSD licensed). We adapt their reader rather than writing our own SMF parser.

---

## Open Issues

- **Undo/redo**: Commands should be invertible (TSE3 pattern). The `CommandHistory` stack lives alongside the Session, not inside it.
- **Large sessions**: JSON becomes slow for sessions with tens of thousands of events. Profile before switching to binary. If binary is needed, use a simple tag-length-value format.
- **Concurrent sessions**: Can an application load two sessions simultaneously (A/B comparison, live set switching)? The Engine holds one active Session reference. Switching is instantaneous (pointer swap at session boundary) but the inactive session must remain valid.
