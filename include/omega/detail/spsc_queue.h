#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <utility>

namespace omega::detail
{

/*
 * Lock-free single-producer / single-consumer ring buffer.
 *
 * push() is called exclusively from the producer thread.
 * pop()  is called exclusively from the consumer thread.
 * empty() and size() are approximate — may reflect stale state from the
 * calling thread's perspective.
 *
 * Capacity must be a power of two; enforced by static_assert.
 * The usable capacity is (Capacity - 1): one slot is reserved to distinguish
 * full from empty without a separate counter.
 */
template <typename T, uint32_t Capacity>
class SpscQueue
{
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(Capacity >= 2, "Capacity must be at least 2");

    alignas(64) std::atomic<uint32_t> tail_{0}; /* write position (producer) */
    alignas(64) std::atomic<uint32_t> head_{0}; /* read position (consumer)  */
    std::array<T, Capacity> storage_;

public:
    /*
     * Enqueue by move. Returns false if the queue is full; item is NOT moved in
     * that case — the caller retains it and may retry.
     *
     * Thread: producer only.
     */
    bool push(T&& item)
    {
        uint32_t tail = tail_.load(std::memory_order_relaxed);
        uint32_t next = (tail + 1U) & (Capacity - 1U);
        if (next == head_.load(std::memory_order_acquire))
        {
            return false;
        }
        storage_[tail] = std::move(item);
        tail_.store(next, std::memory_order_release);
        return true;
    }

    /*
     * Enqueue by copy. Returns false if the queue is full.
     *
     * Thread: producer only.
     */
    bool push(const T& item)
    {
        T copy = item;
        return push(std::move(copy));
    }

    /*
     * Dequeue into out. Returns false if the queue is empty.
     *
     * Thread: consumer only.
     */
    bool pop(T& out)
    {
        uint32_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire))
        {
            return false;
        }
        out = std::move(storage_[head]);
        head_.store((head + 1U) & (Capacity - 1U), std::memory_order_release);
        return true;
    }

    /*
     * Returns true if the queue appears empty at the time of the call.
     * May be stale by the time the caller acts on the result.
     *
     * Thread: any (approximate).
     */
    [[nodiscard]] bool empty() const
    {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    /*
     * Returns the approximate number of items currently in the queue.
     *
     * Thread: any (approximate).
     */
    [[nodiscard]] uint32_t size() const
    {
        uint32_t head_pos = head_.load(std::memory_order_acquire);
        uint32_t tail_pos = tail_.load(std::memory_order_acquire);
        return (tail_pos - head_pos + Capacity) & (Capacity - 1U);
    }
};

}  // namespace omega::detail
