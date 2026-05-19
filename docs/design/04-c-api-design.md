# Design: C API

## Goals

The C API (`omega.h`) is Omega's public contract with the world. Once published, it must not break. It is the only surface that binds to Python, Lua, Swift, JavaScript, Rust, C#, or any other language. The C++ internals can evolve freely; the C API is a stable facade.

**Rules:**
- No C++ types in the public header (no `std::`, no references, no templates)
- All objects are opaque handles
- All ownership is explicit
- All functions return a status code or a handle; never void for mutating operations
- Callbacks carry a `void* userdata` parameter
- The header compiles as both C99 and C++17

---

## Header Structure

```c
/* omega.h — Omega C API */
#ifndef OMEGA_H
#define OMEGA_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Version ─────────────────────────────────────────────────────────── */

#define OMEGA_VERSION_MAJOR 0
#define OMEGA_VERSION_MINOR 1
#define OMEGA_VERSION_PATCH 0

typedef struct {
    uint16_t major, minor, patch;
} omega_version_t;

omega_version_t omega_version(void);

/* ── Status codes ────────────────────────────────────────────────────── */

typedef enum {
    OMEGA_OK              =  0,
    OMEGA_ERR_NOMEM       = -1,   /* allocation failed */
    OMEGA_ERR_INVALID     = -2,   /* null or invalid argument */
    OMEGA_ERR_QUEUE_FULL  = -3,   /* mutation queue is full */
    OMEGA_ERR_NOT_FOUND   = -4,   /* pattern, track, sink not found */
    OMEGA_ERR_RANGE       = -5,   /* tick or index out of range */
    OMEGA_ERR_STATE       = -6,   /* invalid state for this operation */
    OMEGA_ERR_IO          = -7,   /* MIDI port or file I/O error */
} omega_status_t;

const char* omega_status_str(omega_status_t status);

/* ── Opaque handle types ─────────────────────────────────────────────── */

typedef struct omega_engine_s*      omega_engine_t;
typedef struct omega_sink_s*        omega_sink_t;
typedef struct omega_clock_s*       omega_clock_t;
typedef struct omega_track_s*       omega_track_t;
typedef struct omega_pattern_s*     omega_pattern_t;
typedef struct omega_session_s*     omega_session_t;
typedef struct omega_source_s*      omega_source_t;
typedef struct omega_input_s*       omega_input_t;
typedef struct omega_dispatcher_s*  omega_dispatcher_t;
typedef struct omega_input_disp_s*  omega_input_dispatcher_t;

typedef uint32_t omega_track_id_t;
typedef uint32_t omega_pattern_id_t;
typedef uint32_t omega_sink_id_t;
typedef uint32_t omega_slot_id_t;
typedef uint32_t omega_mod_channel_t;
typedef uint64_t omega_tick_t;

#define OMEGA_INVALID_ID  UINT32_MAX
#define OMEGA_PPQN        480u
#define OMEGA_MOD_INVALID UINT32_MAX

/* ── Events ──────────────────────────────────────────────────────────── */

typedef struct {
    omega_tick_t    tick;
    omega_sink_id_t sink_id;
    uint8_t         type;       /* OMEGA_EVENT_* constants */
    uint8_t         channel;
    uint8_t         data[8];
} omega_event_t;

/* Event type constants */
#define OMEGA_EVENT_NOTE_ON       0x00
#define OMEGA_EVENT_NOTE_OFF      0x01
#define OMEGA_EVENT_CC            0x02
#define OMEGA_EVENT_PROGRAM       0x03
#define OMEGA_EVENT_PITCH_BEND    0x04
#define OMEGA_EVENT_AFTERTOUCH    0x05
#define OMEGA_EVENT_POLY_AT       0x06
#define OMEGA_EVENT_SYSEX         0x07
#define OMEGA_EVENT_TEMPO         0x08
#define OMEGA_EVENT_OSC           0x09
#define OMEGA_EVENT_PARAM         0x0A

/* ── Clock source ────────────────────────────────────────────────────── */

/* Function pointer type for clock implementations */
typedef uint64_t (*omega_clock_fn_t)(void* userdata);

omega_clock_t omega_clock_create_internal(void);
omega_clock_t omega_clock_create_custom(omega_clock_fn_t fn, void* userdata);
void          omega_clock_destroy(omega_clock_t clock);

/* ── Output sink ─────────────────────────────────────────────────────── */

/* Called from the timing thread — must not block */
typedef void (*omega_send_fn_t)(const omega_event_t* event, void* userdata);

omega_sink_t    omega_sink_create(omega_send_fn_t fn, void* userdata);
void            omega_sink_destroy(omega_sink_t sink);
omega_sink_id_t omega_sink_id(omega_sink_t sink);

/* ── Engine lifecycle ────────────────────────────────────────────────── */

typedef struct {
    uint32_t   command_queue_capacity;  /* default: 4096; must be power of two */
    size_t     memory_pool_size;        /* 0 = use heap */
    void*      memory_pool;             /* non-null = use this buffer */
} omega_config_t;

omega_engine_t omega_engine_create(const omega_config_t* config);
void           omega_engine_destroy(omega_engine_t engine);

omega_status_t omega_engine_set_clock(omega_engine_t engine, omega_clock_t clock);
omega_status_t omega_engine_add_sink(omega_engine_t engine, omega_sink_t sink);
omega_status_t omega_engine_remove_sink(omega_engine_t engine, omega_sink_id_t sink_id);

/* ── Transport ───────────────────────────────────────────────────────── */

typedef enum {
    OMEGA_TRANSPORT_STOPPED   = 0,
    OMEGA_TRANSPORT_PLAYING   = 1,
    OMEGA_TRANSPORT_RECORDING = 2,
    OMEGA_TRANSPORT_PAUSED    = 3,
} omega_transport_state_t;

omega_status_t          omega_transport_play(omega_engine_t engine);
omega_status_t          omega_transport_stop(omega_engine_t engine);
omega_status_t          omega_transport_record(omega_engine_t engine);
omega_status_t          omega_transport_pause(omega_engine_t engine);
omega_status_t          omega_transport_locate(omega_engine_t engine, omega_tick_t tick);
omega_transport_state_t omega_transport_state(omega_engine_t engine);
omega_tick_t            omega_transport_position(omega_engine_t engine);

/* ── Tempo ───────────────────────────────────────────────────────────── */

/* BPM is represented as milli-BPM: 120.000 BPM = 120000 */
omega_status_t omega_set_tempo(omega_engine_t engine, uint32_t bpm_milli);
uint32_t       omega_get_tempo(omega_engine_t engine);  /* current milli-BPM */

/* ── Process (call from timing thread) ──────────────────────────────── */

/* Advances the engine to ticks_now, firing all due events.
   ticks_now is provided by the clock source (or pass 0 to use engine's clock). */
void omega_process(omega_engine_t engine);

/* ── Timeline mode ───────────────────────────────────────────────────── */

omega_track_id_t omega_track_create(omega_engine_t engine, const char* name);
omega_status_t   omega_track_destroy(omega_engine_t engine, omega_track_id_t track);
omega_status_t   omega_track_set_sink(omega_engine_t engine, omega_track_id_t track,
                                       omega_sink_id_t sink);
omega_status_t   omega_track_set_channel(omega_engine_t engine, omega_track_id_t track,
                                          uint8_t channel);
omega_status_t   omega_track_set_mute(omega_engine_t engine, omega_track_id_t track,
                                       int muted);
omega_status_t   omega_track_add_event(omega_engine_t engine, omega_track_id_t track,
                                        const omega_event_t* event);
omega_status_t   omega_track_delete_event(omega_engine_t engine, omega_track_id_t track,
                                           omega_tick_t tick, uint32_t index);

/* ── Pattern mode ────────────────────────────────────────────────────── */

omega_pattern_id_t omega_pattern_create(omega_engine_t engine, const char* name,
                                         omega_tick_t length_ticks);
omega_status_t     omega_pattern_destroy(omega_engine_t engine, omega_pattern_id_t pattern);
omega_status_t     omega_pattern_set_length(omega_engine_t engine, omega_pattern_id_t pattern,
                                             omega_tick_t length_ticks);
omega_status_t     omega_pattern_add_event(omega_engine_t engine, omega_pattern_id_t pattern,
                                            const omega_event_t* event);

/* ── Performance mode ────────────────────────────────────────────────── */

typedef enum {
    OMEGA_CUE_AT_BOUNDARY  = 0,  /* default: start at next loop boundary */
    OMEGA_CUE_IMMEDIATE    = 1,  /* start immediately */
    OMEGA_CUE_QUANTIZED    = 2,  /* start at next beat boundary */
} omega_cue_mode_t;

omega_status_t omega_perf_assign(omega_engine_t engine, omega_slot_id_t slot,
                                  omega_pattern_id_t pattern);
omega_status_t omega_perf_cue(omega_engine_t engine, omega_slot_id_t slot,
                               omega_cue_mode_t mode);
omega_status_t omega_perf_stop(omega_engine_t engine, omega_slot_id_t slot,
                                omega_cue_mode_t mode);
omega_status_t omega_perf_stop_all(omega_engine_t engine, omega_cue_mode_t mode);
omega_status_t omega_perf_set_transpose(omega_engine_t engine, omega_slot_id_t slot,
                                         int8_t semitones);
omega_status_t omega_perf_set_velocity_scale(omega_engine_t engine, omega_slot_id_t slot,
                                              uint8_t scale_pct);  /* 0-200, 100=unity */
omega_status_t omega_perf_set_random_bias(omega_engine_t engine, omega_slot_id_t slot,
                                           uint8_t bias_pct);  /* 0=none, 100=max */

/* ── Callbacks ────────────────────────────────────────────────────────── */

/* Called when an event is captured during recording (timing thread) */
typedef void (*omega_record_cb_t)(const omega_event_t* event, void* userdata);
omega_status_t omega_set_record_callback(omega_engine_t engine,
                                          omega_record_cb_t cb, void* userdata);

/* Called when transport state changes (timing thread) */
typedef void (*omega_transport_cb_t)(omega_transport_state_t state, void* userdata);
omega_status_t omega_set_transport_callback(omega_engine_t engine,
                                             omega_transport_cb_t cb, void* userdata);

/* ── Performance Context types (defined here; setters follow source/input APIs) ── */

typedef struct {
    uint8_t root;   /* 0–11, C=0 */
    uint8_t mask;   /* semitone bitmask; bit i set = semitone i active */
} omega_scale_t;

typedef struct {
    uint8_t root;
    uint8_t type;         /* OMEGA_CHORD_MAJ, _MIN, _DOM7, _MAJ7, _MIN7, _DIM, _AUG */
    uint8_t voices[6];    /* absolute MIDI note numbers; 0xFF = unused voice */
    uint8_t voice_count;
} omega_chord_t;

typedef struct {
    omega_scale_t scale;
    omega_chord_t chord;
    int8_t        global_transpose;  /* -24 to +24 semitones */
    uint8_t       global_velocity;   /* 0–200, 100=unity */
    uint8_t       chaos;             /* 0–100 */
    uint8_t       groove_id;         /* index into GrooveLibrary; 0 = straight */
    float         swing;             /* 0.0–1.0 */
    uint32_t      random_seed;
} omega_perf_ctx_t;

/* ── ProcessContext (passed to advance and on_locate callbacks) ───────── */

/* omega_process_ctx_t is valid only for the duration of the advance/on_locate call.
   Do not cache the pointer or any pointer inside it. */
typedef struct {
    /* ModulationBus access — timing thread only */
    float  (*mod_get)(omega_mod_channel_t ch, void* mod_userdata);
    void   (*mod_set)(omega_mod_channel_t ch, float value, void* mod_userdata);
    void*    mod_userdata;

    /* PerformanceContext snapshot for this cycle — read-only */
    const omega_perf_ctx_t* perf_ctx;

    /* InputBus — events delivered by EventInput::poll() this cycle */
    const omega_event_t* input_events;
    uint32_t             input_event_count;

    /* Wall-clock duration of this process() cycle in nanoseconds */
    uint64_t cycle_ns;
} omega_process_ctx_t;

/* ── Custom event sources ────────────────────────────────────────────── */

/* advance_fn and locate_fn are called from the timing thread — must not block or allocate */
typedef void (*omega_advance_fn_t)(uint64_t to_tick,
                                    omega_dispatcher_t dispatcher,
                                    const omega_process_ctx_t* ctx,
                                    void* userdata);
typedef void (*omega_transport_start_fn_t)(uint64_t start_tick, void* userdata);
typedef void (*omega_transport_stop_fn_t)(void* userdata);
typedef void (*omega_locate_fn_t)(uint64_t tick,
                                   omega_dispatcher_t chase_dispatcher,
                                   const omega_process_ctx_t* ctx,
                                   void* userdata);

typedef struct {
    omega_advance_fn_t         advance;
    omega_transport_start_fn_t on_start;    /* nullable */
    omega_transport_stop_fn_t  on_stop;     /* nullable */
    omega_locate_fn_t          on_locate;   /* nullable */
    void*                      userdata;
} omega_source_desc_t;

omega_source_t omega_source_create(const omega_source_desc_t* desc);
void           omega_source_destroy(omega_source_t source);
omega_status_t omega_engine_add_source(omega_engine_t engine, omega_source_t source);
omega_status_t omega_engine_remove_source(omega_engine_t engine, omega_source_t source);

/* Dispatch an event from within an advance or on_locate callback */
void omega_dispatch(omega_dispatcher_t dispatcher, const omega_event_t* event);

/* ── Event inputs ────────────────────────────────────────────────────── */

/* poll_fn is called from the timing thread — must not block or allocate */
typedef void (*omega_input_poll_fn_t)(omega_input_dispatcher_t dispatcher, void* userdata);

typedef struct {
    omega_input_poll_fn_t poll;
    void*                 userdata;
} omega_input_desc_t;

omega_input_t  omega_input_create(const omega_input_desc_t* desc);
void           omega_input_destroy(omega_input_t input);
omega_status_t omega_engine_add_input(omega_engine_t engine, omega_input_t input);
omega_status_t omega_engine_remove_input(omega_engine_t engine, omega_input_t input);

/* Call from within poll callback to deliver an event into the InputBus */
void omega_deliver(omega_input_dispatcher_t dispatcher, const omega_event_t* event);

/* How many input events were dropped this cycle due to InputBus overflow */
uint32_t omega_input_overflow_count(omega_engine_t engine);

/* ── Modulation Bus ──────────────────────────────────────────────────── */

omega_mod_channel_t omega_mod_register(omega_engine_t engine, const char* name, float initial);
omega_mod_channel_t omega_mod_find(omega_engine_t engine, const char* name);
float               omega_mod_get(omega_engine_t engine, omega_mod_channel_t ch);
omega_status_t      omega_mod_set(omega_engine_t engine, omega_mod_channel_t ch, float value);

/* Copy all channel values into caller-owned array — safe to call from the UI thread */
omega_status_t omega_mod_snapshot(omega_engine_t engine, float* out, uint32_t count);

/* ── Performance Context setters ─────────────────────────────────────── */

/* Types (omega_scale_t, omega_chord_t, omega_perf_ctx_t) are declared above. */

omega_status_t omega_ctx_set_scale(omega_engine_t engine, const omega_scale_t* scale);
omega_status_t omega_ctx_set_chord(omega_engine_t engine, const omega_chord_t* chord);
omega_status_t omega_ctx_set_transpose(omega_engine_t engine, int8_t semitones);
omega_status_t omega_ctx_set_velocity(omega_engine_t engine, uint8_t scale_pct);
omega_status_t omega_ctx_set_chaos(omega_engine_t engine, uint8_t chaos_pct);
omega_status_t omega_ctx_set_groove(omega_engine_t engine, uint8_t groove_id, float swing);

/* Read current context — for UI display; may read slightly stale values */
omega_status_t omega_ctx_get(omega_engine_t engine, omega_perf_ctx_t* out);

/* ── SMF import/export ───────────────────────────────────────────────── */

omega_status_t omega_smf_import(omega_engine_t engine, const char* path);
omega_status_t omega_smf_export(omega_engine_t engine, const char* path, int smf_type);

/* ── Convenience helpers ─────────────────────────────────────────────── */

/* Build common event types without casting */
omega_event_t omega_make_note_on(omega_tick_t tick, omega_sink_id_t sink,
                                  uint8_t ch, uint8_t note, uint8_t vel,
                                  omega_tick_t duration_ticks);
omega_event_t omega_make_cc(omega_tick_t tick, omega_sink_id_t sink,
                              uint8_t ch, uint8_t cc, uint8_t value);
omega_event_t omega_make_program(omega_tick_t tick, omega_sink_id_t sink,
                                  uint8_t ch, uint8_t program);

#ifdef __cplusplus
}
#endif
#endif /* OMEGA_H */
```

