# Idea Document: OmegaKit — Developer Harness for Omega Applications

## Context

Omega is a rock-solid sequencer engine with a stable C API, but today a developer who wants to build an application on top of it must independently solve: MIDI device discovery and hotplug, a project file format, app lifecycle and thread setup, UI state binding, real-time safety patterns, and platform portability. Every Omega application re-solves these same problems from scratch.

OmegaKit is a "batteries included" layer above Omega — a collection of libraries, UI widget sets, example applications, and language bindings that give developers a working skeleton from day one, without locking them into a single UI framework or platform. The goal is to lower the barrier from "I have Omega as a dependency" to "I have a running application" to a single afternoon.

No code will be written. This is a pure ideas document.

---

## The Gaps OmegaKit Fills

| What a real app needs | What Omega provides today | What OmegaKit would add |
|---|---|---|
| Start the engine, wire up threads | `omega_engine_create()` + manual process loop | `AppContext` scaffold — one call to start |
| MIDI port discovery and hotplug | `omega_sink_create_midi_out()` (knows port name) | `DeviceManager` — enumerate, persist by name, detect reconnect |
| Save and reopen a project | SMF import/export (MIDI content only) | `.omega` project file — names, colors, sequences, all state |
| Undo/redo | `Command` system exists, no history manager | `CommandHistory` — undo stack, redo stack, dirty flag |
| Map a MIDI CC to a parameter | Nothing | `MidiLearn` — right-click any param, assign CC, persist mapping |
| Draw a transport bar | Nothing | `TransportWidget` |
| Draw a piano roll | Nothing | `PianoRollWidget` |
| Display note C4 instead of 60 | Nothing | `NoteNames` helper |
| Format tick as 2:3:120 | Nothing | `TickFormat` helper |
| Ship on Windows, macOS, Linux, Web | Engine builds on all | OmegaKit build recipes + WASM target for browser |
| Expose Omega to Python / Rust / JS | C API exists | Official thin wrappers per language |

---

## Architecture: À la Carte, Not Monolithic

OmegaKit is **not** a monolithic framework. A developer picks only the layers they need. Each layer is independent.

```
┌─────────────────────────────────────────────────────────┐
│                    Your Application                     │
├────────────────────┬────────────────────────────────────┤
│  omega-ui-imgui    │  omega-ui-juce   │  omega-ui-web   │  ← pick one (or none)
├────────────────────┴──────────────────┴─────────────────┤
│              omega-app  (framework-agnostic C++)         │  ← device mgr, project file, app lifecycle
├─────────────────────────────────────────────────────────┤
│              omega-helpers  (header-only C++)            │  ← NoteNames, TickFormat, ScaleHelper, ...
├─────────────────────────────────────────────────────────┤
│                    Omega  (omega.h C API)                │  ← the engine, untouched
└─────────────────────────────────────────────────────────┘
```

A CLI tool uses only `omega-helpers`. A full desktop DAW uses all four layers. A VST plugin uses `omega-app` + `omega-ui-juce`. A browser tool uses `omega-ui-web` over a WASM build.

---

## Layer 1: omega-helpers (Header-Only C++)

Zero dependencies beyond the C standard library. Can be dropped into any project.

### NoteNames
- `note_name(60)` → `"C4"`
- `note_number("D#3")` → `63`
- Configurable middle-C convention (C3 vs C4)
- Enharmonic spelling (sharps or flats)

### TickFormat
- `format_bar_beat(tick, cursor)` → `"2:3:120"` (bar:beat:subdivision)
- `format_minutes_seconds(ns)` → `"1:34.820"`
- `format_smpte(ns, config)` → `"00:01:34:20"`
- Round-trip parse from any format string back to ticks

### BpmCalc
- Tap tempo: feed N timestamps, get BPM with confidence score
- `bpm_to_tick_duration(bpm, ppqn)` — ticks per beat at a given BPM
- `ticks_to_ms(ticks, tempo_map)` — convert ticks to milliseconds using the session's tempo map

### ScaleHelper
- Build a scale mask from root + mode name ("Dorian", "Phrygian", "Whole Tone", etc.)
- `notes_in_scale(mask, octaves)` → vector of MIDI notes
- `nearest_scale_note(note, mask)` → quantize a note to the nearest scale degree
- Chord voicing builder: root + type → `omega_chord_t` struct

### QuantizePresets
- Named groove templates: "Straight", "Shuffle 50%", "Shuffle 67%", "MPC Swing", "Human"
- Each preset is an `omega_groove_id_t` ready to pass to `omega_ctx_set_groove()`
- Load additional templates from JSON or from SMF groove tracks

