# Omega — Development Roadmap

This roadmap covers every milestone from first implementation commit through v1.0.0. Each
milestone has a definition of done that is objective enough for any session to pick up cold.
Sprint breakdowns are sized for 1–3 focused coding sessions each.

---

## Overview

| Milestone | Tag | Description |
|---|---|---|
| M0 | — | Infrastructure complete (done) |
| M1 | — | Core engine builds and passes first tests |
| M2 | `v0.1.0-alpha` | Single-track playback through C API |
| M3 | `v0.2.0-alpha` | All three built-in sources working |
| M4 | `v0.3.0-alpha` | Orchestration layer (inputs, modulation, perf context) |
| M5 | `v0.4.0-alpha` | Platform integrations (real MIDI I/O, SMF import/export) |
| M6 | `v0.5.0-beta` | Polish, coverage, valgrind-clean — release candidate |
| v1.0.0 | `v1.0.0` | ABI-stable public release |

---

## M0 — Infrastructure (DONE)

Everything needed to write code without CI friction. Completed 2026-05-20.

**Deliverables (all committed to main):**
- CMake build system with `FetchContent` deps (libremidi, midifile, Catch2)
- CI matrix: GCC 12, Clang 18, Apple Clang, MSVC on every PR
- ASan/UBSan job, TSan job, clang-format check, clang-tidy on changed files
- Install smoke test (`cmake/smoke_test/`), coverage on main push, benchmark scaffolding
- `include/omega/export.h` (visibility macros), `include/omega/omega.h` (API skeleton)
- `tests/` structure, Catch2 wired up, placeholder test compiles and passes
- `.clang-format`, `.clang-tidy`, `.editorconfig`, `.pre-commit-config.yaml`
- `THIRD_PARTY_LICENSES.md`, `abi/PLACEHOLDER`, `docs/release-checklist.md`

**Definition of done:** All CI jobs pass on main. ✓

---

## M1 — Core Engine (First Real Code)

Build the lock-free infrastructure and data types. No playback yet — just the skeleton that
`engine.process()` will run on.

### Sprint 1.1 — Data Types and Constants

**Files to create:**
- `include/omega/types.h` — `Event` (24-byte), `omega_tick_t`, `omega_sink_id_t`, `OMEGA_PPQN`
- `src/types.cpp` — `omega_status_string()`, `omega_version()`, `omega_make_note_on()`, `omega_make_cc()`, `omega_make_program()`
- Wire `omega_core` as a `STATIC` library in `CMakeLists.txt` (replace `INTERFACE` stub)

**Event struct spec** (from doc 03):
```cpp
struct Event {
    uint64_t tick;       // absolute ticks from session start
    uint32_t sink_id;    // target OutputSink
    uint8_t  payload_tag;
    uint8_t  channel;
    uint8_t  reserved[2];
    uint8_t  data[8];    // inline payload
};
static_assert(sizeof(Event) == 24);
```

**Definition of done:**
- `sizeof(Event) == 24` verified by a `static_assert` and a `TEST_CASE`
- `omega_version()` returns `{0, 1, 0}`
- `omega_status_string(OMEGA_OK)` returns `"ok"` (or similar non-null string)
- All three make helpers produce correct byte layouts verified by unit tests
- CI passes

### Sprint 1.2 — SPSC Queue

**Files to create:**
- `include/omega/detail/spsc_queue.h` — templated, header-only, no external deps

**Spec** (from doc 02):
```cpp
template<typename T, uint32_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "must be power of two");
    alignas(64) std::atomic<uint32_t> head_{0};
    alignas(64) std::atomic<uint32_t> tail_{0};
    T storage_[Capacity];
public:
    bool push(T&& item);  // producer; false if full
    bool pop(T& out);     // consumer; false if empty
    bool empty() const;
    uint32_t size() const;
};
```

**Definition of done:**
- `push()` / `pop()` contract: returns false when full/empty, never blocks
- TSan job passes with a two-thread stress test (producer pushes 1M items, consumer pops)
- Capacity must be a power of two — `static_assert` enforces it
- `alignas(64)` on both index variables (cache line separation)

### Sprint 1.3 — Command Variants

**Files to create:**
- `include/omega/commands.h` — all `Command` variants as value types

