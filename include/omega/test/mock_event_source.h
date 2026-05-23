#pragma once

#include <omega/event_source.h>

#include <algorithm>
#include <vector>

namespace omega
{

/*
 * Test utility: an EventSource primed with events at specific ticks.
 *
 * Call prime() to enqueue events before advancing the engine. Each advance()
 * call dispatches all primed events with tick <= to_tick. Part of the public
 * omega test API.
 *
 * Note: uses std::vector (allocates on prime/erase) — suitable for test code
 * only, not for production use on the timing thread.
 */
class MockEventSource : public EventSource
{
public:
    void prime(Event event) { pending_.push_back(event); }

    void advance(uint64_t to_tick, EventDispatcher& dispatcher, ProcessContext& /*ctx*/) override
    {
        for (auto it = pending_.begin(); it != pending_.end();)
        {
            if (it->tick <= to_tick)
            {
                dispatcher.dispatch(*it);
                it = pending_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    [[nodiscard]] bool empty() const noexcept { return pending_.empty(); }

private:
    std::vector<Event> pending_;
};

}  // namespace omega
