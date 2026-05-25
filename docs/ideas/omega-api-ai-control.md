# Idea Document: OmegaAPI — Unified AI-First Control Layer for the Omega Stack

## Context

Omega, OmegaKit, OmegaHost, and OmegaMix are each designed for developers writing C++ applications. Their APIs speak in ticks, channel indices, MIDI note numbers, bitmasks, and node handles. An AI agent — an LLM, a generative music model, a reinforcement learning policy, or an autonomous composition engine — cannot work at that level without enormous translation overhead.

OmegaAPI is a unified control layer above the entire stack with two goals:

1. **Single surface**: one API that controls Omega (sequencer), OmegaHost (plugins), and OmegaMix (mixing console) without the caller needing to know which layer owns what
2. **AI-first design**: the API speaks in musical concepts, accepts intent rather than implementation details, returns rich observable state, and is structured to be called by LLMs via function calling, by ML pipelines, by autonomous agents, and by real-time generative systems

No code will be written. This is a pure ideas document.

---

## Why AI Needs a Different API

A human developer calling `omega_engine_add_event()` knows what a tick is, what a sink_id means, and how to compute a duration in 480-PPQN ticks. An AI agent calling the same function would need to:

- Convert bar/beat position to absolute ticks using the current tempo map
- Look up the sink_id for the track named "bass"
- Convert the note name "C3" to MIDI number 48
- Convert "quarter note" to 480 ticks
- Convert "mf" to velocity 70

And then hope nothing went wrong with any of those conversions.

Multiply that translation burden across every operation an AI might perform, and the AI spends most of its "thinking budget" on bookkeeping rather than musical decisions.

OmegaAPI inverts this. The API accepts musical intent; OmegaAPI translates to the implementation. The AI thinks about music. The library handles numbers.

### What AI needs from an API

| Need | Why |
|---|---|
| **Musical vocabulary** | Speak in bars, note names, dynamics, key, mode — not ticks, MIDI numbers, bitmasks |
| **Rich readable state** | "What is currently happening?" — full, queryable session state at any moment |
| **Intent operations** | "Add a chord here" rather than "add three note-on events with these parameters" |
| **Safe mutation** | Every change undoable; batch operations atomic; no way to corrupt the session |
| **Observable events** | AI can subscribe to what happens — note fires, bar changes, transport stops |
| **Generation assistance** | Helper operations that do musical reasoning (fill a pattern, harmonize, suggest) |
| **Error recovery** | When an operation is invalid, explain why and suggest what would be valid |
| **Low latency for real-time** | Live AI performance needs sub-10ms decision latency on bar boundaries |

---

## API Surfaces

OmegaAPI is not one protocol — it exposes multiple surfaces for different use cases.

```
┌──────────────────────────────────────────────────────────────────────────┐
│                           Your AI Agent / App                            │
├──────────────┬────────────────┬──────────────────────────────────────────┤
│  MCP Server  │   REST / HTTP  │  WebSocket (streaming)  │  Python SDK    │
├──────────────┴────────────────┴──────────────────────────────────────────┤
│                        OmegaAPI  (semantic bridge)                       │
├──────────────────────────────────────────────────────────────────────────┤
│   Omega  │  OmegaKit  │  OmegaHost  │  OmegaMix                         │
└──────────────────────────────────────────────────────────────────────────┘
```

### Surface 1: MCP Server (primary AI interface)

Model Context Protocol is the standard for exposing tools and resources to LLMs. OmegaAPI runs as an MCP server; any MCP-compatible AI client (Claude, GPT-4, local models via compatible runtimes) can connect and control the full stack.

**MCP Resources** (read-only, AI can always inspect):
- `omega://session` — full session state: tempo, key, time sig, transport position, track list
- `omega://tracks` — all tracks with names, MIDI routing, mute/solo state, event summary
- `omega://patterns` — all patterns in the pattern library with content summaries
- `omega://performance` — all 64 performance slots: assigned pattern, state, parameters
- `omega://mix` — all channel strips with current fader, EQ, compression, automation state
- `omega://plugins` — all loaded plugin instances with parameter states
- `omega://playing` — live snapshot of what is currently sounding (active notes, active slots)

