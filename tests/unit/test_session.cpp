#include <omega/engine.h>
#include <omega/session.h>
#include <omega/test/mock_clock.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace omega;

static std::string tmp_path(const char* suffix)
{
    return (std::filesystem::temp_directory_path() / suffix).string();
}

// ── Null-guard tests ──────────────────────────────────────────────────────────

TEST_CASE("session_save: null path returns ERR_INVALID")
{
    MockClock clk;
    Engine e(&clk);
    REQUIRE(session_save(e, nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("session_load: null path returns ERR_INVALID")
{
    MockClock clk;
    Engine e(&clk);
    REQUIRE(session_load(e, nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("session_load: nonexistent file returns ERR_IO")
{
    MockClock clk;
    Engine e(&clk);
    REQUIRE(session_load(e, "/tmp/omega_no_such_file_session.oms") == OMEGA_ERR_IO);
}

// ── Round-trip: empty engine ──────────────────────────────────────────────────

TEST_CASE("session: empty engine save/load round-trip")
{
    const std::string path = tmp_path("omega_session_empty.oms");

    {
        MockClock clk;
        Engine e(&clk);
        REQUIRE(session_save(e, path.c_str()) == OMEGA_OK);
    }
    {
        MockClock clk;
        Engine e(&clk);
        REQUIRE(session_load(e, path.c_str()) == OMEGA_OK);
        // Default tempo: 120 BPM
        REQUIRE(e.tempo_map().bpm_milli_at(0) == 120000u);
        // No timeline tracks
        REQUIRE(e.timeline_source().tracks().empty());
        // No patterns
        REQUIRE(e.pattern_library().count() == 0u);
    }
    std::remove(path.c_str());
}

// ── Round-trip: tempo map ─────────────────────────────────────────────────────

TEST_CASE("session: tempo map round-trip")
{
    const std::string path = tmp_path("omega_session_tempo.oms");

    {
        MockClock clk;
        Engine e(&clk);
        e.tempo_map().insert(0, 140000u);
        e.tempo_map().insert(960, 100000u);
        REQUIRE(session_save(e, path.c_str()) == OMEGA_OK);
    }
    {
        MockClock clk;
        Engine e(&clk);
        REQUIRE(session_load(e, path.c_str()) == OMEGA_OK);
        REQUIRE(e.tempo_map().bpm_milli_at(0) == 140000u);
        REQUIRE(e.tempo_map().bpm_milli_at(960) == 100000u);
        REQUIRE(e.tempo_map().bpm_milli_at(1000) == 100000u);
    }
    std::remove(path.c_str());
}

// ── Round-trip: time signatures ───────────────────────────────────────────────

TEST_CASE("session: time signature map round-trip")
{
    const std::string path = tmp_path("omega_session_timesig.oms");

    {
        MockClock clk;
        Engine e(&clk);
        e.timesig_set(0, 4, 4);
        e.timesig_set(3840, 3, 4);
        e.process();
        REQUIRE(session_save(e, path.c_str()) == OMEGA_OK);
    }
    {
        MockClock clk;
        Engine e(&clk);
        REQUIRE(session_load(e, path.c_str()) == OMEGA_OK);
        const TimeSigPoint* p0 = e.timesig_map().at(0);
        REQUIRE(p0 != nullptr);
        REQUIRE(p0->numerator == 4);
        REQUIRE(p0->denominator == 4);
        const TimeSigPoint* p1 = e.timesig_map().at(3840);
        REQUIRE(p1 != nullptr);
        REQUIRE(p1->numerator == 3);
        REQUIRE(p1->denominator == 4);
    }
    std::remove(path.c_str());
}

// ── Round-trip: timeline tracks ───────────────────────────────────────────────

TEST_CASE("session: timeline tracks and events round-trip")
{
    const std::string path = tmp_path("omega_session_tracks.oms");

    {
        MockClock clk;
        Engine e(&clk);
        TrackId t = e.add_track("bass");
        e.set_track_channel(t, 1);
        e.add_track_event(t, {0u, 1u, OMEGA_NOTE_ON, 1u, {}, {60, 100, 0, 0, 0xE0, 0x01, 0, 0}});
        REQUIRE(session_save(e, path.c_str()) == OMEGA_OK);
    }
    {
        MockClock clk;
        Engine e(&clk);
        REQUIRE(session_load(e, path.c_str()) == OMEGA_OK);
        const auto& tracks = e.timeline_source().tracks();
        REQUIRE(tracks.size() == 1);
        REQUIRE(tracks[0].name == "bass");
        REQUIRE(tracks[0].channel == 1);
        REQUIRE(tracks[0].events.size() == 1);
        REQUIRE(tracks[0].events[0].tick == 0u);
        REQUIRE(tracks[0].events[0].data[0] == 60);
    }
    std::remove(path.c_str());
}

// ── Round-trip: patterns ──────────────────────────────────────────────────────

TEST_CASE("session: pattern library round-trip")
{
    const std::string path = tmp_path("omega_session_patterns.oms");

    PatternId saved_id = 0;
    {
        MockClock clk;
        Engine e(&clk);
        saved_id = e.create_pattern("lead", 480);
        e.pattern_add_event(saved_id,
                            {0u, 1u, OMEGA_NOTE_ON, 0u, {}, {64, 80, 0, 0, 0xE0, 0x01, 0, 0}});
        REQUIRE(session_save(e, path.c_str()) == OMEGA_OK);
    }
    {
        MockClock clk;
        Engine e(&clk);
        REQUIRE(session_load(e, path.c_str()) == OMEGA_OK);
        REQUIRE(e.pattern_library().count() == 1u);
        bool found = false;
        e.pattern_library().for_each([&](PatternId, const Pattern& pat) {
            REQUIRE(pat.name == "lead");
            REQUIRE(pat.length_ticks == 480u);
            REQUIRE(pat.events.size() == 1u);
            REQUIRE(pat.events[0].data[0] == 64);
            found = true;
        });
        REQUIRE(found);
    }
    std::remove(path.c_str());
}

// ── Round-trip: perf slot assignments ────────────────────────────────────────

TEST_CASE("session: perf slot assignment round-trip")
{
    const std::string path = tmp_path("omega_session_perf.oms");

    {
        MockClock clk;
        Engine e(&clk);
        PatternId pid = e.create_pattern("p1", 480);
        e.perf_assign(0, pid);
        e.perf_set_transpose(0, 3);
        e.perf_set_velocity_scale(0, 80);
        e.perf_set_random_bias(0, 20);
        e.perf_set_repeat_count(0, 4);
        e.process();
        REQUIRE(session_save(e, path.c_str()) == OMEGA_OK);
    }
    {
        MockClock clk;
        Engine e(&clk);
        REQUIRE(session_load(e, path.c_str()) == OMEGA_OK);
        const auto snap = e.perf_source().slot_snapshot(0);
        REQUIRE(snap.assigned != 0);
        REQUIRE(snap.transpose == 3);
        REQUIRE(snap.velocity_scale == 80);
        REQUIRE(snap.random_bias == 20);
        REQUIRE(snap.repeat_count == 4);
    }
    std::remove(path.c_str());
}

// ── Round-trip: song arrangement ─────────────────────────────────────────────

TEST_CASE("session: song arrangement round-trip")
{
    const std::string path = tmp_path("omega_session_song.oms");

    {
        MockClock clk;
        Engine e(&clk);
        PatternId p1 = e.create_pattern("a", 480);
        PatternId p2 = e.create_pattern("b", 960);
        e.song_append(p1, 2);
        e.song_append(p2, 1);
        e.process();
        REQUIRE(session_save(e, path.c_str()) == OMEGA_OK);
    }
    {
        MockClock clk;
        Engine e(&clk);
        REQUIRE(session_load(e, path.c_str()) == OMEGA_OK);
        REQUIRE(e.song_source().entries().size() == 2u);
        REQUIRE(e.song_source().entries()[0].repeat_count == 2u);
        REQUIRE(e.song_source().entries()[1].repeat_count == 1u);
    }
    std::remove(path.c_str());
}

// ── Round-trip: markers ───────────────────────────────────────────────────────

TEST_CASE("session: markers round-trip")
{
    const std::string path = tmp_path("omega_session_markers.oms");

    {
        MockClock clk;
        Engine e(&clk);
        e.marker_list().add("intro", 0);
        e.marker_list().add("verse", 960);
        REQUIRE(session_save(e, path.c_str()) == OMEGA_OK);
    }
    {
        MockClock clk;
        Engine e(&clk);
        REQUIRE(session_load(e, path.c_str()) == OMEGA_OK);
        REQUIRE(e.marker_list().size() == 2u);
        REQUIRE(e.marker_list().at(0)->name == "intro");
        REQUIRE(e.marker_list().at(1)->name == "verse");
        REQUIRE(e.marker_list().at(1)->tick == 960u);
    }
    std::remove(path.c_str());
}

// ── Round-trip: regions ───────────────────────────────────────────────────────

TEST_CASE("session: regions round-trip")
{
    const std::string path = tmp_path("omega_session_regions.oms");

    {
        MockClock clk;
        Engine e(&clk);
        e.region_list().add("chorus", 960, 1920, RegionType::SECTION);
        REQUIRE(session_save(e, path.c_str()) == OMEGA_OK);
    }
    {
        MockClock clk;
        Engine e(&clk);
        REQUIRE(session_load(e, path.c_str()) == OMEGA_OK);
        REQUIRE(e.region_list().size() == 1u);
        REQUIRE(e.region_list().at(0)->name == "chorus");
        REQUIRE(e.region_list().at(0)->start_tick == 960u);
        REQUIRE(e.region_list().at(0)->end_tick == 1920u);
        REQUIRE(e.region_list().at(0)->type == RegionType::SECTION);
    }
    std::remove(path.c_str());
}

// ── Round-trip: loop region ───────────────────────────────────────────────────

TEST_CASE("session: loop region round-trip — enabled")
{
    const std::string path = tmp_path("omega_session_loop.oms");

    {
        MockClock clk;
        Engine e(&clk);
        e.loop_set_immediate(480, 960);
        REQUIRE(session_save(e, path.c_str()) == OMEGA_OK);
    }
    {
        MockClock clk;
        Engine e(&clk);
        REQUIRE(session_load(e, path.c_str()) == OMEGA_OK);
        const auto lr = e.loop_region();
        REQUIRE(lr.start_tick == 480u);
        REQUIRE(lr.end_tick == 960u);
        REQUIRE(lr.enabled);
    }
    std::remove(path.c_str());
}

// ── Round-trip: SMPTE config ──────────────────────────────────────────────────

TEST_CASE("session: SMPTE config round-trip")
{
    const std::string path = tmp_path("omega_session_smpte.oms");

    {
        MockClock clk;
        Engine e(&clk);
        SmpteConfig cfg;
        cfg.fps = 30;
        cfg.drop_frame = true;
        cfg.is_2997 = true;
        e.smpte_config_set(cfg);
        e.process();
        REQUIRE(session_save(e, path.c_str()) == OMEGA_OK);
    }
    {
        MockClock clk;
        Engine e(&clk);
        REQUIRE(session_load(e, path.c_str()) == OMEGA_OK);
        const auto& cfg = e.smpte_config();
        REQUIRE(cfg.has_value());
        REQUIRE(cfg->fps == 30);
        REQUIRE(cfg->drop_frame == true);
        REQUIRE(cfg->is_2997 == true);
    }
    std::remove(path.c_str());
}

// ── Round-trip: performance context ──────────────────────────────────────────

TEST_CASE("session: performance context round-trip")
{
    const std::string path = tmp_path("omega_session_ctx.oms");

    {
        MockClock clk;
        Engine e(&clk);
        e.ctx_set_transpose(5);
        e.ctx_set_velocity(80);
        e.ctx_set_chaos(30);
        e.ctx_set_groove(2, 0.5f);
        e.process();
        REQUIRE(session_save(e, path.c_str()) == OMEGA_OK);
    }
    {
        MockClock clk;
        Engine e(&clk);
        REQUIRE(session_load(e, path.c_str()) == OMEGA_OK);
        omega_perf_ctx_t ctx{};
        e.ctx_get(ctx);
        REQUIRE(ctx.global_transpose == 5);
        REQUIRE(ctx.global_velocity == 80);
        REQUIRE(ctx.chaos == 30);
        REQUIRE(ctx.groove_id == 2);
    }
    std::remove(path.c_str());
}

// ── Round-trip: corrupt file ──────────────────────────────────────────────────

TEST_CASE("session_load: corrupt magic returns ERR_IO")
{
    const std::string path = tmp_path("omega_session_corrupt.oms");
    {
        std::ofstream f(path, std::ios::binary);
        const char bad[] = "JUNK\x01\x00\x00\x00";
        f.write(bad, 8);
    }
    MockClock clk;
    Engine e(&clk);
    REQUIRE(session_load(e, path.c_str()) == OMEGA_ERR_IO);
    std::remove(path.c_str());
}
