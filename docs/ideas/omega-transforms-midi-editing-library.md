# Idea Document: OmegaTransforms — MIDI and Editing Function Library

## Context

Every layer of the stack manipulates MIDI events in some way, but none of them is specifically a library of *algorithms*. Quantize, humanize, arpeggiate, harmonize, retrograde, invert, euclidean, isorhythm, Markov, phasing — these are musical operations that get reimplemented in every sequencer application ever written.

OmegaTransforms is a library of reusable, composable MIDI and editing functions ranging from the everyday (quantize, transpose, velocity curve) to the unusual (isorhythm, negative harmony, cellular automata, spectral voicing). It has no opinion about where in the stack its functions are used — they can be applied as non-destructive real-time processors in OmegaHost's signal graph, as undoable edit Commands in OmegaKit's history, or as standalone generators registered as EventSource instances in the Omega engine.

No code will be written. This is a pure ideas document.

---

## Architecture: Four Layers, One Library

```
omega-transforms
├── core/          Pure C++ algorithms. No Omega dependency.
│                  Input: event range. Output: event vector.
│                  Testable in complete isolation.
│
├── rt/            Real-time TransformSource wrappers.
│                  Each core function wrapped as a TransformSource.
│                  Plugs into OmegaHost's MIDI signal graph.
│                  Processes events per engine cycle, zero allocation.
│
├── edit/          Edit Command implementations.
│                  Each editing function is a Command with execute()/undo().
│                  Plugs into OmegaKit's CommandHistory.
│                  Applied destructively (but undoably) to event lists.
│
├── gen/           Event generators implementing EventSource.
│                  Registered via omega_engine_add_source().
│                  Generate events from musical rules or probability.
│
└── analysis/      Read-only analysis functions.
                   Input: event range. Output: structured reports.
                   Used by OmegaAPI's describe/analyze operations.
```

A developer who wants only the algorithms imports `core/`. One who wants real-time MIDI processing imports `rt/`. One building an editor imports `edit/`. One building a generative system imports `gen/`. They compose freely.

---

## Category 1: Real-Time Processors (rt/)

These wrap a core algorithm as a `TransformSource`. They sit between an upstream source and downstream sinks in OmegaHost's MIDI graph. The upstream source (a TimelineSource track, a PerformanceSource slot, a hardware MIDI input) feeds events into the processor; the processor transforms them and outputs to the next stage.

Multiple processors can be chained — each is a `TransformSource` wrapping the previous one.

---

### Arpeggiator

Intercepts chord note-ons and replaces them with sequential single-note playback at a configurable rate.

**Parameters:**
- Rate: note value (whole, half, quarter, eighth, sixteenth, triplet variants)
- Pattern: Up / Down / Up-Down / Down-Up / As Played / Random / Chord (all simultaneously)
- Octave range: 1–4 (repeats pattern transposed up by octave)
- Gate: note length as % of step duration (staccato to legato)
- Swing: apply shuffle to the arp rate
- Latch: hold last chord when keys are released; clear on new chord

The arpeggiator holds the active note set (from incoming note-ons), generates note-on/off pairs at the arp rate timed to the engine's tick position, and clears on note-off (or latch holds until override).

---

### Chord Spreader / Strum

Delays the note-ons of a chord by small offsets to simulate a strummed string instrument.

**Parameters:**
- Direction: low-to-high, high-to-low, alternating, random
- Spread time: total time across all strings (0–200ms, or in ticks)
- Velocity curve: each successive note slightly louder or softer (simulates pick attack gradient)
- Pattern presets: guitar strum, harp gliss, rasgueado, banjo roll

---

### Scale Quantizer (real-time)

Snaps incoming note pitches to the nearest degree of the current scale, in real time.

**Parameters:**
- Scale source: session PerformanceContext (follows global key), or fixed scale per instance
- Behavior for chromatic passing notes: snap, pass through, or silence
- Output: corrected note-on with original velocity and duration

Useful for constraining a MIDI keyboard to a scale without manual transposition.

---

### Harmonizer

