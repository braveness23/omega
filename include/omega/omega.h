#pragma once

/*
 * omega.h — Public C API
 *
 * ABI stability: the C API is ABI-stable within a MAJOR version (>= 1.0.0).
 * Binaries compiled against v1.0.0 are compatible with any v1.x.y without
 * recompilation. The C++ omega::core API carries no ABI stability guarantee.
 *
 * Ownership: every omega_*_create() returns a caller-owned handle. The caller
 * must call the matching omega_*_destroy() before omega_engine_destroy().
 * The engine holds non-owning references to all objects passed to it.
 *
 * Thread safety: every function declares its thread requirement.
 * The allowed tags are:
 *   Thread: Mutation thread only.
 *   Thread: Timing thread only.
 *   Thread: Any thread.
 *   Thread: Thread-unsafe — external lock required.
 *
 * Error contract: every function returning omega_status_t documents the
 * complete set of codes it can return and the condition for each.
 * No undocumented return codes.
 */

#include <omega/export.h>

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ────────────────────────────────────────────────────────────── */

/* Ticks per quarter note. Query at runtime via this macro; do not hardcode 480. */
#define OMEGA_PPQN 480u

/* ── Tick type ────────────────────────────────────────────────────────────── */

/* Musical time in ticks from session start. */
typedef uint64_t omega_tick_t;

/* ── Version ──────────────────────────────────────────────────────────────── */

typedef struct
{
    int major;
    int minor;
    int patch;
} omega_version_t;

/*
 * Returns the runtime library version.
 *
 * Thread: Any thread.
 *
 * Returns: the version struct — always succeeds.
 */
OMEGA_API omega_version_t omega_version(void);

/* ── Status codes ─────────────────────────────────────────────────────────── */

typedef enum
{
    OMEGA_OK = 0,               /* success */
    OMEGA_ERR_INVALID = -1,     /* NULL argument or invalid parameter */
    OMEGA_ERR_NOMEM = -2,       /* allocation failure */
    OMEGA_ERR_NOT_FOUND = -3,   /* handle not registered */
    OMEGA_ERR_QUEUE_FULL = -4,  /* mutation queue at capacity */
    OMEGA_ERR_UNSUPPORTED = -5, /* operation not supported in current state */
} omega_status_t;

/*
 * Returns a human-readable string for a status code.
 *
 * Thread: Any thread.
 *
 * Returns: a static string — always succeeds. Never NULL.
 */
OMEGA_API const char* omega_status_string(omega_status_t status);

/* ── Events ───────────────────────────────────────────────────────────────── */

/* payload_tag discriminants */
#define OMEGA_NOTE_ON 0x00u  /* data[0]=note, data[1]=vel, data[2-5]=duration_ticks */
#define OMEGA_NOTE_OFF 0x01u /* data[0]=note, data[1]=vel */
#define OMEGA_CC 0x02u       /* data[0]=controller, data[1]=value */
#define OMEGA_PROGRAM 0x03u  /* data[0]=program */

typedef struct
{
    uint64_t tick;       /* absolute musical position from session start */
    uint32_t sink_id;    /* target OutputSink */
    uint8_t payload_tag; /* discriminant — see OMEGA_NOTE_ON etc. */
    uint8_t channel;     /* MIDI channel 0-15, or 0 for non-MIDI */
    uint8_t reserved[2]; /* padding, must be zero */
    uint8_t data[8];     /* inline payload */
} omega_event_t;

/*
 * Creates a note-on event with inline duration.
 *
 * Thread: Any thread.
 *
 * Returns: the filled event struct — always succeeds.
 */
OMEGA_API omega_event_t omega_make_note_on(uint64_t tick,
                                           uint32_t sink_id,
                                           uint8_t channel,
                                           uint8_t note,
                                           uint8_t velocity,
                                           uint32_t duration_ticks);

/*
 * Creates a MIDI CC event.
 *
 * Thread: Any thread.
 *
 * Returns: the filled event struct — always succeeds.
 */
OMEGA_API omega_event_t
omega_make_cc(uint64_t tick, uint32_t sink_id, uint8_t channel, uint8_t controller, uint8_t value);

