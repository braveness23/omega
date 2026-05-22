#include <omega/sink.h>

#include <atomic>

namespace omega
{

namespace
{

uint32_t next_sink_id() noexcept
{
    static std::atomic<uint32_t> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

OutputSink::OutputSink() noexcept : id_{next_sink_id()} {}

}  // namespace omega
