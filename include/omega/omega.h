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

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Version macros ───────────────────────────────────────────────────────── */

/* Compile-time version. Use omega_version() for runtime version checking. */
#define OMEGA_VERSION_MAJOR 1
#define OMEGA_VERSION_MINOR 0
#define OMEGA_VERSION_PATCH 0

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
    OMEGA_OK = 0,                   /* success */
    OMEGA_ERR_INVALID = -1,         /* NULL argument or invalid parameter */
    OMEGA_ERR_NOMEM = -2,           /* allocation failure */
    OMEGA_ERR_NOT_FOUND = -3,       /* handle not registered */
    OMEGA_ERR_QUEUE_FULL = -4,      /* mutation queue at capacity */
    OMEGA_ERR_UNSUPPORTED = -5,     /* operation not supported in current state */
    OMEGA_ERR_NO_METER = -6,        /* no time signature map defined (freeform mode) */
    OMEGA_ERR_NO_SMPTE_CONFIG = -7, /* SmpteConfig not set on the session */
    OMEGA_ERR_IO = -8,              /* MIDI I/O failure (port not open or send error) */
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
#define OMEGA_NOTE_ON 0x00u    /* data[0]=note, data[1]=vel, data[2-5]=duration_ticks */
#define OMEGA_NOTE_OFF 0x01u   /* data[0]=note, data[1]=vel */
#define OMEGA_CC 0x02u         /* data[0]=controller, data[1]=value */
#define OMEGA_PROGRAM 0x03u    /* data[0]=program */
#define OMEGA_PITCH_BEND 0x04u /* data[0]=LSB (7-bit), data[1]=MSB (7-bit); center=0x40,0x00 */
#define OMEGA_AFTERTOUCH 0x05u /* data[0]=pressure (0-127) */
#define OMEGA_POLY_AT 0x06u    /* data[0]=note, data[1]=pressure (0-127) */

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

/* ── Event field accessors ────────────────────────────────────────────────── */

/*
 * Accessors for OMEGA_NOTE_ON / OMEGA_NOTE_OFF events.
 * Behaviour is undefined if the event's payload_tag is not NOTE_ON or NOTE_OFF.
 *
 * Thread: Any thread.
 */
OMEGA_API uint8_t omega_event_note_pitch(const omega_event_t* e);
OMEGA_API uint8_t omega_event_note_velocity(const omega_event_t* e);

/*
 * Returns the inline duration in ticks for an OMEGA_NOTE_ON event.
 * Behaviour is undefined if payload_tag != OMEGA_NOTE_ON.
 *
 * Thread: Any thread.
 */
OMEGA_API uint32_t omega_event_note_duration(const omega_event_t* e);

/*
 * Mutators for OMEGA_NOTE_ON / OMEGA_NOTE_OFF events.
 *
 * Thread: Any thread.
 *
 * Returns:
 *   OMEGA_OK          — field updated.
 *   OMEGA_ERR_INVALID — e is NULL.
 */
OMEGA_API omega_status_t omega_event_set_pitch(omega_event_t* e, uint8_t pitch);
OMEGA_API omega_status_t omega_event_set_velocity(omega_event_t* e, uint8_t vel);

/*
 * Sets the inline duration for an OMEGA_NOTE_ON event.
 * Behaviour is undefined if payload_tag != OMEGA_NOTE_ON.
 *
 * Thread: Any thread.
 *
 * Returns:
 *   OMEGA_OK          — field updated.
 *   OMEGA_ERR_INVALID — e is NULL.
 */
OMEGA_API omega_status_t omega_event_set_duration(omega_event_t* e, uint32_t dur);

/*
 * Accessors for OMEGA_CC events.
 * Behaviour is undefined if payload_tag != OMEGA_CC.
 *
 * Thread: Any thread.
 */
OMEGA_API uint8_t omega_event_cc_number(const omega_event_t* e);
OMEGA_API uint8_t omega_event_cc_value(const omega_event_t* e);

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
    OMEGA_CUE_BAR = 2,         /* wait for the next musical bar boundary */
} omega_cue_mode_t;

typedef enum
{
    OMEGA_TRANSPORT_STOPPED = 0,
    OMEGA_TRANSPORT_PLAYING = 1,
} omega_transport_state_t;

typedef enum
{
    OMEGA_SLOT_EMPTY = 0,
    OMEGA_SLOT_IDLE = 1,
    OMEGA_SLOT_QUEUED = 2,
    OMEGA_SLOT_PLAYING = 3,
    OMEGA_SLOT_STOPPING = 4,
} omega_slot_state_t;

typedef enum
{
    OMEGA_EVENT_SLOT_STARTED = 0,
    OMEGA_EVENT_SLOT_STOPPED = 1,
    OMEGA_EVENT_LOOP_WRAPPED = 2,
    OMEGA_EVENT_TRANSPORT_STOPPED = 3,
} omega_engine_event_t;

/*
 * Callback for engine state-change notifications.
 *
 * event:    the type of state change.
 * detail:   event-specific data:
 *             SLOT_STARTED / SLOT_STOPPED: slot index (0 to OMEGA_SLOT_MAX-1).
 *             LOOP_WRAPPED: new loop count (1-based).
 *             TRANSPORT_STOPPED: always 0.
 * userdata: pointer passed to omega_engine_set_event_callback().
 *
 * Constraint: fires from the timing thread. Must not block, allocate,
 * or call back into the engine.
 */
