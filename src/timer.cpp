#include <omega/timer.h>

#include <chrono>
#include <cstdint>

// clang-format off
#if defined(_WIN32)
// WIN32_LEAN_AND_MEAN is intentionally omitted: timeapi.h needs multimedia
// types from mmsystem.h which lean mode strips. windows.h must come first.
#include <windows.h>  // NOLINT(llvm-include-order)
#include <timeapi.h>  // NOLINT(llvm-include-order)
#else
#include <ctime>
#endif
// clang-format on

namespace omega
{
namespace
{

void platform_sleep_ns(uint64_t ns)
{
#if defined(_WIN32)
    // Convert ns to whole milliseconds, rounding up (ceiling division) so we never
    // return early. Sleep(0) is a yield, not a sleep, so clamp to at least 1 ms.
    DWORD ms = static_cast<DWORD>((ns + 999'999ULL) / 1'000'000ULL);
    if (ms == 0)
        ms = 1;
    Sleep(ms);
#else
    struct timespec ts
    {};
    ts.tv_sec = static_cast<time_t>(ns / 1'000'000'000ULL);
    ts.tv_nsec = static_cast<long>(ns % 1'000'000'000ULL);
    nanosleep(&ts, nullptr);
#endif
}

}  // namespace

OmegaTimer::OmegaTimer(Engine& engine, uint32_t interval_us)
    : engine_(engine), interval_us_(interval_us)
{
#if defined(_WIN32)
    timeBeginPeriod(1);
#endif
    thread_ = std::thread(&OmegaTimer::run, this);
}

OmegaTimer::~OmegaTimer()
{
    stop_.store(true, std::memory_order_release);
    thread_.join();
    engine_.process();
#if defined(_WIN32)
    timeEndPeriod(1);
#endif
}

void OmegaTimer::run()
{
    using Clock = std::chrono::steady_clock;
    const uint64_t interval_ns = static_cast<uint64_t>(interval_us_) * 1'000ULL;

    auto next = Clock::now();

    while (!stop_.load(std::memory_order_acquire))
    {
        engine_.process();

        next += std::chrono::nanoseconds(interval_ns);
        auto now = Clock::now();
        if (next > now)
        {
            auto remaining_ns = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(next - now).count());
            platform_sleep_ns(remaining_ns);
        }
        else
        {
            // We've fallen behind; reset next to avoid a cascade of zero-sleep cycles.
            next = Clock::now();
        }
    }
}

}  // namespace omega
