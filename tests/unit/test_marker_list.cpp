#include <omega/marker_list.h>

#include <catch2/catch_test_macros.hpp>

using namespace omega;

TEST_CASE("MarkerList: empty list has size zero", "[marker_list]")
{
    MarkerList ml;
    CHECK(ml.size() == 0u);
    CHECK(ml.at(0) == nullptr);
    CHECK(ml.find_nearest(0) == nullptr);
}

TEST_CASE("MarkerList: add single marker", "[marker_list]")
{
    MarkerList ml;
    ml.add("verse", 480u);
    REQUIRE(ml.size() == 1u);
    const Marker* m = ml.at(0);
    REQUIRE(m != nullptr);
    CHECK(m->name == "verse");
    CHECK(m->tick == 480u);
}

TEST_CASE("MarkerList: markers sorted by tick on insert", "[marker_list]")
{
    MarkerList ml;
    ml.add("chorus", 1920u);
    ml.add("intro", 0u);
    ml.add("verse", 960u);
    REQUIRE(ml.size() == 3u);
    CHECK(ml.at(0)->tick == 0u);
    CHECK(ml.at(0)->name == "intro");
    CHECK(ml.at(1)->tick == 960u);
    CHECK(ml.at(2)->tick == 1920u);
}

TEST_CASE("MarkerList: duplicate ticks allowed with different names", "[marker_list]")
{
    MarkerList ml;
    ml.add("A", 480u);
    ml.add("B", 480u);
    REQUIRE(ml.size() == 2u);
    CHECK(ml.at(0)->tick == 480u);
    CHECK(ml.at(1)->tick == 480u);
}

TEST_CASE("MarkerList: remove by index", "[marker_list]")
{
    MarkerList ml;
    ml.add("A", 0u);
    ml.add("B", 480u);
    ml.add("C", 960u);
    REQUIRE(ml.remove(1) == OMEGA_OK);
    REQUIRE(ml.size() == 2u);
    CHECK(ml.at(0)->name == "A");
    CHECK(ml.at(1)->name == "C");
}

TEST_CASE("MarkerList: remove out-of-range returns NOT_FOUND", "[marker_list]")
{
    MarkerList ml;
    CHECK(ml.remove(0) == OMEGA_ERR_NOT_FOUND);
    ml.add("A", 0u);
    CHECK(ml.remove(1) == OMEGA_ERR_NOT_FOUND);
}

TEST_CASE("MarkerList: clear empties the list", "[marker_list]")
{
    MarkerList ml;
    ml.add("A", 0u);
    ml.add("B", 480u);
    ml.clear();
    CHECK(ml.size() == 0u);
    CHECK(ml.at(0) == nullptr);
}

TEST_CASE("MarkerList: find_nearest returns marker at or before tick", "[marker_list]")
{
    MarkerList ml;
    ml.add("A", 0u);
    ml.add("B", 480u);
    ml.add("C", 960u);

    const Marker* m = ml.find_nearest(0u);
    REQUIRE(m != nullptr);
    CHECK(m->name == "A");

    m = ml.find_nearest(479u);
    REQUIRE(m != nullptr);
    CHECK(m->name == "A");

    m = ml.find_nearest(480u);
    REQUIRE(m != nullptr);
    CHECK(m->name == "B");

    m = ml.find_nearest(1000u);
    REQUIRE(m != nullptr);
    CHECK(m->name == "C");
}

TEST_CASE("MarkerList: find_nearest on empty list returns null", "[marker_list]")
{
    MarkerList ml;
    CHECK(ml.find_nearest(0u) == nullptr);
    CHECK(ml.find_nearest(9999u) == nullptr);
}

TEST_CASE("MarkerList: find_nearest before first marker returns null", "[marker_list]")
{
    MarkerList ml;
    ml.add("A", 480u);
    CHECK(ml.find_nearest(0u) == nullptr);
    CHECK(ml.find_nearest(479u) == nullptr);
    REQUIRE(ml.find_nearest(480u) != nullptr);
    CHECK(ml.find_nearest(480u)->name == "A");
}

TEST_CASE("MarkerList: size tracking after operations", "[marker_list]")
{
    MarkerList ml;
    CHECK(ml.size() == 0u);
    ml.add("A", 0u);
    CHECK(ml.size() == 1u);
    ml.add("B", 480u);
    CHECK(ml.size() == 2u);
    ml.remove(0);
    CHECK(ml.size() == 1u);
    ml.clear();
    CHECK(ml.size() == 0u);
}
