# Architecture Overview

Omega is organized in three layers: a C++ core, a stable C API, and optional platform integrations.

```
┌──────────────────────────────────────────────────────────────┐
│                      Your Application                        │
│             (any language, any UI framework)                 │
├──────────────────────────────────────────────────────────────┤
│                    C API  —  omega.h                         │
│          Stable ABI. Bindable from any language.             │
├──────────────────────────────────────────────────────────────┤
│                        C++ Core                              │
│                                                              │
│  ┌─────────┐   ┌──────────────────────────────────────────┐ │
│  │ Engine  │   │              Session Data                │ │
│  │process()│   │  PatternLibrary  · TempoMap              │ │
│  └────┬────┘   │  TimeSignatureMap · SmpteConfig          │ │
│       │        │  SinkRegistry    · EventSourceRegistry   │ │
│       │        │  EventInputRegistry                      │ │
│       │        │  ModulationBus   · PerformanceContext    │ │
│       │        └──────────────────────────────────────────┘ │
│       │                                                      │
│  ┌────▼────────────┐  ┌────────────┐  ┌─────────────────┐   │
│  │  EventSource    │  │ OutputSink │  │  ClockSource    │   │
│  │  (abstract)     │  │ (abstract) │  │  (abstract)     │   │
│  └──────┬──────────┘  └────────────┘  └─────────────────┘   │
│         │              ┌────────────┐  ┌─────────────────┐   │
│  Timeline · Pattern    │ EventInput │  │   SpscQueue     │   │
│  Performance · custom  │ (abstract) │  │   (internal)    │   │
│  TransformSource       └────────────┘  └─────────────────┘   │
│                              │                               │
│                       InputBus (per cycle)                   │
├──────────────────────────────────────────────────────────────┤
│   MIDI (libremidi)  ·  OSC · CV · custom sinks/inputs        │
│   SMF (midifile)    ·  Link (optional, GPL)                  │
└──────────────────────────────────────────────────────────────┘
```

## Key Decisions at a Glance

| Decision | Choice |
|---|---|
| Tick resolution | 480 PPQN |
| Time unit | nanoseconds (uint64_t) |
| Thread model | Caller-driven + SPSC queue |
| Memory | std::pmr (swappable allocator) |
| Event storage | Sorted vector per track |
| Event size | 24 bytes |
| Public API | C (extern "C") |
| Error handling | Error codes (no exceptions in API) |
| Playback modes | Pluggable EventSource abstraction |
| Built-in modes | Timeline · Pattern · Performance (all simultaneous) |
| Event input | Pluggable EventInput abstraction |
| Source routing | TransformSource composition (no graph registry) |
| Param modulation | ModulationBus (float channels, updated each cycle) |
| Shared context | PerformanceContext (scale, chord, groove, chaos) |
| Time signature | TimeSignatureMap (empty = freeform) |
| Bar/beat navigation | MeterCursor (non-realtime) |
| SMPTE timecode | SmpteConverter + SmpteConfig (optional) |
| Coordinate interface | PositionConverter base class |
| MIDI I/O | libremidi (MIT) |
| SMF parsing | midifile/Stanford (BSD) |
| Markers/regions | MarkerList + RegionList (session-level) |
| Anchors | AnchorPoint + EventAnchorTable (sparse side table) |
| Snap | snap_to_nearest() with grid/marker/region/anchor targets |
| Timer thread | OmegaTimer (RAII, drift-compensating) |
| Native format | JSON (v1) |
| Undo/redo | Command pattern |
| Core license | MIT |
| Link (optional) | GPL v2+ when enabled |