/*
 * Creates a MIDI program-change event.
 *
 * Thread: Any thread.
 *
 * Returns: the filled event struct — always succeeds.
 */
OMEGA_API omega_event_t omega_make_program(uint64_t tick,
                                           uint32_t sink_id,
                                           uint8_t channel,
                                           uint8_t program);

/* ── Engine ───────────────────────────────────────────────────────────────── */

typedef struct omega_engine_s omega_engine_t;

/*
 * Opaque sink handle.  In C++ consumer code, cast from omega::OutputSink*:
 *
 *   CapturingSink sink;
 *   omega_engine_add_sink(e, (omega_sink_t*)&sink);
 */
typedef struct omega_sink_s omega_sink_t;

typedef uint32_t omega_track_id_t;
typedef uint32_t omega_pattern_id_t;

/* Sentinel: returned by omega_pattern_create() on failure. Never a valid ID. */
#define OMEGA_PATTERN_INVALID 0u

typedef uint32_t omega_slot_id_t;

/* Number of performance slots (0 to OMEGA_SLOT_MAX-1 are valid). */
#define OMEGA_SLOT_MAX 64u

typedef enum
{
    OMEGA_CUE_IMMEDIATE = 0,   /* start/stop immediately */
    OMEGA_CUE_AT_BOUNDARY = 1, /* wait for the next loop boundary */
} omega_cue_mode_t;

typedef enum
{
    OMEGA_TRANSPORT_STOPPED = 0,
    OMEGA_TRANSPORT_PLAYING = 1,
} omega_transport_state_t;

/*
 * Creates a new engine using the built-in real-time clock.
 *
 * Thread: Any thread, before first use.
 *
 * Returns: caller-owned handle; NULL on allocation failure.
 * Call omega_engine_destroy() when done.
 */
OMEGA_API omega_engine_t* omega_engine_create(void);

/*
 * Destroys the engine and frees all associated resources.
 * Stop the engine (omega_engine_stop + process) before destroying it.
 *
 * Thread: Any thread, after all other threads have ceased using the engine.
 */
OMEGA_API void omega_engine_destroy(omega_engine_t* e);

/*
 * Registers an output sink with the engine. The engine holds a non-owning
 * reference; the sink must outlive the engine. Call before playback starts.
 *
 * Thread: Mutation thread only, before playback starts.
 *
 * Returns:
 *   OMEGA_OK          — sink registered.
 *   OMEGA_ERR_INVALID — e or sink is NULL.
 */
OMEGA_API omega_status_t omega_engine_add_sink(omega_engine_t* e, omega_sink_t* sink);

/*
 * Creates a new empty track in the engine's built-in timeline.
 * Call before playback starts.
 *
 * Thread: Mutation thread only, before playback starts.
 *
 * Returns:
 *   OMEGA_OK          — track created; *out_id is set.
 *   OMEGA_ERR_INVALID — e or out_id is NULL.
 */
OMEGA_API omega_status_t omega_engine_add_track(omega_engine_t* e,
                                                const char* name,
                                                omega_track_id_t* out_id);

/*
 * Sets the output sink for a track.
 * Call before playback starts.
 *
 * Thread: Mutation thread only, before playback starts.
 *
 * Returns:
 *   OMEGA_OK            — sink assigned.
 *   OMEGA_ERR_INVALID   — e is NULL.
 *   OMEGA_ERR_NOT_FOUND — track_id is not registered.
 */
OMEGA_API omega_status_t omega_engine_set_track_sink(omega_engine_t* e,
                                                     omega_track_id_t track,
                                                     uint32_t sink_id);

/*
 * Enqueues an event to be added to the given track on the next process() call.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — event enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_engine_add_event(omega_engine_t* e,
                                                omega_track_id_t track,
                                                omega_event_t ev);

/*
 * Enqueues a PLAY command. Playback begins on the next process() call.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_engine_play(omega_engine_t* e);

/*
 * Enqueues a STOP command. Playback halts on the next process() call.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_engine_stop(omega_engine_t* e);

/*
 * Advances the engine by one cycle: drains the command queue, then dispatches
 * all events due since the last process() call. Never allocates or blocks.
 *
 * Thread: Timing thread only.
 */
