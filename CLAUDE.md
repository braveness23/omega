# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

## Status

Omega is in **design phase** — no implementation exists yet. The `src/` directory is empty, `include/omega/` has stub headers (`omega.h`, `export.h`) with documentation conventions but no implementations, and all CMake library targets are `INTERFACE` stubs. The design documents in `docs/design/` are the authoritative source of truth.

---

## Build and Test

Preferred: use CMake presets (defined in `CMakePresets.json`).

```bash
# Development build with ASan + UBSan
cmake --preset dev && cmake --build --preset dev
ctest --preset dev

# ThreadSanitizer (separate preset — TSan and ASan are mutually exclusive)
cmake --preset tsan && cmake --build --preset tsan
ctest --preset tsan

# Release build
cmake --preset release && cmake --build --preset release
```

Manual (without presets):

```bash
# Standard build with tests
cmake -B build -DOMEGA_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build

# Run a single test (by name pattern)
ctest --test-dir build -R test_timing_model

# With AddressSanitizer + UBSanitizer
cmake -B build_san -DOMEGA_BUILD_TESTS=ON -DOMEGA_WITH_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build_san
ctest --test-dir build_san

# With ThreadSanitizer (separate from ASan — mutually exclusive)
cmake -B build_tsan -DOMEGA_BUILD_TESTS=ON -DOMEGA_WITH_TSAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build_tsan
ctest --test-dir build_tsan

# Ableton Link support (changes combined license to GPL v2+)
cmake -B build -DOMEGA_BUILD_TESTS=ON -DOMEGA_WITH_LINK=ON
```

Linux requires `libasound2-dev` (ALSA) for MIDI I/O. macOS uses CoreMIDI (no extra install). Dependencies (libremidi, midifile, Catch2) are fetched automatically via CMake `FetchContent`.

## Formatting

```bash
# Check (CI does this on every PR)
find include src tests -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \
  | xargs clang-format-18 --dry-run --Werror

# Apply
find include src tests -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \
  | xargs clang-format-18 -i
```

Style is Google-based with Allman braces, 4-space indent, 100-column limit. See `.clang-format` for full config.

---

## Architecture

Omega is a **C++ sequencer engine** with three public layers:

1. **`omega::core` (C++17)** — the engine, session data, timing, and event dispatch. Internal to the library; can evolve freely.
2. **`omega.h` (C API)** — a stable `extern "C"` ABI that never breaks. All public handles are opaque pointers. All mutating functions return `omega_status_t`. The only cross-language binding surface.
3. **Platform integrations** — libremidi (MIDI I/O, MIT), midifile (SMF, BSD), optional Ableton Link (GPL v2+).

### Engine / Session separation

`Engine` is the playback machine. `Session` is the data. They are separate objects. The engine holds a reference to the active session and can swap sessions without reinitializing. Session owns shared data: `PatternLibrary`, `SinkRegistry`, `TempoMap`, `TimeSignatureMap`, `ModulationBus`, `PerformanceContext`, `GrooveLibrary`, `EventSourceRegistry`, and `EventInputRegistry`.

### Six extension points

| Extension | Interface | Thread that calls it |
|---|---|---|
| Output destination | `OutputSink::send()` | Timing |
| Clock | `ClockSource::now_ns()` | Timing |
| Playback mode | `EventSource::advance()` | Timing |
| Event input | `EventInput::poll()` | Timing |
| Custom event types | `PayloadRegistry` | N/A (registration only) |
| Edit operations | `Command::execute()` / `undo()` | Mutation |

### EventSource — the core abstraction for playback modes

`engine.process()` polls all `EventInput` instances first (filling the `InputBus`), then iterates all registered `EventSource` instances in registration order, calling `advance(to_tick, dispatcher, ctx)` on each. `ProcessContext` carries read/write access to `ModulationBus`, `PerformanceContext`, and the `InputBus` for this cycle.

Source registration order convention: **MODULATOR → CONTEXT → PLAYBACK**. Modulator sources (`LfoSource`, etc.) write to `ModulationBus`. Context sources (`ChordDetectorSource`, etc.) write to `PerformanceContext`. Playback sources read from both and dispatch events.

The three built-in playback sources are registered automatically:
- **`TimelineSource`** — owns the `Timeline` (tracks + event vectors). Linear multi-track playback. Supports note/CC/program chasing on locate.
- **`SongArrangementSource`** — owns the `SongArrangement`. Chains patterns with repeat counts.
- **`PerformanceSource`** — owns 64 performance slots. State machine: EMPTY → IDLE → QUEUED → PLAYING → STOPPING. Per-slot: transpose (±24 semitones), velocity scale (0–200%), random bias (0–100%). Supports phase-resume chasing on locate. Cue modes: `OMEGA_CUE_AT_BOUNDARY`, `OMEGA_CUE_IMMEDIATE`, `OMEGA_CUE_QUANTIZED`, `OMEGA_CUE_BAR` (bar-boundary cue via `TimeSignatureMap`; degrades to `OMEGA_CUE_AT_BOUNDARY` in freeform mode).

Custom sources implement `EventSource` and are added via `omega_engine_add_source()`. Custom inputs implement `EventInput` and are added via `omega_engine_add_input()`. `TransformSource` is a provided base class for composition-based routing (wraps an upstream source to transform its output).

`EventSource::on_locate(tick, chase_out, ctx)` is called on transport locate. Stateful sources reset local playback position; sources with chasing support dispatch catch-up events via `chase_out`.

### Orchestration layer

