#include <omega/omega.h>

#include <catch2/catch_test_macros.hpp>

// ── sync_external_* C API ─────────────────────────────────────────────────────

TEST_CASE("C API: omega_sync_external_stop stops a playing engine")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    REQUIRE(omega_engine_play(eng) == OMEGA_OK);
    omega_engine_process(eng);
    REQUIRE(omega_engine_transport_state(eng) == OMEGA_TRANSPORT_PLAYING);

    omega_sync_external_stop(eng);

    REQUIRE(omega_engine_transport_state(eng) == OMEGA_TRANSPORT_STOPPED);

    omega_engine_destroy(eng);
}

TEST_CASE("C API: omega_sync_external_locate repositions transport")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    omega_sync_external_locate(eng, 480u);

    // omega_engine_position_tick reads last_position_ns_ directly (not the snap cache).
    // ticks_to_ns / ns_to_ticks round-trip may lose 1 tick due to integer math.
    const omega_tick_t tick = omega_engine_position_tick(eng);
    REQUIRE(tick >= 479u);
    REQUIRE(tick <= 481u);

    omega_engine_destroy(eng);
}

TEST_CASE("C API: omega_sync_external_play starts a stopped engine")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    REQUIRE(omega_engine_transport_state(eng) == OMEGA_TRANSPORT_STOPPED);

    omega_sync_external_play(eng);

    REQUIRE(omega_engine_transport_state(eng) == OMEGA_TRANSPORT_PLAYING);

    omega_engine_stop(eng);
    omega_engine_process(eng);
    omega_engine_destroy(eng);
}

TEST_CASE("C API: sync_external_* null guards")
{
    omega_sync_external_tempo(nullptr, 120'000u);
    omega_sync_external_play(nullptr);
    omega_sync_external_stop(nullptr);
    omega_sync_external_locate(nullptr, 0u);
    REQUIRE(true);  // must not crash
}

TEST_CASE("C API: omega_clock_master_create returns NULL for null midi_out")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    REQUIRE(omega_clock_master_create(eng, nullptr) == nullptr);

    omega_engine_destroy(eng);
}

TEST_CASE("C API: omega_clock_slave_create and destroy")
{
    omega_engine_t* eng = omega_engine_create();
    REQUIRE(eng != nullptr);

    omega_clock_slave_t* slave = omega_clock_slave_create(eng);
    REQUIRE(slave != nullptr);

    omega_clock_slave_destroy(eng, slave);
    omega_engine_destroy(eng);
}

TEST_CASE("C API: omega_clock_slave null guards")
{
    REQUIRE(omega_clock_slave_create(nullptr) == nullptr);
    omega_clock_slave_destroy(nullptr, nullptr);  // must not crash
    REQUIRE(true);
}

TEST_CASE("C API: omega_clock_slave_input create and destroy")
{
    /* No MIDI port available on CI; just verify lifecycle is safe. */
    omega_clock_slave_input_t* input = omega_clock_slave_input_create(nullptr);
    omega_clock_slave_input_destroy(input);  // safe even if null
    REQUIRE(true);
}