OMEGA_API void omega_engine_process(omega_engine_t* e);

/*
 * Returns the current transport state.
 * May return a stale value if called concurrently with process().
 *
 * Thread: Any thread.
 */
OMEGA_API omega_transport_state_t omega_engine_transport_state(const omega_engine_t* e);

/*
 * Returns the current transport position in nanoseconds from session start.
 * Updated by process(); may return a stale value when read concurrently.
 *
 * Thread: Any thread.
 */
OMEGA_API uint64_t omega_engine_position_ns(const omega_engine_t* e);

/* ── Patterns ─────────────────────────────────────────────────────────────── */

/*
 * Creates a new empty pattern in the engine's pattern library.
 * Call before playback starts.
 *
 * Thread: Mutation thread only, before playback starts.
 *
 * Returns the assigned pattern ID (>= 1), or OMEGA_PATTERN_INVALID on failure.
 */
OMEGA_API omega_pattern_id_t omega_pattern_create(omega_engine_t* e,
                                                  const char* name,
                                                  omega_tick_t length_ticks);

/*
 * Removes a pattern from the library. After this call, the ID is invalid and
 * will not be reused.
 *
 * Thread: Mutation thread only, before playback starts.
 *
 * Returns:
 *   OMEGA_OK          — pattern removed.
 *   OMEGA_ERR_INVALID — e is NULL.
 */
OMEGA_API omega_status_t omega_pattern_destroy(omega_engine_t* e, omega_pattern_id_t id);

/*
 * Inserts an event into a pattern in tick-sorted order.
 * Call before playback starts.
 *
 * Thread: Mutation thread only, before playback starts.
 *
 * Returns:
 *   OMEGA_OK            — event inserted.
 *   OMEGA_ERR_INVALID   — e or ev is NULL.
 *   OMEGA_ERR_NOT_FOUND — id is not a valid pattern.
 */
OMEGA_API omega_status_t omega_pattern_add_event(omega_engine_t* e,
                                                 omega_pattern_id_t id,
                                                 const omega_event_t* ev);

/*
 * Updates the length of a pattern.
 * Call before playback starts.
 *
 * Thread: Mutation thread only, before playback starts.
 *
 * Returns:
 *   OMEGA_OK            — length updated.
 *   OMEGA_ERR_INVALID   — e is NULL.
 *   OMEGA_ERR_NOT_FOUND — id is not a valid pattern.
 */
OMEGA_API omega_status_t omega_pattern_set_length(omega_engine_t* e,
                                                  omega_pattern_id_t id,
                                                  omega_tick_t length_ticks);

/* ── Song arrangement ─────────────────────────────────────────────────────── */

/*
 * Enqueues a command to append one entry to the song arrangement.
 * Entries are played in order; the pattern repeats `repeats` times before
 * advancing to the next entry. Entries with repeats == 0 are skipped.
 * Call before playback starts, or at any time from the mutation thread.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_song_append(omega_engine_t* e,
                                           omega_pattern_id_t pattern_id,
                                           uint32_t repeats);

/*
 * Enqueues a command to clear all song arrangement entries and reset playback
 * to the beginning of the arrangement.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_song_clear(omega_engine_t* e);

/* ── Performance source ───────────────────────────────────────────────────── */

