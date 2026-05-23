# Design: Time Signature and Position Coordinate Systems

## Status

**Proposed.** Implementation target: after M4 (Orchestration Layer), before M5 (Platform
Integrations). Tagged `v0.3.5-alpha`. See the [Milestone Placement](#milestone-placement-m45)
section for sprint breakdown.

---

## Decision Summary

| Question | Decision | Rationale |
|---|---|---|
| Where does meter live? | `TimeSignatureMap` on `Session`, parallel to `TempoMap` | Same access pattern; same mutation model |
| Does meter affect tick math? | Never | Engine stays purely tick-based; coordinate systems are a query layer |
| How is freeform expressed? | Empty `TimeSignatureMap` (no entry at tick 0) | No special flag; absence is the contract |
| What does "no meter" mean to callers? | Format-specific error: `OMEGA_ERR_NO_METER` | Explicit failure rather than a silent 4/4 fallback |
| Navigation helper pattern? | `PositionConverter` interface; `MeterCursor` is its first implementation | Allows future formats (SMPTE, samples) to fit the same shape |
| SMPTE / timecode? | `SmpteConfig` on `Session`; `SmpteConverter` implements `PositionConverter` | Orthogonal to musical meter; separate config, same pattern |
| Samples-based position? | Handled at `HostClockSource` layer; not a separate `PositionConverter` | Sample rate belongs to the clock context, not the session |
| C API naming? | Format-specific functions (`omega_tick_to_beat_pos`, `omega_tick_to_smpte`, …) | Avoids ambiguous generic names that collide across coordinate systems |
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

A secondary problem, exposed by broadening the scope: **musical meter is not the only coordinate
system in use.** Film/broadcast work uses SMPTE timecode (HH:MM:SS:FF). Audio plugins and DAWs
pass sample counts. Broadcast automation uses wall-clock time. The design must not assume musical
meter is the only way to express "where in time are we?" — future coordinate systems must fit
without API surgery.

---

## Coordinate Systems — Design Principle

The engine's canonical time representations are **nanoseconds** (wall-clock) and **ticks**
(musical, tempo-dependent). These are the only units the timing thread ever touches.

Everything else — bars and beats, SMPTE addresses, sample counts, wall-clock display strings — is
a **coordinate system**: a way to express a tick or nanosecond position in human- or
application-meaningful terms. Coordinate systems are a **non-realtime query layer**. None of them
are ever consulted inside `process()`, `advance()`, or any timing-thread path.

This constraint is architectural and must not be violated:

> **The timing thread knows only ticks and nanoseconds. Coordinate systems live entirely
> outside the hot path.**

The `PositionConverter` interface (below) formalizes this pattern so that new coordinate systems
can be added without touching the engine, the command queue, or any existing converter.

### Industry coordinate systems

For reference, the formats in active use across the industry:

| Format | Representation | Typical use |
|---|---|---|
| **Musical** | bar / beat / subdivision | DAWs, MIDI sequencers, this library |
| **SMPTE/MTC** | HH:MM:SS:FF at a given frame rate | Film scoring, broadcast, sync to picture |
| **Samples** | integer sample count + sample rate | Audio plugins (VST3/AU/AAX/CLAP), audio DAWs |
| **Wall clock** | HH:MM:SS.mmm | Broadcast automation, cue sheets, scheduling |
| **Feet + frames** | FF+FF (35mm: 16 fr/ft; 16mm: 40 fr/ft) | Film editing (Avid, DaVinci Resolve) |
| **OSC / NTP** | 64-bit fixed-point seconds since 1900 | OSC bundle timestamps, SuperCollider, Max/MSP |
| **Ableton Link** | fractional beat count, shared network epoch | Network beat sync across applications |

Omega v1 implements **musical** and **SMPTE** converters. The others are either trivial derivations
(samples = ns × rate, wall clock = ns offset from start) or out of scope for v1. The
`PositionConverter` interface ensures they can be added without changing the existing API surface.

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

When `TimeSignatureMap::is_freeform()` is true, the session has no musical meter. This is entered
by calling `omega_timesig_clear()` or by never calling `omega_timesig_set()` — the map starts
empty on every new session.

The same principle applies to SMPTE: when `SmpteConfig` is absent, there is no timecode
coordinate system. A session can be metered without SMPTE, SMPTE-configured without musical
meter, both, or neither. These are independent dimensions.

**What changes when musical meter is absent:**
- All `MeterCursor` helpers return `OMEGA_ERR_NO_METER`
- `omega_tick_to_beat_pos()` and `omega_beat_pos_to_tick()` return `OMEGA_ERR_NO_METER`
- `OMEGA_CUE_BAR` degrades to `OMEGA_CUE_AT_BOUNDARY` (see below)
- SMF export emits no time-signature meta events

**What does not change in any freeform configuration:**
- The engine runs and dispatches events exactly as before
- All three built-in sources work without modification
- Recording, looping, and transport all continue to operate on ticks
- `TempoMap` is completely independent; freeform sessions can still have tempo changes

There is no "enter freeform" call. The absence of configuration is the condition. A session that
previously had meter becomes freeform by calling `omega_timesig_clear()`.

---

## `PositionConverter` — The General Interface

All coordinate system translators implement this interface. It lives in the non-realtime layer;
no implementation may be called from the timing thread.

```cpp
// A tick-addressable position in some coordinate system.
// The active type is determined by which PositionConverter produced it.
// Callers must not mix positions from different converters.
struct Position {
    union {
        struct { uint32_t bar; uint8_t beat; uint32_t subdivision; } beat;
        struct { uint8_t h; uint8_t m; uint8_t s; uint8_t frame;
                 uint16_t subframe; }                                smpte;
        // future: sample count, wall clock, feet+frames, …
    };
};

class PositionConverter {
public:
    // Convert absolute tick → Position.
    // Returns an error code if the session lacks the required configuration
    // for this coordinate system (e.g., OMEGA_ERR_NO_METER, OMEGA_ERR_NO_SMPTE_CONFIG).
    virtual omega_status_t tick_to_pos(uint64_t tick, Position& out) const = 0;

    // Convert Position → absolute tick.
    // Returns OMEGA_ERR_INVALID if the position is out of range or inconsistent.
    virtual omega_status_t pos_to_tick(const Position& pos, uint64_t& out) const = 0;

    // Tick of the next boundary of this system's primary unit at or after from_tick.
    // For MeterCursor: next bar boundary.
    // For SmpteConverter: next whole-frame boundary.
    virtual omega_status_t next_boundary(uint64_t from_tick, uint64_t& out) const = 0;

    // Quantize tick to the nearest primary unit boundary.
    virtual omega_status_t quantize(uint64_t tick, uint64_t& out) const = 0;

    virtual ~PositionConverter() = default;
};
```

`MeterCursor` and `SmpteConverter` both derive from `PositionConverter`. Application code that
wants to be format-agnostic (e.g., a generic "snap to grid" operation) can accept a
`PositionConverter&`. Format-specific code uses the concrete type directly for its additional
methods (e.g., `MeterCursor::next_beat_tick()`).

---

## `MeterCursor` — Musical Bar/Beat Navigation

`MeterCursor` implements `PositionConverter` for the musical coordinate system.
**Must not be called from the timing thread.**

```cpp
struct BeatPosition {
    uint32_t bar;         // 1-based
    uint8_t  beat;        // 1-based, 1..numerator
    uint32_t subdivision; // ticks past the beat boundary (0..ticks_per_beat - 1)
};

class MeterCursor : public PositionConverter {
public:
    explicit MeterCursor(const TimeSignatureMap& map);

    // PositionConverter overrides — Position.beat field is active.
    omega_status_t tick_to_pos(uint64_t tick, Position& out) const override;
    omega_status_t pos_to_tick(const Position& pos, uint64_t& out) const override;
    omega_status_t next_boundary(uint64_t from_tick, uint64_t& out) const override;  // → next bar
    omega_status_t quantize(uint64_t tick, uint64_t& out) const override;             // → nearest beat

    // Musical-specific helpers (not on base interface):
    omega_status_t tick_to_beat_pos(uint64_t tick, BeatPosition& out) const;
    omega_status_t beat_pos_to_tick(const BeatPosition& pos, uint64_t& out) const;
    omega_status_t next_bar_tick(uint64_t from_tick, uint64_t& out) const;
    omega_status_t next_beat_tick(uint64_t from_tick, uint64_t& out) const;
    omega_status_t quantize_to_beat(uint64_t tick, uint64_t& out) const;
    omega_status_t quantize_to_subdivision(uint64_t tick, uint64_t subdiv_ticks,
                                           uint64_t& out) const;
};
```

**`tick_to_beat_pos` implementation sketch:**
```
1. If map is freeform → OMEGA_ERR_NO_METER
2. Walk adjacent TimeSigPoint pairs, accumulating bar count:
   For each segment [current_point, next_point):
     span_ticks = next_point.tick - current_point.tick
     bars_in_span = span_ticks / ticks_per_bar(current_point)
     If target_tick falls within this segment:
       offset = target_tick - current_point.tick
       bar_offset    = offset / ticks_per_bar(current_point)
       beat_offset   = (offset % ticks_per_bar) / ticks_per_beat
       subdivision   = offset % ticks_per_beat
       Return {accumulated_bars + bar_offset + 1, beat_offset + 1, subdivision}
     Else:
       accumulated_bars += bars_in_span
       (partial bars at end of segment belong to the next meter)
3. Handle the final open-ended segment (no next point) the same way.
```

O(n) in the number of time signature changes — never called from the timing thread.

---

## `SmpteConfig` — Timecode Configuration

SMPTE timecode requires a frame rate and a drop-frame flag. Unlike musical meter, the frame rate
of a session almost never changes mid-session; a single config struct is sufficient (no map).

```cpp
enum class SmpteRate : uint8_t {
    FPS_24     = 24,
    FPS_25     = 25,
    FPS_2997   = 30,  // 29.97 — use drop_frame flag to distinguish DF/NDF
    FPS_30     = 31,  // distinct from FPS_2997; stored as 31 to avoid ambiguity
    FPS_48     = 48,
    FPS_60     = 60,
};

struct SmpteConfig {
    SmpteRate rate;
    bool      drop_frame;  // only meaningful for FPS_2997; ignored for others

    bool is_valid() const;  // drop_frame=true is invalid for non-2997 rates
};
```

`SmpteConfig` is an optional field on `Session`. When absent, `SmpteConverter` returns
`OMEGA_ERR_NO_SMPTE_CONFIG` from all methods. Setting it goes through the command queue.

**29.97 drop-frame** bookkeeping: frame numbers 00 and 01 are skipped at the start of each minute
except every 10th minute. This keeps the displayed timecode synchronized with real wall-clock time
over long durations. Drop-frame is purely a numbering convention — no actual frames are dropped.
Conversion between drop-frame timecode and frame counts requires the standard DF correction
formula; this is encapsulated entirely within `SmpteConverter`.

**SMPTE as clock source vs. SMPTE as position format are distinct concerns.** `MidiClockSource`
(M5) can receive MTC quarter-frame messages and use them to drive the engine's position — that is
the *input/clock* side. `SmpteConverter` is the *query* side: given a tick, what is the SMPTE
address? These do not share code. A session can use `SmpteConverter` for display without any MTC
input, and vice versa.

---

## `SmpteConverter` — SMPTE/Timecode Navigation

`SmpteConverter` implements `PositionConverter` for SMPTE timecode.
**Must not be called from the timing thread.**

```cpp
struct SmptePosition {
    uint8_t  hours;
    uint8_t  minutes;
    uint8_t  seconds;
    uint8_t  frames;
    uint16_t subframes;  // 1/100ths of a frame; 0 if subframe resolution not needed
};

class SmpteConverter : public PositionConverter {
public:
    SmpteConverter(const SmpteConfig& config, const TempoMap& tempo_map);

    // PositionConverter overrides — Position.smpte field is active.
    omega_status_t tick_to_pos(uint64_t tick, Position& out) const override;
    omega_status_t pos_to_tick(const Position& pos, uint64_t& out) const override;
    omega_status_t next_boundary(uint64_t from_tick, uint64_t& out) const override;  // → next frame
    omega_status_t quantize(uint64_t tick, uint64_t& out) const override;             // → nearest frame

    // SMPTE-specific helpers:
    omega_status_t tick_to_smpte(uint64_t tick, SmptePosition& out) const;
    omega_status_t smpte_to_tick(const SmptePosition& pos, uint64_t& out) const;
    omega_status_t next_frame_tick(uint64_t from_tick, uint64_t& out) const;
};
```

`SmpteConverter` uses `TempoMap` because tick→ns conversion is tempo-dependent, and SMPTE
addresses map to wall-clock time (ns), not musical ticks directly. The conversion path is:
`tick → ns (via TempoMap) → frame count (via SmpteConfig) → HH:MM:SS:FF`.

Both `MeterCursor` and `SmpteConverter` are instantiated by the caller on demand, not owned by
the engine. They read immutable snapshots. Thread safety: safe to call from any non-timing thread
concurrently, as they hold no mutable state.

---

## Session Ownership Update

```
Session
├── TempoMap                  (unchanged)
├── TimeSignatureMap          (replaces single TimeSignature; empty = freeform)
├── SmpteConfig               (optional; absent = no timecode coordinate system)
├── PatternLibrary            (unchanged)
│   ...
```

**Serialization**: `"time_signature"` → `"time_signature_map"`; `"smpte_config"` is new:

```json
{
  "time_signature_map": [
    { "tick": 0,    "numerator": 4, "denominator": 4 },
    { "tick": 7680, "numerator": 7, "denominator": 8 }
  ],
  "smpte_config": { "rate": 24, "drop_frame": false }
}
```

A freeform session: `"time_signature_map": []`, `"smpte_config"` key absent.

**Migration**: on load, if the old `"time_signature"` key is present and `"time_signature_map"` is
absent, synthesize a single-entry map at tick 0. This is the only backward-compatibility shim.

---

## Command Variants

`TimeSignatureMap` is read by `PerformanceSource::advance()` on the timing thread (for
`OMEGA_CUE_BAR` boundary calculation). `SmpteConfig` is not read by the timing thread at all —
it is a query-layer concern only. Both mutations go through the SPSC command queue for consistency
and to ensure the mutation thread never writes while the timing thread reads.

```cpp
struct SetTimeSigCmd    { uint64_t tick; uint8_t numerator; uint8_t denominator; };
struct RemoveTimeSigCmd { uint64_t tick; };
struct ClearTimeSigCmd  {};
struct SetSmpteCmd      { SmpteConfig config; };
struct ClearSmpteCmd    {};
```

---

## C API Additions

Functions are named by coordinate system, not generically, to avoid ambiguity as new systems
are added.

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

omega_status_t omega_timesig_remove(omega_engine_t engine, omega_tick_t tick);
omega_status_t omega_timesig_clear(omega_engine_t engine);

/* Query the active time signature at tick. Returns OMEGA_ERR_NO_METER if freeform.
   Safe to call from any thread. */
omega_status_t omega_timesig_at(omega_engine_t engine, omega_tick_t tick,
                                 omega_time_sig_point_t* out);

int omega_is_freeform(omega_engine_t engine);  /* 1 if no time sig entries */

/* ── Musical Bar/Beat Conversion ─────────────────────────────────────── */
/* Do not call from the timing thread.                                    */

typedef struct {
    uint32_t bar;         /* 1-based */
    uint8_t  beat;        /* 1-based */
    uint32_t subdivision; /* ticks past the beat boundary */
} omega_beat_pos_t;

omega_status_t omega_tick_to_beat_pos(omega_engine_t engine, omega_tick_t tick,
                                       omega_beat_pos_t* out);

omega_status_t omega_beat_pos_to_tick(omega_engine_t engine,
                                       const omega_beat_pos_t* pos, omega_tick_t* out);

omega_status_t omega_next_bar_tick(omega_engine_t engine, omega_tick_t from_tick,
                                    omega_tick_t* out);

omega_status_t omega_quantize_to_beat(omega_engine_t engine, omega_tick_t tick,
                                       omega_tick_t* out);

/* ── SMPTE Timecode ──────────────────────────────────────────────────── */

typedef enum {
    OMEGA_SMPTE_24    = 24,
    OMEGA_SMPTE_25    = 25,
    OMEGA_SMPTE_2997  = 30,  /* 29.97 — set drop_frame to distinguish DF/NDF */
    OMEGA_SMPTE_30    = 31,
    OMEGA_SMPTE_48    = 48,
    OMEGA_SMPTE_60    = 60,
} omega_smpte_rate_t;

typedef struct {
    uint8_t  hours;
    uint8_t  minutes;
    uint8_t  seconds;
    uint8_t  frames;
    uint16_t subframes;  /* 1/100ths of a frame */
} omega_smpte_pos_t;

/* Set the session's SMPTE frame rate and drop-frame flag.
   Returns OMEGA_ERR_INVALID if drop_frame is set for a non-29.97 rate. */
omega_status_t omega_smpte_configure(omega_engine_t engine, omega_smpte_rate_t rate,
                                      int drop_frame);

omega_status_t omega_smpte_clear(omega_engine_t engine);
int            omega_smpte_is_configured(omega_engine_t engine);

/* Convert absolute tick → SMPTE address.
   Returns OMEGA_ERR_NO_SMPTE_CONFIG if SMPTE is not configured. */
omega_status_t omega_tick_to_smpte(omega_engine_t engine, omega_tick_t tick,
                                    omega_smpte_pos_t* out);

/* Convert SMPTE address → absolute tick.
   Returns OMEGA_ERR_NO_SMPTE_CONFIG or OMEGA_ERR_INVALID (out-of-range address). */
omega_status_t omega_smpte_to_tick(omega_engine_t engine, const omega_smpte_pos_t* pos,
                                    omega_tick_t* out);
```

**New status codes** (add to `omega_status_t` enum):
```c
OMEGA_ERR_NO_METER        = -8,   /* operation requires a time signature; session is freeform */
OMEGA_ERR_NO_SMPTE_CONFIG = -9,   /* operation requires SMPTE configuration */
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
directly — not through `MeterCursor`. The cursor's segment-walk allocates; the timing thread uses
a simple inline calculation: find the active `TimeSigPoint` at the current tick, compute
`ticks_per_bar`, find the next multiple.

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

SMF carries no standardized SMPTE frame-rate metadata beyond the file header's `division` field
(which signals SMPTE time code mode vs. tick-based). Omega v1 imports and exports tick-based SMF
only. SMPTE-division SMF (used for film work) is noted as a future item.

---

## Testing Strategy

New file: `tests/unit/test_time_signature_map.cpp`

**`TimeSignatureMap` unit tests:**
- Insert and query: single point, multiple points, query before first entry, between entries
- `remove()`: nominal and `OMEGA_ERR_NOT_FOUND`
- `clear()`: map becomes freeform; subsequent `at()` returns nullptr

**`MeterCursor` unit tests:**
- `ticks_per_bar` correctness for 4/4, 3/4, 7/8, 5/4, 6/8, 12/8
- `tick_to_beat_pos` / `beat_pos_to_tick` round-trip identity within a single meter
- Round-trip across a meter change boundary (e.g., 4/4 → 7/8 at tick 7680)
- `next_bar_tick` at tick 0, mid-bar, and exactly on a bar boundary
- `quantize_to_beat` rounds correctly at midpoint and edges
- All helpers return `OMEGA_ERR_NO_METER` when map is empty

**`SmpteConverter` unit tests:**
- `tick_to_smpte` / `smpte_to_tick` round-trip for 24, 25, 29.97 NDF, 29.97 DF, 30 fps
- Drop-frame: 01:00:00:00 DF is distinct from 01:00:00:00 NDF; verify frame count differs
- All helpers return `OMEGA_ERR_NO_SMPTE_CONFIG` when config is absent

**`PositionConverter` interface test:**
- A generic snap-to-grid function that accepts `PositionConverter&` works identically when
  called with `MeterCursor` or `SmpteConverter`

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
- `include/omega/position_converter.h` — `Position`, `PositionConverter` interface
- `include/omega/time_signature_map.h` — `TimeSigPoint`, `TimeSignatureMap`, `BeatPosition`,
  `MeterCursor`
- `src/time_signature_map.cpp`
- `SetTimeSigCmd`, `RemoveTimeSigCmd`, `ClearTimeSigCmd` added to command variant list
- `Session`: `TimeSignatureMap` replaces the `TimeSignature` field; serialization updated;
  old-key migration path

**Definition of done:**
- All unit tests pass under ASan + UBSan
- `tick_to_beat_pos → beat_pos_to_tick` round-trip is identity for 4/4, 7/8, and 5/4, including
  across a meter change boundary
- `MeterCursor` is accepted where `PositionConverter&` is expected

### Sprint 4.5.2 — `SmpteConfig` + `SmpteConverter`

**Deliverables:**
- `include/omega/smpte.h` — `SmpteRate`, `SmpteConfig`, `SmptePosition`, `SmpteConverter`
- `src/smpte.cpp` — including drop-frame correction formula
- `SetSmpteCmd`, `ClearSmpteCmd` command variants
- `Session`: `SmpteConfig` optional field; serialization (`"smpte_config"` key)

**Definition of done:**
- `tick_to_smpte → smpte_to_tick` round-trip for all six frame rates, including 29.97 DF
- Drop-frame discontinuity test: frame numbers 00 and 01 are absent at non-10th-minute boundaries
- `OMEGA_ERR_NO_SMPTE_CONFIG` returned correctly when config is absent

### Sprint 4.5.3 — C API + `OMEGA_CUE_BAR`

**Deliverables:**
- All `omega_timesig_*`, `omega_tick_to_beat_pos`, `omega_beat_pos_to_tick`,
  `omega_next_bar_tick`, `omega_quantize_to_beat` functions in `src/omega_c.cpp`
- All `omega_smpte_*` functions
- `OMEGA_ERR_NO_METER`, `OMEGA_ERR_NO_SMPTE_CONFIG` status codes in `include/omega/omega.h`
- `OMEGA_CUE_BAR` cue mode with freeform degradation in `PerformanceSource`

**Definition of done:**
- C API compiles and passes tests from a pure C99 file (no C++ headers included)
- `OMEGA_CUE_BAR` test: pattern in 4/4 waits for bar; same pattern in freeform falls back
  without hang or assert
- TSan clean under OmegaTimer + mutation thread calling `omega_timesig_set()` concurrently

---

## Open Issues

- **Compound meter display**: 6/8 and 12/8 are felt in 2 and 4 respectively (dotted quarter
  beats). `MeterCursor` treats every denominator unit as a beat. A `compound_beat_grouping` hint
  on `TimeSigPoint` could expose this in v2.
- **Per-pattern time signature**: patterns have only `length_ticks`. A pattern-local `TimeSigPoint`
  would let `MeterCursor` show bar/beat positions within a pattern independently of session meter.
  Deferred to v2.
- **Feet + frames**: a `FeetFramesConverter` implementing `PositionConverter` would be a small
  addition — it is essentially `SmpteConverter` with a different display mapping. No config
  beyond the film gauge (35mm vs 16mm). Deferred to v2.
- **Sample-based position**: converting ticks to sample counts requires a sample rate from the
  host (DAW or plugin context). This is provided by `HostClockSource` at the clock layer, not
  stored on the session. A `SampleConverter` taking `(TempoMap&, uint32_t sample_rate)` would
  complete the set. Deferred to v2; the `PositionConverter` interface already accommodates it.
- **SMPTE-division SMF**: SMF files with a SMPTE `division` field (used in film scoring) cannot
  be imported as tick-based sessions without a frame-rate-to-tempo conversion. Deferred to v2.
- **OSC/NTP timestamps**: if Omega emits OSC events (the blob store already supports OSC message
  types), bundle timestamps must be NTP. A utility function `omega_ns_to_ntp()` outside the
  `PositionConverter` hierarchy is sufficient — NTP time is not a musical position concept.
- **Irrational meters** (e.g., 4/6 as used in some contemporary scores): not representable with
  power-of-2 denominators. Out of scope for v1.
- **MIDI clock and bar sync**: `MidiClockSource` could emit a bar-pulse event when `OMEGA_CUE_BAR`
  is active, allowing external sync to bar boundaries. Deferred to a later M5 sprint.
- **Tempo ramps and meter changes on the same tick**: both are applied at the top of the next
  `process()` cycle. Order within one cycle does not affect `MeterCursor` since it reads the
  final map state, but should be documented in the command-processing implementation.
