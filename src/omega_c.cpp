#include <omega/commands.h>
#include <omega/engine.h>
#include <omega/omega.h>
#include <omega/sink.h>
#include <omega/types.h>

// omega_engine_s is the heap-allocated owner of the C++ Engine.
// omega_engine_t* (opaque to C callers) points to one of these.
struct omega_engine_s  // NOLINT(readability-identifier-naming)
{
    omega::Engine engine;
    omega_engine_s() {}  // NOLINT(modernize-use-equals-default) — prevents aggregate init
};

// omega_sink_t is an opaque alias for omega::OutputSink.
// C++ callers cast OutputSink* to omega_sink_t* and pass it to the C API.
struct omega_sink_s  // NOLINT(readability-identifier-naming)
{};

extern "C" {

// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
omega_engine_t* omega_engine_create(void)
{
    return new (std::nothrow) omega_engine_s{};  // NOLINT(cppcoreguidelines-owning-memory)
}

void omega_engine_destroy(omega_engine_t* eng)
{
    delete eng;  // NOLINT(cppcoreguidelines-owning-memory)
}

omega_status_t omega_engine_add_sink(omega_engine_t* eng, omega_sink_t* sink)
{
    if (eng == nullptr || sink == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return eng->engine.add_sink(reinterpret_cast<omega::OutputSink*>(sink));
}

omega_status_t omega_engine_add_track(omega_engine_t* eng,
                                      const char* name,
                                      omega_track_id_t* out_id)
{
    if (eng == nullptr || out_id == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    *out_id = eng->engine.add_track(name != nullptr ? name : "");
    return OMEGA_OK;
}

omega_status_t omega_engine_set_track_sink(omega_engine_t* eng,
                                           omega_track_id_t track,
                                           uint32_t sink_id)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.set_track_sink(track, sink_id);
}

omega_status_t omega_engine_add_event(omega_engine_t* eng, omega_track_id_t track, omega_event_t ev)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.enqueue(omega::AddEventCmd{track, ev});
}

omega_status_t omega_engine_play(omega_engine_t* eng)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.enqueue(omega::TransportCmd{omega::TransportAction::PLAY, 0u});
}

omega_status_t omega_engine_stop(omega_engine_t* eng)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.enqueue(omega::TransportCmd{omega::TransportAction::STOP, 0u});
}

void omega_engine_process(omega_engine_t* eng)
{
    if (eng != nullptr)
    {
        eng->engine.process();
    }
}

omega_transport_state_t omega_engine_transport_state(const omega_engine_t* eng)
{
    if (eng == nullptr)
    {
        return OMEGA_TRANSPORT_STOPPED;
    }
    return static_cast<omega_transport_state_t>(
        static_cast<uint8_t>(eng->engine.transport_state()));
}

uint64_t omega_engine_position_ns(const omega_engine_t* eng)
{
    if (eng == nullptr)
    {
        return 0;
    }
    return eng->engine.transport_position_ns();
}

omega_pattern_id_t omega_pattern_create(omega_engine_t* eng,
                                        const char* name,
                                        omega_tick_t length_ticks)
{
    if (eng == nullptr)
    {
        return OMEGA_PATTERN_INVALID;
    }
    return eng->engine.create_pattern(name != nullptr ? name : "", length_ticks);
}

omega_status_t omega_pattern_destroy(omega_engine_t* eng, omega_pattern_id_t id)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    eng->engine.destroy_pattern(id);
    return OMEGA_OK;
}

omega_status_t omega_pattern_add_event(omega_engine_t* eng,
                                       omega_pattern_id_t id,
                                       const omega_event_t* ev)
{
    if (eng == nullptr || ev == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.pattern_add_event(id, *ev);
}

omega_status_t omega_pattern_set_length(omega_engine_t* eng,
                                        omega_pattern_id_t id,
                                        omega_tick_t length_ticks)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.pattern_set_length(id, length_ticks);
}

}  // extern "C"
