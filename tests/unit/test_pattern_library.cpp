#include <omega/pattern_library.h>
#include <omega/types.h>

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace omega;

// ── create / get ─────────────────────────────────────────────────────────────

TEST_CASE("Create a pattern and retrieve it by ID")
{
    PatternLibrary lib;
    PatternId id = lib.create("intro", 960u);

    REQUIRE(id >= 1u);
    const Pattern* pat = lib.get(id);
    REQUIRE(pat != nullptr);
    REQUIRE(pat->id == id);
    REQUIRE(pat->name == "intro");
    REQUIRE(pat->length_ticks == 960u);
    REQUIRE(pat->events.empty());
}

TEST_CASE("Create 1000 patterns and get each by ID")
{
    PatternLibrary lib;
    constexpr int kCount = 1000;
    std::vector<PatternId> ids;
    ids.reserve(kCount);

    for (int i = 0; i < kCount; ++i)
    {
        auto length = static_cast<uint64_t>(i) * static_cast<uint64_t>(OMEGA_PPQN);
        ids.push_back(lib.create("pattern_" + std::to_string(i), length));
    }

    for (int i = 0; i < kCount; ++i)
    {
        auto idx = static_cast<size_t>(i);
        const Pattern* pat = lib.get(ids[idx]);
        REQUIRE(pat != nullptr);
        REQUIRE(pat->name == "pattern_" + std::to_string(i));
        REQUIRE(pat->length_ticks == static_cast<uint64_t>(i) * static_cast<uint64_t>(OMEGA_PPQN));
    }
}

TEST_CASE("IDs are assigned monotonically")
{
    PatternLibrary lib;
    PatternId a = lib.create("a", 480u);
    PatternId b = lib.create("b", 480u);
    PatternId c = lib.create("c", 480u);

    REQUIRE(b == a + 1u);
    REQUIRE(c == b + 1u);
}

TEST_CASE("get returns null for unknown ID")
{
    PatternLibrary lib;
    REQUIRE(lib.get(0u) == nullptr);
    REQUIRE(lib.get(999u) == nullptr);
}

// ── destroy ───────────────────────────────────────────────────────────────────

TEST_CASE("destroy: get returns null after destruction")
{
    PatternLibrary lib;
    PatternId id = lib.create("tmp", 480u);
    REQUIRE(lib.get(id) != nullptr);

    lib.destroy(id);
    REQUIRE(lib.get(id) == nullptr);
}

TEST_CASE("destroy: destroyed ID is not reused by subsequent creates")
{
    PatternLibrary lib;
    PatternId id1 = lib.create("a", 480u);
    PatternId id2 = lib.create("b", 480u);

    lib.destroy(id1);
    REQUIRE(lib.get(id1) == nullptr);

    PatternId id3 = lib.create("c", 480u);
    // id1 must not be reused — next_id only moves forward
    REQUIRE(id3 != id1);
    REQUIRE(lib.get(id1) == nullptr);
    REQUIRE(lib.get(id2) != nullptr);
    REQUIRE(lib.get(id3) != nullptr);
}

TEST_CASE("destroy with unknown ID is a no-op")
{
    PatternLibrary lib;
    PatternId id = lib.create("p", 480u);
    lib.destroy(999u);  // unknown — must not crash or affect other patterns
    REQUIRE(lib.get(id) != nullptr);
}

// ── add_event ─────────────────────────────────────────────────────────────────

TEST_CASE("PatternLibrary: add_event inserts events in tick-sorted order")
{
    PatternLibrary lib;
    PatternId id = lib.create("seq", 960u);

    // Insert in reverse tick order
    REQUIRE(lib.add_event(id, omega_make_note_on(960u, 1u, 0, 64, 100, 0)) == OMEGA_OK);
    REQUIRE(lib.add_event(id, omega_make_note_on(480u, 1u, 0, 62, 100, 0)) == OMEGA_OK);
    REQUIRE(lib.add_event(id, omega_make_note_on(0u, 1u, 0, 60, 100, 0)) == OMEGA_OK);

    const Pattern* pat = lib.get(id);
    REQUIRE(pat->events.size() == 3u);
    REQUIRE(pat->events[0].tick == 0u);
    REQUIRE(pat->events[1].tick == 480u);
    REQUIRE(pat->events[2].tick == 960u);
}

TEST_CASE("add_event preserves sorted order after each insertion")
{
    PatternLibrary lib;
    PatternId id = lib.create("p", 4800u);

    // Insert 10 notes at random-ish ticks
    const std::array<uint64_t, 10> ticks = {
        700u, 100u, 400u, 900u, 200u, 600u, 300u, 800u, 500u, 0u};
    for (uint64_t tick : ticks)
    {
        lib.add_event(id, omega_make_note_on(tick, 1u, 0, 60, 100, 0));
        const Pattern* pat = lib.get(id);
        for (size_t i = 0; i + 1 < pat->events.size(); ++i)
        {
            REQUIRE(pat->events[i].tick <= pat->events[i + 1].tick);
        }
    }

    const Pattern* pat = lib.get(id);
    REQUIRE(pat->events.size() == 10u);
    REQUIRE(pat->events.front().tick == 0u);
    REQUIRE(pat->events.back().tick == 900u);
}

