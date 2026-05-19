# Design Proposal: Orchestration Layer

**Status**: Accepted. All affected design documents have been updated to reflect this design.

This document proposes four complementary additions that together allow Omega to orchestrate virtually any sequencer architecture: **EventInput**, **source routing via TransformSource composition**, **ModulationBus**, and **PerformanceContext**. As a corollary, these additions also close the **note and parameter chasing** gap identified separately.

---

## Motivation

The `EventSource` abstraction (design 10) defines how playback modes generate events. It gives the engine a pluggable output side for sources. What it does not provide is:

- A first-class abstraction for **incoming events** (MIDI in, OSC in, CV, sensors)
- A way for one source to transform or react to another source's output (no **event routing**)
- A mechanism for **continuous parameter modulation** independent of the discrete event stream
- A place to hold **shared musical state** (scale, chord, groove) that multiple sources need without being wired to each other
- A hook that lets sources **chase** to correct state when the transport locates to a mid-session position

Without these, the engine can play back stored data and run generative sources. It cannot close the loop — incoming events cannot influence outgoing events without ad-hoc implementation inside individual sources, and sources cannot share musical context without coupling to each other directly.

---

## The Enabling Change: `ProcessContext`

All four additions are threaded through a single new struct passed to every `EventSource::advance()` call. Rather than expanding the `advance()` parameter list for each addition, `ProcessContext` is the single extensible carrier:

```cpp
struct ProcessContext {
    ModulationBus&      mod_bus;     // continuous parameter channels (read/write)
    PerformanceContext& perf_ctx;    // shared musical environment (read/write)
    InputBus&           input_bus;   // incoming external events this cycle (read-only)
    uint64_t            cycle_ns;    // wall-clock duration of this process() cycle
};
```

`ProcessContext` fields are references — no allocation, no copying. An instance lives on the timing thread's call stack for the duration of `engine.process()` and is not accessible outside that scope.

### Updated `EventSource` Interface

```cpp
class EventSource {
public:
    // advance() now receives full process context alongside the dispatcher.
    virtual void advance(uint64_t to_tick, EventDispatcher& out, ProcessContext& ctx) = 0;

    // on_locate() now receives a dispatcher and context to enable chasing.
    // Sources may dispatch chase events immediately from within this call.
    virtual void on_locate(uint64_t tick, EventDispatcher& chase_out, ProcessContext& ctx) {}

    virtual void on_transport_start(uint64_t start_tick) {}
    virtual void on_transport_stop() {}
    virtual ~EventSource() = default;
};
```

This is a breaking change to the interface. All built-in sources and the C API adapter require updating. Since the library is pre-implementation, the cost is one interface definition and three class signatures — not a migration.

The updated `EventSourceRegistry`:

```cpp
class EventSourceRegistry {
public:
    void add(std::shared_ptr<EventSource> source);
    void remove(EventSource* source);
    void advance_all(uint64_t to_tick, EventDispatcher& out, ProcessContext& ctx);
    void notify_transport_start(uint64_t start_tick);
    void notify_transport_stop();
    void notify_locate(uint64_t tick, EventDispatcher& chase_out, ProcessContext& ctx);
private:
    std::vector<std::shared_ptr<EventSource>> sources_;
};
```

---

## 1. EventInput — Incoming Events

`EventInput` is the symmetric counterpart to `OutputSink`. Where `OutputSink` receives events from the engine and delivers them to the outside world, `EventInput` accepts events from the outside world and delivers them into the engine.

```cpp
class EventInput {
public:
    // Called from the timing thread during engine.process(), before advance() calls.
    // Drain any pending incoming events and deliver them via dispatcher.
    // Must not block or allocate.
    virtual void poll(InputDispatcher& dispatcher) = 0;
    virtual ~EventInput() = default;
};

class InputDispatcher {
public:
    // Deliver an event into the InputBus for this cycle.
    // If the engine is recording, also copies to the per-track staging buffer.
    virtual void deliver(const Event& e) = 0;
};
```

`EventInput` implementations include:

| Input | Description | Dependency |
|---|---|---|
| `MidiInputSource` | Drains incoming MIDI events from a libremidi port | libremidi (MIT) |
| `OscInputSource` | Receives OSC messages over UDP | optional |
| `MockEventInput` | Primed with events for testing; drains one at a time | None (test utility) |

