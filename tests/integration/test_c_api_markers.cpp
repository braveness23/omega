#include <omega/omega.h>

#include <catch2/catch_test_macros.hpp>

// ── Markers via C API ─────────────────────────────────────────────────────────

TEST_CASE("C API markers: null guards")
{
    omega_marker_t out{};
    REQUIRE(omega_marker_add(nullptr, "m", 0u) == OMEGA_ERR_INVALID);
    REQUIRE(omega_marker_add(nullptr, nullptr, 0u) == OMEGA_ERR_INVALID);
    REQUIRE(omega_marker_remove(nullptr, 0u) == OMEGA_ERR_INVALID);
    REQUIRE(omega_marker_count(nullptr) == 0u);
    REQUIRE(omega_marker_at(nullptr, 0u, &out) == OMEGA_ERR_INVALID);
    REQUIRE(omega_marker_at(nullptr, 0u, nullptr) == OMEGA_ERR_INVALID);
    REQUIRE(omega_marker_clear(nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API markers: add, count, at, remove, clear")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    REQUIRE(omega_marker_count(e) == 0u);

    REQUIRE(omega_marker_add(e, "intro", 0u) == OMEGA_OK);
    REQUIRE(omega_marker_add(e, "verse", 1920u) == OMEGA_OK);
    REQUIRE(omega_marker_add(e, "chorus", 3840u) == OMEGA_OK);

    REQUIRE(omega_marker_count(e) == 3u);

    omega_marker_t m{};
    REQUIRE(omega_marker_at(e, 0u, &m) == OMEGA_OK);
    REQUIRE(m.tick == 0u);

    REQUIRE(omega_marker_at(e, 1u, &m) == OMEGA_OK);
    REQUIRE(m.tick == 1920u);

    REQUIRE(omega_marker_at(e, 2u, &m) == OMEGA_OK);
    REQUIRE(m.tick == 3840u);

    // Out of bounds
    REQUIRE(omega_marker_at(e, 99u, &m) == OMEGA_ERR_NOT_FOUND);

    REQUIRE(omega_marker_remove(e, 1u) == OMEGA_OK);
    REQUIRE(omega_marker_count(e) == 2u);

    REQUIRE(omega_marker_clear(e) == OMEGA_OK);
    REQUIRE(omega_marker_count(e) == 0u);

    omega_engine_destroy(e);
}

TEST_CASE("C API markers: add with null name returns OMEGA_ERR_INVALID")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    REQUIRE(omega_marker_add(e, nullptr, 0u) == OMEGA_ERR_INVALID);
    omega_engine_destroy(e);
}

// ── Regions via C API ─────────────────────────────────────────────────────────

TEST_CASE("C API regions: null guards")
{
    omega_region_t out{};
    REQUIRE(omega_region_add(nullptr, "r", 0u, 100u, OMEGA_REGION_LOOP) == OMEGA_ERR_INVALID);
    REQUIRE(omega_region_remove(nullptr, 0u) == OMEGA_ERR_INVALID);
    REQUIRE(omega_region_count(nullptr) == 0u);
    REQUIRE(omega_region_at(nullptr, 0u, &out) == OMEGA_ERR_INVALID);
    REQUIRE(omega_region_at(nullptr, 0u, nullptr) == OMEGA_ERR_INVALID);
    REQUIRE(omega_region_clear(nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API regions: add, count, at, remove, clear")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    REQUIRE(omega_region_count(e) == 0u);

    REQUIRE(omega_region_add(e, "loop-A", 0u, 960u, OMEGA_REGION_LOOP) == OMEGA_OK);
    REQUIRE(omega_region_add(e, "punch", 960u, 1920u, OMEGA_REGION_PUNCH) == OMEGA_OK);
    REQUIRE(omega_region_add(e, "section-B", 1920u, 3840u, OMEGA_REGION_SECTION) == OMEGA_OK);

    REQUIRE(omega_region_count(e) == 3u);

    omega_region_t r{};
    REQUIRE(omega_region_at(e, 0u, &r) == OMEGA_OK);
    REQUIRE(r.start_tick == 0u);
    REQUIRE(r.end_tick == 960u);
    REQUIRE(r.type == OMEGA_REGION_LOOP);

    REQUIRE(omega_region_at(e, 1u, &r) == OMEGA_OK);
    REQUIRE(r.type == OMEGA_REGION_PUNCH);

    REQUIRE(omega_region_at(e, 2u, &r) == OMEGA_OK);
    REQUIRE(r.type == OMEGA_REGION_SECTION);

    // Out of bounds
    REQUIRE(omega_region_at(e, 99u, &r) == OMEGA_ERR_NOT_FOUND);

    REQUIRE(omega_region_remove(e, 0u) == OMEGA_OK);
    REQUIRE(omega_region_count(e) == 2u);

    REQUIRE(omega_region_clear(e) == OMEGA_OK);
    REQUIRE(omega_region_count(e) == 0u);

    omega_engine_destroy(e);
}

TEST_CASE("C API regions: invalid start >= end returns error")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    REQUIRE(omega_region_add(e, "bad", 1000u, 500u, OMEGA_REGION_LOOP) != OMEGA_OK);
    omega_engine_destroy(e);
}

// ── Pattern anchors via C API ─────────────────────────────────────────────────

TEST_CASE("C API anchors: null guards")
{
    REQUIRE(omega_pattern_add_anchor(nullptr, 0u, "a", 0u, OMEGA_ANCHOR_SNAP) == OMEGA_ERR_INVALID);
    REQUIRE(omega_pattern_remove_anchor(nullptr, 0u, "a") == OMEGA_ERR_INVALID);
    REQUIRE(omega_pattern_anchor_count(nullptr, 0u) == 0u);
    REQUIRE(omega_pattern_set_active_snap(nullptr, 0u, 0u) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API anchors: pattern anchor add, count, set_active_snap, remove")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    // Invalid pattern returns NOT_FOUND
    REQUIRE(omega_pattern_add_anchor(e, 9999u, "x", 0u, OMEGA_ANCHOR_SNAP) == OMEGA_ERR_NOT_FOUND);
    REQUIRE(omega_pattern_remove_anchor(e, 9999u, "x") == OMEGA_ERR_NOT_FOUND);
    REQUIRE(omega_pattern_set_active_snap(e, 9999u, 0u) == OMEGA_ERR_NOT_FOUND);

    omega_pattern_id_t pid = omega_pattern_create(e, "pat", 960u);
    REQUIRE(pid != OMEGA_PATTERN_INVALID);

    REQUIRE(omega_pattern_anchor_count(e, pid) == 0u);

    REQUIRE(omega_pattern_add_anchor(e, pid, "downbeat", 0u, OMEGA_ANCHOR_SNAP) == OMEGA_OK);
    REQUIRE(omega_pattern_add_anchor(e, pid, "mid", 480u, OMEGA_ANCHOR_CUE) == OMEGA_OK);

    REQUIRE(omega_pattern_anchor_count(e, pid) == 2u);

    // Set active snap on the SNAP anchor (index 0, sorted by offset)
    REQUIRE(omega_pattern_set_active_snap(e, pid, 0u) == OMEGA_OK);

    REQUIRE(omega_pattern_remove_anchor(e, pid, "downbeat") == OMEGA_OK);
    REQUIRE(omega_pattern_anchor_count(e, pid) == 1u);

    omega_engine_destroy(e);
}

TEST_CASE("C API anchors: add with null name returns OMEGA_ERR_INVALID")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);
    omega_pattern_id_t pid = omega_pattern_create(e, "p", 960u);
    REQUIRE(omega_pattern_add_anchor(e, pid, nullptr, 0u, OMEGA_ANCHOR_SNAP) == OMEGA_ERR_INVALID);
    REQUIRE(omega_pattern_remove_anchor(e, pid, nullptr) == OMEGA_ERR_INVALID);
    omega_engine_destroy(e);
}

// ── Event anchors via C API ───────────────────────────────────────────────────

TEST_CASE("C API event anchors: null guards")
{
    REQUIRE(omega_event_add_anchor(nullptr, 0u, 0u, "a", 0u, 0u) == OMEGA_ERR_INVALID);
    REQUIRE(omega_event_remove_anchor(nullptr, 0u, 0u, "a") == OMEGA_ERR_INVALID);
}

TEST_CASE("C API event anchors: add and remove")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_track_id_t track{};
    REQUIRE(omega_engine_add_track(e, "trk", &track) == OMEGA_OK);

    // Add an anchor to event index 0 on the track
    REQUIRE(omega_event_add_anchor(e, track, 0u, "down", 0u, OMEGA_ANCHOR_SNAP) == OMEGA_OK);

    // Remove it
    REQUIRE(omega_event_remove_anchor(e, track, 0u, "down") == OMEGA_OK);

    // Remove non-existent anchor returns NOT_FOUND
    REQUIRE(omega_event_remove_anchor(e, track, 0u, "missing") == OMEGA_ERR_NOT_FOUND);

    // Null name guards
    REQUIRE(omega_event_add_anchor(e, track, 0u, nullptr, 0u, 0u) == OMEGA_ERR_INVALID);
    REQUIRE(omega_event_remove_anchor(e, track, 0u, nullptr) == OMEGA_ERR_INVALID);

    omega_engine_destroy(e);
}

