#include <omega/drain_sink.h>

#include <atomic>

namespace omega
{

void DrainSink::send(const Event& event) noexcept
{
    if (!queue_.push(event))
    {
        dropped_.fetch_add(1u, std::memory_order_relaxed);
    }
}

bool DrainSink::pop(Event& out) noexcept
{
    return queue_.pop(out);
}

uint32_t DrainSink::size() const noexcept
{
    return queue_.size();
}

bool DrainSink::empty() const noexcept
{
    return queue_.empty();
}

uint32_t DrainSink::dropped() const noexcept
{
    return dropped_.load(std::memory_order_relaxed);
}

}  // namespace omega
