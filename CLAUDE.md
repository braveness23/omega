# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

## Status

Omega is in **design phase** — no implementation exists yet. The `src/` directory is empty, `include/omega/` is a placeholder, and all CMake library targets are `INTERFACE` stubs. The design documents in `docs/design/` are the authoritative source of truth.

---

## Build and Test

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

# Ableton Link support (changes combined license to GPL v2+)
cmake -B build -DOMEGA_BUILD_TESTS=ON -DOMEGA_WITH_LINK=ON
```

Linux requires `libasound2-dev` (ALSA) for MIDI I/O. macOS uses CoreMIDI (no extra install). Dependencies (libremidi, midifile, Catch2) are fetched automatically via CMake `FetchContent`.

## Formatting

```bash
# Check (CI does this on every PR)
find include src tests -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \
  | xargs clang-format-15 --dry-run --Werror

# Apply
find include src tests -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \
  | xargs clang-format-15 -i
```

Style is Google-based with Allman braces, 4-space indent, 100-column limit. See `.clang-format` for full config.

---

## Architecture

Omega is a **C++ sequencer engine** with three public layers:

1. **`omega::core` (C++17)** — the engine, session data, timing, and event dispatch. Internal to the library; can evolve freely.
2. **`omega.h` (C API)** — a stable `extern "C"` ABI that never breaks. All public handles are opaque pointers. All mutating functions return `omega_status_t`. The only cross-language binding surface.
3. **Platform integrations** — libremidi (MIDI I/O, MIT), midifile (SMF, BSD), optional Ableton Link (GPL v2+).

### Engine / Session separation

`Engine` is the playback machine. `Session` is the data. They are separate objects. The engine holds a reference to the active session and can swap sessions without reinitializing. Session owns: `PatternLibrary`, `Timeline`, `SongArrangement`, `PerformanceConfig`, `SinkRegistry`, `TempoMap`.

### Thread model

Two threads only:

- **Timing thread**: calls `engine.process()`. The hot path. Must never allocate, block, or lock.
- **Mutation thread**: enqueues `Command` variants (`AddEventCmd`, `SetTempoCmd`, etc.) via a lock-free SPSC ring buffer (default capacity: 4096). `engine.process()` drains the queue first, then fires events.

The SPSC queue is single-producer. Multiple mutation threads must serialize externally. `OutputSink::send()` and recording callbacks fire from the timing thread — sinks are responsible for their own thread safety.

### Timing

- 480 PPQN tick resolution (compile-time constant; never hardcode 480 in user code).
- All internal time is `uint64_t` nanoseconds from session start. All musical time is `uint64_t` ticks. No floating point in the hot path.
- The `TempoMap` is a sorted list of `TempoPoint{tick, bpm_milli, ns_at_tick}`. `ns_at_tick` is precomputed on insert. BPM is stored as milli-BPM (`uint32_t`): 120 BPM = `120000`.
- The catch-up loop fires all overdue events in order — events are never skipped when cycles run late.

### Three sequencing modes (non-exclusive)

- **Timeline**: linear multi-track playback, scans `session.timeline.tracks[*].events`.
- **Pattern**: song arrangement mode, chains patterns by `PatternId` with repeat counts.
- **Performance**: live cuing with a 64-slot state machine (EMPTY → IDLE → QUEUED → PLAYING → STOPPING). Per-slot real-time params: transpose (±24 semitones), velocity scale (0–200%), random bias (0–100%).

All three modes can run simultaneously. See `docs/design/05-pattern-state-machine.md` for full slot state transitions and cue mode semantics.

### Memory

Events are 24-byte structs stored in `std::pmr::vector<Event>` sorted by tick. PMR allocators make the library embeddable: pass a `monotonic_buffer_resource` over a static buffer to eliminate heap use entirely. Large payloads (sysex, OSC) live in a separate blob store; events hold a blob index.

Recording uses a per-track staging ring buffer (1024 entries). On commit, entries are tick-converted, sorted, and merged into the track vector. The timing thread never inserts mid-vector during playback.

Note-offs are tracked in a separate active-notes table (128 entries per sink/channel), not in the event stream. On transport stop or slot transition to IDLE, all active notes receive immediate note-off.

### C API ownership rules

- Every `omega_*_create()` returns a caller-owned handle; caller must call the matching `omega_*_destroy()` before `omega_engine_destroy()`.
- The engine holds non-owning references to clocks and sinks.
- Exception: `SinkRegistry` (internal) owns sinks via `unique_ptr` after `omega_engine_add_sink()` — the C-level `omega_sink_t` handle is still the caller's responsibility to destroy.

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

Tests use **Catch2** (BSL-1.0, fetched via FetchContent). Two test utilities are **part of the public library** (`include/omega/test/`), available to application authors:

- `MockClock` — manually-advanced clock; use `advance_ticks()`, `advance_beats()`, or `set_ns()`. All time-dependent behavior must be testable without a real clock.
- `CapturingSink` — records all dispatched events; provides `count()`, `has_note_on()`, `has_note_off()`, `first()` queries.

Test files go in `tests/unit/` and `tests/integration/`. Add new `.cpp` files to `tests/CMakeLists.txt` (currently all commented out pending implementation). Benchmarks (`tests/benchmarks/`) use Catch2's `BENCHMARK` macro and are not run in CI.

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