**MCP Tools** (callable write operations):
Everything in the Operations section below is exposed as an MCP tool with a clear description, typed parameters, and a structured response.

**MCP Prompts** (pre-built task templates):
- `generate_drum_pattern` — guided generation with style, density, and bar-length parameters
- `harmonize_melody` — add voices to an existing melodic track
- `create_arrangement` — build a song arrangement from a list of section names
- `mix_for_style` — apply mix settings appropriate for a genre description

### Surface 2: REST / HTTP

A JSON REST API running on localhost. All MCP tools are available as HTTP endpoints. Designed for:
- Web-based AI clients and browser tools
- Integration with OpenAI function calling (which expects JSON schema tool definitions)
- LangChain, LlamaIndex, and other orchestration frameworks
- Scripts and CI pipelines

Every endpoint:
- Accepts JSON body
- Returns JSON with: `result`, `state_delta` (what changed), `session_summary` (key session facts for context), `available_actions` (what can be called next)

### Surface 3: WebSocket (streaming events)

AI agents that react to real-time events need a push channel. The WebSocket stream emits:

| Event | When |
|---|---|
| `transport.started` | Play begins |
| `transport.stopped` | Transport stops |
| `bar.changed` | Every new bar (with bar number, position tick, tempo) |
| `beat.changed` | Every new beat |
| `note.on` | A note fires (track, pitch, velocity, tick) |
| `note.off` | A note ends |
| `slot.state_changed` | A PerformanceSource slot changes state (IDLE→PLAYING, etc.) |
| `parameter.changed` | Any mix or plugin parameter changes |
| `session.modified` | Any mutation completed (what changed, by whom) |

An AI live performance agent subscribes to `bar.changed` and decides what to cue next. An AI mix engineer subscribes to `parameter.changed` and observes the results of its adjustments. A generative system subscribes to `note.on` to react to what a human player is doing.

### Surface 4: Python SDK

A Pythonic wrapper over the REST API. Designed for:
- ML-based music generation (integrate with PyTorch, TensorFlow, music21, magenta)
- Algorithmic composition scripts
- Research and education
- Rapid prototyping

```python
import omegaapi

session = omegaapi.connect()

# Read state
state = session.get_state()
print(state.key, state.tempo, state.current_bar)

# Add a note using musical vocabulary
track = session.tracks["bass"]
track.add_note(bar=2, beat=1, pitch="C2", duration="quarter", velocity="f")

# Generate a pattern
pattern = session.generate_pattern(
    style="funk bass line",
    key=state.key,
    bars=4
)
session.patterns.assign(slot=0, pattern=pattern)
session.performance.cue(slot=0, mode="next_bar")

# React to events
@session.on("bar.changed")
def on_bar(event):
    if event.bar % 8 == 0:
        session.performance.cue(slot=1, mode="immediate")
```

---

## The Semantic Bridge

OmegaAPI translates between musical vocabulary and stack implementation. This translation layer is the core of the library.

### Musical time ↔ ticks

| Input | Internal resolution | Example |
|---|---|---|
| `"bar 3 beat 2"` | MeterCursor → tick | Bar 3, beat 2 at 4/4 = tick 2880 |
| `"0:32.500"` (MM:SS.mmm) | TempoMap ns → tick | Nanoseconds via TempoMap |
| `"00:00:32:12"` (SMPTE) | SmpteConverter → tick | If SMPTE config present |
| `4.5` (bars, float) | MeterCursor → tick | 4 bars + 2 beats at 4/4 |
| Tick (int) | Identity | Direct pass-through for power users |

### Note names ↔ MIDI numbers

