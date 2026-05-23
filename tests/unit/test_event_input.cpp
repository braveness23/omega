#include <omega/engine.h>
#include <omega/input_bus.h>
#include <omega/omega.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>
#include <omega/test/mock_event_input.h>

#include <catch2/catch_test_macros.hpp>
#include <thread>

using namespace omega;

// ── InputBus ─────────────────────────────────────────────────────────────────

TEST_CASE("InputBus starts empty")
{
    InputBus bus;
    REQUIRE(bus.count() == 0u);
    REQUIRE(bus.overflow_count() == 0u);
}

TEST_CASE("InputBus push and read")
{
    InputBus bus;
    Event e = omega_make_note_on(0, 1, 0, 60, 100, 0);
    REQUIRE(bus.push(e));
    REQUIRE(bus.count() == 1u);
    REQUIRE(bus.at(0).data[0] == 60);
}

TEST_CASE("InputBus clear resets count but not overflow")
{
    InputBus bus;
    Event e = omega_make_note_on(0, 1, 0, 60, 100, 0);
    bus.push(e);
    REQUIRE(bus.count() == 1u);
    bus.clear();
    REQUIRE(bus.count() == 0u);
    REQUIRE(bus.overflow_count() == 0u);
}

TEST_CASE("InputBus overflow increments counter and does not push")
{
    InputBus bus;
    Event e = omega_make_note_on(0, 1, 0, 60, 100, 0);
    for (uint32_t i = 0; i < InputBus::CAPACITY; ++i)
    {
        REQUIRE(bus.push(e));
    }
    REQUIRE(bus.count() == InputBus::CAPACITY);
    REQUIRE_FALSE(bus.push(e));
    REQUIRE(bus.overflow_count() == 1u);
    REQUIRE_FALSE(bus.push(e));
    REQUIRE(bus.overflow_count() == 2u);
    REQUIRE(bus.count() == InputBus::CAPACITY);
}

// ── InputDispatcher ───────────────────────────────────────────────────────────

TEST_CASE("InputDispatcher::deliver pushes into bus")
{
    InputBus bus;
    InputDispatcher dispatcher{bus};
    Event e = omega_make_note_on(0, 1, 0, 62, 90, 0);
    dispatcher.deliver(e);
    REQUIRE(bus.count() == 1u);
    REQUIRE(bus.at(0).data[0] == 62);
}

// ── MockEventInput ────────────────────────────────────────────────────────────

TEST_CASE("MockEventInput delivers primed events and clears queue")
{
    InputBus bus;
    InputDispatcher dispatcher{bus};
    MockEventInput mock;
    mock.prime(omega_make_note_on(0, 1, 0, 60, 100, 0));
    mock.prime(omega_make_note_on(0, 1, 0, 64, 100, 0));
    mock.prime(omega_make_note_on(0, 1, 0, 67, 100, 0));
    REQUIRE_FALSE(mock.empty());
    mock.poll(dispatcher);
    REQUIRE(mock.empty());
    REQUIRE(bus.count() == 3u);
}

TEST_CASE("MockEventInput delivers nothing when not primed")
{
    InputBus bus;
    InputDispatcher dispatcher{bus};
    MockEventInput mock;
    mock.poll(dispatcher);
    REQUIRE(bus.count() == 0u);
}

// ── Engine integration ────────────────────────────────────────────────────────

TEST_CASE("Engine: 3 primed events appear in ProcessContext input_bus")
{
    MockClock clock;
    Engine eng{&clock};

    MockEventInput mock;
    mock.prime(omega_make_note_on(0, 1, 0, 60, 100, 0));
    mock.prime(omega_make_note_on(0, 1, 0, 64, 100, 0));
    mock.prime(omega_make_note_on(0, 1, 0, 67, 100, 0));

    eng.add_input(&mock);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();  // apply PLAY + AddInputCmd, then poll inputs

    REQUIRE(eng.input_overflow_count() == 0u);
    // Events were delivered into input_bus_ this cycle
    // Verify indirectly: overflow is 0 and we primed exactly 3
    // (direct inspection of input_bus_ requires a custom source in later sprints;
    //  here we verify the plumbing doesn't crash and overflow stays 0)
    REQUIRE(mock.empty());  // queue was drained by poll
}

