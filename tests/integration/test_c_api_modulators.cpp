#include <omega/omega.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

// ── LFO C API ────────────────────────────────────────────────────────────────

TEST_CASE("C API: omega_lfo_create registers with engine; channel is written each cycle")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    omega_mod_channel_t ch = omega_mod_register(eng, "lfo.vel", 0.0f);
    REQUIRE(ch != OMEGA_MOD_INVALID);

    // Sawtooth at rate=1 beat, depth=1, offset=0
    // At tick=0: wave = 2*0 - 1 = -1.0
    omega_lfo_t* lfo = omega_lfo_create(eng, ch, OMEGA_LFO_SAWTOOTH, 1.0f, 1.0f, 0.0f);
    REQUIRE(lfo != nullptr);

    REQUIRE(omega_engine_play(eng) == OMEGA_OK);
    omega_engine_process(eng);  // tick=0 → wave=-1

    float val = omega_mod_get(eng, ch);
    REQUIRE(val == Approx(-1.0f).margin(1e-5f));

    omega_lfo_destroy(eng, lfo);
    omega_engine_destroy(eng);
}

TEST_CASE("C API: omega_lfo_set_shape changes waveform")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    omega_mod_channel_t ch = omega_mod_register(eng, "s", 0.0f);
    omega_lfo_t* lfo = omega_lfo_create(eng, ch, OMEGA_LFO_SAWTOOTH, 1.0f, 1.0f, 0.0f);
    REQUIRE(lfo != nullptr);

    REQUIRE(omega_engine_play(eng) == OMEGA_OK);
    omega_engine_process(eng);
    float saw_val = omega_mod_get(eng, ch);  // sawtooth at tick=0

    omega_lfo_set_shape(lfo, OMEGA_LFO_SQUARE);
    omega_engine_process(eng);  // same tick (no clock advance) — same phase
    float sq_val = omega_mod_get(eng, ch);

    // Sawtooth at phase=0 → -1; square at phase=0 → +1
    REQUIRE(saw_val == Approx(-1.0f).margin(1e-5f));
    REQUIRE(sq_val == Approx(1.0f).margin(1e-5f));

    omega_lfo_destroy(eng, lfo);
    omega_engine_destroy(eng);
}

TEST_CASE("C API: omega_lfo null guards")
{
    omega_lfo_create(nullptr, 0u, OMEGA_LFO_SINE, 1.0f, 1.0f, 0.0f);  // must not crash
    omega_lfo_set_shape(nullptr, OMEGA_LFO_SINE);                     // must not crash
    omega_lfo_set_rate(nullptr, 1.0f);
    omega_lfo_set_depth(nullptr, 1.0f);
    omega_lfo_set_offset(nullptr, 0.0f);
    omega_lfo_destroy(nullptr, nullptr);
    REQUIRE(true);
}

TEST_CASE("C API: omega_lfo_create returns NULL for zero/negative rate")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    omega_mod_channel_t ch = omega_mod_register(eng, "r", 0.0f);
    REQUIRE(omega_lfo_create(eng, ch, OMEGA_LFO_SINE, 0.0f, 1.0f, 0.0f) == nullptr);
    REQUIRE(omega_lfo_create(eng, ch, OMEGA_LFO_SINE, -1.0f, 1.0f, 0.0f) == nullptr);

    omega_engine_destroy(eng);
}

// ── Envelope C API ────────────────────────────────────────────────────────────

TEST_CASE("C API: omega_envelope_create and add_point; interpolates correctly")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    omega_mod_channel_t ch = omega_mod_register(eng, "env", 0.0f);
    omega_envelope_t* env = omega_envelope_create(eng, ch, 0 /*no loop*/);
    REQUIRE(env != nullptr);

    // Ramp 0→1 over one beat (480 ticks)
    REQUIRE(omega_envelope_add_point(env, 0u, 0.0f) == OMEGA_OK);
    REQUIRE(omega_envelope_add_point(env, 480u, 1.0f) == OMEGA_OK);

    REQUIRE(omega_engine_play(eng) == OMEGA_OK);
    omega_engine_process(eng);  // tick=0 → 0.0
    REQUIRE(omega_mod_get(eng, ch) == Approx(0.0f).margin(1e-5f));

    omega_envelope_destroy(eng, env);
    omega_engine_destroy(eng);
}

TEST_CASE("C API: omega_envelope_add_point overflow returns error")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    omega_mod_channel_t ch = omega_mod_register(eng, "eo", 0.0f);
    omega_envelope_t* env = omega_envelope_create(eng, ch, 0);
    REQUIRE(env != nullptr);

    for (uint32_t i = 0u; i < 64u; ++i)
    {
        REQUIRE(omega_envelope_add_point(env, static_cast<uint64_t>(i), 0.0f) == OMEGA_OK);
    }
    REQUIRE(omega_envelope_add_point(env, 999u, 1.0f) == OMEGA_ERR_INVALID);

    omega_envelope_destroy(eng, env);
    omega_engine_destroy(eng);
}

TEST_CASE("C API: omega_envelope null guards")
{
    REQUIRE(omega_envelope_create(nullptr, 0u, 0) == nullptr);
    REQUIRE(omega_envelope_add_point(nullptr, 0u, 0.0f) == OMEGA_ERR_INVALID);
    omega_envelope_clear(nullptr);
    omega_envelope_destroy(nullptr, nullptr);
    REQUIRE(true);
}

// ── Step modulator C API ──────────────────────────────────────────────────────

TEST_CASE("C API: omega_step_mod_create; steps drive channel value")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    omega_mod_channel_t ch = omega_mod_register(eng, "sm", 0.0f);
    omega_step_mod_t* sm = omega_step_mod_create(eng, ch, 480u /*1 beat*/, 1 /*loop*/);
    REQUIRE(sm != nullptr);

    REQUIRE(omega_step_mod_set_step(sm, 0u, 0.2f) == OMEGA_OK);
    REQUIRE(omega_step_mod_set_step(sm, 1u, 0.8f) == OMEGA_OK);

    REQUIRE(omega_engine_play(eng) == OMEGA_OK);
    omega_engine_process(eng);  // tick=0 → step 0 → 0.2
    REQUIRE(omega_mod_get(eng, ch) == Approx(0.2f).margin(1e-6f));

    omega_step_mod_destroy(eng, sm);
    omega_engine_destroy(eng);
}

TEST_CASE("C API: omega_step_mod set_step returns error for out-of-range index")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    omega_mod_channel_t ch = omega_mod_register(eng, "sme", 0.0f);
    omega_step_mod_t* sm = omega_step_mod_create(eng, ch, 480u, 0);
    REQUIRE(sm != nullptr);

    REQUIRE(omega_step_mod_set_step(sm, 63u, 1.0f) == OMEGA_OK);           // last valid
    REQUIRE(omega_step_mod_set_step(sm, 64u, 1.0f) == OMEGA_ERR_INVALID);  // out of range

    omega_step_mod_destroy(eng, sm);
    omega_engine_destroy(eng);
}

TEST_CASE("C API: omega_step_mod null guards")
{
    REQUIRE(omega_step_mod_create(nullptr, 0u, 480u, 0) == nullptr);
    REQUIRE(omega_step_mod_create(nullptr, 0u, 0u, 0) == nullptr);  // step_ticks=0 invalid
    REQUIRE(omega_step_mod_set_step(nullptr, 0u, 0.0f) == OMEGA_ERR_INVALID);
    omega_step_mod_set_count(nullptr, 4u);
    omega_step_mod_destroy(nullptr, nullptr);
    REQUIRE(true);
}