### InputBus

`InputBus` is the per-cycle snapshot of all events delivered by `EventInput::poll()` calls. It is built fresh each cycle, made available to sources via `ProcessContext`, and discarded at the end of `process()`.

```cpp
class InputBus {
public:
    explicit InputBus(uint32_t capacity);  // capacity set from omega_config_t

    uint32_t       count() const;
    const Event&   at(uint32_t index) const;

    // Filtered access — O(n) linear scan; n is small in practice
    bool           has_type(uint8_t event_type) const;
    uint32_t       count_type(uint8_t event_type) const;
    const Event*   first_of_type(uint8_t event_type) const;
    const Event*   last_of_type(uint8_t event_type) const;

private:
    friend class InputDispatcher;
    void clear();
    void push(const Event& e);  // called by InputDispatcher; drops if full

    std::pmr::vector<Event> events_;   // capacity reserved at construction; never reallocated
    uint32_t count_ = 0;
};
```

If more events arrive in a single cycle than the configured capacity, excess events are dropped with a counter increment (queryable via `omega_input_overflow_count()`). The default capacity is 64, which is generous for typical MIDI input rates at 1ms cycle times. Embedded targets that constrain memory set a smaller value via `omega_config_t::input_bus_capacity`.

Sources that need incoming events call `ctx.input_bus.first_of_type(OMEGA_EVENT_NOTE_ON)` or iterate the bus during `advance()`. No subscription model is needed — bus traversal over a small fixed set is faster than any lookup structure.

### Recording Integration

`EventInput` supersedes the previously ad-hoc MIDI-input-to-recording-buffer path. When the engine is in record mode, `InputDispatcher::deliver()` copies events to the per-track staging ring buffer exactly as described in [03-memory-storage.md](03-memory-storage.md). The staging-and-commit model is unchanged; `EventInput` simply formalizes where events enter the system.

### MIDI Transport and Clock

When a `MidiInputSource` receives MIDI Start, Stop, Continue, or Clock messages, it delivers them to the `InputBus` as `OMEGA_EVENT_TRANSPORT` or `OMEGA_EVENT_MIDI_CLOCK` typed events. A `MidiClockSource` (already listed in design 07) can be implemented as a `ClockSource` that reads from the `InputBus` rather than from a hardware timer — keeping the clock abstraction intact.

---

## 2. Source Routing — Patch Graph via TransformSource Composition

Rather than introducing a separate graph registry, routing is expressed through two complementary mechanisms that require no new infrastructure.

### 2a. InputBus Broadcast (Input → Sources)

All `EventInput` events are broadcast to all sources via `ctx.input_bus`. Sources filter what they need. This is sufficient for most input-consuming sources (arpeggiators, chord detectors, reactive sources). No explicit subscription wiring is required.

### 2b. TransformSource Composition (Source → Source)

A `TransformSource` wraps an upstream `EventSource`. Its `advance()` calls the upstream source into a private intercepting dispatcher, transforms the intercepted events, and re-dispatches the results to the actual output dispatcher. Composition is via C++ construction — the engine's source registry still contains only root sources.

```cpp
class TransformSource : public EventSource {
public:
    explicit TransformSource(std::shared_ptr<EventSource> upstream);

    void advance(uint64_t to_tick, EventDispatcher& out, ProcessContext& ctx) override;
    void on_locate(uint64_t tick, EventDispatcher& chase_out, ProcessContext& ctx) override;
    void on_transport_start(uint64_t start_tick) override;
    void on_transport_stop() override;

protected:
    // Subclass intercepts each upstream event and decides what to emit.
    // May emit zero, one, or many output events per input event.
    virtual void transform(const Event& in, EventDispatcher& out, ProcessContext& ctx) = 0;

    std::shared_ptr<EventSource> upstream_;

private:
    // Inline intercepting dispatcher — no allocation
    struct InterceptDispatcher : public EventDispatcher {
        TransformSource* owner;
        EventDispatcher* final_out;
        ProcessContext*  ctx;
        void dispatch(const Event& e) override {
            owner->transform(e, *final_out, *ctx);
        }
    } intercept_;
};
```

`on_locate()` forwards to the upstream source first, then applies the transform to any chase events. The default `TransformSource` base class handles forwarding automatically; subclasses only override `transform()`.

### Provided Transform Sources