Standard scientific pitch notation: `"C4"` = 60, `"D#3"` = 63, `"Bb5"` = 94. Middle-C convention is configurable (C3 or C4 default). Enharmonic spelling: sharps or flats, configurable per session.

### Dynamics ↔ velocity

| Marking | Velocity |
|---|---|
| `ppp` | 16 |
| `pp` | 33 |
| `p` | 49 |
| `mp` | 64 |
| `mf` | 80 |
| `f` | 96 |
| `ff` | 112 |
| `fff` | 127 |
| Integer 0–127 | Direct |

### Duration names ↔ ticks

| Name | Ticks (480 PPQN) |
|---|---|
| `whole` | 1920 |
| `half` | 960 |
| `quarter` | 480 |
| `eighth` | 240 |
| `sixteenth` | 120 |
| `thirty-second` | 60 |
| Triplet variants | × 2/3 |
| Dotted variants | × 3/2 |
| `"1.5 beats"` | 720 |
| Integer (ticks) | Direct |

### Key / scale ↔ scale mask

`"C minor"` → root=0, mask=0b101101101011 (natural minor). `"F# Dorian"` → root=6, mask=Dorian pattern. Named modes: Ionian, Dorian, Phrygian, Lydian, Mixolydian, Aeolian, Locrian, plus Harmonic minor, Melodic minor, Pentatonic variants, Blues, Chromatic.

### Track name ↔ sink_id / track_id

Track names in OmegaAPI are first-class. `session.tracks["kick"]` resolves the name to the internal `track_id` and `sink_id` transparently. Ambiguous names return a selection prompt. New tracks are named at creation and the name is the primary key throughout the API.

---

## Operations

### Session control

```
get_state()                    → full session snapshot
set_tempo(bpm, [at_bar])       → set tempo, optionally at a bar position
set_key(root, mode)            → set scale root and mode ("C", "minor")
set_time_signature(num, denom, [at_bar])
set_swing(amount)              → 0.0–1.0
locate(position)               → move playhead (bar, beat, or tick)
play()
stop()
record_arm(track, enabled)
checkpoint(name)               → named undo point
undo([steps])
redo([steps])
rollback(checkpoint_name)
```

### Track and event operations

```
create_track(name, [midi_channel], [midi_device])  → TrackRef
delete_track(name)
list_tracks()                                       → [{name, channel, muted, solo, armed}]

add_note(track, position, pitch, duration, velocity)
add_chord(track, position, pitches[], duration, velocity)
add_cc(track, position, controller, value)
remove_events(track, from_position, to_position, [type_filter])
move_events(track, from_position, to_position, offset)
transpose_events(track, from_position, to_position, semitones)
quantize_events(track, from_position, to_position, grid, strength)
get_events(track, [from_position], [to_position])  → [Event]
```

### Pattern operations

```
create_pattern(name, length)              → PatternRef
delete_pattern(name)
add_note_to_pattern(pattern, ...)         → same signature as add_note
fill_pattern(pattern, style, [options])   → see Generation section
clone_pattern(source, new_name)
reverse_pattern(pattern)
transpose_pattern(pattern, semitones)
get_pattern(name)                         → full event list
list_patterns()
```

### Performance / live control

```
assign_slot(slot, pattern)
cue_slot(slot, mode)           → mode: "immediate"|"next_beat"|"next_bar"|"next_section"
stop_slot(slot, mode)
stop_all_slots(mode)
set_slot_transpose(slot, semitones)
set_slot_velocity_scale(slot, percent)
get_slot_state(slot)           → {state, pattern, parameters}
list_slots()
```

### Mix operations

```
set_fader(channel, db)
set_mute(channel, enabled)
set_solo(channel, enabled)
set_pan(channel, position)     → -1.0 to +1.0
set_send(channel, aux, db)
set_eq_band(channel, band, freq, gain, q)
set_compression(channel, threshold, ratio, attack, release)
get_strip_state(channel)       → full strip parameter snapshot
automate(channel, parameter, breakpoints[])  → set automation curve
```

