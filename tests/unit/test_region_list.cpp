#include <omega/region_list.h>

#include <catch2/catch_test_macros.hpp>

using namespace omega;

TEST_CASE("RegionList: empty list has size zero", "[region_list]")
{
    RegionList rl;
    CHECK(rl.size() == 0u);
    CHECK(rl.at(0) == nullptr);
    CHECK(rl.find_containing(0) == nullptr);
}

TEST_CASE("RegionList: add single region", "[region_list]")
{
    RegionList rl;
    REQUIRE(rl.add("loop", 0u, 1920u, RegionType::LOOP) == OMEGA_OK);
    REQUIRE(rl.size() == 1u);
    const Region* r = rl.at(0);
    REQUIRE(r != nullptr);
    CHECK(r->name == "loop");
    CHECK(r->start_tick == 0u);
    CHECK(r->end_tick == 1920u);
    CHECK(r->type == RegionType::LOOP);
}

TEST_CASE("RegionList: start >= end returns INVALID", "[region_list]")
{
    RegionList rl;
    CHECK(rl.add("bad", 480u, 480u, RegionType::LOOP) == OMEGA_ERR_INVALID);
    CHECK(rl.add("bad", 960u, 480u, RegionType::LOOP) == OMEGA_ERR_INVALID);
    CHECK(rl.size() == 0u);
}

TEST_CASE("RegionList: regions sorted by start_tick", "[region_list]")
{
    RegionList rl;
    REQUIRE(rl.add("C", 1920u, 3840u, RegionType::SECTION) == OMEGA_OK);
    REQUIRE(rl.add("A", 0u, 480u, RegionType::LOOP) == OMEGA_OK);
    REQUIRE(rl.add("B", 960u, 1920u, RegionType::PUNCH) == OMEGA_OK);
    REQUIRE(rl.size() == 3u);
    CHECK(rl.at(0)->start_tick == 0u);
    CHECK(rl.at(1)->start_tick == 960u);
    CHECK(rl.at(2)->start_tick == 1920u);
}

TEST_CASE("RegionList: remove by index", "[region_list]")
{
    RegionList rl;
    REQUIRE(rl.add("A", 0u, 480u, RegionType::LOOP) == OMEGA_OK);
    REQUIRE(rl.add("B", 480u, 960u, RegionType::PUNCH) == OMEGA_OK);
    REQUIRE(rl.add("C", 960u, 1920u, RegionType::SECTION) == OMEGA_OK);
    REQUIRE(rl.remove(1) == OMEGA_OK);
    REQUIRE(rl.size() == 2u);
    CHECK(rl.at(0)->name == "A");
    CHECK(rl.at(1)->name == "C");
}

TEST_CASE("RegionList: remove out-of-range returns NOT_FOUND", "[region_list]")
{
    RegionList rl;
    CHECK(rl.remove(0) == OMEGA_ERR_NOT_FOUND);
    REQUIRE(rl.add("A", 0u, 480u, RegionType::LOOP) == OMEGA_OK);
    CHECK(rl.remove(1) == OMEGA_ERR_NOT_FOUND);
}

TEST_CASE("RegionList: clear empties the list", "[region_list]")
{
    RegionList rl;
    REQUIRE(rl.add("A", 0u, 480u, RegionType::LOOP) == OMEGA_OK);
    rl.clear();
    CHECK(rl.size() == 0u);
}

TEST_CASE("RegionList: find_containing returns region covering tick", "[region_list]")
{
    RegionList rl;
    REQUIRE(rl.add("verse", 0u, 960u, RegionType::SECTION) == OMEGA_OK);
    REQUIRE(rl.add("chorus", 960u, 1920u, RegionType::SECTION) == OMEGA_OK);

    const Region* r = rl.find_containing(0u);
    REQUIRE(r != nullptr);
    CHECK(r->name == "verse");

    r = rl.find_containing(959u);
    REQUIRE(r != nullptr);
    CHECK(r->name == "verse");

    r = rl.find_containing(960u);
    REQUIRE(r != nullptr);
    CHECK(r->name == "chorus");

    r = rl.find_containing(1919u);
    REQUIRE(r != nullptr);
    CHECK(r->name == "chorus");

    CHECK(rl.find_containing(1920u) == nullptr);
}

TEST_CASE("RegionList: find_containing returns null when no match", "[region_list]")
{
    RegionList rl;
    REQUIRE(rl.add("A", 480u, 960u, RegionType::LOOP) == OMEGA_OK);
    CHECK(rl.find_containing(0u) == nullptr);
    CHECK(rl.find_containing(479u) == nullptr);
    CHECK(rl.find_containing(960u) == nullptr);
}

TEST_CASE("RegionList: overlapping regions both accessible", "[region_list]")
{
    RegionList rl;
    REQUIRE(rl.add("outer", 0u, 1920u, RegionType::LOOP) == OMEGA_OK);
    REQUIRE(rl.add("inner", 480u, 960u, RegionType::PUNCH) == OMEGA_OK);

    // find_containing returns the first (lowest start) that contains the tick
    const Region* r = rl.find_containing(600u);
    REQUIRE(r != nullptr);
    CHECK(r->name == "outer");

    // Both regions are accessible by index
    CHECK(rl.at(0)->name == "outer");
    CHECK(rl.at(1)->name == "inner");
}