| Transform | Description |
|---|---|
| `ScaleQuantizerSource` | Snaps MIDI note numbers to the active scale in `perf_ctx` |
| `TransposeSource` | Shifts pitch by a fixed offset or a `ModulationBus` channel value |
| `VelocityCurveSource` | Applies a velocity curve (linear, exponential, S-curve, fixed table) |
| `HumanizerSource` | Adds bounded timing jitter and velocity scatter |
| `ChordSpreadSource` | Expands single notes into chords per a voicing table |
| `FilterSource` | Gates events matching a predicate (note range, CC range, type mask) |
| `DelaySource` | Delays all events by a fixed tick offset |

### Composition Example

```cpp
auto step_seq  = std::make_shared<StepSequencerSource>(pattern);
auto quantized = std::make_shared<ScaleQuantizerSource>(step_seq);
auto humanized = std::make_shared<HumanizerSource>(quantized, /*jitter_ticks=*/10);
engine.add_source(humanized);  // humanized is the root; pulls from chain
```

This is structurally identical to iterator adapters or pipeline stages — no separate graph registry or scheduler required. The engine's source list stays flat.

### What This Does Not Cover

Source-to-source event routing where one source's output is broadcast to multiple downstream sources (a fan-out) is not supported by composition alone. This case is uncommon in practice; if needed, a shared `EventBuffer` (filled by one source, read by several) is the mechanism. That design is deferred — fan-out is not required by any planned source.

---

## 3. ModulationBus — Continuous Parameter Control

The event stream carries discrete events. The `ModulationBus` carries continuous parameter values — updated every `process()` cycle by modulator sources, readable by any source or sink.

```cpp
class ModulationBus {
public:
    static constexpr uint32_t MAX_CHANNELS = 256;

    // Registration — call from mutation thread before playback.
    // Returns channel_id; returns OMEGA_MOD_INVALID if MAX_CHANNELS exceeded.
    uint32_t register_channel(const char* name, float initial_value = 0.0f);
    uint32_t find_channel(const char* name) const;

    // Timing-thread access — lock-free, relaxed atomic ordering
    float get(uint32_t channel_id) const;
    void  set(uint32_t channel_id, float value);

    // UI polling — copies all values to a caller-owned array
    // Safe to call from the mutation thread between process() cycles.
    void snapshot(float* out, uint32_t count) const;
};
```

All channel values are `float`. The range semantics are per-channel by convention — [0.0, 1.0] is recommended for normalized parameters, but not enforced. Channel registration assigns the range meaning; document it alongside the channel name.

**Thread safety note**: `ModulationBus` values are written by the timing thread (during modulator source `advance()`) and read by the mutation thread (via `snapshot()`). The implementation stores channels as `std::atomic<float>` with `memory_order_relaxed` for both `get()` and `set()`. Relaxed ordering is sufficient here: there is no dependent data guarded by these values that requires sequencing between threads — a slightly stale modulation value read by the UI is acceptable, and the timing thread only reads its own writes within a single `process()` cycle.

`volatile float[]` is **not** a correct alternative. `volatile` provides no memory ordering or atomicity guarantees in the C++ memory model; it is defined for hardware register access, not inter-thread communication. On ARM in particular, a non-atomic float read can observe a torn write. `std::atomic<float>` with relaxed ordering compiles to a plain load/store on x86 and ARM (no memory barriers generated), so the runtime cost is identical to `volatile` while being correct.

### Modulator Sources

Modulator sources are `EventSource` implementations that write to the bus during `advance()` instead of (or in addition to) dispatching events.

```cpp
class LfoSource : public EventSource {
public:
    // channel_id: the ModulationBus channel this LFO drives
    // rate_hz: oscillation rate (may be expressed as a beat fraction)
    LfoSource(uint32_t channel_id, float rate_hz, LfoShape shape);

    void advance(uint64_t to_tick, EventDispatcher& out, ProcessContext& ctx) override {
        float value = compute(to_tick);
        ctx.mod_bus.set(channel_id_, value);
        // Optionally also emit as a CC event for external gear:
        // out.dispatch(make_cc_event(sink_id_, cc_number_, value * 127.0f));
    }
};
```

`LfoShape` values: `SINE`, `TRIANGLE`, `SQUARE`, `SAW_UP`, `SAW_DOWN`, `RANDOM_STEPPED`, `RANDOM_SMOOTH`.

