#pragma once

#include <omega/export.h>
#include <omega/types.h>

#include <cstdint>

namespace omega
{

/*
 * Abstract output destination for dispatched events.
 *
 * Every subclass receives a unique sink_id at construction, assigned
 * monotonically from a global counter. The engine routes events to sinks by
 * matching omega_event_t::sink_id against the registered sinks.
 *
 * Thread safety: send() and flush() are called from the timing thread and must
 * never allocate, block, or lock. sink_id() may be called from any thread.
 */
class OMEGA_API OutputSink
{
public:
    virtual ~OutputSink() = default;

    OutputSink(const OutputSink&) = delete;
    OutputSink& operator=(const OutputSink&) = delete;
    OutputSink(OutputSink&&) = delete;
    OutputSink& operator=(OutputSink&&) = delete;

    /*
     * Deliver one event to the sink. Called once per dispatched event per cycle.
     * Thread: Timing thread only.
     */
    virtual void send(const Event& event) = 0;

    /*
     * Called after all events for the current cycle have been sent.
     * Implementations may flush buffers or send MIDI running-status here.
     * Thread: Timing thread only.
     */
    virtual void flush() = 0;

    /*
     * Returns the sink's unique ID, assigned at construction.
     * Thread: Any thread.
     */
    [[nodiscard]] uint32_t sink_id() const noexcept { return id_; }

protected:
    /*
     * Assigns a unique ID. Called by every subclass constructor implicitly.
     * Thread: Any thread (construction is external to playback).
     */
    OutputSink() noexcept;

private:
    uint32_t id_;
};

}  // namespace omega