### MidiEventBuilder
- Friendly constructors: `make_note_on(channel, note, velocity, duration_ticks)` → `omega_event_t`
- Builder chains: `Event::cc(channel, controller, value).at(tick)`
- Validation: catch out-of-range values at the call site, not inside the engine

---

## Layer 2: omega-app (Framework-Agnostic C++ Library)

The core application infrastructure. No UI dependency — works headless or with any UI toolkit.

### AppContext
The single entry point for bootstrapping an Omega application.

```
AppContext ctx;
ctx.set_sample_rate(44100);   // or use built-in OmegaTimer
ctx.set_clock(my_clock);      // optional; defaults to OmegaTimer
ctx.start();                  // starts timing thread
ctx.stop();
```

Internally: creates engine, registers default built-in sources, starts the process loop on the timing thread, manages thread lifetime, handles graceful shutdown.

AppContext exposes the engine handle for direct C API calls when needed, but most common operations have ergonomic wrappers.

### DeviceManager
MIDI hardware enumeration and lifecycle. Hotplug-aware.

- `list_inputs()` / `list_outputs()` — available ports by name + index
- `open_input(name)` / `open_output(name)` — match by display name; fallback to index if renamed
- `persist(name)` — save preferred port name to config; restore on next launch even if port index changed
- `on_device_change(callback)` — fires when a USB MIDI device connects or disconnects
- Virtual ports: `create_virtual_input("OmegaKit In")` / `create_virtual_output("OmegaKit Out")`
- Routing matrix: map any input to any track, any track to any output — stored as named connections

### ProjectFile
A JSON-based project container that preserves everything SMF cannot.

**What it stores:**
- Session metadata: name, tempo map, time signature map, loop/punch region
- Track list: name, color, MIDI output (by device name + channel), mute state, per-track transpose, velocity offset
- Pattern library: all patterns with names
- Song arrangement: entry list (pattern + repeat count)
- Performance slots: assignments, cue mode defaults, per-slot parameters
- Sequence list: up to N named sequences, each a full track content snapshot
- PerformanceContext snapshot: current scale, chord, groove, chaos, swing
- ModulationBus named channel assignments and initial values
- Marker list, region list
- Device routing matrix
- MidiLearn mappings

**File format:** `.omega` — human-readable JSON, version field for migration. MIDI content embedded as base64-encoded SMF per track (avoids re-inventing event storage).

**Operations:** `save(path)`, `load(path)`, `recent_files()`, `auto_save(interval)`.

### CommandHistory
Undo/redo stack built on top of Omega's `Command` system.

- `push(command)` — executes and records
- `undo()` / `redo()` — rewinds or reapplies
- `dirty()` — true if unsaved changes exist
- `checkpoint(name)` — named save points (named undo levels)
- Thread-safe: mutations always go through the SPSC queue; CommandHistory is the mutation-thread wrapper

### MidiLearn
Real-time CC-to-parameter mapping.

- `arm(param_id)` — next incoming CC becomes the mapping for this param
- `disarm()` — cancel learn mode
- `map(cc, channel, param_id, range_min, range_max)` — manual mapping
- `unmap(param_id)`
- On each `InputBus` cycle, reads active mappings and enqueues corresponding commands
- Mappings persist in the ProjectFile

### ThreadSafetyChecker (debug builds only)
Development-time assertions that catch wrong-thread access.

- Annotate functions as `OMEGA_TIMING_THREAD_ONLY` or `OMEGA_MUTATION_THREAD_ONLY`
- In debug builds, assert the calling thread matches expectations
- Catches bugs before they become data races in production
- Zero overhead in release builds (assertions compile out)

---

## Layer 3: UI Widget Libraries

### omega-ui-imgui (Dear ImGui)

Immediate-mode widgets. Great for tools, editors, and embedded UIs. Requires an ImGui backend (OpenGL, Metal, DX11, WebGPU — all work).

**Why ImGui for the default?**
- Single-header, embeds anywhere
- Same code renders on desktop, browser (via Emscripten), and Raspberry Pi
- No license headaches (MIT)
- Loved by the tools-and-engines crowd
- Excellent for "I want a working UI in a day" workflows

**Widget set:**

