# Omega — Design Proposal

A C++ sequencing engine and MIDI library. The foundation for building serious music software.

---

## Vision

Omega is a library, not an application. It provides complete machinery for sequencing, timing, and event delivery — everything needed to build a sequencer of any kind, with any interface, for any purpose.

The goal: be the foundation that serious music software is built on. Not a DAW. Not a synthesizer. Not a UI. The engine underneath all of those things.

No such library exists today. MIDI I/O libraries exist. MIDI file parsers exist. Full DAW frameworks exist. Nothing in between covers the engine — the timing model, the event lifecycle, multi-track and pattern state, and live performance semantics — as a portable, embeddable, UI-agnostic library.

---

## What Omega Is Not

- Not a UI framework
- Not a synthesizer or audio engine
- Not MIDI-only
- Not a DAW
- Not opinionated about how users interact with it

---

## Design Principles

**UI-agnostic.** Omega exposes a stable C API (`extern "C"`) so it can be called from any language and embedded in any framework. Python, Lua, Swift, JavaScript, Rust — anything that can call C can use Omega.

**Protocol-agnostic with MIDI as the reference implementation.** The core engine operates on abstract events. MIDI is the first and primary concrete implementation — it shapes the abstraction layer but doesn't own it. The design is validated when a second protocol (OSC, CV, DMX) can be added by implementing an interface, not by patching the core.

**Pluggable at every seam.** Clock sources, output sinks, event payload types — all defined as interfaces with concrete implementations. New protocols and sync sources are first-class extensions, not afterthoughts.

**Embeddable.** No global state, no required runtime, no framework dependencies. Drop it into a plugin, a hardware device, a game, an installation. The library has no opinion about your build system or OS.

**Time-correct playback.** The engine uses a catch-up model: each frame, advance to the current time and fire all pending events in sequence. Late frames catch up rather than drop events. This is the key insight that makes tight MIDI timing possible without a dedicated real-time thread.

---

## Architecture

```
┌──────────────────────────────────────────┐
│            Your Application              │
│   (desktop UI, web, embedded, plugin)    │
├──────────────────────────────────────────┤
│           C API  —  omega.h              │
│         stable ABI, extern "C"           │
├──────────────────────────────────────────┤
│              C++ Core                    │
│  Engine · Timeline · Pattern · Perform   │
│  Transport · Clock · Event model         │
├────────────────┬─────────────────────────┤
│  MIDI (RtMidi) │  OSC · CV · custom ...  │
└────────────────┴─────────────────────────┘
```

The C++ core is where all design lives. The C API is a thin, stable projection of it. The protocol layer is pluggable concrete implementations of core interfaces.

---

## Core Abstractions

### Event

The fundamental unit of sequencing. Every event has:

- **Time** — absolute position in engine ticks (tick resolution TBD, see Open Questions)
- **Destination** — an `OutputSink` to deliver to
- **Payload** — a typed value: MIDI message, OSC message, parameter change, or custom type

The engine schedules and fires events. It never inspects the payload — only the time and destination matter to the core.

### OutputSink

Where events go. Pure virtual interface:

```cpp
class OutputSink {
public:
    virtual void send(const Event& e) = 0;
    virtual ~OutputSink() = default;
};
```

Concrete implementations: `MidiOutputSink`, `OscOutputSink`, `ParameterSink`, and anything else that needs time-tagged delivery. The sequencer holds a pointer to `OutputSink` and never knows which implementation it has.

This is the primary pluggability seam. Adding a new output protocol means writing one class.

### ClockSource

Where time comes from. Abstract interface:

```cpp
class ClockSource {
public:
    virtual Ticks now() = 0;
    virtual ~ClockSource() = default;
};
```

Concrete implementations:
- **InternalClock** — BPM-driven, uses `std::chrono` as the underlying counter
- **MidiClockSource** — slaved to incoming MIDI clock
- **LinkClockSource** — Ableton Link for networked sync
- **HostClockSource** — for plugin use, driven by the host DAW transport

