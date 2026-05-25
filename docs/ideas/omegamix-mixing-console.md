# Idea Document: OmegaMix — Mixing Console Library Integrating with OmegaHost

## Context

OmegaHost provides a flexible signal graph: instruments and effects connected by audio, MIDI, and CV edges. But a signal graph is not a mixing console. It describes routing topology; it does not provide the abstractions a mixing engineer actually works with — channel strips, faders, aux sends, VCA groups, automation, metering, or a monitoring section.

OmegaMix is a library that adds a professional mixing console layer above OmegaHost. It is not a separate audio engine — it extends the OmegaHost graph with specialized node types and provides a higher-level API and UI that maps to the mental model of a hardware mixing desk. The result is a complete chain:

```
Omega (sequences events)
  → OmegaHost (routes to plugins, produces audio streams)
    → OmegaMix (shapes, balances, meters, and outputs the final mix)
```

No code will be written. This is a pure ideas document.

---

## The Mixing Console Mental Model

A hardware mixing console has a fixed structure every engineer knows. OmegaMix mirrors it faithfully in software so that anyone who has used a desk can pick this up immediately.

```
┌────────────────────────────────────────────────────────────────────────────┐
│  INPUT CHANNELS  (one strip per source)                                    │
│  ┌──────┐  ┌──────┐  ┌──────┐  ┌──────┐       ┌──────┐  ┌──────────────┐ │
│  │ CH 1 │  │ CH 2 │  │ CH 3 │  │ CH 4 │  ...  │ GRP1 │  │    MASTER    │ │
│  │      │  │      │  │      │  │      │       │      │  │              │ │
│  │ GAIN │  │ GAIN │  │ GAIN │  │ GAIN │       │ GAIN │  │  MASTER FADER│ │
│  │  EQ  │  │  EQ  │  │  EQ  │  │  EQ  │       │  EQ  │  │              │ │
│  │ COMP │  │ COMP │  │ COMP │  │ COMP │       │ COMP │  │  METERS      │ │
│  │ SEND │  │ SEND │  │ SEND │  │ SEND │       │ SEND │  │              │ │
│  │  PAN │  │  PAN │  │  PAN │  │  PAN │       │  PAN │  │   MONITOR    │ │
│  │  MUTE│  │  MUTE│  │  MUTE│  │  MUTE│       │  MUTE│  │   SECTION    │ │
│  │ SOLO │  │ SOLO │  │ SOLO │  │ SOLO │       │ SOLO │  │              │ │
│  │FADER │  │FADER │  │FADER │  │FADER │       │FADER │  │              │ │
│  └──────┘  └──────┘  └──────┘  └──────┘       └──────┘  └──────────────┘ │
├────────────────────────────────────────────────────────────────────────────┤
│  AUX RETURNS  (reverb, delay, parallel processing)                         │
└────────────────────────────────────────────────────────────────────────────┘
```

---

## Integration Architecture: Console as OmegaHost Nodes

OmegaMix does not introduce a second audio engine alongside OmegaHost. Instead, it adds specialized node types to the OmegaHost graph:

| OmegaMix abstraction | OmegaHost graph representation |
|---|---|
| Input channel strip | `ChannelStripNode` (receives audio from upstream plugin/instrument) |
| Group bus | `GroupBusNode` (sums assigned channel strips) |
| Aux bus | `AuxBusNode` (sums send contributions; routes to effects return chain) |
| Master bus | `MasterBusNode` (final stereo or surround sum; always one per session) |
| Cue / headphone bus | `CueBusNode` (independent mix for performer or engineer monitor feed) |
| Monitor section | `MonitorSectionNode` (control room speaker management) |
| VCA group | Not an audio node — a metadata layer that links fader values across strips |

The patchbay view in OmegaHost shows these nodes like any other. OmegaMix adds a second **console view** — the traditional horizontal fader layout — as an alternate perspective on the same underlying graph. Both views edit the same data; changes in one appear immediately in the other.

### Typical graph topology

