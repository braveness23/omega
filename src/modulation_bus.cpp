#include <omega/modulation_bus.h>

namespace omega
{

ModulationBus::ModulationBus() noexcept
{
    for (auto& ch : channels_)
    {
        ch.store(0u, std::memory_order_relaxed);
    }
}

float ModulationBus::bits_to_float(uint32_t bits) noexcept
{
    float f{};
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

uint32_t ModulationBus::float_to_bits(float f) noexcept
{
    uint32_t bits{};
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

uint32_t ModulationBus::register_channel(const char* name, float initial_value) noexcept
{
    if (next_ >= CAPACITY)
    {
        return INVALID;
    }
    uint32_t ch = next_++;
    if (name != nullptr)
    {
        names_[name] = ch;
    }
    channels_[ch].store(float_to_bits(initial_value), std::memory_order_relaxed);
    return ch;
}

uint32_t ModulationBus::find(const char* name) const noexcept
{
    if (name == nullptr)
    {
        return INVALID;
    }
    auto it = names_.find(name);
    return it != names_.end() ? it->second : INVALID;
}

float ModulationBus::get(uint32_t channel) const noexcept
{
    if (channel >= CAPACITY)
    {
        return 0.0f;
    }
    return bits_to_float(channels_[channel].load(std::memory_order_relaxed));
}

void ModulationBus::set(uint32_t channel, float value) noexcept
{
    if (channel >= CAPACITY)
    {
        return;
    }
    channels_[channel].store(float_to_bits(value), std::memory_order_relaxed);
}

void ModulationBus::snapshot(float* out, uint32_t count) const noexcept
{
    if (out == nullptr)
    {
        return;
    }
    uint32_t n = (count < CAPACITY) ? count : CAPACITY;
    for (uint32_t i = 0; i < n; ++i)
    {
        out[i] = bits_to_float(channels_[i].load(std::memory_order_relaxed));
    }
}

}  // namespace omega
