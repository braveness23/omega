# Changelog

All notable changes to Omega will be documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Omega uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- **Event field accessors** (`omega.h`): symmetric read/write accessors for the `omega_event_t` payload fields — `omega_event_note_pitch()`, `omega_event_note_velocity()`, `omega_event_note_duration()`, `omega_event_set_pitch()`, `omega_event_set_velocity()`, `omega_event_set_duration()`, `omega_event_cc_number()`, `omega_event_cc_value()` — eliminates the need to know the private `data[]` byte layout. 10 new unit tests. Closes #29.
- **Pattern read API** (`omega.h`): `omega_pattern_event_count()`, `omega_pattern_event_at()`, `omega_pattern_event_count_filtered()`, `omega_pattern_length()` — allows C callers to inspect pattern contents without accessing the C++ layer. 12 new integration tests. Closes #28.
- **`omega_engine_position_tick()`** (`omega.h`): returns the current transport position in ticks, converting via the TempoMap (handles tempo automation correctly; no floating-point). 2 new unit tests, 3 new integration tests. Closes #26.
- **Tempo map C API** (`omega.h`): `omega_tempo_set(e, tick, bpm_milli)` inserts or replaces a tempo point; `omega_tempo_remove(e, tick)` removes a tempo automation point (no-op at tick 0); `omega_tempo_at(e, tick, &bpm_milli_out)` queries the BPM in effect at any tick. `TempoMap::remove()` added to support tick-based removal. 14 new integration tests in `tests/integration/test_c_api_tempo.cpp`. Closes #25.
- **`omega_sink_id(sink)`** (`omega.h`): returns the unique `sink_id` for any `omega_sink_t*` handle; 0 on NULL. Removes the need to cast to C++ and call `OutputSink::sink_id()`. Closes #25.
- **transport loop region** (`SetLoopCmd` / `omega_loop_set` / `omega_loop_clear` / `omega_loop_enable`): when playing, the engine automatically locates back to `start_tick` whenever `to_tick >= end_tick`; `on_locate()` is called on all built-in sources at the wrap point to send note-offs and reset playback cursors; C++ API: `Engine::loop_set(start, end)`, `Engine::loop_clear()`, `Engine::loop_enable(bool)`, `Engine::loop_region()`; C API: `omega_loop_set()`, `omega_loop_clear()`, `omega_loop_enable()`; 8 new unit tests in `tests/unit/test_loop.cpp`; closes #24
- **tapedeck example**: 8-track, 8-bar looping MIDI sequencer CLI (`examples/tapedeck/`) using `omega::Engine`, `LibremidiSink`, `OmegaTimer`, and `PerformanceSource`; SPACE play/stop, ESC rewind, Ctrl+C exit; per-track and global MIDI activity indicators; clock time display; `ActivitySink` wrapper for real-time note-on tracking; `OMEGA_BUILD_EXAMPLES=ON` enables the build

---

## [1.0.0] — 2026-05-25

### Added
- **Sprint 7.3 -- stable release**: `OMEGA_VERSION_MAJOR/MINOR/PATCH` compile-time macros in `omega.h`; `omega_version()` now returns `{1,0,0}` via the macros; `CMakeLists.txt` `VERSION` bumped to `1.0.0`, `OMEGA_SOVERSION` bumped to `1`; `abi/v1.0.0.dump` committed as the v1.0.0 ABI baseline
- **Sprint 7.2 -- getting started guide**: `docs/GETTING_STARTED.md` covering CMake FetchContent install, two working examples (MockClock/CapturingSink and LibremidiSink/OmegaTimer), core concepts, thread model, and six extension points
- **Sprint 7.2 -- diagrams**: four Mermaid diagrams in `docs/diagrams/` — architecture layers, `engine.process()` flowchart, PerformanceSource state machine, and class relationships; linked from `docs/ARCHITECTURE.md`
- **Sprint 7.2 -- icons**: three SVG icon variants in `assets/` — omega symbol, music+code hybrid, and geometric three-arc design
- **Sprint 7.1 -- comment remediation**: doc comments added to all 30 `commands.h` structs; inline algorithmic comments added to `performance_source.cpp` (LCG RNG, state machine), `smpte_converter.cpp` (drop-frame math), `timeline.cpp` (duration encoding, active-notes table), `smf_import.cpp` (tick scaling, MIDI meta types), `timer.cpp` (ceiling sleep), and `snap.cpp` (grid paths, anchor offset algorithm)

