# Idea Document: OmegaAudio — Audio Recording, Editing, and Playback Library

## Context

The current stack handles MIDI sequencing (Omega), plugin-generated audio (OmegaHost), mixing (OmegaMix), and AI control (OmegaAPI) — but has no concept of recorded audio. There is nowhere to capture a vocal, a guitar take, a synthesizer bounce, or a sampled loop. There is no clip timeline for placing audio alongside MIDI, no waveform editor, no take management, and no audio format I/O beyond SMF.

OmegaAudio fills this gap. It adds:

1. **Recording** — capture audio from hardware inputs, plugin outputs, or the mix bus in sync with Omega's transport
2. **A clip/region model** — named audio clips placed on a timeline with non-destructive edits
3. **Playback** — stream audio clips back through OmegaHost's signal graph in perfect sync with the sequencer
4. **Editing** — trim, split, fade, gain, normalize, time-stretch, pitch-shift, crossfade — all non-destructive
5. **Format support** — WAV, AIFF, FLAC, MP3, Opus, OGG — read and write

No code will be written. This is a pure ideas document.

---

## Core Concepts

### Audio File

The raw material. An audio file on disk: WAV, AIFF, FLAC, or compressed. OmegaAudio never modifies audio files after import. All edits are stored as metadata on top of the original.

### Media Pool

A session-level registry of all audio files in use. Each file is imported once; all clips reference into it. If the same audio file is used in ten clips across multiple tracks, it appears once in the pool.

The pool tracks: file path, format, sample rate, channel count, duration, and import date. On session open, OmegaAudio verifies all pool files exist and warns about missing or moved files. Missing files can be relinked by browsing to the new location.

### Audio Clip

A reference into a media pool file with a defined in-point and out-point. The clip occupies a position on an audio track's timeline. A clip is not audio — it is a pointer to a region of audio.

Clip properties:
- **Source file** (pool reference)
- **Source in** (sample offset into the file where playback starts)
- **Source out** (sample offset where playback ends)
- **Timeline position** (where on the session timeline the clip begins, expressed as tick or as absolute sample)
- **Gain** (clip-level gain, in dB, applied before the track fader)
- **Fade in**: duration + shape (linear, equal-power, S-curve)
- **Fade out**: duration + shape
- **Muted**: clip silenced without removing it
- **Color**: for visual identification in the timeline
- **Name**: editable label
- **Time lock mode**: musical (follows tempo map — clip stays at bar:beat) or absolute (clip stays at sample position regardless of tempo changes)

### Audio Track

A horizontal lane on the timeline that holds clips. Multiple clips can coexist on one track (non-overlapping by default; overlapping triggers automatic crossfade). Each audio track:

- Has a name and color
- Feeds one **channel strip in OmegaMix** (the track's audio output goes there, just like a plugin's audio output)
- Can be armed for recording (one or more hardware inputs assigned)
- Has a record mode: normal (replace), overdub (all takes summed — rarely used for audio), takes (each pass becomes a new take)
- Has a monitoring mode: off (hear nothing while recording), input (hear the live input through the mix), auto (hear input when armed, playback when not)

### Edit Decision List (EDL)

The authoritative record of all non-destructive edits. The EDL is a sequence of clip descriptors — each describing which region of which source file plays at which timeline position with which gain and fades. Playback reads the EDL; the original audio files are never touched.

The EDL is the "source of truth" stored in the project file. Rendering/bouncing applies all EDL entries to produce a new flat audio file — the first moment anything is written to disk beyond the original recordings.

---

## Recording

### Single-track recording

Arm one audio track. Assign a hardware input (mono or stereo). Press record + play. OmegaAudio captures audio from the input into a new audio file in real time, synchronized to Omega's transport. On stop, the new file is added to the media pool and a clip is created on the track at the recorded range.

### Multi-track recording

Multiple tracks armed simultaneously. All record to separate files in the same pass. Synchronization is guaranteed — all captures use the same hardware clock via the audio device callback. This is the standard workflow for recording a live band.

### Loop recording / takes

In loop mode (a loop region defined in Omega via `RegionList` with type `LOOP`):
- Each pass through the loop creates a new take
- Takes stack on the track (displayed as a take lane selector)
- After recording, the engineer picks the best take (or comps across takes)
- Takes are named by pass number: "Take 1", "Take 2", etc.

