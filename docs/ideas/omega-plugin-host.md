# Idea Document: OmegaHost — Plugin Hosting with Flexible Audio, MIDI, and CV Routing

## Context

Omega is a MIDI sequencer engine — it generates and schedules events, but it produces no audio. To drive real instruments and effects, those events must reach plugin instances: synthesizers, samplers, processors. Today a developer who wants to run a VST3 instrument from an Omega sequence must write all of that themselves.

OmegaHost is a companion library that adds plugin hosting and a flexible routing graph to Omega. It does three things:

1. **Host plugins** — load VST3, CLAP, AU, LV2, and other formats; manage their lifecycle
2. **Route signals** — connect plugin audio, MIDI, and CV ports to each other and to hardware in a patchbay-style graph
3. **Bridge Omega** — expose `OutputSink` and `EventInput` adapters so Omega's sequencer drives plugin instruments and receives plugin-generated events, with Omega's `ModulationBus` feeding plugin parameter automation as CV lanes

No code will be written. This is a pure ideas document.

---

## Plugin Formats to Support

| Format | Status | Platforms | Notes |
|---|---|---|---|
| **CLAP** | Primary target | Mac, Win, Linux | Open source (MIT), modern design, built for DAW/host use cases, active community |
| **VST3** | Secondary target | Mac, Win, Linux | Steinberg free SDK, most widely installed format |
| **Audio Unit (AU)** | macOS only | macOS | System-native; required for many major instruments on Mac |
| **LV2** | Linux/macOS | Linux, macOS | Open standard; dominant on Linux |
| **VST2** | Legacy only | Mac, Win, Linux | SDK no longer distributed; support via wrapper or plugin adapters only |
| **AAX** | Out of scope | Pro Tools only | Closed SDK, Pro Tools exclusive; not worth targeting |

CLAP is the right primary focus: it is open, well-documented, and designed explicitly with hosting in mind. Many developers are releasing CLAP alongside VST3 today.

---

## The Core Concept: A Signal Graph

OmegaHost introduces a **signal graph** — a directed acyclic graph of nodes connected by typed edges.

### Node types

| Node type | What it represents |
|---|---|
| `PluginNode` | A loaded plugin instance (instrument or effect) |
| `AudioInputNode` | Hardware audio input (microphone, line-in) |
| `AudioOutputNode` | Hardware audio output (speakers, interface) |
| `MidiInputNode` | Hardware MIDI input device (or Omega EventInput) |
| `MidiOutputNode` | Hardware MIDI output device (or Omega OutputSink) |
| `MixerNode` | Sums multiple audio streams; per-input gain, pan |
| `SplitterNode` | Duplicates one audio stream to multiple destinations |
| `CvSourceNode` | Emits a CV signal (Omega ModulationBus channel, LFO, etc.) |
| `CvSinkNode` | Receives a CV signal and maps it to a plugin parameter |
| `RecorderNode` | Writes audio to disk (WAV/AIFF) |
| `ReturnNode` | Audio send/return pair for parallel processing |

### Edge types (signal types)

| Signal type | Rate | Data | Description |
|---|---|---|---|
| **Audio** | Sample-rate | `float[]` per channel, per buffer | PCM audio; carries stereo, mono, surround, or N-channel |
| **MIDI** | Event-rate | `MidiEvent` queue per buffer | Note on/off, CC, program, pitch bend, etc. |
| **CV (control-rate)** | Buffer-rate | Single `float` per buffer | Slow modulation: filter cutoff, reverb size, tempo-synced LFO |
| **CV (audio-rate)** | Sample-rate | `float[]` same as audio | Fast modulation: FM carrier, VCO pitch, wavetable scan |

Audio-rate CV is physically the same type as an audio connection — it's just a semantic label that tells the graph this buffer is modulation, not sound. This mirrors how hardware modular synthesis works.

---

## How OmegaHost Connects to Omega

Omega's engine and OmegaHost's audio graph share the same process cycle. Two integration points exist:

### 1. Omega → Plugin (OutputSink bridge)

```
TimelineSource / PerformanceSource
         ↓  (omega_event_t)
  PluginMidiSink  [implements OutputSink]
         ↓  (translated to plugin MIDI buffer)
  PluginNode (instrument)
         ↓  (audio out)
  MixerNode → AudioOutputNode
```