### Changed
- **Sprint 7.3 -- CHANGELOG**: `[Unreleased]` promoted to `[1.0.0] — 2026-05-25`
- **Sprint 7.2 -- migration guide**: `docs/migration/v0-to-v1.md` filled in — ABI stability contract, no breaking changes from v0.6.0-beta, feature-by-version table, known limitations
- **Sprint 7.2 -- README**: added icon, version badge, Getting Started / API / Architecture quick links, v1.0.0 stable-release callout
- **Sprint 7.2 -- archive**: `docs/IMPLEMENTATION_PLAN.md`, `docs/ROADMAP.md`, and dev-phase `docs/STATUS.md` moved to `docs/archive/`; `docs/STATUS.md` replaced with stable-release stub

### Added
- **Sprint 6.4 -- benchmarks**: four Catch2 `BENCHMARK` cases measuring `engine.process()` idle cycle, SPSC `push()`/`pop()` throughput, `TempoMap::ns_to_ticks()` with a 16-point map, and dispatch of 1000 events in a single cycle; benchmark target (`omega_benchmarks`) excluded from default `ctest` run, invoked explicitly via `ctest -R bench_`
- **Sprint 6.3 -- ABI baseline**: `abi/v0.6.0.dump` generated from `libomega_core` `RelWithDebInfo` objects via `abi-dumper`; `abi/PLACEHOLDER` removed; baseline committed to track future ABI compatibility with `abi-compliance-checker`

### Changed
- **Sprint 6.5 -- docs polish**: `README.md` quick-start example verified compilable; `docs/ARCHITECTURE.md`, `docs/STATUS.md`, and `docs/ROADMAP.md` updated to reflect M5/M5.5/M6 completion
- **Sprint 6.2 -- sanitizer verification**: confirmed zero ASan/UBSan and TSan failures across all 388 tests on the M6 codebase
- **Sprint 6.1 -- coverage to 80%**: added integration tests covering the full C API surface for SMF import/export, markers, regions, pattern anchors, event anchors, timer, and snap; two new integration test files (`test_c_api_smf.cpp`, `test_c_api_markers.cpp`) with 27 new test cases; line coverage raised from 78% to 83%

### Added
- **Sprint 5.5.2 -- snap framework**: `snap_to_nearest()` with `SnapConfig{targets, grid_subdiv_ticks, tolerance_ticks}` and `SnapResult{snapped_tick, source, did_snap}`; target flags `SNAP_GRID`, `SNAP_MARKERS`, `SNAP_REGIONS`, `SNAP_ANCHORS`; GRID target supports explicit subdivision (integer math) or `PositionConverter`-based meter/SMPTE grid; ANCHORS target applies the anchor-aware formula (`next_grid(tick + anchor.offset) - anchor.offset`); C API `omega_snap()` with `omega_snap_config_t` / `omega_snap_result_t`
- **Sprint 5.5.1 -- anchors + event side table**: `AnchorPoint{name, offset_ticks, flags}` with
  `OMEGA_ANCHOR_SNAP`, `OMEGA_ANCHOR_WARP`, `OMEGA_ANCHOR_CUE` flag constants; `AnchorList`
  sorted by `offset_ticks` with `add/remove/at/find_by_name/snap_anchors/set_active_snap/
  active_snap`; `EventAnchorTable` sparse side table keyed by `(container_id, event_index)` so
  events remain 24 bytes; `Pattern::anchors` for per-pattern intrinsic anchors; `Engine::
  event_anchors()` accessor for the session-level side table; full C API
  (`omega_pattern_add_anchor`, `omega_pattern_remove_anchor`, `omega_pattern_anchor_count`,
  `omega_pattern_set_active_snap`, `omega_event_add_anchor`, `omega_event_remove_anchor`)

<!-- New releases go above this line -->

---

## [0.5.0-alpha] - 2026-05-24

### Added
- **Sprint 5.4 -- OmegaTimer**: `OmegaTimer` RAII thread wrapper drives `Engine::process()` at
  a configurable interval (default 1 ms); drift-compensating sleep loop using `std::chrono::steady_clock`
  with `nanosleep` on POSIX and `timeBeginPeriod(1)` + `Sleep` on Windows; `~OmegaTimer()` signals
  stop, joins the thread, then calls `process()` one final time; 5 tests covering clean
  construction/destruction, position advancement, TSan-validated concurrent `enqueue()` from a
  mutation thread, and custom interval parameter
