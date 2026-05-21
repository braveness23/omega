#include <omega/clock.h>

#include <chrono>

namespace omega
{

uint64_t InternalClock::now_ns() const
{
    using namespace std::chrono;
    return static_cast<uint64_t>(steady_clock::now().time_since_epoch().count());
}

}  // namespace omega