**Initial variant set** (from doc 02):
```cpp
struct AddEventCmd    { TrackId track; Event event; };
struct DeleteEventCmd { TrackId track; uint64_t tick; uint32_t index; };
struct SetTempoCmd    { uint32_t bpm_milli; };
struct SetLoopCmd     { uint64_t start_tick; uint64_t end_tick; bool enabled; };
struct CuePatternCmd  { SlotId slot; PatternId pattern; CueMode mode; };
struct TransportCmd   { TransportAction action; uint64_t locate_tick; };

using Command = std::variant<
    AddEventCmd, DeleteEventCmd, SetTempoCmd,
    SetLoopCmd, CuePatternCmd, TransportCmd
>;
```

**Definition of done:**
- `Command` fits in a compile-time-checked size (≤ 64 bytes — add a `static_assert`)
- `SpscQueue<Command, 4096>` compiles and passes the two-thread stress test

### Sprint 1.4 — TempoMap

**Files to create:**
- `include/omega/tempo_map.h` + `src/tempo_map.cpp`

**Spec** (from doc 01):
- `TempoPoint { uint64_t tick; uint32_t bpm_milli; uint64_t ns_at_tick; }`
- `TempoMap::insert(tick, bpm_milli)` — inserts and recomputes `ns_at_tick` for all subsequent points
- `TempoMap::ticks_to_ns(uint64_t ticks) const`
- `TempoMap::ns_to_ticks(uint64_t ns) const`
- Default: one point at tick=0, bpm_milli=120000
- All arithmetic uses the integer formulas from doc 01 — no `double`

**Definition of done:**
- `ticks_to_ns(480, 120000)` == `500'000'000` (one beat at 120 BPM = 500 ms) ± 1
- `ns_to_ticks(500'000'000, 120000)` == 480 ± 0 (round-trip exact)
- Multi-point map: insert tempo change mid-session, verify tick↔ns round-trips at each segment
- All tests pass; no floating-point operations in the hot-path functions

### Sprint 1.5 — Engine Skeleton + process()

**Files to create:**
- `include/omega/engine.h` + `src/engine.cpp`

**Minimal Engine public interface:**
```cpp
class Engine {
public:
    explicit Engine(std::pmr::memory_resource* mr = nullptr,
                    uint32_t queue_capacity = 4096);
    ~Engine();

    // Mutation thread
    omega_status_t enqueue(Command cmd);

    // Timing thread
    void process();

    // State accessors (any thread; may read stale)
    TransportState transport_state() const;
    uint64_t       transport_position_ns() const;
};
```

**`process()` skeleton** (from doc 02):
1. Drain the command queue via `SpscQueue::pop()` in a loop; apply each command
2. If transport is stopped, return
3. Query clock: `current_ns = clock_.now_ns()`
4. Convert to ticks via `tempo_map_.ns_to_ticks(current_ns - session_start_ns_)`
5. (Sources not yet registered — no events fired)

**Definition of done:**
- `Engine` constructs and destructs without leaking (valgrind clean)
- Calling `process()` from one thread and `enqueue()` from another simultaneously passes TSan
- `SetTempoCmd` changes the tempo map correctly (verify via `transport_position_ns()` progression)
- `TransportCmd{PLAY}` / `TransportCmd{STOP}` round-trip correctly

---

## M2 — First Playback

Single-track linear playback through the C API. The milestone is: add a note to a track, press
play, and the note fires at the right tick via a `CapturingSink`. No real MIDI output yet.

### Sprint 2.1 — ClockSource + InternalClock

**Files to create:**
- `include/omega/clock.h` — `ClockSource` abstract base, `InternalClock` concrete impl
- `include/omega/test/mock_clock.h` — `MockClock` (public test utility)

**MockClock spec** (from CLAUDE.md):
```cpp
class MockClock : public ClockSource {
public:
    void advance_ticks(uint64_t ticks);   // advances by ticks at current tempo
    void advance_beats(double beats);
    void set_ns(uint64_t ns);
    uint64_t now_ns() const override;
};
```

**Definition of done:**
- `InternalClock::now_ns()` is monotonic across 1M calls in a tight loop
- `MockClock` can be advanced precisely and does not drift
- Engine accepts a clock at construction; defaults to `InternalClock`

### Sprint 2.2 — OutputSink + CapturingSink

**Files to create:**
- `include/omega/sink.h` — `OutputSink` abstract base (`send()`, `flush()`, `sink_id()`)
- `include/omega/test/capturing_sink.h` — `CapturingSink` (public test utility)

