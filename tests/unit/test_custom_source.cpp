#include <omega/engine.h>
#include <omega/omega.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>
#include <omega/test/mock_event_source.h>
#include <omega/transform_source.h>

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

using namespace omega;

// ── MockEventSource ───────────────────────────────────────────────────────────

TEST_CASE("MockEventSource: dispatches primed events at or before to_tick")
{
    MockClock clock;
    Engine eng{&clock};

    CapturingSink sink;
    eng.add_sink(&sink);

    MockEventSource src;
    src.prime(omega_make_note_on(100, sink.sink_id(), 0, 60, 100, 0));
    src.prime(omega_make_note_on(200, sink.sink_id(), 0, 64, 100, 0));
    src.prime(omega_make_note_on(300, sink.sink_id(), 0, 67, 100, 0));

    eng.add_source(&src, OMEGA_SOURCE_PRIORITY_PLAYBACK);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();  // apply PLAY + AddSource; to_tick = 0 (no events fire yet)

    // Advance to tick 240 (half a beat at 120 BPM) — events at 100 and 200 fire; 300 does not.
    clock.advance_beats(0.5);
    eng.process();

    REQUIRE(sink.count() == 2u);
    REQUIRE(sink.has_note_on(60));
    REQUIRE(sink.has_note_on(64));
    REQUIRE_FALSE(sink.has_note_on(67));
    REQUIRE_FALSE(src.empty());  // tick-300 event still pending
}

TEST_CASE("MockEventSource: dispatches remaining events when to_tick advances")
{
    MockClock clock;
    Engine eng{&clock};

    CapturingSink sink;
    eng.add_sink(&sink);

    MockEventSource src;
    src.prime(omega_make_note_on(100, sink.sink_id(), 0, 60, 100, 0));
    src.prime(omega_make_note_on(200, sink.sink_id(), 0, 64, 100, 0));

    eng.add_source(&src, OMEGA_SOURCE_PRIORITY_PLAYBACK);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();  // apply PLAY + AddSource; to_tick = 0

    clock.advance_ticks(300);
    eng.process();

    REQUIRE(sink.count() == 2u);
    REQUIRE(src.empty());
}

// ── Custom source priority ordering ──────────────────────────────────────────

TEST_CASE("Custom sources at lower priority run before built-in sources")
{
    MockClock clock;
    Engine eng{&clock};

    std::vector<std::string> call_order;

    struct OrderCapture : EventSource
    {
        std::vector<std::string>* order{nullptr};
        std::string name;
        void advance(uint64_t /*to_tick*/, EventDispatcher& /*d*/, ProcessContext& /*ctx*/) override
        {
            order->push_back(name);
        }
    };

    OrderCapture mod;
    mod.order = &call_order;
    mod.name = "modulator";

    OrderCapture ctx_src;
    ctx_src.order = &call_order;
    ctx_src.name = "context";

    eng.add_source(&mod, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.add_source(&ctx_src, OMEGA_SOURCE_PRIORITY_CONTEXT);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});

    clock.advance_ticks(1);
    eng.process();

    REQUIRE(call_order.size() == 2u);
    REQUIRE(call_order[0] == "modulator");
    REQUIRE(call_order[1] == "context");
}

TEST_CASE("omega_engine_remove_source: source is not called after removal")
{
    MockClock clock;
    Engine eng{&clock};

    struct CountSource : EventSource
    {
        int calls{0};
        void advance(uint64_t /*to_tick*/, EventDispatcher& /*d*/, ProcessContext& /*ctx*/) override
        {
            ++calls;
        }
    } src;

    eng.add_source(&src, OMEGA_SOURCE_PRIORITY_PLAYBACK);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});

    clock.advance_ticks(1);
    eng.process();  // apply AddSource + PLAY; source called once
    REQUIRE(src.calls == 1);

    eng.remove_source(&src);
    clock.advance_ticks(1);
    eng.process();  // apply RemoveSource; source NOT called
    REQUIRE(src.calls == 1);

    clock.advance_ticks(1);
    eng.process();  // source still not called
    REQUIRE(src.calls == 1);
}