### Plugin operations

```
load_plugin(name_or_id, format)   → PluginRef
unload_plugin(plugin)
set_plugin_parameter(plugin, param_name, value)
get_plugin_parameters(plugin)     → [{name, value, min, max, default}]
save_plugin_preset(plugin, name)
load_plugin_preset(plugin, name)
list_plugins()                    → installed plugin database
```

### Observation and query

```
get_active_notes()             → [{track, pitch, velocity, started_at}]
get_transport_state()          → {playing, recording, position, bar, beat, tick}
get_harmonic_context()         → {key, mode, chord, scale_notes}
get_mix_levels()               → [{channel, peak_db, rms_db, lufs}]
analyze_section(from, to)      → {note_count, pitch_range, density, dominant_pitches}
find_conflicts(tracks[])       → [{bar, beat, description}]
describe_session()             → natural language summary of the current session state
```

---

## Generation Assistance

OmegaAPI includes a generation layer — not a full AI model, but musical rule-based and template-based operations that reduce the work an AI caller has to do. These are operations that require musical knowledge (scale theory, rhythm theory, voice leading) but not machine learning.

### Pattern generation

`fill_pattern(pattern, style, options)` populates a pattern using a style specification:

```json
{
  "style": "four on the floor kick",
  "bars": 2,
  "density": 0.6,
  "key": "C minor",
  "reference_patterns": ["pattern_hihat"]
}
```

Built-in style templates: four-on-the-floor kick, backbeat snare, hi-hat 16th groove, Euclidean rhythm (N pulses in K steps), walking bass line (scalar), chord voicing (close/open/drop-2), blues shuffle, trap hi-hat, bossa nova clave.

The generation operation returns the filled pattern — the AI caller can inspect it, modify it, or discard it before committing.

### Harmonization

`harmonize(track, voices, style)` — given a melodic track, generates N additional voices following voice-leading rules for the given style (classical, jazz, pop, modal). Returns candidate voices as patterns for review before adding to the session.

### Chord progression suggestions

`suggest_progressions(key, length, style)` — returns a list of candidate chord progressions (as chord symbol lists) appropriate for the key and style. The AI caller picks one and maps it to events.

### Groove application

`apply_groove(track, template, strength)` — applies a rhythmic feel (quantization deviation + velocity curve) from the GrooveLibrary to a track. The template can be a named library entry or a custom breakpoint list.

### Scale-aware note correction

`snap_to_scale(track, [from], [to], allow_chromatic_passing)` — moves all notes in range to the nearest scale degree. Optionally keeps notes that function as chromatic passing tones (detected by short duration + stepwise motion).

---

## AI Agent Interaction Model

### State → Decision → Action → Observe loop

The canonical AI control loop:

```
1. READ:    agent calls get_state() and get_events() to understand the session
2. DECIDE:  agent reasons about what musical action to take
3. ACT:     agent calls one or more write operations (add_note, cue_slot, set_fader, ...)
4. OBSERVE: agent receives state_delta from the response; optionally reads updated state
5. REPEAT
```

Every write operation response includes a `session_summary` field — a compact JSON representation of the most relevant session facts — so the agent does not need to call `get_state()` separately after each action. This minimizes round-trips.

### Context window efficiency

An LLM calling OmegaAPI via MCP or HTTP has a limited context window. OmegaAPI response design minimizes token consumption:

- `get_events()` returns events in a compact symbolic format (`"C4/q/mf @ 1:1"`) not verbose JSON by default; verbose JSON available with a flag
- `describe_session()` returns a single paragraph that fits in a prompt prefix
- `get_state()` returns only non-default values (empty fields omitted)
- List operations return counts with optional pagination; AI pulls details on demand

### Safety model

AI agents make mistakes. OmegaAPI enforces:

