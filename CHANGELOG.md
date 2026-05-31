# Changelog

All notable changes to Omega will be documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Omega uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

## [1.1.0] — 2026-05-30

### Added
- **Native session serialization** (closes #62): `omega_session_save(e, path)` / `omega_session_load(e, path)` (C API) and `omega::session_save` / `omega::session_load` (C++ API in `<omega/session.h>`). Saves and restores all engine state not captured by SMF: tempo map, time-signature map, SMPTE config, loop region, markers, regions, pattern library (all patterns and their events), performance slot assignments (pattern, transpose, velocity scale, random bias, repeat count, mute), song arrangement, performance context (scale, chord, groove, chaos, global transpose/velocity), track mute/solo flags, and transport state. Uses a versioned binary format with tagged sections (4-byte tag + 4-byte length) for forward-compatible file reading — unknown sections are skipped. Writes atomically via a `.tmp` rename. 16 new unit tests in `tests/unit/test_session.cpp`.

### Fixed
- **`loop_set_immediate()`**: `loop_set()` enqueues a `SetLoopCmd` that is applied only on the next `process()` call, so `loop_region()` returned stale values when queried before the engine had ticked. Added `Engine::loop_set_immediate(start_tick, end_tick)` (C API: `omega_loop_set_immediate()`) as a synchronous variant that applies the loop region directly — valid only when the engine is stopped (`OMEGA_ERR_UNSUPPORTED` if playing). 3 new unit tests. Closes #58.

### Added
- **Control-sequence Wait semantics** (closes #59): `OMEGA_CTRL_START_SLOT_WAIT` — a new control-sequence payload tag that starts a target slot AND blocks the control-sequence slot's event dispatch until the target reaches IDLE. Encodes `data[0..3]`=target_slot, `data[4]`=cue_mode, `data[5]`=ctrl_slot (0–127). `PerfSlot` gains a `wait_for_slot_` field (initialized to `kNoWait = UINT32_MAX`); `PerformanceSource::advance()` checks the wait flag before dispatching each slot and clears it when the target slot transitions to IDLE or EMPTY; `dispatch_slot_events()` returns early after the Wait event fires, blocking subsequent events in the same buffer. `on_locate()` clears all wait states (prevents stale waits surviving a transport seek). Constructor helper: `omega_make_ctrl_start_slot_wait(tick, sink_id, target_slot, mode, ctrl_slot)`. Dispatched via `Engine::execute_ctrl_event()`. 2 new unit tests.
- **C API parity for track operations, SMF import options, and ControlSink** (closes #63):
  - **Track mute/solo**: `omega_engine_set_track_mute()`, `omega_engine_set_track_solo()`, `omega_engine_track_is_muted()`, `omega_engine_track_is_soloed()` — expose the existing C++ track mute/solo API to C consumers.
  - **Track metadata**: `omega_engine_set_track_name()`, `omega_engine_set_track_channel()` — set display name and representative MIDI channel.
  - **Track event manipulation**: `omega_engine_replace_track_event()`, `omega_engine_shift_track_events()`, `omega_engine_swap_tracks()` — wrap the C++ `replace_track_event`, `shift_track_events`, and `swap_tracks` engine methods.
  - **`omega_smf_import_ex(e, path, opts)`**: extended SMF import with `omega_smf_import_options_t` struct exposing `sink_id`, `split_by_channel`, and `clear_existing`. Passing `opts = NULL` is equivalent to `omega_smf_import()`.
  - **`omega_sink_create_control(e)` / `omega_sink_destroy_control(sink)`**: allocates a `ControlSink` on the heap, making control-sequence patterns usable from C without C++ constructors. Returns `NULL` on allocation failure or `NULL` engine.
  - 26 new integration tests in `tests/integration/test_c_api_tracks.cpp`.
- **`Engine::loop_activate_region(uint32_t region_index)`** (C API: `omega_loop_activate_region()`): activates the `RegionList` entry at the given index as the transport loop, equivalent to calling `loop_set(region.start_tick, region.end_tick)`. Returns `OMEGA_ERR_NOT_FOUND` if the index is out of range or the region's type is not `RegionType::LOOP` / `OMEGA_REGION_LOOP`. Makes `RegionList` the authoritative store for named cue points and eliminates the need for apps to maintain parallel cue-loop arrays (e.g. KCS TRACK mode's six independent cue loops). 4 new unit tests. Closes #61.
- **`PatternLibrary::count()` and `PatternLibrary::for_each()`**: `count()` returns the number of live (non-destroyed) patterns; `for_each(fn)` invokes a callback once per live pattern in unspecified order. Exposed in the C API as `omega_pattern_library_count()` and `omega_pattern_for_each()`. Eliminates the need for callers to maintain a shadow registry of pattern IDs. 6 new unit tests in `tests/unit/test_pattern_library.cpp`; 5 new integration tests in `tests/integration/test_c_api_pattern_library.cpp`. Closes #57 (finding #18).
- **`Engine::convert_tracks_to_patterns(sink_id, loop_end_ticks)`**: engine-level conversion helper — creates one `Pattern` per timeline track (in track-vector order), assigns each to the corresponding `PerformanceSource` slot (slot N = track index N), re-routes all events to `sink_id`, and sets `loop_end_ticks` as the pattern length for every created pattern. Returns the number of patterns created. Thread: mutation thread, engine must be stopped. Exposed in C API as `omega_convert_tracks_to_patterns()`. 7 new unit tests in `tests/unit/test_convert_tracks.cpp`; 4 new integration tests in `tests/integration/test_c_api_pattern_library.cpp`. Closes #57 (finding #19).
- **Per-slot `repeat_count` and `mute` for `PerformanceSource`**: `Engine::perf_set_repeat_count(SlotId, uint32_t)` (0 = infinite loop, N = auto-stop after N full pattern iterations) and `Engine::perf_set_mute(SlotId, bool)` (mute suppresses event dispatch while the pattern cursor keeps advancing; active notes receive immediate note-offs on mute engagement; unmuting resumes from current position). Routed through the SPSC command queue; safe during playback. Exposed in C API as `omega_perf_set_repeat_count()` and `omega_perf_set_mute()`. 8 new unit tests. Closes #56.
- **`Engine::undo()` / `Engine::redo()`** and **`omega_engine_undo()` / `omega_engine_redo()`** (`omega.h`): multi-level undo/redo command history (up to 64 levels) for the four undoable edit operations — `AddEventCmd`, `DeleteEventCmd`, `ReplaceTrackEventCmd`, and `ReplaceEventCmd`. Each undoable `apply()` captures both the inverse command (undo) and the original command (redo) as a `HistoryEntry` on the timing thread before the mutation is applied; `UndoCmd`/`RedoCmd` move entries between the undo and redo stacks and re-apply the appropriate command under `applying_undo_redo_` suppression to prevent recursive recording. History stacks are pre-reserved to 64 entries in the constructor to avoid heap allocation on the timing thread. Redo is cleared whenever a new undoable edit is applied. 17 new unit tests in `tests/unit/test_undo_redo.cpp`. Closes #55.
- **`omega_midi_note_from_name()`** (`omega.h`): inverse of `omega_midi_note_name()` — parses a note name string (e.g. "C4", "F#3", "Bb4", "C-1") into a MIDI pitch byte. Accepts A–G (case-insensitive), optional `#`/`b` accidental, and octave number; returns `OMEGA_OK` on success or `OMEGA_ERR_INVALID` for unrecognised names or out-of-range results. 7 new integration tests. Closes #54.
- **`Engine::shift_track_events(TrackId, int64_t offset_ticks)`**: shifts all events in a timeline track by an offset (positive = delay, negative = advance); events clamped to tick 0; single `stable_sort` at the end rather than O(N) sorts from N `ReplaceTrackEventCmd` calls. Adds `TimelineSource::shift_events()` and `TimelineSource::clear_tracks()`. Engine must be stopped. 6 new unit tests.
- **`Engine::swap_tracks(TrackId, TrackId)`**: swaps the vector positions of two tracks in `TimelineSource`, changing playback and display order. Engine must be stopped. 2 new unit tests.
- **`SmfImportOptions::clear_existing`**: when `true`, clears the engine's timeline, tempo map (reset to 120 BPM), time-signature map, and markers before importing — enabling a clean "replace session" load rather than appending to existing content.
- **`omega::ControlSink`** (`include/omega/control_sink.h`): custom `OutputSink` that intercepts `OMEGA_CTRL_*` payload events and executes them as engine mutations on the timing thread via `Engine::execute_ctrl_event()`. Constructed with `ControlSink(Engine&)`. Control patterns route events here by using `ctrl_sink.sink_id()` as their events' `sink_id`. Thread-safe: same thread as `Engine::apply()` overloads.
- **`Engine::execute_ctrl_event(const Event&)`**: single dispatch point for control-sequence events on the timing thread. Handles `OMEGA_CTRL_START_SLOT`, `OMEGA_CTRL_STOP_SLOT`, `OMEGA_CTRL_SET_TEMPO`, and `OMEGA_CTRL_TRANSPOSE`; all other tags are silently ignored. Centralises control-event dispatch so new `OMEGA_CTRL_*` tags require only a new `case` in one switch. Closes #60.
- **Control-sequence payload tags** (`omega.h`): `OMEGA_CTRL_START_SLOT` (0x0B), `OMEGA_CTRL_STOP_SLOT` (0x0C), `OMEGA_CTRL_SET_TEMPO` (0x0D), `OMEGA_CTRL_TRANSPOSE` (0x0E). Constructor helpers: `omega_make_ctrl_start_slot()`, `omega_make_ctrl_stop_slot()`, `omega_make_ctrl_set_tempo()`, `omega_make_ctrl_transpose()`. 5 new unit tests in `tests/unit/test_control_sink.cpp`.
- **`ReplaceTrackEventCmd`** / **`Engine::replace_track_event()`**: replaces the timeline track event at `(tick, within-tick-index)` with a replacement; re-sorts the event vector if the tick changes. Safe during playback (queued). Mirrors the existing pattern-only `ReplaceEventCmd`. 5 new unit tests.
- **`PERF_MAX_SLOTS` increased from 64 to 128** (`perf_slot.h`, `omega.h`): `std::array<PerfSlot, PERF_MAX_SLOTS>` and all bounds checks use the constant — no other code changes. 1 new unit test verifying slot 127.
- **`omega::Recorder`** (`include/omega/recorder.h`): custom `EventSource` registered at `OMEGA_SOURCE_PRIORITY_MODULATOR` for live MIDI capture into a `TimelineSource` track. Maintains a pending-note table (per-channel, per-pitch) so `NOTE_ON` events are only inserted when their matching `NOTE_OFF` arrives, giving correct inline durations. `on_locate()` clears pending notes to prevent cross-loop duration corruption. 8 new unit tests in `tests/unit/test_recorder.cpp`.
- **Track-level controls** (C++ API): `Engine::set_track_mute/solo(TrackId, bool)`, `track_is_muted/soloed(TrackId)`, `set_track_name(TrackId, string)`, `set_track_channel(TrackId, uint8_t)`. Per-track mute/solo added to `TimelineSource::advance()` with solo-exclusive semantics; suppressed tracks emit no new events while duration-scheduled note-offs still fire. `SetTrackMuteCmd` / `SetTrackSoloCmd` routed through SPSC queue; safe during playback.
- **SMF routing and split-by-channel** (`SmfImportOptions`): `opts.sink_id` routes imported events and tracks to a chosen sink; `opts.split_by_channel` explodes a Type 0 file into one track per MIDI channel. `Track::channel` is now populated from the track's first `NOTE_ON`. `omega::smf_import/export` declared in public `<omega/smf.h>` (was internal-only).
- **Extended `omega_position_t`** (`omega.h`): adds `bpm_milli`, `numerator`, `denominator`, `loop_enabled` — written atomically each `process()` cycle alongside existing bar/beat/tick fields, giving any-thread display of live tempo and meter without data-race risk. Adds `TempoMap::bpm_milli_at(tick)`.
- **`Engine::timeline_source()` non-const accessor**: parallel to existing const overload; required for `Recorder` and `ControlSink` construction.
- **Bug fix**: `on_locate()` was not propagated to custom `EventSource` instances (`custom_sources_`) on either explicit `TransportCmd{LOCATE}` or loop-wrap. Now both sites iterate `custom_sources_` before the three built-in sources.
- **Engine event callbacks** (`omega.h`): `omega_engine_set_event_callback(e, cb, userdata)` registers a callback that fires from the timing thread on state transitions. Events: `OMEGA_EVENT_SLOT_STARTED` (detail = slot index), `OMEGA_EVENT_SLOT_STOPPED` (detail = slot index), `OMEGA_EVENT_LOOP_WRAPPED` (detail = loop count), `OMEGA_EVENT_TRANSPORT_STOPPED` (detail = 0). Callback must not block, allocate, or call back into the engine. C++ API: `Engine::set_event_callback()`. 8 new unit tests in `tests/unit/test_event_callback.cpp`; 3 new integration tests in `tests/integration/test_c_api_callbacks.cpp`.
- **`omega_pattern_for_each_event()`** (`omega.h`): callback-based iterator for pattern events with channel and tag filters. Formalizes the existing thread-safety contract: safe to call from any thread during playback since the timing thread reads events read-only and mutations are applied between `process()` cycles. Returns `OMEGA_ERR_NOT_FOUND` for invalid patterns. The callback index is the event's original position in the unfiltered pattern. 7 new integration tests in `tests/integration/test_c_api_for_each.cpp`.
- **Query boundary APIs** (`omega.h`): thread-safe read access to engine state from any thread. `omega_perf_slot_state(e, slot)` returns the current performance slot state (`OMEGA_SLOT_EMPTY`, `OMEGA_SLOT_IDLE`, `OMEGA_SLOT_QUEUED`, `OMEGA_SLOT_PLAYING`, `OMEGA_SLOT_STOPPING`) — eliminates the need for UI-side mirror variables. `omega_sink_is_muted(e, sink_id, channel)` and `omega_sink_is_soloed(e, sink_id, channel)` query the engine's authoritative mute/solo state. All backed by atomics; safe to call concurrently with `process()`. C++ API: `Engine::perf_slot_state()`, `Engine::sink_is_muted()`, `Engine::sink_is_soloed()`. 16 new unit tests in `tests/unit/test_query_boundary.cpp`; 8 new integration tests in `tests/integration/test_c_api_query.cpp`.
- **`omega_sink_set_mute()` / `omega_sink_set_solo()`** (`omega.h`): per-channel mute and solo for registered output sinks. `omega_sink_set_mute(e, sink_id, channel, muted)` mutes or unmutes a MIDI channel (0–15) or all channels (0xFF) on the given sink; when transitioning from unmuted to muted, the engine immediately sends note-off for every active note on that channel before suppressing further note-ons. `omega_sink_set_solo(e, sink_id, channel, soloed)` enables solo-exclusive mode — while any channel is soloed, only soloed (sink, channel) pairs produce output; engaging solo mid-note sends immediate note-off for all now-suppressed active notes; clearing all solos restores normal mute-based filtering. C++ API: `Engine::sink_set_mute()`, `Engine::sink_set_solo()`. Commands: `SetSinkMuteCmd`, `SetSinkSoloCmd`. 18 new unit tests in `tests/unit/test_mute_solo.cpp`; 11 new integration tests in `tests/integration/test_c_api_mute_solo.cpp`. Closes #31.
- **`ReplaceEventCmd` / `omega_pattern_replace_event()`** (`commands.h`, `omega.h`): replace the event at a 0-based index in a pattern's sorted event vector; if the replacement tick differs from the original the vector is re-sorted; enqueued through the SPSC command queue so it is safe during playback; C API: `omega_pattern_replace_event(e, pat, event_index, replacement)` returns `OMEGA_OK` when the command is enqueued (bounds checking occurs on the timing thread); 4 new unit tests in `tests/unit/test_replace_event.cpp`, 5 new integration tests in `tests/integration/test_c_api_patterns.cpp`. Closes #30.
- **`omega_engine_position()` / `omega_position_t`** (`omega.h`): atomic position snapshot — bar (1-based), beat (1-based), subdivision (ticks past beat boundary), loop_count (wraps of the active loop region), and raw tick — all updated together at the end of each `process()` call. Bar/beat/subdivision are 0 in freeform mode (no time signature). `loop_count` increments on every loop-region wrap and resets to 0 when the loop region is changed via `omega_loop_set()` or `omega_loop_clear()`. Five individually-atomic fields; imperceptibly consistent at normal display refresh rates. C++ accessor: `Engine::position()`. 12 new unit tests in `tests/unit/test_position_snapshot.cpp`. Closes #27.
- **`omega_midi_note_name()`** (`omega.h`): converts a MIDI pitch byte (0–127) to a human-readable note name string ("C4", "F#3", "A-1", etc.) using sharp names and the MIDI octave convention (middle C = C4). Thread-safe. 6 new integration tests. Closes #33.
- **`omega_format_position()`** (`omega.h`): formats a tick position as a "bar:beat.subdivision" string (e.g. "3:2.120") using the engine's `TimeSignatureMap` via `MeterCursor`. Returns `OMEGA_ERR_NO_METER` in freeform mode. 6 new integration tests. Closes #33.
- **Sink-before-pattern ordering** (`omega.h`): documented the `sink_id` ordering constraint explicitly in the `omega_pattern_add_event()` and `omega_sink_create_midi_out()` docstrings — callers must create and register sinks before building patterns that reference their IDs. Closes #32.
- **Event field accessors** (`omega.h`): symmetric read/write accessors for the `omega_event_t` payload fields — `omega_event_note_pitch()`, `omega_event_note_velocity()`, `omega_event_note_duration()`, `omega_event_set_pitch()`, `omega_event_set_velocity()`, `omega_event_set_duration()`, `omega_event_cc_number()`, `omega_event_cc_value()` — eliminates the need to know the private `data[]` byte layout. 10 new unit tests. Closes #29.
- **Pattern read API** (`omega.h`): `omega_pattern_event_count()`, `omega_pattern_event_at()`, `omega_pattern_event_count_filtered()`, `omega_pattern_length()` — allows C callers to inspect pattern contents without accessing the C++ layer. 12 new integration tests. Closes #28.
- **`omega_engine_position_tick()`** (`omega.h`): returns the current transport position in ticks, converting via the TempoMap (handles tempo automation correctly; no floating-point). 2 new unit tests, 3 new integration tests. Closes #26.
- **Tempo map C API** (`omega.h`): `omega_tempo_set(e, tick, bpm_milli)` inserts or replaces a tempo point; `omega_tempo_remove(e, tick)` removes a tempo automation point (no-op at tick 0); `omega_tempo_at(e, tick, &bpm_milli_out)` queries the BPM in effect at any tick. `TempoMap::remove()` added to support tick-based removal. 14 new integration tests in `tests/integration/test_c_api_tempo.cpp`. Closes #25.
- **`omega_sink_id(sink)`** (`omega.h`): returns the unique `sink_id` for any `omega_sink_t*` handle; 0 on NULL. Removes the need to cast to C++ and call `OutputSink::sink_id()`. Closes #25.
- **transport loop region** (`SetLoopCmd` / `omega_loop_set` / `omega_loop_clear` / `omega_loop_enable`): when playing, the engine automatically locates back to `start_tick` whenever `to_tick >= end_tick`; `on_locate()` is called on all built-in sources at the wrap point to send note-offs and reset playback cursors; C++ API: `Engine::loop_set(start, end)`, `Engine::loop_clear()`, `Engine::loop_enable(bool)`, `Engine::loop_region()`; C API: `omega_loop_set()`, `omega_loop_clear()`, `omega_loop_enable()`; 8 new unit tests in `tests/unit/test_loop.cpp`; closes #24
- **tapedeck-3 example** (`examples/tapedeck-3/`): extends tapedeck-2 to demonstrate the Q1–Q3 query-boundary APIs. Eliminates `bool playing` (driven by `OMEGA_EVENT_TRANSPORT_STOPPED` callback), `bool slot_cued` (replaced by `engine.perf_slot_state()` Q1 query), `bool muted_[]` / `bool soloed_[]` shadow arrays (replaced by `engine.sink_is_muted()` / `engine.sink_is_soloed()` Q2 queries), and manual loop-count tracking (replaced by `OMEGA_EVENT_LOOP_WRAPPED` Q3 callback). Adds live slot-state indicator (EMPTY/IDLE/QUEUED/PLAYING/STOPPING) and last-event display in the status line.
- **tapedeck example**: 8-track, 8-bar looping MIDI sequencer CLI (`examples/tapedeck/`) using `omega::Engine`, `LibremidiSink`, `OmegaTimer`, and `PerformanceSource`; SPACE play/stop, ESC rewind, Ctrl+C exit; per-track and global MIDI activity indicators; clock time display; `ActivitySink` wrapper for real-time note-on tracking; `OMEGA_BUILD_EXAMPLES=ON` enables the build

---

## [1.0.0] — 2026-05-25

### Added
- **Sprint 7.3 -- stable release**: `OMEGA_VERSION_MAJOR/MINOR/PATCH` compile-time macros in `omega.h`; `omega_version()` now returns `{1,0,0}` via the macros; `CMakeLists.txt` `VERSION` bumped to `1.0.0`, `OMEGA_SOVERSION` bumped to `1`; `abi/v1.0.0.dump` committed as the v1.0.0 ABI baseline
- **Sprint 7.2 -- getting started guide**: `docs/GETTING_STARTED.md` covering CMake FetchContent install, two working examples (MockClock/CapturingSink and LibremidiSink/OmegaTimer), core concepts, thread model, and six extension points
- **Sprint 7.2 -- diagrams**: four Mermaid diagrams in `docs/diagrams/` — architecture layers, `engine.process()` flowchart, PerformanceSource state machine, and class relationships; linked from `docs/ARCHITECTURE.md`
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
