# Design: Pattern State Machine

This document covers the cuing and loop logic for Pattern and Performance modes. This is the most stateful part of the engine and the part most likely to have subtle bugs if not designed carefully.

---

## Concepts

**Pattern**: A named, looped sequence of events with a defined `length_ticks`. Patterns are stored in the Pattern Library and referenced by ID.

**Slot** (Performance mode only): A numbered position in the Performance grid. A slot can have a pattern assigned to it. Slots are independent — they play simultaneously.

**Loop boundary**: The tick at which a playing pattern completes one full cycle. For a pattern of `length_ticks = L` starting at `start_tick = S`, loop boundaries occur at `S`, `S + L`, `S + 2L`, etc.

**Phase**: The offset within the current pattern loop: `phase = (current_tick - start_tick) % length_ticks`.

---

## Slot States

```
┌─────────┐
│  EMPTY  │  No pattern assigned.
└────┬────┘
     │ assign(pattern_id)
     ▼
┌─────────┐
│  IDLE   │  Pattern assigned, not playing.
└────┬────┘
     │ cue(BOUNDARY) ──────────────────────────────────┐
     │                                                  │
     │ cue(IMMEDIATE) ─────────────────────────────┐   │
     ▼                                             │   ▼
┌─────────┐  loop boundary                    ┌──────────┐
│ QUEUED  │ ──────────────────────────────────►│ PLAYING  │
└────┬────┘                                    └────┬─────┘
     │ cancel / cue(same, BOUNDARY)                │
     │                                              │ cue_stop(BOUNDARY)
     ▼                                              │ or cue(different, BOUNDARY) ─┐
   IDLE                                             ▼                              │
                                              ┌──────────┐                         │
                                              │ STOPPING │ ◄───────────────────────┘
                                              └────┬─────┘
                                                   │ loop boundary
                                                   ▼
                                                  IDLE
                                 (or PLAYING, if a new pattern was queued)
```

**EMPTY**: No pattern assigned. The slot does nothing.
**IDLE**: Pattern assigned; slot is silent and awaiting a cue.
**QUEUED**: A cue has been received; waiting for the next loop boundary to start.
**PLAYING**: Pattern is actively playing and looping.
**STOPPING**: A stop has been received; will go silent at the next loop boundary.

---

## Transitions

### assign(pattern_id)
- EMPTY → IDLE
- IDLE → IDLE (change the assigned pattern; takes effect at next cue)
- QUEUED → QUEUED (change the queued pattern)
- PLAYING → (no change to state; schedules a switch at next boundary)

### cue(slot, BOUNDARY)
- IDLE → QUEUED
- QUEUED → QUEUED (update queued pattern; no-op if same pattern)
- PLAYING → STOPPING + new slot QUEUED (if different pattern)
- PLAYING → (toggle: go STOPPING if same pattern cued again)
- STOPPING → QUEUED (cancel the stop, queue restart)

### cue(slot, IMMEDIATE)
- IDLE → PLAYING (start immediately, phase = 0)
- QUEUED → PLAYING (start immediately, cancel queue)
- PLAYING → PLAYING (restart from phase = 0 immediately)
- STOPPING → PLAYING (cancel stop, restart immediately)

### cue_stop(slot, BOUNDARY)
- PLAYING → STOPPING
- QUEUED → IDLE (cancel before it started)

### cue_stop(slot, IMMEDIATE)
- PLAYING → IDLE (fires note-off for all active notes immediately)
- QUEUED → IDLE
- STOPPING → IDLE (accelerate the stop)

### stop_all(BOUNDARY)
All PLAYING slots → STOPPING
All QUEUED slots → IDLE

### stop_all(IMMEDIATE)
All PLAYING slots → IDLE (immediate note-off)
All QUEUED slots → IDLE

### Loop boundary (internal, fired by engine)
- QUEUED → PLAYING
- STOPPING → IDLE (or PLAYING if another pattern was queued)

### unassign
- Any state → EMPTY (immediate note-off if PLAYING)

---

## Loop Boundary Detection

A loop boundary for slot S occurs when:
```
current_tick >= slot.start_tick + (slot.loop_count + 1) * pattern.length_ticks
```

