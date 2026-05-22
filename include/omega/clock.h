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
    ClockSource() = default;
    virtual ~ClockSource() = default;

    ClockSource(const ClockSource&) = delete;
    ClockSource& operator=(const ClockSource&) = delete;
    ClockSource(ClockSource&&) = delete;
    ClockSource& operator=(ClockSource&&) = delete;

    /*
     * Returns the current time in nanoseconds. Must be monotonically
     * non-decreasing across successive calls from the same thread.
     *
     * Thread: Timing thread only.
     */
    [[nodiscard]] virtual uint64_t now_ns() const = 0;
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
    [[nodiscard]] uint64_t now_ns() const override;
};

}  // namespace omega