Other modulator sources: `EnvelopeSource` (ADSR driven by gate events from the `InputBus`), `StepModSource` (per-step values in a looping table), `ExpressionSource` (maps a MIDI CC from `InputBus` to a named channel).

### Consuming Modulation

Sources read bus values during `advance()`:

```cpp
// Inside StepSequencerSource::advance():
float prob_mod  = ctx.mod_bus.get(probability_mod_channel_);
bool  fire_step = step.probability + prob_mod > random_float();
```

```cpp
// Inside TransposeSource::transform():
float semitones = ctx.mod_bus.get(transpose_channel_) * 24.0f - 12.0f;
Event transposed = in;
transposed.data[0] = clamp(in.data[0] + (int)semitones, 0, 127);
out.dispatch(transposed);
```

### Source Ordering Convention

Modulator sources must run before the sources that consume them. Because sources execute in registration order, the convention is: **register modulator sources first**. This is documented, not enforced by code. A `SourcePriority` enum (`MODULATOR = 0`, `CONTEXT = 1`, `PLAYBACK = 2`) is reserved for a future ordering mechanism if the convention proves insufficient.

---

## 4. PerformanceContext — Shared Musical Environment

`PerformanceContext` is a small struct holding musical state that multiple sources need without point-to-point wiring. It lives in `ProcessContext` and is accessible to all sources every cycle.

```cpp
// Represents a scale as a root note + bitmask of active semitones in one octave
struct Scale {
    uint8_t root;   // 0–11, C=0
    uint8_t mask;   // bit i set → semitone i is in scale. 0b101011010101 = major
};

// Represents a chord (for chord-following arpeggiators and scale-aware sources)
struct Chord {
    uint8_t root;          // 0–11
    uint8_t type;          // OMEGA_CHORD_MAJ, _MIN, _DOM7, _MAJ7, _MIN7, _DIM, _AUG
    uint8_t voices[6];     // absolute MIDI note numbers; 0xFF = unused voice
    uint8_t voice_count;
};

struct PerformanceContext {
    Scale    scale;              // musical key/scale; all sources quantize against this
    Chord    chord;              // current chord; set by UI or by ChordDetectorSource
    int8_t   global_transpose;  // semitones applied globally, -24 to +24
    uint8_t  global_velocity;   // 0–200, 100=unity; applied globally to all note-ons
    uint8_t  chaos;             // 0–100; global randomness influence read by probabilistic sources
    uint8_t  groove_id;         // index into GrooveLibrary; 0 = straight timing
    float    swing;             // 0.0–1.0; 0.5 = straight; applies to groove template
    uint32_t random_seed;       // advances deterministically each process() cycle
};
```

`PerformanceContext` is owned by `Session` alongside `ModulationBus`. Mutations from the UI thread go through the command queue (`SetScaleCmd`, `SetChordCmd`, `SetGrooveCmd`, `SetChaosCmd`, etc.) and are applied at the top of `process()`.

Sources may write to `PerformanceContext` from the timing thread during `advance()`:

```cpp
// ChordDetectorSource: analyzes incoming note-ons and writes the detected chord
class ChordDetectorSource : public EventSource {
    void advance(uint64_t to_tick, EventDispatcher& out, ProcessContext& ctx) override {
        // Collect note-ons from InputBus this cycle
        // Run chord recognition algorithm on active note set
        ctx.perf_ctx.chord = detected_chord_;
        // No events dispatched — this source only updates context
    }
};
```

**Write ordering**: sources run in registration order. If two sources write the same field, last-writer-wins. Register `ChordDetectorSource` before sources that consume `perf_ctx.chord`. The registration-order convention (from ModulationBus source ordering) applies here too.

### GrooveLibrary

`GrooveLibrary` is a `Session` member (alongside `PerformanceContext`) holding named groove templates.

```cpp
struct GrooveTemplate {
    static constexpr uint32_t STEPS = 16;       // sixteenth-note resolution
    int8_t  tick_offset[STEPS];                  // timing nudge per step, in ticks
    int8_t  velocity_offset[STEPS];              // velocity adjustment per step, signed
};

class GrooveLibrary {
public:
    uint8_t     add(std::string_view name, const GrooveTemplate& tmpl);
    const GrooveTemplate* get(uint8_t id) const;
    uint8_t     find_by_name(std::string_view name) const;
};
```

