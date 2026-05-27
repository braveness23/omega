#include <omega/omega.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

struct EventRecord
{
    uint32_t index;
    omega_event_t event;
};

static void collect_event(uint32_t index, const omega_event_t* ev, void* userdata)
{
    auto* out = static_cast<std::vector<EventRecord>*>(userdata);
    out->push_back({index, *ev});
}

static void count_event(uint32_t /*index*/, const omega_event_t* /*ev*/, void* userdata)
{
    auto* count = static_cast<uint32_t*>(userdata);
    ++(*count);
}

// ── Validation ───────────────────────────────────────────────────────────────

TEST_CASE("omega_pattern_for_each_event: null engine returns OMEGA_ERR_INVALID")
{
    REQUIRE(omega_pattern_for_each_event(nullptr, 1, 0xFF, 0xFF, count_event, nullptr) ==
            OMEGA_ERR_INVALID);
}

TEST_CASE("omega_pattern_for_each_event: null callback returns OMEGA_ERR_INVALID")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(omega_pattern_for_each_event(e, 1, 0xFF, 0xFF, nullptr, nullptr) == OMEGA_ERR_INVALID);
    omega_engine_destroy(e);
}

TEST_CASE("omega_pattern_for_each_event: invalid pattern returns OMEGA_ERR_NOT_FOUND")
{
    omega_engine_t* e = omega_engine_create();
    uint32_t count = 0;
    REQUIRE(omega_pattern_for_each_event(e, 999, 0xFF, 0xFF, count_event, &count) ==
            OMEGA_ERR_NOT_FOUND);
    REQUIRE(count == 0u);
    omega_engine_destroy(e);
}

// ── Iteration ────────────────────────────────────────────────────────────────

TEST_CASE("omega_pattern_for_each_event: iterates all events with 0xFF filters")
{
    omega_engine_t* e = omega_engine_create();
    omega_pattern_id_t pat = omega_pattern_create(e, "p", 960u);

    omega_event_t ev0 = omega_make_note_on(0u, 1u, 0, 60, 100, 480u);
    omega_event_t ev1 = omega_make_note_on(240u, 1u, 1, 64, 80, 480u);
    omega_event_t ev2 = omega_make_cc(480u, 1u, 0, 7, 100);
    omega_pattern_add_event(e, pat, &ev0);
    omega_pattern_add_event(e, pat, &ev1);
    omega_pattern_add_event(e, pat, &ev2);

    std::vector<EventRecord> records;
    REQUIRE(omega_pattern_for_each_event(e, pat, 0xFF, 0xFF, collect_event, &records) == OMEGA_OK);
    REQUIRE(records.size() == 3u);
    REQUIRE(records[0].index == 0u);
    REQUIRE(records[1].index == 1u);
    REQUIRE(records[2].index == 2u);

    omega_engine_destroy(e);
}

TEST_CASE("omega_pattern_for_each_event: channel filter selects matching events")
{
    omega_engine_t* e = omega_engine_create();
    omega_pattern_id_t pat = omega_pattern_create(e, "p", 960u);

    omega_event_t ev0 = omega_make_note_on(0u, 1u, 0, 60, 100, 480u);
    omega_event_t ev1 = omega_make_note_on(240u, 1u, 1, 64, 80, 480u);
    omega_event_t ev2 = omega_make_note_on(480u, 1u, 0, 67, 90, 480u);
    omega_pattern_add_event(e, pat, &ev0);
    omega_pattern_add_event(e, pat, &ev1);
    omega_pattern_add_event(e, pat, &ev2);

    std::vector<EventRecord> records;
    REQUIRE(omega_pattern_for_each_event(e, pat, 0u, 0xFF, collect_event, &records) == OMEGA_OK);
    REQUIRE(records.size() == 2u);
    REQUIRE(records[0].event.data[0] == 60u);
    REQUIRE(records[1].event.data[0] == 67u);

    omega_engine_destroy(e);
}

TEST_CASE("omega_pattern_for_each_event: tag filter selects matching events")
{
    omega_engine_t* e = omega_engine_create();
    omega_pattern_id_t pat = omega_pattern_create(e, "p", 960u);

    omega_event_t ev0 = omega_make_note_on(0u, 1u, 0, 60, 100, 480u);
    omega_event_t ev1 = omega_make_cc(240u, 1u, 0, 7, 100);
    omega_pattern_add_event(e, pat, &ev0);
    omega_pattern_add_event(e, pat, &ev1);

    uint32_t count = 0;
    REQUIRE(omega_pattern_for_each_event(e, pat, 0xFF, OMEGA_NOTE_ON, count_event, &count) ==
            OMEGA_OK);
    REQUIRE(count == 1u);

    omega_engine_destroy(e);
}

TEST_CASE("omega_pattern_for_each_event: combined channel and tag filter")
{
    omega_engine_t* e = omega_engine_create();
    omega_pattern_id_t pat = omega_pattern_create(e, "p", 960u);

    omega_event_t ev0 = omega_make_note_on(0u, 1u, 0, 60, 100, 480u);
    omega_event_t ev1 = omega_make_note_on(240u, 1u, 1, 64, 80, 480u);
    omega_event_t ev2 = omega_make_cc(480u, 1u, 0, 7, 100);
    omega_pattern_add_event(e, pat, &ev0);
    omega_pattern_add_event(e, pat, &ev1);
    omega_pattern_add_event(e, pat, &ev2);

    std::vector<EventRecord> records;
    REQUIRE(omega_pattern_for_each_event(e, pat, 0u, OMEGA_NOTE_ON, collect_event, &records) ==
            OMEGA_OK);
    REQUIRE(records.size() == 1u);
    REQUIRE(records[0].event.data[0] == 60u);

    omega_engine_destroy(e);
}

TEST_CASE("omega_pattern_for_each_event: empty pattern invokes zero callbacks")
{
    omega_engine_t* e = omega_engine_create();
    omega_pattern_id_t pat = omega_pattern_create(e, "p", 480u);

    uint32_t count = 0;
    REQUIRE(omega_pattern_for_each_event(e, pat, 0xFF, 0xFF, count_event, &count) == OMEGA_OK);
    REQUIRE(count == 0u);

    omega_engine_destroy(e);
}

TEST_CASE("omega_pattern_for_each_event: index is original unfiltered index")
{
    omega_engine_t* e = omega_engine_create();
    omega_pattern_id_t pat = omega_pattern_create(e, "p", 960u);

    omega_event_t ev0 = omega_make_note_on(0u, 1u, 0, 60, 100, 480u);
    omega_event_t ev1 = omega_make_cc(240u, 1u, 0, 7, 100);
    omega_event_t ev2 = omega_make_note_on(480u, 1u, 0, 64, 90, 480u);
    omega_pattern_add_event(e, pat, &ev0);
    omega_pattern_add_event(e, pat, &ev1);
    omega_pattern_add_event(e, pat, &ev2);

    std::vector<EventRecord> records;
    REQUIRE(omega_pattern_for_each_event(e, pat, 0xFF, OMEGA_NOTE_ON, collect_event, &records) ==
            OMEGA_OK);
    REQUIRE(records.size() == 2u);
    REQUIRE(records[0].index == 0u);
    REQUIRE(records[1].index == 2u);

    omega_engine_destroy(e);
}