### Punch in / punch out

OmegaAudio watches the `PUNCH` region defined in Omega's `RegionList`. Recording begins exactly at the punch-in tick and stops at punch-out. The transport plays normally through the punch region; recording only engages within it. Automatic punch uses sample-accurate scheduling.

Manual punch: the engineer presses record while the transport is playing — recording engages at that exact position.

### Pre-roll and count-in

Before recording begins, OmegaAudio can play back a configurable number of bars (pre-roll) so performers hear context. During pre-roll, the transport plays but recording does not yet engage. After the pre-roll, recording begins on the downbeat.

### Latency compensation for recording

Hardware input monitoring has latency (audio interface round-trip). When OmegaAudio writes the clip to the timeline, it shifts the clip backward by the measured input latency so the recorded audio lines up with the MIDI events that drove the performance. This is measured at session start from the audio device's reported latency, with a user-adjustable trim.

### Bounce / stem recording

Record the output of any OmegaHost node (a plugin, a mix bus, the master output) directly to a clip. This is "internal bounce" — no audio leaves through the hardware output; OmegaHost routes the node's output into OmegaAudio's recorder instead.

Use cases:
- Capture a synthesizer to audio to free up CPU
- Create stem files (drums, bass, synths as separate audio files)
- Render the master mix to a stereo WAV

Bounce speed: can run faster than real time (if no real-time output required) by running the audio callback at the maximum rate the CPU allows — a feature possible because OmegaAudio and OmegaHost share the same process cycle.

---

## Non-Destructive Editing

All edits are stored in the EDL. The original audio file is never touched. A render/bounce step applies edits permanently to produce a new file.

### Trimming

Moving the in-point or out-point of a clip inward (hide audio) or outward (reveal previously hidden audio, up to the source file boundary). Trim is the most common edit operation.

### Splitting

Cut a clip at a position into two clips at the same position. Both clips reference the same source file; the left clip's out-point and right clip's in-point meet at the cut point.

### Moving

Drag a clip to a new timeline position. The clip retains its source reference and all edits; only its timeline position changes.

### Gain editing

Per-clip gain (dB). Adjusts the clip's playback level before the track fader. Does not affect the source file. Also: per-region gain automation — draw a gain curve within a clip for detailed level shaping (like Pro Tools' clip gain).

### Fades

Fade in and fade out on any clip. Three shapes:
- **Linear**: straight dB ramp — fast, sounds mechanical
- **Equal-power**: constant-power curve — natural for panning and crossfades
- **S-curve**: logarithmic — smooth, natural for music

Fades are rendered at playback time from the EDL, not written to the file.

### Crossfades

When two clips overlap on a track, OmegaAudio automatically creates a crossfade at the overlap region: the outgoing clip fades out and the incoming clip fades in simultaneously. The crossfade type (equal-power, linear) is configurable. Crossfade points are draggable in the waveform view.

### Normalize

Analyze a clip's peak or RMS level and compute the gain offset needed to bring it to a target level (e.g., peak at –1dBFS or RMS at –18dBFS). Store the gain offset as a clip property. Non-destructive.

### Reverse

Play a clip backward. Stored as a flag on the clip. At playback, OmegaAudio reads the source file backward from out-point to in-point. Can be combined with time-stretching for reverse-stretch effects.

### Duplicate and loop

Create N copies of a clip placed end-to-end — loop a drum sample, for example. OmegaAudio references the same source file N times; no additional disk space. Gap between copies is configurable (0 for seamless loop, positive for gap, negative for crossfade).

---

## Time-Stretching and Pitch-Shifting

These two operations change tempo or pitch of recorded audio independently. They are computationally expensive and are the most complex DSP in OmegaAudio.

### Time-stretching

Change the playback speed of a clip without changing pitch. Use cases:
- Match a drum loop at 126 BPM to a session at 132 BPM
- Slow down a vocal for an effect
- Auto-stretch all clips to follow tempo map changes

**Algorithms:**
- **Phase vocoder**: frequency-domain processing; good for sustained tones, can produce artifacts on transients
- **Transient-preserving phase vocoder**: detects onsets and adjusts phases to preserve punch — better for drums and percussive material
- **WSOLA** (Waveform Similarity Overlap-Add): time-domain; faster than phase vocoder, good for speech
- **Elastique / zplane**: commercial, highest quality, used in most professional DAWs — may be out of scope for open-source library
- **Rubber Band Library** (GPL) or **SoundTouch** (LGPL): open-source options with reasonable quality

