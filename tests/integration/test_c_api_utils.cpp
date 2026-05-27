#include <omega/omega.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstring>

// ── omega_midi_note_name ───────────────────────────────────────────────────────

TEST_CASE("omega_midi_note_name: middle C (60) is C4")
{
    std::array<char, 16> buf{};
    omega_midi_note_name(60, buf.data(), buf.size());
    REQUIRE(std::strcmp(buf.data(), "C4") == 0);
}

TEST_CASE("omega_midi_note_name: A4 (69)")
{
    std::array<char, 16> buf{};
    omega_midi_note_name(69, buf.data(), buf.size());
    REQUIRE(std::strcmp(buf.data(), "A4") == 0);
}

TEST_CASE("omega_midi_note_name: sharp notes")
{
    std::array<char, 16> buf{};

    omega_midi_note_name(61, buf.data(), buf.size());
    REQUIRE(std::strcmp(buf.data(), "C#4") == 0);

    omega_midi_note_name(63, buf.data(), buf.size());
    REQUIRE(std::strcmp(buf.data(), "D#4") == 0);

    omega_midi_note_name(66, buf.data(), buf.size());
    REQUIRE(std::strcmp(buf.data(), "F#4") == 0);

    omega_midi_note_name(68, buf.data(), buf.size());
    REQUIRE(std::strcmp(buf.data(), "G#4") == 0);

    omega_midi_note_name(70, buf.data(), buf.size());
    REQUIRE(std::strcmp(buf.data(), "A#4") == 0);
}

TEST_CASE("omega_midi_note_name: lowest and highest standard notes")
{
    std::array<char, 16> buf{};

    omega_midi_note_name(0, buf.data(), buf.size());
    REQUIRE(std::strcmp(buf.data(), "C-1") == 0);

    omega_midi_note_name(127, buf.data(), buf.size());
    REQUIRE(std::strcmp(buf.data(), "G9") == 0);
}

TEST_CASE("omega_midi_note_name: null or zero-size buffer is safe")
{
    std::array<char, 16> buf{};
    buf[0] = 'X';
    // NULL out — must not crash.
    omega_midi_note_name(60, nullptr, buf.size());

    // out_size == 0 — must not crash and must leave buf unchanged.
    omega_midi_note_name(60, buf.data(), 0);
    REQUIRE(buf[0] == 'X');
}

TEST_CASE("omega_midi_note_name: output is NUL-terminated when truncated")
{
    std::array<char, 3> buf{'X', 'X', 'X'};
    // "C#4" is 3 chars + NUL; buf has room for exactly 3 bytes → truncated.
    omega_midi_note_name(61, buf.data(), buf.size());
    REQUIRE(buf[2] == '\0');
}

// ── omega_format_position ─────────────────────────────────────────────────────

TEST_CASE("omega_format_position: freeform engine returns OMEGA_ERR_NO_METER")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    std::array<char, 32> buf{};
    // No time signature set — freeform mode.
    REQUIRE(omega_format_position(e, 0, buf.data(), buf.size()) == OMEGA_ERR_NO_METER);

    omega_engine_destroy(e);
}

TEST_CASE("omega_format_position: bar 1, beat 1, subdivision 0")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    // 4/4 at tick 0; process() drains the command queue.
    REQUIRE(omega_timesig_set(e, 0, 4, 4) == OMEGA_OK);
    omega_engine_process(e);

    std::array<char, 32> buf{};
    REQUIRE(omega_format_position(e, 0, buf.data(), buf.size()) == OMEGA_OK);
    REQUIRE(std::strcmp(buf.data(), "1:1.0") == 0);

    omega_engine_destroy(e);
}

TEST_CASE("omega_format_position: beat boundary in 4/4")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    REQUIRE(omega_timesig_set(e, 0, 4, 4) == OMEGA_OK);
    omega_engine_process(e);

    // One beat = OMEGA_PPQN ticks (quarter note at 4/4).
    std::array<char, 32> buf{};
    REQUIRE(omega_format_position(e, OMEGA_PPQN, buf.data(), buf.size()) == OMEGA_OK);
    // Beat 2 of bar 1, subdivision 0.
    REQUIRE(std::strcmp(buf.data(), "1:2.0") == 0);

    omega_engine_destroy(e);
}

TEST_CASE("omega_format_position: bar boundary in 4/4")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    REQUIRE(omega_timesig_set(e, 0, 4, 4) == OMEGA_OK);
    omega_engine_process(e);

    // One bar = 4 beats = 4 * OMEGA_PPQN ticks.
    std::array<char, 32> buf{};
    REQUIRE(omega_format_position(e, 4 * OMEGA_PPQN, buf.data(), buf.size()) == OMEGA_OK);
    REQUIRE(std::strcmp(buf.data(), "2:1.0") == 0);

    omega_engine_destroy(e);
}

TEST_CASE("omega_format_position: subdivision within a beat")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    REQUIRE(omega_timesig_set(e, 0, 4, 4) == OMEGA_OK);
    omega_engine_process(e);

    // Tick 120 = a quarter beat into beat 1 of bar 1.
    std::array<char, 32> buf{};
    REQUIRE(omega_format_position(e, 120, buf.data(), buf.size()) == OMEGA_OK);
    REQUIRE(std::strcmp(buf.data(), "1:1.120") == 0);

    omega_engine_destroy(e);
}

TEST_CASE("omega_format_position: null args return OMEGA_ERR_INVALID")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    REQUIRE(omega_timesig_set(e, 0, 4, 4) == OMEGA_OK);
    omega_engine_process(e);

    std::array<char, 32> buf{};
    REQUIRE(omega_format_position(nullptr, 0, buf.data(), buf.size()) == OMEGA_ERR_INVALID);
    REQUIRE(omega_format_position(e, 0, nullptr, buf.size()) == OMEGA_ERR_INVALID);
    REQUIRE(omega_format_position(e, 0, buf.data(), 0) == OMEGA_ERR_INVALID);

    omega_engine_destroy(e);
}
