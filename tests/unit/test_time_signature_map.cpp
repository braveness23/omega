#include <omega/time_signature_map.h>

#include <catch2/catch_test_macros.hpp>

using namespace omega;

// ── Helper constants ──────────────────────────────────────────────────────────

// At 480 PPQN:
// ticks_per_beat_for(4) = 480*4/4 = 480
// ticks_per_bar_for(4,4) = 480 * 4 = 1920
// ticks_per_beat_for(8) = 480*4/8 = 240
// ticks_per_bar_for(3,8) = 240 * 3 = 720
// ticks_per_beat_for(2) = 480*4/2 = 960
// ticks_per_bar_for(2,2) = 960 * 2 = 1920

static constexpr uint64_t k_4_4_bar = 1920;
static constexpr uint64_t k_3_4_bar = 1440;
static constexpr uint64_t k_3_8_bar = 720;
static constexpr uint64_t k_beat_4 = 480;
static constexpr uint64_t k_beat_8 = 240;

// ── TimeSignatureMap ──────────────────────────────────────────────────────────

TEST_CASE("TimeSignatureMap: empty map is freeform", "[timesig]")
{
    TimeSignatureMap m;
    CHECK(m.is_freeform());
    CHECK(m.size() == 0u);
    CHECK(m.at(0u) == nullptr);
}

TEST_CASE("TimeSignatureMap: insert single entry", "[timesig]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    REQUIRE(m.size() == 1u);
    CHECK(!m.is_freeform());
    const auto* pt = m.at(0u);
    REQUIRE(pt != nullptr);
    CHECK(pt->tick == 0u);
    CHECK(pt->numerator == 4u);
    CHECK(pt->denominator == 4u);
}

TEST_CASE("TimeSignatureMap: replace existing entry", "[timesig]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    REQUIRE(m.insert(0u, 3u, 4u) == OMEGA_OK);
    CHECK(m.size() == 1u);
    const auto* pt = m.at(0u);
    REQUIRE(pt != nullptr);
    CHECK(pt->numerator == 3u);
}

TEST_CASE("TimeSignatureMap: insert sorted by tick", "[timesig]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(1920u, 3u, 4u) == OMEGA_OK);
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    REQUIRE(m.size() == 2u);
    CHECK(m.points()[0].tick == 0u);
    CHECK(m.points()[1].tick == 1920u);
}

TEST_CASE("TimeSignatureMap: invalid denominator rejected", "[timesig]")
{
    TimeSignatureMap m;
    CHECK(m.insert(0u, 4u, 3u) == OMEGA_ERR_INVALID);
    CHECK(m.insert(0u, 4u, 0u) == OMEGA_ERR_INVALID);
    CHECK(m.insert(0u, 4u, 64u) == OMEGA_ERR_INVALID);
    CHECK(m.is_freeform());
}

TEST_CASE("TimeSignatureMap: zero numerator rejected", "[timesig]")
{
    TimeSignatureMap m;
    CHECK(m.insert(0u, 0u, 4u) == OMEGA_ERR_INVALID);
    CHECK(m.is_freeform());
}

TEST_CASE("TimeSignatureMap: valid denominators accepted", "[timesig]")
{
    TimeSignatureMap m;
    for (uint8_t d : {1u, 2u, 4u, 8u, 16u, 32u})
    {
        CHECK(m.insert(static_cast<uint64_t>(d) * 1000u, 4u, d) == OMEGA_OK);
    }
    CHECK(m.size() == 6u);
}

TEST_CASE("TimeSignatureMap: remove existing entry", "[timesig]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    REQUIRE(m.insert(1920u, 3u, 4u) == OMEGA_OK);
    CHECK(m.remove(0u) == OMEGA_OK);
    CHECK(m.size() == 1u);
    CHECK(m.points()[0].tick == 1920u);
}

TEST_CASE("TimeSignatureMap: remove non-existent returns NOT_FOUND", "[timesig]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    CHECK(m.remove(100u) == OMEGA_ERR_NOT_FOUND);
}

TEST_CASE("TimeSignatureMap: clear enters freeform mode", "[timesig]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    m.clear();
    CHECK(m.is_freeform());
}

TEST_CASE("TimeSignatureMap: at returns nullptr before first entry", "[timesig]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(1920u, 4u, 4u) == OMEGA_OK);
    CHECK(m.at(0u) == nullptr);
    CHECK(m.at(1919u) == nullptr);
}

