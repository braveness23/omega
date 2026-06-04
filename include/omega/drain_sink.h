#pragma once

#include <omega/detail/spsc_queue.h>
#include <omega/export.h>
#include <omega/sink.h>

#include <cstdint>

namespace omega
{

/*
 * Lock-free OutputSink that buffers dispatched events in an SPSC ring so a
 * host thread (JS main thread, audio worklet, etc.) can pop them without
 * synchronising with the timing thread.
 *
 * Producer: timing thread calls send() from within engine::process().
 *   — Never allocates, blocks, or locks.
 *   — Events dropped silently when the ring is full (overflow tracked by dropped()).
 *
 * Consumer: any single thread calls pop() to drain events one at a time.
 *
 * CAPACITY (512) is the usable ring size (power of two minus one slot for
 * empty/full discrimination). Adjust at compile time by subclassing or by
 * setting OMEGA_DRAIN_CAPACITY before including this header.
 *
 * Resolves kcs-web friction findings W6 (no drain API) and W7 (event_callback
 * fires from timing thread — use DrainSink + poll from main thread instead).
 */
class OMEGA_API DrainSink final : public OutputSink
{
public:
#ifndef OMEGA_DRAIN_CAPACITY
    static constexpr uint32_t CAPACITY = 512u;
#else
    static constexpr uint32_t CAPACITY = OMEGA_DRAIN_CAPACITY;
#endif

    DrainSink() noexcept = default;

    ~DrainSink() override = default;

    DrainSink(const DrainSink&) = delete;
    DrainSink& operator=(const DrainSink&) = delete;
    DrainSink(DrainSink&&) = delete;
    DrainSink& operator=(DrainSink&&) = delete;

    /*
     * Enqueue one event. Called from the timing thread inside engine::process().
     * Thread: Timing thread only. Never allocates or blocks.
     */
    void send(const Event& event) noexcept override;

    /*
     * Flush is a no-op; DrainSink is unbuffered beyond the SPSC ring.
     * Thread: Timing thread only.
     */
    void flush() noexcept override {}

    /*
     * Dequeue one event into `out`. Returns true if an event was available,
     * false if the ring was empty.
     * Thread: Consumer thread only (single consumer).
     */
    bool pop(Event& out) noexcept;

    /*
     * Approximate number of events currently in the ring.
     * May be stale by the time the caller acts on the result.
     * Thread: Any thread (approximate).
     */
    [[nodiscard]] uint32_t size() const noexcept;

    /*
     * Approximate empty check.
     * Thread: Any thread (approximate).
     */
    [[nodiscard]] bool empty() const noexcept;

    /*
     * Total events dropped because the ring was full.
     * Thread: Any thread (atomic load).
     */
    [[nodiscard]] uint32_t dropped() const noexcept;

private:
    detail::SpscQueue<Event, CAPACITY> queue_{};
    std::atomic<uint32_t> dropped_{0u};
};

}  // namespace omega
