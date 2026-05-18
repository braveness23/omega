# Design: Timing Model

## Decision Summary

| Parameter | Value | Rationale |
|---|---|---|
| Tick resolution | 480 PPQN | SMF standard; divisible by 2, 3, 4, 5, 6, 8, 10, 12, 16, 24 |
| Internal time unit | nanoseconds (`uint64_t`) | Monotonic, no drift, 64-bit wraps in ~584 years |
| Musical time unit | ticks (`uint64_t`) | Absolute from session start, never delta in engine internals |
| Floating point in hot path | Never | All timing math uses integer arithmetic |
| Clock source | `std::chrono::steady_clock` (default) | Monotonic, immune to wall-clock adjustments |

---

## The Core Problem

A MIDI sequencer must answer one question on every engine cycle: **which events are due?**

The answer is: all events whose absolute tick position is ≤ the current tick, and which have not yet fired.

The current tick is derived from elapsed real time and the current tempo. The engine must handle:
- Tempo changes mid-session
- Clock sources other than the internal clock (MIDI sync, Ableton Link)
- Late engine cycles without dropping events (catch-up, not skip)

---

## Tick Resolution: 480 PPQN

480 pulses per quarter note (beat). This is the Standard MIDI File default and divides evenly into all musically useful subdivisions:

| Division | Ticks |
|---|---|
| Whole note | 1920 |
| Half note | 960 |
| Quarter note | 480 |
| Eighth note | 240 |
| Sixteenth note | 120 |
| Thirty-second note | 60 |
| Eighth note triplet | 160 |
| Sixteenth note triplet | 80 |
| Quintuplet | 96 |

PPQN is a compile-time constant in v1. It is exposed in the C API so users can query it; do not hardcode 480 in user code.

```cpp
namespace omega {
    constexpr uint32_t PPQN = 480;
}
```

---

## Time Representation

**Wall-clock time**: `uint64_t` nanoseconds from an arbitrary epoch (session start or `steady_clock` epoch). This is what the clock source provides.

**Musical time**: `uint64_t` ticks from session start (tick 0).

These are two different units. The engine maintains a **tempo map** to convert between them.

---

## The Tempo Map

A tempo map is an ordered list of tempo change points:

```cpp
struct TempoPoint {
    uint64_t tick;           // musical position of this tempo
    uint32_t bpm_milli;      // BPM * 1000 (e.g., 120000 = 120.000 BPM)
    uint64_t ns_at_tick;     // wall-clock nanoseconds at this tick (cached)
};
```

`ns_at_tick` is precomputed when a tempo change is recorded. This makes the tick→ns conversion O(log n) on the tempo map rather than O(n).

**Inserting a tempo change** at tick T with new BPM B:
1. Find the preceding tempo point P
2. Compute `elapsed_ticks = T - P.tick`
3. `new_point.ns_at_tick = P.ns_at_tick + ticks_to_ns(elapsed_ticks, P.bpm_milli)`
4. Invalidate and recompute all subsequent tempo points

**Converting ticks → nanoseconds**:
```
Find the largest TempoPoint with .tick ≤ target_tick.
elapsed_ticks = target_tick - point.tick
ns = point.ns_at_tick + ticks_to_ns(elapsed_ticks, point.bpm_milli)
```

**Converting nanoseconds → ticks** (for recording timestamps):
```
Find the largest TempoPoint with .ns_at_tick ≤ target_ns.
elapsed_ns = target_ns - point.ns_at_tick
ticks = point.tick + ns_to_ticks(elapsed_ns, point.bpm_milli)
```

---

## Integer Arithmetic — No Drift

The core conversion formula, using only integer arithmetic:

