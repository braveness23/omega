#pragma once

#include <omega/event_source.h>
#include <omega/export.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace omega
{

/* ── LfoSource ────────────────────────────────────────────────────────────── */

/*
 * Writes a periodic waveform to a ModulationBus channel each cycle.
 *
 * Phase is derived from to_tick so no internal state is needed and on_locate()
 * is a no-op. Rate is expressed in beats: 1.0 = one LFO cycle per quarter note
 * (PPQN ticks). The modulation_bus pointer in ProcessContext must be non-null.
 *
 * Param setters are safe to call from the mutation thread while advance() runs
 * on the timing thread (atomic relaxed stores/loads, same pattern as ModulationBus).
 *
 * Thread model:
 *   Constructor / set_* — mutation thread only.
 *   advance()           — timing thread only.
 */
class OMEGA_API LfoSource final : public EventSource
{
public:
    enum class Shape : uint32_t
    {
        Sine = 0u,
        Triangle = 1u,
        Sawtooth = 2u,
        Square = 3u,
    };

    /*
     * channel    — ModulationBus channel index (from engine.modulation_bus().register_channel()).
     * shape      — waveform shape.
     * rate_beats — period in beats (1.0 = one cycle per quarter note). Must be > 0.
     * depth      — peak amplitude; output = offset ± depth * wave.
     * offset     — DC centre of the waveform; default 0.0.
     */
    explicit LfoSource(uint32_t channel, Shape shape, float rate_beats,
                       float depth, float offset = 0.0f) noexcept;

    ~LfoSource() override = default;

    LfoSource(const LfoSource&) = delete;
    LfoSource& operator=(const LfoSource&) = delete;
    LfoSource(LfoSource&&) = delete;
    LfoSource& operator=(LfoSource&&) = delete;

    /* Param setters — mutation thread only. */
    void set_shape(Shape shape) noexcept;
    void set_rate_beats(float rate_beats) noexcept;
    void set_depth(float depth) noexcept;
    void set_offset(float offset) noexcept;

    /* Timing thread. */
    void advance(uint64_t to_tick, EventDispatcher& dispatcher, ProcessContext& ctx) override;

private:
    uint32_t channel_;
    std::atomic<uint32_t> shape_{};
    std::atomic<uint32_t> rate_bits_{};
    std::atomic<uint32_t> depth_bits_{};
    std::atomic<uint32_t> offset_bits_{};
};

/* ── EnvelopeSource ───────────────────────────────────────────────────────── */

/*
 * Linearly interpolates between breakpoints and writes the result to a
 * ModulationBus channel each cycle.
 *
 * Breakpoints are (tick_offset, value) pairs. They must be added in ascending
 * tick_offset order from the mutation thread before add_source() is called.
 * When loop is true the envelope repeats with period = last breakpoint tick.
 * When loop is false the last value is held past the final breakpoint.
 *
 * Thread model:
 *   add_point / clear_points — mutation thread only, before add_source().
 *   advance()                — timing thread only.
 */
class OMEGA_API EnvelopeSource final : public EventSource
{
public:
    static constexpr uint32_t MAX_POINTS = 64u;

    /*
     * channel — ModulationBus channel index.
     * loop    — if true, repeats with period equal to the last breakpoint tick.
     */
    explicit EnvelopeSource(uint32_t channel, bool loop) noexcept;

    ~EnvelopeSource() override = default;

    EnvelopeSource(const EnvelopeSource&) = delete;
    EnvelopeSource& operator=(const EnvelopeSource&) = delete;
    EnvelopeSource(EnvelopeSource&&) = delete;
    EnvelopeSource& operator=(EnvelopeSource&&) = delete;

    /*
     * Add a breakpoint at tick_offset with the given value.
     * Must be called in ascending tick_offset order.
     * Returns false if MAX_POINTS is already reached.
     * Thread: Mutation thread only, before add_source().
     */
    bool add_point(uint64_t tick_offset, float value) noexcept;

    /*
     * Remove all breakpoints.
     * Thread: Mutation thread only, before add_source().
     */
    void clear_points() noexcept;

    /* Timing thread. */
    void advance(uint64_t to_tick, EventDispatcher& dispatcher, ProcessContext& ctx) override;

private:
    struct BreakPoint
    {
        uint64_t tick;
        float value;
    };

    uint32_t channel_;
    bool loop_;
    std::array<BreakPoint, MAX_POINTS> points_{};
    uint32_t count_{0u};
};

/* ── StepModulatorSource ──────────────────────────────────────────────────── */

/*
 * Selects one of up to MAX_STEPS float values based on the current tick and
 * writes it to a ModulationBus channel.
 *
 * step_ticks is the duration in sequencer ticks of each step. When loop is
 * true the sequence wraps after the last step; when false the last value is
 * held.
 *
 * Thread model:
 *   set_step / set_count — mutation thread only, before add_source().
 *   advance()            — timing thread only.
 */
class OMEGA_API StepModulatorSource final : public EventSource
{
public:
    static constexpr uint32_t MAX_STEPS = 64u;

    /*
     * channel    — ModulationBus channel index.
     * step_ticks — ticks per step.
     * loop       — if true, the sequence wraps after step_count_ steps.
     */
    explicit StepModulatorSource(uint32_t channel, uint64_t step_ticks, bool loop) noexcept;

    ~StepModulatorSource() override = default;

    StepModulatorSource(const StepModulatorSource&) = delete;
    StepModulatorSource& operator=(const StepModulatorSource&) = delete;
    StepModulatorSource(StepModulatorSource&&) = delete;
    StepModulatorSource& operator=(StepModulatorSource&&) = delete;

    /*
     * Set the value for a step. index must be < MAX_STEPS.
     * Automatically extends step_count if index >= current count.
     * Returns false if index >= MAX_STEPS.
     * Thread: Mutation thread only, before add_source().
     */
    bool set_step(uint32_t index, float value) noexcept;

    /*
     * Explicitly set the number of active steps (clamped to MAX_STEPS).
     * Thread: Mutation thread only, before add_source().
     */
    void set_count(uint32_t count) noexcept;

    /* Timing thread. */
    void advance(uint64_t to_tick, EventDispatcher& dispatcher, ProcessContext& ctx) override;

private:
    uint32_t channel_;
    uint64_t step_ticks_;
    bool loop_;
    std::array<float, MAX_STEPS> values_{};
    uint32_t step_count_{0u};
};

}  // namespace omega