typedef void (*omega_event_callback_t)(omega_engine_event_t event, uint32_t detail, void* userdata);

/*
 * Registers an event callback. Pass NULL for cb to clear the callback.
 * Only one callback is supported; setting a new one replaces the previous.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK          — callback registered (or cleared).
 *   OMEGA_ERR_INVALID — e is NULL.
 */
OMEGA_API omega_status_t omega_engine_set_event_callback(omega_engine_t* e,
                                                         omega_event_callback_t cb,
                                                         void* userdata);

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
 * Returns the unique sink_id of sink. Use this value as the sink_id field in
 * omega_make_note_on / omega_make_cc / omega_engine_set_track_sink to route
 * events to this sink.
 *
 * Returns 0 if sink is NULL (0 is never a valid sink_id).
 *
 * Thread: Any thread.
 */
OMEGA_API uint32_t omega_sink_id(const omega_sink_t* sink);

/*
 * Mutes or unmutes a specific MIDI channel on a registered sink.
 *
 * channel: 0–15 targets one MIDI channel; 0xFF targets all channels.
 * muted:   non-zero = mute, 0 = unmute.
 *
 * When a channel transitions to muted, the engine sends immediate note-off
 * for every active note on that channel before suppressing further note-ons.
 * Safe during playback — enqueued through the SPSC command queue and applied
 * at the start of the next process() cycle.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL, or channel > 15 and channel != 0xFF.
 *   OMEGA_ERR_QUEUE_FULL — command queue is full.
 */
OMEGA_API omega_status_t omega_sink_set_mute(omega_engine_t* e,
                                             uint32_t sink_id,
                                             uint8_t channel,
                                             int muted);

/*
 * Solos or un-solos a specific MIDI channel on a registered sink.
 *
 * channel: 0–15 targets one MIDI channel; 0xFF targets all channels.
 * soloed:  non-zero = solo, 0 = un-solo.
 *
 * While any channel is soloed, only soloed (sink, channel) pairs produce
 * output; all others are effectively muted. When the last solo is cleared,
 * the explicit mute flags resume control. Active notes on channels that
 * become newly suppressed receive immediate note-offs. Safe during playback.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL, or channel > 15 and channel != 0xFF.
 *   OMEGA_ERR_QUEUE_FULL — command queue is full.
 */
OMEGA_API omega_status_t omega_sink_set_solo(omega_engine_t* e,
                                             uint32_t sink_id,
                                             uint8_t channel,
                                             int soloed);

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

/*
 * Returns the current transport position in ticks from session start.
 * Converts the stored nanosecond position through the TempoMap, so the
 * result correctly handles tempo automation. Equivalent to calling
 * omega_engine_position_ns() and doing the conversion manually, but
 * without floating-point arithmetic and without assuming constant tempo.
 * Updated by process(); may return a stale value when read concurrently.
 *
 * Thread: Any thread.
 *
 * Returns: tick position; 0 if e is NULL.
 */
OMEGA_API omega_tick_t omega_engine_position_tick(const omega_engine_t* e);

/*
 * Atomic position snapshot: bar, beat, subdivision, loop_count, and raw tick,
 * all updated together at the end of each process() call.
 *
 * bar and beat are 1-based. subdivision is the number of ticks past the beat
 * boundary (0 to PPQN-1 in typical usage). When the session is in freeform
 * mode (no time signature set), bar, beat, and subdivision are all 0; tick
 * and loop_count are always valid.
 *
 * loop_count is incremented each time the active loop region wraps and is
 * reset to 0 whenever the loop region is changed via omega_loop_set() or
 * omega_loop_clear().
 *
 * bpm_milli, numerator, and denominator report the tempo and meter active at
 * tick. They are written atomically with the rest of the snapshot each cycle,
 * giving any-thread readers (e.g. a UI) a race-free view of tempo/meter that
 * follows changes during playback. numerator/denominator are 0 in freeform
 * mode. loop_enabled is 1 while a loop region is active, else 0.
 */
typedef struct
{
    uint32_t bar;         /* 1-based bar number (0 = freeform/no meter) */
    uint8_t beat;         /* 1-based beat within bar (0 = freeform/no meter) */
    uint32_t subdivision; /* ticks past the beat boundary (0 in freeform mode) */
    uint64_t loop_count;  /* number of loop-region wraps since last loop_set/clear */
    omega_tick_t tick;    /* raw tick for further computation */
    uint32_t bpm_milli;   /* tempo at tick, BPM x 1000 */
    uint8_t numerator;    /* meter numerator at tick (0 = freeform/no meter) */
    uint8_t denominator;  /* meter denominator at tick (0 = freeform/no meter) */
    uint8_t loop_enabled; /* 1 if a loop region is active, else 0 */
} omega_position_t;

/*
 * Fills *out with a consistent position snapshot updated at the end of the
 * most recent process() call. bar, beat, and subdivision are computed from
 * the engine's TimeSignatureMap; all three are 0 in freeform mode. tick and
 * loop_count are always valid. Each field is written atomically; in rare
 * cases, fields from two adjacent process() cycles may appear together — this
 * is imperceptible at normal display refresh rates (<=100 Hz).
 *
 * Thread: Any thread.
 *
 * Returns:
 *   OMEGA_OK          — out filled.
 *   OMEGA_ERR_INVALID — e or out is NULL.
 */
