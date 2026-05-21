#include <omega/sink.h>

#include <atomic>

namespace omega
{

namespace
{
std::atomic<uint32_t> s_next_sink_id{1};
}

OutputSink::OutputSink() noexcept : id_{s_next_sink_id.fetch_add(1, std::memory_order_relaxed)} {}

}  // namespace omega
