#pragma once

#include <omega/event_source.h>
#include <omega/pattern_library.h>
#include <omega/time_signature_map.h>
#include <omega/types.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace omega
{

static constexpr uint32_t PERF_MAX_SLOTS = 128u;

enum class SlotState : uint8_t
{
    EMPTY,
    IDLE,
    QUEUED,
    PLAYING,
    STOPPING,
};

/*
 * Built-in performance playback source.
 *
 * Manages up to PERF_MAX_SLOTS independent pattern slots. Each slot runs a pattern state machine
 * (EMPTY → IDLE → QUEUED → PLAYING → STOPPING → IDLE). Slots play simultaneously
 * and independently. Per-slot parameters (transpose, velocity scale, random bias)
 * are applied at dispatch time.
 *
 * Thread: advance() and on_locate() are called from the timing thread only.
 *         All mutation methods (assign, cue, stop, set_*) are applied from within
 *         the command-queue drain (also timing thread), so no locking is required.
 */
class PerformanceSource final : public EventSource
{
public:
    explicit PerformanceSource(const PatternLibrary& library,
                               const TimeSignatureMap& timesig_map) noexcept;

    /*
     * Assigns a pattern to a slot. pattern == 0 unassigns (any state → EMPTY).
     * Thread: Timing thread only (applied from command queue).
     */
    void assign(SlotId slot, PatternId pattern);

    /*
     * Cues the assigned pattern for the given slot.
     *   CueMode::IMMEDIATE  — starts immediately; start_tick = current_tick.
     *   CueMode::NEXT_BEAT  — queues at next global loop boundary.
     *   CueMode::NEXT_BAR   — queues at next musical bar boundary; falls back
     *                         to NEXT_BEAT if the session is in freeform mode.
     * Thread: Timing thread only (applied from command queue).
     */
    void cue(SlotId slot, CueMode mode, uint64_t current_tick);

    /*
     * Stops the slot.
     *   CueMode::IMMEDIATE  — silences at start of next advance() window.
     *   CueMode::NEXT_BEAT  — silences at next loop boundary.
     * Thread: Timing thread only (applied from command queue).
     */
    void stop(SlotId slot, CueMode mode, uint64_t current_tick);

    /*
     * Stops all slots with the given cue mode.
     * Thread: Timing thread only (applied from command queue).
     */
    void stop_all(CueMode mode, uint64_t current_tick);

    void set_transpose(SlotId slot, int8_t semitones);
    void set_velocity_scale(SlotId slot, uint8_t scale);
    void set_random_bias(SlotId slot, uint8_t bias);

    /*
     * Returns the current state of the given performance slot.
     * Returns SlotState::EMPTY for out-of-range slot indices.
     *
     * Thread: Any thread (state is atomic).
     */
    [[nodiscard]] SlotState slot_state(uint32_t slot) const noexcept;

    void advance(uint64_t to_tick, EventDispatcher& dispatcher, ProcessContext& ctx) override;
    void on_locate(uint64_t tick, EventDispatcher& dispatcher, ProcessContext& ctx) override;

private:
    struct ActiveNote
    {
        uint64_t off_tick;
        uint32_t sink_id;
        uint8_t note;
        uint8_t channel;
    };

    struct PerfSlot
    {
        std::atomic<uint8_t> state{static_cast<uint8_t>(SlotState::EMPTY)};
        PatternId assigned{0};
        PatternId playing{0};
        PatternId pending{0};
        uint64_t start_tick{0};
        uint64_t transition_tick{0};
        int8_t transpose{0};
        uint8_t velocity_scale{100};
        uint8_t random_bias{0};
        uint32_t rng_state{0};
        std::vector<ActiveNote> active_notes;

        void reset() noexcept
        {
            state.store(static_cast<uint8_t>(SlotState::EMPTY), std::memory_order_relaxed);
            assigned = 0;
            playing = 0;
            pending = 0;
            start_tick = 0;
            transition_tick = 0;
            transpose = 0;
            velocity_scale = 100;
            random_bias = 0;
            rng_state = 0;
            active_notes.clear();
        }
    };

    void dispatch_slot_events(PerfSlot& slot,
                              uint64_t from_tick,
                              uint64_t to_tick,
                              EventDispatcher& dispatcher);
    static void fire_note_offs(PerfSlot& slot, uint64_t to_tick, EventDispatcher& dispatcher);
    static void silence_slot(PerfSlot& slot, uint64_t at_tick, EventDispatcher& dispatcher);
    [[nodiscard]] uint64_t next_loop_boundary(const PerfSlot& slot,
                                              uint64_t current_tick) const noexcept;
    [[nodiscard]] static uint64_t global_boundary(uint64_t current_tick, uint64_t length) noexcept;
    [[nodiscard]] uint64_t next_bar_boundary(uint64_t current_tick,
                                             uint64_t loop_len) const noexcept;

    const PatternLibrary& library_;
    const TimeSignatureMap& timesig_map_;
    std::array<PerfSlot, PERF_MAX_SLOTS> slots_{};
    uint64_t next_tick_{0};
};

}  // namespace omega