**CapturingSink spec:**
```cpp
class CapturingSink : public OutputSink {
public:
    size_t count() const;
    bool   has_note_on(uint8_t note, uint8_t channel = 0) const;
    bool   has_note_off(uint8_t note, uint8_t channel = 0) const;
    const Event& first() const;
    const Event& at(size_t index) const;
    void clear();
};
```

**Definition of done:**
- `CapturingSink` records every `send()` call in order
- `has_note_on()` / `has_note_off()` / `count()` return correct values
- `sink_id()` is unique per instance (auto-assigned, monotonically increasing)

### Sprint 2.3 — TimelineSource + TrackData

**Files to create:**
- `include/omega/timeline.h` + `src/timeline.cpp`
- `include/omega/track.h` — `Track { TrackId id; std::string name; std::pmr::vector<Event> events; omega_sink_id_t sink_id; uint8_t channel; bool muted; }`
- `src/timeline_source.cpp` — `TimelineSource : EventSource`

**TimelineSource::advance() logic:**
1. For each track (skip if muted)
2. Binary-search for the first event with `tick > last_dispatched_tick_`
3. Walk forward, dispatching all events with `tick <= to_tick`
4. Update `last_dispatched_tick_`

**Active notes table:** Track note-offs separately. On each cycle after dispatching, scan for
active notes past their `off_tick` and dispatch note-off events.

**Commands that must be handled:**
- `AddEventCmd` — insert into the correct position in the sorted vector (O(n) but safe off timing thread)
- `DeleteEventCmd` — remove by tick+index
- `SetTempoCmd` — update `TempoMap` (already in Sprint 1.4)

**Definition of done:**
- Add a note-on at tick 0 with duration 240 ticks; add a CapturingSink; call `process()` with MockClock
  advanced to tick 1 → note-on fires, note-off has not fired
- Advance to tick 241 → note-off fires
- Add 100 notes at random ticks; verify all fire in tick order
- Muted track fires nothing
- Track with no events calls `advance()` with zero dispatches

### Sprint 2.4 — C API Wiring (Minimal Surface)

**Files to create:**
- `src/omega_c.cpp` — C API implementation
- Convert `omega_core` from `INTERFACE` to `STATIC` in `CMakeLists.txt` (if not done in 1.1)
- Convert `omega` (the C wrapper) from `INTERFACE` to `STATIC`

**C API functions to implement in this sprint:**
```c
omega_engine_t omega_engine_create(const omega_config_t*);
void           omega_engine_destroy(omega_engine_t);
omega_clock_t  omega_clock_create_internal(void);
void           omega_clock_destroy(omega_clock_t);
omega_sink_t   omega_sink_create(omega_send_fn_t, void*);
void           omega_sink_destroy(omega_sink_t);
omega_sink_id_t omega_sink_id(omega_sink_t);
omega_status_t omega_engine_set_clock(omega_engine_t, omega_clock_t);
omega_status_t omega_engine_add_sink(omega_engine_t, omega_sink_t);
omega_status_t omega_transport_play(omega_engine_t);
omega_status_t omega_transport_stop(omega_engine_t);
void           omega_process(omega_engine_t);
omega_track_id_t omega_track_create(omega_engine_t, const char*);
omega_status_t   omega_track_set_sink(omega_engine_t, omega_track_id_t, omega_sink_id_t);
omega_status_t   omega_track_add_event(omega_engine_t, omega_track_id_t, const omega_event_t*);
omega_event_t  omega_make_note_on(...);
omega_event_t  omega_make_cc(...);
```

**Definition of done:**
- The install smoke test (`cmake/smoke_test/main.cpp`) compiles, installs, and runs without error
- A new integration test (`tests/integration/test_c_api_playback.cpp`) uses only the C API,
  adds a track with three notes, plays through a MockClock, and verifies all three notes fire
- `omega_engine_destroy()` with NULL is a no-op (no crash)
- All CI jobs pass including the install smoke test job

**Milestone tag: `v0.1.0-alpha`** — the library builds, installs, and can play events.

---

## M3 — All Three Built-in Sources

Add `SongArrangementSource` and `PerformanceSource`. After this milestone, all three playback
modes from the design documents are functional.

### Sprint 3.1 — PatternLibrary

**Files to create:**
- `include/omega/pattern.h` — `Pattern { PatternId id; std::string name; uint64_t length_ticks; std::pmr::vector<Event> events; }`
- `include/omega/pattern_library.h` + `src/pattern_library.cpp` — `PatternLibrary`

