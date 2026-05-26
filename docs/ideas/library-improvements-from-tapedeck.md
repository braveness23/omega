# Library Improvements — lessons from the tapedeck example

Source: observations made while writing `examples/tapedeck/` (2026-05-25).
Every friction point below was hit during development; workarounds are
described by file and line number so the cost is concrete, not hypothetical.

---

## Executive Summary

Four architectural patterns account for most of the friction:

1. **The C API is write-only.** ~70 mutation functions exist, but almost zero
   query functions. You can create and modify; you cannot observe. Any
   application that needs to display or serialize state must bypass the C ABI
   and reach into the C++ layer.

2. **Transport position lives in two coordinate systems.** The engine works in
   ticks internally. The only public position query returns nanoseconds.
   Applications that think in bars and beats must undo the tempo map in
   floating point, duplicating internal logic that the library already knows
   how to do.

3. **Loop playback requires the wrong source.** `TimelineSource` (the tape
   model) has no loop primitive. `PerformanceSource` (the clip launcher) can
   repeat a pattern, but it's the wrong abstraction, and `SetLoopCmd` — which
   *should* solve this — is defined but never handled by the engine.

4. **Event payload encoding leaks internal layout.** `omega_make_note_on`
   creates events; nothing decodes them. Reading note pitch, velocity, or
   duration from an existing event requires knowing that duration lives at
   `e.data[2..5]` — a byte layout that is not documented anywhere in the
   public headers.

---

## Specific Issues

---

### 1. Implement `SetLoopCmd` on `TimelineSource`

**Impact:** High

**Problem:** `SetLoopCmd` is declared in `commands.h` and appears in the
command variant, but `Engine::apply()` never handles it. The natural model for
a tape loop or linear looping sequencer is a transport loop region on
`TimelineSource`. Because it is not implemented, the tapedeck was forced to use
`PerformanceSource` (a clip launcher) to achieve repetition:

```cpp
// examples/tapedeck/main.cpp L884–891
engine.enqueue(TransportCmd{TransportAction::PLAY, 0});
if (!slot_cued) {
    engine.enqueue(PerfCueCmd{0, CueMode::IMMEDIATE});
    slot_cued = true;
}
```

This leaves `TimelineSource` idle and imposes the wrong mental model on any
consumer building a tape-style or linear-looping sequencer. Pattern assignment,
slot cue modes, and performance state management are all irrelevant overhead
for a simple "play this region, loop it" use case.

**Proposed implementation:** When the transport position reaches `loop.end_tick`,
call `Engine::locate(loop.start_tick)`. `TimelineSource::on_locate()` and chase
already exist; the engine just needs to detect the boundary in `process()`.
`SetLoopCmd` should also set a loop-active flag so looping can be toggled
without clearing the loop region.

**Proposed C API addition:**

```c
OMEGA_API omega_status_t omega_loop_set(omega_engine_t* e,
                                        omega_tick_t start,
                                        omega_tick_t end);
OMEGA_API omega_status_t omega_loop_clear(omega_engine_t* e);
OMEGA_API omega_status_t omega_loop_enable(omega_engine_t* e, int enabled);
```

**GitHub issue title:** `feat: implement SetLoopCmd / timeline loop region`

---

### 2. Add `omega_tempo_set` and `omega_sink_id` to the C API

**Impact:** High

**Problem A — tempo:** The C API has `omega_timesig_set` but no equivalent for
tempo. Setting BPM requires dropping into C++:

```cpp
// examples/tapedeck/main.cpp L680
engine.tempo_map().insert(0, BPM_MILLI);
```

Any consumer using only the stable C ABI cannot set or query tempo. This
breaks the "stable ABI" promise for a fundamental session parameter.

**Problem B — sink ID:** The comment on `omega_sink_create_midi_out` says
"use `omega_sink_id(sink)` to route events to it" — but that function does
not exist in `omega.h`. The tapedeck calls the C++ method:

```cpp
// examples/tapedeck/main.cpp L678
const uint32_t sid = activity.sink_id();
```

A pure-C consumer has no way to retrieve a sink's ID.

**Proposed additions:**

```c
/* Set tempo at tick. bpm_milli = BPM x 1000 (e.g. 120 BPM = 120000). */
OMEGA_API omega_status_t omega_tempo_set(omega_engine_t* e,
                                         omega_tick_t tick,
                                         uint32_t bpm_milli);

/* Remove the tempo point nearest to tick. */
OMEGA_API omega_status_t omega_tempo_remove(omega_engine_t* e,
                                             omega_tick_t tick);

/* Query BPM at a given tick (returns milli-BPM). */
OMEGA_API omega_status_t omega_tempo_at(const omega_engine_t* e,
                                         omega_tick_t tick,
                                         uint32_t* bpm_milli_out);

/* Returns the unique sink_id for routing events to this sink. */
OMEGA_API uint32_t omega_sink_id(const omega_sink_t* sink);
```

