# Prior Art and Influences

This document records the libraries and projects examined during Omega's design phase. It is a living document — update it when new prior art is studied.

For each entry: what it is, its license, what we borrowed (ideas or code), and what we deliberately chose not to borrow.

---

## Libraries Studied

### RtMidi
**Repo**: https://github.com/thestk/rtmidi
**License**: MIT
**What it is**: The de facto standard for cross-platform real-time MIDI I/O in C++. Abstract backend pattern with platform-specific implementations (ALSA, CoreMIDI, WinMM, JACK).

**Borrowed (ideas)**:
- Abstract backend architecture: `MidiApi` base class with platform subclasses. Omega uses the same pattern for `ClockSource` and `OutputSink`.
- Error callback pattern: optional error callback with `void* userdata`. Omega's C API uses the same signature convention.
- Virtual port support concept (platform-dependent).

**Borrowed (code)**: None directly. Omega wraps libremidi rather than RtMidi.

**Deliberately not borrowed**:
- Exception-based error handling as the primary mechanism. Omega uses error codes (C-compatible).
- Index-based port identification. libremidi's handle-based approach is better.

---

### libremidi
**Repo**: https://github.com/celtera/libremidi
**License**: MIT
**What it is**: A modern C++20 rewrite/fork of RtMidi and ModernMIDI. Handle-based port management, MIDI 2.0 support, nanosecond timestamps, `std::function` callbacks.

**Borrowed (ideas)**:
- Handle-based port opening instead of index-based — handles remain valid after device enumeration changes.
- Nanosecond integer timestamps on MIDI input. Omega's timing model uses nanoseconds throughout.
- Zero-allocation message option for real-time contexts.
- `std::function` callbacks with type safety.

**Borrowed (code)**: Omega uses libremidi as its MIDI I/O dependency. The `MidiOutputSink` and MIDI input handler wrap libremidi directly. Attribution in source files and in this document.

**Deliberately not borrowed**:
- C++20 requirement. Omega targets C++17 to maximize compiler compatibility.
- MIDI 2.0 UMP handling (deferred to Omega v2).

---

### FluidSynth Sequencer
**Repo**: https://github.com/FluidSynth/fluidsynth
**License**: LGPL 2.1+
**What it is**: A software MIDI synthesizer with a built-in sequencer API. The sequencer uses a priority queue sorted by timestamp, with client registration for sources and destinations.

**Borrowed (ideas)**:
- Priority queue for event scheduling (Omega uses a sorted vector for cache efficiency, but the conceptual model of "sort by time, dispatch when due" is the same).
- The observation that when multiple events share a tick, note-offs should fire before note-ons. Omega implements this ordering in its dispatch loop.
- The "process(msec)" manual advancement model. Omega's `engine.process()` is the same concept.
- The warning about relative vs. absolute timestamps — always use absolute ticks internally to avoid accumulated error.

**Borrowed (code)**: None. LGPL is not compatible with MIT for code reuse without dynamic linking obligations.

---

### TSE3 (Trax Sequencer Engine)
**Repo**: https://tse3.sourceforge.net/
**License**: GPL
**What it is**: An older but architecturally thoughtful open-source sequencer engine in C++. No UI. Hierarchical song structure (Phrase → Part → Track → Song). Implemented a Command pattern for undo/redo.

**Borrowed (ideas)**:
- The Command pattern for all mutations: every edit operation has an `execute()` and `undo()`. Omega's `Command` base class and `CommandHistory` directly implement this idea.
- Separation of engine from session data. TSE3 keeps the playback engine cleanly separated from the song data structure. Omega's `Engine` / `Session` split follows this.
- Real-time effects applied at dispatch time (quantize, velocity clipping, transpose) without modifying stored event data.

**Borrowed (code)**: None. GPL is incompatible with MIT.

---

### jdksmidi
**Repo**: https://github.com/jdkoftinoff/jdksmidi
**License**: GPL
**What it is**: A C++ MIDI library with track objects, a sequencer core, and a "process chain" architecture for composable MIDI event handlers.

**Borrowed (ideas)**:
- **Note matrix / stuck-note prevention**: tracking simultaneous note-ons per channel to ensure all notes receive note-off on stop or mode change. Omega's active notes table implements this.
- Process chain concept: composable event handlers that can transform events in sequence. Omega's per-slot real-time parameters (transpose, velocity scale, bias) are applied in a chain before dispatch.
- Track iterators for multi-track event access.

**Borrowed (code)**: None. GPL incompatible with MIT.

---

### midifile (Stanford CCRMA)
**Repo**: https://github.com/craigsapp/midifile
**License**: BSD 2-Clause
**What it is**: A C++ library for reading and writing Standard MIDI Files. Clean two-dimensional array interface: `midifile[track][event]`.

**Borrowed (ideas)**:
- The approach of storing absolute ticks internally and converting to/from delta times only on SMF import/export. Omega never stores delta times internally.
- `joinTracks()` / `splitTracks()` for SMF type 0/1 conversion.
- Automatic binary/ASCII format detection on load.

**Borrowed (code)**: Omega uses midifile as the SMF import/export layer, wrapped in `omega::SmfImporter` and `omega::SmfExporter`. Attribution in source files. BSD license is compatible with MIT.

---

### Ableton Link
**Repo**: https://github.com/Ableton/link
**License**: GPL v2+ (commercial license available from Ableton)
**What it is**: A header-only C++ library for beat, tempo, and phase synchronization across multiple applications and devices on a local network.

**Borrowed (ideas)**:
- The concept of "beat time" as a separate coordinate from wall-clock time, with a mapping between them. Omega's TempoMap is the same conceptual structure.
- Latency compensation: adding output latency to timestamps before comparison. Omega documents this as a responsibility of the `HostClockSource` implementation.

**Borrowed (code)**: `LinkClockSource` wraps Ableton Link directly when `OMEGA_WITH_LINK=ON`. This is an opt-in dependency. When enabled, the combined build is GPL v2+.

**License implications**: Enabling Link in a closed-source product requires a commercial Link license from Ableton. This is documented in the README, the CMakeLists, and in `LinkClockSource`'s header.

---

## Projects Examined and Not Used

**modern-midi** (C++11, MIT): Lighter weight than RtMidi, but less actively maintained. libremidi supersedes it.

**portmidi** (BSD-like): Older, less maintained than RtMidi. Not used.

**Qtractor**: A full DAW application (GPL). Studied for UI architecture ideas; not relevant to Omega's library layer.

**Tidal Cycles** (GPL): Purely functional live coding language. Interesting pattern/cycle model but not applicable to a C++ library.

**Sonic Pi** (MIT): Ruby-based live coding environment. Timing model (absolute scheduling into the future) is consistent with Omega's approach. No code reuse.

---

## License Compatibility Summary

| Library | License | Code Reuse | Idea Borrowing |
|---|---|---|---|
| RtMidi | MIT | ✓ (with attribution) | ✓ |
| libremidi | MIT | ✓ (used as dependency) | ✓ |
| midifile | BSD 2-Clause | ✓ (used as dependency) | ✓ |
| Catch2 | BSL-1.0 | ✓ (test only) | ✓ |
| FluidSynth | LGPL 2.1+ | ✗ (would impose LGPL) | ✓ |
| TSE3 | GPL | ✗ | ✓ |
| jdksmidi | GPL | ✗ | ✓ |
| Ableton Link | GPL v2+ | ✓ (opt-in, changes license) | ✓ |

**Omega core license: MIT.** Omega with Link enabled: GPL v2+.
