#include <omega/engine.h>

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

void Engine::apply(const SetTempoCmd& cmd)
{
    uint64_t pos = last_position_ns_.load(std::memory_order_relaxed);
    uint64_t tick = tempo_map_.ns_to_ticks(pos);
    tempo_map_.insert(tick, cmd.bpm_milli);
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
                else if constexpr (std::is_same_v<T, SetTempoCmd>)
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
            },
            cmd);
    }

    if (state_.load(std::memory_order_acquire) != static_cast<uint8_t>(TransportState::PLAYING))
    {
        return;
    }

    uint64_t now = clock_->now_ns();
    uint64_t position = now - session_start_ns_;
    uint64_t to_tick = tempo_map_.ns_to_ticks(position);

    ProcessContext ctx{};
    EventDispatcher dispatcher{sinks_};
    timeline_.advance(to_tick, dispatcher, ctx);
    song_.advance(to_tick, dispatcher, ctx);
    perf_.advance(to_tick, dispatcher, ctx);

    for (auto& [sid, sink] : sinks_)
    {
        sink->flush();
    }

    last_position_ns_.store(position, std::memory_order_release);
}

TransportState Engine::transport_state() const
{
    return static_cast<TransportState>(state_.load(std::memory_order_relaxed));
}

uint64_t Engine::transport_position_ns() const
{
    return last_position_ns_.load(std::memory_order_relaxed);
}

}  // namespace omega