TEST_CASE("TimeSignatureMap: at returns correct active entry", "[timesig]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    REQUIRE(m.insert(1920u, 3u, 4u) == OMEGA_OK);

    const auto* a = m.at(0u);
    REQUIRE(a != nullptr);
    CHECK(a->numerator == 4u);

    const auto* b = m.at(1919u);
    REQUIRE(b != nullptr);
    CHECK(b->numerator == 4u);

    const auto* c = m.at(1920u);
    REQUIRE(c != nullptr);
    CHECK(c->numerator == 3u);

    const auto* d = m.at(99999u);
    REQUIRE(d != nullptr);
    CHECK(d->numerator == 3u);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

TEST_CASE("ticks_per_beat_for: 4/4", "[timesig_helpers]")
{
    CHECK(ticks_per_beat_for(4u) == k_beat_4);
}

TEST_CASE("ticks_per_bar_for: 4/4", "[timesig_helpers]")
{
    CHECK(ticks_per_bar_for(4u, 4u) == k_4_4_bar);
}

TEST_CASE("ticks_per_bar_for: 3/4", "[timesig_helpers]")
{
    CHECK(ticks_per_bar_for(3u, 4u) == k_3_4_bar);
}

TEST_CASE("ticks_per_bar_for: 3/8", "[timesig_helpers]")
{
    CHECK(ticks_per_bar_for(3u, 8u) == k_3_8_bar);
}

// ── MeterCursor: tick_to_beat_pos ─────────────────────────────────────────────

TEST_CASE("MeterCursor: freeform returns NO_METER", "[meter_cursor]")
{
    TimeSignatureMap m;
    MeterCursor cur(m);
    BeatPosition pos{};
    CHECK(cur.tick_to_beat_pos(0u, pos) == OMEGA_ERR_NO_METER);
}

TEST_CASE("MeterCursor: tick before first entry returns NO_METER", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(1920u, 4u, 4u) == OMEGA_OK);
    MeterCursor cur(m);
    BeatPosition pos{};
    CHECK(cur.tick_to_beat_pos(0u, pos) == OMEGA_ERR_NO_METER);
}

TEST_CASE("MeterCursor: 4/4 bar 1 beat 1", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    MeterCursor cur(m);
    BeatPosition pos{};
    REQUIRE(cur.tick_to_beat_pos(0u, pos) == OMEGA_OK);
    CHECK(pos.bar == 1u);
    CHECK(pos.beat == 1u);
    CHECK(pos.subdivision == 0u);
}

TEST_CASE("MeterCursor: 4/4 beat 3 of bar 1", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    MeterCursor cur(m);
    BeatPosition pos{};
    // beat 3 = 2 beats in = 2*480 = 960 ticks
    REQUIRE(cur.tick_to_beat_pos(960u, pos) == OMEGA_OK);
    CHECK(pos.bar == 1u);
    CHECK(pos.beat == 3u);
    CHECK(pos.subdivision == 0u);
}

TEST_CASE("MeterCursor: 4/4 bar 2 beat 1", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    MeterCursor cur(m);
    BeatPosition pos{};
    REQUIRE(cur.tick_to_beat_pos(k_4_4_bar, pos) == OMEGA_OK);
    CHECK(pos.bar == 2u);
    CHECK(pos.beat == 1u);
    CHECK(pos.subdivision == 0u);
}

TEST_CASE("MeterCursor: subdivision within beat", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    MeterCursor cur(m);
    BeatPosition pos{};
    // tick 100 = bar 1, beat 1, subdivision 100
    REQUIRE(cur.tick_to_beat_pos(100u, pos) == OMEGA_OK);
    CHECK(pos.bar == 1u);
    CHECK(pos.beat == 1u);
    CHECK(pos.subdivision == 100u);
}

TEST_CASE("MeterCursor: meter change mid-sequence", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    REQUIRE(m.insert(k_4_4_bar * 2u, 3u, 4u) == OMEGA_OK);  // 3/4 starts at bar 3

    MeterCursor cur(m);
    BeatPosition pos{};

    // Tick in 4/4 section: bar 2 beat 2
    REQUIRE(cur.tick_to_beat_pos(k_4_4_bar + k_beat_4, pos) == OMEGA_OK);
    CHECK(pos.bar == 2u);
    CHECK(pos.beat == 2u);

    // Tick at start of 3/4 section: bar 3 beat 1
    REQUIRE(cur.tick_to_beat_pos(k_4_4_bar * 2u, pos) == OMEGA_OK);
    CHECK(pos.bar == 3u);
    CHECK(pos.beat == 1u);

    // Tick in 3/4 section: bar 4 beat 2
    REQUIRE(cur.tick_to_beat_pos(k_4_4_bar * 2u + k_3_4_bar + k_beat_4, pos) == OMEGA_OK);
    CHECK(pos.bar == 4u);
    CHECK(pos.beat == 2u);
}

