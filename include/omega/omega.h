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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ────────────────────────────────────────────────────────────── */

/* Ticks per quarter note. Query at runtime via this macro; do not hardcode 480. */
#define OMEGA_PPQN 480u

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

/* ── (Further API sections added here as implementation begins) ─────────────
 *
 * Template for engine create/destroy:
 *
 *   Ownership: caller-owned. Call omega_engine_destroy() before exit.
 *   The engine holds non-owning references to all objects passed to it.
 *   Destroy all dependent objects before destroying the engine.
 *
 * Template for a mutating function:
 *
 *   Thread: Mutation thread only.
 *   Must not be called concurrently with another mutation; serialize externally.
 *
 *   Returns:
 *     OMEGA_OK            — <success condition>
 *     OMEGA_ERR_INVALID   — e or <arg> is NULL
 *     OMEGA_ERR_NOMEM     — allocation failed
 *     OMEGA_ERR_QUEUE_FULL — mutation queue at capacity
 */

#ifdef __cplusplus
}
#endif