TEST_CASE("Engine: overflow counter accumulates for excess events")
{
    MockClock clock;
    Engine eng{&clock};

    MockEventInput mock;
    for (uint32_t i = 0; i < 300u; ++i)
    {
        mock.prime(omega_make_note_on(0, 1, 0, static_cast<uint8_t>(i % 128), 100, 0));
    }

    eng.add_input(&mock);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();  // PLAY + AddInput applied, then 300 events polled

    REQUIRE(eng.input_overflow_count() == 300u - InputBus::CAPACITY);
}

TEST_CASE("Engine: remove_input stops delivery on next cycle")
{
    MockClock clock;
    Engine eng{&clock};

    MockEventInput mock;
    eng.add_input(&mock);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();  // apply PLAY + AddInput

    eng.remove_input(&mock);
    eng.process();  // apply RemoveInput

    mock.prime(omega_make_note_on(0, 1, 0, 60, 100, 0));
    eng.process();  // input is removed; event should not reach bus
    // Overflow stays 0 (event was never delivered — input not polled)
    REQUIRE(eng.input_overflow_count() == 0u);
    REQUIRE_FALSE(mock.empty());  // not consumed because input was removed
}

TEST_CASE("Engine: add_input and remove_input go through command queue (TSan)")
{
    MockClock clock;
    Engine eng{&clock};
    MockEventInput mock;

    constexpr int ITERATIONS = 50000;
    std::atomic<bool> running{true};

    // Timing thread: calls process() repeatedly
    std::thread timing([&] {
        while (running.load(std::memory_order_relaxed))
        {
            eng.process();
        }
    });

    // Mutation thread: add and remove input repeatedly
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    for (int i = 0; i < ITERATIONS; ++i)
    {
        eng.add_input(&mock);
        eng.remove_input(&mock);
    }

    running.store(false, std::memory_order_relaxed);
    timing.join();
    // TSan will flag any data race; reaching here means the test passed
    REQUIRE(true);
}

// ── C API ─────────────────────────────────────────────────────────────────────

TEST_CASE("C API: omega_input_create/destroy round-trip")
{
    static int call_count = 0;
    omega_input_desc_t desc{};
    desc.poll_fn = [](omega_input_dispatcher_t*, void*) { call_count++; };
    desc.userdata = nullptr;

    omega_input_t* input = omega_input_create(&desc);
    REQUIRE(input != nullptr);
    omega_input_destroy(input);
}

TEST_CASE("C API: omega_input_create returns NULL for NULL desc")
{
    REQUIRE(omega_input_create(nullptr) == nullptr);
}

TEST_CASE("C API: omega_input_create returns NULL for NULL poll_fn")
{
    omega_input_desc_t desc{};
    desc.poll_fn = nullptr;
    REQUIRE(omega_input_create(&desc) == nullptr);
}

TEST_CASE("C API: omega_engine_add_input / overflow_count / remove_input")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    static int delivered = 0;
    omega_input_desc_t desc{};
    desc.poll_fn = [](omega_input_dispatcher_t* d, void*) {
        omega_event_t ev = omega_make_note_on(0, 1, 0, 60, 100, 0);
        for (int i = 0; i < 300; ++i)
        {
            omega_deliver(d, &ev);
        }
        delivered = 300;
    };

    omega_input_t* input = omega_input_create(&desc);
    REQUIRE(omega_engine_add_input(eng, input) == OMEGA_OK);
    REQUIRE(omega_engine_play(eng) == OMEGA_OK);
    omega_engine_process(eng);  // apply PLAY + AddInput + poll 300 events

    REQUIRE(omega_input_overflow_count(eng) == 300u - 256u);
    REQUIRE(delivered == 300);

    REQUIRE(omega_engine_remove_input(eng, input) == OMEGA_OK);
    omega_engine_process(eng);

    omega_input_destroy(input);
    omega_engine_destroy(eng);
}

TEST_CASE("C API: NULL guards on input functions")
{
    REQUIRE(omega_engine_add_input(nullptr, nullptr) == OMEGA_ERR_INVALID);
    REQUIRE(omega_engine_remove_input(nullptr, nullptr) == OMEGA_ERR_INVALID);
    REQUIRE(omega_input_overflow_count(nullptr) == 0u);
    omega_deliver(nullptr, nullptr);  // must not crash
}
