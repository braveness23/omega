#include <omega/omega.h>
#include <omega/test/capturing_sink.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>

// Cast CapturingSink to the opaque omega_sink_t* accepted by the C API.
static omega_sink_t* as_sink(omega::CapturingSink& s)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<omega_sink_t*>(&s);
}

// ── NULL guards ──────────────────────────────────────────────────────────────

TEST_CASE("C API tracks: set_track_mute null engine returns OMEGA_ERR_INVALID")
{
    REQUIRE(omega_engine_set_track_mute(nullptr, 1, 1) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API tracks: set_track_solo null engine returns OMEGA_ERR_INVALID")
{
    REQUIRE(omega_engine_set_track_solo(nullptr, 1, 1) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API tracks: track_is_muted null engine returns 0")
{
    REQUIRE(omega_engine_track_is_muted(nullptr, 1) == 0);
}

TEST_CASE("C API tracks: track_is_soloed null engine returns 0")
{
    REQUIRE(omega_engine_track_is_soloed(nullptr, 1) == 0);
}

TEST_CASE("C API tracks: set_track_name null engine returns OMEGA_ERR_INVALID")
{
    REQUIRE(omega_engine_set_track_name(nullptr, 1, "x") == OMEGA_ERR_INVALID);
}

TEST_CASE("C API tracks: set_track_name null name returns OMEGA_ERR_INVALID")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    omega_track_id_t t = 0;
    REQUIRE(omega_engine_add_track(e, "t", &t) == OMEGA_OK);
    REQUIRE(omega_engine_set_track_name(e, t, nullptr) == OMEGA_ERR_INVALID);
    omega_engine_destroy(e);
}

TEST_CASE("C API tracks: set_track_channel null engine returns OMEGA_ERR_INVALID")
{
    REQUIRE(omega_engine_set_track_channel(nullptr, 1, 0) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API tracks: replace_track_event null engine returns OMEGA_ERR_INVALID")
{
    omega_event_t ev{};
    REQUIRE(omega_engine_replace_track_event(nullptr, 1, 0, 0, ev) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API tracks: shift_track_events null engine returns OMEGA_ERR_INVALID")
{
    REQUIRE(omega_engine_shift_track_events(nullptr, 1, 100) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API tracks: swap_tracks null engine returns OMEGA_ERR_INVALID")
{
    REQUIRE(omega_engine_swap_tracks(nullptr, 1, 2) == OMEGA_ERR_INVALID);
}

// ── Mute / solo ──────────────────────────────────────────────────────────────

TEST_CASE("C API tracks: set_track_mute and track_is_muted round-trip")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    omega_track_id_t t = 0;
    REQUIRE(omega_engine_add_track(e, "t", &t) == OMEGA_OK);

    REQUIRE(omega_engine_track_is_muted(e, t) == 0);
    REQUIRE(omega_engine_set_track_mute(e, t, 1) == OMEGA_OK);
    omega_engine_process(e);
    REQUIRE(omega_engine_track_is_muted(e, t) == 1);

    REQUIRE(omega_engine_set_track_mute(e, t, 0) == OMEGA_OK);
    omega_engine_process(e);
    REQUIRE(omega_engine_track_is_muted(e, t) == 0);

    omega_engine_destroy(e);
}

TEST_CASE("C API tracks: set_track_solo and track_is_soloed round-trip")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    omega_track_id_t t = 0;
    REQUIRE(omega_engine_add_track(e, "t", &t) == OMEGA_OK);

    REQUIRE(omega_engine_track_is_soloed(e, t) == 0);
    REQUIRE(omega_engine_set_track_solo(e, t, 1) == OMEGA_OK);
    omega_engine_process(e);
    REQUIRE(omega_engine_track_is_soloed(e, t) == 1);

    omega_engine_destroy(e);
}

// ── Track metadata ───────────────────────────────────────────────────────────

TEST_CASE("C API tracks: set_track_name returns NOT_FOUND for bad id")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    REQUIRE(omega_engine_set_track_name(e, 999, "x") == OMEGA_ERR_NOT_FOUND);
    omega_engine_destroy(e);
}

TEST_CASE("C API tracks: set_track_channel returns NOT_FOUND for bad id")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    REQUIRE(omega_engine_set_track_channel(e, 999, 5) == OMEGA_ERR_NOT_FOUND);
    omega_engine_destroy(e);
}

TEST_CASE("C API tracks: set_track_name accepts valid track")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    omega_track_id_t t = 0;
    REQUIRE(omega_engine_add_track(e, "original", &t) == OMEGA_OK);
    REQUIRE(omega_engine_set_track_name(e, t, "renamed") == OMEGA_OK);
    omega_engine_destroy(e);
}

TEST_CASE("C API tracks: set_track_channel accepts valid track")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    omega_track_id_t t = 0;
    REQUIRE(omega_engine_add_track(e, "t", &t) == OMEGA_OK);
    REQUIRE(omega_engine_set_track_channel(e, t, 9) == OMEGA_OK);
    omega_engine_destroy(e);
}

// ── Track event manipulation ─────────────────────────────────────────────────

TEST_CASE("C API tracks: shift_track_events returns NOT_FOUND for bad id")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    REQUIRE(omega_engine_shift_track_events(e, 999, 100) == OMEGA_ERR_NOT_FOUND);
    omega_engine_destroy(e);
}

TEST_CASE("C API tracks: swap_tracks returns NOT_FOUND for bad id")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    omega_track_id_t a = 0;
    REQUIRE(omega_engine_add_track(e, "a", &a) == OMEGA_OK);
    REQUIRE(omega_engine_swap_tracks(e, a, 999) == OMEGA_ERR_NOT_FOUND);
    omega_engine_destroy(e);
}

TEST_CASE("C API tracks: shift_track_events delays events by offset")
{
    omega::CapturingSink sink;
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    omega_engine_add_sink(e, as_sink(sink));

    omega_track_id_t t = 0;
    REQUIRE(omega_engine_add_track(e, "t", &t) == OMEGA_OK);
    REQUIRE(omega_engine_set_track_sink(e, t, sink.sink_id()) == OMEGA_OK);

    omega_event_t ev = omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 0);
    REQUIRE(omega_engine_add_event(e, t, ev) == OMEGA_OK);
    omega_engine_process(e);  // apply AddEventCmd

    REQUIRE(omega_engine_shift_track_events(e, t, 480) == OMEGA_OK);

    // Play: note should not fire at tick 0 (shifted to tick 480)
    omega_engine_play(e);
    omega_engine_process(e);
    REQUIRE_FALSE(sink.has_note_on(60, 0));

    omega_engine_destroy(e);
}