`PluginMidiSink` is an `OutputSink` implementation that, instead of sending bytes to a hardware MIDI port, queues `omega_event_t` values for delivery to a plugin instance at the next audio callback. It translates Omega's event model to the plugin format's MIDI buffer format (VST3 event list, CLAP event list, etc.).

Multiple tracks can route to the same plugin on different MIDI channels. Multiple plugins can receive from the same Omega track (fan-out).

### 2. Plugin → Omega (EventInput bridge)

```
PluginNode (arpeggiator, generative plugin)
         ↓  (plugin MIDI output buffer)
  PluginMidiInput  [implements EventInput]
         ↓  (omega_event_t)
  InputBus → all Omega EventSources
```

`PluginMidiInput` is an `EventInput` implementation that drains a plugin's MIDI output buffer each cycle and injects those events into the `InputBus`. This lets a plugin arpeggiator, chord generator, or generative plugin feed back into Omega's processing pipeline — triggering other tracks, modulating the PerformanceContext, etc.

### 3. Omega ModulationBus → CV lanes → Plugin parameters

```
LfoSource (writes to ModulationBus ch 0)
         ↓  (float, once per buffer)
  CvSourceNode (reads ModulationBus ch 0)
         ↓  (CV edge)
  CvSinkNode (mapped to PluginNode param: "Filter Cutoff")
         ↓  (plugin param update, real-time safe)
  PluginNode
```

Omega's `ModulationBus` is already a 256-channel control-rate float bus. `CvSourceNode` wraps any ModulationBus channel and exposes it as a CV edge in the graph. `CvSinkNode` consumes that edge and writes the value to a plugin parameter index each buffer. This gives Omega's full modulation pipeline — LFOs, step modulators, envelope followers — direct access to plugin parameters without any additional infrastructure.

### 4. Plugin audio-rate CV → Omega ModulationBus

For plugins that generate audio-rate modulation signals (VCO, envelope generators used as CV sources in a modular context), a `CvAudioSinkNode` downsamples the audio-rate signal to control-rate and writes it back into a ModulationBus channel. This closes the loop: audio-rate plugin CV → control-rate Omega modulation → other plugin parameters.

---

## The Unified Process Cycle

OmegaHost owns the audio callback. The audio thread is the master clock.

Each audio buffer cycle:

```
1. Compute current tick range from sample position + TempoMap
2. Call omega_engine_process(to_tick)           ← Omega advances, fills PluginMidiSink queues
3. Topological sort of audio graph (cached)
4. For each node in topological order:
   a. PluginMidiSink nodes: deliver queued MIDI events to plugin input buffer
   b. PluginNode: call plugin process() — consumes MIDI in, produces audio out
   c. MixerNode, SplitterNode: sum / copy audio buffers
   d. CvSourceNode: read ModulationBus, publish float to CV edges
   e. CvSinkNode: read CV edge, call plugin setParameter()
   f. RecorderNode: append audio to file ring buffer
   g. AudioOutputNode: write to hardware output buffer
5. PluginMidiInput nodes: drain plugin MIDI output → InputBus for next Omega cycle
6. Audio-rate CV downsampling → ModulationBus (for next cycle)
```

This single-pass cycle guarantees that Omega's sequencer output reaches plugins within the same audio buffer — no extra latency from inter-thread communication.

---

## Plugin Node in Detail

### Lifecycle

```
discover() → load(path) → instantiate(sample_rate, block_size) → activate()
→ [process cycle: process(audio_in, audio_out, midi_in, midi_out, params)]
→ deactivate() → destroy()
```

The host manages this lifecycle. A plugin is not activated until it has a complete set of connections (all required audio inputs connected). Partial graphs are valid during editing; the host inserts silence for disconnected inputs.

### Audio buses

Plugins declare their bus layouts (mono, stereo, N-channel). The host negotiates bus format at instantiation time. If a stereo plugin connects to a mono bus, the host inserts a format-conversion node automatically — the graph stays type-safe.

### Parameters

Every plugin parameter is addressable by index and optionally by name. Parameters are:
- **Automatable**: can receive CV input from the graph
- **MIDI-learnable**: via OmegaKit's `MidiLearn` (if OmegaKit is also in use)
- **Gestureable**: the host sends `beginEdit()` / `endEdit()` notifications around user gestures (required by VST3/CLAP for DAW automation recording)
- **Snapshotable**: entire parameter state is saved as a preset blob in the project file

### Plugin state

