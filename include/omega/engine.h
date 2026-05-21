#pragma once

#include <omega/clock.h>
#include <omega/commands.h>
#include <omega/detail/spsc_queue.h>
#include <omega/omega.h>
#include <omega/tempo_map.h>
#include <omega/types.h>

#include <atomic>
#include <cstdint>
#include <memory_resource>

namespace omega
{

/*
 * Sequencer engine — the playback machine.
 *
 * Two-thread contract:
 *   Timing thread   — calls process() at whatever interval suits the host.
 *   Mutation thread — calls enqueue() to deliver commands; returns immediately.
 *
 * process() and enqueue() must not be called concurrently from the same side
 * (i.e., two callers may not both call process() simultaneously, nor two
 * callers both call enqueue() simultaneously — SPSC invariant).
 */
class Engine
{
public:
    /*
     * Constructs the engine.
     *
     * clock           — clock source; NULL uses a built-in InternalClock.
     *                   The engine holds a non-owning reference; the clock
     *                   must outlive the engine.
     * mr              — optional PMR allocator; NULL uses the heap default.
     *                   Reserved for future use.
     * queue_capacity  — reserved; current implementation uses a compile-time
     *                   capacity of 4096.
     *
     * Thread: any thread, before first use.
     */
    explicit Engine(ClockSource* clock = nullptr,
                    std::pmr::memory_resource* mr = nullptr,
                    uint32_t queue_capacity = 4096);

    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /*
     * Enqueue a command for the engine to apply on the next process() call.
     *
     * Thread: Mutation thread only.
     *
     * Returns:
     *   OMEGA_OK            — command enqueued; will be applied next cycle.
     *   OMEGA_ERR_QUEUE_FULL — queue at capacity; command was NOT enqueued.
     */
    omega_status_t enqueue(Command cmd);

    /*
     * Advance the engine by one cycle: drain the command queue, then (if
     * playing) update the transport position. Must never allocate, block, or
     * lock.
     *
     * Thread: Timing thread only.
     */
    void process();

    /*
     * Returns the current transport state. May return a stale value if called
     * concurrently with process().
     *
     * Thread: Any thread.
     */
    TransportState transport_state() const;

    /*
     * Returns the current transport position in nanoseconds from session start.
     * Updated by process(); may return a stale value when read concurrently.
     *
     * Thread: Any thread.
     */
    uint64_t transport_position_ns() const;

private:
    void apply(const SetTempoCmd& cmd);
    void apply(const TransportCmd& cmd);

    InternalClock internal_clock_;
    ClockSource* clock_;

    detail::SpscQueue<Command, 4096> queue_;
    TempoMap tempo_map_;
    std::atomic<uint8_t> state_{static_cast<uint8_t>(TransportState::STOPPED)};
    uint64_t session_start_ns_{0};
    std::atomic<uint64_t> last_position_ns_{0};
};

}  // namespace omega