Clock is injected into the engine at construction. Swapping sync source is a runtime operation.

### Transport

Play, stop, record, locate. A state machine that accepts input from hardware controls, MIDI messages, or API calls — all going through the same path. No special-casing for any input source.

States: `Stopped`, `Playing`, `Recording`, `Paused`

Transport emits events (start, stop, locate, tempo change) that the engine and any listeners can respond to.

### EventPayload

Payloads are typed via `std::variant`. Built-in types:

```cpp
using EventPayload = std::variant<
    MidiMessage,
    OscMessage,
    ParameterChange
>;
```

Custom payload types can be registered at runtime (mechanism TBD — see Open Questions). The engine stores and fires them; the output sink interprets them. Type safety is maintained; the core never casts blindly.

---

## Sequencing Modes

Three first-class modes built on the same engine with different arrangement and performance semantics. This is the core of what makes Omega distinctive.

### Timeline

Linear multi-track recording. The classic model — absolute time positions, multiple tracks, each with a name, output sink assignment, MIDI channel, and mute/solo state.

- Up to N tracks (N configurable at engine init, not hardcoded)
- Per-track: name, sink, channel, port, mute, solo
- Absolute time event positions
- Suitable for composition and final arrangement

### Pattern

Named, loopable sequences. Patterns have a defined length and loop independently. Can be chained into a linear song arrangement or launched freely.

- Patterns are first-class objects: named, stored by name, referenced by name
- Pattern length is independent per pattern
- Patterns can be nested (a pattern that references other patterns)
- Song mode: patterns arranged on a timeline with repeat counts
- Patterns are reusable across Timeline and Performance modes

### Performance

Live cuing and real-time control. The least common mode in current software and the most important design goal of Omega.

Patterns are assigned to slots. Slots are launched by any control input — API call, MIDI note, keypress, hardware button. The engine decides when to switch (immediately, or at next loop boundary).

Per-slot real-time controls:
- **Transpose** — semitone offset, MIDI-controllable
- **Velocity scale** — proportional scaling, MIDI-controllable
- **Random bias** — probability of note pitch variation, configurable range, MIDI-controllable
- **Loop length override** — stretch or compress independently of pattern length
- **Mute** — silence without stopping

All per-slot parameters are MIDI-controllable in real time. Tempo and global bias are also MIDI-controllable.

Performance mode has no equivalent in current open source software. It is where Omega earns its reason to exist.

---

## MIDI Implementation

MIDI is the reference implementation that proves the abstraction layer is right.

### I/O

Built on **RtMidi** for cross-platform port enumeration and byte I/O. Wrapped in `MidiOutputSink` and a MIDI input handler that feeds recorded events and sync signals into the engine.

### Event Coverage

Full MIDI 1.0: note on/off, control change, program change, pitch bend, channel aftertouch, polyphonic aftertouch, system exclusive, real-time (clock, start, stop, continue, song position pointer).

Running status on output for efficiency.

### Multi-port

Multiple output ports are first-class. Each track or pattern slot can target a different port and channel. Port count is not hardcoded.

### Note-off Tracking

Notes store their duration at record time. The engine fires note-off independently of the event stream — a separate duration countdown per active note. This means notes ring naturally across any number of events without requiring explicit note-off records in the sequence.

### Sync

- MIDI clock output: sends 24 PPQN clock, start, stop, song position pointer
- MIDI clock input: `MidiClockSource` slaves the engine to external clock
- Song position pointer: send on locate, receive to chase

### MIDI 2.0

Out of scope for v1. The abstraction layer should not preclude it.

---

## Beyond MIDI

The OutputSink interface is ready for:

- **OSC** — Open Sound Control to network destinations
- **CV/Gate** — Analog voltage control via appropriate hardware interface
- **DMX** — Lighting cue sequencing
- **Plugin parameters** — Direct automation without MIDI roundtrip
- **Custom** — Any time-tagged event delivery need