Stretch modes: musical (follows tempo map — clip stretches automatically when tempo changes) or manual (explicit stretch ratio set by the user).

### Pitch-shifting

Change pitch without changing speed. Use cases:
- Tune a vocal recording up or down by semitones
- Correct pitch of a note without moving it in time
- Create harmonies from a single recording

Pitch-shifting is implemented as time-stretch + sample-rate conversion combined. The same algorithm choices apply.

### Warp markers (beat-stretching)

Place warp markers at transients in the audio (kick hits, snare hits, note attacks). The stretch algorithm pins each warp marker to a specific grid position. The audio between markers stretches or compresses to make the transients land exactly on the grid.

This is how Ableton's warping works. It is the most powerful form of audio manipulation for music — it can take a poorly-timed recording and make it groove-perfect without audible artifacts on well-tuned material.

OmegaAudio's warp markers integrate with Omega's `AnchorList` concept (already in the engine) — warp markers are `ANCHOR_WARP` anchors on an audio clip.

---

## Playback Architecture

### AudioClipNode in OmegaHost

Audio clips play back as `AudioClipNode` instances in OmegaHost's signal graph — the same node type system as plugins and hardware inputs. Each audio track in OmegaAudio maps to one `AudioClipNode` in the graph.

The `AudioClipNode` process function:
1. Receives the current sample position from the transport
2. Looks up the EDL to find which clip (if any) is active at this position
3. Computes gain, fade, and stretch factors from the clip properties
4. Reads audio from the source file (or decoded buffer) into the output audio buffer
5. Applies gain and fade curves sample-by-sample

This design means audio tracks appear in OmegaMix as channel strips automatically, just like plugin instruments — no special-casing needed.

### Read-ahead streaming

Audio files can be too large to load into memory. OmegaAudio streams audio from disk with a read-ahead buffer. A dedicated I/O thread (separate from the audio thread) reads ahead by N samples (configurable, default ~1 second) into a ring buffer. The audio thread reads from the ring buffer — no disk I/O on the audio thread itself.

If the ring buffer runs dry (disk too slow, or a seek), OmegaAudio substitutes silence and logs an underrun event. The UI shows a warning.

On locate (transport jump), the I/O thread immediately seeks and begins refilling the ring buffer. A brief silence may occur at the new position while the buffer refills — minimized by pre-seeking during pre-roll.

### Sample rate conversion

If a clip's source file is at a different sample rate than the session (e.g., a 48kHz file in a 44.1kHz session), OmegaAudio resamples on the fly using a high-quality algorithm (libsamplerate's SRC_SINC_BEST_QUALITY or equivalent). Sample rate conversion happens in the `AudioClipNode` before the output buffer is populated.

### Sample-accurate synchronization

Audio clip playback starts and stops at the exact sample calculated from Omega's tick position and the TempoMap. The conversion:

```
sample_position = tick_to_ns(tick, tempo_map) * sample_rate / 1_000_000_000
```

This uses the same `TempoMap::ns_at_tick()` precomputed values Omega already maintains. Tempo changes mid-session correctly reposition audio clips that are in "musical" time lock mode.

---

## Waveform Display

The waveform is the primary UI for audio editing. OmegaAudio maintains a **peak cache** for each audio file — precomputed min/max amplitude values at multiple zoom levels. This enables instant waveform rendering at any zoom level without reading the full audio file.

### Peak cache structure

For each audio file, OmegaAudio generates a `.peak` sidecar file containing:
- Peak (min + max) at 64-sample resolution (fine zoom)
- Peak at 512-sample resolution (medium zoom)
- Peak at 4096-sample resolution (overview zoom)

At display time, the renderer picks the resolution that gives ~1 sample per pixel and reads only the needed range. A 10-minute stereo file at 44.1kHz renders a full-width overview in microseconds.

Peak caches are generated in the background after import. Before the cache is ready, OmegaAudio displays a progress indicator and generates on-demand for the visible region.

### Waveform rendering hints for UI implementors

