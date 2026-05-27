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

// ── Pattern read API (issue #28) ──────────────────────────────────────────────

TEST_CASE("C API: omega_pattern_event_count returns 0 for empty pattern")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_pattern_id_t id = omega_pattern_create(e, "p", 960u);
    uint32_t count = 99u;
    REQUIRE(omega_pattern_event_count(e, id, &count) == OMEGA_OK);
    REQUIRE(count == 0u);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_pattern_event_count counts inserted events")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_pattern_id_t id = omega_pattern_create(e, "p", 960u);
    omega_event_t ev0 = omega_make_note_on(0u, 1u, 0, 60, 100, 480u);
    omega_event_t ev1 = omega_make_note_on(480u, 1u, 0, 64, 100, 480u);
    REQUIRE(omega_pattern_add_event(e, id, &ev0) == OMEGA_OK);
    REQUIRE(omega_pattern_add_event(e, id, &ev1) == OMEGA_OK);

    uint32_t count = 0u;
    REQUIRE(omega_pattern_event_count(e, id, &count) == OMEGA_OK);
    REQUIRE(count == 2u);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_pattern_event_count null guards")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    omega_pattern_id_t id = omega_pattern_create(e, "p", 960u);
    uint32_t count = 0u;

    REQUIRE(omega_pattern_event_count(nullptr, id, &count) == OMEGA_ERR_INVALID);
    REQUIRE(omega_pattern_event_count(e, id, nullptr) == OMEGA_ERR_INVALID);
    REQUIRE(omega_pattern_event_count(e, OMEGA_PATTERN_INVALID, &count) == OMEGA_ERR_NOT_FOUND);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_pattern_event_at returns correct event by index")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_pattern_id_t id = omega_pattern_create(e, "p", 960u);
    omega_event_t ev0 = omega_make_note_on(0u, 1u, 0, 60, 100, 480u);
    omega_event_t ev1 = omega_make_note_on(480u, 1u, 0, 67, 80, 480u);
    REQUIRE(omega_pattern_add_event(e, id, &ev0) == OMEGA_OK);
    REQUIRE(omega_pattern_add_event(e, id, &ev1) == OMEGA_OK);

    omega_event_t out{};
    REQUIRE(omega_pattern_event_at(e, id, 0u, &out) == OMEGA_OK);
    REQUIRE(omega_event_note_pitch(&out) == 60u);

    REQUIRE(omega_pattern_event_at(e, id, 1u, &out) == OMEGA_OK);
    REQUIRE(omega_event_note_pitch(&out) == 67u);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_pattern_event_at returns NOT_FOUND for out-of-bounds index")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_pattern_id_t id = omega_pattern_create(e, "p", 960u);
    omega_event_t out{};
    REQUIRE(omega_pattern_event_at(e, id, 0u, &out) == OMEGA_ERR_NOT_FOUND);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_pattern_event_at null guards")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    omega_pattern_id_t id = omega_pattern_create(e, "p", 960u);
    omega_event_t out{};

    REQUIRE(omega_pattern_event_at(nullptr, id, 0u, &out) == OMEGA_ERR_INVALID);
    REQUIRE(omega_pattern_event_at(e, id, 0u, nullptr) == OMEGA_ERR_INVALID);
    REQUIRE(omega_pattern_event_at(e, OMEGA_PATTERN_INVALID, 0u, &out) == OMEGA_ERR_NOT_FOUND);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_pattern_event_count_filtered counts by channel")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_pattern_id_t id = omega_pattern_create(e, "p", 960u);
    omega_event_t ev_ch0 = omega_make_note_on(0u, 1u, 0, 60, 100, 480u);
    omega_event_t ev_ch1 = omega_make_note_on(0u, 1u, 1, 64, 100, 480u);
    omega_event_t ev_ch0b = omega_make_note_on(240u, 1u, 0, 67, 100, 240u);
    REQUIRE(omega_pattern_add_event(e, id, &ev_ch0) == OMEGA_OK);
    REQUIRE(omega_pattern_add_event(e, id, &ev_ch1) == OMEGA_OK);
    REQUIRE(omega_pattern_add_event(e, id, &ev_ch0b) == OMEGA_OK);

    uint32_t count = 0u;
    REQUIRE(omega_pattern_event_count_filtered(e, id, 0, 0xFF, &count) == OMEGA_OK);
    REQUIRE(count == 2u);

    REQUIRE(omega_pattern_event_count_filtered(e, id, 1, 0xFF, &count) == OMEGA_OK);
    REQUIRE(count == 1u);

    // wildcard channel
    REQUIRE(omega_pattern_event_count_filtered(e, id, 0xFF, 0xFF, &count) == OMEGA_OK);
    REQUIRE(count == 3u);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_pattern_event_count_filtered counts by payload_tag")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_pattern_id_t id = omega_pattern_create(e, "p", 960u);
    omega_event_t note = omega_make_note_on(0u, 1u, 0, 60, 100, 480u);
    omega_event_t cc = omega_make_cc(240u, 1u, 0, 7, 64);
    REQUIRE(omega_pattern_add_event(e, id, &note) == OMEGA_OK);
    REQUIRE(omega_pattern_add_event(e, id, &cc) == OMEGA_OK);

    uint32_t count = 0u;
    REQUIRE(omega_pattern_event_count_filtered(e, id, 0xFF, OMEGA_NOTE_ON, &count) == OMEGA_OK);
    REQUIRE(count == 1u);

    REQUIRE(omega_pattern_event_count_filtered(e, id, 0xFF, OMEGA_CC, &count) == OMEGA_OK);
    REQUIRE(count == 1u);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_pattern_event_count_filtered null guards")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    omega_pattern_id_t id = omega_pattern_create(e, "p", 960u);
    uint32_t count = 0u;

    REQUIRE(omega_pattern_event_count_filtered(nullptr, id, 0xFF, 0xFF, &count) ==
            OMEGA_ERR_INVALID);
    REQUIRE(omega_pattern_event_count_filtered(e, id, 0xFF, 0xFF, nullptr) == OMEGA_ERR_INVALID);
    REQUIRE(omega_pattern_event_count_filtered(e, OMEGA_PATTERN_INVALID, 0xFF, 0xFF, &count) ==
            OMEGA_ERR_NOT_FOUND);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_pattern_length returns correct length")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_pattern_id_t id = omega_pattern_create(e, "p", 1920u);
    omega_tick_t len = 0u;
    REQUIRE(omega_pattern_length(e, id, &len) == OMEGA_OK);
    REQUIRE(len == 1920u);

    // after set_length
    REQUIRE(omega_pattern_set_length(e, id, 3840u) == OMEGA_OK);
    REQUIRE(omega_pattern_length(e, id, &len) == OMEGA_OK);
    REQUIRE(len == 3840u);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_pattern_length null guards")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    omega_pattern_id_t id = omega_pattern_create(e, "p", 960u);
    omega_tick_t len = 0u;

    REQUIRE(omega_pattern_length(nullptr, id, &len) == OMEGA_ERR_INVALID);
    REQUIRE(omega_pattern_length(e, id, nullptr) == OMEGA_ERR_INVALID);
    REQUIRE(omega_pattern_length(e, OMEGA_PATTERN_INVALID, &len) == OMEGA_ERR_NOT_FOUND);

    omega_engine_destroy(e);
}