**GitHub issue title:** `api-gap: add omega_tempo_set, omega_tempo_at, omega_sink_id to C API`

---

### 3. Add `omega_engine_position_tick()`

**Impact:** High

**Problem:** `omega_engine_position_ns()` returns the transport position in
nanoseconds. Most musical operations — snap, loop detection, bar/beat display —
work in ticks. The tapedeck converts in two different places using floating-point
BPM arithmetic:

```cpp
// examples/tapedeck/main.cpp L395-411  (display)
double pos_sec = static_cast<double>(pos_ns) / 1.0e9;
double beats = pos_sec * BPM / 60.0;

// examples/tapedeck/main.cpp L937-940  (loop counter)
double pos_beats = static_cast<double>(pos_ns) / 1.0e9 * BPM / 60.0;
auto idx = static_cast<uint64_t>(pos_beats) / (BARS * BEATS_PER_BAR);
```

This is error-prone (floating-point, assumes constant tempo, will drift with
tempo changes), and duplicates the tempo-map inversion that the engine already
performs every cycle. The existing `omega_tick_to_beat_pos()` function is
correct, but callers need a tick to use it.

**Proposed addition:**

```c
/* Current transport position in ticks. Safe to call from any thread. */
OMEGA_API omega_tick_t omega_engine_position_tick(const omega_engine_t* e);
```

**GitHub issue title:** `api-gap: add omega_engine_position_tick()`

---

### 4. Provide a thread-safe position snapshot

**Impact:** Medium-High

**Problem:** Even with `omega_engine_position_tick()`, callers must then call
`omega_tick_to_beat_pos()` while ensuring thread safety. Display threads
typically want bar, beat, subdivision, and loop wrap count in one atomic read.

The tapedeck tracks loop count itself via floating-point conversion (L934-945),
polling at 50 ms -- which can silently skip increments if the UI thread stalls.

**Proposed addition:**

```c
typedef struct {
    uint32_t bar;          /* 1-based */
    uint8_t  beat;         /* 1-based */
    uint32_t subdivision;  /* ticks past the beat boundary */
    uint64_t loop_count;   /* wraps of the active loop region */
    omega_tick_t tick;     /* raw tick for further computation */
} omega_position_t;

/* Atomic snapshot written at end of each process() cycle. Any-thread safe. */
OMEGA_API omega_status_t omega_engine_position(const omega_engine_t* e,
                                               omega_position_t* out);
```

The engine writes this struct atomically at the end of each `process()` call;
readers never block. `loop_count` is incremented by the engine each time the
loop wraps (see issue 1), eliminating the polling approximation entirely.

**GitHub issue title:** `feat: atomic position snapshot (bar/beat/loop_count)`

---

### 5. Add a pattern read API to the C surface

**Impact:** High

**Problem:** There is no C API for reading events out of a pattern. The tapedeck
accesses the C++ layer directly in multiple places:

```cpp
// examples/tapedeck/main.cpp L504
const Pattern* pat = engine.pattern_library().get(pat_id);

// L539-542  -- filter by channel
for (const auto& e : pat->events)
    if (e.channel == ch && e.payload_tag == OMEGA_NOTE_ON)
        ++total;

// L826-839  -- find the raw vector index of a selected event
for (int i = 0; i < (int)ppat->events.size(); ++i) { ... }
```

Any application that needs to display, export, or edit events is forced through
the C++ ABI, which undermines the "stable ABI" promise.

**Proposed additions:**

```c
/* Total event count in a pattern. */
OMEGA_API omega_status_t omega_pattern_event_count(const omega_engine_t* e,
                                                    omega_pattern_id_t pat,
                                                    uint32_t* count_out);

/* Read event at index idx (0-based). */
OMEGA_API omega_status_t omega_pattern_event_at(const omega_engine_t* e,
                                                 omega_pattern_id_t pat,
                                                 uint32_t idx,
                                                 omega_event_t* event_out);

/* Count events matching channel and payload_tag (0xFF = any). */
OMEGA_API omega_status_t omega_pattern_event_count_filtered(
    const omega_engine_t* e,
    omega_pattern_id_t pat,
    uint8_t channel,         /* 0xFF = all channels */
    uint8_t payload_tag,     /* 0xFF = all tags */
    uint32_t* count_out);

/* Query pattern length in ticks. */
OMEGA_API omega_status_t omega_pattern_length(const omega_engine_t* e,
                                              omega_pattern_id_t pat,
                                              omega_tick_t* length_out);
```