- Draw min/max envelope as a filled shape — standard DAW look
- Draw RMS envelope as a lighter inner shape for perceived loudness context
- Fade-in and fade-out regions: overlay a semi-transparent gradient matching the fade shape
- Crossfade regions: show both outgoing (red tint) and incoming (green tint) waveforms overlaid
- Warp markers: vertical lines with drag handles
- Gain automation: superimpose the gain curve as a line overlay
- Clip color is user-assigned; waveform renders in a lighter tint of the clip color

---

## Format Support

### Read (import)

| Format | Library | Notes |
|---|---|---|
| WAV | libsndfile (LGPL) | All bit depths: 16, 24, 32-int, 32/64-float |
| AIFF | libsndfile | All bit depths; standard Mac format |
| FLAC | libFLAC (BSD) | Lossless; embedded via libsndfile |
| MP3 | minimp3 (CC0) | Decode only; no encoder |
| OGG Vorbis | libvorbis (BSD) | Lossy; common on Linux |
| Opus | libopus (BSD) | Modern lossy; excellent at low bitrates |
| CAF | libsndfile | Apple Core Audio Format |
| W64 | libsndfile | Sony Wave64 for files > 4GB |

### Write (export / bounce)

| Format | Quality notes |
|---|---|
| WAV 24-bit | Standard delivery format; lossless |
| WAV 32-bit float | Archival; preserves headroom beyond 0dBFS |
| AIFF 24-bit | Mac delivery standard |
| FLAC | Lossless compressed; smaller than WAV |
| MP3 | Lossy; via LAME (LGPL); for consumer delivery |
| OGG Vorbis | Lossy; open; for streaming |
| Opus | Lossy; best quality/size for low bitrates |

### Broadcast WAV (BWF)

The industry standard for professional audio exchange. BWF embeds a `bext` chunk in the WAV file containing: description, originator, date/time, SMPTE timecode origin, and a unique identifier. OmegaAudio writes BWF metadata on export so files can be exchanged with Pro Tools, Nuendo, and broadcast facilities. On import, OmegaAudio reads the timecode origin to automatically place the clip at the correct SMPTE position.

---

## Comping (Take Compilation)

Loop recording produces multiple takes. Comping selects the best moments from different takes to assemble a perfect composite performance.

### Take lanes

After loop recording, all takes appear as horizontal lanes within the track. The active (playing) section of each take is highlighted. Other sections are greyed out.

Comping workflow:
1. Listen to each take
2. Click a region of a take to make that region active (replaces whatever was active there)
3. The boundary between regions can be dragged to a preferred edit point
4. Boundaries automatically crossfade across the take boundary
5. When satisfied, "flatten" the comp: OmegaAudio renders the selected regions into a single new clip (destructive to the takes structure, but preserves originals in the pool)

This is equivalent to Pro Tools' playlists, Logic's take folders, or Ableton's comping feature.

---

## Integration with the Stack

### With Omega (transport sync)

OmegaAudio registers a transport observer with Omega. On `play`, the observer starts streaming. On `stop`, streaming halts and notes are silenced. On `locate`, the I/O thread seeks immediately. Loop regions from Omega's `RegionList` (type `LOOP`) trigger seamless loop playback — the clip wraps at the loop boundary.

Tempo changes in Omega's `TempoMap` recompute sample positions for all "musical" time-locked clips immediately, keeping audio in sync with MIDI through tempo automation.

### With OmegaHost (signal graph)

Each OmegaAudio audio track is an `AudioClipNode` in OmegaHost's graph. The node appears in the patchbay alongside plugin nodes. Its audio output can be:
- Routed directly to an `AudioOutputNode` (bypassing the mix)
- Routed to a `ChannelStripNode` in OmegaMix (the normal case)
- Routed to another plugin for processing
- Recorded back into OmegaAudio (internal bounce)

### With OmegaMix (channel strips)

Audio tracks appear as channel strips in OmegaMix automatically — one strip per audio track. The strip provides gain, EQ, dynamics, sends, pan, and fader just like any other source. Automation of the strip's fader is recorded against the session timeline and plays back in sync.

### With OmegaAPI (AI recording and editing)

OmegaAPI exposes audio operations:

