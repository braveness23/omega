# Changelog

All notable changes to Omega will be documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Omega uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- Initial design proposal covering architecture, event model, and three sequencing modes
- Design documents: timing model, thread model, memory/storage, C API, pattern state machine, session container, extensions, testing strategy, prior art
- Project infrastructure: license, contributing guide, CI, issue templates
- **EventSource abstraction** (design 10): pluggable playback modes via `EventSource` interface; `TransformSource` base class for composition-based routing; `on_locate()` with `chase_out` dispatcher for note/CC/program chasing; updated `EventSourceRegistry` signatures; `MockEventSource` test utility; extended extension point catalogue (step sequencer, generative, polyrhythmic, reactive)
- **Orchestration layer** (design 11): four complementary additions that together enable virtually any sequencer architecture to be expressed:
  - `EventInput` / `InputBus` — first-class abstraction for incoming events (MIDI, OSC, CV); polled each cycle before `advance()` calls; events broadcast to all sources via `ProcessContext`
  - `TransformSource` — composition-based source routing; provided transforms: `ScaleQuantizerSource`, `TransposeSource`, `VelocityCurveSource`, `HumanizerSource`, `ChordSpreadSource`, `FilterSource`, `DelaySource`
  - `ModulationBus` — 256 named `float` channels; written by modulator sources (`LfoSource`, `EnvelopeSource`, `StepModSource`, `ExpressionSource`), readable by any source or sink each cycle
  - `PerformanceContext` — shared musical state (scale, chord, groove, chaos, global transpose/velocity, deterministic random seed); `GrooveLibrary` with 6 pre-loaded templates
  - `ProcessContext` struct threading all four additions through every `advance()` and `on_locate()` call with no allocation
  - Note and parameter chasing via updated `on_locate(tick, chase_out, ctx)` signature
  - New C API sections: `omega_input_*`, `omega_mod_*`, `omega_ctx_*`, `omega_process_ctx_t`
  - New session JSON keys: `modulation_bus`, `performance_context`, `groove_library`
  - `MockEventInput` test utility

---

<!-- New releases go above this line in the format:
## [0.2.0] - YYYY-MM-DD
### Added
### Changed
### Deprecated
### Removed
### Fixed
### Security
-->

[Unreleased]: https://github.com/braveness23/omega/compare/HEAD...HEAD
