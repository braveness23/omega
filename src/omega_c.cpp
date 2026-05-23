#include <omega/commands.h>
#include <omega/engine.h>
#include <omega/event_input.h>
#include <omega/event_source.h>
#include <omega/omega.h>
#include <omega/perf_slot.h>
#include <omega/sink.h>
#include <omega/types.h>

#include <new>

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

// omega_input_t is the heap-allocated owner of a CEventInput.
// omega_input_dispatcher_t is an opaque alias for omega::InputDispatcher.
struct omega_input_dispatcher_s  // NOLINT(readability-identifier-naming)
{};

// omega_source_t is the heap-allocated owner of a CEventSource.
// omega_dispatcher_t is an opaque alias for omega::EventDispatcher.
// omega_process_context_t is an opaque alias for omega::ProcessContext.
struct omega_source_s  // NOLINT(readability-identifier-naming)
{};
struct omega_dispatcher_s  // NOLINT(readability-identifier-naming)
{};
struct omega_process_context_s  // NOLINT(readability-identifier-naming)
{};

class CEventInput : public omega::EventInput
{
public:
    CEventInput(omega_input_poll_fn_t poll_fn, void* userdata) noexcept
        : poll_fn_{poll_fn}, userdata_{userdata}
    {}

    void poll(omega::InputDispatcher& dispatcher) override
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        poll_fn_(reinterpret_cast<omega_input_dispatcher_t*>(&dispatcher), userdata_);
    }

private:
    omega_input_poll_fn_t poll_fn_;
    void* userdata_;
};

class CEventSource : public omega::EventSource
{
public:
    CEventSource(omega_source_advance_fn_t advance_fn, void* userdata, uint32_t priority) noexcept
        : advance_fn_{advance_fn}, userdata_{userdata}, priority_{priority}
    {}

    void advance(uint64_t to_tick,
                 omega::EventDispatcher& dispatcher,
                 omega::ProcessContext& ctx) override
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        advance_fn_(to_tick,
                    reinterpret_cast<omega_dispatcher_t*>(&dispatcher),
                    reinterpret_cast<omega_process_context_t*>(&ctx),  // NOLINT
                    userdata_);
    }

    [[nodiscard]] uint32_t priority() const noexcept { return priority_; }

private:
    omega_source_advance_fn_t advance_fn_;
    void* userdata_;
    uint32_t priority_;
};

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

omega_status_t omega_song_append(omega_engine_t* eng,
                                 omega_pattern_id_t pattern_id,
                                 uint32_t repeats)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.song_append(pattern_id, repeats);
}

omega_status_t omega_song_clear(omega_engine_t* eng)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.song_clear();
}

omega_status_t omega_perf_assign(omega_engine_t* eng,
                                 omega_slot_id_t slot,
                                 omega_pattern_id_t pattern_id)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.perf_assign(slot, pattern_id);
}

omega_status_t omega_perf_cue(omega_engine_t* eng, omega_slot_id_t slot, omega_cue_mode_t mode)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    omega::CueMode cpp_mode =
        (mode == OMEGA_CUE_IMMEDIATE) ? omega::CueMode::IMMEDIATE : omega::CueMode::NEXT_BEAT;
    return eng->engine.perf_cue(slot, cpp_mode);
}

omega_status_t omega_perf_stop(omega_engine_t* eng, omega_slot_id_t slot, omega_cue_mode_t mode)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    omega::CueMode cpp_mode =
        (mode == OMEGA_CUE_IMMEDIATE) ? omega::CueMode::IMMEDIATE : omega::CueMode::NEXT_BEAT;
    return eng->engine.perf_stop(slot, cpp_mode);
}

omega_status_t omega_perf_stop_all(omega_engine_t* eng, omega_cue_mode_t mode)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    omega::CueMode cpp_mode =
        (mode == OMEGA_CUE_IMMEDIATE) ? omega::CueMode::IMMEDIATE : omega::CueMode::NEXT_BEAT;
    return eng->engine.perf_stop_all(cpp_mode);
}

omega_status_t omega_perf_set_transpose(omega_engine_t* eng, omega_slot_id_t slot, int8_t semitones)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.perf_set_transpose(slot, semitones);
}

omega_status_t omega_perf_set_velocity_scale(omega_engine_t* eng,
                                             omega_slot_id_t slot,
                                             uint8_t scale)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.perf_set_velocity_scale(slot, scale);
}

omega_status_t omega_perf_set_random_bias(omega_engine_t* eng, omega_slot_id_t slot, uint8_t bias)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.perf_set_random_bias(slot, bias);
}

omega_input_t* omega_input_create(const omega_input_desc_t* desc)
{
    if (desc == nullptr || desc->poll_fn == nullptr)
    {
        return nullptr;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto* p = new (std::nothrow) CEventInput(desc->poll_fn, desc->userdata);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<omega_input_t*>(p);
}

void omega_input_destroy(omega_input_t* input)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-pro-type-reinterpret-cast)
    delete reinterpret_cast<CEventInput*>(input);
}

