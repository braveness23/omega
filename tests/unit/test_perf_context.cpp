#include <omega/engine.h>
#include <omega/omega.h>
#include <omega/perf_context.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>
#include <omega/test/mock_event_source.h>

#include <catch2/catch_test_macros.hpp>
#include <thread>

using namespace omega;

// ── ProcessContext snapshot via custom source ─────────────────────────────────

// A source that captures the ProcessContext.perf_ctx on each advance() call.
struct ContextCapture : EventSource
{
    omega_perf_ctx_t last{};
    bool called{false};
    void advance(uint64_t /*to_tick*/, EventDispatcher& /*d*/, ProcessContext& ctx) override
    {
        last = ctx.perf_ctx;
        called = true;
    }
};

// ── Transpose ─────────────────────────────────────────────────────────────────

TEST_CASE("omega_ctx_set_transpose: applied on next process() cycle")
{
    MockClock clock;
    Engine eng{&clock};

    ContextCapture cap;
    eng.add_source(&cap, OMEGA_SOURCE_PRIORITY_CONTEXT);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});

    eng.ctx_set_transpose(12);

    clock.advance_ticks(1);
    eng.process();

    REQUIRE(cap.called);
    REQUIRE(cap.last.global_transpose == 12);
}

TEST_CASE("omega_ctx_set_transpose: negative value")
{
    MockClock clock;
    Engine eng{&clock};

    ContextCapture cap;
    eng.add_source(&cap, OMEGA_SOURCE_PRIORITY_CONTEXT);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.ctx_set_transpose(-24);

    clock.advance_ticks(1);
    eng.process();

    REQUIRE(cap.last.global_transpose == -24);
}

// ── Velocity ──────────────────────────────────────────────────────────────────

TEST_CASE("omega_ctx_set_velocity: applied on next process() cycle")
{
    MockClock clock;
    Engine eng{&clock};

    ContextCapture cap;
    eng.add_source(&cap, OMEGA_SOURCE_PRIORITY_CONTEXT);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.ctx_set_velocity(200);

    clock.advance_ticks(1);
    eng.process();

    REQUIRE(cap.last.global_velocity == 200u);
}

// ── Chaos ─────────────────────────────────────────────────────────────────────

TEST_CASE("omega_ctx_set_chaos: applied on next process() cycle")
{
    MockClock clock;
    Engine eng{&clock};

    ContextCapture cap;
    eng.add_source(&cap, OMEGA_SOURCE_PRIORITY_CONTEXT);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.ctx_set_chaos(75);

    clock.advance_ticks(1);
    eng.process();

    REQUIRE(cap.last.chaos == 75u);
}

// ── Scale ─────────────────────────────────────────────────────────────────────

TEST_CASE("omega_ctx_set_scale: applied on next process() cycle")
{
    MockClock clock;
    Engine eng{&clock};

    ContextCapture cap;
    eng.add_source(&cap, OMEGA_SOURCE_PRIORITY_CONTEXT);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});

    omega_scale_t scale{};
    scale.root = 5;          // F
    scale.bitmask = 0x0AB5;  // major scale semitone pattern

    eng.ctx_set_scale(scale);

    clock.advance_ticks(1);
    eng.process();

    REQUIRE(cap.last.scale.root == 5u);
    REQUIRE(cap.last.scale.bitmask == 0x0AB5u);
}

// ── Chord ─────────────────────────────────────────────────────────────────────

TEST_CASE("omega_ctx_set_chord: applied on next process() cycle")
{
    MockClock clock;
    Engine eng{&clock};

    ContextCapture cap;
    eng.add_source(&cap, OMEGA_SOURCE_PRIORITY_CONTEXT);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});

    omega_chord_t chord{};
    chord.root = 0;  // C
    chord.type = 1;  // major
    chord.voices[0] = 60;
    chord.voices[1] = 64;
    chord.voices[2] = 67;

    eng.ctx_set_chord(chord);

    clock.advance_ticks(1);
    eng.process();

    REQUIRE(cap.last.chord.root == 0u);
    REQUIRE(cap.last.chord.type == 1u);
    REQUIRE(cap.last.chord.voices[0] == 60u);
    REQUIRE(cap.last.chord.voices[1] == 64u);
    REQUIRE(cap.last.chord.voices[2] == 67u);
}

// ── Groove ────────────────────────────────────────────────────────────────────

