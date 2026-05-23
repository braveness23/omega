# Omega

[![CI](https://github.com/braveness23/omega/actions/workflows/ci.yml/badge.svg)](https://github.com/braveness23/omega/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)

A C++ sequencing engine and MIDI library. The foundation for building serious music software.

Omega is a library, not an application. It handles timing, event scheduling, multi-track and pattern sequencing, and live performance control — with no opinion about what the interface looks like or what protocols it speaks.

**Current status**: Active development — M4 complete (v0.4.0). M5 (MIDI I/O, SMF import/export) in progress. See [docs/STATUS.md](docs/STATUS.md).

---

## What Makes Omega Different

Every DAW solves sequencing for one use case: linear tracks, tape-deck model. Nothing in the open source ecosystem provides an embeddable sequencer engine that covers all three classic paradigms — and then opens the door to virtually any sequencer ever conceived:

- **Timeline** — linear multi-track recording, the classic model
- **Pattern** — named loopable sequences, chainable into arrangements
- **Performance** — live cuing with real-time transpose, velocity scaling, and probabilistic variation

Beyond the three built-in modes, Omega's orchestration layer enables the full range of sequencer architectures:

- **Reactive / generative** — incoming events (MIDI, OSC) become source material via `EventInput`; generative sources produce events procedurally with no stored data
- **Modulated** — `ModulationBus` carries continuous parameter control (LFOs, envelopes, step modulators) readable by any source each cycle
- **Scale/chord-aware** — `PerformanceContext` provides shared musical state (scale, chord, groove) that multiple sources read without being wired to each other
- **Composable** — `TransformSource` composes sources into processing chains (quantize → humanize → chord-spread) without a separate patch graph
- **Chasing** — transport locate correctly reconstructs note, CC, and program state for any source that supports it

---

## Design

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full architecture overview and links to all design documents.

See [PROPOSAL.md](PROPOSAL.md) for the original vision document.

Key decisions at a glance:
- **C++17** core with a **stable C API** (`extern "C"`) for cross-language bindings
- **480 PPQN** tick resolution, **nanosecond** wall-clock precision
- **Caller-driven** engine with a **lock-free command queue** — no hidden threads
- **`std::pmr`** for swappable allocators — works on embedded targets without a heap
- **MIT license** — permissive for commercial and open source use
- **MIDI via libremidi** (MIT), **SMF via midifile** (BSD), **Ableton Link optional** (GPL v2+)

---

## Requirements

- C++17 compiler: GCC 10+, Clang 11+, MSVC 2019+
- CMake 3.16+
- Platform MIDI support: ALSA (Linux), CoreMIDI (macOS), WinMM (Windows)

---

## Building

```bash
git clone https://github.com/braveness23/omega.git
cd omega
cmake -B build -DOMEGA_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

Optional Ableton Link support (changes license to GPL v2+):
```bash
cmake -B build -DOMEGA_WITH_LINK=ON
```

---

## License

MIT — see [LICENSE](LICENSE).

When built with `OMEGA_WITH_LINK=ON`, the combined work is GPL v2+.
See [docs/design/07-extensions.md](docs/design/07-extensions.md) for details.

---

*In memory of Emile Tobenfeld.*
