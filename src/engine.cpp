#include <omega/engine.h>

#include <chrono>
#include <type_traits>

namespace omega
{

namespace
{

uint64_t wall_ns()
{
    using namespace std::chrono;
    return static_cast<uint64_t>(steady_clock::now().time_since_epoch().count());
}

}  // namespace

Engine::Engine(std::pmr::memory_resource* /*mr*/, uint32_t /*queue_capacity*/) {}

Engine::~Engine() = default;

omega_status_t Engine::enqueue(Command cmd)
{
    if (queue_.push(std::move(cmd)))
        return OMEGA_OK;
    return OMEGA_ERR_QUEUE_FULL;
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
            session_start_ns_ = wall_ns() - pos;
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
                session_start_ns_ = wall_ns() - pos;
            }
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
                if constexpr (std::is_same_v<T, SetTempoCmd>)
                    apply(c);
                else if constexpr (std::is_same_v<T, TransportCmd>)
                    apply(c);
            },
            cmd);
    }

    if (state_.load(std::memory_order_acquire) != static_cast<uint8_t>(TransportState::PLAYING))
    {
        return;
    }

    uint64_t now = wall_ns();
    uint64_t position = now - session_start_ns_;

    // Advance to current tick (sources will be wired in M2+).
    (void)tempo_map_.ns_to_ticks(position);

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