Adds N voices to each incoming note, voiced according to a scale and harmony rule.

**Parameters:**
- Voices: 1–4 added voices (up to 5-voice output from 1 input)
- Intervals: fixed intervals (thirds, fifths) or scale-aware (stays in key)
- Voice type: above, below, or both
- Scale source: session PerformanceContext or fixed
- Octave limit: voices constrained to a range so they don't fly off to extremes

Each added voice fires as a separate note-on at the same tick. All voices track the original's velocity (or offset ±N from it).

---

### Chord Detector → PerformanceContext

Listens to incoming notes and infers the current chord (root + type). Writes the result to the session's PerformanceContext each cycle. Downstream sources (including the Omega engine's other event sources) can read the detected chord.

**Parameters:**
- Detection window: how long to collect notes before deciding (in ms or beats)
- Confidence threshold: minimum certainty before updating the context
- Root bias: prioritize certain roots (the bass note, the lowest note, the most recent)
- Output: writes `omega_ctx_set_chord()` — other sources read it via ProcessContext

This is the "automatic chord follower" that lets a keyboard player drive the session's harmonic context without manual input.

---

### Legato Processor

Extends the duration of each note to reach exactly to the start of the next note on the same channel — removes gaps between notes.

**Parameters:**
- Mode: true legato (notes touch with no gap), overlap (notes slightly overlap for portamento effects), trim only (only shorten notes that overlap, don't extend gaps)
- Max extension: don't extend a note beyond N ticks (prevent infinite sustain on the last note)
- Per pitch: apply only between notes of the same pitch (melodic legato)

---

### Polyphony Limiter

Caps the number of simultaneously sounding notes on a channel. Notes beyond the limit are silenced.

**Parameters:**
- Limit: 1–127
- Priority: keep newest (steal oldest), keep oldest (steal newest), keep loudest, keep lowest, keep highest
- Apply mode: per MIDI channel, or global across all channels

Useful for constraining polyphony to match a hardware synth's voice count.

---

### Velocity Curve

Remaps incoming note velocities through a user-defined curve before output.

**Parameters:**
- Curve shape: linear, exponential, logarithmic, S-curve, or user-drawn (breakpoint list)
- Input range: compress a narrow range to full output range (velocity expansion)
- Output range: compress full input range to a narrow output range (velocity compression)
- Fixed output: map all velocities to one value (useful for drum machines with no velocity)

---

### MIDI Echo (Delay)

Re-fires each incoming note N times at increasing tick offsets, with velocity decay per repeat.

**Parameters:**
- Delay time: in ticks (musical) or ms (absolute)
- Repeats: 1–8
- Velocity decay: % reduction per repeat
- Pitch shift per repeat: ±N semitones (creates rising/falling echo effects)
- Feedback: output of the echo feeds back into itself (for infinite reverb effect — with careful gain)

---

### Channel Router / Splitter

Routes incoming events to different output sinks or channels based on rules.

**Parameters:**
- Route by pitch range: low notes → channel 10 (drums), high notes → channel 1 (melody)
- Route by velocity: soft notes → one sink, loud notes → another
- Route by event type: notes → channel 1, CCs → channel 2
- Fan-out: copy every event to multiple outputs simultaneously

---

### Monophonizer

Enforces monophonic (single-note) playback. When a new note-on arrives while a note is sounding, the previous note receives an immediate note-off.

**Parameters:**
- Priority: last-note (most recent wins), high-note, low-note
- Retrigger: when priority note is released, resume the next-priority held note
- Glide: emit a pitch bend event between pitches (for portamento on non-hardware synths via CC)

---

### Note Range Filter

Passes only notes within a defined pitch range; silences or blocks notes outside it.

**Parameters:**
- Low bound (MIDI 0–127 or note name)
- High bound
- Mode: silence outside (note-off emitted), or block (no event at all)
- Invert: pass only notes *outside* the range (exclude a range)

---

## Category 2: Edit Operations (edit/)

These are applied to an event list as a `Command` — execute() transforms the list; undo() restores the original. All are composable (a macro command can contain several).

---

### Quantize

Move notes' tick positions toward the nearest grid point.

**Parameters:**
- Grid: note value (whole, half, quarter, eighth, sixteenth, thirty-second, triplet variants)
- Strength: 0–100% (0 = no change, 100 = snap to grid exactly)
- Swing: apply swing factor to alternate grid points
- Groove template: pull toward a groove template's timing deviations instead of a pure grid
- What to quantize: note-on only, note-off only, both, CCs
- Range: tick range to apply within

---

### Humanize

Add controlled randomness to timing and velocity to counter mechanical quantization.

**Parameters:**
- Timing randomness: ±N ticks (normally distributed; seed configurable for reproducibility)
- Velocity randomness: ±N (0–127 clamped)
- Apply to: timing only, velocity only, or both
- Per-note random seed: same seed = same humanization (deterministic variation; different seed = new feel)

---

### Swing

Delay alternating subdivisions to create a shuffle feel, without changing note pitches.

**Parameters:**
- Grid: which subdivisions are affected (eighth notes, sixteenth notes)
- Amount: 50% = straight, 67% = heavy shuffle (triplet feel), configurable 50–75%
- Push: delay even beats (standard swing) or odd beats

---

### Transpose

Shift all note pitches by a fixed interval.

**Parameters:**
- Semitones: ±N (unclamped; clamp to 0–127 MIDI range is optional)
- Scale-aware: move each note to the nearest scale degree in the transposed key (avoid chromatic notes)
- Octave: transpose by ±N octaves (12 semitones × N)
- Range filter: only transpose notes in a pitch range

---

### Invert

Mirror note pitches around an axis pitch. The axis is typically the root of the key.

Example: inversion around C4 (60) means a note at E4 (64) becomes Ab3 (56) — the same interval below the axis.

**Parameters:**
- Axis pitch: MIDI note or note name
- Scale-aware inversion: snap inverted pitches to scale degrees (avoids chromatic pitches)
- Mode: strict (exact pitch mirror) or tonal (diatonic inversion within the key)

---

### Retrograde

Reverse the order of events in time. The last event becomes first, the first becomes last. Duration is preserved.

**Parameters:**
- Range: full pattern or a tick range
- Maintain position: retrograde events within their original time range (flip around the midpoint) vs. start from position 0
- Retrograde of CCs: apply to CCs as well as notes, or notes only

---

### Augmentation and Diminution

Stretch or compress the temporal spacing of events.

- **Augmentation**: multiply all tick positions and durations by a ratio (e.g., ×2 = twice as slow)
- **Diminution**: divide (e.g., ÷2 = twice as fast)
- **Arbitrary ratio**: any float multiplier, not just integer (e.g., ×1.5 for sesquialtera)
- **Fit to range**: stretch/compress to exactly fill a target duration in bars

---

### Velocity Ramp

Gradually change note velocities from a start value to an end value across a tick range (crescendo / decrescendo).

**Parameters:**
- Start velocity (or "as is" to use the current velocity of the first note)
- End velocity
- Curve: linear, exponential, logarithmic
- Range: tick start and end

---

### CC Thin

Remove redundant CC events — events whose value is the same as the previous event of the same CC number, or events spaced more frequently than necessary.

**Parameters:**
- Minimum spacing: remove CCs closer than N ticks apart
- Redundancy threshold: remove events whose value differs from previous by less than ±N
- Target CC: apply to a specific CC number, or all CCs

---

### Legato Edit (offline)

Extend note durations so each note ends exactly where the next one begins (post-quantize version of the Legato Processor).

---

### Overlap Fix

Shorten notes that overlap with subsequent notes on the same pitch or channel — prevents hung notes on monophonic synthesizers.

**Parameters:**
- Mode: shorten outgoing note to non-overlap, or delete the overlap portion
- Minimum duration: never shorten a note below N ticks

---

### Explode

Split a polyphonic track into N monophonic tracks — one pitch per track, or one voice per track.

**Parameters:**
- Method: by pitch (each unique pitch → its own track), by order (highest note → track 1, second → track 2, etc.), by channel
- Target: create new tracks, or output to existing tracks

---

### Implode

Merge multiple tracks into one polyphonic track.

**Parameters:**
- Merge mode: sum all events onto one track, or interleave by tick
- Deduplicate: remove events at the same tick with the same pitch

---

### Chop

Divide notes into a series of shorter equal notes at a grid rate.

**Parameters:**
- Grid: divide each note into pieces of this note value
- Gate: length of each piece as % of the grid interval
- Velocity: constant, or ramp down (natural decay simulation)

---

### Roll Generator

Generate rapid repeated notes from a longer note — drum roll, tremolo.

**Parameters:**
- Rate: repetitions per beat (16th, 32nd notes, etc.)
- Velocity shape: flat, crescendo, decrescendo, random
- Duration: apply to the full note duration

---

### Flam Generator

Add a "flam" to drum hits — a ghost note just before the main hit.

**Parameters:**
- Flam offset: ticks before the main hit (typically 10–30 ticks)
- Ghost velocity: velocity of the preceding ghost note (typically 50–70% of main)
- Sticking pattern: apply flam to every hit, alternate hits, or user-selected hits

---

### Pitch Correction (offline)

Snap notes to the nearest scale degree. The offline (destructive) version of the Scale Quantizer.

**Parameters:**
- Scale (root + mode)
- Strength: 0–100% (partial snap vs. full)
- Pass-through threshold: notes already within N semitones of a scale degree are untouched

---

## Category 3: Generators (gen/)

These implement `EventSource` and generate events from scratch, registered into the Omega engine alongside the built-in sources.

---

### Euclidean Rhythm Generator

Distributes N pulses across K steps as evenly as possible — the Björklund algorithm. Produces rhythms found in world music: Cuban clave, West African, Balkan.

**Parameters:**
- Pulses (N): number of hits
- Steps (K): total steps in the pattern
- Rotation: offset the pattern start by R steps (shifts the downbeat position)
- Note: which MIDI note to trigger on each pulse
- Velocity: fixed, or a velocity curve across the pattern
- Length: note duration in ticks
- Rate: step duration in ticks (sixteenth = 120 ticks at 480 PPQN)

Examples: E(3,8,0) = Tresillo. E(5,8,0) = Cuban clave variant. E(7,12,0) = West African bell pattern.

---

### Isorhythm Generator

A medieval polyphonic technique: a **talea** (rhythm pattern) and **color** (pitch series) of different lengths repeat independently. Their interaction creates a rich, long-form structure before cycling back to the beginning.

**Parameters:**
- Talea: a rhythm pattern (list of durations and rests)
- Color: a pitch series (list of MIDI notes)
- Repeats: how many times to cycle each, or auto (cycle until LCM of lengths is reached)
- Output track: which sink receives the events

Example: talea = [480, 240, 480, 240, 480] (5 elements), color = [60, 62, 64, 65, 67, 69] (6 elements). LCM = 30 elements before full cycle. Every pair produces a different rhythm/pitch combination.

---

### Phasing Generator

Two identical patterns play simultaneously; one is slightly longer per cycle, causing them to gradually drift out of phase and back into alignment — Steve Reich's "Piano Phase" technique.

**Parameters:**
- Pattern: the shared source pattern
- Phase rate: how many ticks per cycle the second voice drifts
- Output sinks: two output destinations (the two voices)
- Sync mode: auto-realign at full cycle (LCM of lengths), or drift indefinitely

---

### Markov Chain Melody Generator

Generate melodies from a probability transition matrix: given the current note, the next note is chosen by probability.

**Parameters:**
- Transition matrix: N×N probability table (N = number of pitches in the model)
- Pitch set: which MIDI notes are in the vocabulary
- Duration distribution: probability table of note lengths
- Velocity distribution: probability table or fixed
- Training mode: learn the transition matrix from an existing pattern or track
- Random seed: for reproducibility

The trained matrix captures the "feel" of the source material. Generate new melodies that share its statistical character without literally repeating it.

---

### L-System Generator

Turtle-graphics-style rewriting system applied to musical events. Produces self-similar, fractal structures.

**Parameters:**
- Alphabet: symbols mapping to musical operations (N = note, R = rest, U = transpose up, D = transpose down, P = push state, p = pop state, etc.)
- Axiom: starting string
- Production rules: rewriting rules applied each generation (e.g., N → NUN, U → UD)
- Generations: how many rewriting steps to apply
- Note mapping: which MIDI note each N-event produces

After G generations, the string is "played" left to right, interpreting each symbol as a musical action. Produces branching, self-repeating patterns that sound organic.

---

### Cellular Automata Generator

Apply a 1-dimensional cellular automaton (e.g., Wolfram's Rule 30, 90, 110, 126) to generate rhythms or melodies.

**Parameters:**
- Rule: 0–255 (Wolfram elementary automata numbering)
- Initial state: seed row (single center cell = "standard", random = generative)
- Row interpretation: each row = one bar; each live cell = one step in the rhythm
- Pitch mapping: map cell position to MIDI pitch (left = low, right = high)
- Steps: how many rows to generate

Rule 30 is chaotic (pseudo-random, no repeating). Rule 90 is fractal (Sierpinski triangle). Rule 110 is Turing-complete. Each produces a completely different feel.

---

### Stochastic Step Generator

A step sequencer where each step has a configurable probability of firing.

**Parameters:**
- Steps: N steps (8, 16, 32, 64)
- Per step: probability (0–100%), note, velocity, length
- Seed: fixed seed = repeatable pattern; random seed = evolving generative output
- Re-seed mode: re-randomize every N bars (pattern evolution), or lock

---

### Spectral Chord Generator

Voice a chord according to the harmonic series — lower intervals wide, upper intervals compressed — mimicking the natural overtone structure of acoustic sound.

**Parameters:**
- Root: fundamental pitch
- Density: how many partials to include (2–16)
- Register: starting octave for the fundamental
- Partials to include: all (1, 2, 3, 4, 5, 6, 7, 8...), odd only (1, 3, 5, 7...), prime only, custom
- Quantize to: 12-TET (snap partials to nearest semitone), just intonation (via pitch bend), or leave as pitch bend deviations

Produces chords that are not in any traditional harmony textbook but are aurally consonant — they align with how acoustic instruments naturally resonate.

---

### Polyrhythm Generator

Layer N rhythmic patterns with different cycle lengths, all starting simultaneously and cycling at their own rates.

**Parameters:**
- Layers: list of (cycle length, pulse count, note, velocity) tuples
- Sync: each layer loops independently vs. all sync at LCM
- Output: all layers to one track (sum), or one track per layer

Example: 3-beat cycle, 4-beat cycle, 5-beat cycle playing simultaneously = 60-beat total before full cycle.

---

### Counterpoint Generator

Generate a second voice that follows species counterpoint rules against an existing melody.

**Parameters:**
- Cantus firmus: the existing melody track
- Species: 1st (note against note), 2nd (two notes against one), 3rd (four against one), 4th (syncopated), 5th (florid — mixed)
- Mode: strict (1600s rules) or free (allow more dissonance)
- Voice range: the generated voice stays within a SATB range
- Output: new track

---

## Category 4: Unusual / Algorithmic Functions

These are the operations that do not appear in typical DAW toolsets but have strong musical or compositional value.

---

### Negative Harmony

Mirror note pitches around a tonal axis — the "negative" or "mirror" of a melody or chord within a key.

Theory: in a key, the tonic and dominant axes act as a mirror. The note a third above the tonic becomes a third below; major becomes minor; dominant becomes subdominant. Jacob Collier popularized this concept.

**Parameters:**
- Key root: the center of the mirror (e.g., C)
- Mode: diatonic mirror (stay in scale) or chromatic mirror (exact semitone reflection)
- Axis: tonic only, or tonic+dominant (authentic axis)
- Apply to: notes only, or chords (recalculate each chord's mirror)

Example: a C major chord (C, E, G) reflected around the tonic+dominant axis → C minor chord inverted (G, Eb, C).

---

### Metric Modulation

Smoothly change tempo by reinterpreting a subdivision as the new beat — the same physical duration becomes a different beat value.

**Parameters:**
- Old beat unit: what the current beat is (quarter note)
- Subdivision: which subdivision of the old beat becomes the new beat (triplet eighth = 2/3 of a quarter)
- New BPM: derived automatically (old BPM × ratio), or override
- Transition point: which bar the modulation occurs

Example: at 120 BPM, a triplet eighth note = 80ms. If that becomes the new quarter note, new BPM = 150. The music shifts up in tempo with no discontinuity.

OmegaTransforms computes the new BPM and inserts a TempoMap point at the transition tick. The perceptual effect: the groove subtly accelerates or decelerates without a hard cut.

---

### Pitch Class Set Operations

Twelve-tone and set theory operations on collections of pitches, independent of octave.

**Operations:**
- **Normal form**: reorder a pitch class set into its most compact representation
- **Prime form**: reduce to canonical form (most compact, starting from 0)
- **Inversion**: invert a set around 0 (or any axis)
- **Retrograde**: reverse the ordering
- **Transposition**: shift all pitch classes by T (mod 12)
- **Set complement**: pitches *not* in the set
- **Similarity**: Forte set-class matching — given a chord, find the closest set class in the Forte table
- **Z-relation**: find Z-related sets (same interval vector, different prime form)

These operations take note events as input, transform the pitch classes, and emit new note events with the transformed pitches redistributed across the original rhythm and octaves.

---

### Just Intonation Retune

Retune a MIDI sequence from equal temperament to just intonation — exact frequency ratios derived from the harmonic series — via pitch bend messages.

**Parameters:**
- Root: the 1/1 (the untempered pitch)
- Limit: 3-limit (Pythagorean), 5-limit (classic JI), 7-limit, 11-limit
- Temperament: just, meantone, quarter-comma meantone, Kirnberger III, Werkmeister III, custom
- Bend range: ±2 semitones (standard) or ±12 semitones (for extreme retuning)
- Per-channel tracking: use separate MIDI channels for each pitch to allow simultaneous retunes (requires multi-timbral output)

This inserts or modifies pitch bend events so that intervals are tuned to exact ratios. The result is smoother chords (no beating on perfect fifths) but requires a synth that responds to pitch bend.

---

### Rhythmic Canons

Generate a strict rhythmic canon — a pattern and a time-displaced copy of itself, offset by N ticks, layered to create interlocking textures.

**Parameters:**
- Source pattern: the base rhythm
- Voices: 2–8 canonic entries
- Entry interval: ticks between each entry
- Pitch offset per entry: transpose each voice (e.g., +12 semitones per entry = octave canon)
- Type: strict canon (exact copy), retrograde canon (mirror copy enters in reverse), augmentation canon (copy enters at half speed)

---

### Probability Matrix Melody

An N×N matrix where matrix[i][j] = probability that note j follows note i. Different from Markov chains in that the matrix is manually editable — the composer controls each transition probability explicitly.

**Parameters:**
- Pitch vocabulary: the N pitches in use
- Matrix: NxN table of probabilities (rows sum to 1.0)
- Velocity: uniform, or a separate velocity matrix
- Duration: uniform, or a duration matrix

The composer specifies "from C, 70% chance of going to E, 20% chance of going to G, 10% chance of going to F" etc. More intentional than training from data; produces a predetermined but non-deterministic feel.

---

### Fractal Melody (Self-Similar Interpolation)

Build a melody by recursive self-similar interpolation between two pitches.

Algorithm: given a start pitch A and end pitch B, the midpoint M is computed. Then the interval A→M is filled the same way, and M→B. After N levels of recursion, a pitch sequence emerges that is similar to itself at multiple time scales — the same contour governs the large scale and the small scale.

**Parameters:**
- Start pitch, end pitch
- Recursion depth: 2–8 levels
- Interpolation: linear (midpoint = average), weighted (bias toward one end), random-within-range
- Rhythm: uniform (equal duration per generated note), or fractal duration (duration also self-similar)

---

### Voice Leading Optimizer

Given a sequence of chord symbols or chord note-sets, compute the optimal voice leading — the movement between chords that minimizes total pitch motion.

**Parameters:**
- Chords: sequence of pitch sets
- Voices: N voices to maintain
- Voice ranges: soprano, alto, tenor, bass ranges (SATB) or custom
- Constraints: no parallel fifths/octaves, no voice crossing, max leap per voice
- Objective: minimize total semitone motion (smooth) or minimize maximum voice motion (balanced)

Returns the voice-led version of the chord sequence as a set of N parallel melody lines, each as a track.

---

### Rhythmic Diminution by Subdivision

A pattern is compressed so that each note becomes one of its subdivisions. Unlike simple time-compression (which only moves timestamps), this also modifies the note values so the result is rhythmically coherent.

Example: a quarter-note pattern becomes a sixteenth-note pattern — four times as fast, but each note keeps its rhythmic relationship to its neighbors.

---

### Spectral Transposition

Transpose a chord by frequency ratio rather than by semitones. Standard transposition moves all notes by the same number of semitones (equal intervals). Spectral transposition multiplies all frequencies by a constant ratio — the result is not necessarily in equal temperament.

**Parameters:**
- Ratio: e.g., 3/2 (perfect fifth up in just intonation), 5/4 (major third up), 2/1 (octave)
- Output: use pitch bend to approximate the non-ET pitches, or snap to nearest semitone

Useful for generating spectral music in the tradition of Murail, Grisey, and Haas.

---

## Category 5: Analysis Functions (analysis/)

Read-only. Input: a range of events. Output: structured data.

---

### Chord Detection

Given a set of simultaneous or near-simultaneous note-ons, identify the most likely chord name.

**Output:** root, quality (major, minor, dominant 7, major 7, minor 7, diminished, augmented, sus2, sus4, half-diminished, etc.), inversion, confidence score. If multiple interpretations are equally plausible, returns the top 3.

**Method:** pitch class set analysis + Forte table lookup + contextual bias toward key if PerformanceContext key is set.

---

### Key Detection

Analyze a sequence of notes and estimate the most likely key.

**Method:** Krumhansl-Schmuckler algorithm — correlate pitch class histogram against major and minor key profiles. Returns top 3 key candidates with confidence scores.

**Input:** note event range. Typically run over 4–8 bars for reliable results.

---

### Groove Analysis (Timing Deviation Map)

Measure how far each note deviates from the nearest grid point. Returns a map of (tick_position → deviation_ticks, deviation_velocity). This deviation map can be saved as a groove template and applied to other material via OmegaKit's GrooveLibrary.

**Output:** groove template data — the "feel" of a performance, extractable and reapplicable.

---

### Density Analysis

Count notes per bar, notes per beat, notes per subdivisions. Returns:
- Average note density across the range
- Density over time (histogram by bar)
- Peak density bar, minimum density bar
- Note-on rate (events per second)
- Polyphony histogram (how often 1, 2, 3, 4 notes are simultaneous)

---

### Pitch Histogram

Count occurrences of each pitch class (0–11) across the note range. Returns:
- Normalized frequency per pitch class (sums to 1.0)
- Most common pitch class (tonal center candidate)
- Pitch range (lowest and highest MIDI note)
- Mean pitch, median pitch
- Pitch class set (which pitch classes appear at all)

---

### Interval Vector

Compute the interval vector of the pitch class set — a summary of how many times each interval class (1 through 6 semitones) appears between all note pairs. Standard in 12-tone theory (Forte's set theory).

---

### Dissonance Score

Rate the harmonic tension of a set of simultaneous notes using psychoacoustic consonance/dissonance models (Tenney's harmonic distance, or Plomp-Levelt roughness).

**Output:** scalar dissonance value (0 = pure consonance, higher = more dissonant). Useful for: finding the most dissonant moment in a progression, constraining generated harmonies to a target dissonance level.

---

### Rhythmic Complexity Score

Rate the rhythmic complexity of a drum or rhythm pattern.

**Method:** Keith's complexity measure (based on the syncopation model), or Toussaint's "evenness" metric. Returns a scalar 0–1 where 0 = perfectly even (all on the beat) and 1 = maximally complex.

---

### Voice Leading Analysis

Given two consecutive chords (each as a set of pitches), compute the voice leading — how each voice moves from the first chord to the second. Returns:
- Per-voice motion (in semitones)
- Total motion (sum of absolute semitone movements)
- Smoothness rating (inverse of total motion)
- Parallel fifths/octaves detected (boolean flags)
- Voice crossings detected

---

## Integration Points Across the Stack

| OmegaTransforms component | Integrates as |
|---|---|
| `rt/` TransformSource wrappers | MIDI processing nodes in OmegaHost signal graph |
| `edit/` Command implementations | Undoable Commands in OmegaKit CommandHistory |
| `gen/` EventSource generators | Registered via `omega_engine_add_source()` |
| `analysis/` functions | Called by OmegaAPI `analyze_section()`, `describe_session()` |
| Groove templates (from Groove Analysis) | Stored in Omega's GrooveLibrary |
| Chord Detection output | Writes to PerformanceContext via `omega_ctx_set_chord()` |
| Scale Quantizer (rt/) | Reads from PerformanceContext scale via ProcessContext |
| Voice Leading Optimizer | Generates tracks via OmegaKit's `CommandHistory` |

---

## The Complete Stack Picture (with OmegaTransforms)

```
┌───────────────────────────────────────────────────────────────────────┐
│  AI Agents (OmegaAPI — MCP, REST, WebSocket, Python SDK)              │
├───────────────────────────────────────────────────────────────────────┤
│  OmegaMix    channel strips · buses · VCAs · automation · metering    │
├────────────────────────────┬──────────────────────────────────────────┤
│  OmegaHost                 │  OmegaAudio                              │
│  plugin graph · CV routing │  recording · clips · editing · playback  │
├────────────────────────────┴──────────────────────────────────────────┤
│  OmegaTransforms   real-time processors · edit commands ·             │
│                    generators · analysis · algorithmic functions      │
├───────────────────────────────────────────────────────────────────────┤
│  OmegaKit    app scaffold · device manager · project file · widgets   │
├───────────────────────────────────────────────────────────────────────┤
│  Omega       sequencer · MIDI · TempoMap · TimeSignatureMap · C API   │
└───────────────────────────────────────────────────────────────────────┘
```

OmegaTransforms is horizontal — it injects into every layer. An algorithm from `core/` can be used standalone, as a real-time `TransformSource` in OmegaHost, as an edit `Command` in OmegaKit, as a `gen/` generator in Omega, or called from OmegaAPI's analysis endpoints.

---

## Open Questions for Future Brainstorming

- Should the unusual/algorithmic generators (L-system, cellular automata, isorhythm) live in OmegaTransforms core, or in a separate `omega-experimental` library with a less stable API contract?
- Is there value in a visual node editor specifically for chaining `rt/` TransformSource processors — a "MIDI FX rack" view separate from OmegaHost's audio patchbay?
- For the Markov chain generator: should it support second-order (note pairs) and higher-order transitions, or is first-order sufficient for most musical use?
- Should the Just Intonation Retune function target MIDI 2.0's per-note pitch attribute (when available) rather than pitch bend — giving true independent tuning per note without channel overhead?
- Is a visual matrix editor for the Probability Matrix Melody (a clickable NxN grid in the UI) worth specifying here, or left to the application layer?
- Should OmegaTransforms include a "MIDI script" interpreter — a simple DSL for writing custom transforms in a safe sandbox — so non-C++ developers can define their own algorithms?
- Spectral analysis integration: could OmegaAudio's audio content (frequency analysis of recorded clips) feed into OmegaTransforms' Spectral Chord Generator, turning recorded audio into MIDI chord events automatically?