OMEGA_API omega_status_t omega_engine_position(const omega_engine_t* e, omega_position_t* out);

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
 * Ordering constraint: the sink_id embedded in ev must refer to a sink that
 * has already been created (via omega_sink_create_midi_out() or a custom sink)
 * and registered with omega_engine_add_sink().  Create all sinks first, read
 * their IDs with omega_sink_id(), then build your patterns.  Events that
 * reference a sink_id that does not exist at dispatch time will be silently
 * dropped.
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

/*
 * Replaces the event at zero-based index event_index in the pattern with
 * replacement. If the replacement tick differs from the original, the event
 * vector is re-sorted. Safe during playback — enqueued through the SPSC
 * command queue and applied at the start of the next process() cycle.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e or replacement is NULL.
 *   OMEGA_ERR_QUEUE_FULL — command queue is full.
 *
 * Note: OMEGA_ERR_NOT_FOUND (invalid pattern or index out of bounds) is
 * detected on the timing thread when the command is applied; the function
 * itself returns OMEGA_OK as long as the command was enqueued successfully.
 */
OMEGA_API omega_status_t omega_pattern_replace_event(omega_engine_t* e,
                                                     omega_pattern_id_t pat,
                                                     uint32_t event_index,
                                                     const omega_event_t* replacement);

/* ── Pattern read API ─────────────────────────────────────────────────────── */

/*
 * Returns the total number of events in the pattern via *count_out.
 *
 * Thread: Mutation thread only. Must not be called concurrently with process().
 *
 * Returns:
 *   OMEGA_OK            — *count_out written.
 *   OMEGA_ERR_INVALID   — e or count_out is NULL.
 *   OMEGA_ERR_NOT_FOUND — pat is not a valid pattern ID.
 */
OMEGA_API omega_status_t omega_pattern_event_count(const omega_engine_t* e,
                                                   omega_pattern_id_t pat,
                                                   uint32_t* count_out);

/*
 * Copies the event at zero-based index idx into *event_out.
 *
 * Thread: Mutation thread only. Must not be called concurrently with process().
 *
 * Returns:
 *   OMEGA_OK            — *event_out written.
 *   OMEGA_ERR_INVALID   — e or event_out is NULL.
 *   OMEGA_ERR_NOT_FOUND — pat is not a valid pattern ID, or idx >= event count.
 */
OMEGA_API omega_status_t omega_pattern_event_at(const omega_engine_t* e,
                                                omega_pattern_id_t pat,
                                                uint32_t idx,
                                                omega_event_t* event_out);

/*
 * Counts events matching channel and payload_tag, writing the result to
 * *count_out. Pass 0xFF for channel or payload_tag to match any value.
 *
 * Thread: Mutation thread only. Must not be called concurrently with process().
 *
 * Returns:
 *   OMEGA_OK            — *count_out written.
 *   OMEGA_ERR_INVALID   — e or count_out is NULL.
 *   OMEGA_ERR_NOT_FOUND — pat is not a valid pattern ID.
 */
OMEGA_API omega_status_t omega_pattern_event_count_filtered(const omega_engine_t* e,
                                                            omega_pattern_id_t pat,
                                                            uint8_t channel,
                                                            uint8_t payload_tag,
                                                            uint32_t* count_out);

/*
 * Returns the length of the pattern in ticks via *length_out.
 *
 * Thread: Mutation thread only. Must not be called concurrently with process().
 *
 * Returns:
 *   OMEGA_OK            — *length_out written.
 *   OMEGA_ERR_INVALID   — e or length_out is NULL.
 *   OMEGA_ERR_NOT_FOUND — pat is not a valid pattern ID.
 */
OMEGA_API omega_status_t omega_pattern_length(const omega_engine_t* e,
                                              omega_pattern_id_t pat,
                                              omega_tick_t* length_out);

/*
 * Iterates events in a pattern, invoking cb for each matching event.
 *
 * channel_filter: 0xFF = all channels; 0-15 = match specific channel.
 * tag_filter: 0xFF = all payload tags; specific tag value = match that tag.
 *
 * The callback receives the zero-based event index (in the unfiltered pattern),
 * a pointer to the event (valid only for the duration of the callback), and
 * the caller-supplied userdata pointer.
 *
 * Thread safety: safe to call from any thread, provided the caller does not
 * concurrently mutate pattern contents via the command queue. During playback
 * this is inherently safe: the timing thread reads events read-only, and
 * mutations are applied between process() cycles.
 *
 * Thread: Any thread (see above).
 *
 * Returns:
 *   OMEGA_OK            — iteration completed (zero or more events visited).
 *   OMEGA_ERR_INVALID   — e or cb is NULL.
 *   OMEGA_ERR_NOT_FOUND — pat is not a valid pattern ID.
 */
OMEGA_API omega_status_t omega_pattern_for_each_event(const omega_engine_t* e,
                                                      omega_pattern_id_t pat,
                                                      uint8_t channel_filter,
                                                      uint8_t tag_filter,
                                                      void (*cb)(uint32_t index,
                                                                 const omega_event_t* event,
                                                                 void* userdata),
                                                      void* userdata);

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

