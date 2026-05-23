#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

namespace omega
{

/*
 * 256-channel float modulation bus shared across all EventSource instances.
 *
 * Modulator sources (priority 0) write channels each process() cycle; playback
 * sources read them via ProcessContext::modulation_bus. Channel indices are
 * assigned once at registration time and never change.
 *
 * Storage uses atomic<uint32_t> with float bit-cast so set() on the timing
 * thread and snapshot() on the mutation thread are TSan-clean on all platforms.
 */
class ModulationBus
{
public:
    static constexpr uint32_t CAPACITY = 256;
    static constexpr uint32_t INVALID = 0xFFFFFFFFu;

    ModulationBus() noexcept;

    /*
     * Registers a named channel and sets its initial value. Returns the
     * channel index, or INVALID if all 256 slots are taken.
     * Thread: Mutation thread only, before playback starts.
     */
    uint32_t register_channel(const char* name, float initial_value) noexcept;

    /*
     * Finds a channel index by name. Returns INVALID if not registered.
     * Thread: Mutation thread only.
     */
    [[nodiscard]] uint32_t find(const char* name) const noexcept;

    /*
     * Gets / sets a channel value. Hot path — no allocation, no locking.
     * Out-of-range channel indices are silently ignored (get returns 0.0f).
     * Thread: Timing thread only.
     */
    [[nodiscard]] float get(uint32_t channel) const noexcept;
    void set(uint32_t channel, float value) noexcept;

    /*
     * Copies up to `count` channel values (capped at CAPACITY) into `out`.
     * Thread: Any thread (each load is atomic; individual values may be stale).
     */
    void snapshot(float* out, uint32_t count) const noexcept;

private:
    static float bits_to_float(uint32_t bits) noexcept;
    static uint32_t float_to_bits(float f) noexcept;

    std::array<std::atomic<uint32_t>, CAPACITY> channels_;
    std::unordered_map<std::string, uint32_t> names_;
    uint32_t next_{0};
};

}  // namespace omega