```
[SurgeXT plugin] ──audio──→ [ChannelStripNode: Synth Lead]
[Kontakt plugin] ──audio──→ [ChannelStripNode: Strings]
[DrumPlugin]     ──audio──→ [ChannelStripNode: Kick]
                 ──audio──→ [ChannelStripNode: Snare]
                 ──audio──→ [ChannelStripNode: OH]

[ChannelStripNode: Kick]    ──assign──→ [GroupBusNode: Drums]
[ChannelStripNode: Snare]   ──assign──→ [GroupBusNode: Drums]
[ChannelStripNode: OH]      ──assign──→ [GroupBusNode: Drums]

[ChannelStripNode: Synth Lead] ──send 1──→ [AuxBusNode: Reverb]
[ChannelStripNode: Strings]    ──send 1──→ [AuxBusNode: Reverb]
[GroupBusNode: Drums]          ──send 1──→ [AuxBusNode: Reverb]

[AuxBusNode: Reverb] → [Valhalla Reverb plugin] → [MasterBusNode]

[ChannelStripNode: Synth Lead] ──→ [MasterBusNode]
[ChannelStripNode: Strings]    ──→ [MasterBusNode]
[GroupBusNode: Drums]          ──→ [MasterBusNode]

[MasterBusNode] → [MonitorSectionNode] → [AudioOutputNode: Speakers]
                → [RecorderNode: session_mix.wav]
```

---

## Channel Strip Anatomy

The channel strip is the fundamental unit of the console. Every source gets one.

### Signal flow through a strip (top to bottom)

```
Audio In (from plugin or hardware)
    ↓
  Input Gain (trim) — coarse level adjustment, ±20dB
    ↓
  High-Pass Filter — frequency and slope (6/12/18/24 dB/oct)
    ↓
  Low-Pass Filter — frequency and slope
    ↓
  4-Band Parametric EQ
      Band 1: shelving or bell, 20Hz–2kHz
      Band 2: bell, 50Hz–8kHz
      Band 3: bell, 200Hz–20kHz
      Band 4: shelving or bell, 2kHz–20kHz
    ↓
  Insert Slot A (pre-dynamics) — any OmegaHost plugin
    ↓
  Gate / Expander
      threshold, ratio, attack, release, hold, range
    ↓
  Compressor / Limiter
      threshold, ratio, knee, attack, release, makeup gain, auto-makeup
      sidechain: internal (post-EQ) or external (from another channel)
    ↓
  Insert Slot B (post-dynamics) — any OmegaHost plugin
    ↓
  Aux Sends (N sends, each pre-fader or post-fader)
      per send: level, pre/post switch, mute
    ↓
  Pan / Balance — stereo pan law (0dB / -3dB / -6dB center)
    ↓
  Fader (main level, -∞ to +12dBFS, unity = 0dB)
    ↓
  Mute (hard mute, audio thread)
    ↓
  Solo (AFL/PFL/SIP modes — see below)
    ↓
  Bus assignment (route to: Master, Group 1–8, both, none)
    ↓
Audio Out (to assigned bus)
```

### Solo modes

Three classic solo behaviors, user-selectable globally:

- **AFL (After-Fader Listen)**: soloed channel routed to solo bus post-fader; other channels continue to output (true solo — un-soloed channels are not muted, the solo bus is a separate listen point)
- **PFL (Pre-Fader Listen)**: soloed channel tapped before the fader — useful for setting gain on a silent channel
- **SIP (Solo In Place)**: soloed channel plays through master; all other channels are muted — the classic "everything goes quiet" solo

### Stereo and mono channels

Channel strips can be configured as:
- **Mono**: single input, panner places it in stereo field
- **Stereo**: stereo input, balance control (moves energy left/right without changing level)
- **Mid/Side**: M/S stereo input with M and S gain controls; converts to L/R at output

---

## Bus Types

### Group bus (mix bus, subgroup)

A summing bus that collects multiple channel strips. Has its own channel strip (EQ, dynamics, inserts, fader). Use cases: drum bus compression, parallel processing, stem routing.

Group buses can feed the master bus or other group buses (nested groups). Depth limit: prevent cycles at the graph level.

