#include <omega/anchor_point.h>
#include <omega/commands.h>
#include <omega/engine.h>
#include <omega/event_anchor_table.h>
#include <omega/event_input.h>
#include <omega/event_source.h>
#include <omega/midi_io.h>
#include <omega/omega.h>
#include <omega/perf_slot.h>
#include <omega/region_list.h>
#include <omega/sink.h>
#include <omega/smf.h>
#include <omega/smpte_converter.h>
#include <omega/snap.h>
#include <omega/tempo_map.h>
#include <omega/time_signature_map.h>
#include <omega/timer.h>
#include <omega/types.h>

#include <algorithm>
#include <new>
#include <string>

namespace
{

omega::CueMode to_cpp_cue_mode(omega_cue_mode_t m) noexcept
{
    switch (m)
    {
        case OMEGA_CUE_IMMEDIATE:
            return omega::CueMode::IMMEDIATE;
        case OMEGA_CUE_BAR:
            return omega::CueMode::NEXT_BAR;
        default:
            return omega::CueMode::NEXT_BEAT;
    }
}

}  // namespace

// omega_engine_s is the heap-allocated owner of the C++ Engine.
// omega_engine_t* (opaque to C callers) points to one of these.
struct omega_engine_s  // NOLINT(readability-identifier-naming)
{
    omega::Engine engine;
    omega_engine_s() {}  // NOLINT(modernize-use-equals-default) — prevents aggregate init
};

// omega_timer_s owns the OmegaTimer.
struct omega_timer_s  // NOLINT(readability-identifier-naming)
{
    omega::OmegaTimer timer;
    explicit omega_timer_s(omega::Engine& e, uint32_t us) : timer(e, us != 0u ? us : 1000u) {}
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
        auto* d = reinterpret_cast<omega_dispatcher_t*>(&dispatcher);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        auto* c = reinterpret_cast<omega_process_context_t*>(&ctx);
        advance_fn_(to_tick, d, c, userdata_);
    }

    [[nodiscard]] uint32_t priority() const noexcept { return priority_; }

private:
    omega_source_advance_fn_t advance_fn_;
    void* userdata_;
    uint32_t priority_;
};

// Owns a LibremidiInput and exposes it as an omega::EventInput (for the C API).
// Returned by omega_input_create_midi_in(); destroyed by omega_input_destroy_midi_in().
class MidiInputHolder : public omega::EventInput
{
public:
    explicit MidiInputHolder(const char* port_name) noexcept : input_{port_name} {}
    void poll(omega::InputDispatcher& dispatcher) override { input_.poll(dispatcher); }

private:
    omega::LibremidiInput input_;
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

omega_status_t omega_engine_set_event_callback(omega_engine_t* eng,
                                               omega_event_callback_t cb,
                                               void* userdata)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    eng->engine.set_event_callback(cb, userdata);
    return OMEGA_OK;
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

uint32_t omega_sink_id(const omega_sink_t* sink)
{
    if (sink == nullptr)
    {
        return 0u;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<const omega::OutputSink*>(sink)->sink_id();
}

omega_status_t omega_sink_set_mute(omega_engine_t* eng,
                                   uint32_t sink_id,
                                   uint8_t channel,
                                   int muted)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.sink_set_mute(sink_id, channel, muted != 0);
}

omega_status_t omega_sink_set_solo(omega_engine_t* eng,
                                   uint32_t sink_id,
                                   uint8_t channel,
                                   int soloed)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.sink_set_solo(sink_id, channel, soloed != 0);
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

omega_status_t omega_engine_undo(omega_engine_t* eng)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.undo();
}

omega_status_t omega_engine_redo(omega_engine_t* eng)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.redo();
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

omega_tick_t omega_engine_position_tick(const omega_engine_t* eng)
{
    if (eng == nullptr)
    {
        return 0u;
    }
    return eng->engine.transport_position_tick();
}

