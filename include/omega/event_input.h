#pragma once

#include <omega/input_bus.h>

namespace omega
{

/*
 * Delivers events from the timing thread into the per-cycle InputBus.
 *
 * Created by the engine at the start of each process() cycle and passed to
 * every registered EventInput::poll() call. Excess events are silently
 * dropped by the InputBus (overflow counter incremented).
 *
 * Thread: Timing thread only (created and used within process()).
 */
class InputDispatcher
{
public:
    explicit InputDispatcher(InputBus& bus) noexcept : bus_{&bus} {}

    void deliver(const Event& event) noexcept { bus_->push(event); }

private:
    InputBus* bus_;
};

/*
 * Abstract incoming event source (MIDI, OSC, CV, etc.).
 *
 * Registered with the engine via omega_engine_add_input(). Called once per
 * process() cycle before any EventSource::advance() call. Deliver events by
 * calling dispatcher.deliver().
 *
 * Thread: poll() is called from the timing thread. Must never allocate,
 * block, or lock.
 */
class EventInput
{
public:
    virtual ~EventInput() = default;

    EventInput(const EventInput&) = delete;
    EventInput& operator=(const EventInput&) = delete;
    EventInput(EventInput&&) = delete;
    EventInput& operator=(EventInput&&) = delete;

    /* Thread: Timing thread only. Must never allocate, block, or lock. */
    virtual void poll(InputDispatcher& dispatcher) = 0;

protected:
    EventInput() = default;
};

}  // namespace omega
