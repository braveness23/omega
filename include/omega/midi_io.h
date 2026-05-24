#pragma once

#include <omega/event_input.h>
#include <omega/export.h>
#include <omega/sink.h>
#include <omega/types.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace omega
{

/*
 * Translates an omega Event to MIDI 1.0 bytes.
 *
 * Fills `out` with the appropriate status byte and data bytes.
 * `out` must point to at least 3 bytes.
 *
 * Returns the number of bytes written (1-3), or 0 for unsupported payload tags.
 *
 * Thread: Any thread. Pure function.
 */
OMEGA_API std::size_t event_to_midi_bytes(const Event& e, uint8_t* out) noexcept;

/*
 * OutputSink that sends events to a real MIDI output port via libremidi.
 *
 * Port selection (port_name argument):
 *   nullptr  — open the first available output port.
 *   ""       — open a virtual output port named "Omega Out".
 *   other    — match port by display_name or port_name (first match wins).
 *
 * If no matching port exists, construction succeeds but send() silently drops
 * events (no crash, no exception from the timing thread).
 *
 * Thread: send() and flush() are timing-thread-only. is_port_open() is any thread.
 */
class OMEGA_API LibremidiSink : public OutputSink
{
public:
    explicit LibremidiSink(const char* port_name) noexcept;
    ~LibremidiSink() override;

    LibremidiSink(const LibremidiSink&) = delete;
    LibremidiSink& operator=(const LibremidiSink&) = delete;
    LibremidiSink(LibremidiSink&&) = delete;
    LibremidiSink& operator=(LibremidiSink&&) = delete;

    void send(const Event& e) override;
    void flush() override {}

    [[nodiscard]] bool is_port_open() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/*
 * EventInput that receives events from a real MIDI input port via libremidi.
 *
 * Port selection (port_name argument):
 *   nullptr  — open the first available input port.
 *   ""       — open a virtual input port named "Omega In".
 *   other    — match port by display_name or port_name (first match wins).
 *
 * Incoming MIDI bytes are converted to omega Events and queued internally.
 * poll() drains the queue into the InputBus each cycle without blocking.
 *
 * Thread: poll() is timing-thread-only. is_port_open() is any thread.
 */
class OMEGA_API LibremidiInput : public EventInput
{
public:
    explicit LibremidiInput(const char* port_name) noexcept;
    ~LibremidiInput() override;

    LibremidiInput(const LibremidiInput&) = delete;
    LibremidiInput& operator=(const LibremidiInput&) = delete;
    LibremidiInput(LibremidiInput&&) = delete;
    LibremidiInput& operator=(LibremidiInput&&) = delete;

    void poll(InputDispatcher& dispatcher) override;

    [[nodiscard]] bool is_port_open() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace omega