TEST_CASE("Custom source add/remove goes through command queue (TSan)")
{
    MockClock clock;
    Engine eng{&clock};

    struct NoopSource : EventSource
    {
        void advance(uint64_t /*to_tick*/, EventDispatcher& /*d*/, ProcessContext& /*ctx*/) override
        {}
    } src;

    constexpr int ITERATIONS = 30000;
    std::atomic<bool> running{true};

    std::thread timing([&] {
        while (running.load(std::memory_order_relaxed))
        {
            clock.advance_ticks(1);
            eng.process();
        }
    });

    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    for (int i = 0; i < ITERATIONS; ++i)
    {
        eng.add_source(&src, OMEGA_SOURCE_PRIORITY_PLAYBACK);
        eng.remove_source(&src);
    }

    running.store(false, std::memory_order_relaxed);
    timing.join();
    REQUIRE(true);  // TSan will flag any data race
}

// ── TransformSource / ChannelFilterSource ─────────────────────────────────────

TEST_CASE("TransformSource: ChannelFilterSource drops events on wrong channel")
{
    MockClock clock;
    Engine eng{&clock};

    CapturingSink sink;
    eng.add_sink(&sink);

    // Build a MockEventSource with events on channels 0 and 1.
    MockEventSource upstream;
    upstream.prime(omega_make_note_on(10, sink.sink_id(), 0, 60, 100, 0));  // ch 0 — keep
    upstream.prime(omega_make_note_on(10, sink.sink_id(), 0, 62, 100, 0));  // ch 0 — keep
    // Manually craft a channel-1 note
    Event ch1_note = omega_make_note_on(10, sink.sink_id(), 0, 64, 100, 0);
    ch1_note.channel = 1;
    upstream.prime(ch1_note);

    ChannelFilterSource filter{upstream, 0};  // keep only channel 0
    eng.add_source(&filter, OMEGA_SOURCE_PRIORITY_PLAYBACK);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();  // apply PLAY + AddSource; to_tick = 0

    clock.advance_ticks(20);
    eng.process();

    REQUIRE(sink.count() == 2u);
    REQUIRE(sink.has_note_on(60));
    REQUIRE(sink.has_note_on(62));
    REQUIRE_FALSE(sink.has_note_on(64));
}

TEST_CASE("TransformSource: passes all events when filter accepts all")
{
    MockClock clock;
    Engine eng{&clock};

    CapturingSink sink;
    eng.add_sink(&sink);

    MockEventSource upstream;
    upstream.prime(omega_make_note_on(10, sink.sink_id(), 0, 60, 100, 0));
    upstream.prime(omega_make_note_on(10, sink.sink_id(), 0, 64, 100, 0));

    ChannelFilterSource filter{upstream, 0};  // both events are on ch 0
    eng.add_source(&filter, OMEGA_SOURCE_PRIORITY_PLAYBACK);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();  // apply PLAY + AddSource; to_tick = 0

    clock.advance_ticks(20);
    eng.process();

    REQUIRE(sink.count() == 2u);
}

TEST_CASE("TransformSource: drops all events when none match filter")
{
    MockClock clock;
    Engine eng{&clock};

    CapturingSink sink;
    eng.add_sink(&sink);

    MockEventSource upstream;
    upstream.prime(omega_make_note_on(10, sink.sink_id(), 0, 60, 100, 0));
    upstream.prime(omega_make_note_on(10, sink.sink_id(), 0, 64, 100, 0));

    ChannelFilterSource filter{upstream, 9};  // filter for ch 9 — all events are ch 0
    eng.add_source(&filter, OMEGA_SOURCE_PRIORITY_PLAYBACK);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();  // apply PLAY + AddSource; to_tick = 0

    clock.advance_ticks(20);
    eng.process();

    REQUIRE(sink.count() == 0u);
}

