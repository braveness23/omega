#include <omega/anchor_point.h>

#include <catch2/catch_test_macros.hpp>

using namespace omega;

TEST_CASE("AnchorList: empty list edge cases", "[anchor_point]")
{
    AnchorList al;
    CHECK(al.size() == 0u);
    CHECK(al.at(0) == nullptr);
    CHECK(al.find_by_name("x") == nullptr);
    CHECK(al.active_snap() == nullptr);
    CHECK(al.snap_anchors().empty());
    CHECK(al.remove("x") == OMEGA_ERR_NOT_FOUND);
    CHECK(al.set_active_snap(0) == OMEGA_ERR_NOT_FOUND);
}

TEST_CASE("AnchorList: add and retrieve sorted by offset", "[anchor_point]")
{
    AnchorList al;
    al.add("end", 960u, ANCHOR_CUE);
    al.add("start", 0u, ANCHOR_SNAP);
    al.add("mid", 480u, ANCHOR_WARP);

    REQUIRE(al.size() == 3u);
    CHECK(al.at(0)->offset_ticks == 0u);
    CHECK(al.at(0)->name == "start");
    CHECK(al.at(1)->offset_ticks == 480u);
    CHECK(al.at(1)->name == "mid");
    CHECK(al.at(2)->offset_ticks == 960u);
    CHECK(al.at(2)->name == "end");
}

TEST_CASE("AnchorList: remove by name", "[anchor_point]")
{
    AnchorList al;
    al.add("A", 0u, ANCHOR_SNAP);
    al.add("B", 480u, ANCHOR_WARP);
    al.add("C", 960u, ANCHOR_CUE);

    REQUIRE(al.remove("B") == OMEGA_OK);
    REQUIRE(al.size() == 2u);
    CHECK(al.at(0)->name == "A");
    CHECK(al.at(1)->name == "C");

    CHECK(al.remove("X") == OMEGA_ERR_NOT_FOUND);
}

TEST_CASE("AnchorList: active snap set and get", "[anchor_point]")
{
    AnchorList al;
    al.add("snap1", 0u, ANCHOR_SNAP);
    al.add("snap2", 480u, ANCHOR_SNAP | ANCHOR_CUE);

    REQUIRE(al.set_active_snap(0) == OMEGA_OK);
    const AnchorPoint* s = al.active_snap();
    REQUIRE(s != nullptr);
    CHECK(s->name == "snap1");

    REQUIRE(al.set_active_snap(1) == OMEGA_OK);
    s = al.active_snap();
    REQUIRE(s != nullptr);
    CHECK(s->name == "snap2");
}

TEST_CASE("AnchorList: set_active_snap requires ANCHOR_SNAP flag", "[anchor_point]")
{
    AnchorList al;
    al.add("warp", 0u, ANCHOR_WARP);
    al.add("snap", 480u, ANCHOR_SNAP);

    CHECK(al.set_active_snap(0) == OMEGA_ERR_INVALID);
    CHECK(al.active_snap() == nullptr);

    CHECK(al.set_active_snap(1) == OMEGA_OK);
    REQUIRE(al.active_snap() != nullptr);
    CHECK(al.active_snap()->name == "snap");
}

TEST_CASE("AnchorList: snap_anchors returns filtered view", "[anchor_point]")
{
    AnchorList al;
    al.add("A", 0u, ANCHOR_SNAP);
    al.add("B", 100u, ANCHOR_WARP);
    al.add("C", 200u, ANCHOR_SNAP | ANCHOR_CUE);
    al.add("D", 300u, ANCHOR_CUE);

    auto snaps = al.snap_anchors();
    REQUIRE(snaps.size() == 2u);
    CHECK(snaps[0]->name == "A");
    CHECK(snaps[1]->name == "C");
}

TEST_CASE("AnchorList: combinable flags", "[anchor_point]")
{
    AnchorList al;
    al.add("multi", 0u, ANCHOR_SNAP | ANCHOR_CUE);
    REQUIRE(al.size() == 1u);
    const AnchorPoint* a = al.at(0);
    REQUIRE(a != nullptr);
    CHECK((a->flags & ANCHOR_SNAP) != 0u);
    CHECK((a->flags & ANCHOR_CUE) != 0u);
    CHECK((a->flags & ANCHOR_WARP) == 0u);

    auto snaps = al.snap_anchors();
    REQUIRE(snaps.size() == 1u);
    CHECK(snaps[0]->name == "multi");
}

TEST_CASE("AnchorList: find_by_name", "[anchor_point]")
{
    AnchorList al;
    al.add("alpha", 0u, ANCHOR_SNAP);
    al.add("beta", 480u, ANCHOR_WARP);

    const AnchorPoint* a = al.find_by_name("alpha");
    REQUIRE(a != nullptr);
    CHECK(a->offset_ticks == 0u);

    CHECK(al.find_by_name("gamma") == nullptr);
    CHECK(al.find_by_name("") == nullptr);
}

TEST_CASE("AnchorList: active_snap tracks removal correctly", "[anchor_point]")
{
    AnchorList al;
    al.add("A", 0u, ANCHOR_SNAP);
    al.add("B", 480u, ANCHOR_SNAP);
    al.add("C", 960u, ANCHOR_SNAP);

    // set active to "B" (index 1)
    REQUIRE(al.set_active_snap(1) == OMEGA_OK);
    CHECK(al.active_snap()->name == "B");

    // remove "A" -- index shifts: B is now index 0, C is index 1
    REQUIRE(al.remove("A") == OMEGA_OK);
    const AnchorPoint* s = al.active_snap();
    REQUIRE(s != nullptr);
    CHECK(s->name == "B");

    // remove active snap "B" -- active_snap() should return nullptr
    REQUIRE(al.remove("B") == OMEGA_OK);
    CHECK(al.active_snap() == nullptr);
}

TEST_CASE("AnchorList: clear resets active snap", "[anchor_point]")
{
    AnchorList al;
    al.add("A", 0u, ANCHOR_SNAP);
    REQUIRE(al.set_active_snap(0) == OMEGA_OK);
    al.clear();
    CHECK(al.size() == 0u);
    CHECK(al.active_snap() == nullptr);
}