None of these require changes to the engine core.

---

## Recording

- Real-time capture from any MIDI input into any mode
- Quantization: on input (live) or post-capture
- Overdub: add events to existing content without erasing
- Step recording: advance manually, insert events at current position

---

## Edit Operations

Non-destructive where possible. The full operational palette:

- Insert, delete, copy, paste (insert / replace / merge / fill modes)
- Quantize — time and/or duration, with strength (partial quantize)
- Transpose — by interval, invert, set absolute
- Velocity — scale, set, linear ramp, invert, clip
- Duration — scale, set, humanize
- Time reverse
- Split — by note range, channel, event type, or measure boundary
- Merge — combine sequences or tracks
- Humanize — controlled randomization of timing, velocity, duration

---

## File Formats

- **Native format** (TBD): complete session state, all modes, all parameters
- **Standard MIDI file**: import (type 0 and type 1) and export
- **Pattern export**: single pattern as SMF type 0

---

## Language and Dependencies

| Component | Choice | Reason |
|---|---|---|
| Core library | C++17 | Dave knows it, virtual dispatch for plugins, `std::variant` for payloads |
| C API surface | `extern "C"` | Universal bindability |
| MIDI I/O | RtMidi | Cross-platform, minimal, proven |
| Network sync | Ableton Link (optional) | Best-in-class networked tempo sync, open source |
| Build system | CMake | Universal, RtMidi already uses it |
| Testing | Catch2 | Header-optional, widely used |

No UI framework. No audio engine. No framework dependencies in the public API.

---

## Open Questions

These need answers before or during v1 implementation:

1. **Tick resolution** — What is one tick? Options: 96, 480, or 960 PPQN. Higher resolution = smoother timing, larger numbers. 480 is SMF standard and a reasonable default.

2. **Custom payload extension** — How are third-party payload types registered at runtime? Options: type-erased wrapper, plugin registration table, or limiting `std::variant` to a known closed set with a generic escape hatch.

3. **Thread model** — Does the engine run on its own thread, or is it driven by the caller? Caller-driven is simpler and more embeddable. A dedicated thread gives better timing but adds complexity. Consider: caller-driven by default, optional dedicated thread helper.

4. **Persistence format** — Binary (compact, fast) or text (human-readable, diffable)? Or both? SQLite is an interesting third option.

5. **Pattern nesting depth** — Patterns that reference patterns: how deep, and how are cycles handled?

6. **MIDI 2.0** — At what point does it become relevant? Don't design against it but don't require it.

7. **C API completeness** — Does the C API expose 100% of the C++ surface, or is it a curated subset? A curated subset is safer for ABI stability.

---

## Naming Conventions (working)

| Concept | C++ Name | C API Name |
|---|---|---|
| Library namespace | `omega::` | `omega_` prefix |
| Engine instance | `Engine` | `omega_engine_t` |
| Output sink | `OutputSink` | `omega_sink_t` |
| Clock source | `ClockSource` | `omega_clock_t` |
| Event | `Event` | `omega_event_t` |
| Transport | `Transport` | `omega_transport_t` |
| Timeline | `Timeline` | `omega_timeline_t` |
| Pattern | `Pattern` | `omega_pattern_t` |
| Performance | `Performance` | `omega_perf_t` |

---

## Implementation Order

1. `Event`, `OutputSink`, `ClockSource`, `Transport` — interfaces only, no implementations
2. `InternalClock` — BPM engine using `std::chrono`
3. `MidiOutputSink` via RtMidi — proves the OutputSink interface
4. Timeline playback engine — the time-correcting loop, the core of everything
5. Pattern mode on top of the same engine
6. Performance mode — slots, cuing, real-time controls
7. Recording — MIDI capture into all three modes
8. Edit operations
9. C API (`omega.h`)
10. File I/O — native format, SMF import/export
11. Additional sinks — OSC, others as needed
12. Ableton Link integration