Sources apply groove by reading `perf_ctx.groove_id`, fetching the template, and offsetting the event's tick and velocity before dispatching. The `TimelineSource`, `SongArrangementSource`, `PerformanceSource`, and `StepSequencerSource` all implement groove application natively. Custom sources opt in by reading the template themselves.

Pre-loaded templates in v1: `STRAIGHT`, `MPC_SWING_54`, `MPC_SWING_58`, `808_SHUFFLE`, `BOSSA`, `JAZZ_SHUFFLE`. Additional templates are additive — adding one never invalidates existing IDs.

### `random_seed` Determinism

`PerformanceContext::random_seed` advances by a fixed increment each process cycle. Probabilistic sources seed their per-cycle RNG from this value rather than from `std::rand()`. Two sessions with the same `random_seed` at playback start produce identical randomized output — useful for reproducible generative compositions. The seed is included in the session serialization format.

---

## 5. Note and Parameter Chasing

Chasing is not a new component — it falls out naturally from the updated `on_locate()` signature. Sources that need to chase implement the logic themselves inside `on_locate()`, which now has access to both a dispatcher and the process context.

```cpp
void TimelineSource::on_locate(uint64_t tick,
                                EventDispatcher& chase_out,
                                ProcessContext& ctx) {
    playhead_ = tick;

    for (auto& track : tracks_) {
        // Note chasing: find notes whose [start_tick, start_tick + duration) contains tick
        for (const auto& event : track.events) {
            if (event.type == OMEGA_EVENT_NOTE_ON) {
                uint64_t note_end = event.tick + note_duration(event);
                if (event.tick < tick && note_end > tick) {
                    Event chased = event;
                    chased.tick = tick;          // dispatch immediately
                    chase_out.dispatch(chased);
                    active_notes_.record(event); // ensures note-off fires at note_end
                }
            }
        }
        // CC chasing: find the most recent value of each CC before tick
        for (uint8_t cc = 0; cc < 128; ++cc) {
            const Event* last = track.last_cc_before(cc, tick);
            if (last) chase_out.dispatch(*last);
        }
        // Program change chasing
        const Event* prog = track.last_program_before(tick);
        if (prog) chase_out.dispatch(*prog);
    }
}
```

**Chasing is opt-in per source.** The default `on_locate()` does nothing — this preserves current behavior for sources that don't require it. Built-in sources (`TimelineSource`, `SongArrangementSource`, `PerformanceSource`) implement full chasing. Custom sources implement as needed.

**Chase events fire immediately.** The `chase_out` dispatcher routes events directly to `OutputSink` instances before the first `advance()` cycle at the new position. Chase events have the locate tick as their `tick` field; sinks receive them as if they were live events.

**Performance chasing** (`PerformanceSource`): on locate, each PLAYING slot computes its phase at the new tick position (`phase = (tick - start_tick) % pattern.length_ticks`). If any notes were active at that phase, they are chased. The slot does not restart — it resumes mid-pattern at the correct offset.

**Chase cost note**: for large sessions, scanning all events before the locate point may take several milliseconds. Chase therefore runs during the `notify_locate()` call, which is driven by the mutation thread (not the timing thread). The `chase_out` dispatcher must be thread-safe or the locate command must be applied at the start of the next timing cycle with the chase events pre-queued. This is an open issue — see below.

---

## Updated `engine.process()` Order

```
1. Drain command queue
   └─ Apply SetScaleCmd, SetChordCmd, SetGrooveCmd, SetModCmd, AddSourceCmd, etc.

2. Build ProcessContext on the stack
   └─ References to ModulationBus, PerformanceContext, InputBus

3. Clear InputBus from previous cycle

4. Poll all registered EventInputs
   └─ EventInput::poll(InputDispatcher) → InputBus filled

5. Call advance(to_tick, dispatcher, ctx) on all registered EventSources in order:
   │
   ├─ [MODULATOR priority] LfoSource, EnvelopeSource, StepModSource
   │   └─ Write ModulationBus channels
   │
   ├─ [CONTEXT priority] ChordDetectorSource, KeyDetectorSource
   │   └─ Write PerformanceContext fields
   │
   └─ [PLAYBACK priority] TimelineSource, SongArrangementSource, PerformanceSource,
                           StepSequencerSource, HumanizerSource, ... (root transforms)
       └─ Read ModBus, PerfCtx, InputBus; dispatch events

6. For each dispatched event: route to OutputSink by sink_id

7. Call flush() on all sinks (MIDI batching, OSC bundle send, etc.)
```