**Spec:**
- `PatternId` is `uint32_t`; IDs are assigned monotonically and never reused
- `PatternLibrary::create(name, length_ticks)` returns `PatternId`
- `PatternLibrary::get(PatternId)` returns `Pattern*` or null
- `PatternLibrary::destroy(PatternId)` removes (callers must stop using it)
- Events within a pattern are stored in a `std::pmr::vector<Event>` sorted by tick

**Definition of done:**
- Create 1000 patterns; get each by ID; all return correct name and length
- Destroy a pattern; get returns null; its ID is not reused
- Add events to a pattern; verify sorted order after each insertion

**C API for this sprint:**
```c
omega_pattern_id_t omega_pattern_create(omega_engine_t, const char*, omega_tick_t length);
omega_status_t     omega_pattern_destroy(omega_engine_t, omega_pattern_id_t);
omega_status_t     omega_pattern_add_event(omega_engine_t, omega_pattern_id_t, const omega_event_t*);
omega_status_t     omega_pattern_set_length(omega_engine_t, omega_pattern_id_t, omega_tick_t);
```

### Sprint 3.2 — SongArrangementSource

**Files to create:**
- `include/omega/song_arrangement.h` — `SongArrangement { std::vector<ArrangementEntry> entries; }` where `ArrangementEntry { PatternId pattern_id; uint32_t repeat_count; }`
- `src/song_arrangement_source.cpp` — `SongArrangementSource : EventSource`

**`advance()` logic:**
- Walk entries in order, each repeated `repeat_count` times
- Track current entry index and current repetition
- For each cycle, determine which entry is active at `to_tick`
- Dispatch events from the resolved pattern within the active time window
- On loop-back within an entry, wrap event ticks correctly

**Definition of done:**
- Two patterns A (length 960 ticks, one note at tick 0) and B (length 1920, note at tick 0 and 960)
- Arrangement: A×2, B×1 = 0–960, 960–1920, 1920–3840
- Advance through the full arrangement; verify all five notes fire at correct absolute ticks
- Pattern with `repeat_count = 0` is a no-op (skipped)
- Locating into the middle of an arrangement entry resumes correctly

**C API for this sprint:**
```c
omega_status_t omega_song_append(omega_engine_t, omega_pattern_id_t, uint32_t repeats);
omega_status_t omega_song_clear(omega_engine_t);
```

### Sprint 3.3 — PerformanceSource State Machine

**Files to create:**
- `include/omega/perf_slot.h` — `SlotState` enum, `PerfSlot` struct
- `src/performance_source.cpp` — `PerformanceSource : EventSource`

**State machine** (verbatim from doc 05):
```
EMPTY → IDLE → QUEUED → PLAYING → STOPPING → IDLE
```

**Required transitions:** all 20 transitions from doc 05 (assign, cue BOUNDARY, cue IMMEDIATE,
cue_stop BOUNDARY, cue_stop IMMEDIATE, stop_all, loop boundary, unassign).

**Per-slot real-time parameters:**
- `int8_t transpose` (-24 to +24); applied at dispatch time: `clamp(note + transpose, 0, 127)`
- `uint8_t velocity_scale` (0–200, 100=unity): `clamp((vel * scale) / 100, 1, 127)`
- `uint8_t random_bias` (0–100): probabilistic pitch offset ±5 semitones

**Loop boundary detection** (from doc 05):
```
boundary_tick = ceil_div(current_tick - slot.start_tick, pattern.length_ticks)
                * pattern.length_ticks + slot.start_tick
```

**Definition of done:**
- All 20 state transitions covered by `TEST_CASE` or `SECTION` entries
- PLAYING → STOPPING → IDLE fires note-off for all active notes at loop boundary
- Two slots playing simultaneously: each maintains independent state and note table
- Transpose +12 on a slot containing note 60: dispatched as note 72
- Velocity scale 50 on velocity 100 note: dispatched as velocity 50
- TSan job passes with mutations from one thread and `process()` from another

**C API for this sprint:**
```c
omega_status_t omega_perf_assign(omega_engine_t, omega_slot_id_t, omega_pattern_id_t);
omega_status_t omega_perf_cue(omega_engine_t, omega_slot_id_t, omega_cue_mode_t);
omega_status_t omega_perf_stop(omega_engine_t, omega_slot_id_t, omega_cue_mode_t);
omega_status_t omega_perf_stop_all(omega_engine_t, omega_cue_mode_t);
omega_status_t omega_perf_set_transpose(omega_engine_t, omega_slot_id_t, int8_t);
omega_status_t omega_perf_set_velocity_scale(omega_engine_t, omega_slot_id_t, uint8_t);
omega_status_t omega_perf_set_random_bias(omega_engine_t, omega_slot_id_t, uint8_t);
```