Where `loop_count` is the number of complete loops so far (0-indexed).

Alternatively, using modular arithmetic:
```
boundary_tick = slot.start_tick + ceil_div(current_tick - slot.start_tick, pattern.length_ticks) * pattern.length_ticks
```

The engine checks for pending transitions at the loop boundary during `process()`. If a QUEUED slot's pattern has a different length than the STOPPING slot's pattern, the engine uses the **stopping slot's loop boundary** (not the queued pattern's length) to determine when to switch.

---

## Phase and Start Alignment

When a pattern starts (QUEUED → PLAYING), the engine records `slot.start_tick`.

**Global beat alignment** (default): `start_tick` is rounded up to the next multiple of the queued pattern's length relative to the global playback origin (tick 0).
```
start_tick = ceil_div(current_tick, pattern.length_ticks) * pattern.length_ticks
```

This keeps patterns phase-coherent with the global grid. Two slots cued at the same time always start at the same boundary.

**Free start** (opt-in): `start_tick = current_tick`. The pattern starts at phase 0 immediately regardless of global position. Use for slots that don't need to be grid-aligned.

The cue mode enum: `OMEGA_CUE_AT_BOUNDARY` uses global alignment. `OMEGA_CUE_QUANTIZED` snaps to the nearest beat. `OMEGA_CUE_IMMEDIATE` sets `start_tick = current_tick`.

---

## Per-Slot Real-Time Parameters

These parameters are applied during event dispatch, not stored in the pattern:

**Transpose** (`int8_t semitones`, range -24 to +24):
Applied to note-on and note-off MIDI note numbers. Clamped to 0–127.
```
dispatched_note = clamp(event.note + slot.transpose, 0, 127)
```

**Velocity scale** (`uint8_t pct`, 0–200, 100=unity):
Applied to note-on velocity.
```
dispatched_velocity = clamp((event.velocity * slot.velocity_scale) / 100, 1, 127)
```
Velocity 0 means note-off in MIDI; minimum dispatched velocity is 1.

**Random bias** (`uint8_t pct`, 0–100):
A probabilistic pitch offset applied per note. At 0%, no effect. At 100%, the note may be offset by up to ±bias_range semitones (default: ±5).
```
if (rand_float() < slot.bias_pct / 100.0f) {
    offset = random_int(-bias_range, +bias_range)
    dispatched_note = clamp(event.note + slot.transpose + offset, 0, 127)
}
```
The random seed per slot is seeded at session start and advances deterministically during playback, making the same session reproducible if desired.

These parameters are set via the command queue (mutation thread) and read during `process()` (timing thread). Use `std::atomic` for each parameter — they are single scalar values and atomic reads are sufficient without the full command queue round-trip.

---

## Pattern Mode vs. Performance Mode

**Pattern mode** is a simplified view: patterns arranged on a linear timeline with repeat counts, chained by index. There is no slot state machine; instead, each timeline position has a `(pattern_id, start_tick, repeat_count)` tuple. The engine plays the song arrangement linearly.

**Performance mode** uses the full slot state machine described here. Pattern mode and Performance mode can coexist in a session — a song arrangement in Pattern mode can be playing while Performance slots are live.

---

## Note-Off on State Transitions

Whenever a slot transitions to IDLE (for any reason), the engine must silence all notes that the slot had active. This is done via the active notes table (see Memory document): scan all active notes tagged with this slot's sink/channel combination and fire note-off immediately.

This prevents stuck notes when patterns are stopped mid-phrase.

---

## Edge Cases

**Zero-length pattern**: `length_ticks = 0`. Treated as a one-shot — plays once, then transitions to IDLE automatically. No loop.

**Pattern shorter than one beat**: Legal. The loop boundary fires multiple times per beat. Performance correctly handles sub-beat patterns.

**Simultaneous cue of all slots**: The engine processes slot transitions in slot-index order within a single `process()` call. Order is deterministic.

**Pattern changed while PLAYING**: The new pattern takes effect at the next loop boundary. Current loop plays to completion with the old pattern.

**Pattern deleted from library while a slot references it**: The slot transitions to IDLE with immediate note-off. The engine detects the invalid pattern ID during the next playback scan.