- **Sprint 5.3 -- SMF export**: `omega_smf_export()` writes Standard MIDI Files (Type 0 or
  Type 1) from the engine's active session; exports tempo map, time signatures, markers, and
  all track events (note on/off with duration, CC, program change); empty tracks are
  automatically skipped so the midifile library never encounters a zero-event track list;
  7 round-trip tests covering null/bad-path errors, Type 0 single-track, Type 1 two-track
  import→export→reimport, tempo changes, markers, and time signatures
- **Sprint 5.2 -- SMF import + markers + regions**: `omega_smf_import()` reads Standard MIDI
  Files (Type 0/1, tempo, time signatures, markers, cue points, non-480 PPQN tick scaling);
  `MarkerList` and `RegionList` as Session-level data with sorted insertion and find helpers;
  full C API for marker/region CRUD (`omega_marker_add/remove/count/at/clear`,
  `omega_region_add/remove/count/at/clear`); `OMEGA_REGION_LOOP/PUNCH/SECTION` constants
- **Sprint 5.1 -- libremidi MIDI I/O**: `LibremidiSink` wrapping `libremidi::midi_out` and `LibremidiInput`
  wrapping `libremidi::midi_in`; `event_to_midi_bytes()` translation helper; C API
  `omega_sink_create_midi_out()` / `omega_sink_destroy_midi_out()` and
  `omega_input_create_midi_in()` / `omega_input_destroy_midi_in()`; `OMEGA_ERR_IO = -8` status
  code for I/O failures; `OMEGA_PITCH_BEND`, `OMEGA_AFTERTOUCH`, `OMEGA_POLY_AT` C API constants
  with full round-trip serialization/deserialization support

---

## [0.4.0] - 2026-05-23

### Added
- **M4.5 — TimeSignatureMap, SmpteConverter, OMEGA_CUE_BAR** (sprints 4.5.1–4.5.3):
  - `TimeSignatureMap` — sorted list of `TimeSigPoint{tick, numerator, denominator}`; empty = freeform mode; `insert()` validates denominator (power-of-two 1–32) and replaces any existing point at the same tick; `remove()`; `at()` returns the governing time signature at any tick (sprint 4.5.1)
  - `MeterCursor` (implements `PositionConverter`) — `tick_to_beat_pos()` / `beat_pos_to_tick()` with bar counting; `next_bar_tick()` / `next_beat_tick()`; `quantize_to_beat()` / `quantize_to_subdivision()` (round-half-up); non-realtime only (sprint 4.5.1)
  - `PositionConverter` abstract base — `next_boundary()` and `quantize()` virtual interface used by `MeterCursor` and `SmpteConverter` (sprint 4.5.1)
  - `SmpteConfig` — `{fps, drop_frame, is_2997}`; `is_valid_smpte_config()` validates fps ∈ {24,25,30}, is_2997 requires fps=30, drop_frame requires is_2997 (sprint 4.5.2)
  - `SmpteConverter` (implements `PositionConverter`) — tick↔HH:MM:SS:FF via `ns_to_frame` (floor) / `frame_to_ns` (ceiling); NDF for 24/25/30fps, drop-frame NTSC 29.97 (17982 frames per 10-min group); `next_boundary()`, `quantize()` (round-half-up); non-realtime only (sprint 4.5.2)
  - `OMEGA_CUE_BAR = 2` — new cue mode; `PerformanceSource` waits for the next musical bar boundary (uses `TimeSignatureMap::at()` on timing thread, safe); falls back to next-beat in freeform mode (sprint 4.5.3)
  - `OMEGA_ERR_NO_METER = -6`, `OMEGA_ERR_NO_SMPTE_CONFIG = -7` — new status codes (sprint 4.5.3)
  - C API: `omega_timesig_set/remove/clear/is_freeform/at`, `omega_tick_to_beat_pos`, `omega_beat_pos_to_tick`, `omega_next_bar_tick`, `omega_quantize_to_beat` (sprint 4.5.3)
  - C API: `omega_smpte_config_set/clear`, `omega_tick_to_smpte`, `omega_smpte_to_tick` (sprint 4.5.3)
  - Engine: `timesig_set/remove/clear`, `smpte_config_set/clear`, `tempo_map()` accessor; `SetTimeSigCmd`, `RemoveTimeSigCmd`, `ClearTimeSigCmd`, `SetSmpteConfigCmd`, `ClearSmpteConfigCmd` command variants (sprint 4.5.3)
