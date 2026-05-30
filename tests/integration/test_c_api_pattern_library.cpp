#include <omega/omega.h>
#include <omega/test/capturing_sink.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

// Cast CapturingSink (C++) to the opaque omega_sink_t* accepted by the C API.
static omega_sink_t* as_sink(omega::CapturingSink& s)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<omega_sink_t*>(&s);
}

// ── omega_pattern_library_count ───────────────────────────────────────────────

TEST_CASE("omega_pattern_library_count: returns 0 for new engine")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(omega_pattern_library_count(e) == 0u);
    omega_engine_destroy(e);
}

TEST_CASE("omega_pattern_library_count: NULL engine returns 0")
{
    REQUIRE(omega_pattern_library_count(nullptr) == 0u);
}

TEST_CASE("omega_pattern_library_count: increments on create, decrements on destroy")
{
    omega_engine_t* e = omega_engine_create();

    omega_pattern_id_t a = omega_pattern_create(e, "a", 480u);
    REQUIRE(omega_pattern_library_count(e) == 1u);

    omega_pattern_id_t b = omega_pattern_create(e, "b", 960u);
    REQUIRE(omega_pattern_library_count(e) == 2u);

    omega_pattern_destroy(e, a);
    REQUIRE(omega_pattern_library_count(e) == 1u);

    omega_pattern_destroy(e, b);
    REQUIRE(omega_pattern_library_count(e) == 0u);

    omega_engine_destroy(e);
}

// ── omega_pattern_for_each ────────────────────────────────────────────────────

