#include <omega/modulators.h>
#include <omega/types.h>

#include <cmath>
#include <cstring>

namespace omega
{

/* ── Bit-cast helpers (float ↔ uint32) ──────────────────────────────────── */
/* Same pattern as ModulationBus — avoids UB from type-punning via memcpy. */

static float bits_to_float(uint32_t bits) noexcept
{
    float f{};
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

static uint32_t float_to_bits(float f) noexcept
{
    uint32_t bits{};
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

static constexpr float kTwoPi = 6.28318530717958647692f;

/* ── LfoSource ────────────────────────────────────────────────────────────── */

LfoSource::LfoSource(uint32_t channel, Shape shape, float rate_beats,
                     float depth, float offset) noexcept
    : channel_{channel}
{
    shape_.store(static_cast<uint32_t>(shape), std::memory_order_relaxed);
    rate_bits_.store(float_to_bits(rate_beats), std::memory_order_relaxed);
    depth_bits_.store(float_to_bits(depth), std::memory_order_relaxed);
    offset_bits_.store(float_to_bits(offset), std::memory_order_relaxed);
}

void LfoSource::set_shape(Shape shape) noexcept
{
    shape_.store(static_cast<uint32_t>(shape), std::memory_order_relaxed);
}

void LfoSource::set_rate_beats(float rate_beats) noexcept
{
    rate_bits_.store(float_to_bits(rate_beats), std::memory_order_relaxed);
}

void LfoSource::set_depth(float depth) noexcept
{
    depth_bits_.store(float_to_bits(depth), std::memory_order_relaxed);
}

void LfoSource::set_offset(float offset) noexcept
{
    offset_bits_.store(float_to_bits(offset), std::memory_order_relaxed);
}

void LfoSource::advance(uint64_t to_tick, EventDispatcher& /*dispatcher*/, ProcessContext& ctx)
{
    if (ctx.modulation_bus == nullptr)
    {
        return;
    }

    const float rate   = bits_to_float(rate_bits_.load(std::memory_order_relaxed));
    const float depth  = bits_to_float(depth_bits_.load(std::memory_order_relaxed));
    const float offset = bits_to_float(offset_bits_.load(std::memory_order_relaxed));
    const auto  shape  = static_cast<Shape>(shape_.load(std::memory_order_relaxed));

    /* period in ticks = PPQN * rate_beats; guard against non-positive rate. */
    const float period = static_cast<float>(PPQN) * (rate > 0.0f ? rate : 1.0f);
    const float phase  = std::fmod(static_cast<float>(to_tick), period) / period;

    float wave{};
    switch (shape)
    {
    case Shape::Sine:
        wave = std::sin(kTwoPi * phase);
        break;
    case Shape::Triangle:
        /* Rises from -1→+1 over first half, falls +1→-1 over second half. */
        wave = 1.0f - 2.0f * std::abs(2.0f * phase - 1.0f);
        break;
    case Shape::Sawtooth:
        wave = 2.0f * phase - 1.0f;
        break;
    case Shape::Square:
        wave = phase < 0.5f ? 1.0f : -1.0f;
        break;
    }

    ctx.modulation_bus->set(channel_, offset + depth * wave);
}

/* ── EnvelopeSource ───────────────────────────────────────────────────────── */

EnvelopeSource::EnvelopeSource(uint32_t channel, bool loop) noexcept
    : channel_{channel}, loop_{loop}
{}

bool EnvelopeSource::add_point(uint64_t tick_offset, float value) noexcept
{
    if (count_ >= MAX_POINTS)
    {
        return false;
    }
    points_[count_++] = {tick_offset, value};
    return true;
}

void EnvelopeSource::clear_points() noexcept
{
    count_ = 0u;
}

void EnvelopeSource::advance(uint64_t to_tick, EventDispatcher& /*dispatcher*/, ProcessContext& ctx)
{
    if (ctx.modulation_bus == nullptr || count_ == 0u)
    {
        return;
    }

    float tick_f = static_cast<float>(to_tick);

    if (loop_ && count_ > 1u)
    {
        const float period = static_cast<float>(points_[count_ - 1u].tick);
        if (period > 0.0f)
        {
            tick_f = std::fmod(tick_f, period);
        }
    }

    /* Before or at first breakpoint: hold first value. */
    if (tick_f <= static_cast<float>(points_[0u].tick))
    {
        ctx.modulation_bus->set(channel_, points_[0u].value);
        return;
    }

    /* Past last breakpoint: hold last value. */
    if (tick_f >= static_cast<float>(points_[count_ - 1u].tick))
    {
        ctx.modulation_bus->set(channel_, points_[count_ - 1u].value);
        return;
    }

    /* Find enclosing segment and interpolate linearly. */
    for (uint32_t i = 0u; i < count_ - 1u; ++i)
    {
        const float t0 = static_cast<float>(points_[i].tick);
        const float t1 = static_cast<float>(points_[i + 1u].tick);
        if (tick_f >= t0 && tick_f < t1)
        {
            const float t     = (tick_f - t0) / (t1 - t0);
            const float value = points_[i].value + t * (points_[i + 1u].value - points_[i].value);
            ctx.modulation_bus->set(channel_, value);
            return;
        }
    }
}

/* ── StepModulatorSource ──────────────────────────────────────────────────── */

StepModulatorSource::StepModulatorSource(uint32_t channel, uint64_t step_ticks,
                                         bool loop) noexcept
    : channel_{channel}, step_ticks_{step_ticks}, loop_{loop}
{}

bool StepModulatorSource::set_step(uint32_t index, float value) noexcept
{
    if (index >= MAX_STEPS)
    {
        return false;
    }
    values_[index] = value;
    if (index >= step_count_)
    {
        step_count_ = index + 1u;
    }
    return true;
}

void StepModulatorSource::set_count(uint32_t count) noexcept
{
    step_count_ = count < MAX_STEPS ? count : MAX_STEPS;
}

void StepModulatorSource::advance(uint64_t to_tick, EventDispatcher& /*dispatcher*/,
                                  ProcessContext& ctx)
{
    if (ctx.modulation_bus == nullptr || step_count_ == 0u || step_ticks_ == 0u)
    {
        return;
    }

    uint64_t index = to_tick / step_ticks_;
    if (loop_)
    {
        index = index % static_cast<uint64_t>(step_count_);
    }
    else if (index >= static_cast<uint64_t>(step_count_))
    {
        index = static_cast<uint64_t>(step_count_) - 1u;
    }

    ctx.modulation_bus->set(channel_, values_[static_cast<uint32_t>(index)]);
}

}  // namespace omega