| Widget | What it shows |
|---|---|
| `TransportWidget` | Play/stop/record buttons, position display, tempo, loop toggle |
| `TrackHeaderWidget` | Name, MIDI ch, color swatch, mute/solo/arm toggles, transpose, velocity offset |
| `PianoRollWidget` | Scrollable/zoomable note grid, drag to create/move/resize events |
| `EventListWidget` | Tabular view of all events; click-to-edit; filter by type |
| `StepGridWidget` | 16/32/64-step grid; per-step: on/off, note, velocity, length |
| `PerfPadWidget` | 4×4 or 8×8 pad grid; each pad → a PerformanceSource slot; color reflects state (IDLE/QUEUED/PLAYING) |
| `SequenceLauncherWidget` | N named sequence slots; click to switch; active sequence highlighted |
| `DevicePickerWidget` | Dropdown of available MIDI ports; shows connected/disconnected status |
| `ChordWidget` | Root + mode picker; chord type; outputs to `omega_ctx_set_scale()` / `set_chord()` |
| `ModBusWidget` | Read-only table of all 256 ModulationBus channels and live values |
| `MixerWidget` | Per-track sliders: velocity scale, transpose, random bias |
| `QuantizePanel` | Grid resolution, strength slider, groove picker, apply button |
| `MarkerBar` | Horizontal ruler showing markers and regions; click to locate |
| `PianoKeyboardWidget` | 88-key keyboard; highlights active notes; click to send note-on |
| `MidiMonitorWidget` | Scrolling real-time log of incoming and outgoing MIDI events |

### omega-ui-juce (JUCE Components)

The same conceptual widget set as ImGui, but as `juce::Component` subclasses. Enables shipping as a VST3 / AU / CLAP plugin with Omega as the sequencing engine inside.

- Inherits JUCE's look-and-feel system so widgets match the host DAW aesthetic
- `OmegaAudioProcessor` base class: wires JUCE's `processBlock()` to `omega_engine_process()`
- Handles JUCE's real-time constraints (no allocation, no locking in the audio callback)
- JUCE's `ValueTree` → OmegaKit ProjectFile bridge for DAW state save/restore

Requires JUCE license (GPL or commercial). Kept as a separate optional module so GPL doesn't contaminate the core.

### omega-ui-web (WASM + WebMIDI)

Compiles Omega + omega-app to WebAssembly via Emscripten. JavaScript/TypeScript bindings via Embind.

**What it enables:**
- Run Omega entirely in the browser — no install, no plugins
- WebMIDI API provides hardware MIDI I/O (Chrome/Edge; Firefox via extension)
- Suitable for: browser-based pattern editors, educational tools, web DAWs, live performance tools

**JS/TS API (thin wrapper over WASM):**
```typescript
const engine = await OmegaKit.create();
const track = engine.addTrack();
engine.addEvent(track, { tick: 0, note: 60, velocity: 100, duration: 480 });
engine.play();
```

**UI components (Web):**
- Framework-agnostic vanilla JS widgets (no React/Vue dependency)
- Optional React wrapper package for teams using React
- Same widget concepts as ImGui/JUCE: PianoRoll, StepGrid, TransportBar, PerfPads

---

## Layer 4: Example Applications

Shipped as part of OmegaKit, built with `OMEGAKIT_BUILD_EXAMPLES=ON`.

### omega-cli (No UI)
A command-line sequencer. Load an `.omega` project, play it, record to SMF. Demonstrates omega-app-core without any GUI. Good first example — small, no UI framework required.

```
omega-cli --project my_song.omega --play
omega-cli --import song.mid --output recorded.omega
```

### omega-track (ImGui Desktop App)
A KCS Track Mode clone (see `kcs-track-mode.md`) built with ImGui. The flagship demo application. Multi-track linear sequencer, real MIDI I/O, event list editor, piano roll, sequence launcher. Runs on macOS, Windows, Linux.

### omega-perform (ImGui Desktop App)
A performance-focused launcher. Full-screen 8×8 pad grid backed by PerformanceSource. Cue patterns live, adjust transpose/velocity per pad, switch scales mid-performance. Think Ableton Session View, distilled.

### omega-plugin (JUCE Plugin)
A VST3 / AU / CLAP plugin that puts Omega's full sequencer engine inside any DAW. The host provides the audio clock; Omega provides the pattern/performance sequencing. Requires JUCE.

### omega-web-demo (Browser)
A minimal web application: load an SMF file, display it in a piano roll, play it back via WebMIDI. No install. Ships as a GitHub Pages demo.

---

## Language Bindings

Omega's C API makes bindings straightforward. OmegaKit ships official thin wrappers.

### Python (omega-py)
- CFFI or ctypes binding — auto-generated from `omega.h` with a short handwritten ergonomics layer
- Ideal for: algorithmic composition scripts, education, rapid prototyping
- `pip install omega-sequencer`
- Example: generate a Euclidean rhythm pattern and play it back in ten lines of Python

### Rust (omega-rs)
- `bindgen`-generated unsafe bindings + a safe idiomatic wrapper
- Pattern: `engine.add_track()` returns a `Track` handle; drop triggers `omega_track_destroy()`
- Crates.io package

