#include <omega/engine.h>

#include <algorithm>
#include <type_traits>

namespace omega
{

Engine::Engine(ClockSource* clock, std::pmr::memory_resource* /*mr*/,
               uint32_t /*queue_capacity*/)
    : clock_{clock ? clock : &internal_clock_}
{
}

Engine::~Engine() = default;

void Engine::set_clock(ClockSource* clock) noexcept
{
    clock_ = clock ? clock : &internal_clock_;
}

omega_status_t Engine::add_sink(OutputSink* sink)
{
    if (!sink)
        return OMEGA_ERR_INVALID;

    uint32_t id = sink->sink_id();
    auto it = std::lower_bound(sinks_.begin(), sinks_.end(), id,
                               [](const std::pair<uint32_t, OutputSink*>& p, uint32_t v)
                               { return p.first < v; });
    sinks_.insert(it, {id, sink});
    return OMEGA_OK;
}

TrackId Engine::add_track(std::string name)
{
    return timeline_.add_track(std::move(name));
}

omega_status_t Engine::set_track_sink(TrackId track_id, uint32_t sink_id)
{
    return timeline_.set_sink(track_id, sink_id);
}

omega_status_t Engine::enqueue(Command cmd)
{
    if (queue_.push(std::move(cmd)))
        return OMEGA_OK;
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
            state_.store(static_cast<uint8_t>(TransportState::PLAYING),
                         std::memory_order_release);
            break;
        }
        case TransportAction::STOP:
            state_.store(static_cast<uint8_t>(TransportState::STOPPED),
                         std::memory_order_release);
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
            break;
        }
    }
}

void Engine::process()
{
    // Drain the command queue; apply commands in order.
    Command cmd;
    uint32_t drain_limit = queue_.size();
    while (drain_limit-- > 0 && queue_.pop(cmd))
    {
        std::visit(
            [this](auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, AddEventCmd>)
                    apply(c);
                else if constexpr (std::is_same_v<T, DeleteEventCmd>)
                    apply(c);
                else if constexpr (std::is_same_v<T, SetTempoCmd>)
                    apply(c);
                else if constexpr (std::is_same_v<T, TransportCmd>)
                    apply(c);
            },
            cmd);
    }

    if (state_.load(std::memory_order_acquire) != static_cast<uint8_t>(TransportState::PLAYING))
        return;

    uint64_t now = clock_->now_ns();
    uint64_t position = now - session_start_ns_;
    uint64_t to_tick = tempo_map_.ns_to_ticks(position);

    ProcessContext ctx{};
    EventDispatcher dispatcher{sinks_};
    timeline_.advance(to_tick, dispatcher, ctx);

    // Flush all sinks after dispatching.
    for (auto& [id, sink] : sinks_)
        sink->flush();

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
