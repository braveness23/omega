#include <omega/omega.h>

#include <catch2/catch_test_macros.hpp>

// ── Fixtures ──────────────────────────────────────────────────────────────────

struct EngineFixture
{
    omega_engine_t* eng{nullptr};
    EngineFixture() : eng{omega_engine_create()} {}
    ~EngineFixture() { omega_engine_destroy(eng); }
    EngineFixture(const EngineFixture&) = delete;
    EngineFixture& operator=(const EngineFixture&) = delete;
    EngineFixture(EngineFixture&&) = delete;
    EngineFixture& operator=(EngineFixture&&) = delete;
};

// ── NULL guard tests ──────────────────────────────────────────────────────────

TEST_CASE("C API: omega_timesig_set null engine returns INVALID", "[c_api_timesig]")
{
    CHECK(omega_timesig_set(nullptr, 0u, 4u, 4u) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: omega_timesig_remove null engine returns INVALID", "[c_api_timesig]")
{
    CHECK(omega_timesig_remove(nullptr, 0u) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: omega_timesig_clear null engine returns INVALID", "[c_api_timesig]")
{
    CHECK(omega_timesig_clear(nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: omega_timesig_is_freeform null engine returns -1", "[c_api_timesig]")
{
    CHECK(omega_timesig_is_freeform(nullptr) == -1);
}

TEST_CASE("C API: omega_timesig_at null engine returns INVALID", "[c_api_timesig]")
{
    omega_time_sig_point_t pt{};
    CHECK(omega_timesig_at(nullptr, 0u, &pt) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: omega_timesig_at null out returns INVALID", "[c_api_timesig]")
{
    EngineFixture f;
    CHECK(omega_timesig_at(f.eng, 0u, nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: omega_tick_to_beat_pos null guards", "[c_api_timesig]")
{
    EngineFixture f;
    omega_beat_pos_t pos{};
    CHECK(omega_tick_to_beat_pos(nullptr, 0u, &pos) == OMEGA_ERR_INVALID);
    CHECK(omega_tick_to_beat_pos(f.eng, 0u, nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: omega_beat_pos_to_tick null guards", "[c_api_timesig]")
{
    EngineFixture f;
    omega_beat_pos_t pos{1u, 1u, 0u};
    uint64_t out = 0u;
    CHECK(omega_beat_pos_to_tick(nullptr, &pos, &out) == OMEGA_ERR_INVALID);
    CHECK(omega_beat_pos_to_tick(f.eng, nullptr, &out) == OMEGA_ERR_INVALID);
    CHECK(omega_beat_pos_to_tick(f.eng, &pos, nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: omega_next_bar_tick null guards", "[c_api_timesig]")
{
    EngineFixture f;
    uint64_t out = 0u;
    CHECK(omega_next_bar_tick(nullptr, 0u, &out) == OMEGA_ERR_INVALID);
    CHECK(omega_next_bar_tick(f.eng, 0u, nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: omega_quantize_to_beat null guards", "[c_api_timesig]")
{
    EngineFixture f;
    uint64_t out = 0u;
    CHECK(omega_quantize_to_beat(nullptr, 0u, &out) == OMEGA_ERR_INVALID);
    CHECK(omega_quantize_to_beat(f.eng, 0u, nullptr) == OMEGA_ERR_INVALID);
}

// ── Basic timesig round-trip (via engine command queue) ───────────────────────

TEST_CASE("C API: timesig freeform by default", "[c_api_timesig]")
{
    EngineFixture f;
    CHECK(omega_timesig_is_freeform(f.eng) == 1);
}

TEST_CASE("C API: timesig_set enqueues and applies after process", "[c_api_timesig]")
{
    EngineFixture f;
    REQUIRE(omega_timesig_set(f.eng, 0u, 4u, 4u) == OMEGA_OK);
    omega_engine_process(f.eng);  // drain queue
    CHECK(omega_timesig_is_freeform(f.eng) == 0);

    omega_time_sig_point_t pt{};
    REQUIRE(omega_timesig_at(f.eng, 0u, &pt) == OMEGA_OK);
    CHECK(pt.tick == 0u);
    CHECK(pt.numerator == 4u);
    CHECK(pt.denominator == 4u);
}

TEST_CASE("C API: timesig_clear enters freeform mode after process", "[c_api_timesig]")
{
    EngineFixture f;
    REQUIRE(omega_timesig_set(f.eng, 0u, 4u, 4u) == OMEGA_OK);
    omega_engine_process(f.eng);
    REQUIRE(omega_timesig_is_freeform(f.eng) == 0);

    REQUIRE(omega_timesig_clear(f.eng) == OMEGA_OK);
    omega_engine_process(f.eng);
    CHECK(omega_timesig_is_freeform(f.eng) == 1);
}

TEST_CASE("C API: tick_to_beat_pos freeform returns NO_METER", "[c_api_timesig]")
{
    EngineFixture f;
    omega_beat_pos_t pos{};
    CHECK(omega_tick_to_beat_pos(f.eng, 0u, &pos) == OMEGA_ERR_NO_METER);
}

TEST_CASE("C API: tick_to_beat_pos after timesig_set", "[c_api_timesig]")
{
    EngineFixture f;
    REQUIRE(omega_timesig_set(f.eng, 0u, 4u, 4u) == OMEGA_OK);
    omega_engine_process(f.eng);

    omega_beat_pos_t pos{};
    REQUIRE(omega_tick_to_beat_pos(f.eng, 0u, &pos) == OMEGA_OK);
    CHECK(pos.bar == 1u);
    CHECK(pos.beat == 1u);
    CHECK(pos.subdivision == 0u);
}

TEST_CASE("C API: beat_pos_to_tick round-trip", "[c_api_timesig]")
{
    EngineFixture f;
    REQUIRE(omega_timesig_set(f.eng, 0u, 4u, 4u) == OMEGA_OK);
    omega_engine_process(f.eng);

    // Bar 2, beat 3, subdivision 100
    omega_beat_pos_t pos{2u, 3u, 100u};
    uint64_t tick = 0u;
    REQUIRE(omega_beat_pos_to_tick(f.eng, &pos, &tick) == OMEGA_OK);

    omega_beat_pos_t back{};
    REQUIRE(omega_tick_to_beat_pos(f.eng, tick, &back) == OMEGA_OK);
    CHECK(back.bar == pos.bar);
    CHECK(back.beat == pos.beat);
    CHECK(back.subdivision == pos.subdivision);
}

TEST_CASE("C API: next_bar_tick returns NO_METER for freeform", "[c_api_timesig]")
{
    EngineFixture f;
    uint64_t out = 0u;
    CHECK(omega_next_bar_tick(f.eng, 0u, &out) == OMEGA_ERR_NO_METER);
}

TEST_CASE("C API: next_bar_tick returns bar boundary", "[c_api_timesig]")
{
    EngineFixture f;
    REQUIRE(omega_timesig_set(f.eng, 0u, 4u, 4u) == OMEGA_OK);
    omega_engine_process(f.eng);

    constexpr uint64_t k_bar = 480u * 4u;  // 4/4 at 480 PPQN = 1920 ticks
    uint64_t out = 0u;
    REQUIRE(omega_next_bar_tick(f.eng, 1u, &out) == OMEGA_OK);
    CHECK(out == k_bar);
}

TEST_CASE("C API: timesig_set invalid denominator returns INVALID", "[c_api_timesig]")
{
    EngineFixture f;
    CHECK(omega_timesig_set(f.eng, 0u, 4u, 3u) == OMEGA_ERR_INVALID);
}

// ── SMPTE null guards ─────────────────────────────────────────────────────────

TEST_CASE("C API: omega_smpte_config_set null guards", "[c_api_smpte]")
{
    EngineFixture f;
    omega_smpte_config_t cfg{30u, 0u, 0u};
    CHECK(omega_smpte_config_set(nullptr, &cfg) == OMEGA_ERR_INVALID);
    CHECK(omega_smpte_config_set(f.eng, nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: omega_smpte_config_clear null engine returns INVALID", "[c_api_smpte]")
{
    CHECK(omega_smpte_config_clear(nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: omega_tick_to_smpte null guards", "[c_api_smpte]")
{
    EngineFixture f;
    omega_smpte_time_t t{};
    CHECK(omega_tick_to_smpte(nullptr, 0u, &t) == OMEGA_ERR_INVALID);
    CHECK(omega_tick_to_smpte(f.eng, 0u, nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: omega_smpte_to_tick null guards", "[c_api_smpte]")
{
    EngineFixture f;
    omega_smpte_time_t t{};
    uint64_t out = 0u;
    CHECK(omega_smpte_to_tick(nullptr, &t, &out) == OMEGA_ERR_INVALID);
    CHECK(omega_smpte_to_tick(f.eng, nullptr, &out) == OMEGA_ERR_INVALID);
    CHECK(omega_smpte_to_tick(f.eng, &t, nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: smpte without config returns NO_SMPTE_CONFIG", "[c_api_smpte]")
{
    EngineFixture f;
    omega_smpte_time_t t{};
    uint64_t out = 0u;
    CHECK(omega_tick_to_smpte(f.eng, 0u, &t) == OMEGA_ERR_NO_SMPTE_CONFIG);
    CHECK(omega_smpte_to_tick(f.eng, &t, &out) == OMEGA_ERR_NO_SMPTE_CONFIG);
}

TEST_CASE("C API: smpte_config_set invalid config returns INVALID", "[c_api_smpte]")
{
    EngineFixture f;
    omega_smpte_config_t bad{0u, 0u, 0u};
    CHECK(omega_smpte_config_set(f.eng, &bad) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: smpte round-trip 30fps NDF", "[c_api_smpte]")
{
    // Use a short timecode well within the safe TempoMap range (~5 min at 120 BPM)
    EngineFixture f;
    omega_smpte_config_t cfg{30u, 0u, 0u};
    REQUIRE(omega_smpte_config_set(f.eng, &cfg) == OMEGA_OK);
    omega_engine_process(f.eng);

    omega_smpte_time_t orig{0u, 0u, 5u, 3u};  // 00:00:05:03 (frame 153 = 51*3, exact at 120 BPM)
    uint64_t tick = 0u;
    REQUIRE(omega_smpte_to_tick(f.eng, &orig, &tick) == OMEGA_OK);
    omega_smpte_time_t back{};
    REQUIRE(omega_tick_to_smpte(f.eng, tick, &back) == OMEGA_OK);
    CHECK(back.hours == orig.hours);
    CHECK(back.minutes == orig.minutes);
    CHECK(back.seconds == orig.seconds);
    CHECK(back.frames == orig.frames);
}

TEST_CASE("C API: smpte_config_clear removes config", "[c_api_smpte]")
{
    EngineFixture f;
    omega_smpte_config_t cfg{30u, 0u, 0u};
    REQUIRE(omega_smpte_config_set(f.eng, &cfg) == OMEGA_OK);
    omega_engine_process(f.eng);

    REQUIRE(omega_smpte_config_clear(f.eng) == OMEGA_OK);
    omega_engine_process(f.eng);

    omega_smpte_time_t t{};
    CHECK(omega_tick_to_smpte(f.eng, 0u, &t) == OMEGA_ERR_NO_SMPTE_CONFIG);
}

// ── OMEGA_CUE_BAR ─────────────────────────────────────────────────────────────

TEST_CASE("C API: OMEGA_CUE_BAR value is distinct from other modes", "[c_api_cue_bar]")
{
    CHECK(OMEGA_CUE_BAR != OMEGA_CUE_IMMEDIATE);
    CHECK(OMEGA_CUE_BAR != OMEGA_CUE_AT_BOUNDARY);
    CHECK(OMEGA_CUE_BAR == 2);
}

TEST_CASE("C API: omega_perf_cue with OMEGA_CUE_BAR enqueues OK", "[c_api_cue_bar]")
{
    EngineFixture f;
    // Set a 4/4 time signature first
    REQUIRE(omega_timesig_set(f.eng, 0u, 4u, 4u) == OMEGA_OK);

    // Create a pattern and assign it to slot 0
    omega_pattern_id_t pat = omega_pattern_create(f.eng, "test", 1920u);
    REQUIRE(pat != OMEGA_PATTERN_INVALID);

    REQUIRE(omega_perf_assign(f.eng, 0u, pat) == OMEGA_OK);
    // OMEGA_CUE_BAR must enqueue without error
    CHECK(omega_perf_cue(f.eng, 0u, OMEGA_CUE_BAR) == OMEGA_OK);
}
