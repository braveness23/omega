#pragma once

#include <omega/process_context.h>
#include <omega/sink.h>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace omega
{

/*
 * Dispatches events from sources to registered OutputSinks.
 *
 * Constructed by Engine at the start of each process() cycle and passed to
 * every EventSource::advance() call. Looks up sinks by event.sink_id and
 * delivers via OutputSink::send(). Silently drops events whose sink_id is
 * not registered.
 *
 * Thread: Timing thread only (created and used within process()).
 */
class EventDispatcher
{
public:
    using SinkList = std::vector<std::pair<uint32_t, OutputSink*>>;

    explicit EventDispatcher(const SinkList& sinks) noexcept : sinks_{&sinks} {}

    void dispatch(const Event& event) noexcept
    {
        auto it = std::lower_bound(
            sinks_->begin(),
            sinks_->end(),
            event.sink_id,
            [](const std::pair<uint32_t, OutputSink*>& p, uint32_t id) { return p.first < id; });
        if (it != sinks_->end() && it->first == event.sink_id)
        {
            it->second->send(event);
        }
    }

private:
    const SinkList* sinks_;
};

/*
 * Abstract playback source. Registered with the engine; called once per
 * process() cycle to advance playback and dispatch due events.
 *
 * Thread: advance() and on_locate() are called from the timing thread only.
 */
class EventSource
{
public:
    virtual ~EventSource() = default;

    EventSource(const EventSource&) = delete;
    EventSource& operator=(const EventSource&) = delete;
    EventSource(EventSource&&) = delete;
    EventSource& operator=(EventSource&&) = delete;

    /*
     * Advance playback to `to_tick`, dispatching all events in
     * (last_dispatched_tick, to_tick].
     *
     * Thread: Timing thread only. Must never allocate, block, or lock.
     */
    virtual void advance(uint64_t to_tick, EventDispatcher& dispatcher, ProcessContext& ctx) = 0;

    /*
     * Called when the transport locates to `tick`. Stateful sources reset
     * their playback position; sources supporting chase may dispatch
     * catch-up events via `chase_out`.
     *
     * Default: resets playback position to `tick` (no chasing).
     * Thread: Timing thread only.
     */
    virtual void on_locate(uint64_t tick, EventDispatcher& /*chase_out*/, ProcessContext& /*ctx*/)
    {
        (void)tick;
    }

protected:
    EventSource() = default;
};

}  // namespace omega