---

## Impact on Existing Design Documents

| Document | Change needed |
|---|---|
| [02-thread-model.md](02-thread-model.md) | Add EventInput poll step to process() order; update process() diagram; note that chase runs on mutation thread |
| [04-c-api-design.md](04-c-api-design.md) | Update `omega_advance_fn_t` and `omega_locate_fn_t` signatures; add EventInput, ModBus, PerfCtx C API sections |
| [06-session-container.md](06-session-container.md) | Session owns `ModulationBus`, `PerformanceContext`, `GrooveLibrary`, `EventInputRegistry` alongside existing members |
| [07-extensions.md](07-extensions.md) | Add EventInput as sixth extension point; update EventSource interface in code block; add ModulationBus and PerformanceContext sections |
| [08-testing-strategy.md](08-testing-strategy.md) | Add `MockEventInput` as test utility; add chasing test patterns and modulation bus test patterns |
| [10-event-source-abstraction.md](10-event-source-abstraction.md) | Update `EventSource` interface (ProcessContext parameter); add `on_locate()` to interface; note TransformSource as provided base class |
| [ARCHITECTURE.md](../ARCHITECTURE.md) | Update ASCII diagram to show EventInput layer; update key decisions table |

---

## C API Additions

### EventInput

```c
typedef struct omega_input_disp_s* omega_input_dispatcher_t;
typedef void (*omega_input_poll_fn_t)(omega_input_dispatcher_t dispatcher, void* userdata);

typedef struct {
    omega_input_poll_fn_t poll;
    void*                 userdata;
} omega_input_desc_t;

typedef struct omega_input_s* omega_input_t;

omega_input_t  omega_input_create(const omega_input_desc_t* desc);
void           omega_input_destroy(omega_input_t input);
omega_status_t omega_engine_add_input(omega_engine_t engine, omega_input_t input);
omega_status_t omega_engine_remove_input(omega_engine_t engine, omega_input_t input);

/* Call from within poll callback to deliver an event into the InputBus */
void omega_deliver(omega_input_dispatcher_t dispatcher, const omega_event_t* event);

/* Query how many input events were dropped this cycle (InputBus overflow) */
uint32_t omega_input_overflow_count(omega_engine_t engine);
```

### ModulationBus

```c
typedef uint32_t omega_mod_channel_t;
#define OMEGA_MOD_INVALID UINT32_MAX

omega_mod_channel_t omega_mod_register(omega_engine_t engine, const char* name, float initial);
omega_mod_channel_t omega_mod_find(omega_engine_t engine, const char* name);
float               omega_mod_get(omega_engine_t engine, omega_mod_channel_t ch);
omega_status_t      omega_mod_set(omega_engine_t engine, omega_mod_channel_t ch, float value);

/* Copy all channel values to caller-owned array; safe from UI thread */
omega_status_t omega_mod_snapshot(omega_engine_t engine, float* out, uint32_t count);
```

### PerformanceContext

```c
typedef struct {
    uint8_t root;   /* 0–11, C=0 */
    uint8_t mask;   /* semitone bitmask */
} omega_scale_t;

typedef struct {
    uint8_t root;
    uint8_t type;        /* OMEGA_CHORD_MAJ, _MIN, _DOM7, _MAJ7, _MIN7, _DIM, _AUG */
    uint8_t voices[6];   /* MIDI note numbers; 0xFF = unused */
    uint8_t voice_count;
} omega_chord_t;

typedef struct {
    omega_scale_t scale;
    omega_chord_t chord;
    int8_t        global_transpose;
    uint8_t       global_velocity;
    uint8_t       chaos;
    uint8_t       groove_id;
    float         swing;
    uint32_t      random_seed;
} omega_perf_ctx_t;

omega_status_t omega_ctx_set_scale(omega_engine_t engine, const omega_scale_t* scale);
omega_status_t omega_ctx_set_chord(omega_engine_t engine, const omega_chord_t* chord);
omega_status_t omega_ctx_set_transpose(omega_engine_t engine, int8_t semitones);
omega_status_t omega_ctx_set_velocity(omega_engine_t engine, uint8_t scale_pct);
omega_status_t omega_ctx_set_chaos(omega_engine_t engine, uint8_t chaos_pct);
omega_status_t omega_ctx_set_groove(omega_engine_t engine, uint8_t groove_id, float swing);

/* Read current context — for UI display; may read slightly stale values */
omega_status_t omega_ctx_get(omega_engine_t engine, omega_perf_ctx_t* out);
```

