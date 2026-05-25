# Architecture Diagram

Omega is organized in three layers.

```mermaid
block-beta
  columns 1
  app["Your Application\n(any language, any UI framework)"]
  api["C API — omega.h\nStable ABI · bindable from any language"]
  block:core["C++ Core (omega::core)"]
    columns 2
    engine["Engine\nprocess()"]
    session["Session Data\nPatternLibrary · TempoMap\nTimeSignatureMap · SinkRegistry\nModulationBus · PerformanceContext\nMarkerList · RegionList"]
    sources["EventSource (abstract)\n──────────────────\nTimelineSource\nSongArrangementSource\nPerformanceSource\nTransformSource\ncustom sources"]
    io["OutputSink (abstract)\nLibremidiSink · CapturingSink\n\nClockSource (abstract)\nInternalClock · MockClock\n\nEventInput (abstract)\nLibremidiInput · MockEventInput"]
  end
  platform["Platform Integrations\nlibremidi (MIDI I/O) · midifile (SMF) · Ableton Link (optional, GPL)"]

  app --> api
  api --> core
  core --> platform
```

## Layer Responsibilities

| Layer | Stability | Purpose |
|---|---|---|
| **C API** (`omega.h`) | ABI-stable within MAJOR | Single cross-language boundary; all public handles are opaque pointers |
| **C++ Core** (`omega::core`) | No ABI guarantee | Engine, session data, timing, event dispatch — evolves freely |
| **Platform integrations** | Versioned separately | Real MIDI I/O, SMF file I/O, optional sync |
