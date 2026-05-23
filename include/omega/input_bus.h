#pragma once

#include <omega/types.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace omega
{

/*
 * Per-cycle buffer for incoming events delivered by EventInput instances.
 *
 * Cleared at the start of each process() cycle. Delivered events are
 * visible to all registered EventSource instances via ProcessContext.
 *
 * Fixed capacity of CAPACITY events. Excess events are silently dropped;
 * overflow_count() accumulates the total number of dropped events.
 *
 * Thread: clear() and push() — Timing thread only.
 *         overflow_count()   — Any thread (atomic read).
 */
class InputBus
{
public:
    static constexpr uint32_t CAPACITY = 256;

    void clear() noexcept { count_ = 0; }

    bool push(const Event& event) noexcept
    {
        if (count_ >= CAPACITY)
        {
            overflow_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        events_[count_++] = event;
        return true;
    }

    [[nodiscard]] uint32_t count() const noexcept { return count_; }

    [[nodiscard]] const Event& at(uint32_t index) const noexcept { return events_[index]; }

    [[nodiscard]] uint32_t overflow_count() const noexcept
    {
        return overflow_.load(std::memory_order_relaxed);
    }

private:
    std::array<Event, CAPACITY> events_;
    uint32_t count_{0};
    std::atomic<uint32_t> overflow_{0};
};

}  // namespace omega