/* ── Query boundary ──────────────────────────────────────────────────────── */

/*
 * Returns the current state of the given performance slot.
 * Returns OMEGA_SLOT_EMPTY for NULL engine or out-of-range slot.
 *
 * Thread: Any thread.
 */
OMEGA_API omega_slot_state_t omega_perf_slot_state(const omega_engine_t* e, omega_slot_id_t slot);

/*
 * Returns non-zero if the given MIDI channel on the specified sink is muted.
 * Returns 0 for NULL engine, unregistered sink_id, or channel > 15.
 *
 * Thread: Any thread.
 */
OMEGA_API int omega_sink_is_muted(const omega_engine_t* e, uint32_t sink_id, uint8_t channel);

/*
 * Returns non-zero if the given MIDI channel on the specified sink is soloed.
 * Returns 0 for NULL engine, unregistered sink_id, or channel > 15.
 *
 * Thread: Any thread.
 */
OMEGA_API int omega_sink_is_soloed(const omega_engine_t* e, uint32_t sink_id, uint8_t channel);

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

/* ── MIDI I/O ─────────────────────────────────────────────────────────────── */

/*
 * Creates an OutputSink backed by a real MIDI output port via libremidi.
 *
 * port_name:
 *   NULL — open the first available output port.
 *   ""   — open a virtual output port named "Omega Out".
 *   other — match by display name or port name (first match wins).
 *
 * Returns a caller-owned sink handle on success. If no matching port exists,
 * construction still succeeds but send() will silently return OMEGA_ERR_IO.
 * Returns NULL only on allocation failure. Call omega_engine_add_sink() then
 * use omega_sink_id(sink) to route events to it.
 * Destroy with omega_sink_destroy_midi_out() — do NOT pass to omega_sink_destroy().
 *
 * Ordering constraint: create and register all sinks before building patterns
 * that embed their sink_id.  The sink_id is assigned at construction time.
 * Passing a sink_id to omega_pattern_add_event() before the corresponding
 * sink exists will cause those events to be silently dropped at dispatch time.
 *
 * Thread: Any thread, before playback starts.
 */
OMEGA_API omega_sink_t* omega_sink_create_midi_out(const char* port_name);

/*
 * Destroys a MIDI output sink created with omega_sink_create_midi_out().
 * Must not be called while the sink is still registered with an engine.
 *
 * Thread: Any thread, after playback is stopped and sink is deregistered.
 */
OMEGA_API void omega_sink_destroy_midi_out(omega_sink_t* sink);

/*
 * Creates an EventInput backed by a real MIDI input port via libremidi.
 *
 * port_name:
 *   NULL — open the first available input port.
 *   ""   — open a virtual input port named "Omega In".
 *   other — match by display name or port name (first match wins).
 *
 * Returns a caller-owned input handle. If no port is available, construction
 * still succeeds but poll() delivers no events. Returns NULL on allocation failure.
 * Destroy with omega_input_destroy_midi_in() — do NOT pass to omega_input_destroy().
 *
 * Thread: Any thread, before omega_engine_add_input().
 */
OMEGA_API omega_input_t* omega_input_create_midi_in(const char* port_name);

/*
 * Destroys a MIDI input created with omega_input_create_midi_in().
 * Must not be called while the input is still registered with an engine.
 *
 * Thread: Any thread, after omega_engine_remove_input() has been processed.
 */
OMEGA_API void omega_input_destroy_midi_in(omega_input_t* input);

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

/* ── Tempo map ────────────────────────────────────────────────────────────── */

/*
 * Insert or replace a tempo point at tick.
 * bpm_milli is BPM × 1000 (e.g. 120 BPM = 120000). Zero is invalid.
 *
 * The change takes effect on the next process() call after the command drains.
 * The default tempo (120 BPM at tick 0) is set at construction and can be
 * overridden by calling omega_tempo_set(e, 0, new_bpm_milli).
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL, or bpm_milli is zero.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_tempo_set(omega_engine_t* e, omega_tick_t tick, uint32_t bpm_milli);

/*
 * Remove the tempo point at exactly tick.
 * No-op if no point exists at that tick, or if tick == 0
 * (the origin point cannot be removed; use omega_tempo_set(e, 0, bpm) instead).
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_tempo_remove(omega_engine_t* e, omega_tick_t tick);

/*
 * Query the BPM (as milli-BPM) in effect at the given tick.
 * Returns the bpm_milli of the last tempo point whose tick <= the query tick.
 *
 * Thread: Mutation thread only. Must not be called concurrently with process().
 *
 * Returns:
 *   OMEGA_OK          — *bpm_milli_out written.
 *   OMEGA_ERR_INVALID — e or bpm_milli_out is NULL.
 */
OMEGA_API omega_status_t omega_tempo_at(const omega_engine_t* e,
                                        omega_tick_t tick,
                                        uint32_t* bpm_milli_out);

/* ── Time signature map ───────────────────────────────────────────────────── */

/* A single time signature change point. */
typedef struct
{
    uint64_t tick;       /* tick at which this meter takes effect */
    uint8_t numerator;   /* beats per bar (1-99) */
    uint8_t denominator; /* beat unit: 1, 2, 4, 8, 16, or 32 */
} omega_time_sig_point_t;