// ── Timer via C API ───────────────────────────────────────────────────────────

TEST_CASE("C API timer: null engine returns null")
{
    omega_timer_t* t = omega_timer_create(nullptr, 1000u);
    REQUIRE(t == nullptr);
}

TEST_CASE("C API timer: create and destroy")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_timer_t* t = omega_timer_create(e, 10000u);
    REQUIRE(t != nullptr);
    omega_timer_destroy(t);

    omega_engine_destroy(e);
}

TEST_CASE("C API timer: destroy null is safe")
{
    omega_timer_destroy(nullptr);  // must not crash
}

// ── Snap via C API ────────────────────────────────────────────────────────────

TEST_CASE("C API snap: null guards")
{
    omega_snap_config_t cfg{OMEGA_SNAP_MARKERS, 0u, 0u};
    omega_snap_result_t res{};
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    REQUIRE(omega_snap(nullptr, 100u, &cfg, &res) == OMEGA_ERR_INVALID);
    REQUIRE(omega_snap(e, 100u, nullptr, &res) == OMEGA_ERR_INVALID);
    REQUIRE(omega_snap(e, 100u, &cfg, nullptr) == OMEGA_ERR_INVALID);

    omega_engine_destroy(e);
}

TEST_CASE("C API snap: GRID in freeform mode without subdiv returns OMEGA_ERR_NO_METER")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    omega_snap_config_t cfg{OMEGA_SNAP_GRID, 0u, 0u};
    omega_snap_result_t res{};
    REQUIRE(omega_snap(e, 100u, &cfg, &res) == OMEGA_ERR_NO_METER);

    omega_engine_destroy(e);
}

