#include <omega/engine.h>
#include <omega/marker_list.h>
#include <omega/omega.h>
#include <omega/tempo_map.h>
#include <omega/time_signature_map.h>
#include <omega/timeline.h>
#include <omega/types.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "MidiFile.h"

// Forward declarations from smf_import.cpp and smf_export.cpp
namespace omega
{
omega_status_t smf_import(Engine& engine, const char* path);
omega_status_t smf_export(Engine& engine, const char* path, int smf_type);
}  // namespace omega

static std::string tmp_path(const char* suffix)
{
    auto name = std::string("omega_smf_export_") + suffix + ".mid";
    return (std::filesystem::temp_directory_path() / name).string();
}

TEST_CASE("SMF export: null path returns OMEGA_ERR_INVALID", "[smf_export]")
{
    omega::Engine engine;
    CHECK(omega::smf_export(engine, nullptr, 1) == OMEGA_ERR_INVALID);
}

TEST_CASE("SMF export: bad path returns OMEGA_ERR_IO", "[smf_export]")
{
    omega::Engine engine;
    omega_status_t s = omega::smf_export(engine, "/no_such_dir/omega_smf_export_test.mid", 1);
    CHECK(s == OMEGA_ERR_IO);
}

TEST_CASE("SMF export: round-trip Type 0 - single track", "[smf_export]")
{
    const std::string path = tmp_path("rt_type0");

    // Build an engine with one track and two notes
    omega::Engine eng_out;
    eng_out.tempo_map().insert(0u, 120000u);  // 120 BPM

    omega::TrackId tid = eng_out.add_track("track0");

    omega::Event ev{};
    ev.tick = 480u;
    ev.payload_tag = OMEGA_NOTE_ON;
    ev.channel = 0u;
    ev.data[0] = 60u;   // note C4
    ev.data[1] = 100u;  // velocity
    uint32_t dur = 240u;
    std::memcpy(&ev.data[2], &dur, sizeof(dur));
    eng_out.add_track_event(tid, ev);

    REQUIRE(omega::smf_export(eng_out, path.c_str(), 0) == OMEGA_OK);

    // Verify file is a valid MIDI file with 1 track (Type 0)
    smf::MidiFile mf;
    REQUIRE(mf.read(path) != 0);
    CHECK(mf.getNumTracks() == 1);

    std::remove(path.c_str());
}

TEST_CASE("SMF export: round-trip Type 1 - two tracks", "[smf_export]")
{
    const std::string path_out = tmp_path("rt_type1_out");
    const std::string path_in = tmp_path("rt_type1_in");

    // Build a Type 1 source file with midifile
    {
        smf::MidiFile mf;
        mf.setTPQ(480);
        mf.addTracks(2);  // total 3 tracks: 0=meta, 1=track1, 2=track2
        mf.addTempo(0, 0, 120.0);
        mf.addTimeSignature(0, 0, 4, 4);
        mf.addMarker(0, 0, "start");
        mf.addNoteOn(1, 0, 0, 60, 100);
        mf.addNoteOff(1, 480, 0, 60, 0);
        mf.addNoteOn(2, 0, 1, 64, 90);
        mf.addNoteOff(2, 240, 1, 64, 0);
        mf.sortTracks();
        REQUIRE(mf.write(path_in) != 0);
    }

    // Import into engine
    omega::Engine eng;
    REQUIRE(omega::smf_import(eng, path_in.c_str()) == OMEGA_OK);

    // Export as Type 1
    REQUIRE(omega::smf_export(eng, path_out.c_str(), 1) == OMEGA_OK);

    // Re-import into fresh engine and verify
    omega::Engine eng2;
    REQUIRE(omega::smf_import(eng2, path_out.c_str()) == OMEGA_OK);

    // Marker should be preserved
    const omega::Marker* m = eng2.marker_list().find_nearest(0u);
    REQUIRE(m != nullptr);
    CHECK(m->name == "start");
    CHECK(m->tick == 0u);

    // Time signature preserved
    const omega::TimeSigPoint* ts = eng2.timesig_map().at(0u);
    REQUIRE(ts != nullptr);
    CHECK(ts->numerator == 4u);
    CHECK(ts->denominator == 4u);

    std::remove(path_in.c_str());
    std::remove(path_out.c_str());
}