**Automatic checkpointing**: before any write operation, OmegaAPI creates an implicit checkpoint. The last N checkpoints are always reversible (N configurable, default 50).

**Validation before execution**: write operations validate their parameters and return a structured error with a `suggestion` field if invalid. Example: `add_note` with a pitch outside the current scale returns `{error: "outside_scale", suggestion: "nearest_scale_note: D3", severity: "warning"}`. The AI can override (chromatic note) or accept the suggestion.

**Dry-run mode**: any operation can be called with `dry_run: true` — it validates and returns what would change, without executing. AI agents can preview before committing.

**Rate limiting**: configurable maximum mutations per second. Prevents a runaway agent from flooding the session with events.

**Read-only mode**: the API can be opened in read-only mode — useful for AI analysis agents that should observe but not modify.

### Multi-agent scenarios

Multiple AI agents can connect to OmegaAPI simultaneously. Example: a composition agent handles the sequencer (Omega layer), a mix agent handles faders and EQ (OmegaMix layer), and a real-time performance agent handles slot cueing (PerformanceSource layer).

Each agent has a named identity. The `session.modified` event includes the agent name that made the change. The mutation log shows the full history of which agent did what.

Conflict resolution: the last write wins at the parameter level. If two agents try to set the same fader simultaneously, both operations succeed (they queue through the SPSC command queue), and the second overwrites the first. Agents can lock a parameter temporarily (advisory lock, not enforced) to signal exclusive ownership.

---

## Real-Time AI Performance

For live AI performance — an agent that responds to a human musician and makes decisions in real time — latency is critical.

### Latency budget

At 120 BPM, one bar = 2 seconds. The AI must make its cueing decision and have it executed before the bar boundary. With 100ms of network latency (local REST) and 10ms of processing, the agent has ~1.85 seconds of decision time per bar — comfortable.

At 180 BPM, one bar = 1.33 seconds. Still workable for bar-level decisions.

Beat-level decisions at 180 BPM give 333ms per beat — tight. The WebSocket stream is preferred over REST for real-time use (no HTTP overhead per message).

### Event subscription for live response

```python
@session.on("bar.changed")
def on_bar(event):
    # Decide what to do at this bar boundary
    # event.bar, event.tempo, event.time_ns
    
    harmonic = session.get_harmonic_context()
    
    if should_change_pattern(harmonic):
        next_pattern = select_pattern(harmonic)
        session.performance.cue(slot=2, pattern=next_pattern, mode="immediate")

@session.on("note.on")
def on_note(event):
    # Human plays a note — AI responds
    if event.track == "keyboard_input":
        chord = detect_chord(recent_notes)
        session.set_key(chord.root, chord.mode)
```

### AI-to-AI musical dialogue

A real-time performance scenario where two AI agents converse musically:

- Agent A (melody): subscribes to `note.on`, listens to Agent B's bass line, generates complementary melodic responses
- Agent B (bass): subscribes to `bar.changed`, reads current chord from `get_harmonic_context()`, selects next bass pattern from PerformanceSource
- Both agents observe each other's outputs and adapt — an emergent musical conversation mediated by OmegaAPI

---

## Integration with the Full Stack

### One API, four layers

| Operation domain | Underlying layer |
|---|---|
| Transport, tracks, events, patterns, sequences | Omega |
| Project file, device manager, app lifecycle | OmegaKit |
| Plugins, signal graph, CV routing | OmegaHost |
| Channel strips, buses, automation, metering | OmegaMix |

The AI caller never needs to know which layer handles what. OmegaAPI routes calls internally.

### Extending OmegaAPI with custom operations

Developers who build domain-specific AI applications can register custom operations:

```cpp
omegaapi::register_tool("generate_euclidean_rhythm", {
    .description = "Fill a pattern with a Euclidean rhythm",
    .parameters = { {"pulses", "int"}, {"steps", "int"}, {"pitch", "string"} },
    .handler = my_euclidean_handler
});
```