/*
 * Assigns a pattern to a performance slot. pattern_id == OMEGA_PATTERN_INVALID
 * unassigns (any state → EMPTY, immediate note-off if playing).
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_perf_assign(omega_engine_t* e,
                                           omega_slot_id_t slot,
                                           omega_pattern_id_t pattern_id);

/*
 * Cues the assigned pattern for the given slot.
 *   OMEGA_CUE_IMMEDIATE  — starts immediately.
 *   OMEGA_CUE_AT_BOUNDARY — waits for the next loop boundary.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_perf_cue(omega_engine_t* e,
                                        omega_slot_id_t slot,
                                        omega_cue_mode_t mode);

/*
 * Stops the given slot.
 *   OMEGA_CUE_IMMEDIATE  — silences at the start of the next advance window.
 *   OMEGA_CUE_AT_BOUNDARY — silences at the next loop boundary.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_perf_stop(omega_engine_t* e,
                                         omega_slot_id_t slot,
                                         omega_cue_mode_t mode);

/*
 * Stops all slots.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_perf_stop_all(omega_engine_t* e, omega_cue_mode_t mode);

/*
 * Sets the transpose for a slot in semitones (-24 to +24).
 * Applied at dispatch time to all note events.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_perf_set_transpose(omega_engine_t* e,
                                                  omega_slot_id_t slot,
                                                  int8_t semitones);

/*
 * Sets the velocity scale for a slot (0–200, 100 = unity).
 * Applied at dispatch time: dispatched_vel = clamp((vel * scale) / 100, 1, 127).
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_perf_set_velocity_scale(omega_engine_t* e,
                                                       omega_slot_id_t slot,
                                                       uint8_t scale);

/*
 * Sets the random pitch bias for a slot (0–100).
 * At 100%, each note may be randomly offset by up to ±5 semitones.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_perf_set_random_bias(omega_engine_t* e,
                                                    omega_slot_id_t slot,
                                                    uint8_t bias);

/* ── Inputs ───────────────────────────────────────────────────────────────── */

typedef struct omega_input_s omega_input_t;
typedef struct omega_input_dispatcher_s omega_input_dispatcher_t;

typedef void (*omega_input_poll_fn_t)(omega_input_dispatcher_t* dispatcher, void* userdata);

typedef struct
{
    omega_input_poll_fn_t poll_fn;
    void* userdata;
} omega_input_desc_t;

/*
 * Creates a callback-based EventInput from a poll function and user data.
 *
 * Thread: Any thread, before passing to omega_engine_add_input().
 *
 * Returns: caller-owned handle; NULL if desc or poll_fn is NULL, or on
 * allocation failure. Call omega_input_destroy() when done.
 */
OMEGA_API omega_input_t* omega_input_create(const omega_input_desc_t* desc);

/*
 * Destroys an EventInput handle. Must not be called while the input is still
 * registered with an engine.
 *
 * Thread: Any thread, after omega_engine_remove_input() has been processed.
 */
OMEGA_API void omega_input_destroy(omega_input_t* input);

/*
 * Enqueues a command to register an EventInput with the engine. The input is
 * added to the polling list on the next process() call. The engine holds a
 * non-owning reference; the input must outlive the engine (or until removed).
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e or input is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_engine_add_input(omega_engine_t* e, omega_input_t* input);

/*
 * Enqueues a command to deregister an EventInput. The input is removed from
 * the polling list on the next process() call.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e or input is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_engine_remove_input(omega_engine_t* e, omega_input_t* input);

/*
 * Returns the cumulative number of events dropped since engine creation due
 * to InputBus capacity overflow (capacity is 256 events per cycle).
 *
 * Thread: Any thread.
 *
 * Returns: overflow event count; 0 if e is NULL.
 */
OMEGA_API uint32_t omega_input_overflow_count(const omega_engine_t* e);

/*
 * Delivers one event into the InputBus from within an omega_input_poll_fn_t
 * callback.
 *
 * Thread: Timing thread only (called from within poll_fn).
 */
OMEGA_API void omega_deliver(omega_input_dispatcher_t* dispatcher, const omega_event_t* ev);

/* ── Modulation bus ───────────────────────────────────────────────────────── */

/* Modulation channel index. OMEGA_MOD_INVALID is returned on failure. */
typedef uint32_t omega_mod_channel_t;
#define OMEGA_MOD_INVALID 0xFFFFFFFFu

/*
 * Registers a named modulation channel with an initial value.
 *
 * Thread: Mutation thread only, before playback starts.
 *
 * Returns: channel index on success, OMEGA_MOD_INVALID if 256 channels
 * are already registered or e is NULL.
 */
