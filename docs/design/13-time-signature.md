# Design: Time Signature Support

## Status

**Accepted.** Implementation target: after M4 (Orchestration Layer), before M5 (Platform
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
| Common interface for all coordinate systems? | `PositionConverter` base class | Snap-to-grid tools work without caring about the format |
| Where does SMPTE config live? | `SmpteConfig` (optional) on `Session`, independent of `TimeSignatureMap` | SMPTE is a query-layer concern; sessions without video don't need it |

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
- `omega_tick_to_beat_pos()` returns `OMEGA_ERR_NO_METER`
- `omega_beat_pos_to_tick()` returns `OMEGA_ERR_NO_METER`
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

class MeterCursor : public PositionConverter {
public:
    explicit MeterCursor(const TimeSignatureMap& map);

    // Convert tick → {bar, beat, subdivision}.
    // Returns OMEGA_ERR_NO_METER if freeform.
    omega_status_t tick_to_beat_pos(uint64_t tick, BeatPosition& out) const;

    // Convert {bar, beat, subdivision} → tick.
    // Returns OMEGA_ERR_NO_METER if freeform.
    // Returns OMEGA_ERR_INVALID if beat > numerator at that bar's active meter.
    omega_status_t beat_pos_to_tick(const BeatPosition& pos, uint64_t& out) const;

    // Tick of the next bar boundary at or after from_tick.
    omega_status_t next_bar_tick(uint64_t from_tick, uint64_t& out) const;

    // Tick of the next beat boundary at or after from_tick.
    omega_status_t next_beat_tick(uint64_t from_tick, uint64_t& out) const;

    // Quantize tick to the nearest beat (round-half-up).
    omega_status_t quantize_to_beat(uint64_t tick, uint64_t& out) const;

    // Quantize tick to the nearest subdivision of subdiv_ticks length.
    omega_status_t quantize_to_subdivision(uint64_t tick, uint64_t subdiv_ticks,
                                           uint64_t& out) const;

    // PositionConverter overrides: quantize maps to nearest beat; next_boundary maps to next bar.
    omega_status_t quantize(uint64_t tick, uint64_t& out) const override;
    omega_status_t next_boundary(uint64_t from_tick, uint64_t& out) const override;
};
```

**`tick_to_beat_pos` implementation sketch:**
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

## Coordinate Systems — Design Principle

**The timing thread knows only ticks and nanoseconds.** All coordinate systems — bar/beat,
SMPTE timecode, sample count, feet+frames — are a non-realtime query layer. They are never
consulted inside `process()`.

This means:
- `MeterCursor`, `SmpteConverter`, and any future converter are constructed on the mutation thread
  or the UI thread, not held live on the timing thread.
- Converters read session state (TempoMap, TimeSignatureMap, SmpteConfig) at construction time or
  via const snapshot; they never block on a lock.
- The engine's hot path stays pure integers and no coordinate translation.

### Industry Format Table

| Format | Typical use case | v1 support |
|---|---|---|
| Bar.Beat.Subdivision | Musical editing, pattern cuing | `MeterCursor` |
| HH:MM:SS:FF (SMPTE) | Film/TV post, hardware MTC sync | `SmpteConverter` |
| Samples | Audio plugin host positions | future |
| Feet+Frames | Film editorial (North America) | future |
| OSC/NTP timestamp | Network sync, OSC time tags | future |
| Link beat time | Ableton Link peer sync | future |
| Wall clock | Human-readable display | trivial via `ns_to_wall()` |

---

## `PositionConverter` — Base Interface

`PositionConverter` is the common base class for all coordinate-system helpers. A generic
snap-to-grid or quantize function can accept `PositionConverter&` without caring which format
is active.

```cpp
class PositionConverter {
public:
    virtual ~PositionConverter() = default;

    // Quantize tick to the nearest grid point in this coordinate system.
    // Returns OMEGA_ERR_NO_METER (or OMEGA_ERR_NO_SMPTE_CONFIG) if the required
    // session config is absent.
    virtual omega_status_t quantize(uint64_t tick, uint64_t& out) const = 0;

    // Tick of the next grid boundary at or after from_tick.
    virtual omega_status_t next_boundary(uint64_t from_tick, uint64_t& out) const = 0;
};
```

`MeterCursor` implements `PositionConverter` with bar-aligned grid semantics.
`SmpteConverter` implements it with frame-aligned grid semantics.
Both are constructed on the non-realtime thread and are not shared with the timing thread.

---

## `SmpteConfig`

`SmpteConfig` is an optional struct on `Session`. A session without video does not need a
`SmpteConfig`; SMPTE helpers return `OMEGA_ERR_NO_SMPTE_CONFIG` when it is absent. It is
independent of `TimeSignatureMap` — a session can have meter without SMPTE, SMPTE without
meter, both, or neither.

```cpp
struct SmpteConfig {
    uint8_t fps;         // nominal frame rate: 24, 25, or 30
    bool    drop_frame;  // true = 29.97 drop-frame (only valid when fps == 30)
    bool    is_2997;     // true = 29.97 (1000/1001 actual rate); false = exactly fps
};
```

**What 29.97 drop-frame actually means**: SMPTE 29.97 drop-frame does not drop any video
frames. It drops *frame number labels* — specifically, frames 0 and 1 of the first second of
every minute, except every tenth minute — so that the running frame counter stays synchronized
with real elapsed time despite 29.97 ≠ 30. Without drop-frame, a counter running at 30 fps
labels gains ~2 frames per minute relative to wall clock, accumulating to ~3.6 s per hour. Drop-
frame corrects this by skipping those two label values at the right moments, keeping
HH:MM:SS:FF continuously synchronized to real time. The video content is unaffected.

**Serialization**: stored as `"smpte_config"` in the `.omega` file:
```json
{ "smpte_config": { "fps": 30, "drop_frame": true, "is_2997": true } }
```
Absent key = no SMPTE config (session is not video-locked).

**Mutation**: goes through the command queue via `SetSmpteConfigCmd` / `ClearSmpteConfigCmd`.
Since SMPTE config is session-wide and rarely changed mid-session, this is a simple
value-replace command with no undo complexity.

---

## `SmpteConverter`

`SmpteConverter` implements `PositionConverter`. It converts between ticks and SMPTE
HH:MM:SS:FF timecode.

**Conversion path**: `tick → nanoseconds (via TempoMap) → elapsed frame count → HH:MM:SS:FF`

This is the **query side**, not the MTC clock side. `SmpteConverter` does not generate or
receive MTC quarter-frame messages — that is an M5 platform integration concern. Its job is
solely to translate tick positions into human-readable (or machine-readable) SMPTE addresses.

```cpp
struct SmpteTime {
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
    uint8_t frames;
};

class SmpteConverter : public PositionConverter {
public:
    // Requires TempoMap for the tick → ns conversion and SmpteConfig for frame rate.
    SmpteConverter(const TempoMap& tempo_map, const SmpteConfig& config);

    // Convert absolute tick → SMPTE HH:MM:SS:FF.
    omega_status_t tick_to_smpte(uint64_t tick, SmpteTime& out) const;

    // Convert SMPTE HH:MM:SS:FF → absolute tick.
    // Returns OMEGA_ERR_INVALID for impossible drop-frame addresses.
    omega_status_t smpte_to_tick(const SmpteTime& smpte, uint64_t& out) const;

    // PositionConverter overrides: grid unit = one frame.
    omega_status_t quantize(uint64_t tick, uint64_t& out) const override;
    omega_status_t next_boundary(uint64_t from_tick, uint64_t& out) const override;
};
```

**Drop-frame address arithmetic**: for 29.97 DF, frame addresses 0 and 1 of each minute
(except minutes 0, 10, 20, 30, 40, 50) are skipped. `smpte_to_tick` rejects those addresses
with `OMEGA_ERR_INVALID`. `tick_to_smpte` never produces them.

---

## Session Ownership Update

`TimeSignatureMap` replaces the current `TimeSignature` singleton in `Session`:

```
Session
├── TempoMap                  (unchanged)
├── TimeSignatureMap          (replaces single TimeSignature; empty = freeform)
├── SmpteConfig               (optional; absent = no SMPTE config)
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
omega_status_t omega_tick_to_beat_pos(omega_engine_t engine, omega_tick_t tick,
                                       omega_beat_pos_t* out);

/* Convert {bar, beat, subdivision} → absolute tick.
   Returns OMEGA_ERR_NO_METER if freeform.
   Returns OMEGA_ERR_INVALID if beat > numerator at that bar's active meter. */
omega_status_t omega_beat_pos_to_tick(omega_engine_t engine,
                                       const omega_beat_pos_t* pos, omega_tick_t* out);

/* Tick of the next bar boundary at or after from_tick.
   Returns OMEGA_ERR_NO_METER if freeform. */
omega_status_t omega_next_bar_tick(omega_engine_t engine, omega_tick_t from_tick,
                                    omega_tick_t* out);

/* Quantize tick to the nearest beat.
   Returns OMEGA_ERR_NO_METER if freeform. */
omega_status_t omega_quantize_to_beat(omega_engine_t engine, omega_tick_t tick,
                                       omega_tick_t* out);

/* ── SMPTE Config ────────────────────────────────────────────────────── */

typedef struct {
    uint8_t fps;          /* nominal frame rate: 24, 25, or 30 */
    int     drop_frame;   /* 1 = 29.97 drop-frame (only valid when fps == 30); 0 otherwise */
    int     is_2997;      /* 1 = 29.97 actual rate; 0 = exactly fps */
} omega_smpte_config_t;

/* Set the session SMPTE config. Enqueues via the command queue. */
omega_status_t omega_smpte_config_set(omega_engine_t engine,
                                       const omega_smpte_config_t* config);

/* Query the current SMPTE config. Returns OMEGA_ERR_NO_SMPTE_CONFIG if not set. */
omega_status_t omega_smpte_config_get(omega_engine_t engine, omega_smpte_config_t* out);

/* Clear the SMPTE config, removing video lock from the session. */
omega_status_t omega_smpte_config_clear(omega_engine_t engine);

/* ── SMPTE Conversion ────────────────────────────────────────────────── */
/* Do not call from the timing thread.                                    */

typedef struct {
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
    uint8_t frames;
} omega_smpte_time_t;

/* Convert absolute tick → SMPTE HH:MM:SS:FF.
   Returns OMEGA_ERR_NO_SMPTE_CONFIG if smpte_config has not been set. */
omega_status_t omega_tick_to_smpte(omega_engine_t engine, omega_tick_t tick,
                                    omega_smpte_time_t* out);

/* Convert SMPTE HH:MM:SS:FF → absolute tick.
   Returns OMEGA_ERR_NO_SMPTE_CONFIG if not set.
   Returns OMEGA_ERR_INVALID for impossible drop-frame addresses (e.g., frame 0 of minute 1). */
omega_status_t omega_smpte_to_tick(omega_engine_t engine,
                                    const omega_smpte_time_t* smpte, omega_tick_t* out);
```

**New status codes** (add to `omega_status_t` enum):
```c
OMEGA_ERR_NO_METER        = -8,   /* operation requires a time signature; session is freeform */
OMEGA_ERR_NO_SMPTE_CONFIG = -9,   /* operation requires SmpteConfig; none has been set */
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
- `include/omega/time_signature_map.h` — `TimeSigPoint`, `TimeSignatureMap`, `PositionConverter`,
  `MeterCursor`, `BeatPosition`
- `src/time_signature_map.cpp`
- `SetTimeSigCmd`, `RemoveTimeSigCmd`, `ClearTimeSigCmd` added to command variant list in
  `src/engine.cpp`
- `Session`: `TimeSignatureMap` replaces the `TimeSignature` field; serialization updated;
  old-key migration path

**Definition of done:**
- All unit tests pass under ASan + UBSan
- `tick_to_beat_pos → beat_pos_to_tick` round-trip is identity for 4/4, 7/8, and 5/4, including
  across a meter change boundary

### Sprint 4.5.2 — `SmpteConverter`

**Deliverables:**
- `include/omega/smpte_converter.h` — `SmpteConfig`, `SmpteTime`, `SmpteConverter`
- `src/smpte_converter.cpp`
- `SetSmpteConfigCmd`, `ClearSmpteConfigCmd` added to command variant list in `src/engine.cpp`
- `Session`: optional `SmpteConfig` field; serialization as `"smpte_config"` key (absent = not set)

**Definition of done:**
- All unit tests pass under ASan + UBSan
- `tick_to_smpte → smpte_to_tick` round-trip is identity for 24fps, 25fps, 30fps NDF, and 29.97 DF
- 29.97 DF impossible addresses (e.g., frame 0 of minute 1) are rejected with `OMEGA_ERR_INVALID`
- Accumulated error over a 1-hour session at 29.97 DF does not exceed 1 frame

### Sprint 4.5.3 — C API + `OMEGA_CUE_BAR`

**Deliverables:**
- All `omega_timesig_*`, `omega_tick_to_beat_pos`, `omega_beat_pos_to_tick`,
  `omega_next_bar_tick`, `omega_quantize_to_beat` functions in `src/omega_c.cpp`
- All `omega_smpte_config_*`, `omega_tick_to_smpte`, `omega_smpte_to_tick` functions in
  `src/omega_c.cpp`
- `OMEGA_ERR_NO_METER`, `OMEGA_ERR_NO_SMPTE_CONFIG` status codes in `include/omega/omega.h`
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
- **Feet+frames**: the North American film editorial format (16 or 35mm, 16 frames per foot at
  24fps). Implementable as a future `PositionConverter` subclass once `SmpteConverter` is
  validated; shares the same tick → ns → frame-count path.
- **Sample-based position**: DAW plugin hosts often express positions as sample offsets at a fixed
  sample rate. A `SampleConverter(sample_rate, tempo_map)` would implement `PositionConverter`
  the same way `SmpteConverter` does. Depends on the host providing sample rate at construction
  time; no session state needed.
- **SMPTE-division SMF**: SMF headers can encode time code division (`fps` + `ticks per frame`)
  instead of PPQN. Omega currently assumes PPQN headers. An SMF with SMPTE division header
  should map its frame-based timing into the Omega tick space at import time. Interaction with
  `SmpteConfig` needs design; deferred to M5.
- **OSC/NTP timestamp**: OSC bundles carry NTP timestamps (seconds + fraction since 1900). A
  future `NtpConverter` would map NTP time to ticks via session start wall-clock offset. Useful
  for network-synchronized multi-device setups; out of scope for v1.
