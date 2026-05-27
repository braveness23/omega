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

TEST_CASE("C API: omega_tempo_set null engine returns INVALID", "[c_api_tempo]")
{
    CHECK(omega_tempo_set(nullptr, 0u, 120000u) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: omega_tempo_set zero bpm_milli returns INVALID", "[c_api_tempo]")
{
    EngineFixture f;
    CHECK(omega_tempo_set(f.eng, 0u, 0u) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: omega_tempo_remove null engine returns INVALID", "[c_api_tempo]")
{
    CHECK(omega_tempo_remove(nullptr, 0u) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: omega_tempo_at null engine returns INVALID", "[c_api_tempo]")
{
    uint32_t bpm = 0u;
    CHECK(omega_tempo_at(nullptr, 0u, &bpm) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: omega_tempo_at null out returns INVALID", "[c_api_tempo]")
{
    EngineFixture f;
    CHECK(omega_tempo_at(f.eng, 0u, nullptr) == OMEGA_ERR_INVALID);
}

// ── Default state ─────────────────────────────────────────────────────────────

TEST_CASE("C API: default tempo is 120 BPM at tick 0", "[c_api_tempo]")
{
    EngineFixture f;
    uint32_t bpm = 0u;
    REQUIRE(omega_tempo_at(f.eng, 0u, &bpm) == OMEGA_OK);
    CHECK(bpm == 120000u);
}

TEST_CASE("C API: omega_tempo_at queries default tempo at any tick", "[c_api_tempo]")
{
    EngineFixture f;
    // Without any changes, the default 120 BPM applies everywhere.
    uint32_t bpm = 0u;
    REQUIRE(omega_tempo_at(f.eng, 9600u, &bpm) == OMEGA_OK);
    CHECK(bpm == 120000u);
}

// ── Round-trip: set then query ────────────────────────────────────────────────

TEST_CASE("C API: omega_tempo_set enqueues and applies after process", "[c_api_tempo]")
{
    EngineFixture f;
    REQUIRE(omega_tempo_set(f.eng, 0u, 140000u) == OMEGA_OK);
    omega_engine_process(f.eng);  // drain queue

    uint32_t bpm = 0u;
    REQUIRE(omega_tempo_at(f.eng, 0u, &bpm) == OMEGA_OK);
    CHECK(bpm == 140000u);
}

TEST_CASE("C API: omega_tempo_set at non-zero tick inserts automation point", "[c_api_tempo]")
{
    EngineFixture f;
    constexpr uint64_t k_half_bar = 480u * 2u;  // 2 beats into a default 4/4 bar

    REQUIRE(omega_tempo_set(f.eng, k_half_bar, 200000u) == OMEGA_OK);
    omega_engine_process(f.eng);

    // Before the change point the default 120 BPM is still in effect.
    uint32_t bpm = 0u;
    REQUIRE(omega_tempo_at(f.eng, 0u, &bpm) == OMEGA_OK);
    CHECK(bpm == 120000u);

    // At and after the change point, 200 BPM takes over.
    REQUIRE(omega_tempo_at(f.eng, k_half_bar, &bpm) == OMEGA_OK);
    CHECK(bpm == 200000u);

    REQUIRE(omega_tempo_at(f.eng, k_half_bar + 1u, &bpm) == OMEGA_OK);
    CHECK(bpm == 200000u);
}

TEST_CASE("C API: omega_tempo_set replaces an existing point at the same tick", "[c_api_tempo]")
{
    EngineFixture f;
    REQUIRE(omega_tempo_set(f.eng, 0u, 100000u) == OMEGA_OK);
    omega_engine_process(f.eng);
    REQUIRE(omega_tempo_set(f.eng, 0u, 180000u) == OMEGA_OK);
    omega_engine_process(f.eng);

    uint32_t bpm = 0u;
    REQUIRE(omega_tempo_at(f.eng, 0u, &bpm) == OMEGA_OK);
    CHECK(bpm == 180000u);
}

// ── Remove ────────────────────────────────────────────────────────────────────

TEST_CASE("C API: omega_tempo_remove non-zero tick removes the point", "[c_api_tempo]")
{
    EngineFixture f;
    constexpr uint64_t k_tick = 4800u;

    REQUIRE(omega_tempo_set(f.eng, k_tick, 160000u) == OMEGA_OK);
    omega_engine_process(f.eng);

    uint32_t bpm = 0u;
    REQUIRE(omega_tempo_at(f.eng, k_tick, &bpm) == OMEGA_OK);
    CHECK(bpm == 160000u);

    REQUIRE(omega_tempo_remove(f.eng, k_tick) == OMEGA_OK);
    omega_engine_process(f.eng);

    // After removal the default 120 BPM is back in effect everywhere.
    REQUIRE(omega_tempo_at(f.eng, k_tick, &bpm) == OMEGA_OK);
    CHECK(bpm == 120000u);
}

TEST_CASE("C API: omega_tempo_remove tick 0 is a no-op", "[c_api_tempo]")
{
    EngineFixture f;
    REQUIRE(omega_tempo_set(f.eng, 0u, 150000u) == OMEGA_OK);
    omega_engine_process(f.eng);

    // Attempt to remove tick=0 should be accepted (enqueued) but be a no-op.
    REQUIRE(omega_tempo_remove(f.eng, 0u) == OMEGA_OK);
    omega_engine_process(f.eng);

    // The explicit 150 BPM at tick 0 should still be in place.
    uint32_t bpm = 0u;
    REQUIRE(omega_tempo_at(f.eng, 0u, &bpm) == OMEGA_OK);
    CHECK(bpm == 150000u);
}

TEST_CASE("C API: omega_tempo_remove non-existent tick is a no-op", "[c_api_tempo]")
{
    EngineFixture f;
    // Nothing at tick 9999; remove should enqueue without error.
    REQUIRE(omega_tempo_remove(f.eng, 9999u) == OMEGA_OK);
    omega_engine_process(f.eng);

    uint32_t bpm = 0u;
    REQUIRE(omega_tempo_at(f.eng, 0u, &bpm) == OMEGA_OK);
    CHECK(bpm == 120000u);
}

// ── omega_sink_id ─────────────────────────────────────────────────────────────

TEST_CASE("C API: omega_sink_id null returns 0", "[c_api_tempo]")
{
    CHECK(omega_sink_id(nullptr) == 0u);
}
