#pragma once

#include <omega/engine.h>

#include <atomic>
#include <cstdint>
#include <thread>

namespace omega
{

/*
 * RAII timer that drives Engine::process() at a fixed interval.
 *
 * Construct on the "mutation" side; the internal thread becomes the timing
 * thread for the lifetime of the timer object. No other caller may invoke
 * Engine::process() while an OmegaTimer is alive.
 *
 * Thread safety: constructor/destructor are not concurrent-safe with respect
 * to each other; create/destroy from one thread. Engine::enqueue() may be
 * called concurrently from any thread while the timer is running.
 */
class OmegaTimer
{
public:
    /*
     * Starts the timer thread. process() is called at approximately
     * interval_us microseconds. Default: 1000 µs (1 ms).
     *
     * The engine reference must outlive the OmegaTimer.
     */
    explicit OmegaTimer(Engine& engine, uint32_t interval_us = 1000);

    /*
     * Signals the timer thread to stop, waits for it to join, and calls
     * process() one final time on the calling thread before returning.
     */
    ~OmegaTimer();

    OmegaTimer(const OmegaTimer&) = delete;
    OmegaTimer& operator=(const OmegaTimer&) = delete;
    OmegaTimer(OmegaTimer&&) = delete;
    OmegaTimer& operator=(OmegaTimer&&) = delete;

private:
    void run();

    Engine& engine_;
    uint32_t interval_us_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

}  // namespace omega
