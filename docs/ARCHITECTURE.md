# Architecture Overview

Omega is organized in three layers: a C++ core, a stable C API, and optional platform integrations.

```
┌──────────────────────────────────────────────────────┐
│                   Your Application                   │
│          (any language, any UI framework)            │
├──────────────────────────────────────────────────────┤
│                   C API  —  omega.h                  │
│         Stable ABI. Bindable from any language.      │
├──────────────────────────────────────────────────────┤
│                     C++ Core                         │
│                                                      │
│  ┌─────────┐  ┌─────────┐  ┌───────────────────┐    │
│  │ Engine  │  │ Session │  │  Command History  │    │
│  └────┬────┘  └────┬────┘  └───────────────────┘    │
│       │            │                                 │
│  ┌────▼────────────▼────────────────────────────┐   │
│  │              Session Data                    │   │
│  │  PatternLibrary · TempoMap · SinkRegistry    │   │
│  │  EventSourceRegistry                         │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  ┌─────────────┐  ┌────────────┐  ┌─────────────┐   │
│  │ EventSource │  │ OutputSink │  │ ClockSource │   │
│  │  (abstract) │  │ (abstract) │  │ (abstract)  │   │
│  └──────┬──────┘  └─────┬──────┘  └─────────────┘   │
│         │               │          ┌─────────────┐   │
│  Timeline · Pattern     │          │  SpscQueue  │   │
│  Performance · custom   │          │  (internal) │   │
│                         │          └─────────────┘   │
├─────────────────────────┴────────────────────────────┤
│   MIDI (libremidi)  ·  OSC · CV · custom sinks ...   │
│   SMF (midifile)    ·  Link (optional, GPL)          │
└──────────────────────────────────────────────────────┘
```

## Design Documents

Start here if you're new to the codebase:

1. [Timing Model](design/01-timing-model.md) — ticks, BPM, tempo map, integer arithmetic
2. [Thread Model](design/02-thread-model.md) — caller-driven engine, lock-free command queue
3. [Memory and Storage](design/03-memory-storage.md) — PMR allocators, sorted event vectors, blob store
4. [C API Design](design/04-c-api-design.md) — opaque handles, ownership, `omega.h` sketch
5. [Pattern State Machine](design/05-pattern-state-machine.md) — slot states, cuing, loop boundaries
6. [Session Container](design/06-session-container.md) — what owns what, mode switching, persistence
7. [Extensions](design/07-extensions.md) — sinks, clocks, custom payloads, Ableton Link
8. [Testing Strategy](design/08-testing-strategy.md) — MockClock, CapturingSink, CI
9. [Prior Art](design/09-prior-art.md) — libraries studied, what was borrowed, license compatibility
10. [EventSource Abstraction](design/10-event-source-abstraction.md) — pluggable playback modes via EventSource; new mode catalogue

## Key Decisions at a Glance

| Decision | Choice | Document |
|---|---|---|
| Tick resolution | 480 PPQN | [01](design/01-timing-model.md) |
| Time unit | nanoseconds (uint64_t) | [01](design/01-timing-model.md) |
| Thread model | Caller-driven + SPSC queue | [02](design/02-thread-model.md) |
| Memory | std::pmr (swappable allocator) | [03](design/03-memory-storage.md) |
| Event storage | Sorted vector per track | [03](design/03-memory-storage.md) |
| Event size | 24 bytes | [03](design/03-memory-storage.md) |
| Public API | C (extern "C") | [04](design/04-c-api-design.md) |
| Error handling | Error codes (no exceptions in API) | [04](design/04-c-api-design.md) |
| Playback modes | Pluggable EventSource abstraction | [10](design/10-event-source-abstraction.md) |
| Built-in modes | Timeline · Pattern · Performance (all simultaneous) | [06](design/06-session-container.md) |
| MIDI I/O | libremidi (MIT) | [09](design/09-prior-art.md) |
| SMF parsing | midifile/Stanford (BSD) | [09](design/09-prior-art.md) |
| Native format | JSON (v1) | [06](design/06-session-container.md) |
| Undo/redo | Command pattern | [07](design/07-extensions.md) |
| Core license | MIT | LICENSE |
| Link (optional) | GPL v2+ when enabled | [07](design/07-extensions.md) |