/* Bar/beat position: bar and beat are 1-based. */
typedef struct
{
    uint32_t bar;         /* 1-based bar number */
    uint8_t beat;         /* 1-based beat within bar */
    uint32_t subdivision; /* ticks past the beat boundary */
} omega_beat_pos_t;

/*
 * Insert or replace a time signature at tick.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL, denominator is not a power of 2 in [1,32],
 *                          or numerator is zero.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_timesig_set(omega_engine_t* e,
                                           uint64_t tick,
                                           uint8_t numerator,
                                           uint8_t denominator);

/*
 * Remove the time signature at exactly tick.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_timesig_remove(omega_engine_t* e, uint64_t tick);

/*
 * Clear all time signature entries (enter freeform mode).
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_timesig_clear(omega_engine_t* e);

/*
 * Returns 1 if no time signature has been set (freeform mode), 0 otherwise.
 *
 * Thread: Mutation thread only. Must not be called concurrently with process().
 *
 * Returns: 1 = freeform, 0 = metered; -1 if e is NULL.
 */
OMEGA_API int omega_timesig_is_freeform(const omega_engine_t* e);

/*
 * Returns the active time signature at or before tick, or fills *out with zeros
 * if the map is empty or tick precedes the first entry.
 *
 * Thread: Mutation thread only. Must not be called concurrently with process().
 *
 * Returns:
 *   OMEGA_OK           — out filled.
 *   OMEGA_ERR_NOT_FOUND — no entry at or before tick (or map is empty).
 *   OMEGA_ERR_INVALID  — e or out is NULL.
 */
OMEGA_API omega_status_t omega_timesig_at(const omega_engine_t* e,
                                          uint64_t tick,
                                          omega_time_sig_point_t* out);

/*
 * Convert absolute tick → {bar, beat, subdivision}.
 *
 * Thread: Mutation thread only. Must not be called concurrently with process().
 *
 * Returns:
 *   OMEGA_OK             — out filled.
 *   OMEGA_ERR_INVALID    — e or out is NULL.
 *   OMEGA_ERR_NO_METER   — session is in freeform mode or tick precedes first entry.
 */
OMEGA_API omega_status_t omega_tick_to_beat_pos(const omega_engine_t* e,
                                                uint64_t tick,
                                                omega_beat_pos_t* out);

/*
 * Convert {bar, beat, subdivision} → absolute tick.
 *
 * Thread: Mutation thread only. Must not be called concurrently with process().
 *
 * Returns:
 *   OMEGA_OK             — out filled.
 *   OMEGA_ERR_INVALID    — e or pos is NULL, or bar/beat is zero, or beat > numerator.
 *   OMEGA_ERR_NO_METER   — session is in freeform mode.
 */
OMEGA_API omega_status_t omega_beat_pos_to_tick(const omega_engine_t* eng,
                                                const omega_beat_pos_t* in,
                                                uint64_t* out);

/*
 * Tick of the next bar boundary at or after from_tick.
 *
 * Thread: Mutation thread only. Must not be called concurrently with process().
 *
 * Returns:
 *   OMEGA_OK           — out filled.
 *   OMEGA_ERR_INVALID  — e or out is NULL.
 *   OMEGA_ERR_NO_METER — session is in freeform mode.
 */
OMEGA_API omega_status_t omega_next_bar_tick(const omega_engine_t* e,
                                             uint64_t from_tick,
                                             uint64_t* out);

/*
 * Quantize tick to the nearest beat (round-half-up).
 *
 * Thread: Mutation thread only. Must not be called concurrently with process().
 *
 * Returns:
 *   OMEGA_OK           — out filled.
 *   OMEGA_ERR_INVALID  — e or out is NULL.
 *   OMEGA_ERR_NO_METER — session is in freeform mode.
 */
OMEGA_API omega_status_t omega_quantize_to_beat(const omega_engine_t* e,
                                                uint64_t tick,
                                                uint64_t* out);

/*
 * Formats a tick position as a human-readable bar:beat.subdivision string.
 *
 * Converts tick through the engine's TimeSignatureMap using MeterCursor and
 * writes a NUL-terminated string of the form "3:2.120" (1-based bar, 1-based
 * beat, ticks-past-beat-boundary) into the out buffer.  At most out_size
 * bytes are written (including the NUL terminator); the output is always
 * NUL-terminated when out_size > 0.
 *
 * Thread: Mutation thread only. Must not be called concurrently with process().
 *
 * Returns:
 *   OMEGA_OK            — out filled.
 *   OMEGA_ERR_INVALID   — e or out is NULL, or out_size is 0.
 *   OMEGA_ERR_NO_METER  — session is in freeform mode (no time signature set).
 */
OMEGA_API omega_status_t omega_format_position(const omega_engine_t* e,
                                               omega_tick_t tick,
                                               char* out,
                                               size_t out_size);

/* ── SMPTE config ─────────────────────────────────────────────────────────── */

/*
 * SMPTE frame-rate configuration.
 *   fps        — 24, 25, or 30 (for 29.97: set fps=30 and is_2997=1)
 *   drop_frame — enable drop-frame addressing; only valid when is_2997=1
 *   is_2997    — 1 = 30000/1001 time base; fps must be 30
 */
