#include <omega/midi_io.h>
#include <omega/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <libremidi/libremidi.hpp>
#include <string>

namespace omega
{

// ── Translation helper ────────────────────────────────────────────────────────

std::size_t event_to_midi_bytes(const Event& e, uint8_t* out) noexcept
{
    const uint8_t ch = e.channel & 0x0Fu;
    switch (static_cast<PayloadTag>(e.payload_tag))
    {
        case PayloadTag::NOTE_ON:
            out[0] = static_cast<uint8_t>(0x90u | ch);
            out[1] = e.data[0] & 0x7Fu;
            out[2] = e.data[1] & 0x7Fu;
            return 3;
        case PayloadTag::NOTE_OFF:
            out[0] = static_cast<uint8_t>(0x80u | ch);
            out[1] = e.data[0] & 0x7Fu;
            out[2] = e.data[1] & 0x7Fu;
            return 3;
        case PayloadTag::CC:
            out[0] = static_cast<uint8_t>(0xB0u | ch);
            out[1] = e.data[0] & 0x7Fu;
            out[2] = e.data[1] & 0x7Fu;
            return 3;
        case PayloadTag::PROGRAM:
            out[0] = static_cast<uint8_t>(0xC0u | ch);
            out[1] = e.data[0] & 0x7Fu;
            return 2;
        case PayloadTag::PITCH_BEND:
            out[0] = static_cast<uint8_t>(0xE0u | ch);
            out[1] = e.data[0] & 0x7Fu;  // LSB
            out[2] = e.data[1] & 0x7Fu;  // MSB
            return 3;
        case PayloadTag::AFTERTOUCH:
            out[0] = static_cast<uint8_t>(0xD0u | ch);
            out[1] = e.data[0] & 0x7Fu;
            return 2;
        case PayloadTag::POLY_AT:
            out[0] = static_cast<uint8_t>(0xA0u | ch);
            out[1] = e.data[0] & 0x7Fu;  // note
            out[2] = e.data[1] & 0x7Fu;  // pressure
            return 3;
        default:
            return 0;
    }
}

// ── LibremidiSink::Impl ───────────────────────────────────────────────────────

struct LibremidiSink::Impl
{
    libremidi::midi_out midi_out;
    bool port_open{false};

    explicit Impl(const char* port_name) noexcept
    {
        try
        {
            if (port_name == nullptr)
            {
                libremidi::observer obs{};
                auto ports = obs.get_output_ports();
                if (!ports.empty())
                {
                    if (auto err = midi_out.open_port(ports[0], "Omega Out"); !err.is_set())
                    {
                        port_open = true;
                    }
                }
            }
            else if (*port_name == '\0')
            {
                if (auto err = midi_out.open_virtual_port("Omega Out"); !err.is_set())
                {
                    port_open = true;
                }
            }
            else
            {
                const std::string name{port_name};
                libremidi::observer obs{};
                for (auto& p : obs.get_output_ports())
                {
                    if (p.port_name == name || p.display_name == name)
                    {
                        if (auto err = midi_out.open_port(p, "Omega Out"); !err.is_set())
                        {
                            port_open = true;
                        }
                        break;
                    }
                }
            }
        }
        catch (...)
        {}
    }
};

// ── LibremidiSink ─────────────────────────────────────────────────────────────

LibremidiSink::LibremidiSink(const char* port_name) noexcept
    : impl_{new(std::nothrow) Impl{port_name}}  // NOLINT(cppcoreguidelines-owning-memory)
{}

LibremidiSink::~LibremidiSink() = default;

bool LibremidiSink::is_port_open() const noexcept
{
    return impl_ != nullptr && impl_->port_open;
}

void LibremidiSink::send(const Event& e)
{
    if (impl_ == nullptr || !impl_->port_open)
    {
        return;
    }
    std::array<uint8_t, 3> buf{};
    const std::size_t n = event_to_midi_bytes(e, buf.data());
    if (n == 0)
    {
        return;
    }
    try
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        impl_->midi_out.send_message(reinterpret_cast<const unsigned char*>(buf.data()), n);
    }
    catch (...)
    {}
}

}  // namespace omega
