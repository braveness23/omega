#include <omega/engine.h>
#include <omega/smpte_converter.h>
#include <omega/time_signature_map.h>

#include <algorithm>
#include <type_traits>

namespace omega
{

Engine::Engine(ClockSource* clock, std::pmr::memory_resource* /*mr*/, uint32_t /*queue_capacity*/)
    : clock_{clock != nullptr ? clock : &internal_clock_}
{}

Engine::~Engine() = default;

void Engine::set_clock(ClockSource* clock) noexcept
{
    clock_ = clock != nullptr ? clock : &internal_clock_;
}

omega_status_t Engine::add_sink(OutputSink* sink)
{
    if (sink == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }

    uint32_t sid = sink->sink_id();
    auto it = std::lower_bound(
        sinks_.begin(),
        sinks_.end(),
        sid,
        [](const std::pair<uint32_t, OutputSink*>& p, uint32_t v) { return p.first < v; });
    sinks_.insert(it, {sid, sink});
    return OMEGA_OK;
}

PatternId Engine::create_pattern(std::string name, uint64_t length_ticks)
{
    return patterns_.create(std::move(name), length_ticks);
}

void Engine::destroy_pattern(PatternId id)
{
    patterns_.destroy(id);
}

omega_status_t Engine::pattern_add_event(PatternId id, Event event)
{
    return patterns_.add_event(id, event);
}

omega_status_t Engine::pattern_set_length(PatternId id, uint64_t length_ticks)
{
    return patterns_.set_length(id, length_ticks);
}

omega_status_t Engine::pattern_replace_event(PatternId id,
                                             uint32_t event_index,
                                             const Event& replacement)
{
    return enqueue(ReplaceEventCmd{id, event_index, replacement});
}

omega_status_t Engine::add_track_event(TrackId track_id, const Event& event)
{
    return timeline_.add_event(track_id, event);
}

PatternLibrary& Engine::pattern_library() noexcept
{
    return patterns_;
}

const PatternLibrary& Engine::pattern_library() const noexcept
{
    return patterns_;
}

TrackId Engine::add_track(std::string name)
{
    return timeline_.add_track(std::move(name));
}

omega_status_t Engine::set_track_sink(TrackId track_id, uint32_t sink_id)
{
    return timeline_.set_sink(track_id, sink_id);
}

omega_status_t Engine::song_append(PatternId id, uint32_t repeat_count)
{
    return enqueue(SongAppendCmd{id, repeat_count});
}

omega_status_t Engine::song_clear()
{
    return enqueue(SongClearCmd{});
}

omega_status_t Engine::perf_assign(SlotId slot, PatternId pattern)
{
    return enqueue(PerfAssignCmd{slot, pattern});
}

omega_status_t Engine::perf_cue(SlotId slot, CueMode mode)
{
    return enqueue(PerfCueCmd{slot, mode});
}

omega_status_t Engine::perf_stop(SlotId slot, CueMode mode)
{
    return enqueue(PerfStopCmd{slot, mode});
}

omega_status_t Engine::perf_stop_all(CueMode mode)
{
    return enqueue(PerfStopAllCmd{mode});
}

omega_status_t Engine::perf_set_transpose(SlotId slot, int8_t semitones)
{
    return enqueue(PerfSetTransposeCmd{slot, semitones});
}

omega_status_t Engine::perf_set_velocity_scale(SlotId slot, uint8_t scale)
{
    return enqueue(PerfSetVelocityScaleCmd{slot, scale});
}

omega_status_t Engine::perf_set_random_bias(SlotId slot, uint8_t bias)
{
    return enqueue(PerfSetRandomBiasCmd{slot, bias});
}

omega_status_t Engine::add_input(EventInput* input)
{
    if (input == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return enqueue(AddInputCmd{input});
}

omega_status_t Engine::remove_input(EventInput* input)
{
    if (input == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return enqueue(RemoveInputCmd{input});
}

uint32_t Engine::input_overflow_count() const noexcept
{
    return input_bus_.overflow_count();
}

omega_status_t Engine::ctx_set_scale(const omega_scale_t& scale)
{
    return enqueue(SetCtxScaleCmd{scale});
}

omega_status_t Engine::ctx_set_chord(const omega_chord_t& chord)
{
    return enqueue(SetCtxChordCmd{chord});
}

omega_status_t Engine::ctx_set_transpose(int8_t semitones)
{
    return enqueue(SetCtxTransposeCmd{semitones});
}

omega_status_t Engine::ctx_set_velocity(uint8_t velocity)
{
    return enqueue(SetCtxVelocityCmd{velocity});
}

omega_status_t Engine::ctx_set_chaos(uint8_t chaos)
{
    return enqueue(SetCtxChaosCmd{chaos});
}

omega_status_t Engine::ctx_set_groove(uint8_t groove_id, float swing)
{
    return enqueue(SetCtxGrooveCmd{groove_id, swing});
}

omega_status_t Engine::tempo_set(uint64_t tick, uint32_t bpm_milli)
{
    if (bpm_milli == 0u)
    {
        return OMEGA_ERR_INVALID;
    }
    return enqueue(SetTempoPointCmd{tick, bpm_milli});
}

omega_status_t Engine::tempo_remove(uint64_t tick)
{
    return enqueue(RemoveTempoPointCmd{tick});
}

omega_status_t Engine::timesig_set(uint64_t tick, uint8_t numerator, uint8_t denominator)
{
    if (!is_valid_timesig_denominator(denominator) || numerator == 0u)
    {
        return OMEGA_ERR_INVALID;
    }
    return enqueue(SetTimeSigCmd{tick, numerator, denominator});
}

omega_status_t Engine::timesig_remove(uint64_t tick)
{
    return enqueue(RemoveTimeSigCmd{tick});
}

omega_status_t Engine::timesig_clear()
{
    return enqueue(ClearTimeSigCmd{});
}

omega_status_t Engine::loop_set(uint64_t start_tick, uint64_t end_tick)
{
    if (end_tick <= start_tick)
    {
        return OMEGA_ERR_INVALID;
    }
    return enqueue(SetLoopCmd{start_tick, end_tick, true});
}

omega_status_t Engine::loop_clear()
{
    return enqueue(SetLoopCmd{0, 0, false});
}

omega_status_t Engine::loop_enable(bool enabled)
{
    return enqueue(SetLoopCmd{loop_start_tick_, loop_end_tick_, enabled});
}

omega_status_t Engine::smpte_config_set(const SmpteConfig& config)
{
    if (!is_valid_smpte_config(config))
    {
        return OMEGA_ERR_INVALID;
    }
    return enqueue(SetSmpteConfigCmd{config});
}

omega_status_t Engine::smpte_config_clear()
{
    return enqueue(ClearSmpteConfigCmd{});
}

omega_status_t Engine::add_source(EventSource* source, uint32_t priority)
{
    if (source == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return enqueue(AddSourceCmd{source, priority});
}

omega_status_t Engine::remove_source(EventSource* source)
{
    if (source == nullptr)
    {
        return OMEGA_ERR_INVALID;
    }
    return enqueue(RemoveSourceCmd{source});
}

omega_status_t Engine::enqueue(Command cmd)
{
    if (queue_.push(cmd))
    {
        return OMEGA_OK;
    }
    return OMEGA_ERR_QUEUE_FULL;
}

void Engine::apply(const AddEventCmd& cmd)
{
    timeline_.add_event(cmd.track, cmd.event);
}

void Engine::apply(const DeleteEventCmd& cmd)
{
    timeline_.remove_event(cmd.track, cmd.tick, cmd.index);
}

void Engine::apply(const ReplaceEventCmd& cmd)
{
    Pattern* p = patterns_.get(cmd.pattern_id);
    if (p == nullptr || cmd.event_index >= static_cast<uint32_t>(p->events.size()))
    {
        return;
    }
    p->events[cmd.event_index] = cmd.replacement;
    // Re-sort only if the tick changed (otherwise the order is already valid).
    std::stable_sort(p->events.begin(), p->events.end(), [](const Event& a, const Event& b) {
        return a.tick < b.tick;
    });
}

void Engine::apply(const SetLoopCmd& cmd)
{
    // Reset the loop wrap counter whenever the loop region itself changes
    // (loop_set / loop_clear). Toggling enabled without changing the region
    // does not reset the counter so the display thread sees a monotone count.
    if (cmd.start_tick != loop_start_tick_ || cmd.end_tick != loop_end_tick_)
    {
        loop_count_ = 0;
    }
    loop_start_tick_ = cmd.start_tick;
    loop_end_tick_ = cmd.end_tick;
    loop_enabled_ = cmd.enabled;
}

void Engine::apply(const SetTempoCmd& cmd)
{
    uint64_t pos = last_position_ns_.load(std::memory_order_relaxed);
    uint64_t tick = tempo_map_.ns_to_ticks(pos);
    tempo_map_.insert(tick, cmd.bpm_milli);
}

void Engine::apply(const SetTempoPointCmd& cmd)
{
    tempo_map_.insert(cmd.tick, cmd.bpm_milli);
}

void Engine::apply(const RemoveTempoPointCmd& cmd)
{
    tempo_map_.remove(cmd.tick);
}

void Engine::apply(const TransportCmd& cmd)
{
    switch (cmd.action)
    {
        case TransportAction::PLAY:
        {
            uint64_t pos = last_position_ns_.load(std::memory_order_relaxed);
            session_start_ns_ = clock_->now_ns() - pos;
            state_.store(static_cast<uint8_t>(TransportState::PLAYING), std::memory_order_release);
            break;
        }
        case TransportAction::STOP:
            state_.store(static_cast<uint8_t>(TransportState::STOPPED), std::memory_order_release);
            break;
        case TransportAction::LOCATE:
        {
            uint64_t pos = tempo_map_.ticks_to_ns(cmd.locate_tick);
            last_position_ns_.store(pos, std::memory_order_release);
            if (state_.load(std::memory_order_relaxed) ==
                static_cast<uint8_t>(TransportState::PLAYING))
            {
                session_start_ns_ = clock_->now_ns() - pos;
            }
            ProcessContext ctx{};
            ctx.input_bus = &input_bus_;
            ctx.modulation_bus = &mod_bus_;
            ctx.perf_ctx = perf_ctx_;
            EventDispatcher dispatcher{sinks_};
            timeline_.on_locate(cmd.locate_tick, dispatcher, ctx);
            song_.on_locate(cmd.locate_tick, dispatcher, ctx);
            perf_.on_locate(cmd.locate_tick, dispatcher, ctx);
            break;
        }
    }
}

void Engine::apply(const SongAppendCmd& cmd)
{
    song_.append(cmd.pattern_id, cmd.repeat_count);
}

void Engine::apply(const SongClearCmd& /*cmd*/)
{
    song_.clear();
}

void Engine::apply(const PerfAssignCmd& cmd)
{
    perf_.assign(cmd.slot, cmd.pattern);
}

void Engine::apply(const PerfCueCmd& cmd)
{
    uint64_t pos = last_position_ns_.load(std::memory_order_relaxed);
    uint64_t tick = tempo_map_.ns_to_ticks(pos);
    perf_.cue(cmd.slot, cmd.mode, tick);
}

void Engine::apply(const PerfStopCmd& cmd)
{
    uint64_t pos = last_position_ns_.load(std::memory_order_relaxed);
    uint64_t tick = tempo_map_.ns_to_ticks(pos);
    perf_.stop(cmd.slot, cmd.mode, tick);
}

void Engine::apply(const PerfStopAllCmd& cmd)
{
    uint64_t pos = last_position_ns_.load(std::memory_order_relaxed);
    uint64_t tick = tempo_map_.ns_to_ticks(pos);
    perf_.stop_all(cmd.mode, tick);
}

void Engine::apply(const PerfSetTransposeCmd& cmd)
{
    perf_.set_transpose(cmd.slot, cmd.semitones);
}

void Engine::apply(const PerfSetVelocityScaleCmd& cmd)
{
    perf_.set_velocity_scale(cmd.slot, cmd.scale);
}

void Engine::apply(const PerfSetRandomBiasCmd& cmd)
{
    perf_.set_random_bias(cmd.slot, cmd.bias);
}

void Engine::apply(const AddInputCmd& cmd)
{
    if (cmd.input != nullptr)
    {
        inputs_.push_back(cmd.input);
    }
}

void Engine::apply(const RemoveInputCmd& cmd)
{
    auto it = std::find(inputs_.begin(), inputs_.end(), cmd.input);
    if (it != inputs_.end())
    {
        inputs_.erase(it);
    }
}

void Engine::apply(const SetCtxScaleCmd& cmd)
{
    perf_ctx_.scale = cmd.scale;
}

void Engine::apply(const SetCtxChordCmd& cmd)
{
    perf_ctx_.chord = cmd.chord;
}

void Engine::apply(const SetCtxTransposeCmd& cmd)
{
    perf_ctx_.global_transpose = cmd.semitones;
}

void Engine::apply(const SetCtxVelocityCmd& cmd)
{
    perf_ctx_.global_velocity = cmd.velocity;
}

void Engine::apply(const SetCtxChaosCmd& cmd)
{
    perf_ctx_.chaos = cmd.chaos;
}

void Engine::apply(const SetCtxGrooveCmd& cmd)
{
    perf_ctx_.groove_id = cmd.groove_id;
    perf_ctx_.swing = cmd.swing;
}

void Engine::apply(const AddSourceCmd& cmd)
{
    if (cmd.source == nullptr)
    {
        return;
    }
    // Insert in priority order; equal priorities maintain registration order.
    auto it = std::upper_bound(
        custom_sources_.begin(),
        custom_sources_.end(),
        cmd.priority,
        [](uint32_t pri, const std::pair<uint32_t, EventSource*>& p) { return pri < p.first; });
    custom_sources_.insert(it, {cmd.priority, cmd.source});
}

void Engine::apply(const RemoveSourceCmd& cmd)
{
    auto it = std::find_if(
        custom_sources_.begin(),
        custom_sources_.end(),
        [&](const std::pair<uint32_t, EventSource*>& p) { return p.second == cmd.source; });
    if (it != custom_sources_.end())
    {
        custom_sources_.erase(it);
    }
}

void Engine::apply(const SetTimeSigCmd& cmd)
{
    timesig_map_.insert(cmd.tick, cmd.numerator, cmd.denominator);
}

void Engine::apply(const RemoveTimeSigCmd& cmd)
{
    timesig_map_.remove(cmd.tick);
}

void Engine::apply(const ClearTimeSigCmd& /*cmd*/)
{
    timesig_map_.clear();
}

void Engine::apply(const SetSmpteConfigCmd& cmd)
{
    smpte_config_ = cmd.config;
}

void Engine::apply(const ClearSmpteConfigCmd& /*cmd*/)
{
    smpte_config_.reset();
}

void Engine::process()
{
    Command cmd;
    uint32_t drain_limit = queue_.size();
    while (drain_limit-- > 0 && queue_.pop(cmd))
    {
        std::visit(
            [this](auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, AddEventCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, DeleteEventCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, ReplaceEventCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, SetTempoCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, SetTempoPointCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, RemoveTempoPointCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, SetLoopCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, TransportCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, SongAppendCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, SongClearCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, PerfAssignCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, PerfCueCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, PerfStopCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, PerfStopAllCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, PerfSetTransposeCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, PerfSetVelocityScaleCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, PerfSetRandomBiasCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, AddInputCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, RemoveInputCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, SetCtxScaleCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, SetCtxChordCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, SetCtxTransposeCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, SetCtxVelocityCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, SetCtxChaosCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, SetCtxGrooveCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, AddSourceCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, RemoveSourceCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, SetTimeSigCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, RemoveTimeSigCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, ClearTimeSigCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, SetSmpteConfigCmd>)
                {
                    apply(c);
                }
                else if constexpr (std::is_same_v<T, ClearSmpteConfigCmd>)
                {
                    apply(c);
                }
            },
            cmd);
    }

    if (state_.load(std::memory_order_acquire) != static_cast<uint8_t>(TransportState::PLAYING))
    {
        return;
    }

    input_bus_.clear();
    {
        InputDispatcher input_dispatcher{input_bus_};
        for (auto* input : inputs_)
        {
            input->poll(input_dispatcher);
        }
    }

    uint64_t now = clock_->now_ns();
    uint64_t position = now - session_start_ns_;
    uint64_t to_tick = tempo_map_.ns_to_ticks(position);

    ProcessContext ctx{};
    ctx.input_bus = &input_bus_;
    ctx.modulation_bus = &mod_bus_;
    ctx.perf_ctx = perf_ctx_;

    EventDispatcher dispatcher{sinks_};

    // Loop detection: when the transport has reached or passed loop_end_tick_,
    // locate all sources back to loop_start_tick_ and resume from there.
    if (loop_enabled_ && loop_end_tick_ > loop_start_tick_ && to_tick >= loop_end_tick_)
    {
        ++loop_count_;

        uint64_t loop_pos_ns = tempo_map_.ticks_to_ns(loop_start_tick_);
        session_start_ns_ = now - loop_pos_ns;
        position = loop_pos_ns;
        to_tick = loop_start_tick_;

        timeline_.on_locate(loop_start_tick_, dispatcher, ctx);
        song_.on_locate(loop_start_tick_, dispatcher, ctx);
        perf_.on_locate(loop_start_tick_, dispatcher, ctx);
    }

    // Custom sources run before built-ins, in priority/registration order.
    for (auto& [pri, src] : custom_sources_)
    {
        src->advance(to_tick, dispatcher, ctx);
    }

    // Built-in PLAYBACK sources always run last (priority 2, registered at engine creation).
    timeline_.advance(to_tick, dispatcher, ctx);
    song_.advance(to_tick, dispatcher, ctx);
    perf_.advance(to_tick, dispatcher, ctx);

    for (auto& [sid, sink] : sinks_)
    {
        sink->flush();
    }

    last_position_ns_.store(position, std::memory_order_release);

    // Update the position snapshot for any-thread readers (display, etc.).
    // MeterCursor::tick_to_beat_pos is safe here: the TimeSignatureMap was
    // already fully updated during the command-drain phase above, so no
    // mutation-thread write is racing with this read.
    {
        BeatPosition bp{};
        bool has_meter = !timesig_map_.is_freeform() &&
                         (MeterCursor{timesig_map_}.tick_to_beat_pos(to_tick, bp) == OMEGA_OK);
        snap_bar_.store(has_meter ? bp.bar : 0u, std::memory_order_relaxed);
        snap_beat_.store(has_meter ? bp.beat : 0u, std::memory_order_relaxed);
        snap_sub_.store(has_meter ? bp.subdivision : 0u, std::memory_order_relaxed);
        snap_loop_count_.store(loop_count_, std::memory_order_relaxed);
        // tick written last with release so readers that load tick first with
        // acquire will see all preceding stores.
        snap_tick_.store(to_tick, std::memory_order_release);
    }
}

TransportState Engine::transport_state() const
{
    return static_cast<TransportState>(state_.load(std::memory_order_relaxed));
}

uint64_t Engine::transport_position_ns() const
{
    return last_position_ns_.load(std::memory_order_relaxed);
}

uint64_t Engine::transport_position_tick() const
{
    return tempo_map_.ns_to_ticks(last_position_ns_.load(std::memory_order_relaxed));
}

omega_position_t Engine::position() const noexcept
{
    omega_position_t out{};
    // Load tick first (acquire) so we see any preceding relaxed stores.
    out.tick = snap_tick_.load(std::memory_order_acquire);
    out.bar = snap_bar_.load(std::memory_order_relaxed);
    out.beat = snap_beat_.load(std::memory_order_relaxed);
    out.subdivision = snap_sub_.load(std::memory_order_relaxed);
    out.loop_count = snap_loop_count_.load(std::memory_order_relaxed);
    return out;
}

}  // namespace omega