TEST_CASE("omega_pattern_for_each: NULL engine returns INVALID")
{
    auto cb = [](omega_pattern_id_t, void*) {};
    REQUIRE(omega_pattern_for_each(nullptr, cb, nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("omega_pattern_for_each: NULL callback returns INVALID")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(omega_pattern_for_each(e, nullptr, nullptr) == OMEGA_ERR_INVALID);
    omega_engine_destroy(e);
}

TEST_CASE("omega_pattern_for_each: empty library iterates zero times")
{
    omega_engine_t* e = omega_engine_create();
    int calls = 0;
    REQUIRE(omega_pattern_for_each(
                e, [](omega_pattern_id_t, void* ud) { ++*static_cast<int*>(ud); }, &calls) ==
            OMEGA_OK);
    REQUIRE(calls == 0);
    omega_engine_destroy(e);
}

TEST_CASE("omega_pattern_for_each: visits all live pattern IDs")
{
    omega_engine_t* e = omega_engine_create();

    omega_pattern_id_t id1 = omega_pattern_create(e, "x", 480u);
    omega_pattern_id_t id2 = omega_pattern_create(e, "y", 960u);
    omega_pattern_id_t id3 = omega_pattern_create(e, "z", 1920u);

    std::vector<omega_pattern_id_t> visited;
    auto cb = [](omega_pattern_id_t id, void* ud) {
        static_cast<std::vector<omega_pattern_id_t>*>(ud)->push_back(id);
    };

    REQUIRE(omega_pattern_for_each(e, cb, &visited) == OMEGA_OK);
    REQUIRE(visited.size() == 3u);
    REQUIRE(std::find(visited.begin(), visited.end(), id1) != visited.end());
    REQUIRE(std::find(visited.begin(), visited.end(), id2) != visited.end());
    REQUIRE(std::find(visited.begin(), visited.end(), id3) != visited.end());

    omega_engine_destroy(e);
}

TEST_CASE("omega_pattern_for_each: skips destroyed patterns")
{
    omega_engine_t* e = omega_engine_create();

    omega_pattern_id_t id1 = omega_pattern_create(e, "a", 480u);
    omega_pattern_id_t id2 = omega_pattern_create(e, "b", 480u);
    omega_pattern_destroy(e, id1);

    std::vector<omega_pattern_id_t> visited;
    auto cb = [](omega_pattern_id_t id, void* ud) {
        static_cast<std::vector<omega_pattern_id_t>*>(ud)->push_back(id);
    };

    omega_pattern_for_each(e, cb, &visited);
    REQUIRE(visited.size() == 1u);
    REQUIRE(visited[0] == id2);

    omega_engine_destroy(e);
}

// ── omega_convert_tracks_to_patterns ─────────────────────────────────────────

TEST_CASE("omega_convert_tracks_to_patterns: NULL engine returns INVALID")
{
    uint32_t count = 0;
    REQUIRE(omega_convert_tracks_to_patterns(nullptr, 1u, 960u, &count) == OMEGA_ERR_INVALID);
}

TEST_CASE("omega_convert_tracks_to_patterns: NULL count_out returns INVALID")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(omega_convert_tracks_to_patterns(e, 1u, 960u, nullptr) == OMEGA_ERR_INVALID);
    omega_engine_destroy(e);
}

TEST_CASE("omega_convert_tracks_to_patterns: empty timeline produces 0 patterns")
{
    omega_engine_t* e = omega_engine_create();
    uint32_t count = 99u;
    REQUIRE(omega_convert_tracks_to_patterns(e, 1u, 960u, &count) == OMEGA_OK);
    REQUIRE(count == 0u);
    REQUIRE(omega_pattern_library_count(e) == 0u);
    omega_engine_destroy(e);
}

TEST_CASE("omega_convert_tracks_to_patterns: one track → one pattern in library")
{
    omega::CapturingSink sink;
    omega_engine_t* e = omega_engine_create();
    omega_engine_add_sink(e, as_sink(sink));

    omega_track_id_t t{};
    omega_engine_add_track(e, "bass", &t);
    omega_engine_set_track_sink(e, t, sink.sink_id());

    // Add event via queue then flush (engine is stopped; process() still drains queue).
    omega_event_t ev = omega_make_note_on(0u, sink.sink_id(), 0, 48, 100, 0);
    omega_engine_add_event(e, t, ev);
    omega_engine_process(e);

    uint32_t count = 0;
    REQUIRE(omega_convert_tracks_to_patterns(e, sink.sink_id(), 960u, &count) == OMEGA_OK);
    REQUIRE(count == 1u);
    REQUIRE(omega_pattern_library_count(e) == 1u);

    // Verify pattern has expected length.
    omega_pattern_id_t pid = OMEGA_PATTERN_INVALID;
    omega_pattern_for_each(
        e,
        [](omega_pattern_id_t id, void* ud) { *static_cast<omega_pattern_id_t*>(ud) = id; },
        &pid);
    REQUIRE(pid != OMEGA_PATTERN_INVALID);

    omega_tick_t len = 0;
    REQUIRE(omega_pattern_length(e, pid, &len) == OMEGA_OK);
    REQUIRE(len == 960u);

    omega_engine_destroy(e);
}

TEST_CASE("omega_convert_tracks_to_patterns: events re-routed to new sink_id")
{
    omega::CapturingSink sink_a;
    omega::CapturingSink sink_b;

    omega_engine_t* e = omega_engine_create();
    omega_engine_add_sink(e, as_sink(sink_a));
    omega_engine_add_sink(e, as_sink(sink_b));

    omega_track_id_t t{};
    omega_engine_add_track(e, "lead", &t);
    omega_engine_set_track_sink(e, t, sink_a.sink_id());

    omega_event_t ev = omega_make_note_on(0u, sink_a.sink_id(), 0, 60, 80, 0);
    omega_engine_add_event(e, t, ev);
    omega_engine_process(e);  // flush queue

    uint32_t count = 0;
    omega_convert_tracks_to_patterns(e, sink_b.sink_id(), 480u, &count);
    REQUIRE(count == 1u);

    omega_pattern_id_t pid = OMEGA_PATTERN_INVALID;
    omega_pattern_for_each(
        e,
        [](omega_pattern_id_t id, void* ud) { *static_cast<omega_pattern_id_t*>(ud) = id; },
        &pid);
    REQUIRE(pid != OMEGA_PATTERN_INVALID);

    omega_event_t out{};
    REQUIRE(omega_pattern_event_at(e, pid, 0u, &out) == OMEGA_OK);
    REQUIRE(out.sink_id == sink_b.sink_id());

    omega_engine_destroy(e);
}