TEST_CASE("omega_ctx_set_groove: applied on next process() cycle")
{
    MockClock clock;
    Engine eng{&clock};

    ContextCapture cap;
    eng.add_source(&cap, OMEGA_SOURCE_PRIORITY_CONTEXT);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.ctx_set_groove(3, 0.6f);

    clock.advance_ticks(1);
    eng.process();

    REQUIRE(cap.last.groove_id == 3u);
    REQUIRE(cap.last.swing == 0.6f);
}

// ── ctx_get ───────────────────────────────────────────────────────────────────

TEST_CASE("Engine::ctx_get returns last-committed values")
{
    MockClock clock;
    Engine eng{&clock};

    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.ctx_set_transpose(7);
    eng.ctx_set_velocity(150);

    clock.advance_ticks(1);
    eng.process();  // apply the commands

    omega_perf_ctx_t out{};
    eng.ctx_get(out);
    REQUIRE(out.global_transpose == 7);
    REQUIRE(out.global_velocity == 150u);
}

// ── TSan: mutation thread enqueues, timing thread reads via ProcessContext ────

TEST_CASE("TSan: mutation thread sets scale, timing thread reads via ProcessContext")
{
    MockClock clock;
    Engine eng{&clock};

    ContextCapture cap;
    eng.add_source(&cap, OMEGA_SOURCE_PRIORITY_CONTEXT);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});

    constexpr int ITERATIONS = 20000;
    std::atomic<bool> running{true};

    std::thread timing([&] {
        while (running.load(std::memory_order_relaxed))
        {
            clock.advance_ticks(1);
            eng.process();
        }
    });

    omega_scale_t s{};
    for (int i = 0; i < ITERATIONS; ++i)
    {
        s.root = static_cast<uint8_t>(i % 12);
        eng.ctx_set_scale(s);
    }

    running.store(false, std::memory_order_relaxed);
    timing.join();
    REQUIRE(true);  // TSan will flag any data race
}

// ── C API ─────────────────────────────────────────────────────────────────────

TEST_CASE("C API: omega_ctx_set_transpose and omega_ctx_get")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    REQUIRE(omega_ctx_set_transpose(eng, 12) == OMEGA_OK);
    omega_engine_play(eng);
    omega_engine_process(eng);  // apply command

    omega_perf_ctx_t out{};
    REQUIRE(omega_ctx_get(eng, &out) == OMEGA_OK);
    REQUIRE(out.global_transpose == 12);

    omega_engine_destroy(eng);
}

TEST_CASE("C API: omega_ctx_set_scale and omega_ctx_get")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    omega_scale_t s{};
    s.root = 9;  // A
    s.bitmask = 0x0555;
    REQUIRE(omega_ctx_set_scale(eng, &s) == OMEGA_OK);
    omega_engine_play(eng);
    omega_engine_process(eng);

    omega_perf_ctx_t out{};
    omega_ctx_get(eng, &out);
    REQUIRE(out.scale.root == 9u);

    omega_engine_destroy(eng);
}

TEST_CASE("C API: null guards for perf context")
{
    REQUIRE(omega_ctx_set_scale(nullptr, nullptr) == OMEGA_ERR_INVALID);
    REQUIRE(omega_ctx_set_chord(nullptr, nullptr) == OMEGA_ERR_INVALID);
    REQUIRE(omega_ctx_set_transpose(nullptr, 0) == OMEGA_ERR_INVALID);
    REQUIRE(omega_ctx_set_velocity(nullptr, 0) == OMEGA_ERR_INVALID);
    REQUIRE(omega_ctx_set_chaos(nullptr, 0) == OMEGA_ERR_INVALID);
    REQUIRE(omega_ctx_set_groove(nullptr, 0, 0.0f) == OMEGA_ERR_INVALID);
    REQUIRE(omega_ctx_get(nullptr, nullptr) == OMEGA_ERR_INVALID);

    omega_engine_t* eng = omega_engine_create();
    REQUIRE(omega_ctx_set_scale(eng, nullptr) == OMEGA_ERR_INVALID);
    REQUIRE(omega_ctx_set_chord(eng, nullptr) == OMEGA_ERR_INVALID);
    REQUIRE(omega_ctx_get(eng, nullptr) == OMEGA_ERR_INVALID);
    omega_engine_destroy(eng);
}
