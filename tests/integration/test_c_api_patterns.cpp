#include <omega/omega.h>
#include <omega/test/capturing_sink.h>

#include <catch2/catch_test_macros.hpp>

// ── omega_pattern_create ──────────────────────────────────────────────────────

TEST_CASE("C API: omega_pattern_create returns valid ID")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_pattern_id_t id = omega_pattern_create(e, "bass", 960u);
    REQUIRE(id != OMEGA_PATTERN_INVALID);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_pattern_create with NULL engine returns OMEGA_PATTERN_INVALID")
{
    REQUIRE(omega_pattern_create(nullptr, "x", 480u) == OMEGA_PATTERN_INVALID);
}

TEST_CASE("C API: omega_pattern_create with NULL name succeeds")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_pattern_id_t id = omega_pattern_create(e, nullptr, 480u);
    REQUIRE(id != OMEGA_PATTERN_INVALID);

    omega_engine_destroy(e);
}

// ── omega_pattern_destroy ─────────────────────────────────────────────────────

TEST_CASE("C API: omega_pattern_destroy returns OMEGA_OK for valid ID")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_pattern_id_t id = omega_pattern_create(e, "tmp", 480u);
    REQUIRE(id != OMEGA_PATTERN_INVALID);
    REQUIRE(omega_pattern_destroy(e, id) == OMEGA_OK);

    // Operations on the destroyed ID now return NOT_FOUND
    omega_event_t ev = omega_make_note_on(0u, 1u, 0, 60, 100, 0);
    REQUIRE(omega_pattern_add_event(e, id, &ev) == OMEGA_ERR_NOT_FOUND);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_pattern_destroy with NULL engine returns ERR_INVALID")
{
    REQUIRE(omega_pattern_destroy(nullptr, 1u) == OMEGA_ERR_INVALID);
}

// ── omega_pattern_add_event ───────────────────────────────────────────────────

TEST_CASE("C API: omega_pattern_add_event inserts event successfully")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_pattern_id_t id = omega_pattern_create(e, "melody", 960u);
    omega_event_t ev = omega_make_note_on(240u, 1u, 0, 60, 100, 0);
    REQUIRE(omega_pattern_add_event(e, id, &ev) == OMEGA_OK);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_pattern_add_event null guards")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    omega_pattern_id_t id = omega_pattern_create(e, "p", 480u);
    omega_event_t ev = omega_make_note_on(0u, 1u, 0, 60, 100, 0);

    REQUIRE(omega_pattern_add_event(nullptr, id, &ev) == OMEGA_ERR_INVALID);
    REQUIRE(omega_pattern_add_event(e, id, nullptr) == OMEGA_ERR_INVALID);
    REQUIRE(omega_pattern_add_event(e, OMEGA_PATTERN_INVALID, &ev) == OMEGA_ERR_NOT_FOUND);

    omega_engine_destroy(e);
}

// ── omega_pattern_set_length ──────────────────────────────────────────────────

TEST_CASE("C API: omega_pattern_set_length returns OMEGA_OK for valid ID")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_pattern_id_t id = omega_pattern_create(e, "p", 480u);
    REQUIRE(omega_pattern_set_length(e, id, 1920u) == OMEGA_OK);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_pattern_set_length null guards")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    omega_pattern_id_t id = omega_pattern_create(e, "p", 480u);

    REQUIRE(omega_pattern_set_length(nullptr, id, 960u) == OMEGA_ERR_INVALID);
    REQUIRE(omega_pattern_set_length(e, OMEGA_PATTERN_INVALID, 960u) == OMEGA_ERR_NOT_FOUND);

    omega_engine_destroy(e);
}