TEST_CASE("C API snap: snap to marker")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    REQUIRE(omega_marker_add(e, "beat", 480u) == OMEGA_OK);

    omega_snap_config_t cfg{OMEGA_SNAP_MARKERS, 0u, 0u};
    omega_snap_result_t res{};
    REQUIRE(omega_snap(e, 470u, &cfg, &res) == OMEGA_OK);
    REQUIRE(res.did_snap == 1);
    REQUIRE(res.snapped_tick == 480u);

    omega_engine_destroy(e);
}

TEST_CASE("C API snap: snap to region boundary")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    REQUIRE(omega_region_add(e, "loop", 0u, 960u, OMEGA_REGION_LOOP) == OMEGA_OK);

    omega_snap_config_t cfg{OMEGA_SNAP_REGIONS, 0u, 0u};
    omega_snap_result_t res{};
    REQUIRE(omega_snap(e, 950u, &cfg, &res) == OMEGA_OK);
    REQUIRE(res.did_snap == 1);
    REQUIRE(res.snapped_tick == 960u);  // nearest boundary is region end

    omega_engine_destroy(e);
}

TEST_CASE("C API snap: GRID with explicit subdivision")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    // No time signature needed when grid_subdiv_ticks is explicit
    omega_snap_config_t cfg{OMEGA_SNAP_GRID, 480u, 0u};
    omega_snap_result_t res{};
    REQUIRE(omega_snap(e, 230u, &cfg, &res) == OMEGA_OK);
    REQUIRE(res.did_snap == 1);
    REQUIRE(res.snapped_tick == 0u);  // nearest grid point at 0 or 480; 230 < 240 -> 0

    omega_engine_destroy(e);
}

TEST_CASE("C API snap: tolerance prevents snap when too far")
{
    omega_engine_t* e = omega_engine_create();
    REQUIRE(e != nullptr);

    REQUIRE(omega_marker_add(e, "far", 960u) == OMEGA_OK);

    // tolerance = 50 ticks; tick 100 is 860 ticks from the marker
    omega_snap_config_t cfg{OMEGA_SNAP_MARKERS, 0u, 50u};
    omega_snap_result_t res{};
    REQUIRE(omega_snap(e, 100u, &cfg, &res) == OMEGA_OK);
    REQUIRE(res.did_snap == 0);
    REQUIRE(res.snapped_tick == 100u);

    omega_engine_destroy(e);
}