- **M4.2 — ModulationBus** (sprint 4.2):
  - `ModulationBus` — 256-channel named float bus; `register_channel()`, `find()`, `get()`, `set()`, `snapshot()`; TSan-clean cross-thread access via `atomic<uint32_t>` bit-cast (sprint 4.2)
  - `ProcessContext.modulation_bus` — non-owning pointer to the engine's `ModulationBus`; set before every `advance()` call (sprint 4.2)
  - C API: `omega_mod_register`, `omega_mod_find`, `omega_mod_get`, `omega_mod_set`, `omega_mod_snapshot` (sprint 4.2)
- **M4.3 — PerformanceContext** (sprint 4.3):
  - `omega_perf_ctx_t` — shared musical state: scale (root + 12-bit bitmask), chord (root, type, 6 voices), global transpose (±127 semitones), global velocity (0–200), chaos (0–100), groove ID, swing, random seed (sprint 4.3)
  - `ProcessContext.perf_ctx` — copy of the engine's `PerformanceContext` snapshotted each cycle; readable by any source via `ctx.perf_ctx` (sprint 4.3)
  - `Engine::ctx_set_scale/chord/transpose/velocity/chaos/groove` — enqueue `SetCtx*` commands; applied atomically on next `process()` cycle (sprint 4.3)
  - `Engine::ctx_get` — reads last-committed `omega_perf_ctx_t`; must not be called concurrently with `process()` (sprint 4.3)
  - C API: `omega_ctx_set_scale`, `omega_ctx_set_chord`, `omega_ctx_set_transpose`, `omega_ctx_set_velocity`, `omega_ctx_set_chaos`, `omega_ctx_set_groove`, `omega_ctx_get` (sprint 4.3)
- **M4.4 — Custom EventSource + TransformSource** (sprint 4.4):
  - `EventSource::advance()` now receives `ProcessContext&` giving sources access to `InputBus`, `ModulationBus`, and `PerformanceContext` each cycle (sprint 4.4)
  - `Engine::add_source(source, priority)` / `Engine::remove_source(source)` — register custom sources via SPSC command queue; insertion is sorted by priority so MODULATOR(0) → CONTEXT(1) → PLAYBACK(2) always precedes built-in sources (sprint 4.4)
  - `TransformSource` — abstract base for composition-based event routing; wraps an upstream `EventSource`, captures its output in a 512-event stack buffer, and re-dispatches transformed events; `ChannelFilterSource` concrete implementation (sprint 4.4)
  - `MockEventSource` test utility — primes events at specific ticks; dispatches those with `tick <= to_tick` during `advance()`; part of the public test API in `include/omega/test/` (sprint 4.4)
  - C API: `omega_source_create/destroy`, `omega_engine_add_source`, `omega_engine_remove_source`, `omega_dispatch` (sprint 4.4)
- **M4.1 — EventInput + InputBus** (sprint 4.1):
  - `EventInput` abstract base — poll-based incoming event source (MIDI, OSC, CV); called each cycle before `EventSource::advance()` (sprint 4.1)
  - `InputDispatcher` — per-cycle helper passed to `EventInput::poll()`; delivers events into the `InputBus` (sprint 4.1)
  - `InputBus` — fixed-capacity (256 events) per-cycle event buffer; overflow increments a cumulative counter readable via `omega_input_overflow_count()` (sprint 4.1)
  - `MockEventInput` test utility — primes events to deliver on the next `poll()` call; part of the public test API in `include/omega/test/` (sprint 4.1)
  - `ProcessContext.input_bus` — pointer to the cycle's InputBus; set before every `advance()` call (sprint 4.1)
  - C API: `omega_input_create/destroy`, `omega_engine_add_input`, `omega_engine_remove_input`, `omega_input_overflow_count`, `omega_deliver` (sprint 4.1)
  - `AddInputCmd` / `RemoveInputCmd` — new `Command` variants; input list modified on timing thread via SPSC queue (sprint 4.1)

