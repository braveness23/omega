#include <omega/midi_io.h>
#include <omega/omega.h>
#include <omega/types.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

// ── LibremidiSink ─────────────────────────────────────────────────────────────

TEST_CASE("LibremidiSink - construction with no ports does not crash", "[midi_io]")
{
    // nullptr = first available port; if none exist, sink is created successfully
    // but port_open() == false. Must never throw or crash.
    omega::LibremidiSink sink{nullptr};
    // Construction must not crash regardless of MIDI hardware availability.
    SUCCEED();
}

TEST_CASE("LibremidiSink - translates NOTE_ON to correct bytes", "[midi_io]")
{
    uint8_t buf[3] = {};

    omega::Event e{};
    e.payload_tag = OMEGA_NOTE_ON;
    e.channel = 3u;
    e.data[0] = 60u;   // note
    e.data[1] = 100u;  // velocity

    const std::size_t n = omega::event_to_midi_bytes(e, buf);

    REQUIRE(n == 3u);
    CHECK(buf[0] == (0x90u | 3u));  // NOTE_ON, channel 3
    CHECK(buf[1] == 60u);
    CHECK(buf[2] == 100u);
}

TEST_CASE("LibremidiSink - translates NOTE_OFF to correct bytes", "[midi_io]")
{
    uint8_t buf[3] = {};

    omega::Event e{};
    e.payload_tag = OMEGA_NOTE_OFF;
    e.channel = 0u;
    e.data[0] = 64u;
    e.data[1] = 0u;

    const std::size_t n = omega::event_to_midi_bytes(e, buf);

    REQUIRE(n == 3u);
    CHECK(buf[0] == 0x80u);
    CHECK(buf[1] == 64u);
    CHECK(buf[2] == 0u);
}

TEST_CASE("LibremidiSink - translates CC to correct bytes", "[midi_io]")
{
    uint8_t buf[3] = {};

    omega::Event e{};
    e.payload_tag = OMEGA_CC;
    e.channel = 5u;
    e.data[0] = 7u;    // controller (volume)
    e.data[1] = 100u;  // value

    const std::size_t n = omega::event_to_midi_bytes(e, buf);

    REQUIRE(n == 3u);
    CHECK(buf[0] == (0xB0u | 5u));
    CHECK(buf[1] == 7u);
    CHECK(buf[2] == 100u);
}

TEST_CASE("LibremidiSink - translates PROGRAM to correct bytes", "[midi_io]")
{
    uint8_t buf[3] = {};

    omega::Event e{};
    e.payload_tag = OMEGA_PROGRAM;
    e.channel = 2u;
    e.data[0] = 42u;  // program

    const std::size_t n = omega::event_to_midi_bytes(e, buf);

    REQUIRE(n == 2u);
    CHECK(buf[0] == (0xC0u | 2u));
    CHECK(buf[1] == 42u);
}

TEST_CASE("LibremidiSink - unsupported event returns 0 bytes", "[midi_io]")
{
    uint8_t buf[3] = {};

    omega::Event e{};
    e.payload_tag = 0xFFu;  // CUSTOM / unsupported

    const std::size_t n = omega::event_to_midi_bytes(e, buf);
    CHECK(n == 0u);
}

TEST_CASE("LibremidiSink - channel clamped to low nibble", "[midi_io]")
{
    uint8_t buf[3] = {};

    omega::Event e{};
    e.payload_tag = OMEGA_NOTE_ON;
    e.channel = 0x1Fu;  // top bits must be masked
    e.data[0] = 60u;
    e.data[1] = 80u;

    omega::event_to_midi_bytes(e, buf);
    CHECK((buf[0] & 0xF0u) == 0x90u);
    CHECK((buf[0] & 0x0Fu) == 0x0Fu);  // only low nibble
}

// ── LibremidiInput ────────────────────────────────────────────────────────────

TEST_CASE("LibremidiInput - construction with no ports does not crash", "[midi_io]")
{
    omega::LibremidiInput input{nullptr};
    SUCCEED();
}

TEST_CASE("LibremidiInput - poll delivers no events when no hardware", "[midi_io]")
{
    omega::LibremidiInput input{nullptr};

    omega::InputBus bus{};
    omega::InputDispatcher dispatcher{bus};
    input.poll(dispatcher);

    CHECK(bus.count() == 0u);
}

// ── C API ─────────────────────────────────────────────────────────────────────

TEST_CASE("C API - omega_sink_create_midi_out with null port", "[midi_io]")
{
    omega_sink_t* sink = omega_sink_create_midi_out(nullptr);
    // Must not crash; may return a valid handle (port not open) or null on alloc failure.
    // If non-null, must destroy cleanly.
    if (sink != nullptr)
    {
        omega_sink_destroy_midi_out(sink);
    }
    SUCCEED();
}

TEST_CASE("C API - omega_input_create_midi_in with null port", "[midi_io]")
{
    omega_input_t* input = omega_input_create_midi_in(nullptr);
    if (input != nullptr)
    {
        omega_input_destroy_midi_in(input);
    }
    SUCCEED();
}

TEST_CASE("C API - OMEGA_ERR_IO is defined and distinct", "[midi_io]")
{
    CHECK(OMEGA_ERR_IO == -8);
    CHECK(OMEGA_ERR_IO != OMEGA_OK);
    CHECK(OMEGA_ERR_IO != OMEGA_ERR_INVALID);
    CHECK(OMEGA_ERR_IO != OMEGA_ERR_NO_SMPTE_CONFIG);
}