```
record_arm(track, input)
record_start(punch_in_position)
record_stop()
import_file(path)                          → ClipRef
place_clip(track, clip, position)
trim_clip(clip, new_in, new_out)
split_clip(clip, position)                 → [ClipRef, ClipRef]
set_clip_gain(clip, db)
set_clip_fade(clip, in_duration, out_duration, shape)
stretch_clip(clip, ratio)
pitch_shift_clip(clip, semitones)
bounce_track(track, from, to)              → ClipRef
bounce_master(from, to, format)            → file path
get_waveform_peaks(clip, from, to, resolution) → [{min, max}]
describe_audio_content(clip)               → natural language summary
```

AI agents can record, edit, comp, and bounce audio entirely through OmegaAPI. The semantic bridge handles bar:beat positions for all clip placement and trim operations.

---

## Project File Integration

OmegaAudio adds a `media_pool` and `audio_tracks` section to the `.omega` project file:

```json
{
  "media_pool": [
    {
      "id": "file_1", "path": "audio/vocal_take1.wav",
      "sample_rate": 44100, "channels": 1, "duration_samples": 2116800,
      "peak_cache": "audio/vocal_take1.peak"
    }
  ],
  "audio_tracks": [
    {
      "id": "atrack_1", "name": "Lead Vocal", "color": "#A05CE0",
      "input": "Focusrite USB: Mic 1", "monitoring": "auto",
      "clips": [
        {
          "id": "clip_1", "pool_file": "file_1",
          "source_in": 0, "source_out": 2116800,
          "timeline_tick": 1920, "time_lock": "musical",
          "gain_db": -3.0,
          "fade_in": { "duration_samples": 441, "shape": "equal_power" },
          "fade_out": { "duration_samples": 882, "shape": "equal_power" },
          "stretch_ratio": 1.0, "pitch_shift_semitones": 0.0
        }
      ],
      "takes": [
        { "pass": 1, "clip": "clip_1" },
        { "pass": 2, "clip": "clip_2" }
      ],
      "mix_strip_id": "strip_vocal"
    }
  ]
}
```

Audio files themselves are stored relative to the project folder. On save, OmegaAudio copies any externally referenced files into the project's `audio/` folder (optional — configurable).

---

## The Complete Stack Picture (Final)

```
┌────────────────────────────────────────────────────────────────────────┐
│  AI Agents / Applications  (via OmegaAPI — MCP, REST, WebSocket, SDK) │
├────────────────────────────────────────────────────────────────────────┤
│  OmegaMix    channel strips · buses · VCAs · automation · metering     │
├──────────────────────────────┬─────────────────────────────────────────┤
│  OmegaHost                   │  OmegaAudio                             │
│  plugin graph · CV routing   │  recording · clips · editing · waveform │
│  CLAP · VST3 · AU · LV2      │  time-stretch · comping · bounce        │
├──────────────────────────────┴─────────────────────────────────────────┤
│  OmegaKit    app scaffold · device manager · project file · widgets    │
├────────────────────────────────────────────────────────────────────────┤
│  Omega       sequencer · MIDI · TempoMap · TimeSignatureMap · C API    │
└────────────────────────────────────────────────────────────────────────┘
```

OmegaHost and OmegaAudio sit side by side at the same layer — both produce audio that feeds into OmegaMix. The difference is source: OmegaHost generates audio through plugins; OmegaAudio reads it from disk. They meet in OmegaMix channel strips.

---

## Open Questions for Future Brainstorming

- Should time-stretching be real-time (applied in the AudioClipNode process function each buffer) or offline (rendered to a new temp file on demand)? Real-time is more flexible; offline is cheaper on CPU during playback.
- Is RubberBand (GPL) acceptable as a dependency, or does the library license need to stay more permissive? SoundTouch (LGPL) is the alternative.
- Should warp markers use Omega's existing `AnchorList` infrastructure, or introduce a separate concept in OmegaAudio?
- Is MIDI-triggered audio playback in scope — clips that fire based on MIDI note-on, like a sampler? This would bridge OmegaAudio and the PerformanceSource concept from Omega.
- Should OmegaAudio include a basic pitch-correction pass (auto-tune style) as a non-destructive clip effect, or leave that to OmegaHost plugins?
- Spectral editing: editing the frequency content of audio directly (like iZotope RX or Izotope Spectral Repair) — future extension or out of scope?
- Multi-channel audio (stems > stereo): should audio tracks support more than two channels (5.1, Atmos objects) from day one, or stereo-first?
- Should bounce-faster-than-real-time be limited to offline export only, or should it also support "freeze track" (freeze a track to audio to save CPU, unfreeze to edit)?
