#include <omega/omega.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

// Write a minimal valid Type 0 MIDI file with one note at tick 0 (120 BPM, 480 PPQN).
static std::string write_minimal_mid()
{
    auto path = (std::filesystem::temp_directory_path() / "omega_capi_smf.mid").string();

    // MIDI header: MThd len=6 type=0 tracks=1 ppq=480
    // MIDI track:  MTrk len=20  tempo + note-on + note-off + end-of-track
    // delta=480 in VLQ: 0x83 0x60
    // clang-format off
    static const std::array<uint8_t, 48> kMidi = {
        0x4D, 0x54, 0x68, 0x64,  // "MThd"
        0x00, 0x00, 0x00, 0x06,  // length = 6
        0x00, 0x00,              // type 0
        0x00, 0x01,              // 1 track
        0x01, 0xE0,              // 480 PPQN
        0x4D, 0x54, 0x72, 0x6B,  // "MTrk"
        0x00, 0x00, 0x00, 0x14,  // length = 20
        0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,  // tempo 120 BPM
        0x00, 0x90, 0x3C, 0x64,                     // note-on ch0 C4 vel100
        0x83, 0x60, 0x80, 0x3C, 0x00,              // note-off after 480 ticks
        0x00, 0xFF, 0x2F, 0x00                      // end-of-track
    };
    // clang-format on

    std::ofstream f(path, std::ios::binary);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    f.write(reinterpret_cast<const char*>(kMidi.data()),
            static_cast<std::streamsize>(kMidi.size()));
    return path;
}

// ── omega_smf_import null guards ─────────────────────────────────────────────

TEST_CASE("C API SMF: omega_smf_import null engine returns OMEGA_ERR_INVALID")
{
    REQUIRE(omega_smf_import(nullptr, "/tmp/whatever.mid") == OMEGA_ERR_INVALID);
}

TEST_CASE("C API SMF: omega_smf_import null path returns OMEGA_ERR_INVALID")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    REQUIRE(omega_smf_import(e, nullptr) == OMEGA_ERR_INVALID);
    omega_engine_destroy(e);
}

TEST_CASE("C API SMF: omega_smf_import bad path returns OMEGA_ERR_IO")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    REQUIRE(omega_smf_import(e, "/tmp/omega_no_such_file_xyz.mid") == OMEGA_ERR_IO);
    omega_engine_destroy(e);
}

// ── omega_smf_export null guards ─────────────────────────────────────────────

TEST_CASE("C API SMF: omega_smf_export null engine returns OMEGA_ERR_INVALID")
{
    REQUIRE(omega_smf_export(nullptr, "/tmp/whatever.mid", 0) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API SMF: omega_smf_export null path returns OMEGA_ERR_INVALID")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    REQUIRE(omega_smf_export(e, nullptr, 0) == OMEGA_ERR_INVALID);
    omega_engine_destroy(e);
}

// ── round-trip ────────────────────────────────────────────────────────────────

TEST_CASE("C API SMF: import then export round-trip")
{
    const std::string src = write_minimal_mid();
    const std::string dst =
        (std::filesystem::temp_directory_path() / "omega_capi_smf_export.mid").string();

    // Import
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    REQUIRE(omega_smf_import(e, src.c_str()) == OMEGA_OK);

    // Export what was imported
    REQUIRE(omega_smf_export(e, dst.c_str(), 0) == OMEGA_OK);
    omega_engine_destroy(e);

    // Re-import and verify it's parseable
    omega_engine_t* e2 = omega_engine_create();
    REQUIRE(e2 != nullptr);
    REQUIRE(omega_smf_import(e2, dst.c_str()) == OMEGA_OK);
    omega_engine_destroy(e2);

    std::remove(src.c_str());
    std::remove(dst.c_str());
}

TEST_CASE("C API SMF: export engine with track then reimport")
{
    const std::string path =
        (std::filesystem::temp_directory_path() / "omega_capi_smf_track.mid").string();

    // Build an engine with one track and one note, then export.
    {
        omega_engine_t* e = omega_engine_create();
        REQUIRE(e != nullptr);

        omega_track_id_t track{};
        REQUIRE(omega_engine_add_track(e, "bass", &track) == OMEGA_OK);

        omega_event_t ev = omega_make_note_on(0u, 0u, 0, 60, 100, 480);
        REQUIRE(omega_engine_add_event(e, track, ev) == OMEGA_OK);

        // Process once to drain command queue before export
        omega_engine_process(e);
        REQUIRE(omega_smf_export(e, path.c_str(), 0) == OMEGA_OK);
        omega_engine_destroy(e);
    }

    // Reimport and check the engine accepts it.
    {
        omega_engine_t* e = omega_engine_create();
        REQUIRE(e != nullptr);
        REQUIRE(omega_smf_import(e, path.c_str()) == OMEGA_OK);
        omega_engine_destroy(e);
    }

    std::remove(path.c_str());
}