typedef struct
{
    uint8_t fps;
    uint8_t drop_frame; /* 0 or 1 */
    uint8_t is_2997;    /* 0 or 1 */
} omega_smpte_config_t;

/* SMPTE timecode address. */
typedef struct
{
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
    uint8_t frames;
} omega_smpte_time_t;

/*
 * Set the SMPTE frame-rate config.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e or config is NULL, or config values are invalid.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_smpte_config_set(omega_engine_t* e,
                                                const omega_smpte_config_t* config);

/*
 * Clear the SMPTE config. Subsequent conversion calls return OMEGA_ERR_NO_SMPTE_CONFIG.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_smpte_config_clear(omega_engine_t* e);

/*
 * Convert absolute tick → HH:MM:SS:FF.
 *
 * Thread: Mutation thread only. Must not be called concurrently with process().
 *
 * Returns:
 *   OMEGA_OK                   — out filled.
 *   OMEGA_ERR_INVALID          — e or out is NULL.
 *   OMEGA_ERR_NO_SMPTE_CONFIG  — SMPTE config not set.
 */
OMEGA_API omega_status_t omega_tick_to_smpte(const omega_engine_t* e,
                                             uint64_t tick,
                                             omega_smpte_time_t* out);

/*
 * Convert HH:MM:SS:FF → absolute tick.
 *
 * Thread: Mutation thread only. Must not be called concurrently with process().
 *
 * Returns:
 *   OMEGA_OK                   — out filled.
 *   OMEGA_ERR_INVALID          — e, t, or out is NULL; or the SMPTE address is
 *                                illegal (e.g., frame 0/1 at a non-round drop-frame minute).
 *   OMEGA_ERR_NO_SMPTE_CONFIG  — SMPTE config not set.
 */
OMEGA_API omega_status_t omega_smpte_to_tick(const omega_engine_t* e,
                                             const omega_smpte_time_t* t,
                                             uint64_t* out);

/* ── Transport loop ───────────────────────────────────────────────────────── */

/*
 * Sets the transport loop region and enables looping. While the transport
 * is playing, whenever the position reaches end, it automatically locates
 * back to start (sending note-offs and resetting source cursors).
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL, or end <= start.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_loop_set(omega_engine_t* e, omega_tick_t start, omega_tick_t end);

/*
 * Disables looping and clears the loop region.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_loop_clear(omega_engine_t* e);

/*
 * Enables (enabled != 0) or disables (enabled == 0) looping without
 * changing the stored loop region.
 *
 * Thread: Mutation thread only. Must not be called concurrently with
 * omega_engine_process().
 *
 * Returns:
 *   OMEGA_OK             — command enqueued.
 *   OMEGA_ERR_INVALID    — e is NULL.
 *   OMEGA_ERR_QUEUE_FULL — queue at capacity.
 */
OMEGA_API omega_status_t omega_loop_enable(omega_engine_t* e, int enabled);

/* ── SMF import / export ──────────────────────────────────────────────────── */

/*
 * Imports a Standard MIDI File into the engine session. Reads all tracks,
 * imports tempo changes into TempoMap, time signatures into TimeSignatureMap,
 * marker meta-events into MarkerList, and note/CC/program events into new
 * timeline tracks. Non-480 PPQN files are tick-scaled automatically.
 *
 * The engine must be stopped before calling this function.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK          -- import succeeded.
 *   OMEGA_ERR_INVALID -- e or path is NULL.
 *   OMEGA_ERR_IO      -- file not found, not a valid MIDI file, or read error.
 */
OMEGA_API omega_status_t omega_smf_import(omega_engine_t* e, const char* path);

/*
 * Exports the engine session to a Standard MIDI File.
 * Writes the tempo map, time signature map, markers, and all timeline track
 * events (note on/off pairs, CC, program change) to the file at path.
 *
 * smf_type: 0 = Type 0 (single track), 1 = Type 1 (multi-track).
 * For Type 1, track 0 carries meta events; each omega track occupies its own
 * subsequent track. For Type 0, all events are merged into track 0.
 *
 * The engine must be stopped before calling this function.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK          -- export succeeded.
 *   OMEGA_ERR_INVALID -- e or path is NULL.
 *   OMEGA_ERR_IO      -- could not write file.
 */
OMEGA_API omega_status_t omega_smf_export(omega_engine_t* e, const char* path, int smf_type);

/* ── Markers ──────────────────────────────────────────────────────────────── */

typedef struct
{
    const char* name;
    omega_tick_t tick;
} omega_marker_t;

#define OMEGA_REGION_LOOP 0u
#define OMEGA_REGION_PUNCH 1u
#define OMEGA_REGION_SECTION 2u

typedef struct
{
    const char* name;
    omega_tick_t start_tick;
    omega_tick_t end_tick;
    uint8_t type;
} omega_region_t;

/*
 * Adds a named marker at the given tick.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK          -- marker added.
 *   OMEGA_ERR_INVALID -- e or name is NULL.
 */
OMEGA_API omega_status_t omega_marker_add(omega_engine_t* e, const char* name, omega_tick_t tick);

/*
 * Removes the marker at the given index.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK            -- marker removed.
 *   OMEGA_ERR_INVALID   -- e is NULL.
 *   OMEGA_ERR_NOT_FOUND -- index >= marker count.
 */
