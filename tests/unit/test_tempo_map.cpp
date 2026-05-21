#include <omega/tempo_map.h>

#include <catch2/catch_test_macros.hpp>

using omega::TempoMap;

TEST_CASE("TempoMap default: tick 0 and ns 0 are co-located")
{
    TempoMap tm;
    REQUIRE(tm.ticks_to_ns(0u) == 0u);
    REQUIRE(tm.ns_to_ticks(0u) == 0u);
}

TEST_CASE("TempoMap one beat at 120 BPM equals 500 ms")
{
    TempoMap tm;
    /* 480 ticks at 120 BPM = one quarter note = 500,000,000 ns */
    REQUIRE(tm.ticks_to_ns(480u) == 500'000'000ULL);
}

TEST_CASE("TempoMap ticks_to_ns is linear at 120 BPM")
{
    TempoMap tm;
    REQUIRE(tm.ticks_to_ns(0u) == 0ULL);
    REQUIRE(tm.ticks_to_ns(480u) == 500'000'000ULL);
    REQUIRE(tm.ticks_to_ns(960u) == 1'000'000'000ULL);
}

TEST_CASE("TempoMap ns_to_ticks round-trip at 120 BPM")
{
    TempoMap tm;
    REQUIRE(tm.ns_to_ticks(500'000'000ULL) == 480u);
    REQUIRE(tm.ns_to_ticks(1'000'000'000ULL) == 960u);
}

TEST_CASE("TempoMap ticks_to_ns / ns_to_ticks round-trip for beat-aligned values")
{
    TempoMap tm;
    /* Use PPQN-aligned ticks where the integer conversion is lossless.
     * Non-aligned values (e.g. tick=100) truncate and do not round-trip exactly. */
    for (uint64_t tick : {0ULL, 480ULL, 960ULL, 4'800ULL, 48'000ULL})
    {
        uint64_t ns = tm.ticks_to_ns(tick);
        REQUIRE(tm.ns_to_ticks(ns) == tick);
    }
}

TEST_CASE("TempoMap multi-point: tempo change mid-session")
{
    TempoMap tm;
    /* First beat at 120 BPM (0–480 ticks = 0–500 ms), then 240 BPM */
    tm.insert(480u, 240'000u);

    /* Before the change: same as pure 120 BPM */
    REQUIRE(tm.ticks_to_ns(0u) == 0ULL);
    REQUIRE(tm.ticks_to_ns(480u) == 500'000'000ULL);

    /* After: 240 BPM = 250 ms per beat; tick 960 = 500 ms + 250 ms = 750 ms */
    REQUIRE(tm.ticks_to_ns(960u) == 750'000'000ULL);
}

TEST_CASE("TempoMap multi-point ns_to_ticks round-trip")
{
    TempoMap tm;
    tm.insert(480u, 240'000u);

    REQUIRE(tm.ns_to_ticks(500'000'000ULL) == 480u);
    REQUIRE(tm.ns_to_ticks(750'000'000ULL) == 960u);
}

TEST_CASE("TempoMap insert replaces existing point at same tick")
{
    TempoMap tm;
    tm.insert(0u, 240'000u); /* replace default 120 BPM at tick 0 */

    /* Now one beat should take 250 ms instead of 500 ms */
    REQUIRE(tm.ticks_to_ns(480u) == 250'000'000ULL);
}

TEST_CASE("TempoMap three-segment accuracy")
{
    TempoMap tm;
    /* 120 BPM for beat 0-1, 60 BPM for beat 1-2, 240 BPM for beat 2+ */
    tm.insert(480u, 60'000u);  /* 1000 ms per beat */
    tm.insert(960u, 240'000u); /* 250 ms per beat */

    /* tick 480 = 500 ms (120 BPM) */
    REQUIRE(tm.ticks_to_ns(480u) == 500'000'000ULL);
    /* tick 960 = 500 ms + 1000 ms = 1500 ms (60 BPM) */
    REQUIRE(tm.ticks_to_ns(960u) == 1'500'000'000ULL);
    /* tick 1440 = 1500 ms + 250 ms = 1750 ms (240 BPM) */
    REQUIRE(tm.ticks_to_ns(1440u) == 1'750'000'000ULL);
}