```cpp
// nanoseconds per tick at a given BPM (in milli-BPM)
// = 60,000,000,000 ns/min / (BPM * PPQN)
// = 60,000,000,000,000 ns/min / (bpm_milli * PPQN)
uint64_t ns_per_tick(uint32_t bpm_milli) {
    return 60'000'000'000'000ULL / (static_cast<uint64_t>(bpm_milli) * PPQN);
}

uint64_t ticks_to_ns(uint64_t ticks, uint32_t bpm_milli) {
    // Multiply first, divide after — overflow check:
    // max ticks in a session: ~34M (10 min at 120 BPM)
    // 34M * 60T = 2e15, fits in uint64_t (max ~1.8e19)
    return (ticks * 60'000'000'000'000ULL) / (static_cast<uint64_t>(bpm_milli) * PPQN);
}

uint64_t ns_to_ticks(uint64_t ns, uint32_t bpm_milli) {
    return (ns * static_cast<uint64_t>(bpm_milli) * PPQN) / 60'000'000'000'000ULL;
}
```

For BPM=120.000, PPQN=480:
- `ns_per_tick` = 60T / (120000 * 480) = 60T / 57,600,000 = 1,041,666 ns ≈ 1.042 ms ✓

**Overflow safety**: At 300 BPM and 64-bit nanoseconds, the session can run for ~5 hours before overflow. Sessions longer than this are not a design goal for v1.

---

## The Catch-Up Loop

The engine does not fire events from a timer interrupt. It is called periodically by the timing thread (or the caller), and it fires all events that are due since the last call.

```
On each engine cycle:
  current_ns = clock.now()
  current_tick = ns_to_ticks(current_ns - session_start_ns, tempo_map)
  
  For each track/pattern:
    While next_event.tick <= current_tick:
      fire(next_event)
      advance to next event
```

**If a cycle fires late** (thread preempted, system load spike): the next cycle catches up by firing all missed events in order. Events are never skipped. This matches the behavior Emile implemented in KCS using the 200Hz hardware counter.

**If cycles are very frequent** (called faster than the tick rate): the condition `next_event.tick <= current_tick` simply fires nothing, and returns immediately. No busy-wait.

---

## Clock Sources

All clock sources implement:

```cpp
class ClockSource {
public:
    virtual uint64_t now_ns() = 0;   // monotonic nanoseconds
    virtual ~ClockSource() = default;
};
```

**InternalClock**: `std::chrono::steady_clock::now().time_since_epoch().count()` — monotonic, OS-provided, ~1–10ns resolution on modern hardware.

**MidiClockSource**: Receives MIDI clock pulses (24 PPQN). Interpolates between pulses to provide finer resolution. Must translate 24-PPQN MIDI clock into 480-PPQN omega ticks.

**LinkClockSource** (optional, GPL): Ableton Link provides beat time as a double. Convert to nanoseconds and ticks via the tempo map. Link also provides tempo; the engine must update its tempo map when Link tempo changes.

**HostClockSource** (for plugin use): The host DAW provides sample position and sample rate. Convert: `ns = (sample_position * 1,000,000,000) / sample_rate`.

---

## Tempo Tap and MIDI CC Tempo Control

Both are implemented as calls to `engine.set_tempo(bpm)` which inserts a new TempoPoint at the current tick. The catch-up loop naturally handles the resulting tempo change.

---

## Relationship to MIDI Standard

Standard MIDI files store tempo as **microseconds per beat** (a 24-bit value). Omega stores BPM as milli-BPM (`uint32_t`). Conversion:

```cpp
uint32_t midi_tempo_to_bpm_milli(uint32_t us_per_beat) {
    return 60'000'000'000ULL / us_per_beat;  // rounds down
}
uint32_t bpm_milli_to_midi_tempo(uint32_t bpm_milli) {
    return 60'000'000'000ULL / bpm_milli;
}
```

SMF import/export translates through these functions. The internal representation is always milli-BPM.

---

## Open Issues

- **Sub-tick resolution**: Should events be schedulable at nanosecond precision within a tick, or is tick-level precision sufficient? Deferring to v2.
- **SMPTE/MTC**: Time code sync is not in scope for v1.
- **Tempo ramps**: Gradual tempo changes (accelerando/ritardando) require a continuous tempo function rather than discrete TempoPoints. Defer to v2 — implement as many closely-spaced TempoPoints for now.