**Milestone tag: `v0.2.0-alpha`** — all three playback sources functional, tested, and
accessible via C API.

---

## M4 — Orchestration Layer

EventInput, InputBus, ModulationBus, PerformanceContext, TransformSource, and custom source/input
registration. After this milestone, the extension API is complete.

### Sprint 4.1 — EventInput + InputBus

**Files to create:**
- `include/omega/event_input.h` — `EventInput` abstract base (`poll(InputDispatcher&)`)
- `include/omega/input_bus.h` — `InputBus` (fixed-size event queue, 256 entries max)
- `include/omega/test/mock_event_input.h` — `MockEventInput` (public test utility)
- `src/event_input_registry.cpp`

**InputBus spec** (from doc 11):
- Fixed capacity (256 events); overflow increments a counter (returned by `omega_input_overflow_count()`)
- Cleared at the top of each `process()` call, before `poll()` is called on inputs
- All events in the bus are visible to all sources via `ProcessContext.input_bus`

**MockEventInput spec:**
```cpp
class MockEventInput : public EventInput {
public:
    void prime(Event e);   // enqueue an event to deliver on next poll()
    void poll(InputDispatcher& dispatcher) override;
};
```

**`process()` updated sequence** (add to Sprint 1.5 skeleton):
1. Drain command queue
2. Clear `input_bus_`
3. For each `EventInput` in registry: `input.poll(input_dispatcher_)`
4. Build `ProcessContext` with `input_bus_`
5. For each `EventSource`: `source.advance(to_tick, dispatcher, ctx)`
6. For each `OutputSink`: `sink.flush()`

**Definition of done:**
- Prime MockEventInput with 3 events; advance engine one cycle; all 3 appear in `ProcessContext.input_events`
- Overflow: prime 300 events; `omega_input_overflow_count()` returns 44 (300 - 256)
- `omega_engine_add_input()` / `omega_engine_remove_input()` go through the command queue (TSan clean)

**C API for this sprint:**
```c
omega_input_t  omega_input_create(const omega_input_desc_t*);
void           omega_input_destroy(omega_input_t);
omega_status_t omega_engine_add_input(omega_engine_t, omega_input_t);
omega_status_t omega_engine_remove_input(omega_engine_t, omega_input_t);
uint32_t       omega_input_overflow_count(omega_engine_t);
void           omega_deliver(omega_input_dispatcher_t, const omega_event_t*);
```

### Sprint 4.2 — ModulationBus

**Files to create:**
- `include/omega/modulation_bus.h` + `src/modulation_bus.cpp`

**Spec** (from doc 11):
- 256 named `float` channels, indexed by `omega_mod_channel_t` (`uint32_t`)
- `register(name, initial)` — assigns a channel index; returns `OMEGA_MOD_INVALID` if full
- `find(name)` — looks up a channel by name; returns `OMEGA_MOD_INVALID` if not found
- `get(ch)` / `set(ch, value)` — hot path; timing thread only; no locking
- `snapshot(out, count)` — copies all channel values atomically for UI reads

**Definition of done:**
- Register 256 channels; 257th returns `OMEGA_MOD_INVALID`
- `get()` / `set()` from the timing thread, `snapshot()` from the mutation thread — TSan clean
- A custom `EventSource` writes to channel 0; the next source reads channel 0 and gets the updated value within the same cycle

**C API for this sprint:**
```c
omega_mod_channel_t omega_mod_register(omega_engine_t, const char*, float initial);
omega_mod_channel_t omega_mod_find(omega_engine_t, const char*);
float               omega_mod_get(omega_engine_t, omega_mod_channel_t);
omega_status_t      omega_mod_set(omega_engine_t, omega_mod_channel_t, float);
omega_status_t      omega_mod_snapshot(omega_engine_t, float* out, uint32_t count);
```

### Sprint 4.3 — PerformanceContext

**Files to create:**
- `include/omega/perf_context.h` + `src/perf_context.cpp`