---

## Ownership Rules

Every `omega_*_create()` call returns a handle that the caller owns. Every handle must be passed to its corresponding `omega_*_destroy()` before `omega_engine_destroy()` is called. Destroying the engine first is undefined behavior.

The engine does not take ownership of clocks or sinks — it holds a non-owning reference. The caller is responsible for lifetime management.

Strings passed to the API (pattern names, track names, file paths) are copied by the library. The caller's string does not need to outlive the call.

---

## Error Handling

All mutating functions return `omega_status_t`. `OMEGA_OK` (0) on success, a negative value on error. This is C-idiomatic and works naturally in every binding language.

The library does not use C++ exceptions across the C API boundary. Internally, exceptions may be used (bounded by `try`/`catch` wrappers at the C API boundary). In embedded builds, exceptions may be disabled entirely — the C++ core must tolerate `OMEGA_NO_EXCEPTIONS` as a compile flag.

---

## Versioning and ABI Stability

The C API uses **semantic versioning**. The MAJOR version increments on any breaking change to the ABI. The header exposes `OMEGA_VERSION_MAJOR` as a compile-time constant; `omega_version()` provides the runtime version.

Callers should verify `omega_version().major == OMEGA_VERSION_MAJOR` at startup if dynamic linking.

The opaque handle pattern means struct internals can change freely without breaking the ABI. New functions can be added in MINOR versions. Functions are never removed in PATCH or MINOR versions.