Each plugin instance has a state blob (binary, plugin-owned format). The host saves and restores this blob as part of the project file. On load, the host reinstantiates the plugin and calls `setState(blob)` before activating. If the plugin is missing (uninstalled), the host stores the blob and warns the user rather than discarding state.

### Sandboxing (future)

Poorly written plugins crash their host. The forward-looking idea: run each plugin in a subprocess (or at minimum a separate thread with a watchdog). Audio is shared via lock-free ring buffers or shared memory. If the plugin process dies, the host substitutes silence and reports an error. This is optional and expensive — initial versions skip it.

---

## The Routing Graph: Patchbay Metaphor

The graph is edited in a **patchbay view** — a canvas where nodes are boxes and edges are cables. The UX concept:

- Nodes have **input ports** (left side) and **output ports** (right side)
- Ports are color-coded by signal type: **blue** = audio, **yellow** = MIDI, **orange** = CV (control-rate), **red** = CV (audio-rate)
- Drag from an output port to an input port to create a connection
- Incompatible types are rejected at connection time (audio cannot connect to MIDI)
- Audio-rate CV → audio input is allowed (explicit "use as CV" toggle on the destination port)
- A port can have multiple incoming connections (implicit mix) or multiple outgoing connections (fan-out)
- Cycles are detected and rejected

The graph is separate from the track-based linear timeline view. Think of it as the "wiring room" underneath the sequencer.

---

## Flexible Routing: Key Scenarios

### Scenario 1: Classic instrument chain
```
Omega Track 1 (MIDI ch 1) → Surge XT (synth) → Reverb plugin → Master Mix → Audio Out
```
One track, one instrument, one effect, out to speakers. The simplest case.

### Scenario 2: Multi-timbral instrument
```
Omega Track 1 (MIDI ch 1) ─┐
Omega Track 2 (MIDI ch 2) ─┤→ Kontakt (multi-timbral) → Audio Out
Omega Track 3 (MIDI ch 3) ─┘
```
Three Omega tracks, all MIDI channels routed into one plugin instance on different channels. Kontakt handles its own internal mixing.

### Scenario 3: Parallel effect processing
```
Drum synth plugin → Splitter ─→ Compressor → ─┐
                              └→ Distortion → ─┤→ Mixer → Audio Out
                                               (dry blend)
```
The splitter fans the drum audio to two effect chains; the mixer recombines.

### Scenario 4: ModulationBus → plugin parameter
```
Omega LfoSource (sine, 0.5Hz) → ModBus ch 0
ModBus ch 0 → CvSourceNode → [CV edge] → CvSinkNode → Moog Model D "Filter Cutoff"
```
The LFO from Omega's modulation pipeline controls a plugin filter in real time.

### Scenario 5: Generative plugin feeds Omega
```
Omega PerformanceSource (plays chord) → [MIDI] → Arpeggiate plugin
Arpeggiate plugin MIDI out → PluginMidiInput [EventInput] → InputBus
InputBus → custom Omega EventSource (triggers pads based on input)
```
A plugin arpeggiator generates note patterns; those notes feed back into Omega's performance system to trigger pad slots.

### Scenario 6: CV-driven modular patching
```
Omega ModBus ch 1 (step sequencer values) → CvSourceNode → [CV edge]
                                                            → [split]
                                                              → VCO "Pitch" (audio-rate, via CV plugin)
                                                              → Filter "Cutoff" (control-rate)
                                                              → VCA "Gain" (control-rate)
```
A step sequencer in Omega drives pitch, filter, and amplitude of a modular-style signal chain simultaneously.

---

## CV Routing in Depth

CV (Control Voltage) is the lingua franca of modular synthesis. In the analog world, CV is a voltage that controls a parameter. In software, CV is a float signal.

### Two rates

**Control-rate CV** (one float per buffer):
- Updated once every audio buffer (~5ms at 44100/256)
- Sufficient for: filter cutoff, reverb size, tempo sync, volume automation
- Maps directly to Omega's ModulationBus
- Low CPU cost

**Audio-rate CV** (one float per sample):
- Updated every sample (~22μs at 44100Hz)
- Required for: FM synthesis, wavetable scan, pitch modulation at audio rates
- Same data type as an audio buffer — just semantically labeled as CV
- Higher CPU cost; use only when the modulation must be audio-rate

### CV sources in OmegaHost