**Spec** (from doc 04):
```cpp
struct PerfContext {
    omega_scale_t scale;          // root (0-11) + semitone bitmask
    omega_chord_t chord;          // root, type, voices[6]
    int8_t        global_transpose;   // -24 to +24
    uint8_t       global_velocity;    // 0-200, 100=unity
    uint8_t       chaos;              // 0-100
    uint8_t       groove_id;
    float         swing;
    uint32_t      random_seed;
};
```

- Written via command queue from mutation thread (setters)
- Snapshotted into `ProcessContext` at the top of each `process()` call
- Read by all sources during `advance()`

**Definition of done:**
- `omega_ctx_set_transpose(engine, +12)` → next `process()` cycle, `ProcessContext.perf_ctx.global_transpose == 12`
- TSan: mutation thread sets scale, timing thread reads it via ProcessContext — clean
- `omega_ctx_get()` returns the last-committed context values

**C API for this sprint:**
```c
omega_status_t omega_ctx_set_scale(omega_engine_t, const omega_scale_t*);
omega_status_t omega_ctx_set_chord(omega_engine_t, const omega_chord_t*);
omega_status_t omega_ctx_set_transpose(omega_engine_t, int8_t);
omega_status_t omega_ctx_set_velocity(omega_engine_t, uint8_t);
omega_status_t omega_ctx_set_chaos(omega_engine_t, uint8_t);
omega_status_t omega_ctx_set_groove(omega_engine_t, uint8_t, float);
omega_status_t omega_ctx_get(omega_engine_t, omega_perf_ctx_t*);
```

### Sprint 4.4 — Custom EventSource + TransformSource

**Files to create:**
- `src/event_source_registry.cpp` — wraps source registration through the command queue
- `include/omega/transform_source.h` — `TransformSource : EventSource` (wraps another source, filters/transforms its output)

**Custom source registration (C API):**
```c
omega_source_t omega_source_create(const omega_source_desc_t*);
void           omega_source_destroy(omega_source_t);
omega_status_t omega_engine_add_source(omega_engine_t, omega_source_t);
omega_status_t omega_engine_remove_source(omega_engine_t, omega_source_t);
void           omega_dispatch(omega_dispatcher_t, const omega_event_t*);
```

**TransformSource:** Abstract base; subclasses override `transform(Event& e)` returning
`bool` (false = drop the event). Concrete example to ship: `ChannelFilterSource` (drops events
not on a specified MIDI channel).

**Source registration order:** The engine enforces MODULATOR → CONTEXT → PLAYBACK by maintaining
three priority buckets. Custom sources are assigned a priority at registration time.

**Definition of done:**
- Custom `advance_fn` is called from `process()` in the correct order relative to built-in sources
- `TransformSource` wrapping a `TimelineSource` drops all events on channel 1; verify via CapturingSink
- `omega_engine_remove_source()` followed immediately by `process()` — source is not called (TSan clean)
- `MockEventSource` (public test utility) primed with events; all events are dispatched at correct ticks

**Milestone tag: `v0.3.0-alpha`** — full extension API functional.

---

## M5 — Platform Integrations

Real MIDI I/O via libremidi, SMF import/export via midifile. After this milestone the library
is usable for real musical work.

### Sprint 5.1 — libremidi MIDI I/O Sink + Input

**Files to create:**
- `src/libremidi_sink.cpp` — `LibremidiSink : OutputSink` (wraps a `libremidi::midi_out`)
- `src/libremidi_input.cpp` — `LibremidiInput : EventInput` (wraps a `libremidi::midi_in`)
- `include/omega/midi_io.h` — public headers for both

**LibremidiSink:** `send()` translates `Event` payload_tag to MIDI bytes and sends via libremidi.
`flush()` is a no-op (libremidi sends immediately on `send_message()`).

**LibremidiInput:** Constructor opens a port; `poll()` drains pending messages from libremidi's
message queue, converts to `Event`, calls `dispatcher.deliver()`.

**Definition of done:**
- On Linux (CI): enumerate MIDI ports without crashing (even when no ports exist)
- `LibremidiSink` translates `OMEGA_EVENT_NOTE_ON` to correct 3-byte MIDI message
- `LibremidiInput` delivers at least note-on, note-off, CC events correctly
- If no MIDI hardware is present, construction succeeds but `send()` is a no-op with `OMEGA_ERR_IO` returned

**C API for this sprint:**
```c
omega_sink_t  omega_sink_create_midi_out(const char* port_name);
omega_input_t omega_input_create_midi_in(const char* port_name);
// port_name NULL = first available port; "" = virtual port
```

