#pragma once

#include <omega/clock.h>
#include <omega/omega.h>

#include <cstdint>

namespace omega
{

/*
 * Deterministic clock for tests.
 *
 * Time only advances when explicitly told to via advance_ticks(),
 * advance_beats(), or set_ns(). Inject into Engine to produce
 * deterministic, timing-independent test results.
 *
 * Thread: not thread-safe — advance and query from the same thread.
 */
class MockClock final : public ClockSource
{
public:
    /*
     * Set the BPM used by advance_ticks() and advance_beats().
     * Default: 120 BPM (120000 milli-BPM).
     */
    void set_bpm(uint32_t bpm_milli) noexcept { bpm_milli_ = bpm_milli; }

    /*
     * Advance the clock by `ticks` ticks at the current BPM.
     * Uses the same integer arithmetic as TempoMap to avoid drift
     * when advancing in OMEGA_PPQN-sized chunks.
     */
    void advance_ticks(uint64_t ticks) noexcept
    {
        now_ += ticks * (60'000'000'000'000ULL / bpm_milli_) / OMEGA_PPQN;
    }

    /*
     * Advance the clock by `beats` beats at the current BPM.
     */
    void advance_beats(double beats) noexcept
    {
        uint64_t ns_per_beat = 60'000'000'000'000ULL / bpm_milli_;
        now_ += static_cast<uint64_t>(beats * static_cast<double>(ns_per_beat));
    }

    /*
     * Set the absolute clock position in nanoseconds.
     */
    void set_ns(uint64_t ns) noexcept { now_ = ns; }

    /*
     * Thread: Timing thread only.
     */
    [[nodiscard]] uint64_t now_ns() const noexcept override { return now_; }

private:
    uint64_t now_{0};
    uint32_t bpm_milli_{120'000};
};

}  // namespace omega
