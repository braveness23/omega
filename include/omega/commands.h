#pragma once

#include <omega/omega.h>
#include <omega/smpte_converter.h>
#include <omega/types.h>

#include <cstdint>
#include <variant>

namespace omega
{
class EventInput;   // forward declaration for AddInputCmd / RemoveInputCmd
class EventSource;  // forward declaration for AddSourceCmd / RemoveSourceCmd
}  // namespace omega

namespace omega
{

/* Inserts one event into a track's sorted event vector. */
struct AddEventCmd
{
    TrackId track;
    Event event;
};

/* Removes the event at the given tick with the given within-tick index. */
struct DeleteEventCmd
{
    TrackId track;
    uint64_t tick;
    uint32_t index;  // 0-based position among events sharing the same tick
};

/* Sets the global tempo. bpm_milli is BPM × 1000 (e.g. 120 BPM = 120000). */
struct SetTempoCmd
{
    uint32_t bpm_milli;
};

// ── Tempo map commands ────────────────────────────────────────────────────────

/* Inserts or replaces a tempo point at tick. bpm_milli = BPM × 1000. */
struct SetTempoPointCmd
{
    uint64_t tick;
    uint32_t bpm_milli;
};

/* Removes the tempo point at tick (no-op if none exists there). */
struct RemoveTempoPointCmd
{
    uint64_t tick;
};

/*
 * Configures the transport loop region.
 * enabled=false leaves the start/end range intact but deactivates looping.
 */
struct SetLoopCmd
{
    uint64_t start_tick;
    uint64_t end_tick;
    bool enabled;
};

/* Assigns a pattern to a PerformanceSource slot and queues it for playback. */
struct CuePatternCmd
{
    SlotId slot;
    PatternId pattern;
    CueMode mode;
};

/* Controls transport state: play, stop, or locate to an absolute tick. */
struct TransportCmd
{
    TransportAction action;
    uint64_t locate_tick;  // only meaningful when action == LOCATE
};

/*
 * Appends one entry to the SongArrangement.
 * repeat_count of 0 skips the entry entirely.
 */
struct SongAppendCmd
{
    PatternId pattern_id;
    uint32_t repeat_count;
};

/* Clears all entries from the SongArrangement and resets playback position. */
struct SongClearCmd
{};

/* Assigns a pattern to a PerformanceSource slot without starting playback. */
struct PerfAssignCmd
{
    SlotId slot;
    PatternId pattern;
};

/* Queues a PerformanceSource slot to begin playing at the specified cue boundary. */
struct PerfCueCmd
{
    SlotId slot;
    CueMode mode;
};

/* Queues a PerformanceSource slot to stop at the specified cue boundary. */
struct PerfStopCmd
{
    SlotId slot;
    CueMode mode;
};

/* Queues all active PerformanceSource slots to stop at the specified cue boundary. */
struct PerfStopAllCmd
{
    CueMode mode;
};

/*
 * Sets the semitone transpose for a PerformanceSource slot.
 * Range: -24 to +24; applied to every note dispatched from this slot.
 */
struct PerfSetTransposeCmd
{
    SlotId slot;
    int8_t semitones;
};

/*
 * Sets the velocity scale for a PerformanceSource slot.
 * Range: 0–200; 100 = unity (no change). Applied as (velocity × scale) / 100, clamped 1–127.
 */
struct PerfSetVelocityScaleCmd
{
    SlotId slot;
    uint8_t scale;
};

/*
 * Sets the random pitch-bias probability for a PerformanceSource slot.
 * Range: 0–100 (percentage chance per note that a random ±5-semitone offset is applied).
 * 0 = no randomisation; 100 = every note receives an offset.
 */
struct PerfSetRandomBiasCmd
{
    SlotId slot;
    uint8_t bias;
};

/* Registers an EventInput with the engine so it is polled each process() cycle. */
struct AddInputCmd
{
    EventInput* input;  // non-owning
};

/* Unregisters an EventInput; polling stops after the next process() cycle. */
struct RemoveInputCmd
{
    EventInput* input;  // non-owning
};

// ── Performance context commands ──────────────────────────────────────────────

/* Sets the global scale (root note + semitone bitmask) in the PerformanceContext. */
struct SetCtxScaleCmd
{
    omega_scale_t scale;
};

/* Sets the current chord (root, type, voices) in the PerformanceContext. */
struct SetCtxChordCmd
{
    omega_chord_t chord;
};

/* Sets the global semitone transpose in the PerformanceContext. Range: -24 to +24. */
struct SetCtxTransposeCmd
{
    int8_t semitones;
};

/* Sets the global velocity scale in the PerformanceContext. Range: 0–200; 100 = unity. */
struct SetCtxVelocityCmd
{
    uint8_t velocity;
};

/* Sets the global chaos level in the PerformanceContext. Range: 0–100. */
struct SetCtxChaosCmd
{
    uint8_t chaos;
};

/*
 * Selects the active groove template and swing amount in the PerformanceContext.
 * groove_id: index into the GrooveLibrary.
 * swing: 0.0–1.0; 0.5 = straight timing, > 0.5 delays off-beats.
 */
struct SetCtxGrooveCmd
{
    uint8_t groove_id;
    float swing;
};

// ── Time signature commands ───────────────────────────────────────────────────

/* Inserts or replaces a time signature point at the given tick. */
struct SetTimeSigCmd
{
    uint64_t tick;
    uint8_t numerator;
    uint8_t denominator;  // literal note value: 4 = quarter note, 8 = eighth note, etc.
};

/* Removes the time signature point at the given tick (no-op if none exists there). */
struct RemoveTimeSigCmd
{
    uint64_t tick;
};

/* Clears the entire TimeSignatureMap, returning the session to freeform (no-meter) mode. */
struct ClearTimeSigCmd
{};

// ── SMPTE config commands ─────────────────────────────────────────────────────

/* Sets the SMPTE frame rate and drop-frame flag on the session. */
struct SetSmpteConfigCmd
{
    SmpteConfig config;
};

/* Clears SMPTE config; subsequent SMPTE helpers return OMEGA_ERR_NO_SMPTE_CONFIG. */
struct ClearSmpteConfigCmd
{};

// ── Custom source commands ────────────────────────────────────────────────────

/*
 * Registers a custom EventSource with the engine at the given priority bucket.
 * priority: 0 = MODULATOR (runs first), 1 = CONTEXT, 2 = PLAYBACK (runs last).
 */
struct AddSourceCmd
{
    EventSource* source;  // non-owning
    uint32_t priority;
};

/* Unregisters a custom EventSource; it will not be called after the next process() cycle. */
struct RemoveSourceCmd
{
    EventSource* source;  // non-owning
};

using Command = std::variant<AddEventCmd,
                             DeleteEventCmd,
                             SetTempoCmd,
                             SetTempoPointCmd,
                             RemoveTempoPointCmd,
                             SetLoopCmd,
                             CuePatternCmd,
                             TransportCmd,
                             SongAppendCmd,
                             SongClearCmd,
                             PerfAssignCmd,
                             PerfCueCmd,
                             PerfStopCmd,
                             PerfStopAllCmd,
                             PerfSetTransposeCmd,
                             PerfSetVelocityScaleCmd,
                             PerfSetRandomBiasCmd,
                             AddInputCmd,
                             RemoveInputCmd,
                             SetCtxScaleCmd,
                             SetCtxChordCmd,
                             SetCtxTransposeCmd,
                             SetCtxVelocityCmd,
                             SetCtxChaosCmd,
                             SetCtxGrooveCmd,
                             AddSourceCmd,
                             RemoveSourceCmd,
                             SetTimeSigCmd,
                             RemoveTimeSigCmd,
                             ClearTimeSigCmd,
                             SetSmpteConfigCmd,
                             ClearSmpteConfigCmd>;

static_assert(sizeof(Command) <= 64, "Command must fit within 64 bytes");

}  // namespace omega