### Sprint 5.2 — SMF Import

**Files to create:**
- `src/smf_import.cpp` — `omega_smf_import()` implementation using midifile

**Spec:**
- Opens an SMF file via midifile's `MidiFile::read()`
- For each track: create an omega track, convert all MIDI events to omega `Event` structs
- Tempo map events (FF 51) update the engine's tempo map
- Convert SMF ticks to omega ticks: `omega_tick = smf_tick * (OMEGA_PPQN / smf_ppqn)` — handle non-480 PPQN files
- Bulk-insert all events via `AddEventCmd` (one command per event — acceptable for import path)
- Note-off events: convert to note-on duration if the matching note-on is in the same track

**Definition of done:**
- Import a known SMF file (include a small test .mid in `tests/fixtures/`)
- Verify track count, tempo, and that the first note fires at the correct tick
- Import a Type 0 and a Type 1 SMF — both work
- Import a file with a non-480 PPQN division — ticks scale correctly

**C API:** `omega_status_t omega_smf_import(omega_engine_t, const char* path);`

### Sprint 5.3 — SMF Export

**Files to create:**
- `src/smf_export.cpp` — `omega_smf_export()` implementation using midifile

**Spec:**
- Iterate all tracks in `TimelineSource`; write events in tick order
- Export tempo map as FF 51 events
- Convert omega ticks back to SMF ticks (PPQN=480, so 1:1 for standard files)
- Type 0 for single track; Type 1 for multiple tracks

**Definition of done:**
- Export a session, re-import it, verify round-trip fidelity (same track count, tempos, events)
- Export-import round trip for a 4-track, 32-bar session with tempo changes

**C API:** `omega_status_t omega_smf_export(omega_engine_t, const char* path, int smf_type);`

### Sprint 5.4 — OmegaTimer

**Files to create:**
- `include/omega/timer.h` + `src/timer.cpp` — `OmegaTimer` RAII thread wrapper

**Spec** (from doc 02):
```cpp
class OmegaTimer {
public:
    OmegaTimer(Engine& engine, uint32_t interval_us = 1000);
    ~OmegaTimer();  // joins the thread; fires one last process() before exit
};
```

**Platform sleep:** `nanosleep` on Linux/macOS, `timeBeginPeriod` + `Sleep` on Windows.

**Definition of done:**
- Timer runs `process()` at ~1ms intervals; jitter < 5ms on an unloaded system
- `~OmegaTimer()` joins cleanly; no hung threads (verified with `valgrind --tool=helgrind`)
- TSan job passes with OmegaTimer + mutation thread doing `enqueue()` concurrently

**Milestone tag: `v0.4.0-alpha`** — real MIDI I/O and SMF import/export working.

---

## M6 — Polish (Beta)

Harden, cover, and document. The goal is a library that a third party could adopt.

### Sprint 6.1 — Coverage to 80 %

**Target:** 80% line coverage on the core library (excluding test utilities and platform I/O).

**Method:** Add unit tests for every untested branch. Use the `coverage` CI job output to identify
gaps. Focus on:
- TempoMap edge cases: empty map, single point, 100+ tempo changes
- SPSC queue boundary: exactly full (4096 items), one slot left
- PerformanceSource transitions that are hard to trigger manually
- Error paths: null handles, invalid IDs, queue full

**Definition of done:**
- `gcovr` reports ≥ 80% line coverage on CI
- No uncovered `OMEGA_ERR_*` branches in the C API implementation

### Sprint 6.2 — Valgrind and Sanitizer Clean

**Definition of done:**
- `valgrind --leak-check=full` on the test suite: 0 leaks, 0 errors
- ASan/UBSan CI job: 0 errors
- TSan CI job: 0 races
- Helgrind on `OmegaTimer` + stress test: 0 errors

### Sprint 6.3 — ABI Dump

**Steps:**
1. Build the library with debug symbols: `cmake -B build_abi -DCMAKE_BUILD_TYPE=RelWithDebInfo`
2. Run `abidw build_abi/libomega.so > abi/v0.5.0.dump`
3. Commit `abi/v0.5.0.dump`
4. Remove `abi/PLACEHOLDER`
5. Update `abi-check` CI job to use `abidiff` against the dump

**Definition of done:**
- `abi/v0.5.0.dump` committed and verified with `abidiff --no-unreferenced-symbols`
- CI `abi-check` job runs `abidiff` and fails if any incompatible change is introduced