TEST_CASE("SMF export: round-trip tempo changes", "[smf_export]")
{
    const std::string path = tmp_path("rt_tempo");

    // Set up engine with 3 tempo changes
    omega::Engine eng_out;
    eng_out.tempo_map().insert(0u, 120000u);     // 120 BPM at tick 0
    eng_out.tempo_map().insert(1920u, 150000u);  // 150 BPM at tick 1920
    eng_out.tempo_map().insert(3840u, 90000u);   // 90 BPM at tick 3840

    REQUIRE(omega::smf_export(eng_out, path.c_str(), 0) == OMEGA_OK);

    // Re-import and verify tempo map
    omega::Engine eng_in;
    REQUIRE(omega::smf_import(eng_in, path.c_str()) == OMEGA_OK);

    // 120 BPM from tick 0: 1920 ticks = 4 beats = 2000000000 ns
    uint64_t ns_at_1920 = eng_in.tempo_map().ticks_to_ns(1920u);
    CHECK(ns_at_1920 == 2000000000u);

    // 150 BPM from tick 1920: 1920 more ticks = 4 beats @ 400000 us/beat = 1600000000 ns
    uint64_t ns_at_3840 = eng_in.tempo_map().ticks_to_ns(3840u);
    CHECK(ns_at_3840 == 3600000000u);

    std::remove(path.c_str());
}

TEST_CASE("SMF export: round-trip markers", "[smf_export]")
{
    const std::string path = tmp_path("rt_markers");

    omega::Engine eng_out;
    eng_out.tempo_map().insert(0u, 120000u);
    eng_out.marker_list().add("intro", 0u);
    eng_out.marker_list().add("verse", 1920u);
    eng_out.marker_list().add("chorus", 3840u);

    REQUIRE(omega::smf_export(eng_out, path.c_str(), 0) == OMEGA_OK);

    omega::Engine eng_in;
    REQUIRE(omega::smf_import(eng_in, path.c_str()) == OMEGA_OK);

    CHECK(eng_in.marker_list().size() == 3u);

    const omega::Marker* m = eng_in.marker_list().find_nearest(0u);
    REQUIRE(m != nullptr);
    CHECK(m->name == "intro");

    m = eng_in.marker_list().find_nearest(1920u);
    REQUIRE(m != nullptr);
    CHECK(m->name == "verse");

    m = eng_in.marker_list().find_nearest(3840u);
    REQUIRE(m != nullptr);
    CHECK(m->name == "chorus");

    std::remove(path.c_str());
}

TEST_CASE("SMF export: round-trip time signatures", "[smf_export]")
{
    const std::string path = tmp_path("rt_timesig");

    omega::Engine eng_out;
    eng_out.tempo_map().insert(0u, 120000u);
    eng_out.timesig_map().insert(0u, 4u, 4u);
    eng_out.timesig_map().insert(1920u, 3u, 4u);

    REQUIRE(omega::smf_export(eng_out, path.c_str(), 0) == OMEGA_OK);

    omega::Engine eng_in;
    REQUIRE(omega::smf_import(eng_in, path.c_str()) == OMEGA_OK);

    CHECK(!eng_in.timesig_map().is_freeform());

    const omega::TimeSigPoint* p0 = eng_in.timesig_map().at(0u);
    REQUIRE(p0 != nullptr);
    CHECK(p0->numerator == 4u);
    CHECK(p0->denominator == 4u);

    const omega::TimeSigPoint* p1 = eng_in.timesig_map().at(1920u);
    REQUIRE(p1 != nullptr);
    CHECK(p1->numerator == 3u);
    CHECK(p1->denominator == 4u);

    std::remove(path.c_str());
}