| Source | Type | Description |
|---|---|---|
| Omega ModulationBus channel | Control-rate | LFOs, step modulators, envelopes from Omega |
| Plugin audio output (flagged as CV) | Audio-rate | Plugin acts as a CV generator (oscillator, envelope) |
| Hardware CV input (audio interface) | Audio-rate | Real 1V/octave CV from modular hardware, via audio interface |
| MIDI CC (via EventInput) | Control-rate | Incoming CC values promoted to CV |
| Host automation lane | Control-rate | Drawn automation curve, evaluated per buffer |

### CV sinks in OmegaHost

| Sink | Type | Description |
|---|---|---|
| Plugin parameter index | Control or audio-rate | Writes float value to plugin parameter |
| Omega ModulationBus channel | Control-rate | Closes the loop from plugin back to Omega |
| Hardware CV output (audio interface) | Audio-rate | Sends CV to real modular hardware |
| MIDI CC out | Control-rate | Converts CV to outgoing CC messages (for hardware without CV) |

### Scaling and mapping

Raw CV values need scaling before they reach a plugin parameter. Every `CvSinkNode` has:
- **Input range**: expected CV range (e.g., 0.0–1.0 from ModBus, or -5V to +5V from hardware)
- **Output range**: target parameter range (e.g., 20Hz–20kHz for filter cutoff)
- **Curve**: linear, exponential, or user-drawn curve (important for pitch: 1V/octave = exponential)
- **Slew limiter**: optional sample-and-hold or portamento on the CV value (smooths sudden jumps)

---

## Plugin Discovery and Management

### Scanning
On first launch (or user request), OmegaHost scans standard plugin paths per platform:
- macOS: `~/Library/Audio/Plug-Ins/VST3`, `~/Library/Audio/Plug-Ins/Components` (AU), `/Library/Audio/Plug-Ins/CLAP`
- Windows: `C:\Program Files\Common Files\VST3`, `C:\Program Files\Common Files\CLAP`
- Linux: `~/.clap`, `/usr/lib/clap`, `~/.vst3`

Scan results are cached in a plugin database (SQLite or JSON). The database stores: name, vendor, version, format, path, capability flags (instrument vs. effect, MIDI out, sidechain, etc.).

### Plugin browser
A searchable, filterable list of all discovered plugins. Columns: name, vendor, format, type (instrument/effect/analyzer), favorite flag. Drag from browser onto the graph canvas to instantiate.

### Blacklisting
Plugins that crash during scan are blacklisted automatically. The blacklist is editable. A "rescan single plugin" option lets developers test fixes.

---

## Audio Device Management

OmegaHost needs an audio I/O layer independent of MIDI. Options:

- **PortAudio** (MIT) — lowest common denominator, works everywhere, not the lowest latency
- **RtAudio** (MIT) — similar to PortAudio, slightly more modern
- **libsoundio** (MIT) — well-regarded, cross-platform
- **JUCE AudioDeviceManager** — if JUCE is already present (via OmegaKit); don't duplicate
- **Core Audio direct** (macOS), **WASAPI direct** (Windows), **JACK / PipeWire** (Linux) — platform-specific, lowest latency

The idea: OmegaHost defines an abstract `AudioDevice` interface (device name, sample rate, block size, channel count, start/stop/callback). Backends implement it per platform. The default backend per platform: Core Audio on macOS, WASAPI on Windows, PipeWire/JACK with ALSA fallback on Linux.

Sample rates and block sizes are negotiated at graph startup. The graph is compiled at a fixed sample rate and block size; changing them requires restarting.

---

## Thread Model

Three threads:

| Thread | Responsibility | Real-time? |
|---|---|---|
| **Audio thread** | Audio callback: Omega process() + graph traversal + plugin process() | Yes — no allocation, no locking |
| **Mutation thread** | Graph edits: add/remove nodes and edges, load/unload plugins, parameter changes from UI | No |
| **UI thread** | Rendering, user input | No |