### Fixed
- clang-tidy: added `NOLINT` for `clang-analyzer-optin.performance.Padding` on `Engine` (field ordering constrained by initialization-order dependency)
- clang-tidy: value-initialize `InputBus::events_`, `ModulationBus::bits_to_float` local `f`, `float_to_bits` local `bits`, and `CaptureDispatcher::events` to suppress `cppcoreguidelines-init-variables` / `cppcoreguidelines-pro-type-member-init`
- clang-tidy: change `ModulationBus::channels_` and `CaptureDispatcher::events` from C-style arrays to `std::array<>` (`cppcoreguidelines-avoid-c-arrays`)
- clang-tidy: `TransformSource` Rule of 5 — add `= delete` for copy/move; remove redundant `override` on `final` override (`cppcoreguidelines-special-member-functions`, `modernize-use-override`)
- clang-tidy: remove redundant `inline` specifier from `constexpr` helpers in `time_signature_map.h` and `smpte_converter.h` (`readability-redundant-inline-specifier`)
- clang-tidy: name anonymous parameters and initialize uninit fields in test structs (`readability-named-parameter`, `cppcoreguidelines-pro-type-member-init`)
- clang-tidy: replace C-style float arrays with `std::array` in test files; use range-based for where applicable
- MSVC: initialize `bits_to_float` / `float_to_bits` locals before `memcpy` — fixes MSVC `/RTCu` crash in ModulationBus TSan test
- MSVC: replace em dash (`—`) with ASCII hyphen in `TEST_CASE` names — fixes CTest filter encoding failure on Windows (`test_modulation_bus`, `test_custom_source`, `test_smpte_converter`)
- clang-tidy: split comma-separated `uint64_t` declarations in `test_time_signature_map.cpp` (`readability-isolate-declaration`)

---

## [0.3.0] - 2026-05-23

### Added
- **M3 — Pattern library, song arrangement, and performance source** (sprints 3.1–3.3):
  - `PatternLibrary` — named, length-bounded event sequences stored in PMR vectors; C API `omega_pattern_create/destroy/add_event/set_length` (sprint 3.1)
  - `SongArrangementSource` — chains patterns with repeat counts; window-based tick dispatch; `on_locate()` for mid-arrangement resume; note-off tracking; C API `omega_song_append/clear` (sprint 3.2)
  - `PerformanceSource` — 64-slot state machine (EMPTY→IDLE→QUEUED→PLAYING→STOPPING); per-slot transpose (±24 semitones), velocity scale (0–200%), random bias (0–100%); phase-resume on locate; C API `omega_perf_assign/cue/stop/stop_all/set_transpose/set_velocity_scale/set_random_bias` (sprint 3.3)

---

## [0.2.0] - 2026-05-22

### Added
- **M2 — Single-track playback via C API** (sprints 2.1–2.4):
  - `ClockSource` abstraction + `MockClock` test utility (sprint 2.1)
  - `OutputSink` interface + `CapturingSink` test utility (sprint 2.2)
  - `TimelineSource` + `TrackData` — linear multi-track playback, note/CC chasing on locate (sprint 2.3)
  - C API wiring: `omega_engine_add_track`, `omega_engine_add_event`, `omega_engine_play/stop/locate`, `omega_engine_process`; C API integration tests (sprint 2.4)

### Fixed
- Reduced `SpscQueue` default capacity from 65536 to 4096 to avoid stack overflow on Windows (default stack is 1 MB; 65536-slot queue consumed ~4 MB)

---

## [0.1.0] - 2026-05-22

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
- **EventSource abstraction** (design 10): pluggable playback modes via `EventSource` interface; `TransformSource` base class for composition-based routing; `on_locate()` with `chase_out` dispatcher for note/CC/program chasing; updated `EventSourceRegistry` signatures; `MockEventSource` test utility; extended extension point catalogue (step sequencer, generative, polyrhythmic, reactive)
- Initial design proposal covering architecture, event model, and three sequencing modes
- Design documents: timing model, thread model, memory/storage, C API, pattern state machine, session container, extensions, testing strategy, prior art
- Project infrastructure: license, contributing guide, CI, issue templates

---

[Unreleased]: https://github.com/braveness23/omega/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/braveness23/omega/compare/v0.6.0-beta...v1.0.0
[0.5.0-alpha]: https://github.com/braveness23/omega/compare/v0.4.0...v0.5.0-alpha
[0.4.0]: https://github.com/braveness23/omega/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/braveness23/omega/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/braveness23/omega/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/braveness23/omega/releases/tag/v0.1.0