**GitHub issue title:** `api-gap: pattern read API (event_count, event_at, pattern_length)`

---

### 6. Add event field accessors

**Impact:** High

**Problem:** `omega_make_note_on()` constructs events, but there are no
symmetric accessors for reading or modifying individual fields. The tapedeck
must know that duration is stored at `e.data[2..5]`:

```cpp
// examples/tapedeck/main.cpp L553-554  (reading)
uint32_t dur = 0;
std::memcpy(&dur, e.data + 2, sizeof(dur));

// L775-778  (editing)
uint32_t dur = 0;
std::memcpy(&dur, edit_event.data + 2, sizeof(dur));
dur -= 120;
std::memcpy(edit_event.data + 2, &dur, sizeof(dur));
```

This byte-layout knowledge is not documented anywhere in the public headers.

**Proposed additions:**

```c
/* Accessors for OMEGA_NOTE_ON / OMEGA_NOTE_OFF events. */
OMEGA_API uint8_t  omega_event_note_pitch(const omega_event_t* e);
OMEGA_API uint8_t  omega_event_note_velocity(const omega_event_t* e);
OMEGA_API uint32_t omega_event_note_duration(const omega_event_t* e);

/* Mutators -- return OMEGA_ERR_INVALID_PARAM if e is wrong type. */
OMEGA_API omega_status_t omega_event_set_pitch(omega_event_t* e, uint8_t pitch);
OMEGA_API omega_status_t omega_event_set_velocity(omega_event_t* e, uint8_t vel);
OMEGA_API omega_status_t omega_event_set_duration(omega_event_t* e, uint32_t dur);

/* CC events. */
OMEGA_API uint8_t omega_event_cc_number(const omega_event_t* e);
OMEGA_API uint8_t omega_event_cc_value(const omega_event_t* e);
```

**GitHub issue title:** `api-gap: event field accessors (pitch, velocity, duration, cc)`

---

### 7. Add `ReplaceEventCmd` for live event editing

**Impact:** Medium

**Problem:** There is no command for replacing an event in a pattern. Editing an
event requires stopping playback so the timing thread is not reading the pattern,
then writing directly into the pattern's event vector:

```cpp
// examples/tapedeck/main.cpp L843-849
if (playing) {
    engine.enqueue(TransportCmd{TransportAction::STOP, 0});
    playing = false;
}
// then on save (L753-758):
ppat->events[edit_raw_idx] = edit_event;
```

This is a data race if the transport is running. The workaround (stop transport)
is safe but prevents live performance editing.

**Proposed addition:**

```cpp
// commands.h
struct ReplaceEventCmd {
    PatternId pattern_id;
    uint32_t  event_index;  /* 0-based index in the sorted event vector */
    Event     replacement;
};
```

**C API shim:**

```c
OMEGA_API omega_status_t omega_pattern_replace_event(omega_engine_t* e,
                                                      omega_pattern_id_t pat,
                                                      uint32_t event_index,
                                                      const omega_event_t* replacement);
```

**GitHub issue title:** `feat: ReplaceEventCmd -- live pattern event editing without stopping transport`

---

### 8. Build mute/solo into the library

**Impact:** Medium

**Problem:** Per-track mute and solo are universal DAW features. The library
provides no support. The tapedeck built an 89-line `ActivitySink` wrapper
(L142-230) with 16 channel atomics, a `any_soloed_` flag, and custom toggle
logic.

Beyond code volume, the workaround has a correctness bug: `ActivitySink::send()`
suppresses note-on events when a channel is muted, but note-off events pass
through unconditionally (they are not `OMEGA_NOTE_ON`). Notes that were playing
when the mute was toggled continue to sound until they naturally end. Correct
mute handling requires sending immediate note-off for all active notes -- logic
the engine already has for transport stop.

**Proposed additions:**

```c
OMEGA_API omega_status_t omega_sink_set_mute(omega_engine_t* e,
                                              uint32_t sink_id,
                                              uint8_t channel,   /* 0xFF = all */
                                              int muted);

OMEGA_API omega_status_t omega_sink_set_solo(omega_engine_t* e,
                                              uint32_t sink_id,
                                              uint8_t channel,
                                              int soloed);
```

