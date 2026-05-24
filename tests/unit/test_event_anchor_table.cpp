#include <omega/event_anchor_table.h>

#include <catch2/catch_test_macros.hpp>

using namespace omega;

TEST_CASE("EventAnchorTable: empty table", "[event_anchor_table]")
{
    EventAnchorTable t;
    CHECK(t.size() == 0u);
    CHECK(t.get(1u, 0u) == nullptr);
    CHECK(t.remove(1u, 0u) == OMEGA_ERR_NOT_FOUND);
}

TEST_CASE("EventAnchorTable: add and retrieve anchor for event", "[event_anchor_table]")
{
    EventAnchorTable t;
    t.get_or_create(1u, 0u).add("beat", 0u, ANCHOR_SNAP);

    const AnchorList* al = t.get(1u, 0u);
    REQUIRE(al != nullptr);
    REQUIRE(al->size() == 1u);
    const AnchorPoint* a = al->at(0);
    REQUIRE(a != nullptr);
    CHECK(a->name == "beat");
    CHECK(a->offset_ticks == 0u);
    CHECK((a->flags & ANCHOR_SNAP) != 0u);
}

TEST_CASE("EventAnchorTable: missing key returns null", "[event_anchor_table]")
{
    EventAnchorTable t;
    t.get_or_create(1u, 0u).add("x", 0u, 0u);

    CHECK(t.get(1u, 1u) == nullptr);  // different event_index
    CHECK(t.get(2u, 0u) == nullptr);  // different container_id
}

TEST_CASE("EventAnchorTable: remove entry", "[event_anchor_table]")
{
    EventAnchorTable t;
    t.get_or_create(3u, 7u).add("y", 100u, ANCHOR_CUE);

    REQUIRE(t.get(3u, 7u) != nullptr);
    CHECK(t.remove(3u, 7u) == OMEGA_OK);
    CHECK(t.get(3u, 7u) == nullptr);
    CHECK(t.remove(3u, 7u) == OMEGA_ERR_NOT_FOUND);
}

TEST_CASE("EventAnchorTable: multiple events with independent anchor lists", "[event_anchor_table]")
{
    EventAnchorTable t;
    t.get_or_create(1u, 0u).add("snap", 0u, ANCHOR_SNAP);
    t.get_or_create(1u, 1u).add("warp", 240u, ANCHOR_WARP);
    t.get_or_create(2u, 0u).add("cue", 480u, ANCHOR_CUE);

    CHECK(t.size() == 3u);

    const AnchorList* a0 = t.get(1u, 0u);
    REQUIRE(a0 != nullptr);
    CHECK(a0->size() == 1u);
    CHECK(a0->at(0)->name == "snap");

    const AnchorList* a1 = t.get(1u, 1u);
    REQUIRE(a1 != nullptr);
    CHECK(a1->size() == 1u);
    CHECK(a1->at(0)->name == "warp");

    const AnchorList* a2 = t.get(2u, 0u);
    REQUIRE(a2 != nullptr);
    CHECK(a2->size() == 1u);
    CHECK(a2->at(0)->name == "cue");
}

TEST_CASE("EventAnchorTable: clear empties the table", "[event_anchor_table]")
{
    EventAnchorTable t;
    t.get_or_create(1u, 0u).add("x", 0u, 0u);
    t.get_or_create(2u, 5u).add("y", 480u, ANCHOR_SNAP);
    REQUIRE(t.size() == 2u);
    t.clear();
    CHECK(t.size() == 0u);
    CHECK(t.get(1u, 0u) == nullptr);
}