Custom tools appear in the MCP tool list and REST API automatically. This allows domain specialists (drum machine builders, generative music researchers, live coders) to extend the API vocabulary without forking OmegaAPI.

---

## Authentication and Access Control

For deployments where the Omega stack runs on a server (shared studio, cloud rendering, collaborative session):

- **API key**: simple bearer token per connecting agent
- **Scope**: each key is granted scopes (read-only, sequencer-only, mix-only, full-access)
- **Rate limit**: per-key
- **Audit log**: all mutations logged with agent identity, timestamp, and operation

Local (single-user) deployments skip authentication by default (loopback only).

---

## Schema and Documentation Generation

OmegaAPI auto-generates:
- **OpenAPI 3.x schema** from operation definitions — import into any REST client or code generator
- **MCP tool manifest** — ready to paste into any MCP client configuration
- **JSON Schema** for all request and response types
- **Markdown reference docs** — operation name, description, parameters, response fields, examples

This means an AI model can be given the auto-generated tool manifest and immediately understand what operations are available — no manual documentation maintenance.

---

## Project File Integration

OmegaAPI adds an `api_log` section to the `.omega` project file — a replay log of all AI-initiated mutations in the session:

```json
{
  "api_log": [
    {
      "tick": 0, "agent": "composition-agent-v2",
      "op": "add_note", "track": "bass",
      "args": { "position": "1:1", "pitch": "C2", "duration": "quarter", "velocity": "mf" }
    }
  ]
}
```

This log is optional (can be disabled) and serves as an audit trail for AI-generated content — useful for understanding what an AI agent did to a session, and for replaying the session build step by step.

---

## The Complete Stack Picture (Updated)

```
┌─────────────────────────────────────────────────────────────────────────┐
│  AI Agents  (LLMs, generative models, RL policies, live coders)         │
├────────────┬────────────────┬─────────────────────┬─────────────────────┤
│ MCP Server │   REST / HTTP  │  WebSocket (stream)  │  Python SDK         │
├────────────┴────────────────┴──────────────────────┴─────────────────────┤
│  OmegaAPI  —  semantic bridge, generation helpers, safety, auth, schema  │
├─────────────────────────────────────────────────────────────────────────┤
│  OmegaMix  —  mixing console, automation, metering                       │
├─────────────────────────────────────────────────────────────────────────┤
│  OmegaHost —  plugin graph, CLAP/VST3/AU/LV2, CV routing                │
├─────────────────────────────────────────────────────────────────────────┤
│  OmegaKit  —  app scaffold, device manager, project file, widgets        │
├─────────────────────────────────────────────────────────────────────────┤
│  Omega     —  sequencer engine, C API, MIDI I/O, SMF, timing            │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Open Questions for Future Brainstorming

- Should OmegaAPI ship an embeddings-based semantic search for the plugin database — "find me a warm pad synth" → ranked plugin list?
- Should `fill_pattern` and `harmonize` use built-in rule-based algorithms, or call out to an external AI model (e.g., a MusicGen or MuseNet API)?
- Is there a case for a persistent AI "memory" resource in MCP — the agent can store session-specific context (preferred styles, learned patterns) and retrieve it across sessions?
- Should the WebSocket stream include audio feature data (spectral centroid, RMS, onset detection) so AI agents can react to the sonic content, not just the MIDI events?
- Multi-user collaborative sessions: can two human + AI teams share one OmegaAPI session with conflict resolution?
- Should OmegaAPI expose a `simulate(operations[])` endpoint — run a batch of operations on a copy of the session, return the resulting state, then discard — so an AI can evaluate many alternatives before committing?
- Is there appetite for a "music diff" operation — given two session states, describe the musical difference in natural language?
- Should the Python SDK include a Gym-style reinforcement learning environment (observation space, action space, reward function) for training RL-based composition agents on top of Omega?