omega_status_t omega_engine_add_input(omega_engine_t* eng, omega_input_t* input)
{
    if (eng == nullptr || input == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return eng->engine.add_input(reinterpret_cast<omega::EventInput*>(input));
}

omega_status_t omega_engine_remove_input(omega_engine_t* eng, omega_input_t* input)
{
    if (eng == nullptr || input == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return eng->engine.remove_input(reinterpret_cast<omega::EventInput*>(input));
}

uint32_t omega_input_overflow_count(const omega_engine_t* eng)
{
    if (eng == nullptr)
    {
        return 0;
    }
    return eng->engine.input_overflow_count();
}

void omega_deliver(omega_input_dispatcher_t* dispatcher, const omega_event_t* ev)
{
    if (dispatcher == nullptr || ev == nullptr)
    {
        return;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    reinterpret_cast<omega::InputDispatcher*>(dispatcher)
        ->deliver(
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            *reinterpret_cast<const omega::Event*>(ev));
}

// ── Modulation bus ────────────────────────────────────────────────────────────

omega_mod_channel_t omega_mod_register(omega_engine_t* eng, const char* name, float initial)
{
    if (eng == nullptr)
    {
        return OMEGA_MOD_INVALID;
    }
    return eng->engine.modulation_bus().register_channel(name, initial);
}

omega_mod_channel_t omega_mod_find(omega_engine_t* eng, const char* name)
{
    if (eng == nullptr)
    {
        return OMEGA_MOD_INVALID;
    }
    return eng->engine.modulation_bus().find(name);
}

float omega_mod_get(omega_engine_t* eng, omega_mod_channel_t channel)
{
    if (eng == nullptr)
    {
        return 0.0f;
    }
    return eng->engine.modulation_bus().get(channel);
}

omega_status_t omega_mod_set(omega_engine_t* eng, omega_mod_channel_t channel, float value)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    eng->engine.modulation_bus().set(channel, value);
    return OMEGA_OK;
}

omega_status_t omega_mod_snapshot(omega_engine_t* eng, float* out, uint32_t count)
{
    if (eng == nullptr || out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    eng->engine.modulation_bus().snapshot(out, count);
    return OMEGA_OK;
}

// ── Performance context ───────────────────────────────────────────────────────

omega_status_t omega_ctx_set_scale(omega_engine_t* eng, const omega_scale_t* scale)
{
    if (eng == nullptr || scale == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.ctx_set_scale(*scale);
}

omega_status_t omega_ctx_set_chord(omega_engine_t* eng, const omega_chord_t* chord)
{
    if (eng == nullptr || chord == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.ctx_set_chord(*chord);
}

omega_status_t omega_ctx_set_transpose(omega_engine_t* eng, int8_t semitones)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.ctx_set_transpose(semitones);
}

omega_status_t omega_ctx_set_velocity(omega_engine_t* eng, uint8_t velocity)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.ctx_set_velocity(velocity);
}

omega_status_t omega_ctx_set_chaos(omega_engine_t* eng, uint8_t chaos)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.ctx_set_chaos(chaos);
}

omega_status_t omega_ctx_set_groove(omega_engine_t* eng, uint8_t groove_id, float swing)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.ctx_set_groove(groove_id, swing);
}

omega_status_t omega_ctx_get(omega_engine_t* eng, omega_perf_ctx_t* ctx)
{
    if (eng == nullptr || ctx == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    eng->engine.ctx_get(*ctx);
    return OMEGA_OK;
}

// ── Custom event sources ──────────────────────────────────────────────────────

omega_source_t* omega_source_create(const omega_source_desc_t* desc)
{
    if (desc == nullptr || desc->advance_fn == nullptr)
    {
        return nullptr;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto* p = new (std::nothrow) CEventSource(desc->advance_fn, desc->userdata, desc->priority);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<omega_source_t*>(p);
}

void omega_source_destroy(omega_source_t* source)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-pro-type-reinterpret-cast)
    delete reinterpret_cast<CEventSource*>(source);
}

omega_status_t omega_engine_add_source(omega_engine_t* eng, omega_source_t* source)
{
    if (eng == nullptr || source == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto* csrc = reinterpret_cast<CEventSource*>(source);
    return eng->engine.add_source(csrc, csrc->priority());
}

omega_status_t omega_engine_remove_source(omega_engine_t* eng, omega_source_t* source)
{
    if (eng == nullptr || source == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return eng->engine.remove_source(reinterpret_cast<omega::EventSource*>(source));
}

void omega_dispatch(omega_dispatcher_t* dispatcher, const omega_event_t* ev)
{
    if (dispatcher == nullptr || ev == nullptr)
    {
        return;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    reinterpret_cast<omega::EventDispatcher*>(dispatcher)
        ->dispatch(
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            *reinterpret_cast<const omega::Event*>(ev));
}

uint32_t omega_ctx_input_count(const omega_process_context_t* ctx)
{
    if (ctx == nullptr)
    {
        return 0;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* pctx = reinterpret_cast<const omega::ProcessContext*>(ctx);
    return pctx->input_bus != nullptr ? pctx->input_bus->count() : 0u;
}

const omega_event_t* omega_ctx_input_at(const omega_process_context_t* ctx, uint32_t i)
{
    if (ctx == nullptr)
    {
        return nullptr;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* pctx = reinterpret_cast<const omega::ProcessContext*>(ctx);
    if (pctx->input_bus == nullptr || i >= pctx->input_bus->count())
    {
        return nullptr;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<const omega_event_t*>(&pctx->input_bus->at(i));
}

float omega_ctx_mod_get_ctx(const omega_process_context_t* ctx, omega_mod_channel_t channel)
{
    if (ctx == nullptr)
    {
        return 0.0f;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* pctx = reinterpret_cast<const omega::ProcessContext*>(ctx);
    return pctx->modulation_bus != nullptr ? pctx->modulation_bus->get(channel) : 0.0f;
}

void omega_ctx_mod_set_ctx(omega_process_context_t* ctx, omega_mod_channel_t channel, float value)
{
    if (ctx == nullptr)
    {
        return;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto* pctx = reinterpret_cast<omega::ProcessContext*>(ctx);
    if (pctx->modulation_bus != nullptr)
    {
        pctx->modulation_bus->set(channel, value);
    }
}

}  // extern "C"