TEST_CASE("C API tracks: swap_tracks changes track order")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_track_id_t a = 0;
    omega_track_id_t b = 0;
    REQUIRE(omega_engine_add_track(e, "a", &a) == OMEGA_OK);
    REQUIRE(omega_engine_add_track(e, "b", &b) == OMEGA_OK);
    REQUIRE(omega_engine_swap_tracks(e, a, b) == OMEGA_OK);

    omega_engine_destroy(e);
}

// ── ControlSink ──────────────────────────────────────────────────────────────

TEST_CASE("C API tracks: omega_sink_create_control null engine returns nullptr")
{
    REQUIRE(omega_sink_create_control(nullptr) == nullptr);
}

TEST_CASE("C API tracks: omega_sink_create_control returns a usable sink")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_sink_t* ctrl = omega_sink_create_control(e);
    REQUIRE(ctrl != nullptr);
    REQUIRE(omega_engine_add_sink(e, ctrl) == OMEGA_OK);
    REQUIRE(omega_sink_id(ctrl) > 0u);

    omega_engine_destroy(e);
    omega_sink_destroy_control(ctrl);
}

// ── omega_smf_import_ex ──────────────────────────────────────────────────────

static std::string write_minimal_mid_for_tracks()
{
    auto path = (std::filesystem::temp_directory_path() / "omega_c_api_tracks.mid").string();
    // clang-format off
    static const std::array<uint8_t, 48> kMidi = {
        0x4D,0x54,0x68,0x64, 0x00,0x00,0x00,0x06, 0x00,0x00, 0x00,0x01, 0x01,0xE0,
        0x4D,0x54,0x72,0x6B, 0x00,0x00,0x00,0x14,
        0x00,0xFF,0x51,0x03,0x07,0xA1,0x20,
        0x00,0x90,0x3C,0x64,
        0x83,0x60,0x80,0x3C,0x00,
        0x00,0xFF,0x2F,0x00
    };
    // clang-format on
    std::ofstream f(path, std::ios::binary);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    f.write(reinterpret_cast<const char*>(kMidi.data()),
            static_cast<std::streamsize>(kMidi.size()));
    return path;
}

TEST_CASE("C API tracks: omega_smf_import_ex null engine returns OMEGA_ERR_INVALID")
{
    REQUIRE(omega_smf_import_ex(nullptr, "/tmp/x.mid", nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API tracks: omega_smf_import_ex null path returns OMEGA_ERR_INVALID")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    REQUIRE(omega_smf_import_ex(e, nullptr, nullptr) == OMEGA_ERR_INVALID);
    omega_engine_destroy(e);
}

TEST_CASE("C API tracks: omega_smf_import_ex with null opts behaves like omega_smf_import")
{
    auto path = write_minimal_mid_for_tracks();
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    REQUIRE(omega_smf_import_ex(e, path.c_str(), nullptr) == OMEGA_OK);
    omega_engine_destroy(e);
}

TEST_CASE("C API tracks: omega_smf_import_ex clear_existing succeeds")
{
    auto path = write_minimal_mid_for_tracks();
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    REQUIRE(omega_smf_import(e, path.c_str()) == OMEGA_OK);

    omega_smf_import_options_t opts{};
    opts.clear_existing = 1;
    REQUIRE(omega_smf_import_ex(e, path.c_str(), &opts) == OMEGA_OK);

    omega_engine_destroy(e);
}
