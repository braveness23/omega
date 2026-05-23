#include <omega/engine.h>
#include <omega/modulation_bus.h>
#include <omega/omega.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>
#include <omega/test/mock_event_source.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <thread>

using namespace omega;

// ── ModulationBus unit tests ──────────────────────────────────────────────────

TEST_CASE("ModulationBus starts with all channels at 0.0f")
{
    ModulationBus bus;
    std::array<float, ModulationBus::CAPACITY> out{};
    bus.snapshot(out.data(), ModulationBus::CAPACITY);
    for (float v : out)
    {
        REQUIRE(v == 0.0f);
    }
}

TEST_CASE("ModulationBus: register 256 channels; 257th returns INVALID")
{
    ModulationBus bus;
    for (uint32_t i = 0; i < ModulationBus::CAPACITY; ++i)
    {
        REQUIRE(bus.register_channel(nullptr, 0.0f) == i);
    }
    REQUIRE(bus.register_channel("overflow", 1.0f) == ModulationBus::INVALID);
}

TEST_CASE("ModulationBus: register with name, then find")
{
    ModulationBus bus;
    uint32_t ch = bus.register_channel("lfo.rate", 0.5f);
    REQUIRE(ch != ModulationBus::INVALID);
    REQUIRE(bus.find("lfo.rate") == ch);
    REQUIRE(bus.find("nonexistent") == ModulationBus::INVALID);
    REQUIRE(bus.find(nullptr) == ModulationBus::INVALID);
}

TEST_CASE("ModulationBus: initial value set at registration")
{
    ModulationBus bus;
    uint32_t ch = bus.register_channel("x", 3.14f);
    REQUIRE(bus.get(ch) == 3.14f);
}

TEST_CASE("ModulationBus: set and get round-trip")
{
    ModulationBus bus;
    uint32_t ch = bus.register_channel("y", 0.0f);
    bus.set(ch, 42.5f);
    REQUIRE(bus.get(ch) == 42.5f);
}

TEST_CASE("ModulationBus: out-of-range channel returns 0.0f and ignores set")
{
    ModulationBus bus;
    REQUIRE(bus.get(ModulationBus::CAPACITY) == 0.0f);
    REQUIRE(bus.get(ModulationBus::INVALID) == 0.0f);
    bus.set(ModulationBus::CAPACITY, 1.0f);  // must not crash
    REQUIRE(bus.get(ModulationBus::CAPACITY) == 0.0f);
}

TEST_CASE("ModulationBus: snapshot copies values")
{
    ModulationBus bus;
    uint32_t ch0 = bus.register_channel("a", 1.0f);
    uint32_t ch1 = bus.register_channel("b", 2.0f);
    std::array<float, 2> out{};
    bus.snapshot(out.data(), 2);
    REQUIRE(out[ch0] == 1.0f);
    REQUIRE(out[ch1] == 2.0f);
}

TEST_CASE("ModulationBus: snapshot with null out is a no-op")
{
    ModulationBus bus;
    bus.snapshot(nullptr, 10);  // must not crash
}

TEST_CASE("ModulationBus: set/get from timing thread, snapshot from mutation thread (TSan)")
{
    ModulationBus bus;
    uint32_t ch = bus.register_channel("osc", 0.0f);
    std::atomic<bool> running{true};

    std::thread timing([&] {
        int count = 0;
        while (running.load(std::memory_order_relaxed))
        {
            bus.set(ch, static_cast<float>(++count));
        }
    });

    std::array<float, ModulationBus::CAPACITY> out{};
    for (int i = 0; i < 10000; ++i)
    {
        bus.snapshot(out.data(), ModulationBus::CAPACITY);
    }

    running.store(false, std::memory_order_relaxed);
    timing.join();
    REQUIRE(true);  // TSan will flag any data race
}

// ── Engine integration ────────────────────────────────────────────────────────