OMEGA_API omega_status_t omega_marker_remove(omega_engine_t* e, uint32_t index);

/*
 * Returns the number of markers in the session.
 *
 * Thread: Mutation thread only.
 *
 * Returns: count, or 0 if e is NULL.
 */
OMEGA_API uint32_t omega_marker_count(const omega_engine_t* e);

/*
 * Fills *out with the marker at index.
 * The name pointer is valid until the marker list is modified.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK            -- out filled.
 *   OMEGA_ERR_INVALID   -- e or out is NULL.
 *   OMEGA_ERR_NOT_FOUND -- index >= marker count.
 */
OMEGA_API omega_status_t omega_marker_at(const omega_engine_t* e,
                                         uint32_t index,
                                         omega_marker_t* out);

/*
 * Clears all markers.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK          -- cleared.
 *   OMEGA_ERR_INVALID -- e is NULL.
 */
OMEGA_API omega_status_t omega_marker_clear(omega_engine_t* e);

/* ── Regions ──────────────────────────────────────────────────────────────── */

/*
 * Adds a named region [start_tick, end_tick) of the given type.
 * Type must be one of OMEGA_REGION_LOOP, OMEGA_REGION_PUNCH, OMEGA_REGION_SECTION.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK          -- region added.
 *   OMEGA_ERR_INVALID -- e or name is NULL, or start_tick >= end_tick.
 */
OMEGA_API omega_status_t omega_region_add(
    omega_engine_t* e, const char* name, omega_tick_t start, omega_tick_t end, uint8_t type);

/*
 * Removes the region at the given index.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK            -- region removed.
 *   OMEGA_ERR_INVALID   -- e is NULL.
 *   OMEGA_ERR_NOT_FOUND -- index >= region count.
 */
OMEGA_API omega_status_t omega_region_remove(omega_engine_t* e, uint32_t index);

/*
 * Returns the number of regions in the session.
 *
 * Thread: Mutation thread only.
 *
 * Returns: count, or 0 if e is NULL.
 */
OMEGA_API uint32_t omega_region_count(const omega_engine_t* e);

/*
 * Fills *out with the region at index.
 * The name pointer is valid until the region list is modified.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK            -- out filled.
 *   OMEGA_ERR_INVALID   -- e or out is NULL.
 *   OMEGA_ERR_NOT_FOUND -- index >= region count.
 */
OMEGA_API omega_status_t omega_region_at(const omega_engine_t* e,
                                         uint32_t index,
                                         omega_region_t* out);

/*
 * Clears all regions.
 *
 * Thread: Mutation thread only.
 *
 * Returns:
 *   OMEGA_OK          -- cleared.
 *   OMEGA_ERR_INVALID -- e is NULL.
 */
OMEGA_API omega_status_t omega_region_clear(omega_engine_t* e);

/* ── Anchors ──────────────────────────────────────────────────────────────── */

/* AnchorPoint flag bits. Flags may be combined (e.g., OMEGA_ANCHOR_SNAP | OMEGA_ANCHOR_CUE). */
#define OMEGA_ANCHOR_SNAP 0x01u /* snap reference point */
#define OMEGA_ANCHOR_WARP 0x02u /* stretch/warp boundary */
#define OMEGA_ANCHOR_CUE 0x04u  /* cue launch point */

/*
 * Adds a named anchor to a pattern's intrinsic AnchorList.
 * Anchors are sorted by offset_ticks on insertion.
 *
 * Thread: Mutation thread only, before playback starts.
 *
 * Returns:
 *   OMEGA_OK            -- anchor added.
 *   OMEGA_ERR_INVALID   -- e or name is NULL.
 *   OMEGA_ERR_NOT_FOUND -- pid is not a valid pattern.
 */
OMEGA_API omega_status_t omega_pattern_add_anchor(omega_engine_t* e,
                                                  omega_pattern_id_t pid,
                                                  const char* name,
                                                  omega_tick_t offset,
                                                  uint32_t flags);

/*
 * Removes the first anchor with the given name from the pattern's AnchorList.
 *
 * Thread: Mutation thread only, before playback starts.
 *
 * Returns:
 *   OMEGA_OK            -- anchor removed.
 *   OMEGA_ERR_INVALID   -- e or name is NULL.
 *   OMEGA_ERR_NOT_FOUND -- pid is not a valid pattern, or name not found.
 */
OMEGA_API omega_status_t omega_pattern_remove_anchor(omega_engine_t* e,
                                                     omega_pattern_id_t pid,
                                                     const char* name);

/*
 * Returns the number of anchors in a pattern's AnchorList.
 *
 * Thread: Mutation thread only.
 *
 * Returns: count, or 0 if e is NULL or pid is not found.
 */
OMEGA_API uint32_t omega_pattern_anchor_count(const omega_engine_t* e, omega_pattern_id_t pid);

/*
 * Sets the active snap anchor for a pattern by index.
 * The anchor at index must have the OMEGA_ANCHOR_SNAP flag set.
 *
 * Thread: Mutation thread only, before playback starts.
 *
 * Returns:
 *   OMEGA_OK            -- active snap set.
 *   OMEGA_ERR_INVALID   -- e is NULL, or anchor lacks OMEGA_ANCHOR_SNAP flag.
 *   OMEGA_ERR_NOT_FOUND -- pid is not a valid pattern, or index >= anchor count.
 */
