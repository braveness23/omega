# Idea Document: KCS Track Mode — Modern Reimplementation on Omega

## Context

Dr. T's Keyboard Controlled Sequencer (KCS) was the gold standard MIDI sequencer on Atari ST and Amiga in the late 1980s. Its Track Mode was a 48-track linear recorder/arranger that influenced nearly every DAW that followed. This document brainstorms a modern native desktop application that faithfully reproduces the KCS Track Mode *workflow* with a clean contemporary UI, using Omega as the sequencer engine underneath.

No code will be written. This is a pure ideas document.

---

## What KCS Track Mode Was

KCS Track Mode is a linear, tape-deck-style multi-track MIDI sequencer. Its defining characteristics:

### Track structure
- **48 tracks**, each an independent sequence of MIDI events
- Each track: name, MIDI output channel, velocity offset, transposition, length
- Tracks could be different lengths (polymetric playback)
- Tracks loop independently when their length is shorter than the song

### Recording
- **Real-time record**: arm a track, press play, recording begins from MIDI input
- **Overdub**: add to an existing track without erasing it
- **Step record**: enter notes one at a time with explicit note length selection
- **Punch in/out**: define a region where recording replaces existing content
- **Note filter**: record only notes in a specific pitch range
- **Channel filter**: record only from a specific incoming MIDI channel

### Playback controls
- Per-track: **mute**, **solo**, **record arm**
- **Ghost notes**: faint display of a reference track's notes on another track (visual aid)
- **Track echo**: MIDI delay — re-fires events N ticks later, with decay on velocity

### Quantization
- Pre-quantize (applied as notes come in) or post-quantize (applied after recording)
- Standard grid values (whole, half, quarter, eighth, sixteenth, triplet variants)
- **Groove quantize**: apply a groove/swing template
- **Humanize**: add random timing/velocity variation (anti-quantize)

### Event editing
- **Event list view**: raw text list of all events with tick, type, value columns — fully editable
- Block operations: select a tick range on a track; then copy, move, erase, transpose, velocity-scale the block
- **Track operations**: copy track, append track to another, merge two tracks, reverse a track

### Sequences (scenes)
- KCS stored up to 12 independent "sequences" — complete track setups with different content
- You could switch sequences mid-song to jump to a chorus, bridge, etc.
- This is the forerunner of modern "scenes" in Ableton Live and clip launchers

### Sync & tempo
- Internal tempo with tap-tempo
- External MIDI clock sync (slave to hardware)
- SMPTE sync to video (with the SMPTE Programmer add-on)
- Tempo changes recorded into the sequence

---

## How Omega Covers This

| KCS Feature | Omega Mechanism |
|---|---|
| 48 linear tracks | `TimelineSource` — unlimited tracks |
| MIDI output per track | `omega_engine_add_sink()` + `set_sink(track_id, sink_id)` |
| Real-time record from MIDI in | `LibremidiInput` → `EventInput` → `InputBus` |
| Punch in/out | Record window logic on top of EventInput |
| Mute per track | `Track::muted` flag |
| Transpose per track | Per-event transform (TransformSource pattern) |
| Velocity offset per track | Per-event transform (same) |
| Quantize | Post-process tick snapping using `MeterCursor` grid |
| Groove/swing | `PerformanceContext::swing` + `GrooveLibrary` |
| Humanize | `PerformanceContext::chaos` channel |
| Loop markers | `RegionList` with `LOOP` type |
| Punch markers | `RegionList` with `PUNCH` type |
| Sequences / scenes | Multiple `Session` objects or `TimelineSource` swap |
| Tempo changes | `TempoMap` with multiple `TempoPoint` entries |
| SMPTE sync | `SmpteConverter` + `SmpteConfig` on `Session` |
| Event list editor | Direct read of track event vectors |
| SMF import/export | `omega_smf_import()` / `omega_smf_export()` |
| Snap-to-grid | `snap_to_nearest()` with `SNAP_GRID` |
| Chase on locate | `on_locate()` built into all sources |
| Undo/redo | `Command` queue pattern |
| Track echo (MIDI delay) | Custom `TransformSource` — re-emits events at offset ticks |

Omega gaps to address in the idea (not implementation):
- Step record: needs an input-mode state machine above the engine
- Overdub merge: needs a record/commit cycle (record to staging, merge on stop)
- Per-track velocity offset and transpose: needs a per-track `TransformSource` wrapper
- Multi-sequence: needs a session-swap concept or scoped timeline regions

---

## The Modern UI Concept

### Visual language
- Dark background, high contrast — modern pro audio aesthetic (think Bitwig, not GarageBand)
- Color-coded tracks with user-assignable colors
- Monospace font for event list; proportional font for labels
- Compact information density — no wasted whitespace, but not cramped
- Resizable panels with split-pane layout

### Main layout (four-panel)

```
┌─────────────────────────────────────────────────────────┐
│  TRANSPORT BAR  [◀◀] [▶] [■] [⏺] TEMPO BPM  POS  LOOP  │
├────────┬────────────────────────────────────────────────┤
│ TRACK  │           TIMELINE / PIANO ROLL                │
│ HEADER │                                                │
│ panel  │   Track 1  ████████████░░░░████████████        │
│        │   Track 2  ░░░░░████░░░░░░░░░░████░░░         │
│  name  │   Track 3  ████░░░░░░░░████████░░░░░░░        │
│  ch    │   ...                                          │
│  mute  │                                                │
│  solo  ├────────────────────────────────────────────────┤
│  arm   │         EVENT LIST (bottom split)              │
│  color │   Tick    Type    Note  Vel  Dur               │
│        │   0:1:0   NOTE    C4    100  480               │
│        │   0:1:2   CC      7     64   —                 │
└────────┴────────────────────────────────────────────────┘
```