### JavaScript / TypeScript (omega-js)
- The WASM build exposed via the omega-ui-web module
- Also available as a standalone npm package (`npm install @omega-sequencer/core`) for Node.js use (e.g., generative music scripts, server-side MIDI rendering)

---

## Project File Format: `.omega`

A dedicated format is the linchpin of OmegaKit. Without it, every app invents its own.

**Design principles:**
- JSON at the top level — human-readable, git-diffable, no binary format headaches
- MIDI content stored as base64-encoded SMF blobs per track — reuses Omega's SMF infrastructure
- Versioned: `"format_version": 1` field; ProjectFile migrates on load
- Forward-compatible: unknown fields are preserved on round-trip (apps don't corrupt each other's data)

**Rough schema sketch:**
```json
{
  "format_version": 1,
  "name": "My Song",
  "bpm": 120000,
  "tempo_map": [ { "tick": 0, "bpm_milli": 120000 } ],
  "time_signatures": [ { "tick": 0, "num": 4, "denom": 4 } ],
  "tracks": [
    {
      "id": 1, "name": "Drums", "color": "#E05C5C",
      "midi_device": "Arturia KeyStep", "midi_channel": 10,
      "muted": false, "transpose": 0, "velocity_offset": 0,
      "events_smf_b64": "..."
    }
  ],
  "patterns": [ { "id": 1, "name": "Verse Drums", "length": 1920, "events_smf_b64": "..." } ],
  "song_arrangement": [ { "pattern_id": 1, "repeat_count": 4 } ],
  "sequences": [
    { "name": "Verse", "track_snapshot": [...], "mute_states": {...} }
  ],
  "performance_slots": [
    { "slot": 0, "pattern_id": 2, "transpose": 0, "velocity_scale": 100, "random_bias": 0 }
  ],
  "performance_context": { "scale_root": 0, "scale_mask": 2741, "chord_type": "none", "swing": 0.0 },
  "modulation_bus": [ { "name": "lfo1", "channel": 0, "value": 0.5 } ],
  "markers": [ { "tick": 0, "name": "Intro" } ],
  "regions": [ { "start": 0, "end": 7680, "name": "Verse", "type": "section" } ],
  "midi_learn": [ { "cc": 7, "channel": 1, "param": "track_1_velocity_offset" } ],
  "device_routing": [ { "input": "Arturia KeyStep", "track": 1 } ]
}
```

---

## Cross-Platform Build Strategy

OmegaKit follows Omega's CMake pattern. One build system, all platforms.

| Platform | Core + Helpers | ImGui UI | JUCE UI | Web/WASM |
|---|---|---|---|---|
| macOS | ✓ native | ✓ Metal backend | ✓ CoreAudio/CoreMIDI | ✓ Emscripten |
| Windows | ✓ native | ✓ DX11 backend | ✓ WASAPI/WinMM | ✓ Emscripten |
| Linux | ✓ native (ALSA) | ✓ OpenGL backend | ✓ ALSA/JACK | ✓ Emscripten |
| Raspberry Pi | ✓ native (ALSA) | ✓ OpenGL ES | ✗ (no JUCE ARM prebuilt) | ✓ Emscripten |
| Browser | ✗ | ✗ | ✗ | ✓ WebMIDI + WebAssembly |

FetchContent declarations for ImGui, Emscripten toolchain files, and JUCE are provided in `cmake/` so consumers don't have to hunt for them.

---

## What OmegaKit Is Not

To keep scope clear:

- **Not a DAW**: no audio recording, no VST hosting in the core (the JUCE plugin example hosts inside a DAW, but OmegaKit itself doesn't host plugins)
- **Not an alternative to Omega**: it sits above Omega, never forks it or patches the engine
- **Not a mandatory framework**: every layer is optional; you can use just the helpers
- **Not opinionated about UI aesthetics**: widgets are functional, not styled — developers apply their own theme

---

## Open Questions for Future Brainstorming

- Should omega-ui-imgui ship a default theme/stylesheet, or leave styling entirely to the developer?
- Should the `.omega` project file embed SMF blobs, or store events natively (re-implementing SMF)?
- Is a Raspberry Pi headless target (omega-cli as a hardware sequencer) worth its own example?
- Should omega-py target CPython only, or also MicroPython for embedded use?
- How does OmegaKit version independently of Omega? Lock to an Omega minor version, or use a compatibility table?
- Should `DeviceManager` have a built-in MIDI monitor UI, or leave that to the app?
- Is there appetite for an omega-max or omega-pd (Max/MSP or Pure Data external) binding?
