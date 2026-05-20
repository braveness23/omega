# Changelog

All notable changes to Omega will be documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Omega uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
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