When a channel is muted mid-note, the engine sends immediate note-off for all
active notes on that channel before suppressing further note-ons.

**GitHub issue title:** `feat: built-in mute/solo with correct note-off handling`

---

### 9. Track loop count inside the engine

**Impact:** Medium

**Problem:** Once `SetLoopCmd` is implemented (issue 1), the engine will know
every time the loop wraps. Currently the tapedeck estimates loop count by
polling at 50 ms (L934-945), which can skip increments if the UI stalls.

**Resolution:** Include `loop_count` in the `omega_position_t` snapshot
proposed in issue 4. The engine increments an internal atomic counter each
time the loop wraps; `omega_engine_position()` copies it into the snapshot.

**GitHub issue title:** *(child of issue 4 -- covered by the position snapshot)*

---

### 10. Document (or eliminate) sink-before-pattern creation ordering

**Impact:** Low

**Problem:** The `sink_id` embedded in pattern events is assigned at
`OutputSink` construction time. The consumer must create the sink *before*
building the pattern. This ordering constraint is invisible in the API -- nothing
enforces or documents it. Wrong order results in silent wrong-sink routing.

```cpp
// examples/tapedeck/main.cpp L675-684
ActivitySink activity(midi_sink);        // sink created first
const uint32_t sid = activity.sink_id(); // get ID
PatternId pat = engine.create_pattern("8-bar loop", PATTERN_LEN);
populate_pattern(engine, pat, sid);      // reference sid in events
```

**Options:**
- Document the required order in `omega_sink_create_midi_out` and
  `omega_pattern_add_event`.
- Allow a sentinel `sink_id` of `0` meaning "default sink" (the first
  registered sink), resolved at dispatch time.

**GitHub issue title:** `dx: document or eliminate sink-before-pattern creation ordering`

---

### 11. Add MIDI utility functions to the C API

**Impact:** Low

**Problem:** The tapedeck implements two generic utilities that every MIDI
application needs:

```cpp
// examples/tapedeck/main.cpp L349-371
static void format_note(uint8_t midi_note, char* out, size_t out_size) { ... }
static void format_pos(uint64_t tick, char* out, size_t out_size) { ... }
```

`format_pos` duplicates logic from `MeterCursor` and hardcodes `BEATS_PER_BAR`.

**Proposed additions:**

```c
/* "C4", "F#3", etc. Writes at most out_size bytes including NUL. */
OMEGA_API void omega_midi_note_name(uint8_t pitch, char* out, size_t out_size);

/* "3:2.120" (bar:beat.subdivision) -- uses engine's time-signature map. */
OMEGA_API omega_status_t omega_format_position(const omega_engine_t* e,
                                                omega_tick_t tick,
                                                char* out,
                                                size_t out_size);
```

**GitHub issue title:** `dx: add omega_midi_note_name and omega_format_position helpers`

---

## Big-Picture Findings

### A. The C API is write-only

The public C API (`omega.h`) has approximately 70 mutation/creation functions
and very few query functions. Applications that need to observe state -- to
display it, validate it, serialize it, or react to it -- must bypass the C ABI
and use the C++ layer directly. This undermines the "stable ABI" promise. The
most impactful queries to add are covered in issues 2, 3, 4, and 5.

### B. Loop playback requires the wrong source

`TimelineSource` is the natural primitive for tape-style sequencing: events on
a timeline, played in order, looped over a region. `PerformanceSource` is a
clip launcher: patterns assigned to slots, cued with phase modes. The tapedeck
needed the former but was forced to use the latter because `SetLoopCmd` is
unimplemented. Fixing issue 1 restores the correct mental model.

### C. Mute/solo correctness is harder than it looks

The naive workaround (suppress note-on in the sink's `send()`) leaves notes
sustained when muting mid-phrase. Correct mute handling requires the engine to
flush active notes on mute -- the same mechanism it already uses on transport
stop. This is an argument for building mute/solo into the engine rather than
delegating it to consumer-side sink wrappers. See issue 8.

### D. Event payload encoding leaks internal layout

`omega_make_note_on()` is the only public constructor for note events. There
are no accessors. Any code that reads or modifies an event must know the
private `data[]` byte layout -- a hidden ABI contract. Issue 6 closes this gap.

### E. Transport position lives in two coordinate systems

The engine reasons in ticks. The only public position query returns nanoseconds.
Any application that displays bar/beat, computes loop boundaries, or snaps to
grid must perform the tick-to-ns conversion itself -- in floating point, with
hardcoded BPM assumptions that break under tempo changes. Issues 3 and 4
close this gap.