// ── omega_pattern_replace_event (issue #30) ───────────────────────────────────

TEST_CASE("C API: omega_pattern_replace_event null guards")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    omega_pattern_id_t id = omega_pattern_create(e, "p", 960u);
    omega_event_t ev = omega_make_note_on(0u, 1u, 0, 60, 100, 0);

    REQUIRE(omega_pattern_replace_event(nullptr, id, 0u, &ev) == OMEGA_ERR_INVALID);
    REQUIRE(omega_pattern_replace_event(e, id, 0u, nullptr) == OMEGA_ERR_INVALID);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_pattern_replace_event returns OMEGA_OK when enqueued")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_pattern_id_t id = omega_pattern_create(e, "p", 960u);
    omega_event_t ev = omega_make_note_on(0u, 1u, 0, 60, 100, 480u);
    REQUIRE(omega_pattern_add_event(e, id, &ev) == OMEGA_OK);

    omega_event_t repl = omega_make_note_on(0u, 1u, 0, 62, 90, 480u);
    REQUIRE(omega_pattern_replace_event(e, id, 0u, &repl) == OMEGA_OK);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_pattern_replace_event replaces event on next process")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_pattern_id_t id = omega_pattern_create(e, "p", 960u);
    omega_event_t ev = omega_make_note_on(0u, 1u, 0, 60, 100, 480u);
    REQUIRE(omega_pattern_add_event(e, id, &ev) == OMEGA_OK);

    // Replace C4 with D4.
    omega_event_t repl = omega_make_note_on(0u, 1u, 0, 62, 90, 480u);
    REQUIRE(omega_pattern_replace_event(e, id, 0u, &repl) == OMEGA_OK);

    // process() drains the queue even in stopped state.
    omega_engine_process(e);

    omega_event_t out{};
    REQUIRE(omega_pattern_event_at(e, id, 0u, &out) == OMEGA_OK);
    REQUIRE(omega_event_note_pitch(&out) == 62u);
    REQUIRE(omega_event_note_velocity(&out) == 90u);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_pattern_replace_event with out-of-bounds index is silent no-op")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_pattern_id_t id = omega_pattern_create(e, "p", 960u);
    omega_event_t ev = omega_make_note_on(0u, 1u, 0, 60, 100, 480u);
    REQUIRE(omega_pattern_add_event(e, id, &ev) == OMEGA_OK);

    omega_event_t repl = omega_make_note_on(0u, 1u, 0, 62, 90, 480u);
    REQUIRE(omega_pattern_replace_event(e, id, 99u, &repl) == OMEGA_OK);  // way out of bounds
    omega_engine_process(e);

    // Original event must be unchanged.
    omega_event_t out{};
    REQUIRE(omega_pattern_event_at(e, id, 0u, &out) == OMEGA_OK);
    REQUIRE(omega_event_note_pitch(&out) == 60u);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_pattern_replace_event reorders events when tick changes")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_pattern_id_t id = omega_pattern_create(e, "p", 1920u);
    // C4 at tick 0, E4 at tick 480
    omega_event_t ev0 = omega_make_note_on(0u, 1u, 0, 60, 100, 240u);
    omega_event_t ev1 = omega_make_note_on(480u, 1u, 0, 64, 100, 240u);
    REQUIRE(omega_pattern_add_event(e, id, &ev0) == OMEGA_OK);
    REQUIRE(omega_pattern_add_event(e, id, &ev1) == OMEGA_OK);

    // Replace index 0 (C4@0) with G4@960 — tick moves past E4@480
    omega_event_t repl = omega_make_note_on(960u, 1u, 0, 67, 80, 240u);
    REQUIRE(omega_pattern_replace_event(e, id, 0u, &repl) == OMEGA_OK);
    omega_engine_process(e);

    // After re-sort: index 0 = E4@480, index 1 = G4@960
    omega_event_t out0{};
    omega_event_t out1{};
    REQUIRE(omega_pattern_event_at(e, id, 0u, &out0) == OMEGA_OK);
    REQUIRE(omega_pattern_event_at(e, id, 1u, &out1) == OMEGA_OK);
    REQUIRE(omega_event_note_pitch(&out0) == 64u);  // E4
    REQUIRE(out0.tick == 480u);
    REQUIRE(omega_event_note_pitch(&out1) == 67u);  // G4
    REQUIRE(out1.tick == 960u);

    omega_engine_destroy(e);
}