---

## Open Issues

- **Iteration**: How does a UI enumerate tracks, patterns, and events? A callback-based iterator (`omega_track_foreach()`) is cleaner than returning arrays. Design in v2.
- **Batch operations**: Adding 10,000 events one at a time via the queue is slow. A bulk-insert command that takes an array of events is needed for SMF import. Add `omega_track_add_events_bulk()`.
- **Thread-safe query**: Querying `omega_transport_position()` from the UI thread may read stale data. Document this; do not add a lock. Similarly, `omega_mod_get()` from the UI thread should use `omega_mod_snapshot()` instead.
- **Built-in source access**: The existing `omega_track_*`, `omega_pattern_*`, and `omega_perf_*` functions target the built-in `TimelineSource`, `SongArrangementSource`, and `PerformanceSource` respectively. If a caller removes a built-in source via `omega_engine_remove_source()`, those functions return `OMEGA_ERR_NOT_FOUND`. Document clearly; consider making built-in sources non-removable in v1.
- **Custom source serialization**: Custom `EventSource` and `EventInput` implementations cannot be serialized by the library. The `.omega` session file preserves built-in source data only. Custom sources and inputs are session-ephemeral — they must be re-registered and re-configured by the application on each load.
- **ProcessContext pointer lifetime**: The `omega_process_ctx_t*` passed to `advance_fn_t` and `locate_fn_t` is valid only for the duration of that call. The `input_events` array inside it is stack-lifetime. Document explicitly — callers must not cache either pointer.
- **PerformanceContext forward declaration**: `omega_perf_ctx_t` is referenced in `omega_process_ctx_t` but defined later in the same header. Reorder or use a forward declaration so the header compiles as C99 top-to-bottom.