### Sprint 6.4 — API Documentation

**Files to create or update:**
- `include/omega/omega.h` — every function documented with Thread, Returns, and Errors fields
- `README.md` — quick-start example: create engine, add track, add note, play for 2 beats, verify with CapturingSink
- `docs/ARCHITECTURE.md` — verify it reflects actual implementation (not just design)

**Definition of done:**
- Every public C API function has a doc comment
- README quick-start compiles and runs without modification
- No `TODO` or `FIXME` in public headers

### Sprint 6.5 — Benchmark Baselines

**Benchmarks to add** (from doc 12 §4.5):
- `bench.process_cycle` — `engine.process()` with 1 track, 0 due events (baseline overhead)
- `bench.command_enqueue` — `SpscQueue::push()` throughput (single-threaded)
- `bench.tempo_lookup` — `TempoMap::ns_to_ticks()` with 16-point map
- `bench.dispatch_1k` — `process()` with 1000 events due in one cycle

**Definition of done:**
- All four benchmarks run in CI and are uploaded as artifacts
- `bench.process_cycle` < 5 µs on a modern desktop (documented as informational, not a hard gate)

**Milestone tag: `v0.5.0-beta`**

---

## v1.0.0 — Stable Release

### Checklist (see also `docs/release-checklist.md`)

**Code:**
- [ ] All M6 definitions of done met
- [ ] ABI dump committed at `abi/v1.0.0.dump`; `abi-check` CI job enforces it
- [ ] `OMEGA_VERSION_MAJOR == 1` in `omega.h` and `CMakeLists.txt`
- [ ] `OMEGA_SOVERSION = 1` in `CMakeLists.txt`
- [ ] `CHANGELOG.md` `[Unreleased]` section promoted to `[1.0.0]` with release date

**Tests:**
- [ ] ≥ 80% line coverage (CI enforces)
- [ ] Valgrind clean, ASan/TSan clean
- [ ] All three built-in source state machines have full Catch2 coverage
- [ ] Integration test covers C API from a pure C consumer (no C++ in the test file)

**Documentation:**
- [ ] `docs/migration/v0-to-v1.md` finalized (no breaking changes since ABI starts fresh)
- [ ] README reflects current API with working quick-start
- [ ] All design docs updated to say "Implemented in v1.0.0" where applicable

**Release:**
- [ ] Tag `v1.0.0` on main; push tag
- [ ] CI builds the tag; all jobs green
- [ ] GitHub Release created with CHANGELOG section as release notes
- [ ] Codecov badge updated in README

**Definition of done:** The tag is pushed, all CI jobs on the tag pass, and the ABI dump
is committed. The library is installable via `cmake --install` and usable by a third-party
project that depends on it via `find_package(omega REQUIRED)`.

---

## Appendix: Source Registration Order

The engine enforces three priority buckets. Register in this order or the modulation/context
values will be stale when playback sources read them:

```
Priority 0 — MODULATOR sources  (write to ModulationBus)
Priority 1 — CONTEXT sources    (write to PerformanceContext)
Priority 2 — PLAYBACK sources   (read from both; dispatch events)
```

Built-in sources: `TimelineSource` and `SongArrangementSource` are Priority 2.
`PerformanceSource` is Priority 2. Custom modulator sources (LFO, etc.) are Priority 0.

---

## Appendix: Sprint Dependency Graph

```
M1.1 (types) → M1.2 (SPSC) → M1.3 (commands) → M1.5 (engine)
                                                    ↑
M1.4 (TempoMap) ───────────────────────────────────┘

M2.1 (clocks) → M2.2 (sinks) → M2.3 (timeline) → M2.4 (C API)  → v0.1.0-alpha
                                                    ↓
M3.1 (patterns) → M3.2 (song) ─────────────────────→ M3.3 (perf) → v0.2.0-alpha
                                                    ↓
M4.1 (inputs) → M4.2 (modbuf) → M4.3 (perfctx) → M4.4 (custom) → v0.3.0-alpha
                                                    ↓
M5.1 (MIDI I/O) → M5.2 (import) → M5.3 (export) → M5.4 (timer) → v0.4.0-alpha
                                                    ↓
M6.1–6.5 (polish) ──────────────────────────────────────────────→ v0.5.0-beta → v1.0.0
```

Each sprint in the same milestone can be worked in parallel if two sessions are available,
except where arrows indicate a strict dependency.