OMEGA_API omega_mod_channel_t omega_mod_register(omega_engine_t* e,
                                                 const char* name,
                                                 float initial);

/*
 * Finds a registered channel index by name.
 *
 * Thread: Mutation thread only.
 *
 * Returns: channel index, or OMEGA_MOD_INVALID if not found or e is NULL.
 */
OMEGA_API omega_mod_channel_t omega_mod_find(omega_engine_t* e, const char* name);

/*
 * Gets the current value of a modulation channel.
 * Out-of-range channels return 0.0f.
 *
 * Thread: Timing thread only (hot path; no locking).
 *
 * Returns: channel value, or 0.0f if e is NULL.
 */
OMEGA_API float omega_mod_get(omega_engine_t* e, omega_mod_channel_t channel);

/*
 * Sets a modulation channel value.
 * Out-of-range channels are silently ignored.
 *
 * Thread: Timing thread only (hot path; no locking).
 *
 * Returns:
 *   OMEGA_OK          — value set.
 *   OMEGA_ERR_INVALID — e is NULL.
 */
OMEGA_API omega_status_t omega_mod_set(omega_engine_t* e, omega_mod_channel_t channel, float value);

/*
 * Copies up to `count` modulation channel values into `out`.
 * Values are read atomically; individual values may be stale by one cycle.
 *
 * Thread: Any thread.
 *
 * Returns:
 *   OMEGA_OK          — snapshot written.
 *   OMEGA_ERR_INVALID — e or out is NULL.
 */
OMEGA_API omega_status_t omega_mod_snapshot(omega_engine_t* e, float* out, uint32_t count);

/* ── Performance context ──────────────────────────────────────────────────── */

/* Musical scale: chromatic root (0-11) and a 12-bit semitone presence mask. */
typedef struct
{
    uint8_t root;     /* 0-11: chromatic root */
    uint8_t reserved; /* must be zero */
    uint16_t bitmask; /* bit i set = scale degree i (0=root) is present */
} omega_scale_t;

/* Chord descriptor: root, type, and up to six voice pitches (0 = unused). */
typedef struct
{
    uint8_t root;      /* 0-11 */
    uint8_t type;      /* 0=none, 1=major, 2=minor, 3=dom7, 4=maj7, 5=min7 */
    uint8_t voices[6]; /* MIDI note numbers (0 = unused slot) */
} omega_chord_t;

/* Global musical context snapshotted into ProcessContext each cycle. */
typedef struct
{
    omega_scale_t scale;
    omega_chord_t chord;
    int8_t global_transpose; /* -24 to +24 semitones */
    uint8_t global_velocity; /* 0-200, 100 = unity */
    uint8_t chaos;           /* 0-100 */
    uint8_t groove_id;
    float swing; /* 0.0 = straight, 1.0 = full swing */
    uint32_t random_seed;
} omega_perf_ctx_t;

/*
 * Sets the active scale. Applied on the next process() cycle.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e or scale is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_ctx_set_scale(omega_engine_t* e, const omega_scale_t* scale);

/*
 * Sets the active chord. Applied on the next process() cycle.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e or chord is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_ctx_set_chord(omega_engine_t* e, const omega_chord_t* chord);

/*
 * Sets the global transpose (-24 to +24 semitones). Applied next cycle.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_ctx_set_transpose(omega_engine_t* e, int8_t semitones);

/*
 * Sets the global velocity scale (0-200, 100 = unity). Applied next cycle.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_ctx_set_velocity(omega_engine_t* e, uint8_t velocity);

/*
 * Sets the chaos level (0-100). Applied on the next process() cycle.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_ctx_set_chaos(omega_engine_t* e, uint8_t chaos);

/*
 * Sets the groove template and swing amount. Applied on the next process() cycle.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_ctx_set_groove(omega_engine_t* e, uint8_t groove_id, float swing);

/*
 * Returns a snapshot of the current performance context. Values reflect the
 * last-committed state as of the most recent process() cycle.
 *
 * Thread: Mutation thread only. Must not be called concurrently with
 * omega_engine_process().
 *
 * Returns:
 *   OMEGA_OK          — ctx filled.
 *   OMEGA_ERR_INVALID — e or ctx is NULL.
 */