Between audio thread and mutation thread: a lock-free SPSC queue (same model as Omega's command queue). Graph edits are enqueued as commands; the audio thread applies them at the start of the next buffer using double-buffering of the compiled graph.

The compiled graph (topologically sorted node list + pre-resolved buffer pointers) is swap-buffered: the mutation thread builds a new compiled graph, the audio thread atomically adopts it at a safe boundary.

Plugin `setParameter()` from the UI thread to the audio thread: lock-free parameter queues (one per plugin). No locks ever cross the audio thread boundary.

---

## Latency Considerations

Each plugin in the audio chain may introduce processing latency (declared by the plugin as `latency_samples`). OmegaHost measures the total latency of each path through the graph and applies **latency compensation**: MIDI events destined for a plugin further downstream are pre-delivered by the corresponding number of samples, so all instruments stay in sync at the output.

This is the same latency compensation mechanism every DAW implements. It is non-trivial to implement correctly, especially in graphs with parallel paths of different lengths.

---

## Preset and State Management

### Plugin presets
Each plugin exposes a preset list. OmegaHost caches preset names in the plugin database. Presets are:
- **Factory**: shipped with the plugin, read-only
- **User**: stored by OmegaHost in a per-plugin folder
- **Project**: embedded in the `.omega` project file (or a companion OmegaHost project file)

### Graph snapshots
The entire graph topology (node list, connections, parameter states, plugin state blobs) is a snapshot. Snapshots can be named ("Verse patch", "Bridge patch") and switched live — similar to Omega's sequence concept but for the audio graph.

Switching snapshots while audio plays: nodes present in both snapshots are updated in place (parameter morph). Nodes only in the new snapshot are instantiated first, then activated. Nodes only in the old snapshot are deactivated after the switch.

---

## Project File Integration

OmegaHost adds a section to the `.omega` project file format (from the OmegaKit idea):

```json
{
  "plugin_graph": {
    "nodes": [
      {
        "id": "node_1", "type": "plugin",
        "plugin_id": "com.surge-synth-team.surge-xt",
        "format": "clap", "path": "/usr/lib/clap/surge-xt.clap",
        "state_blob_b64": "...",
        "parameters": [ { "index": 0, "value": 0.5 } ]
      },
      { "id": "node_2", "type": "audio_output", "device": "MacBook Pro Speakers" },
      { "id": "node_3", "type": "mixer", "inputs": 2 }
    ],
    "edges": [
      { "from": "node_1", "from_port": "audio_out_L", "to": "node_3", "to_port": "in_0_L" },
      { "from": "node_1", "from_port": "audio_out_R", "to": "node_3", "to_port": "in_0_R" },
      { "from": "node_3", "from_port": "out_L", "to": "node_2", "to_port": "in_L" }
    ],
    "cv_edges": [
      { "from_modbus_channel": 0, "to_node": "node_1", "to_param_index": 7,
        "scale_in": [0.0, 1.0], "scale_out": [20.0, 20000.0], "curve": "exponential" }
    ],
    "midi_routes": [
      { "from_omega_sink_id": 1, "to_node": "node_1", "midi_channel": 1 }
    ]
  }
}
```

If OmegaHost is not in use, this section is simply absent. The file remains valid for Omega + OmegaKit.

---

## Relationship to Existing Ecosystems

| Existing tool | OmegaHost's relationship |
|---|---|
| **JUCE AudioPluginHost** | Inspiration; JUCE's is tightly coupled to JUCE's app model. OmegaHost is framework-agnostic and Omega-native |
| **Carla** (KXStudio) | Full-featured Linux plugin host; OmegaHost would be lighter and Omega-integrated |
| **VCV Rack** | Modular CV-focused; OmegaHost borrows the CV routing concept and extends it to traditional plugins |
| **Ardour / Mixbus** | Full DAW; OmegaHost is a library, not an app |
| **clap-helpers** | Reference CLAP host implementation; OmegaHost would build on or reference this |

---

## Open Questions for Future Brainstorming

- Should OmegaHost own the audio device, or should it be embeddable inside a JUCE app that already owns the audio callback?
- Audio-rate CV from hardware modular (via audio interface): is 1V/octave tracking a goal, or out of scope?
- Plugin sandboxing (out-of-process): how important is crash isolation vs. latency overhead?
- Should graph snapshots morph parameters smoothly (crossfade) or switch instantly?
- Is MIDI 2.0 (high-resolution, per-note expression) a target format? CLAP supports it; VST3 partially does.
- How deep should the latency compensation go — total output latency only, or per-path compensation in parallel graph branches?
- Should OmegaHost expose a CV hardware I/O layer (Expert Sleepers ES-8, MOTU AVB) for direct modular integration?
- Is there value in an OmegaHost "rack" view (vertical strip per plugin, à la Reason) in addition to the patchbay graph view?