### Aux bus (effects send/return)

Collects post-send audio from multiple channel strips. Typically feeds an effect plugin (reverb, delay); the effect's output returns to the master bus as an aux return channel strip.

A bus is an aux bus by convention (post-send logic), not by structural difference. The distinction matters for workflow, not DSP.

### Master bus

The final stereo (or surround) summing point. Always exactly one per session. Has an extended processing chain:

```
Sum of all assigned channels and groups
    ↓
  Insert chain (mastering plugins: limiter, multiband, stereo widener)
    ↓
  Master fader
    ↓
  Output metering (LUFS, true peak, phase correlation)
    ↓
  To monitor section
  To recorder
```

### Cue bus (headphone mix / in-ear monitor mix)

An independent mix for performers. Each channel strip can send to N cue buses independently, with its own level per send. The performer hears their own cue mix; the engineer hears the main mix. Multiple cue buses support multiple performers with different mixes.

Each cue bus is an `AudioOutputNode` in OmegaHost routed to a specific hardware output pair (e.g., headphone amp).

---

## VCA Groups

VCA (Voltage-Controlled Amplifier) groups are a hardware console concept with no exact software equivalent — and they are often misunderstood.

**What a VCA is NOT:** a mix group (which sums audio). A VCA master fader does not carry any audio.

**What a VCA IS:** a remote control for other faders. When you move the VCA master, it offsets the gain of all assigned channel faders by the same dB amount — without actually touching the underlying fader positions. Pull the fader back, push it forward, the offsets accumulate on top.

Why this matters:
- Automation records on the channel faders, not the VCA master
- You can bring the VCA master back to 0dB and recover the original fader positions exactly
- Multiple VCA masters can control the same channel (stacked VCAs) — each adds its offset independently
- On a hardware SSL or Neve desk, VCAs are critical for live mix automation

In OmegaMix:
- A VCA group has a master fader, a gain offset value, and a list of assigned strips
- Each audio cycle: channel gain = (base fader gain) + (sum of all VCA group offsets for this channel)
- VCA assignment is stored per channel strip as a set of group IDs, not an audio edge
- Automation of the VCA master records the VCA offset curve; automation of the channel strip records the base fader curve — independent, additive

---

## Metering

All meters read from the audio thread and push to the UI thread via lock-free ring buffers. The UI thread polls at display rate (60fps) and renders the most recent values. Meters never block or allocate on the audio thread.

### Per-channel meters

| Meter | Description |
|---|---|
| **Peak** | Instantaneous sample peak; bar graph with peak hold (configurable hold time) |
| **RMS** | Root-mean-square over a short window (~300ms); reflects perceived loudness |
| **VU** | Classic volume unit meter; ballistic response (300ms integration) |

Selectable: peak only, RMS only, peak + RMS overlay, or VU. Default: peak + RMS.

### Master bus meters

| Meter | Description |
|---|---|
| **True Peak** | Inter-sample peak estimation; catches overs that standard peak meters miss |
| **LUFS Momentary** | Integrated loudness over 400ms window (EBU R128) |
| **LUFS Short-Term** | Integrated over 3-second window |
| **LUFS Integrated** | Over the entire session (transport-aware; resets on stop) |
| **Phase Correlation** | –1 (out of phase) to +1 (mono compatible); shows as a needle or bar |
| **Goniometer** | L/R stereo field visualizer (Lissajous figure) |

### Spectrum analyzer

Optional per-channel or master bus: FFT-based spectrum display, configurable bin resolution and decay. Runs on the audio thread with a lock-free FIFO to the UI.

---

## Automation

Console automation records and replays fader moves, mute toggles, and send level changes in sync with Omega's transport.

### Automation modes (per channel, per parameter)

| Mode | Behavior |
|---|---|
| **Off** | Automation data ignored; manual control only |
| **Read** | Automation data plays back; controls move accordingly |
| **Write** | All movements recorded; overwrites existing data |
| **Touch** | Reads automation until the user touches a control; then writes while touched; returns to Read on release |
| **Latch** | Like Touch, but stays in Write mode after release until transport stops |
| **Trim** | Reads existing automation; adjustments are additive offsets on top |

