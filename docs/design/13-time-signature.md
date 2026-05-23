# Design: Time Signature Support

## Status

**Proposed.** Implementation target: after M4 (Orchestration Layer), before M5 (Platform
Integrations). Tagged `v0.3.5-alpha`. See the [Milestone Placement](#milestone-placement-m45)
section for sprint breakdown.

---

## Decision Summary

| Question | Decision | Rationale |
|---|---|---|
| Where does meter live? | `TimeSignatureMap` on `Session`, parallel to `TempoMap` | Same access pattern; same mutation model |
| Does meter affect tick math? | Never | Engine stays purely tick-based; meter is a display/snap/cue concern |
| How is freeform expressed? | Empty `TimeSignatureMap` (no entry at tick 0) | No special flag; absence is the contract |
| What does "no meter" mean to callers? | Helpers return `OMEGA_ERR_NO_METER` | Explicit failure rather than a silent 4/4 fallback |
| Where do navigation helpers live? | `MeterCursor` in the C++ layer; C API wrappers | Not called from the timing thread |
| Polymetric patterns? | Already supported via `length_ticks`; no changes needed | Patterns are free-length; session meter is independent |

---

## The Problem

Time signature has three distinct roles that the current single-field `TimeSignature` cannot serve:

1. **Grid and display** — where do bar lines fall? what are the beat subdivisions? A DAW piano-roll
   or a "snap to bar" feature cannot answer these questions without a map of meter changes.

2. **Cue quantization** — `PerformanceSource` cues patterns at loop boundaries or beat boundaries.
   Cuing at the *next bar* requires knowing where bars fall, which changes when the meter does.

3. **SMF fidelity** — Standard MIDI Files carry time signature meta events (0x58). Round-tripping
   a file through Omega must not silently discard them.

The current `TimeSignature { numerator, denominator }` singleton covers none of these: it cannot
change mid-session, and there is no API to query bar or beat positions from it.

Freeform mode — a session with no meter at all — must be a first-class state, not an error papered
over with a default 4/4. Valid freeform use cases include: real-time recorded rubato performances,
generative sources driven by `EventInput`, and tick-tape compositions where the author thinks in
absolute ticks with no reference to bars or beats.

---

## `TimeSignatureMap`

A sorted list of time signature change points, parallel in structure to `TempoMap`.

```cpp
struct TimeSigPoint {
    uint64_t tick;        // tick at which this meter takes effect
    uint8_t  numerator;   // beats per bar (1–99)
    uint8_t  denominator; // beat unit: 1, 2, 4, 8, 16, or 32 (literal, not exponent)
};

class TimeSignatureMap {
public:
    // Insert or replace the entry at tick.
    // Returns OMEGA_ERR_INVALID if denominator is not a power of 2 in [1, 32].
    omega_status_t insert(uint64_t tick, uint8_t numerator, uint8_t denominator);

    omega_status_t remove(uint64_t tick);  // OMEGA_ERR_NOT_FOUND if absent
    void           clear();                // enters freeform mode

    // Returns the active TimeSigPoint at tick (the last point at or before it).
    // Returns nullptr if the map is empty or tick precedes the first entry.
    const TimeSigPoint* at(uint64_t tick) const;

    bool   is_freeform() const { return points_.empty(); }
    size_t size() const;

private:
    std::vector<TimeSigPoint> points_;  // maintained in tick order
};
```

**Denominator convention**: the stored value is the literal note-value denominator (4 = quarter
note, 8 = eighth note), not a power-of-2 exponent. SMF encodes it as an exponent (`2` means
2^2 = 4); the import layer decodes it. Validation rejects non-power-of-2 values.

**Ticks per beat and bar** at a given `TimeSigPoint`:

```cpp
// PPQN is quarter-note ticks. Scale by note-value ratio.
constexpr uint64_t ticks_per_beat(const TimeSigPoint& p) {
    return (static_cast<uint64_t>(omega::PPQN) * 4) / p.denominator;
}

constexpr uint64_t ticks_per_bar(const TimeSigPoint& p) {
    return ticks_per_beat(p) * p.numerator;
}
```

| Meter | `ticks_per_beat` | `ticks_per_bar` |
|---|---|---|
| 4/4 | 480 | 1920 |
| 3/4 | 480 | 1440 |
| 6/8 | 240 | 1440 |
| 7/8 | 240 | 1680 |
| 5/4 | 480 | 2400 |
| 12/8 | 240 | 2880 |

---

## Freeform Mode

When `TimeSignatureMap::is_freeform()` is true, the session has no meter. This is entered by
calling `omega_timesig_clear()` or by never calling `omega_timesig_set()` at all — the map starts
empty on every new session.

**What changes in freeform mode:**
- `omega_tick_to_position()` returns `OMEGA_ERR_NO_METER`
- `omega_position_to_tick()` returns `OMEGA_ERR_NO_METER`
- All `MeterCursor` navigation helpers return `OMEGA_ERR_NO_METER`
- `OMEGA_CUE_BAR` degrades to `OMEGA_CUE_AT_BOUNDARY` (see below)
- SMF export emits no time-signature meta events

**What does not change:**
- The engine runs and dispatches events exactly as before
- All three built-in sources work without modification
- Recording, looping, and transport all continue to operate on ticks
- `TempoMap` is completely independent; freeform sessions can still have tempo changes

There is no "enter freeform" call. The absence of entries in `TimeSignatureMap` is the condition.
A session that previously had meter becomes freeform by calling `omega_timesig_clear()`.

---

## `MeterCursor` — Bar/Beat Navigation

`MeterCursor` is a non-realtime helper. It **must not be called from the timing thread**.
Construct it from a const snapshot of `TimeSignatureMap` obtained via the C++ layer or the query
API — never from a pointer held across a mutation.

```cpp
struct BeatPosition {
    uint32_t bar;         // 1-based
    uint8_t  beat;        // 1-based, 1..numerator
    uint32_t subdivision; // ticks past the beat boundary (0..ticks_per_beat - 1)
};

class MeterCursor {
public:
    explicit MeterCursor(const TimeSignatureMap& map);

    // Convert tick → {bar, beat, subdivision}.
    // Returns OMEGA_ERR_NO_METER if freeform.
    omega_status_t tick_to_position(uint64_t tick, BeatPosition& out) const;

    // Convert {bar, beat, subdivision} → tick.
    // Returns OMEGA_ERR_NO_METER if freeform.
    // Returns OMEGA_ERR_INVALID if beat > numerator at that bar's active meter.
    omega_status_t position_to_tick(const BeatPosition& pos, uint64_t& out) const;

    // Tick of the next bar boundary at or after from_tick.
    omega_status_t next_bar_tick(uint64_t from_tick, uint64_t& out) const;

    // Tick of the next beat boundary at or after from_tick.
    omega_status_t next_beat_tick(uint64_t from_tick, uint64_t& out) const;

    // Quantize tick to the nearest beat (round-half-up).
    omega_status_t quantize_to_beat(uint64_t tick, uint64_t& out) const;

    // Quantize tick to the nearest subdivision of subdiv_ticks length.
    omega_status_t quantize_to_subdivision(uint64_t tick, uint64_t subdiv_ticks,
                                           uint64_t& out) const;
};
```

**`tick_to_position` implementation sketch:**
```
1. If map is freeform → OMEGA_ERR_NO_METER
2. Walk adjacent TimeSigPoint pairs, accumulating bar count:
   For each segment [current_point, next_point):
     span_ticks = next_point.tick - current_point.tick
     bars_in_span = span_ticks / ticks_per_bar(current_point)
     remainder_ticks = span_ticks % ticks_per_bar(current_point)
     If target_tick falls within this segment:
       offset = target_tick - current_point.tick
       bar offset = offset / ticks_per_bar(current_point)
       beat offset = (offset % ticks_per_bar) / ticks_per_beat
       subdivision = offset % ticks_per_beat
       Return {accumulated_bars + bar_offset + 1, beat_offset + 1, subdivision}
     Else:
       accumulated_bars += bars_in_span
       (partial bars at end of segment belong to the next meter)
3. Handle the final open-ended segment (no next point) the same way.
```

This is O(n) in the number of time signature changes — acceptable since it is never called from
the timing thread.

---

## Session Ownership Update

`TimeSignatureMap` replaces the current `TimeSignature` singleton in `Session`:

```
Session
├── TempoMap                  (unchanged)
├── TimeSignatureMap          (replaces single TimeSignature; empty = freeform)
├── PatternLibrary            (unchanged)
│   ...
```

**Serialization**: the key changes from `"time_signature"` to `"time_signature_map"`:

```json
{
  "time_signature_map": [
    { "tick": 0,    "numerator": 4, "denominator": 4 },
    { "tick": 7680, "numerator": 7, "denominator": 8 }
  ]
}
```

A freeform session serializes as `"time_signature_map": []`.

**Migration**: on load, if the old `"time_signature"` key is present and `"time_signature_map"` is
absent, synthesize a single-entry map at tick 0. This is the only backward-compatibility shim.

---

## Command Variants

`TimeSignatureMap` is read by `PerformanceSource::advance()` on the timing thread (for
`OMEGA_CUE_BAR` boundary calculation). All mutations therefore go through the SPSC command queue,
exactly as `TempoMap` mutations do.

Two new command variants:

```cpp
struct SetTimeSigCmd {
    uint64_t tick;
    uint8_t  numerator;
    uint8_t  denominator;
};

struct ClearTimeSigCmd {};
```

`remove` at a specific tick is covered by a `RemoveTimeSigCmd { uint64_t tick }` variant.
The timing thread applies these at the start of the next `process()` cycle.

---

## C API Additions

```c
/* ── Time Signature ──────────────────────────────────────────────────── */

typedef struct {
    omega_tick_t tick;
    uint8_t      numerator;
    uint8_t      denominator;
} omega_time_sig_point_t;

/* Insert or replace the time signature at tick. tick=0 sets the opening meter.
   Enqueues via the command queue.
   Returns OMEGA_ERR_INVALID if denominator is not a power of 2 in [1, 32]. */
omega_status_t omega_timesig_set(omega_engine_t engine, omega_tick_t tick,
                                  uint8_t numerator, uint8_t denominator);

/* Remove the time signature point at exactly tick.
   Returns OMEGA_ERR_NOT_FOUND if no point exists there. */
omega_status_t omega_timesig_remove(omega_engine_t engine, omega_tick_t tick);

/* Remove all time signature points, entering freeform mode. */
omega_status_t omega_timesig_clear(omega_engine_t engine);

/* Query the active time signature at tick (the last point at or before it).
   Returns OMEGA_ERR_NO_METER if the session is freeform.
   Safe to call from any thread. */
omega_status_t omega_timesig_at(omega_engine_t engine, omega_tick_t tick,
                                 omega_time_sig_point_t* out);

/* Returns 1 if the session is freeform (no time signature entries), 0 otherwise. */
int omega_is_freeform(omega_engine_t engine);

/* ── Bar/Beat Conversion ─────────────────────────────────────────────── */
/* Do not call from the timing thread.                                    */

typedef struct {
    uint32_t bar;         /* 1-based */
    uint8_t  beat;        /* 1-based */
    uint32_t subdivision; /* ticks past the beat boundary */
} omega_beat_pos_t;

/* Convert absolute tick → {bar, beat, subdivision}.
   Returns OMEGA_ERR_NO_METER if freeform. */
omega_status_t omega_tick_to_position(omega_engine_t engine, omega_tick_t tick,
                                       omega_beat_pos_t* out);

/* Convert {bar, beat, subdivision} → absolute tick.
   Returns OMEGA_ERR_NO_METER if freeform.
   Returns OMEGA_ERR_INVALID if beat > numerator at that bar's active meter. */
omega_status_t omega_position_to_tick(omega_engine_t engine,
                                       const omega_beat_pos_t* pos, omega_tick_t* out);

/* Tick of the next bar boundary at or after from_tick.
   Returns OMEGA_ERR_NO_METER if freeform. */
omega_status_t omega_next_bar_tick(omega_engine_t engine, omega_tick_t from_tick,
                                    omega_tick_t* out);

/* Quantize tick to the nearest beat.
   Returns OMEGA_ERR_NO_METER if freeform. */
omega_status_t omega_quantize_to_beat(omega_engine_t engine, omega_tick_t tick,
                                       omega_tick_t* out);
```

**New status code** (add to `omega_status_t` enum):
```c
OMEGA_ERR_NO_METER    = -8,   /* operation requires a time signature; session is freeform */
```

---

## `OMEGA_CUE_BAR` — Meter-Aware Cue Mode

A new cue mode added to `PerformanceSource`:

```c
typedef enum {
    OMEGA_CUE_AT_BOUNDARY  = 0,  /* next loop-length boundary (unchanged) */
    OMEGA_CUE_IMMEDIATE    = 1,  /* fire immediately (unchanged) */
    OMEGA_CUE_QUANTIZED    = 2,  /* next beat boundary (unchanged) */
    OMEGA_CUE_BAR          = 3,  /* next bar boundary per TimeSignatureMap */
} omega_cue_mode_t;
```

When `OMEGA_CUE_BAR` is requested and the session is freeform, `PerformanceSource` degrades
silently to `OMEGA_CUE_AT_BOUNDARY`. A live performance must not stall waiting for a bar that
never arrives. If an error callback is registered, the degradation is reported through it.

`OMEGA_CUE_BAR` boundary calculation inside `PerformanceSource::advance()` reads `TimeSignatureMap`
directly (no `MeterCursor` — the cursor's bar-walk is too allocation-heavy for the timing thread).
The timing thread uses a simple inline calculation: find the active `TimeSigPoint` at the current
tick, compute `ticks_per_bar`, find the next multiple.

---

## SMF Integration (M5 interaction)

Time signature changes are encoded in SMF as meta event 0x58. The M5 sprints for SMF
import/export must be extended as follows:

**Import (Sprint 5.2 addition):**
- On encountering meta event 0x58: decode the SMF denominator byte as an exponent
  (`denominator = 1 << smf_byte[1]`), then call `TimeSignatureMap::insert()`.
- If no 0x58 event appears in the file, leave `TimeSignatureMap` empty (freeform).
- The SMF "clocks per metronome click" and "32nd notes per MIDI quarter" bytes are stored as
  session metadata but do not affect the engine.

**Export (Sprint 5.3 addition):**
- For each `TimeSigPoint`, emit a meta 0x58 at the correct delta-tick position.
- Encode: `smf_byte[1] = log2(denominator)` (valid since denominator is always a power of 2).
- Use standard defaults for metronome click (24) and 32nd-note fields (8).
- If freeform, emit no 0x58 events.

---

## Testing Strategy

New file: `tests/unit/test_time_signature_map.cpp`

**`TimeSignatureMap` unit tests:**
- Insert and query: single point, multiple points, query before first entry, query between entries
- `remove()`: nominal and `OMEGA_ERR_NOT_FOUND`
- `clear()`: map becomes freeform; subsequent `at()` returns nullptr

**`MeterCursor` unit tests:**
- `ticks_per_bar` correctness for 4/4, 3/4, 7/8, 5/4, 6/8, 12/8
- `tick_to_position` / `position_to_tick` round-trip identity within a single meter
- Round-trip across a meter change boundary (e.g., 4/4 → 7/8 at tick 7680)
- `next_bar_tick` at tick 0, mid-bar, and exactly on a bar boundary
- `quantize_to_beat` rounds correctly at midpoint and edges
- All helpers return `OMEGA_ERR_NO_METER` when map is empty

**Integration tests:**
- `OMEGA_CUE_BAR`: pattern queued in 4/4 waits for the next bar boundary; same pattern in a
  freeform session falls back to `OMEGA_CUE_AT_BOUNDARY` without error
- SMF round-trip: a file with two time signature changes imports to the correct `TimeSignatureMap`
  and exports to identical meta events; delta-tick positions match to the tick
- Migration: a session file carrying old `"time_signature": {"numerator":3,"denominator":4}`
  loads as a single-entry map at tick 0

---

## Milestone Placement: M4.5

This work slots between M4 (Orchestration Layer) and M5 (Platform Integrations) because:
- It depends on M4 for the full command queue and session infrastructure
- M5 Sprint 5.2/5.3 must be extended to handle `TimeSignatureMap`; those sprints should read this
  document before implementation

Proposed tag: `v0.3.5-alpha`

### Sprint 4.5.1 — `TimeSignatureMap` + `MeterCursor`

**Deliverables:**
- `include/omega/time_signature_map.h` — `TimeSigPoint`, `TimeSignatureMap`, `MeterCursor`,
  `BeatPosition`
- `src/time_signature_map.cpp`
- `SetTimeSigCmd`, `RemoveTimeSigCmd`, `ClearTimeSigCmd` added to command variant list in
  `src/engine.cpp`
- `Session`: `TimeSignatureMap` replaces the `TimeSignature` field; serialization updated;
  old-key migration path

**Definition of done:**
- All unit tests pass under ASan + UBSan
- `tick_to_position → position_to_tick` round-trip is identity for 4/4, 7/8, and 5/4, including
  across a meter change boundary

### Sprint 4.5.2 — C API + `OMEGA_CUE_BAR`

**Deliverables:**
- All `omega_timesig_*`, `omega_tick_to_position`, `omega_position_to_tick`,
  `omega_next_bar_tick`, `omega_quantize_to_beat` functions in `src/omega_c.cpp`
- `OMEGA_ERR_NO_METER` status code in `include/omega/omega.h`
- `OMEGA_CUE_BAR` cue mode with freeform degradation in `PerformanceSource`

**Definition of done:**
- C API compiles and passes tests from a pure C99 file (no C++ headers included)
- `OMEGA_CUE_BAR` test: pattern in 4/4 waits for bar; same pattern in freeform falls back without
  hang or assert
- TSan clean under OmegaTimer + mutation thread calling `omega_timesig_set()` concurrently

---

## Open Issues

- **Compound meter display**: 6/8 and 12/8 are felt in 2 and 4 respectively (dotted quarter
  beats). `MeterCursor` treats every denominator unit as a beat. A `compound_beat_grouping` hint
  on `TimeSigPoint` could expose this in v2.
- **Per-pattern time signature**: patterns have only `length_ticks`. A pattern-local `TimeSigPoint`
  would let `MeterCursor` show bar/beat positions within a pattern independently of session meter.
  Deferred to v2.
- **Tempo ramps and meter ramps together**: when both a tempo change and a meter change land on the
  same tick, order of application is undefined. Both are applied at the top of the next
  `process()` cycle; in practice the order within one cycle does not matter since `MeterCursor`
  reads only the final `TimeSignatureMap` state.
- **Irrational meters** (e.g., 4/6 as used in some contemporary scores): not representable with
  power-of-2 denominators. Out of scope for v1.
- **MIDI clock and bar sync**: `MidiClockSource` could emit a bar-pulse event when
  `OMEGA_CUE_BAR` is active, allowing external sync to bar boundaries. Deferred to a later M5
  sprint.
