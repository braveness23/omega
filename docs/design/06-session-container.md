# Design: Session Container

## What Is a Session

A `Session` is the top-level save/load unit. It owns all musical data and configuration. The `Engine` holds a reference to the active `Session` and operates on it.

The separation of `Engine` (the playback machine) from `Session` (the data) makes it possible to:
- Load a new session without destroying the engine
- Compare two sessions
- Implement save-as without side effects
- Test session data independently of the playback engine

---

## Ownership Hierarchy

```
Session
├── PatternLibrary           (owns all Pattern objects, indexed by PatternId)
├── Timeline                 (owns Track objects; references PatternIds)
│   ├── Track[0]
│   │   └── vector<Event>
│   ├── Track[1]
│   └── ...
├── SongArrangement          (ordered list of PatternRef: {pattern_id, repeat_count})
├── PerformanceConfig        (slot assignments and per-slot parameters)
│   ├── Slot[0]: {pattern_id, transpose, velocity_scale, bias}
│   ├── Slot[1]
│   └── ...
├── SinkRegistry             (named output sinks, registered by the host application)
├── TempoMap                 (list of TempoPoints; see timing model)
├── TimeSignature            (numerator, denominator — display/grid only, not timing)
├── LoopRegion               {start_tick, end_tick, enabled}
└── Metadata                 {name, author, created_at, modified_at}
```

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

## Timeline

The Timeline represents linear time and multi-track recording. It is the "tape deck" view.

```cpp
class Timeline {
public:
    TrackId  create_track(std::string_view name);
    void     destroy_track(TrackId id);
    Track*   get_track(TrackId id);
    uint32_t track_count() const;
};

class Track {
public:
    std::string      name;
    SinkId           sink_id;
    uint8_t          channel;
    bool             muted;
    bool             soloed;
    std::pmr::vector<Event> events;  // sorted by tick
};
```

Solo logic: if any track is soloed, only soloed tracks play. Implemented in the engine's event dispatch, not in the Track itself.

---

## Song Arrangement

The Song Arrangement describes how patterns are chained for linear playback in Pattern mode.

```cpp
struct PatternRef {
    PatternId  pattern_id;
    uint32_t   repeat_count;  // 0 = infinite loop (until manual stop)
};

class SongArrangement {
    std::pmr::vector<PatternRef> refs_;
public:
    void append(PatternId id, uint32_t repeats = 1);
    void insert(size_t index, PatternId id, uint32_t repeats = 1);
    void remove(size_t index);
    Ticks total_length() const;
};
```

---

## PerformanceConfig

```cpp
struct Slot {
    PatternId  pattern_id;           // OMEGA_INVALID_ID if empty
    int8_t     transpose;            // -24 to +24 semitones
    uint8_t    velocity_scale;       // 0-200, 100=unity
    uint8_t    random_bias;          // 0-100%
    uint8_t    bias_range;           // max semitone offset for bias (default: 5)
};

class PerformanceConfig {
    std::array<Slot, OMEGA_MAX_SLOTS> slots_;  // OMEGA_MAX_SLOTS = 64 for v1
public:
    Slot&       slot(SlotId id);
    const Slot& slot(SlotId id) const;
};
```

`OMEGA_MAX_SLOTS = 64` for v1. Exposed as a constant in the C API so UIs can size their grids accordingly.

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

## Mode Switching

The three modes (Timeline, Pattern, Performance) are views over the same session data. Switching modes does not destroy or reload data.

The Engine has a current **playback mode** that determines which data structure drives the playback scan:

- `TIMELINE`: Engine scans `session.timeline.tracks[*].events`
- `PATTERN`: Engine scans `session.song_arrangement` → resolves to pattern events
- `PERFORMANCE`: Engine scans active slots → resolves to pattern events

All three can be active simultaneously:
- Timeline plays the recorded tracks
- Performance slots play independently alongside the timeline

This is an intentional design decision: the modes are not mutually exclusive. A live performance setup might have Timeline playing a backing track while Performance slots handle live melodic improvisation.

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
  "performance": { "slots": [...] }
}
```

Events are encoded as arrays for compactness: `[tick, type, channel, data...]`.

### SMF Import/Export

Standard MIDI File import reads type 0 and type 1 files into the Timeline. Tempo changes in the SMF populate the TempoMap. Export writes the Timeline as type 0 (merged) or type 1 (per-track).

SMF import is built on midifile (Stanford CCRMA, BSD licensed). We adapt their reader rather than writing our own SMF parser.

---

## Open Issues

- **Undo/redo**: Commands should be invertible (TSE3 pattern). The `CommandHistory` stack lives alongside the Session, not inside it.
- **Large sessions**: JSON becomes slow for sessions with tens of thousands of events. Profile before switching to binary. If binary is needed, use a simple tag-length-value format.
- **Concurrent sessions**: Can an application load two sessions simultaneously (A/B comparison, live set switching)? The Engine holds one active Session reference. Switching is instantaneous (pointer swap at session boundary) but the inactive session must remain valid.