TEST_CASE("add_event handles events at the same tick")
{
    PatternLibrary lib;
    PatternId id = lib.create("p", 480u);

    REQUIRE(lib.add_event(id, omega_make_note_on(240u, 1u, 0, 60, 100, 0)) == OMEGA_OK);
    REQUIRE(lib.add_event(id, omega_make_note_on(240u, 1u, 0, 64, 100, 0)) == OMEGA_OK);
    REQUIRE(lib.add_event(id, omega_make_note_on(240u, 1u, 0, 67, 100, 0)) == OMEGA_OK);

    const Pattern* pat = lib.get(id);
    REQUIRE(pat->events.size() == 3u);
    REQUIRE(pat->events[0].tick == 240u);
    REQUIRE(pat->events[1].tick == 240u);
    REQUIRE(pat->events[2].tick == 240u);
}

TEST_CASE("add_event returns ERR_NOT_FOUND for unknown pattern")
{
    PatternLibrary lib;
    omega_event_t ev = omega_make_note_on(0u, 1u, 0, 60, 100, 0);
    REQUIRE(lib.add_event(999u, ev) == OMEGA_ERR_NOT_FOUND);
}

// ── set_length ────────────────────────────────────────────────────────────────

TEST_CASE("set_length updates pattern length")
{
    PatternLibrary lib;
    PatternId id = lib.create("p", 480u);

    REQUIRE(lib.set_length(id, 960u) == OMEGA_OK);
    REQUIRE(lib.get(id)->length_ticks == 960u);
}

TEST_CASE("set_length returns ERR_NOT_FOUND for unknown pattern")
{
    PatternLibrary lib;
    REQUIRE(lib.set_length(999u, 480u) == OMEGA_ERR_NOT_FOUND);
}

// ── count / for_each ─────────────────────────────────────────────────────────

TEST_CASE("count returns 0 for empty library")
{
    PatternLibrary lib;
    REQUIRE(lib.count() == 0u);
}

TEST_CASE("count reflects live patterns after create and destroy")
{
    PatternLibrary lib;
    REQUIRE(lib.count() == 0u);

    PatternId a = lib.create("a", 480u);
    REQUIRE(lib.count() == 1u);

    PatternId b = lib.create("b", 480u);
    REQUIRE(lib.count() == 2u);

    lib.destroy(a);
    REQUIRE(lib.count() == 1u);

    lib.destroy(b);
    REQUIRE(lib.count() == 0u);
}

TEST_CASE("for_each visits all live patterns")
{
    PatternLibrary lib;
    PatternId id1 = lib.create("x", 480u);
    PatternId id2 = lib.create("y", 960u);
    PatternId id3 = lib.create("z", 1920u);

    std::vector<PatternId> visited;
    lib.for_each([&](PatternId id, const Pattern& /*pat*/) { visited.push_back(id); });

    REQUIRE(visited.size() == 3u);
    // Order is unspecified; check presence.
    REQUIRE(std::find(visited.begin(), visited.end(), id1) != visited.end());
    REQUIRE(std::find(visited.begin(), visited.end(), id2) != visited.end());
    REQUIRE(std::find(visited.begin(), visited.end(), id3) != visited.end());
}

TEST_CASE("for_each skips destroyed patterns")
{
    PatternLibrary lib;
    PatternId id1 = lib.create("a", 480u);
    PatternId id2 = lib.create("b", 480u);
    lib.destroy(id1);

    std::vector<PatternId> visited;
    lib.for_each([&](PatternId id, const Pattern& /*pat*/) { visited.push_back(id); });

    REQUIRE(visited.size() == 1u);
    REQUIRE(visited[0] == id2);
}

TEST_CASE("for_each on empty library invokes callback zero times")
{
    PatternLibrary lib;
    int calls = 0;
    lib.for_each([&](PatternId /*id*/, const Pattern& /*pat*/) { ++calls; });
    REQUIRE(calls == 0);
}

TEST_CASE("for_each exposes correct Pattern fields")
{
    PatternLibrary lib;
    PatternId id = lib.create("test", 1234u);

    bool found = false;
    lib.for_each([&](PatternId pid, const Pattern& pat) {
        if (pid == id)
        {
            found = true;
            REQUIRE(pat.name == "test");
            REQUIRE(pat.length_ticks == 1234u);
        }
    });
    REQUIRE(found);
}
