#include <omega/detail/spsc_queue.h>
#include <omega/midi_io.h>
#include <omega/types.h>

#include <cstddef>
#include <cstdint>
#include <libremidi/libremidi.hpp>
#include <string>

namespace omega
{

// ── MIDI byte → Event translation ────────────────────────────────────────────

namespace
{

bool midi_bytes_to_event(const libremidi::message& msg,
                         Event& out) noexcept  // NOLINT(readability-function-cognitive-complexity)
{
    if (msg.bytes.empty())
    {
        return false;
    }
    const uint8_t status = msg.bytes[0];
    const uint8_t type = status & 0xF0u;
    const uint8_t ch = status & 0x0Fu;

    out = Event{};
    out.channel = ch;

    switch (type)
    {
        case 0x90u:
        {
            if (msg.bytes.size() < 3u)
            {
                return false;
            }
            const uint8_t vel = msg.bytes[2];
            if (vel == 0u)
            {
                out.payload_tag = static_cast<uint8_t>(PayloadTag::NOTE_OFF);
                out.data[0] = msg.bytes[1];
                out.data[1] = 0u;
            }
            else
            {
                out.payload_tag = static_cast<uint8_t>(PayloadTag::NOTE_ON);
                out.data[0] = msg.bytes[1];
                out.data[1] = vel;
            }
            return true;
        }
        case 0x80u:
        {
            if (msg.bytes.size() < 3u)
            {
                return false;
            }
            out.payload_tag = static_cast<uint8_t>(PayloadTag::NOTE_OFF);
            out.data[0] = msg.bytes[1];
            out.data[1] = msg.bytes[2];
            return true;
        }
        case 0xB0u:
        {
            if (msg.bytes.size() < 3u)
            {
                return false;
            }
            out.payload_tag = static_cast<uint8_t>(PayloadTag::CC);
            out.data[0] = msg.bytes[1];
            out.data[1] = msg.bytes[2];
            return true;
        }
        case 0xC0u:
        {
            if (msg.bytes.size() < 2u)
            {
                return false;
            }
            out.payload_tag = static_cast<uint8_t>(PayloadTag::PROGRAM);
            out.data[0] = msg.bytes[1];
            return true;
        }
        case 0xE0u:
        {
            if (msg.bytes.size() < 3u)
            {
                return false;
            }
            out.payload_tag = static_cast<uint8_t>(PayloadTag::PITCH_BEND);
            out.data[0] = msg.bytes[1] & 0x7Fu;  // LSB
            out.data[1] = msg.bytes[2] & 0x7Fu;  // MSB
            return true;
        }
        case 0xD0u:
        {
            if (msg.bytes.size() < 2u)
            {
                return false;
            }
            out.payload_tag = static_cast<uint8_t>(PayloadTag::AFTERTOUCH);
            out.data[0] = msg.bytes[1] & 0x7Fu;
            return true;
        }
        case 0xA0u:
        {
            if (msg.bytes.size() < 3u)
            {
                return false;
            }
            out.payload_tag = static_cast<uint8_t>(PayloadTag::POLY_AT);
            out.data[0] = msg.bytes[1] & 0x7Fu;  // note
            out.data[1] = msg.bytes[2] & 0x7Fu;  // pressure
            return true;
        }
        default:
            return false;
    }
}

}  // namespace

// ── LibremidiInput::Impl ──────────────────────────────────────────────────────

struct LibremidiInput::Impl
{
    detail::SpscQueue<Event, 256> queue;
    libremidi::midi_in midi_in;
    bool port_open{false};

    explicit Impl(const char* port_name) noexcept
        : queue{}, midi_in{[this]() {
              libremidi::input_configuration cfg;
              cfg.on_message = [this](const libremidi::message& msg) noexcept {
                  Event e{};
                  if (midi_bytes_to_event(msg, e))
                  {
                      queue.push(e);
                  }
              };
              cfg.ignore_sysex = true;
              cfg.ignore_timing = true;
              cfg.ignore_sensing = true;
              return cfg;
          }()}
    {
        try
        {
            if (port_name == nullptr)
            {
                libremidi::observer obs{};
                auto ports = obs.get_input_ports();
                if (!ports.empty())
                {
                    if (auto err = midi_in.open_port(ports[0], "Omega In"); !err.is_set())
                    {
                        port_open = true;
                    }
                }
            }
            else if (*port_name == '\0')
            {
                if (auto err = midi_in.open_virtual_port("Omega In"); !err.is_set())
                {
                    port_open = true;
                }
            }
            else
            {
                const std::string name{port_name};
                libremidi::observer obs{};
                for (auto& p : obs.get_input_ports())
                {
                    if (p.port_name == name || p.display_name == name)
                    {
                        if (auto err = midi_in.open_port(p, "Omega In"); !err.is_set())
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

// ── LibremidiInput ────────────────────────────────────────────────────────────

LibremidiInput::LibremidiInput(const char* port_name) noexcept
    : impl_{new(std::nothrow) Impl{port_name}}
{}

LibremidiInput::~LibremidiInput() = default;

bool LibremidiInput::is_port_open() const noexcept
{
    return impl_ != nullptr && impl_->port_open;
}

void LibremidiInput::poll(InputDispatcher& dispatcher)
{
    if (impl_ == nullptr)
    {
        return;
    }
    Event e{};
    while (impl_->queue.pop(e))
    {
        dispatcher.deliver(e);
    }
}

}  // namespace omega