// ── MeterCursor: beat_pos_to_tick ─────────────────────────────────────────────

TEST_CASE("MeterCursor: beat_pos_to_tick freeform returns NO_METER", "[meter_cursor]")
{
    TimeSignatureMap m;
    MeterCursor cur(m);
    uint64_t tick = 0u;
    BeatPosition pos{1u, 1u, 0u};
    CHECK(cur.beat_pos_to_tick(pos, tick) == OMEGA_ERR_NO_METER);
}

TEST_CASE("MeterCursor: beat_pos_to_tick zero bar/beat returns INVALID", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    MeterCursor cur(m);
    uint64_t tick = 0u;
    CHECK(cur.beat_pos_to_tick({0u, 1u, 0u}, tick) == OMEGA_ERR_INVALID);
    CHECK(cur.beat_pos_to_tick({1u, 0u, 0u}, tick) == OMEGA_ERR_INVALID);
}

TEST_CASE("MeterCursor: beat_pos_to_tick beat > numerator returns INVALID", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    MeterCursor cur(m);
    uint64_t tick = 0u;
    CHECK(cur.beat_pos_to_tick({1u, 5u, 0u}, tick) == OMEGA_ERR_INVALID);
}

TEST_CASE("MeterCursor: tick_to_beat_pos / beat_pos_to_tick round-trip", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    REQUIRE(m.insert(k_4_4_bar * 4u, 3u, 4u) == OMEGA_OK);
    MeterCursor cur(m);

    // Round-trip several ticks
    for (uint64_t tick : {0u, 100u, 480u, 960u, 1920u, 3840u, 5760u, 5900u})
    {
        BeatPosition pos{};
        REQUIRE(cur.tick_to_beat_pos(tick, pos) == OMEGA_OK);
        uint64_t out_tick = 0u;
        REQUIRE(cur.beat_pos_to_tick(pos, out_tick) == OMEGA_OK);
        CHECK(out_tick == tick);
    }
}

// ── MeterCursor: next_bar_tick ────────────────────────────────────────────────

TEST_CASE("MeterCursor: next_bar_tick freeform returns NO_METER", "[meter_cursor]")
{
    TimeSignatureMap m;
    MeterCursor cur(m);
    uint64_t out = 0u;
    CHECK(cur.next_bar_tick(0u, out) == OMEGA_ERR_NO_METER);
}

TEST_CASE("MeterCursor: next_bar_tick on boundary returns same tick", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    MeterCursor cur(m);
    uint64_t out = 0u;
    REQUIRE(cur.next_bar_tick(0u, out) == OMEGA_OK);
    CHECK(out == 0u);
    REQUIRE(cur.next_bar_tick(k_4_4_bar, out) == OMEGA_OK);
    CHECK(out == k_4_4_bar);
}

TEST_CASE("MeterCursor: next_bar_tick mid-bar advances to next bar", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    MeterCursor cur(m);
    uint64_t out = 0u;
    REQUIRE(cur.next_bar_tick(1u, out) == OMEGA_OK);
    CHECK(out == k_4_4_bar);
    REQUIRE(cur.next_bar_tick(k_4_4_bar - 1u, out) == OMEGA_OK);
    CHECK(out == k_4_4_bar);
}

TEST_CASE("MeterCursor: next_bar_tick crosses time-sig boundary", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    REQUIRE(m.insert(k_4_4_bar, 3u, 4u) == OMEGA_OK);
    MeterCursor cur(m);

    // Asking from inside bar 1 of 4/4: the time-sig change at tick 1920 is a bar boundary
    uint64_t out = 0u;
    REQUIRE(cur.next_bar_tick(1u, out) == OMEGA_OK);
    CHECK(out == k_4_4_bar);  // new time sig starts here (implicit bar boundary)
}

// ── MeterCursor: next_beat_tick ───────────────────────────────────────────────

