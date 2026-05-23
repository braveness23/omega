#pragma once

#include <omega/event_input.h>

#include <vector>

namespace omega
{

/*
 * Test utility: primes events to be delivered on the next poll() call.
 *
 * Call prime() to enqueue events, then advance the engine one cycle.
 * All primed events are delivered into the InputBus and the queue is
 * cleared. Part of the public omega test API.
 */
class MockEventInput : public EventInput
{
public:
    void prime(Event event) { pending_.push_back(event); }

    void poll(InputDispatcher& dispatcher) override
    {
        for (const auto& e : pending_)
        {
            dispatcher.deliver(e);
        }
        pending_.clear();
    }

    [[nodiscard]] bool empty() const noexcept { return pending_.empty(); }

private:
    std::vector<Event> pending_;
};

}  // namespace omega
