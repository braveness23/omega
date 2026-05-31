#include <omega/omega.h>

#include <array>
#include <cstring>

namespace
{

const std::array<const char*, 8> k_status_strings = {
    "ok",              /* OMEGA_OK = 0            */
    "invalid",         /* OMEGA_ERR_INVALID = -1  */
    "out of memory",   /* OMEGA_ERR_NOMEM = -2    */
    "not found",       /* OMEGA_ERR_NOT_FOUND = -3 */
    "queue full",      /* OMEGA_ERR_QUEUE_FULL = -4 */
    "unsupported",     /* OMEGA_ERR_UNSUPPORTED = -5 */
    "no meter",        /* OMEGA_ERR_NO_METER = -6 */
    "no smpte config", /* OMEGA_ERR_NO_SMPTE_CONFIG = -7 */
};

constexpr int k_status_count = static_cast<int>(k_status_strings.size());

}  // namespace

extern "C" {

omega_version_t omega_version(void)
{
    omega_version_t v;
    v.major = OMEGA_VERSION_MAJOR;
    v.minor = OMEGA_VERSION_MINOR;
    v.patch = OMEGA_VERSION_PATCH;
    return v;
}

const char* omega_status_string(omega_status_t status)
{
    int idx = -(int)status;
    if (idx < 0 || idx >= k_status_count)
    {
        return "unknown";
    }
    return k_status_strings[idx];
}

omega_event_t omega_make_note_on(uint64_t tick,
                                 uint32_t sink_id,
                                 uint8_t channel,
                                 uint8_t note,
                                 uint8_t velocity,
                                 uint32_t duration_ticks)
{
    omega_event_t e;
    memset(&e, 0, sizeof(e));
    e.tick = tick;
    e.sink_id = sink_id;
    e.payload_tag = OMEGA_NOTE_ON;
    e.channel = channel;
    e.data[0] = note;
    e.data[1] = velocity;
    memcpy(&e.data[2], &duration_ticks, sizeof(duration_ticks));
    return e;
}

omega_event_t omega_make_cc(
    uint64_t tick, uint32_t sink_id, uint8_t channel, uint8_t controller, uint8_t value)
{
    omega_event_t e;
    memset(&e, 0, sizeof(e));
    e.tick = tick;
    e.sink_id = sink_id;
    e.payload_tag = OMEGA_CC;
    e.channel = channel;
    e.data[0] = controller;
    e.data[1] = value;
    return e;
}

omega_event_t omega_make_program(uint64_t tick, uint32_t sink_id, uint8_t channel, uint8_t program)
{
    omega_event_t e;
    memset(&e, 0, sizeof(e));
    e.tick = tick;
    e.sink_id = sink_id;
    e.payload_tag = OMEGA_PROGRAM;
    e.channel = channel;
    e.data[0] = program;
    return e;
}

// ── Event field accessors ─────────────────────────────────────────────────────

uint8_t omega_event_note_pitch(const omega_event_t* e)
{
    return e->data[0];
}

uint8_t omega_event_note_velocity(const omega_event_t* e)
{
    return e->data[1];
}

uint32_t omega_event_note_duration(const omega_event_t* e)
{
    uint32_t dur = 0;
    memcpy(&dur, &e->data[2], sizeof(dur));
    return dur;
}

omega_status_t omega_event_set_pitch(omega_event_t* e, uint8_t pitch)
{
    if (e == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    e->data[0] = pitch;
    return OMEGA_OK;
}

omega_status_t omega_event_set_velocity(omega_event_t* e, uint8_t vel)
{
    if (e == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    e->data[1] = vel;
    return OMEGA_OK;
}

omega_status_t omega_event_set_duration(omega_event_t* e, uint32_t dur)
{
    if (e == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    memcpy(&e->data[2], &dur, sizeof(dur));
    return OMEGA_OK;
}

uint8_t omega_event_cc_number(const omega_event_t* e)
{
    return e->data[0];
}

uint8_t omega_event_cc_value(const omega_event_t* e)
{
    return e->data[1];
}

omega_event_t omega_make_ctrl_start_slot(uint64_t tick,
                                         uint32_t sink_id,
                                         uint32_t slot,
                                         omega_cue_mode_t mode)
{
    omega_event_t e;
    memset(&e, 0, sizeof(e));
    e.tick = tick;
    e.sink_id = sink_id;
    e.payload_tag = OMEGA_CTRL_START_SLOT;
    memcpy(&e.data[0], &slot, sizeof(slot));
    e.data[4] = static_cast<uint8_t>(mode);
    return e;
}

omega_event_t omega_make_ctrl_stop_slot(uint64_t tick,
                                        uint32_t sink_id,
                                        uint32_t slot,
                                        omega_cue_mode_t mode)
{
    omega_event_t e;
    memset(&e, 0, sizeof(e));
    e.tick = tick;
    e.sink_id = sink_id;
    e.payload_tag = OMEGA_CTRL_STOP_SLOT;
    memcpy(&e.data[0], &slot, sizeof(slot));
    e.data[4] = static_cast<uint8_t>(mode);
    return e;
}

omega_event_t omega_make_ctrl_set_tempo(uint64_t tick, uint32_t sink_id, uint32_t bpm_milli)
{
    omega_event_t e;
    memset(&e, 0, sizeof(e));
    e.tick = tick;
    e.sink_id = sink_id;
    e.payload_tag = OMEGA_CTRL_SET_TEMPO;
    memcpy(&e.data[0], &bpm_milli, sizeof(bpm_milli));
    return e;
}

omega_event_t omega_make_ctrl_transpose(uint64_t tick,
                                        uint32_t sink_id,
                                        uint32_t slot,
                                        int8_t semitones)
{
    omega_event_t e;
    memset(&e, 0, sizeof(e));
    e.tick = tick;
    e.sink_id = sink_id;
    e.payload_tag = OMEGA_CTRL_TRANSPOSE;
    memcpy(&e.data[0], &slot, sizeof(slot));
    e.data[4] = static_cast<uint8_t>(semitones);
    return e;
}

omega_event_t omega_make_ctrl_start_slot_wait(
    uint64_t tick, uint32_t sink_id, uint32_t target_slot, omega_cue_mode_t mode, uint8_t ctrl_slot)
{
    omega_event_t e;
    memset(&e, 0, sizeof(e));
    e.tick = tick;
    e.sink_id = sink_id;
    e.payload_tag = OMEGA_CTRL_START_SLOT_WAIT;
    memcpy(&e.data[0], &target_slot, sizeof(target_slot));
    e.data[4] = static_cast<uint8_t>(mode);
    e.data[5] = ctrl_slot;
    return e;
}

}  // extern "C"