TEST_CASE("MeterCursor: next_beat_tick on boundary returns same tick", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    MeterCursor cur(m);
    uint64_t out = 0u;
    REQUIRE(cur.next_beat_tick(0u, out) == OMEGA_OK);
    CHECK(out == 0u);
    REQUIRE(cur.next_beat_tick(k_beat_4, out) == OMEGA_OK);
    CHECK(out == k_beat_4);
}

TEST_CASE("MeterCursor: next_beat_tick advances to next beat", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    MeterCursor cur(m);
    uint64_t out = 0u;
    REQUIRE(cur.next_beat_tick(1u, out) == OMEGA_OK);
    CHECK(out == k_beat_4);
    REQUIRE(cur.next_beat_tick(k_beat_4 - 1u, out) == OMEGA_OK);
    CHECK(out == k_beat_4);
}

// ── MeterCursor: quantize_to_beat ────────────────────────────────────────────

TEST_CASE("MeterCursor: quantize_to_beat rounds down when remainder < half", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    MeterCursor cur(m);
    uint64_t out = 0u;
    REQUIRE(cur.quantize_to_beat(k_beat_4 / 2u - 1u, out) == OMEGA_OK);
    CHECK(out == 0u);
}

TEST_CASE("MeterCursor: quantize_to_beat rounds up when remainder >= half", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    MeterCursor cur(m);
    uint64_t out = 0u;
    REQUIRE(cur.quantize_to_beat(k_beat_4 / 2u, out) == OMEGA_OK);
    CHECK(out == k_beat_4);  // round-half-up
}

TEST_CASE("MeterCursor: quantize_to_beat exact boundary stays", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    MeterCursor cur(m);
    uint64_t out = 0u;
    REQUIRE(cur.quantize_to_beat(k_beat_4, out) == OMEGA_OK);
    CHECK(out == k_beat_4);
}

// ── MeterCursor: quantize_to_subdivision ─────────────────────────────────────

TEST_CASE("MeterCursor: quantize_to_subdivision zero returns INVALID", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    MeterCursor cur(m);
    uint64_t out = 0u;
    CHECK(cur.quantize_to_subdivision(0u, 0u, out) == OMEGA_ERR_INVALID);
}

TEST_CASE("MeterCursor: quantize_to_subdivision rounds to nearest", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    MeterCursor cur(m);
    constexpr uint64_t k_eighth = k_beat_4 / 2u;  // 240 ticks
    constexpr uint64_t k_half = k_eighth / 2u;    // 120 ticks (half of one subdivision)
    uint64_t out = 0u;
    // 119 ticks from 0 — remainder 119 < half (120) → round down to 0
    REQUIRE(cur.quantize_to_subdivision(k_half - 1u, k_eighth, out) == OMEGA_OK);
    CHECK(out == 0u);
    // 120 ticks from 0 — remainder 120 == half → round up to 240 (round-half-up)
    REQUIRE(cur.quantize_to_subdivision(k_half, k_eighth, out) == OMEGA_OK);
    CHECK(out == k_eighth);
    // exact boundary stays
    REQUIRE(cur.quantize_to_subdivision(k_eighth, k_eighth, out) == OMEGA_OK);
    CHECK(out == k_eighth);
    // 361 ticks from 0 — in second subdivision (240..479); remainder = 121 >= half (120) → 480
    REQUIRE(cur.quantize_to_subdivision(k_eighth + k_half + 1u, k_eighth, out) == OMEGA_OK);
    CHECK(out == k_eighth * 2u);
}

// ── MeterCursor: PositionConverter overrides ─────────────────────────────────

TEST_CASE("MeterCursor: quantize() delegates to quantize_to_beat", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    MeterCursor cur(m);
    uint64_t out1 = 0u;
    uint64_t out2 = 0u;
    REQUIRE(cur.quantize(100u, out1) == OMEGA_OK);
    REQUIRE(cur.quantize_to_beat(100u, out2) == OMEGA_OK);
    CHECK(out1 == out2);
}

TEST_CASE("MeterCursor: next_boundary() delegates to next_bar_tick", "[meter_cursor]")
{
    TimeSignatureMap m;
    REQUIRE(m.insert(0u, 4u, 4u) == OMEGA_OK);
    MeterCursor cur(m);
    uint64_t out1 = 0u;
    uint64_t out2 = 0u;
    REQUIRE(cur.next_boundary(100u, out1) == OMEGA_OK);
    REQUIRE(cur.next_bar_tick(100u, out2) == OMEGA_OK);
    CHECK(out1 == out2);
}
