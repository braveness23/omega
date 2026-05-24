#include <omega/engine.h>
#include <omega/omega.h>
#include <omega/test/capturing_sink.h>
#include <omega/test/mock_clock.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <string>

#include "MidiFile.h"

// Forward declaration from src/smf_import.cpp
namespace omega
{
omega_status_t smf_import(Engine& engine, const char* path);
}

static std::string tmp_path(const char* suffix)
{
    return std::string("/tmp/omega_smf_test_") + suffix + ".mid";
}

TEST_CASE("SMF import: null path returns OMEGA_ERR_INVALID", "[smf_import]")
{
    omega::Engine engine;
    CHECK(omega::smf_import(engine, nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("SMF import: bad path returns OMEGA_ERR_IO", "[smf_import]")
{
    omega::Engine engine;
    omega_status_t s = omega::smf_import(engine, "/tmp/omega_smf_does_not_exist_xyz.mid");
    CHECK(s == OMEGA_ERR_IO);
}

TEST_CASE("SMF import: Type 0 - single track created", "[smf_import]")
{
    const std::string path = tmp_path("type0");

    // Build a simple Type 0 file (one track)
    smf::MidiFile mf;
    mf.setTPQ(480);
    // Track 0 exists by default; add a note at beat 1 (tick 480)
    mf.addTempo(0, 0, 120.0);
    mf.addNoteOn(0, 480, 0, 60, 100);
    mf.addNoteOff(0, 960, 0, 60, 0);
    mf.sortTracks();
    REQUIRE(mf.write(path.c_str()) != 0);

    omega::Engine engine;
    REQUIRE(omega::smf_import(engine, path.c_str()) == OMEGA_OK);
    CHECK(engine.transport_state() == omega::TransportState::STOPPED);

    std::remove(path.c_str());
}

TEST_CASE("SMF import: non-480 PPQN tick scaling via markers", "[smf_import]")
{
    const std::string path = tmp_path("ppqn240");
    // 240 PPQN: SMF tick 240 (one beat) -> omega tick (240*480)/240 = 480
    smf::MidiFile mf;
    mf.setTPQ(240);
    mf.addTempo(0, 0, 120.0);
    mf.addMarker(0, 240, "beat1");  // SMF tick 240 -> omega tick 480
    mf.sortTracks();
    REQUIRE(mf.write(path.c_str()) != 0);

    omega::Engine engine;
    REQUIRE(omega::smf_import(engine, path.c_str()) == OMEGA_OK);

    // Marker at SMF tick 240 (240 PPQN) must appear at omega tick 480
    const omega::Marker* m = engine.marker_list().find_nearest(480u);
    REQUIRE(m != nullptr);
    CHECK(m->name == "beat1");
    CHECK(m->tick == 480u);

    std::remove(path.c_str());
}

TEST_CASE("SMF import: tempo changes imported into TempoMap", "[smf_import]")
{
    const std::string path = tmp_path("tempo");
    smf::MidiFile mf;
    mf.setTPQ(480);
    mf.addTempo(0, 0, 120.0);     // 120 BPM at tick 0   = 500000 us/beat
    mf.addTempo(0, 1920, 150.0);  // 150 BPM at tick 1920 = 400000 us/beat
    mf.sortTracks();
    REQUIRE(mf.write(path.c_str()) != 0);

    omega::Engine engine;
    REQUIRE(omega::smf_import(engine, path.c_str()) == OMEGA_OK);

    // 120 BPM: tick 1920 = 4 beats = 4 * 500000 us = 2000000000 ns
    uint64_t ns_at_1920 = engine.tempo_map().ticks_to_ns(1920u);
    CHECK(ns_at_1920 == 2000000000u);

    // After 150 BPM at tick 1920: 1920 more ticks = 4 beats @ 400000 us/beat = 1600000000 ns
    uint64_t ns_at_3840 = engine.tempo_map().ticks_to_ns(3840u);
    CHECK(ns_at_3840 == 3600000000u);

    std::remove(path.c_str());
}

TEST_CASE("SMF import: markers populated into MarkerList", "[smf_import]")
{
    const std::string path = tmp_path("markers");
    smf::MidiFile mf;
    mf.setTPQ(480);
    mf.addTempo(0, 0, 120.0);
    mf.addMarker(0, 0, "intro");
    mf.addMarker(0, 1920, "verse");
    mf.sortTracks();
    REQUIRE(mf.write(path.c_str()) != 0);

    omega::Engine engine;
    REQUIRE(omega::smf_import(engine, path.c_str()) == OMEGA_OK);

    CHECK(engine.marker_list().size() >= 2u);

    const omega::Marker* m = engine.marker_list().find_nearest(0u);
    REQUIRE(m != nullptr);
    CHECK(m->name == "intro");
    CHECK(m->tick == 0u);

    m = engine.marker_list().find_nearest(1920u);
    REQUIRE(m != nullptr);
    CHECK(m->name == "verse");
    CHECK(m->tick == 1920u);

    std::remove(path.c_str());
}

TEST_CASE("SMF import: time signatures imported into TimeSignatureMap", "[smf_import]")
{
    const std::string path = tmp_path("timesig");
    smf::MidiFile mf;
    mf.setTPQ(480);
    mf.addTempo(0, 0, 120.0);
    mf.addTimeSignature(0, 0, 4, 4);     // 4/4 at tick 0
    mf.addTimeSignature(0, 1920, 3, 4);  // 3/4 at tick 1920
    mf.sortTracks();
    REQUIRE(mf.write(path.c_str()) != 0);

    omega::Engine engine;
    REQUIRE(omega::smf_import(engine, path.c_str()) == OMEGA_OK);

    CHECK(!engine.timesig_map().is_freeform());

    const omega::TimeSigPoint* p0 = engine.timesig_map().at(0u);
    REQUIRE(p0 != nullptr);
    CHECK(p0->numerator == 4u);
    CHECK(p0->denominator == 4u);

    const omega::TimeSigPoint* p1 = engine.timesig_map().at(1920u);
    REQUIRE(p1 != nullptr);
    CHECK(p1->numerator == 3u);
    CHECK(p1->denominator == 4u);

    std::remove(path.c_str());
}