TEST_CASE("Engine: custom source writes to modulation channel, next source reads it")
{
    MockClock clock;
    Engine eng{&clock};

    CapturingSink sink;
    eng.add_sink(&sink);

    uint32_t ch = eng.modulation_bus().register_channel("sig", 0.0f);

    // Writer source: sets channel `ch` to 1.0f during advance()
    struct WriterSource : EventSource
    {
        ModulationBus* bus{nullptr};
        uint32_t ch{0};
        bool called{false};
        void advance(uint64_t /*to_tick*/, EventDispatcher& /*d*/, ProcessContext& ctx) override
        {
            ctx.modulation_bus->set(ch, 1.0f);
            called = true;
        }
    } writer;
    writer.bus = &eng.modulation_bus();
    writer.ch = ch;

    // Reader source: reads channel `ch`; stores the value; dispatches a note if value != 0
    struct ReaderSource : EventSource
    {
        ModulationBus* bus{nullptr};
        uint32_t ch{0};
        float last_value{-1.0f};
        void advance(uint64_t to_tick, EventDispatcher& d, ProcessContext& ctx) override
        {
            last_value = ctx.modulation_bus->get(ch);
            if (last_value != 0.0f)
            {
                d.dispatch(omega_make_note_on(to_tick, sink_id, 0, 60, 100, 0));
            }
        }
        uint32_t sink_id{0};
    } reader;
    reader.bus = &eng.modulation_bus();
    reader.ch = ch;
    reader.sink_id = sink.sink_id();

    reader.sink_id = sink.sink_id();

    // Register writer (priority 0=MODULATOR) before reader (priority 2=PLAYBACK).
    eng.add_source(&writer, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.add_source(&reader, OMEGA_SOURCE_PRIORITY_PLAYBACK);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();  // apply commands; to_tick = 0; sources called once

    // Reset and advance for a clean second cycle.
    writer.called = false;
    reader.last_value = -1.0f;
    sink.clear();

    clock.advance_ticks(480);
    eng.process();

    REQUIRE(writer.called);
    REQUIRE(reader.last_value == 1.0f);
    REQUIRE(sink.count() == 1u);
}

// ── C API ─────────────────────────────────────────────────────────────────────

TEST_CASE("C API: omega_mod_register and find")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    omega_mod_channel_t ch = omega_mod_register(eng, "cutoff", 0.7f);
    REQUIRE(ch != OMEGA_MOD_INVALID);
    REQUIRE(omega_mod_find(eng, "cutoff") == ch);
    REQUIRE(omega_mod_find(eng, "missing") == OMEGA_MOD_INVALID);

    omega_engine_destroy(eng);
}

TEST_CASE("C API: omega_mod_register 256 channels; 257th returns INVALID")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    for (uint32_t i = 0; i < 256; ++i)
    {
        REQUIRE(omega_mod_register(eng, nullptr, 0.0f) != OMEGA_MOD_INVALID);
    }
    REQUIRE(omega_mod_register(eng, "x", 1.0f) == OMEGA_MOD_INVALID);

    omega_engine_destroy(eng);
}

TEST_CASE("C API: omega_mod_get / omega_mod_set")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    omega_mod_channel_t ch = omega_mod_register(eng, "depth", 0.0f);
    REQUIRE(omega_mod_get(eng, ch) == 0.0f);
    REQUIRE(omega_mod_set(eng, ch, 0.5f) == OMEGA_OK);
    REQUIRE(omega_mod_get(eng, ch) == 0.5f);

    omega_engine_destroy(eng);
}

TEST_CASE("C API: omega_mod_snapshot")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    omega_mod_channel_t ch = omega_mod_register(eng, "val", 3.0f);
    std::array<float, 4> out{};
    REQUIRE(omega_mod_snapshot(eng, out.data(), 4) == OMEGA_OK);
    REQUIRE(out[ch] == 3.0f);

    omega_engine_destroy(eng);
}

TEST_CASE("C API: null guards for modulation bus")
{
    REQUIRE(omega_mod_register(nullptr, "x", 0.0f) == OMEGA_MOD_INVALID);
    REQUIRE(omega_mod_find(nullptr, "x") == OMEGA_MOD_INVALID);
    REQUIRE(omega_mod_get(nullptr, 0) == 0.0f);
    REQUIRE(omega_mod_set(nullptr, 0, 1.0f) == OMEGA_ERR_INVALID);
    REQUIRE(omega_mod_snapshot(nullptr, nullptr, 0) == OMEGA_ERR_INVALID);
}