### What can be automated

Every fader, mute button, send level, send mute, pan position, EQ band (any parameter), and dynamics parameter on any channel strip. Essentially: any parameter that has a value visible in the UI.

VCA master faders are also automatable. Automation of a VCA master affects all assigned channels (via offset accumulation) — the same as moving the fader live.

### Automation data model

Automation is stored as a list of (tick, value) breakpoints per parameter per channel. Between breakpoints, the value is interpolated (linear or cubic, user-selectable). This maps cleanly to the existing Omega `TempoMap` pattern of breakpoint lists.

Automation data is stored in the `.omega` project file alongside event data.

### Automation and Omega's transport

Automation playback is driven by Omega's transport position — the same tick counter that drives sequences. On locate, automation jumps to the values at the new position. On stop, Touch/Latch modes disengage. This is handled in the audio thread's process cycle, immediately after `omega_engine_process()`.

Recording automation: when the transport is rolling in Write/Touch/Latch mode, parameter changes from the UI are timestamped with the current tick and appended to the automation breakpoint list via the mutation thread command queue — the same mechanism as any other Omega edit.

---

## Monitoring Section

The monitor section is the control room management hub. It is separate from the master bus and does not affect the mix output.

```
Source selector
  ├── Main Mix (from MasterBusNode)
  ├── Aux Bus 1 (check reverb return in isolation)
  ├── Cue Bus 1, 2, 3... (check headphone mix)
  └── External Input (DAW return, reference track, etc.)
    ↓
  Mono Sum button (collapse to mono for mono compatibility check)
    ↓
  Dim button (instant –20dB; hold to talk)
    ↓
  Speaker output level (control room volume, does not write to master bus)
    ↓
  Speaker selector A / B / C (switch between monitor pairs)
    ↓
  AudioOutputNode (to speaker amp)
```

### Talkback
- A talkback mic input (any audio input on the interface) feeds into the cue buses at a configurable level
- Talkback is activated by a button (momentary or latching)
- While talkback is active, the monitor section can optionally dim the speakers

### Reference level calibration
- The monitoring section stores a calibration offset for each speaker pair (A/B/C)
- K-System metering integration (K-12, K-14, K-20) — calibrated reference levels for broadcast/film/music

---

## DSP: Built-In vs. Plugin

Every channel strip needs EQ, compressor, and gate DSP. Two approaches:

### Option A: Built-in DSP (recommended for the core)
OmegaMix ships clean reference implementations of:
- Biquad parametric EQ (each band: RBJ-filter based)
- Soft-knee compressor with RMS detection
- Downward expander / gate
- Linear-phase high-pass / low-pass filter

Advantages: works out of the box with no plugin dependencies, predictable CPU cost, always available. The reference implementations are not "world-class" DSP — they are transparent and correct. Users who want better EQ or compression use insert slots.

### Option B: Strip DSP as plugin instances
Each EQ, compressor, and gate in the strip is actually a CLAP plugin instance in OmegaHost. The strip is a pre-wired subgraph.

Advantages: DSP is swappable; users can replace the built-in compressor with their preferred one.

Disadvantages: creates many plugin instances; complicates the graph; plugin discovery required just to open a project.

### Hybrid (ideal)
Built-in DSP for the defaults. Insert slots for plugin overrides. The insert slot replaces (not augments) the built-in EQ or dynamics when populated.

---

## Hardware Controller Integration

A physical mixing console surface (Mackie Control, SSL UF8, AVID S1, Behringer X-Touch) communicates via Mackie Control Universal (MCU) or HUI protocol over MIDI.

OmegaMix maps its channel strips to hardware surface faders, knobs, and buttons via an MCU/HUI bridge. This bridge is an `EventInput` in OmegaHost — incoming MCU MIDI messages translate to OmegaMix parameter changes via the mutation thread command queue.

Hardware fader moves are automation-recordable: the MCU bridge tags each change with the current transport tick, feeding the automation system as if the on-screen fader had moved.