OMEGA_API omega_status_t omega_pattern_set_active_snap(omega_engine_t* e,
                                                       omega_pattern_id_t pid,
                                                       uint32_t index);

/*
 * Adds a named anchor to the event side table for a given track and event index.
 * The EventAnchorTable is a sparse side table; events without anchors have no
 * entry and events remain exactly 24 bytes.
 *
 * Thread: Mutation thread only, before playback starts.
 *
 * Returns:
 *   OMEGA_OK          -- anchor added.
 *   OMEGA_ERR_INVALID -- e or name is NULL.
 */
OMEGA_API omega_status_t omega_event_add_anchor(omega_engine_t* e,
                                                omega_track_id_t track,
                                                uint32_t event_index,
                                                const char* name,
                                                omega_tick_t offset,
                                                uint32_t flags);

/*
 * Removes the first anchor with the given name from the event side table entry
 * for (track, event_index).
 *
 * Thread: Mutation thread only, before playback starts.
 *
 * Returns:
 *   OMEGA_OK            -- anchor removed.
 *   OMEGA_ERR_INVALID   -- e or name is NULL.
 *   OMEGA_ERR_NOT_FOUND -- no anchor entry for (track, event_index), or name not found.
 */
OMEGA_API omega_status_t omega_event_remove_anchor(omega_engine_t* e,
                                                   omega_track_id_t track,
                                                   uint32_t event_index,
                                                   const char* name);

/* ── Utilities ────────────────────────────────────────────────────────────── */

/*
 * Converts a MIDI pitch byte to a human-readable note name string.
 *
 * Writes a NUL-terminated string such as "C4", "F#3", or "A-1" into out.
 * At most out_size bytes are written (including the NUL terminator); the
 * output is always NUL-terminated when out_size > 0.  pitch must be in the
 * range 0–127 (standard MIDI); values above 127 are clamped.
 *
 * Sharp names are used: C, C#, D, D#, E, F, F#, G, G#, A, A#, B.
 * Octave follows the MIDI convention: middle C (pitch 60) is C4.
 *
 * Thread: Any thread.
 */
OMEGA_API void omega_midi_note_name(uint8_t pitch, char* out, size_t out_size);

/* ── Timer ────────────────────────────────────────────────────────────────── */

typedef struct omega_timer_s omega_timer_t;

/*
 * Creates an OmegaTimer that drives engine->process() at interval_us
 * microseconds. Starts the internal thread immediately.
 *
 * interval_us == 0 uses the default of 1000 µs (1 ms).
 *
 * Thread: Mutation thread only, before starting the timer loop.
 *
 * Returns:
 *   Non-NULL -- timer started.
 *   NULL     -- e is NULL or allocation failed.
 */
OMEGA_API omega_timer_t* omega_timer_create(omega_engine_t* e, uint32_t interval_us);

/*
 * Stops and joins the timer thread, calls process() one final time,
 * and frees the timer object.
 *
 * Thread: Mutation thread only.
 */
OMEGA_API void omega_timer_destroy(omega_timer_t* timer);

/* ── Snap ─────────────────────────────────────────────────────────────────── */

/* Enabled snap target bits (combinable). */
#define OMEGA_SNAP_GRID 0x01u
#define OMEGA_SNAP_MARKERS 0x02u
#define OMEGA_SNAP_REGIONS 0x04u
#define OMEGA_SNAP_ANCHORS 0x08u

/*
 * Snap configuration passed to omega_snap().
 *
 *   targets           -- bitfield of OMEGA_SNAP_* flags.
 *   grid_subdiv_ticks -- grid subdivision in ticks; 0 = derive from engine meter.
 *   tolerance_ticks   -- maximum snap distance; 0 = unlimited.
 */
typedef struct
{
    uint8_t targets;
    omega_tick_t grid_subdiv_ticks;
    omega_tick_t tolerance_ticks;
} omega_snap_config_t;

/*
 * Result returned by omega_snap().
 *
 *   snapped_tick -- the snapped position (equals input tick when did_snap == 0).
 *   source       -- which OMEGA_SNAP_* target provided the winning candidate.
 *   did_snap     -- 1 if a snap occurred, 0 if no candidate was within tolerance.
 */
typedef struct
{
    omega_tick_t snapped_tick;
    uint8_t source;
    int did_snap;
} omega_snap_result_t;

/*
 * Snap tick to the nearest candidate from the enabled target sets.
 *
 * When OMEGA_SNAP_GRID is set and the engine has no time signature (freeform
 * mode) and grid_subdiv_ticks is 0, returns OMEGA_ERR_NO_METER.
 *
 * Thread: Mutation thread only. Must not be called concurrently with process().
 *
 * Returns:
 *   OMEGA_OK            -- out filled; check did_snap to see if a snap occurred.
 *   OMEGA_ERR_INVALID   -- e, config, or out is NULL.
 *   OMEGA_ERR_NO_METER  -- OMEGA_SNAP_GRID requested, freeform mode, no subdiv given.
 */
OMEGA_API omega_status_t omega_snap(const omega_engine_t* e,
                                    omega_tick_t tick,
                                    const omega_snap_config_t* config,
                                    omega_snap_result_t* out);

#ifdef __cplusplus
}
#endif
