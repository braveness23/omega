# Changelog

All notable changes to Omega will be documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Omega uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- **M2 — Single-track playback via C API** (sprints 2.1–2.4):
  - `ClockSource` abstraction + `MockClock` test utility (sprint 2.1)
  - `OutputSink` interface + `CapturingSink` test utility (sprint 2.2)
  - `TimelineSource` + `TrackData` — linear multi-track playback, note/CC chasing on locate (sprint 2.3)
  - C API wiring: `omega_engine_add_track`, `omega_engine_add_event`, `omega_engine_play/stop/locate`, `omega_engine_process`; C API integration tests (sprint 2.4)
- **M3 — Pattern library, song arrangement, and performance source** (sprints 3.1–3.3):
  - `PatternLibrary` — named, length-bounded event sequences stored in PMR vectors; C API `omega_pattern_create/destroy/add_event/set_length` (sprint 3.1)
  - `SongArrangementSource` — chains patterns with repeat counts; window-based tick dispatch; `on_locate()` for mid-arrangement resume; note-off tracking; C API `omega_song_append/clear` (sprint 3.2)
  - `PerformanceSource` — 64-slot state machine (EMPTY→IDLE→QUEUED→PLAYING→STOPPING); per-slot transpose (±24 semitones), velocity scale (0–200%), random bias (0–100%); phase-resume on locate; C API `omega_perf_assign/cue/stop/stop_all/set_transpose/set_velocity_scale/set_random_bias` (sprint 3.3)

### Fixed
- Reduced `SpscQueue` default capacity from 65536 to 4096 to avoid stack overflow on Windows (default stack is 1 MB; 65536-slot queue consumed ~4 MB)

### Added
- **M1 — Core engine** (sprints 1.1–1.5): first real implementation code:
  - `include/omega/omega.h`: `omega_event_t` (24-byte Event struct), `OMEGA_PPQN` constant, payload-tag constants (`OMEGA_NOTE_ON` etc.), `omega_make_note_on()`, `omega_make_cc()`, `omega_make_program()` helpers
  - `include/omega/types.h`: C++ types — `omega::Event` alias, `PayloadTag`, `CueMode`, `TransportAction`, `TransportState` enums, `TrackId`/`SlotId`/`PatternId` typedefs
  - `include/omega/detail/spsc_queue.h`: lock-free SPSC ring buffer (`omega::detail::SpscQueue<T,Capacity>`), header-only, cache-line-separated atomics, power-of-two static_assert
  - `include/omega/commands.h`: all six `Command` variant types; `static_assert(sizeof(Command) <= 64)`
  - `include/omega/tempo_map.h` + `src/tempo_map.cpp`: integer-only tick↔ns conversion, precomputed `ns_at_tick`, multi-point insert with recomputation of subsequent points
  - `include/omega/engine.h` + `src/engine.cpp`: `Engine` skeleton — SPSC queue drain loop in `process()`, atomic `TransportState` and position, `SetTempoCmd`/`TransportCmd` handling
  - `omega_core` converted from `INTERFACE` to `STATIC` library
  - 36 unit tests covering all five sprints (ASan + UBSan clean)

- **Library foundation** (design 12): non-implementation infrastructure required before first code commit:
  - `CMakePresets.json` with `dev` (ASan/UBSan), `tsan` (TSan), `release`, and CI presets
  - `OMEGA_WITH_TSAN` CMake option; TSan and ASan+UBSan are now mutually exclusive with a hard error
  - `OMEGA_BUILD_BENCHMARKS` CMake option; benchmark scaffold in `tests/benchmarks/`
  - `OMEGA_SOVERSION` variable (starts at 0); version and SOVERSION wired to install targets
  - `include/omega/export.h` with `OMEGA_API` visibility macro and `OMEGA_DEPRECATED(msg)` macro
  - `include/omega/omega.h` stub with ABI stability guarantee, thread-safety annotation conventions, error contract conventions, and ownership conventions — ready for function declarations
  - `cmake/OmegaConfig.cmake.in` updated with `find_dependency(libremidi)` for installed consumers
  - `cmake/omega.pc.in` pkg-config template; installed to `lib/pkgconfig/` on `cmake --install`
  - `cmake/smoke_test/` minimal consumer project; validated by new CI install smoke test job
  - `THIRD_PARTY_LICENSES.md` inventorying all dependencies (libremidi, midifile, Catch2, Ableton Link)
  - `.editorconfig` enforcing UTF-8, LF endings, consistent indent before clang-format runs
  - `.pre-commit-config.yaml` for local pre-commit hooks (end-of-file, trailing whitespace, clang-format, clang-tidy)
  - `.clang-tidy` baseline config checked in; treated as code
  - `abi/PLACEHOLDER` stub for the ABI baseline dump to be generated at v0.1.0
  - `docs/release-checklist.md` full release checklist
  - `docs/migration/v0-to-v1.md` migration guide template
  - CI jobs added: TSan, clang-tidy (PR-only), CHANGELOG lint (PR-only), ABI compliance stub (PR-only), install smoke test, coverage (main-only), benchmarks (main-only)
  - All `FetchContent` dependencies pinned to specific tags or commit hashes (libremidi v5.1.0, midifile `98917df`, Catch2 v3.5.2, Ableton Link Link-3.1.5)
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
