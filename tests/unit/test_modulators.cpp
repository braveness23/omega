#include <omega/engine.h>
#include <omega/modulators.h>
#include <omega/omega.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>
#include <omega/types.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace omega;
using Catch::Approx;

// ── LfoSource ─────────────────────────────────────────────────────────────────

TEST_CASE("LfoSource: sine at tick 0 writes 0.0 (sin(0) = 0)")
{
    MockClock clock;
    Engine eng{&clock};

    uint32_t ch = eng.modulation_bus().register_channel("lfo", 0.0f);
    LfoSource lfo{ch, LfoSource::Shape::Sine, 1.0f /*rate=1 beat*/, 1.0f /*depth*/};

    eng.add_source(&lfo, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();  // tick 0 — advance called; sin(0) = 0

    REQUIRE(eng.modulation_bus().get(ch) == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("LfoSource: sine at quarter-period (tick = PPQN/4) is near +1")
{
    MockClock clock;
    Engine eng{&clock};

    uint32_t ch = eng.modulation_bus().register_channel("lfo", 0.0f);
    LfoSource lfo{ch, LfoSource::Shape::Sine, 1.0f, 1.0f};

    eng.add_source(&lfo, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();

    clock.advance_ticks(PPQN / 4);  // phase = 0.25 → sin(π/2) ≈ 1
    eng.process();

    REQUIRE(eng.modulation_bus().get(ch) == Approx(1.0f).margin(0.01f));
}

TEST_CASE("LfoSource: depth and offset shift output")
{
    MockClock clock;
    Engine eng{&clock};

    uint32_t ch = eng.modulation_bus().register_channel("v", 0.0f);
    // Sawtooth at half-period (phase=0.5) → wave=0; output = offset + depth*0 = 0.5
    LfoSource lfo{ch, LfoSource::Shape::Sawtooth, 1.0f, 0.3f /*depth*/, 0.5f /*offset*/};

    eng.add_source(&lfo, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();

    clock.advance_ticks(PPQN / 2);  // phase=0.5 → wave = 2*0.5-1 = 0
    eng.process();

    REQUIRE(eng.modulation_bus().get(ch) == Approx(0.5f).margin(1e-5f));
}

TEST_CASE("LfoSource: square wave above/below 0.5 phase threshold")
{
    MockClock clock;
    Engine eng{&clock};

    uint32_t ch = eng.modulation_bus().register_channel("sq", 0.0f);
    LfoSource lfo{ch, LfoSource::Shape::Square, 1.0f, 1.0f};

    eng.add_source(&lfo, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});

    // tick = PPQN/4 → phase=0.25 < 0.5 → +1
    clock.advance_ticks(PPQN / 4);
    eng.process();
    REQUIRE(eng.modulation_bus().get(ch) == Approx(1.0f).margin(1e-6f));

    // tick = 3*PPQN/4 → phase=0.75 >= 0.5 → -1
    clock.advance_ticks(PPQN / 2);
    eng.process();
    REQUIRE(eng.modulation_bus().get(ch) == Approx(-1.0f).margin(1e-6f));
}

TEST_CASE("LfoSource: triangle wave at phase 0.5 is +1")
{
    MockClock clock;
    Engine eng{&clock};

    uint32_t ch = eng.modulation_bus().register_channel("tri", 0.0f);
    LfoSource lfo{ch, LfoSource::Shape::Triangle, 1.0f, 1.0f};

    eng.add_source(&lfo, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();

    clock.advance_ticks(PPQN / 2);  // phase=0.5 → 1 - 2|2*0.5 - 1| = 1 - 0 = 1
    eng.process();

    REQUIRE(eng.modulation_bus().get(ch) == Approx(1.0f).margin(1e-5f));
}

TEST_CASE("LfoSource: set_shape updates waveform")
{
    MockClock clock;
    Engine eng{&clock};

    uint32_t ch = eng.modulation_bus().register_channel("sh", 0.0f);
    LfoSource lfo{ch, LfoSource::Shape::Sine, 1.0f, 1.0f};

    eng.add_source(&lfo, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});

    clock.advance_ticks(PPQN / 4);
    eng.process();
    const float sine_val = eng.modulation_bus().get(ch);

    lfo.set_shape(LfoSource::Shape::Sawtooth);
    eng.process();  // same tick, different shape
    const float saw_val = eng.modulation_bus().get(ch);

    REQUIRE(sine_val != Approx(saw_val).margin(0.01f));
}

// ── EnvelopeSource ────────────────────────────────────────────────────────────

TEST_CASE("EnvelopeSource: holds first value before first breakpoint")
{
    MockClock clock;
    Engine eng{&clock};

    uint32_t ch = eng.modulation_bus().register_channel("env", 0.0f);
    EnvelopeSource env{ch, false};
    env.add_point(480u, 0.0f);  // first point at tick 480
    env.add_point(960u, 1.0f);

    eng.add_source(&env, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();  // tick=0, before first point → holds 0.0

    REQUIRE(eng.modulation_bus().get(ch) == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("EnvelopeSource: interpolates between breakpoints")
{
    MockClock clock;
    Engine eng{&clock};

    uint32_t ch = eng.modulation_bus().register_channel("env2", 0.0f);
    EnvelopeSource env{ch, false};
    env.add_point(0u, 0.0f);
    env.add_point(480u, 1.0f);  // ramp 0→1 over one beat

    eng.add_source(&env, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();

    clock.advance_ticks(240);  // midpoint → value ≈ 0.5
    eng.process();

    REQUIRE(eng.modulation_bus().get(ch) == Approx(0.5f).margin(1e-4f));
}

TEST_CASE("EnvelopeSource: holds last value past final breakpoint")
{
    MockClock clock;
    Engine eng{&clock};

    uint32_t ch = eng.modulation_bus().register_channel("env3", 0.0f);
    EnvelopeSource env{ch, false};
    env.add_point(0u, 0.5f);
    env.add_point(480u, 1.0f);

    eng.add_source(&env, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();

    clock.advance_ticks(1000);  // past 480 → holds 1.0
    eng.process();

    REQUIRE(eng.modulation_bus().get(ch) == Approx(1.0f).margin(1e-6f));
}

TEST_CASE("EnvelopeSource: looping wraps period correctly")
{
    MockClock clock;
    Engine eng{&clock};

    uint32_t ch = eng.modulation_bus().register_channel("envl", 0.0f);
    EnvelopeSource env{ch, true};
    env.add_point(0u, 0.0f);
    env.add_point(480u, 1.0f);  // period = 480 ticks

    eng.add_source(&env, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();

    // tick=480 wraps to 0 → value = 0.0 (at the start of the loop)
    clock.advance_ticks(480);
    eng.process();

    REQUIRE(eng.modulation_bus().get(ch) == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("EnvelopeSource: add_point returns false when full")
{
    EnvelopeSource env{0u, false};
    for (uint32_t i = 0u; i < EnvelopeSource::MAX_POINTS; ++i)
    {
        REQUIRE(env.add_point(static_cast<uint64_t>(i), 0.0f));
    }
    REQUIRE_FALSE(env.add_point(99u, 0.0f));
}

// ── StepModulatorSource ───────────────────────────────────────────────────────

TEST_CASE("StepModulatorSource: selects step 0 at tick 0")
{
    MockClock clock;
    Engine eng{&clock};

    uint32_t ch = eng.modulation_bus().register_channel("sm", 0.0f);
    StepModulatorSource sm{ch, 480u /*step_ticks=1 beat*/, false};
    sm.set_step(0u, 0.25f);
    sm.set_step(1u, 0.75f);

    eng.add_source(&sm, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();

    REQUIRE(eng.modulation_bus().get(ch) == Approx(0.25f).margin(1e-6f));
}

TEST_CASE("StepModulatorSource: advances to step 1 after step_ticks")
{
    MockClock clock;
    Engine eng{&clock};

    uint32_t ch = eng.modulation_bus().register_channel("sm2", 0.0f);
    StepModulatorSource sm{ch, 480u, false};
    sm.set_step(0u, 0.1f);
    sm.set_step(1u, 0.9f);

    eng.add_source(&sm, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();

    clock.advance_ticks(480);
    eng.process();

    REQUIRE(eng.modulation_bus().get(ch) == Approx(0.9f).margin(1e-6f));
}

TEST_CASE("StepModulatorSource: holds last step value when not looping")
{
    MockClock clock;
    Engine eng{&clock};

    uint32_t ch = eng.modulation_bus().register_channel("sm3", 0.0f);
    StepModulatorSource sm{ch, 480u, false};
    sm.set_step(0u, 0.2f);
    sm.set_step(1u, 0.8f);

    eng.add_source(&sm, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();

    clock.advance_ticks(5000);  // way past 2 steps
    eng.process();

    REQUIRE(eng.modulation_bus().get(ch) == Approx(0.8f).margin(1e-6f));
}

TEST_CASE("StepModulatorSource: loops back to step 0 after all steps")
{
    MockClock clock;
    Engine eng{&clock};

    uint32_t ch = eng.modulation_bus().register_channel("sml", 0.0f);
    StepModulatorSource sm{ch, 480u, true};
    sm.set_step(0u, 0.1f);
    sm.set_step(1u, 0.5f);
    sm.set_step(2u, 0.9f);

    eng.add_source(&sm, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();

    clock.advance_ticks(480 * 3);  // 3 steps → wraps to step 0
    eng.process();

    REQUIRE(eng.modulation_bus().get(ch) == Approx(0.1f).margin(1e-6f));
}

TEST_CASE("StepModulatorSource: set_count limits active steps")
{
    MockClock clock;
    Engine eng{&clock};

    uint32_t ch = eng.modulation_bus().register_channel("smc", 0.0f);
    StepModulatorSource sm{ch, 480u, true};
    sm.set_step(0u, 0.1f);
    sm.set_step(1u, 0.5f);
    sm.set_step(2u, 0.9f);
    sm.set_count(2u);  // only 2 steps active; step 2 ignored

    eng.add_source(&sm, OMEGA_SOURCE_PRIORITY_MODULATOR);
    eng.enqueue(TransportCmd{TransportAction::PLAY, 0u});
    eng.process();

    clock.advance_ticks(480 * 2);  // 2 steps → wraps to step 0 (count=2)
    eng.process();

    REQUIRE(eng.modulation_bus().get(ch) == Approx(0.1f).margin(1e-6f));
}