OMEGA_API omega_status_t omega_ctx_get(omega_engine_t* e, omega_perf_ctx_t* ctx);

/* ── Custom event sources ─────────────────────────────────────────────────── */

typedef struct omega_source_s omega_source_t;
typedef struct omega_dispatcher_s omega_dispatcher_t;
typedef struct omega_process_context_s omega_process_context_t;

/*
 * Source priority buckets. Register MODULATOR (LFO, etc.) before CONTEXT
 * (chord detector, etc.) and both before PLAYBACK sources.
 */
#define OMEGA_SOURCE_PRIORITY_MODULATOR 0u
#define OMEGA_SOURCE_PRIORITY_CONTEXT 1u
#define OMEGA_SOURCE_PRIORITY_PLAYBACK 2u

/*
 * Called from the timing thread each cycle to advance the source.
 * Must never allocate, block, or lock.
 */
typedef void (*omega_source_advance_fn_t)(uint64_t to_tick,
                                          omega_dispatcher_t* dispatcher,
                                          omega_process_context_t* ctx,
                                          void* userdata);

typedef struct
{
    omega_source_advance_fn_t advance_fn; /* required; must not be NULL */
    void* userdata;
    uint32_t priority; /* OMEGA_SOURCE_PRIORITY_* constant */
} omega_source_desc_t;

/*
 * Creates a callback-based EventSource from an advance function and user data.
 *
 * Thread: Any thread, before passing to omega_engine_add_source().
 *
 * Returns: caller-owned handle; NULL if desc or advance_fn is NULL, or on
 * allocation failure. Call omega_source_destroy() when done.
 */
OMEGA_API omega_source_t* omega_source_create(const omega_source_desc_t* desc);

/*
 * Destroys an EventSource handle. Must not be called while still registered.
 *
 * Thread: Any thread, after omega_engine_remove_source() has been processed.
 */
OMEGA_API void omega_source_destroy(omega_source_t* source);

/*
 * Enqueues a command to register a custom EventSource. The source is added on
 * the next process() call. The engine holds a non-owning reference; the source
 * must outlive the engine (or until removed).
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e or source is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_engine_add_source(omega_engine_t* e, omega_source_t* source);

/*
 * Enqueues a command to deregister a custom EventSource. Removed on the next
 * process() call.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e or source is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_engine_remove_source(omega_engine_t* e, omega_source_t* source);

/*
 * Dispatches one event to sinks from within an omega_source_advance_fn_t
 * callback.
 *
 * Thread: Timing thread only (called from within advance_fn).
 */
OMEGA_API void omega_dispatch(omega_dispatcher_t* dispatcher, const omega_event_t* ev);

/*
 * Returns the number of InputBus events available in this cycle, from within
 * an omega_source_advance_fn_t callback.
 *
 * Thread: Timing thread only (called from within advance_fn).
 *
 * Returns: 0 if ctx is NULL.
 */
OMEGA_API uint32_t omega_ctx_input_count(const omega_process_context_t* ctx);

/*
 * Returns the event at index `i` in the InputBus for this cycle. Undefined
 * behaviour if i >= omega_ctx_input_count(ctx).
 *
 * Thread: Timing thread only (called from within advance_fn).
 */
OMEGA_API const omega_event_t* omega_ctx_input_at(const omega_process_context_t* ctx, uint32_t i);

/*
 * Gets a modulation channel value from within an advance_fn callback.
 *
 * Thread: Timing thread only.
 *
 * Returns: channel value, or 0.0f if ctx is NULL.
 */
OMEGA_API float omega_ctx_mod_get_ctx(const omega_process_context_t* ctx,
                                      omega_mod_channel_t channel);

/*
 * Sets a modulation channel value from within an advance_fn callback.
 *
 * Thread: Timing thread only.
 */
OMEGA_API void omega_ctx_mod_set_ctx(omega_process_context_t* ctx,
                                     omega_mod_channel_t channel,
                                     float value);

#ifdef __cplusplus
}
#endif