Four session-level additions enable the engine to orchestrate virtually any sequencer architecture (see `docs/design/11-orchestration-layer.md`):

- **`EventInput` / `InputBus`** — incoming events (MIDI, OSC, CV) are polled each cycle and broadcast to all sources via `ProcessContext`.
- **`TransformSource`** — composition-based routing; wraps any source to transform its output before it reaches sinks.
- **`ModulationBus`** — 256 named `float` channels; updated by modulator sources, read by any source to drive continuous parameters.
- **`PerformanceContext`** — shared musical state (scale, chord, groove template, chaos, global transpose/velocity) readable and writable by any source each cycle.

### Thread model

Two threads only:

- **Timing thread**: calls `engine.process()`. Must never allocate, block, or lock. Drains the SPSC command queue, polls all `EventInput` instances, then calls `advance(to_tick, dispatcher, ctx)` on every registered source in priority order, dispatching due events to sinks.
- **Mutation thread**: enqueues `Command` variants (`AddEventCmd`, `SetTempoCmd`, `SetScaleCmd`, `AddSourceCmd`, etc.) via a lock-free SPSC ring buffer (default capacity: 4096). Returns immediately.

The SPSC queue is single-producer. Multiple mutation threads must serialize externally. `OutputSink::send()` and `EventInput::poll()` fire from the timing thread — implementations must not block or allocate.

### Timing

- 480 PPQN tick resolution (compile-time constant; never hardcode 480 in user code).
- All internal time is `uint64_t` nanoseconds from session start. All musical time is `uint64_t` ticks. No floating point in the hot path.
- The `TempoMap` is a sorted list of `TempoPoint{tick, bpm_milli, ns_at_tick}`. `ns_at_tick` is precomputed on insert. BPM is stored as milli-BPM (`uint32_t`): 120 BPM = `120000`.
- The `TimeSignatureMap` is a sorted list of `TimeSigPoint{tick, numerator, denominator}`, parallel to `TempoMap`. An empty map means freeform mode (no meter). Denominator is the literal note value (4 = quarter note). `OMEGA_ERR_NO_METER` is returned by any meter-dependent helper when the session is freeform.
- The optional `SmpteConfig` on `Session` stores frame rate and drop-frame flag for SMPTE timecode. Absent = no video lock. `OMEGA_ERR_NO_SMPTE_CONFIG` is returned by SMPTE helpers when not set.
- `PositionConverter` is the base class for all coordinate-system helpers (bar/beat, SMPTE, future: samples, feet+frames). `MeterCursor` and `SmpteConverter` both implement it. Neither may be called from the timing thread. Snap-to-grid utilities accept `PositionConverter&` without caring which format is active.
- The catch-up loop fires all overdue events in order — events are never skipped when cycles run late.

### Memory

Events are 24-byte structs stored in `std::pmr::vector<Event>` sorted by tick. PMR allocators make the library embeddable: pass a `monotonic_buffer_resource` over a static buffer to eliminate heap use entirely. Large payloads (sysex, OSC) live in a separate blob store; events hold a blob index.

Recording uses a per-track staging ring buffer (1024 entries). On commit, entries are tick-converted, sorted, and merged into the track vector. The timing thread never inserts mid-vector during playback.

Note-offs are tracked in a separate active-notes table (128 entries per sink/channel), not in the event stream. On transport stop or slot transition to IDLE, all active notes receive immediate note-off.

### C API ownership rules

- Every `omega_*_create()` returns a caller-owned handle; caller must call the matching `omega_*_destroy()` before `omega_engine_destroy()`.
- The engine holds non-owning references to clocks. `SinkRegistry` owns sinks; `EventSourceRegistry` holds shared_ptr to sources.
- `omega_track_*`, `omega_pattern_*`, and `omega_perf_*` functions target the built-in sources. Removing a built-in source via `omega_engine_remove_source()` makes those functions return `OMEGA_ERR_NOT_FOUND`.

---

## Naming Conventions

| Category | Convention |
|---|---|
| Types | `PascalCase` |
| Functions / variables | `snake_case` |
| Private members | trailing underscore (`value_`) |
| Constants / enums | `SCREAMING_SNAKE_CASE` |
| C API public symbols | `omega_` prefix |

---

## Testing

Tests use **Catch2** (BSL-1.0, fetched via FetchContent). Four test utilities are **part of the public library** (`include/omega/test/`), available to application authors:

- `MockClock` — manually-advanced clock; use `advance_ticks()`, `advance_beats()`, or `set_ns()`. All time-dependent behavior must be testable without a real clock.
- `CapturingSink` — records all dispatched events; provides `count()`, `has_note_on()`, `has_note_off()`, `first()` queries.
- `MockEventSource` — primed with events at specific ticks; injects them into the engine pipeline during `advance()`. Use alongside `CapturingSink` to test source→sink paths in isolation.
- `MockEventInput` — primed with events to deliver during `poll()`; simulates incoming MIDI/OSC without hardware. Use to test reactive and input-driven sources.

Test files go in `tests/unit/` and `tests/integration/`. Add new `.cpp` files to `tests/CMakeLists.txt` (currently all commented out pending implementation). Benchmarks (`tests/benchmarks/`) use Catch2's `BENCHMARK` macro; they run in CI on `main` pushes only (not on PR builds).

---

## Commit Messages

Use Conventional Commits format:
```
feat: add OscSink implementation
fix: correct note-off timing when loop wraps
docs: clarify thread safety in C API
test: add pattern state machine edge case tests
chore: update libremidi to 5.x
```