MCU banking: hardware surfaces have 8 or 16 faders. Banking scrolls the surface mapping across all console channels. OmegaMix manages the bank assignment and updates the surface fader positions and scribble-strip labels on each bank change.

---

## Project File Integration

OmegaMix adds a `mixing_console` section to the `.omega` project file:

```json
{
  "mixing_console": {
    "channel_strips": [
      {
        "id": "strip_1", "name": "Synth Lead", "color": "#5C8AE0",
        "source_node": "node_1",
        "gain_db": 0.0,
        "hpf": { "enabled": true, "freq": 80.0, "slope": 12 },
        "eq": [
          { "band": 1, "type": "low_shelf", "freq": 120, "gain": -2.0, "q": 0.7 },
          { "band": 2, "type": "bell", "freq": 3200, "gain": 1.5, "q": 1.2 }
        ],
        "compressor": { "enabled": true, "threshold": -18.0, "ratio": 3.0,
                        "attack_ms": 10.0, "release_ms": 80.0, "makeup": 4.0 },
        "sends": [
          { "aux_id": "aux_1", "level_db": -6.0, "pre_fader": false, "muted": false }
        ],
        "pan": 0.15,
        "fader_db": -3.0,
        "muted": false,
        "bus_assignment": "master",
        "vca_groups": [1]
      }
    ],
    "group_buses": [
      { "id": "grp_1", "name": "Drums", "color": "#E05C5C" }
    ],
    "aux_buses": [
      { "id": "aux_1", "name": "Verb", "return_node": "node_reverb" }
    ],
    "vca_groups": [
      { "id": 1, "name": "Keys", "offset_db": 0.0 }
    ],
    "automation": {
      "strip_1": {
        "fader_db": [ { "tick": 0, "value": -3.0 }, { "tick": 3840, "value": -6.0 } ],
        "muted": [ { "tick": 7680, "value": 1 } ]
      }
    },
    "monitor": {
      "dim_db": -20.0,
      "speakers": [
        { "slot": "A", "name": "Genelec 8030", "calibration_db": 0.0 }
      ]
    }
  }
}
```

---

## The Complete Stack Picture

With OmegaMix in place, the full library stack looks like this:

```
┌────────────────────────────────────────────────────────────────────────────┐
│  Your Application  (built with OmegaKit harness)                           │
├────────────────────────────────────────────────────────────────────────────┤
│  OmegaMix  — channel strips, buses, VCAs, automation, metering, monitor    │
├────────────────────────────────────────────────────────────────────────────┤
│  OmegaHost — signal graph, plugin hosting (CLAP/VST3/AU/LV2), CV routing  │
├────────────────────────────────────────────────────────────────────────────┤
│  OmegaKit  — app scaffold, device manager, project file, helpers, widgets  │
├────────────────────────────────────────────────────────────────────────────┤
│  Omega     — sequencer engine, C API, MIDI I/O, SMF, timing               │
└────────────────────────────────────────────────────────────────────────────┘
```

Each layer is optional. A developer can use Omega + OmegaHost without OmegaMix (raw graph, no console abstraction). Or they can use all four layers and have a complete studio application skeleton.

---

## Open Questions for Future Brainstorming

- Should OmegaMix's built-in EQ use linear-phase filters (more transparent on buses, higher latency) or minimum-phase (classic analog-style, zero added latency)?
- Should automation breakpoints be tick-based (follows tempo changes) or time-based (absolute seconds)? Both have precedent.
- Is surround (5.1 / 7.1 / Atmos) in scope from the start, or stereo-first with surround as a v2 extension?
- How does VCA automation interact with channel strip automation — especially in DAW plugin mode where both the plugin host (Ableton, Logic) and OmegaMix might be trying to control the same fader?
- Should OmegaMix expose a headless API (no UI, parameter automation only) so it can be used inside a VST plugin where the host provides the UI?
- Is there appetite for a "scene" system on the console level — recalling different channel strip settings for different song sections — independent of Omega's sequence switching?
- Should MCU/HUI hardware controller support be in OmegaMix core, or a separate `omegamix-hid` module?