omega_status_t omega_engine_position(const omega_engine_t* e, omega_position_t* out)
{
    if (e == nullptr || out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    *out = e->engine.position();
    return OMEGA_OK;
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

omega_status_t omega_pattern_replace_event(omega_engine_t* eng,
                                           omega_pattern_id_t pat,
                                           uint32_t event_index,
                                           const omega_event_t* replacement)
{
    if (eng == nullptr || replacement == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.pattern_replace_event(pat, event_index, *replacement);
}

// ── Pattern read API ──────────────────────────────────────────────────────────

omega_status_t omega_pattern_event_count(const omega_engine_t* eng,
                                         omega_pattern_id_t pat,
                                         uint32_t* count_out)
{
    if (eng == nullptr || count_out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    const omega::Pattern* p = eng->engine.pattern_library().get(pat);
    if (p == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    *count_out = static_cast<uint32_t>(p->events.size());
    return OMEGA_OK;
}

omega_status_t omega_pattern_event_at(const omega_engine_t* eng,
                                      omega_pattern_id_t pat,
                                      uint32_t idx,
                                      omega_event_t* event_out)
{
    if (eng == nullptr || event_out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    const omega::Pattern* p = eng->engine.pattern_library().get(pat);
    if (p == nullptr || idx >= static_cast<uint32_t>(p->events.size()))
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    *event_out = p->events[idx];
    return OMEGA_OK;
}

omega_status_t omega_pattern_event_count_filtered(const omega_engine_t* eng,
                                                  omega_pattern_id_t pat,
                                                  uint8_t channel,
                                                  uint8_t payload_tag,
                                                  uint32_t* count_out)
{
    if (eng == nullptr || count_out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    const omega::Pattern* p = eng->engine.pattern_library().get(pat);
    if (p == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    uint32_t n = 0;
    for (const auto& ev : p->events)
    {
        bool ch_match = (channel == 0xFFu) || (ev.channel == channel);
        bool tag_match = (payload_tag == 0xFFu) || (ev.payload_tag == payload_tag);
        if (ch_match && tag_match)
        {
            ++n;
        }
    }
    *count_out = n;
    return OMEGA_OK;
}

omega_status_t omega_pattern_length(const omega_engine_t* eng,
                                    omega_pattern_id_t pat,
                                    omega_tick_t* length_out)
{
    if (eng == nullptr || length_out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    const omega::Pattern* p = eng->engine.pattern_library().get(pat);
    if (p == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    *length_out = p->length_ticks;
    return OMEGA_OK;
}

omega_status_t omega_pattern_for_each_event(const omega_engine_t* eng,
                                            omega_pattern_id_t pat,
                                            uint8_t channel_filter,
                                            uint8_t tag_filter,
                                            void (*cb)(uint32_t, const omega_event_t*, void*),
                                            void* userdata)
{
    if (eng == nullptr || cb == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    const omega::Pattern* p = eng->engine.pattern_library().get(pat);
    if (p == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>(p->events.size()); ++i)
    {
        const auto& ev = p->events[i];
        bool ch_match = (channel_filter == 0xFFu) || (ev.channel == channel_filter);
        bool tag_match = (tag_filter == 0xFFu) || (ev.payload_tag == tag_filter);
        if (ch_match && tag_match)
        {
            cb(i, &ev, userdata);
        }
    }
    return OMEGA_OK;
}

uint32_t omega_pattern_library_count(const omega_engine_t* eng)
{
    if (eng == nullptr)
    {
        return 0u;
    }
    return eng->engine.pattern_library().count();
}

omega_status_t omega_pattern_for_each(const omega_engine_t* eng,
                                      void (*cb)(omega_pattern_id_t, void*),
                                      void* userdata)
{
    if (eng == nullptr || cb == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    eng->engine.pattern_library().for_each(
        [cb, userdata](omega::PatternId id, const omega::Pattern& /*pat*/) { cb(id, userdata); });
    return OMEGA_OK;
}

omega_status_t omega_convert_tracks_to_patterns(omega_engine_t* eng,
                                                uint32_t sink_id,
                                                omega_tick_t loop_end_ticks,
                                                uint32_t* count_out)
{
    if (eng == nullptr || count_out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    *count_out = eng->engine.convert_tracks_to_patterns(sink_id, loop_end_ticks);
    return OMEGA_OK;
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
    omega::CueMode cpp_mode = to_cpp_cue_mode(mode);
    return eng->engine.perf_cue(slot, cpp_mode);
}

omega_status_t omega_perf_stop(omega_engine_t* eng, omega_slot_id_t slot, omega_cue_mode_t mode)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    omega::CueMode cpp_mode = to_cpp_cue_mode(mode);
    return eng->engine.perf_stop(slot, cpp_mode);
}

omega_status_t omega_perf_stop_all(omega_engine_t* eng, omega_cue_mode_t mode)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    omega::CueMode cpp_mode = to_cpp_cue_mode(mode);
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

omega_status_t omega_perf_set_repeat_count(omega_engine_t* eng,
                                           omega_slot_id_t slot,
                                           uint32_t count)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.perf_set_repeat_count(slot, count);
}

omega_status_t omega_perf_set_mute(omega_engine_t* eng, omega_slot_id_t slot, int muted)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.perf_set_mute(slot, muted != 0);
}

// ── Query boundary ──────────────────────────────────────────────────────────

omega_slot_state_t omega_perf_slot_state(const omega_engine_t* eng, omega_slot_id_t slot)
{
    if (eng == nullptr)
    {
        return OMEGA_SLOT_EMPTY;
    }
    auto s = eng->engine.perf_slot_state(slot);
    return static_cast<omega_slot_state_t>(s);
}

int omega_sink_is_muted(const omega_engine_t* eng, uint32_t sink_id, uint8_t channel)
{
    if (eng == nullptr)
    {
        return 0;
    }
    return eng->engine.sink_is_muted(sink_id, channel) ? 1 : 0;
}

int omega_sink_is_soloed(const omega_engine_t* eng, uint32_t sink_id, uint8_t channel)
{
    if (eng == nullptr)
    {
        return 0;
    }
    return eng->engine.sink_is_soloed(sink_id, channel) ? 1 : 0;
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

// ── Tempo map ─────────────────────────────────────────────────────────────────

omega_status_t omega_tempo_set(omega_engine_t* eng, omega_tick_t tick, uint32_t bpm_milli)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.tempo_set(tick, bpm_milli);
}

omega_status_t omega_tempo_remove(omega_engine_t* eng, omega_tick_t tick)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.tempo_remove(tick);
}

omega_status_t omega_tempo_at(const omega_engine_t* eng, omega_tick_t tick, uint32_t* bpm_milli_out)
{
    if (eng == nullptr || bpm_milli_out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    const auto& pts = eng->engine.tempo_map().points();
    // Find the last point whose tick <= query tick (upper_bound trick).
    auto it = std::upper_bound(
        pts.begin(), pts.end(), tick, [](omega_tick_t t, const omega::TempoMap::TempoPoint& p) {
            return t < p.tick;
        });
    if (it != pts.begin())
    {
        --it;
    }
    *bpm_milli_out = it->bpm_milli;
    return OMEGA_OK;
}

// ── Time signature map ────────────────────────────────────────────────────────

omega_status_t omega_timesig_set(omega_engine_t* eng,
                                 uint64_t tick,
                                 uint8_t numerator,
                                 uint8_t denominator)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.timesig_set(tick, numerator, denominator);
}

omega_status_t omega_timesig_remove(omega_engine_t* eng, uint64_t tick)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.timesig_remove(tick);
}

omega_status_t omega_timesig_clear(omega_engine_t* eng)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.timesig_clear();
}

int omega_timesig_is_freeform(const omega_engine_t* eng)
{
    if (eng == nullptr)
    {
        return -1;
    }
    return eng->engine.timesig_map().is_freeform() ? 1 : 0;
}

omega_status_t omega_timesig_at(const omega_engine_t* eng,
                                uint64_t tick,
                                omega_time_sig_point_t* out)
{
    if (eng == nullptr || out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    const omega::TimeSigPoint* pt = eng->engine.timesig_map().at(tick);
    if (pt == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    out->tick = pt->tick;
    out->numerator = pt->numerator;
    out->denominator = pt->denominator;
    return OMEGA_OK;
}

omega_status_t omega_tick_to_beat_pos(const omega_engine_t* eng,
                                      uint64_t tick,
                                      omega_beat_pos_t* out)
{
    if (eng == nullptr || out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    omega::MeterCursor cursor(eng->engine.timesig_map());
    omega::BeatPosition pos{};
    omega_status_t s = cursor.tick_to_beat_pos(tick, pos);
    if (s == OMEGA_OK)
    {
        out->bar = pos.bar;
        out->beat = pos.beat;
        out->subdivision = pos.subdivision;
    }
    return s;
}

omega_status_t omega_beat_pos_to_tick(const omega_engine_t* eng,
                                      const omega_beat_pos_t* in,
                                      uint64_t* out)
{
    if (eng == nullptr || in == nullptr || out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    omega::MeterCursor cursor(eng->engine.timesig_map());
    omega::BeatPosition pos{};
    pos.bar = in->bar;
    pos.beat = in->beat;
    pos.subdivision = in->subdivision;
    return cursor.beat_pos_to_tick(pos, *out);
}

omega_status_t omega_next_bar_tick(const omega_engine_t* eng, uint64_t from_tick, uint64_t* out)
{
    if (eng == nullptr || out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    omega::MeterCursor cursor(eng->engine.timesig_map());
    return cursor.next_bar_tick(from_tick, *out);
}

omega_status_t omega_quantize_to_beat(const omega_engine_t* eng, uint64_t tick, uint64_t* out)
{
    if (eng == nullptr || out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    omega::MeterCursor cursor(eng->engine.timesig_map());
    return cursor.quantize_to_beat(tick, *out);
}

// ── SMPTE config ──────────────────────────────────────────────────────────────

omega_status_t omega_smpte_config_set(omega_engine_t* eng, const omega_smpte_config_t* config)
{
    if (eng == nullptr || config == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    omega::SmpteConfig cpp_config;
    cpp_config.fps = config->fps;
    cpp_config.drop_frame = (config->drop_frame != 0u);
    cpp_config.is_2997 = (config->is_2997 != 0u);
    return eng->engine.smpte_config_set(cpp_config);
}

omega_status_t omega_smpte_config_clear(omega_engine_t* eng)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.smpte_config_clear();
}

omega_status_t omega_tick_to_smpte(const omega_engine_t* eng,
                                   uint64_t tick,
                                   omega_smpte_time_t* out)
{
    if (eng == nullptr || out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    const auto& cfg_opt = eng->engine.smpte_config();
    if (!cfg_opt.has_value())
    {
        return OMEGA_ERR_NO_SMPTE_CONFIG;
    }
    omega::SmpteConverter converter(*cfg_opt, eng->engine.tempo_map());
    omega::SmpteTime t{};
    omega_status_t s = converter.tick_to_smpte(tick, t);
    if (s == OMEGA_OK)
    {
        out->hours = t.hours;
        out->minutes = t.minutes;
        out->seconds = t.seconds;
        out->frames = t.frames;
    }
    return s;
}

omega_status_t omega_smpte_to_tick(const omega_engine_t* eng,
                                   const omega_smpte_time_t* t,
                                   uint64_t* out)
{
    if (eng == nullptr || t == nullptr || out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    const auto& cfg_opt = eng->engine.smpte_config();
    if (!cfg_opt.has_value())
    {
        return OMEGA_ERR_NO_SMPTE_CONFIG;
    }
    omega::SmpteConverter converter(*cfg_opt, eng->engine.tempo_map());
    omega::SmpteTime smpte{};
    smpte.hours = t->hours;
    smpte.minutes = t->minutes;
    smpte.seconds = t->seconds;
    smpte.frames = t->frames;
    return converter.smpte_to_tick(smpte, *out);
}

// ── MIDI I/O ──────────────────────────────────────────────────────────────────

omega_sink_t* omega_sink_create_midi_out(const char* port_name)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto* p = new (std::nothrow) omega::LibremidiSink{port_name};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<omega_sink_t*>(p);
}

void omega_sink_destroy_midi_out(omega_sink_t* sink)
{
    // omega_sink_t* aliases omega::OutputSink*; LibremidiSink has virtual dtor.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-pro-type-reinterpret-cast)
    delete reinterpret_cast<omega::LibremidiSink*>(sink);
}

omega_input_t* omega_input_create_midi_in(const char* port_name)
{
    // MidiInputHolder owns a LibremidiInput and implements EventInput::poll().
    // omega_engine_add_input() casts omega_input_t* → omega::EventInput* (by layout).
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto* holder = new (std::nothrow) MidiInputHolder{port_name};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<omega_input_t*>(holder);
}

void omega_input_destroy_midi_in(omega_input_t* input)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-pro-type-reinterpret-cast)
    delete reinterpret_cast<omega::EventInput*>(input);
}

// ── SMF import / export ───────────────────────────────────────────────────────

omega_status_t omega_smf_import(omega_engine_t* eng, const char* path)
{
    if (eng == nullptr || path == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return omega::smf_import(eng->engine, path);
}

omega_status_t omega_smf_export(omega_engine_t* eng, const char* path, int smf_type)
{
    if (eng == nullptr || path == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return omega::smf_export(eng->engine, path, smf_type);
}

// ── Markers ───────────────────────────────────────────────────────────────────

omega_status_t omega_marker_add(omega_engine_t* eng, const char* name, omega_tick_t tick)
{
    if (eng == nullptr || name == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    eng->engine.marker_list().add(name, tick);
    return OMEGA_OK;
}

omega_status_t omega_marker_remove(omega_engine_t* eng, uint32_t index)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.marker_list().remove(index);
}

uint32_t omega_marker_count(const omega_engine_t* eng)
{
    if (eng == nullptr)
    {
        return 0u;
    }
    return eng->engine.marker_list().size();
}

omega_status_t omega_marker_at(const omega_engine_t* eng, uint32_t index, omega_marker_t* out)
{
    if (eng == nullptr || out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    const omega::Marker* m = eng->engine.marker_list().at(index);
    if (m == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    out->name = m->name.c_str();
    out->tick = m->tick;
    return OMEGA_OK;
}

omega_status_t omega_marker_clear(omega_engine_t* eng)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    eng->engine.marker_list().clear();
    return OMEGA_OK;
}

// ── Regions ───────────────────────────────────────────────────────────────────

omega_status_t omega_region_add(
    omega_engine_t* eng, const char* name, omega_tick_t start, omega_tick_t end, uint8_t type)
{
    if (eng == nullptr || name == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    auto region_type = static_cast<omega::RegionType>(type);
    return eng->engine.region_list().add(name, start, end, region_type);
}

omega_status_t omega_region_remove(omega_engine_t* eng, uint32_t index)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.region_list().remove(index);
}

uint32_t omega_region_count(const omega_engine_t* eng)
{
    if (eng == nullptr)
    {
        return 0u;
    }
    return eng->engine.region_list().size();
}

omega_status_t omega_region_at(const omega_engine_t* eng, uint32_t index, omega_region_t* out)
{
    if (eng == nullptr || out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    const omega::Region* r = eng->engine.region_list().at(index);
    if (r == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    out->name = r->name.c_str();
    out->start_tick = r->start_tick;
    out->end_tick = r->end_tick;
    out->type = static_cast<uint8_t>(r->type);
    return OMEGA_OK;
}

omega_status_t omega_region_clear(omega_engine_t* eng)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    eng->engine.region_list().clear();
    return OMEGA_OK;
}

// ── Anchors ───────────────────────────────────────────────────────────────────

omega_status_t omega_pattern_add_anchor(omega_engine_t* eng,
                                        omega_pattern_id_t pid,
                                        const char* name,
                                        omega_tick_t offset,
                                        uint32_t flags)
{
    if (eng == nullptr || name == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    omega::Pattern* pat = eng->engine.pattern_library().get(pid);
    if (pat == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    pat->anchors.add(name, offset, flags);
    return OMEGA_OK;
}

omega_status_t omega_pattern_remove_anchor(omega_engine_t* eng,
                                           omega_pattern_id_t pid,
                                           const char* name)
{
    if (eng == nullptr || name == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    omega::Pattern* pat = eng->engine.pattern_library().get(pid);
    if (pat == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    return pat->anchors.remove(std::string(name));
}

uint32_t omega_pattern_anchor_count(const omega_engine_t* eng, omega_pattern_id_t pid)
{
    if (eng == nullptr)
    {
        return 0u;
    }
    const omega::Pattern* pat = eng->engine.pattern_library().get(pid);
    if (pat == nullptr)
    {
        return 0u;
    }
    return pat->anchors.size();
}

omega_status_t omega_pattern_set_active_snap(omega_engine_t* eng,
                                             omega_pattern_id_t pid,
                                             uint32_t index)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    omega::Pattern* pat = eng->engine.pattern_library().get(pid);
    if (pat == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    return pat->anchors.set_active_snap(index);
}

omega_status_t omega_event_add_anchor(omega_engine_t* eng,
                                      omega_track_id_t track,
                                      uint32_t event_index,
                                      const char* name,
                                      omega_tick_t offset,
                                      uint32_t flags)
{
    if (eng == nullptr || name == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    eng->engine.event_anchors().get_or_create(track, event_index).add(name, offset, flags);
    return OMEGA_OK;
}

omega_status_t omega_event_remove_anchor(omega_engine_t* eng,
                                         omega_track_id_t track,
                                         uint32_t event_index,
                                         const char* name)
{
    if (eng == nullptr || name == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    omega::AnchorList* al = eng->engine.event_anchors().get(track, event_index);
    if (al == nullptr)
    {
        return OMEGA_ERR_NOT_FOUND;
    }
    return al->remove(std::string(name));
}

omega_timer_t* omega_timer_create(omega_engine_t* eng, uint32_t interval_us)
{
    if (eng == nullptr)
    {
        return nullptr;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return new (std::nothrow) omega_timer_s(eng->engine, interval_us);
}

void omega_timer_destroy(omega_timer_t* timer)
{
    delete timer;  // NOLINT(cppcoreguidelines-owning-memory)
}

omega_status_t omega_snap(const omega_engine_t* eng,
                          omega_tick_t tick,
                          const omega_snap_config_t* config,
                          omega_snap_result_t* out)
{
    if (eng == nullptr || config == nullptr || out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    // GRID with freeform engine and no explicit subdivision is not supported.
    if ((config->targets & OMEGA_SNAP_GRID) != 0u && config->grid_subdiv_ticks == 0u &&
        eng->engine.timesig_map().is_freeform())
    {
        return OMEGA_ERR_NO_METER;
    }

    omega::MeterCursor cursor(eng->engine.timesig_map());
    omega::SnapConfig cpp_config{
        config->targets, config->grid_subdiv_ticks, config->tolerance_ticks};

    omega::SnapResult result = omega::snap_to_nearest(
        tick, cpp_config, cursor, eng->engine.marker_list(), eng->engine.region_list(), nullptr);

    out->snapped_tick = result.snapped_tick;
    out->source = static_cast<uint8_t>(result.source);
    out->did_snap = result.did_snap ? 1 : 0;
    return OMEGA_OK;
}

omega_status_t omega_loop_set(omega_engine_t* eng, omega_tick_t start, omega_tick_t end)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.loop_set(start, end);
}

omega_status_t omega_loop_set_immediate(omega_engine_t* eng, omega_tick_t start, omega_tick_t end)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.loop_set_immediate(start, end);
}

omega_status_t omega_loop_clear(omega_engine_t* eng)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.loop_clear();
}

omega_status_t omega_loop_enable(omega_engine_t* eng, int enabled)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.loop_enable(enabled != 0);
}

omega_status_t omega_loop_activate_region(omega_engine_t* eng, uint32_t region_index)
{
    if (eng == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return eng->engine.loop_activate_region(region_index);
}

// ── Utilities ─────────────────────────────────────────────────────────────────

namespace
{

// Writes the decimal representation of n into buf[pos..out_size-1], advancing
// *pos past the digits written.  Stops early if the buffer is full.  Does NOT
// write a NUL — the caller is responsible for termination.
void write_uint(char* buf, size_t out_size, size_t& pos, uint32_t n)
{
    // Compute digits in reverse, then copy forward.
    char tmp[11];  // max 10 digits for uint32_t + sentinel
    size_t ndig = 0;
    do
    {
        tmp[ndig++] = static_cast<char>('0' + static_cast<int>(n % 10u));
        n /= 10u;
    } while (n != 0u);

    // tmp holds digits in reverse order.
    for (size_t i = ndig; i > 0u && pos + 1u < out_size; --i)
    {
        buf[pos++] = tmp[i - 1u];
    }
}

// Appends a single char if space remains (leaving room for the NUL).
void write_char(char* buf, size_t out_size, size_t& pos, char c)
{
    if (pos + 1u < out_size)
    {
        buf[pos++] = c;
    }
}

}  // namespace

void omega_midi_note_name(uint8_t pitch, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0)
    {
        return;
    }

    static constexpr const char* k_names[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    // Clamp to valid MIDI range.
    if (pitch > 127u)
    {
        pitch = 127u;
    }

    const int octave = static_cast<int>(pitch / 12u) - 1;  // MIDI: C4 = 60
    const char* name = k_names[pitch % 12u];

    size_t pos = 0;
    // Copy note name (1 or 2 chars).
    for (size_t i = 0; name[i] != '\0'; ++i)
    {
        write_char(out, out_size, pos, name[i]);
    }

    // Append octave: range is -1 (pitch 0) to 9 (pitch 127).
    if (octave < 0)
    {
        write_char(out, out_size, pos, '-');
        write_char(out, out_size, pos, static_cast<char>('0' + (-octave)));
    }
    else
    {
        write_char(out, out_size, pos, static_cast<char>('0' + octave));
    }

    out[pos] = '\0';
}

omega_status_t omega_midi_note_from_name(const char* name, uint8_t* out)
{
    if (name == nullptr || out == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }

    const char* p = name;

    // Natural semitone for A B C D E F G.
    static constexpr int k_semitones[7] = {9, 11, 0, 2, 4, 5, 7};

    char letter = *p++;
    if (letter >= 'a' && letter <= 'z')
    {
        letter = static_cast<char>(letter - 32);
    }
    if (letter < 'A' || letter > 'G')
    {
        return OMEGA_ERR_INVALID;
    }
    int semitone = k_semitones[letter - 'A'];

    // Optional accidental.
    if (*p == '#')
    {
        semitone += 1;
        ++p;
    }
    else if (*p == 'b')
    {
        semitone -= 1;
        ++p;
    }

    // Octave: optional leading '-', then a single digit.
    int sign = 1;
    if (*p == '-')
    {
        sign = -1;
        ++p;
    }

    if (*p < '0' || *p > '9')
    {
        return OMEGA_ERR_INVALID;
    }
    const int octave = sign * (*p++ - '0');

    if (*p != '\0')
    {
        return OMEGA_ERR_INVALID;
    }

    const int pitch = (octave + 1) * 12 + semitone;
    if (pitch < 0 || pitch > 127)
    {
        return OMEGA_ERR_INVALID;
    }

    *out = static_cast<uint8_t>(pitch);
    return OMEGA_OK;
}

omega_status_t omega_format_position(const omega_engine_t* e,
                                     omega_tick_t tick,
                                     char* out,
                                     size_t out_size)
{
    if (e == nullptr || out == nullptr || out_size == 0)
    {
        return OMEGA_ERR_INVALID;
    }

    omega::MeterCursor cursor(e->engine.timesig_map());
    omega::BeatPosition pos{};
    const omega_status_t st = cursor.tick_to_beat_pos(tick, pos);
    if (st != OMEGA_OK)
    {
        return st;
    }

    // Write "bar:beat.subdivision" without snprintf (avoid vararg).
    size_t p = 0;
    write_uint(out, out_size, p, pos.bar);
    write_char(out, out_size, p, ':');
    write_uint(out, out_size, p, static_cast<uint32_t>(pos.beat));
    write_char(out, out_size, p, '.');
    write_uint(out, out_size, p, pos.subdivision);
    out[p] = '\0';

    return OMEGA_OK;
}

}  // extern "C"