### Updated `advance` and `on_locate` Signatures (C API)

```c
/* omega_process_ctx_t — passed to advance and on_locate callbacks */
typedef struct {
    /* ModulationBus access */
    float  (*mod_get)(omega_mod_channel_t ch, void* mod_userdata);
    void   (*mod_set)(omega_mod_channel_t ch, float value, void* mod_userdata);
    void*    mod_userdata;

    /* PerformanceContext — read-only snapshot for this cycle */
    const omega_perf_ctx_t* perf_ctx;

    /* InputBus — array valid only for the duration of this advance() call */
    const omega_event_t* input_events;
    uint32_t             input_event_count;

    /* Timing */
    uint64_t cycle_ns;
} omega_process_ctx_t;

/* Updated function pointer types */
typedef void (*omega_advance_fn_t)(uint64_t to_tick,
                                    omega_dispatcher_t dispatcher,
                                    const omega_process_ctx_t* ctx,
                                    void* userdata);

typedef void (*omega_locate_fn_t)(uint64_t tick,
                                   omega_dispatcher_t chase_dispatcher,
                                   const omega_process_ctx_t* ctx,
                                   void* userdata);

/* Updated source descriptor */
typedef struct {
    omega_advance_fn_t         advance;
    omega_transport_start_fn_t on_start;    /* nullable */
    omega_transport_stop_fn_t  on_stop;     /* nullable */
    omega_locate_fn_t          on_locate;   /* nullable */
    void*                      userdata;
} omega_source_desc_t;
```

---

## Session Serialization Additions

The `.omega` JSON format gains three new top-level keys:

```json
{
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

`EventInput` instances are not serialized — they are session-ephemeral (hardware connections re-established on each load). This matches the existing behavior for custom `EventSource` implementations.

---

## Open Issues

- **Chase thread safety**: `on_locate()` with a chase dispatcher needs to run on the timing thread to avoid data races with the active notes table and event vectors. But large sessions may scan thousands of events during chase, violating the "no blocking on the timing thread" rule. Proposed resolution: the `locate` command snapshots a chase-event list on the mutation thread (read-only scan of immutable event data), then the timing thread applies the pre-built chase list atomically at the start of the next cycle. Design deferred.

- **PerformanceContext write conflicts**: Two sources writing the same field in the same cycle produce last-writer-wins behavior determined by registration order. If this becomes a pain point, a priority field on source registration (`OMEGA_SOURCE_PRIORITY_MODULATOR / CONTEXT / PLAYBACK`) could enforce ordering without relying on convention.

- **ModulationBus channel count**: `MAX_CHANNELS = 256` is a compile-time constant. Make it a construction-time parameter (like command queue capacity) before v1 ships, so embedded targets can shrink it.

- **InputBus overflow policy**: Currently events are dropped when `InputBus` is full. An alternative is a ring buffer that overwrites the oldest events. For most use cases (MIDI input at 1ms cycle rate), 64 events is never reached — but document the policy and make the capacity configurable.

- **TransformSource chain locate cost**: A chain of five `TransformSource` wrappers calls five `on_locate()` calls recursively. Each may scan backward through event history. The total cost could be significant. Consider a `on_locate()` fast path that only resets the playhead and defers event scanning to the first `advance()` call. Opt-in via a `OMEGA_LOCATE_LAZY_CHASE` flag on the source descriptor.

- **GrooveLibrary IDs**: `uint8_t` limits the library to 256 templates including the 6 built-in ones. Should be sufficient for v1. Promote to `uint16_t` in v2 if needed.

- **C API ProcessContext lifetime**: The `omega_process_ctx_t*` pointer passed to `advance_fn_t` and `locate_fn_t` is valid only for the duration of that call. Document explicitly. The `input_events` array inside it is also stack-lifetime — callers must not cache the pointer.
