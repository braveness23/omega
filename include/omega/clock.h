#pragma once

#include <omega/export.h>

#include <cstdint>

namespace omega
{

/*
 * Abstract clock — the sole time source for the timing thread.
 *
 * Implementations must be monotonically non-decreasing. The timing thread
 * calls now_ns() on every process() cycle; implementations must never
 * allocate, block, or lock.
 *
 * Thread: Timing thread only.
 */
class OMEGA_API ClockSource
{
public:
    virtual ~ClockSource() = default;

    /*
     * Returns the current time in nanoseconds. Must be monotonically
     * non-decreasing across successive calls from the same thread.
     *
     * Thread: Timing thread only.
     */
    virtual uint64_t now_ns() const = 0;
};

/*
 * System wall-clock backed by std::chrono::steady_clock. Suitable for
 * production use. Not suitable for deterministic testing — use MockClock.
 *
 * Thread: Timing thread only.
 */
class OMEGA_API InternalClock final : public ClockSource
{
public:
    uint64_t now_ns() const override;
};

}  // namespace omega