### Track header panel (leftmost column)
- Track number + editable name
- MIDI output port (dropdown) + MIDI channel (1–16)
- Color swatch
- Mute (M) / Solo (S) / Record Arm (R) buttons
- Per-track transpose (±24 semitones) — like KCS but displayed inline
- Per-track velocity offset (±64) — same
- Expand arrow → opens step-editor view for that track

### Timeline panel (main area)
- Horizontal scrolling, zoomable
- Notes drawn as colored bars (mini piano-roll inline)
- Bar/beat grid lines; tempo change markers shown as vertical lines
- Loop region shown as a shaded overlay with drag handles
- Punch in/out shown as a red overlay with drag handles
- Playhead scrubbing with click/drag
- Ghost notes: toggle to show another track's content faintly behind the active track

### Event list panel (toggleable bottom panel)
- Faithful to KCS — all events as rows: Tick, Bar:Beat:Tick, Type, Ch, Data1, Data2, Duration
- Editable in-place — click any cell to edit
- Filter by event type (note only, CC only, etc.)
- Selection syncs with timeline panel (select events, they highlight on both)

### Step editor panel (per-track, collapsible)
- A grid of buttons, one per step
- 16 steps by default (configurable to 32, 64)
- Each step: on/off, note, velocity, length
- Classic KCS step-entry feel but with a visual grid instead of prompts

### Sequence launcher sidebar (right panel, optional)
- 12 sequence slots (faithful to KCS)
- Each slot: name, arm button, color
- Switch sequences with a single click (swaps the timeline content)
- Inspired by KCS but visually like a mini clip launcher

### Inspector (floating or docked)
- Context-sensitive: shows properties of selected track, event, or region
- For selected track: full parameter set (MIDI port, channel, transpose, velocity, record mode)
- For selected event: all event fields editable
- For selected region: start/end ticks, loop count, snap target

---

## KCS-Specific Features Worth Preserving (and Modernizing)

### Track Echo (MIDI delay)
KCS could echo any track — re-fire its events after a delay, with each repeat slightly quieter (velocity decay).

Modern take: a per-track "Echo" section in the inspector — delay time (in ticks or ms), repeats (1–8), velocity decay %. This maps to a `TransformSource` layer per track.

### Ghost Notes
KCS allowed you to see another track's notes as a transparent overlay while editing. Invaluable for aligning bass lines to drum patterns.

Modern take: in the track header, a "Ghost" dropdown to pick any other track to show as overlay. Rendered at 20% opacity in the timeline panel.

### Polymetric loops
KCS tracks could have different lengths and loop independently. A 3-bar drum pattern against a 4-bar bass line creates natural phasing.

Modern take: per-track loop length override in the inspector. Visual indicator shows where each track loops relative to the global timeline.

### Multiple sequences
KCS had 12 sequences — like snapshots of the whole track arrangement.

Modern take: a "Sequences" sidebar with 12 named slots. Each sequence stores a complete snapshot of track content and mute states. Switching sequences crossfades (or jump-cuts) the engine. The engine swaps `Session` references under the hood.

### Groove quantize
KCS had groove templates for swing and shuffle.

Modern take: a Quantize panel (accessible from toolbar) with:
- Grid resolution
- Strength (0–100%) — how hard to pull notes toward the grid
- Groove template picker (Omega's `GrooveLibrary`)
- Apply: destructive (edits events) or non-destructive (applies at playback via PerformanceContext)

---

## Ideas for Going Beyond KCS

These didn't exist in KCS but feel natural in a modern reimplementation on Omega:

1. **MIDI learn**: right-click any parameter → "Learn MIDI CC" — maps incoming CC to that parameter live
2. **Ableton Link**: sync tempo across apps and devices on the same network (optional, GPL)
3. **Piano roll as first-class editor**: KCS only had event list; a piano roll view makes note editing visual
4. **Clip-within-track**: instead of one long event stream per track, allow named clips within a track — move, duplicate, delete whole clips
5. **Automation lanes**: CC/pitch bend drawn as curves below each track in timeline, not just events
6. **Take system**: record multiple takes; choose the best one — keeps underlying events
7. **MIDI FX chain per track**: chain multiple `TransformSource` instances per track (quantize → transpose → echo → humanize)
8. **Built-in arpeggiator per track**: `TransformSource` that expands incoming chords
9. **Scene launcher**: Omega's `PerformanceSource` 64-slot pad launcher exposed as a secondary view alongside Track Mode

---

## Open Questions for Future Brainstorming

- What UI framework? (JUCE for native C++ audio apps is the obvious choice; ImGui for lightweight; Qt for cross-platform)
- How is the step editor triggered — per-track, or a separate full-screen mode like KCS?
- Should sequences be destructively separate or non-destructively layered (like arrangement clips)?
- Should track echo be a "MIDI FX" in a per-track chain, or a track-level parameter like KCS had it?
- What's the file format — SMF only, or a project format that preserves all track names, colors, sequences?
- Should the app host VST plugins for sound generation, or remain a pure MIDI sequencer (like KCS was)?
