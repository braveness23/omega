#include <omega/omega.h>
#include <omega/types.h>

#include <catch2/catch_test_macros.hpp>
#include <cstring>

TEST_CASE("Event struct is 24 bytes")
{
    static_assert(sizeof(omega::Event) == 24, "Event must be exactly 24 bytes");
    REQUIRE(sizeof(omega::Event) == 24u);
}

TEST_CASE("omega_version returns 1.0.0")
{
    omega_version_t v = omega_version();
    REQUIRE(v.major == OMEGA_VERSION_MAJOR);
    REQUIRE(v.minor == OMEGA_VERSION_MINOR);
    REQUIRE(v.patch == OMEGA_VERSION_PATCH);
    REQUIRE(v.major == 1);
    REQUIRE(v.minor == 0);
    REQUIRE(v.patch == 0);
}

TEST_CASE("omega_status_string returns non-null strings for all known codes")
{
    REQUIRE(omega_status_string(OMEGA_OK) != nullptr);
    REQUIRE(omega_status_string(OMEGA_ERR_INVALID) != nullptr);
    REQUIRE(omega_status_string(OMEGA_ERR_NOMEM) != nullptr);
    REQUIRE(omega_status_string(OMEGA_ERR_NOT_FOUND) != nullptr);
    REQUIRE(omega_status_string(OMEGA_ERR_QUEUE_FULL) != nullptr);
    REQUIRE(omega_status_string(OMEGA_ERR_UNSUPPORTED) != nullptr);
}

TEST_CASE("omega_status_string(OMEGA_OK) is \"ok\"")
{
    REQUIRE(std::string(omega_status_string(OMEGA_OK)) == "ok");
}

TEST_CASE("omega_make_note_on produces correct layout")
{
    omega_event_t e = omega_make_note_on(100u, 1u, 2u, 60u, 100u, 480u);
    REQUIRE(e.tick == 100u);
    REQUIRE(e.sink_id == 1u);
    REQUIRE(e.channel == 2u);
    REQUIRE(e.payload_tag == OMEGA_NOTE_ON);
    REQUIRE(e.data[0] == 60u);  /* note */
    REQUIRE(e.data[1] == 100u); /* velocity */

    uint32_t dur = 0;
    std::memcpy(&dur, &e.data[2], sizeof(dur));
    REQUIRE(dur == 480u);
}

TEST_CASE("omega_make_cc produces correct layout")
{
    omega_event_t e = omega_make_cc(200u, 0u, 0u, 7u, 64u);
    REQUIRE(e.tick == 200u);
    REQUIRE(e.payload_tag == OMEGA_CC);
    REQUIRE(e.data[0] == 7u);  /* controller */
    REQUIRE(e.data[1] == 64u); /* value */
}

TEST_CASE("omega_make_program produces correct layout")
{
    omega_event_t e = omega_make_program(0u, 0u, 0u, 42u);
    REQUIRE(e.payload_tag == OMEGA_PROGRAM);
    REQUIRE(e.data[0] == 42u);
}

TEST_CASE("omega_make_note_on reserved bytes are zero")
{
    omega_event_t e = omega_make_note_on(0u, 0u, 0u, 60u, 64u, 240u);
    REQUIRE(e.reserved[0] == 0u);
    REQUIRE(e.reserved[1] == 0u);
}

// ── Event field accessors (issue #29) ─────────────────────────────────────────

TEST_CASE("omega_event_note_pitch reads data[0]")
{
    omega_event_t e = omega_make_note_on(0u, 0u, 0u, 72u, 100u, 0u);
    REQUIRE(omega_event_note_pitch(&e) == 72u);
}

TEST_CASE("omega_event_note_velocity reads data[1]")
{
    omega_event_t e = omega_make_note_on(0u, 0u, 0u, 60u, 99u, 0u);
    REQUIRE(omega_event_note_velocity(&e) == 99u);
}

TEST_CASE("omega_event_note_duration reads data[2-5]")
{
    omega_event_t e = omega_make_note_on(0u, 0u, 0u, 60u, 100u, 960u);
    REQUIRE(omega_event_note_duration(&e) == 960u);
}

TEST_CASE("omega_event_set_pitch mutates pitch")
{
    omega_event_t e = omega_make_note_on(0u, 0u, 0u, 60u, 100u, 480u);
    REQUIRE(omega_event_set_pitch(&e, 64u) == OMEGA_OK);
    REQUIRE(omega_event_note_pitch(&e) == 64u);
    // other fields unaffected
    REQUIRE(omega_event_note_velocity(&e) == 100u);
    REQUIRE(omega_event_note_duration(&e) == 480u);
}

TEST_CASE("omega_event_set_velocity mutates velocity")
{
    omega_event_t e = omega_make_note_on(0u, 0u, 0u, 60u, 100u, 480u);
    REQUIRE(omega_event_set_velocity(&e, 127u) == OMEGA_OK);
    REQUIRE(omega_event_note_velocity(&e) == 127u);
    REQUIRE(omega_event_note_pitch(&e) == 60u);
}

TEST_CASE("omega_event_set_duration mutates duration")
{
    omega_event_t e = omega_make_note_on(0u, 0u, 0u, 60u, 100u, 480u);
    REQUIRE(omega_event_set_duration(&e, 240u) == OMEGA_OK);
    REQUIRE(omega_event_note_duration(&e) == 240u);
    REQUIRE(omega_event_note_pitch(&e) == 60u);
    REQUIRE(omega_event_note_velocity(&e) == 100u);
}

TEST_CASE("omega_event_set_pitch returns ERR_INVALID for NULL")
{
    REQUIRE(omega_event_set_pitch(nullptr, 60u) == OMEGA_ERR_INVALID);
}

TEST_CASE("omega_event_set_velocity returns ERR_INVALID for NULL")
{
    REQUIRE(omega_event_set_velocity(nullptr, 100u) == OMEGA_ERR_INVALID);
}

TEST_CASE("omega_event_set_duration returns ERR_INVALID for NULL")
{
    REQUIRE(omega_event_set_duration(nullptr, 480u) == OMEGA_ERR_INVALID);
}

TEST_CASE("omega_event_cc_number and omega_event_cc_value read CC fields")
{
    omega_event_t e = omega_make_cc(0u, 0u, 0u, 7u, 63u);
    REQUIRE(omega_event_cc_number(&e) == 7u);
    REQUIRE(omega_event_cc_value(&e) == 63u);
}

TEST_CASE("omega_event_set_pitch and omega_event_set_velocity work on NOTE_OFF events")
{
    // NOTE_OFF uses the same data[0]/data[1] layout
    omega_event_t e{};
    e.payload_tag = OMEGA_NOTE_OFF;
    e.data[0] = 60u;
    e.data[1] = 0u;
    REQUIRE(omega_event_note_pitch(&e) == 60u);
    REQUIRE(omega_event_note_velocity(&e) == 0u);
    omega_event_set_pitch(&e, 62u);
    REQUIRE(omega_event_note_pitch(&e) == 62u);
}
