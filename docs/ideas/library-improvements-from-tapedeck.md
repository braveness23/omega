# Library Improvements — lessons from the tapedeck example

Source: observations made while writing `examples/tapedeck/` (2026-05-25).
These are gaps actually hit during development, ranked by impact.

---

## 1. Implement `SetLoopCmd` (high impact)

`SetLoopCmd` is defined in `commands.h` and documented in the API, but
`Engine::apply()` never handles it. The natural model for a tapedeck — or
any linear looping sequencer — is a transport loop region on the timeline.
Because it's unimplemented, the only looping primitive available is
`PerformanceSource`, which is really a clip-launcher (Ableton Session View
style), not a tape loop. The workaround functions but leaves the
`TimelineSource` idle and forces the wrong mental model on the consumer.

**What to implement:** When the transport reaches `loop.end_tick`, locate
back to `loop.start_tick` and continue. `TimelineSource::on_locate()` and
chase already exist; the engine just needs to detect the boundary in
`process()`.

---

## 2. Add `omega_tempo_set` and `omega_sink_id` to the C API (high impact)

The C API (`omega.h`) has `omega_timesig_set` but no tempo equivalent.
Setting tempo requires dropping into C++ (`engine.tempo_map().insert()`),
which breaks the "stable ABI" promise for any consumer using only the C
surface.

Additionally, the comment on `omega_sink_create_midi_out` says:
> use `omega_sink_id(sink)` to route events to it

— but that function does not exist in `omega.h`. Any pure-C consumer
wanting to populate pattern events with the correct `sink_id` has no way
to get it.

**Suggested additions:**

```c
/* Set tempo at tick (bpm_milli = BPM × 1000, e.g. 120 BPM = 120000). */
OMEGA_API omega_status_t omega_tempo_set(omega_engine_t* e,
                                         uint64_t tick,
                                         uint32_t bpm_milli);

/* Returns the unique sink_id for routing events to this sink. */
OMEGA_API uint32_t omega_sink_id(const omega_sink_t* sink);
```

---

## 3. Provide a thread-safe position snapshot (medium impact)

`omega_engine_position_ns()` is safe to call from any thread but stale.
Converting nanoseconds to bar/beat requires `MeterCursor` or
`PositionConverter`, which are mutation-thread-only. Any display loop on a
separate thread is left to recompute the conversion manually in floating
point — which is error-prone and duplicates internal logic.

**Suggested addition:**

```c
typedef struct {
    uint32_t bar;         /* 1-based */
    uint8_t  beat;        /* 1-based */
    uint32_t subdivision; /* ticks past the beat boundary */
    uint64_t loop_count;  /* number of times the loop region has wrapped */
} omega_position_t;

/* Atomic snapshot; safe to call from any thread. */
OMEGA_API omega_status_t omega_engine_position(const omega_engine_t* e,
                                               omega_position_t* out);
```

The engine can maintain this as an atomically-updated struct written at the
end of each `process()` cycle, eliminating the need for consumers to
reimplement the conversion.

---

## 4. Document (or enforce) sink-before-pattern ordering (low impact)

The `sink_id` needed to populate pattern events is assigned at
`LibremidiSink` construction from a global counter. The consumer must
therefore create the sink first, retrieve its ID, then build the pattern.
This ordering constraint is invisible in the API — nothing enforces or
mentions it. A developer who builds the pattern first (natural order of
thinking about a session) will silently route all events to the wrong sink.

**Options:**
- Document the required order explicitly in `omega_sink_create_midi_out`
  and `omega_pattern_add_event`.
- Alternatively, allow patterns to use a sentinel `sink_id` of `0` meaning
  "default sink" (the first registered sink), resolved at dispatch time.
