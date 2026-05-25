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

}  // extern "C"