// ── C API ─────────────────────────────────────────────────────────────────────

TEST_CASE("C API: omega_source_create / omega_source_destroy round-trip")
{
    static int called = 0;
    omega_source_desc_t desc{};
    desc.advance_fn = [](uint64_t, omega_dispatcher_t*, omega_process_context_t*, void*) {
        ++called;
    };
    desc.userdata = nullptr;
    desc.priority = OMEGA_SOURCE_PRIORITY_PLAYBACK;

    omega_source_t* src = omega_source_create(&desc);
    REQUIRE(src != nullptr);
    omega_source_destroy(src);
}

TEST_CASE("C API: omega_source_create returns NULL for NULL desc / advance_fn")
{
    REQUIRE(omega_source_create(nullptr) == nullptr);

    omega_source_desc_t desc{};
    desc.advance_fn = nullptr;
    REQUIRE(omega_source_create(&desc) == nullptr);
}

TEST_CASE("C API: omega_engine_add_source / remove_source")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    static int advance_calls = 0;
    omega_source_desc_t desc{};
    desc.advance_fn = [](uint64_t, omega_dispatcher_t*, omega_process_context_t*, void*) {
        ++advance_calls;
    };
    desc.priority = OMEGA_SOURCE_PRIORITY_PLAYBACK;

    omega_source_t* src = omega_source_create(&desc);
    REQUIRE(omega_engine_add_source(eng, src) == OMEGA_OK);
    REQUIRE(omega_engine_play(eng) == OMEGA_OK);
    omega_engine_process(eng);  // apply AddSource + PLAY; advance_fn called once
    REQUIRE(advance_calls == 1);

    REQUIRE(omega_engine_remove_source(eng, src) == OMEGA_OK);
    omega_engine_process(eng);  // apply RemoveSource
    omega_engine_process(eng);  // source not called
    REQUIRE(advance_calls == 1);

    omega_source_destroy(src);
    omega_engine_destroy(eng);
}

TEST_CASE("C API: omega_dispatch delivers event to sink from advance_fn")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    static int notes_received = 0;
    omega_input_desc_t sink_desc{};  // use a callback sink via the C API's event loop

    // Use a C++ CapturingSink registered directly (opaque cast)
    CapturingSink csink;
    omega_engine_add_sink(eng,
                          reinterpret_cast<omega_sink_t*>(
                              &csink));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

    uint32_t sid = csink.sink_id();

    struct Ctx
    {
        uint32_t sid;
    } ctx{sid};

    omega_source_desc_t desc{};
    desc.advance_fn =
        [](uint64_t to_tick, omega_dispatcher_t* d, omega_process_context_t*, void* ud) {
            auto* c = static_cast<Ctx*>(ud);
            omega_event_t ev = omega_make_note_on(to_tick, c->sid, 0, 72, 100, 0);
            omega_dispatch(d, &ev);
        };
    desc.userdata = &ctx;
    desc.priority = OMEGA_SOURCE_PRIORITY_PLAYBACK;

    omega_source_t* src = omega_source_create(&desc);
    REQUIRE(omega_engine_add_source(eng, src) == OMEGA_OK);
    REQUIRE(omega_engine_play(eng) == OMEGA_OK);
    omega_engine_process(eng);  // source called; note dispatched

    REQUIRE(csink.count() == 1u);
    REQUIRE(csink.has_note_on(72));

    omega_source_destroy(src);
    omega_engine_destroy(eng);
}

TEST_CASE("C API: null guards for custom sources")
{
    REQUIRE(omega_engine_add_source(nullptr, nullptr) == OMEGA_ERR_INVALID);
    REQUIRE(omega_engine_remove_source(nullptr, nullptr) == OMEGA_ERR_INVALID);
    omega_dispatch(nullptr, nullptr);  // must not crash
}
