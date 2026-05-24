#include <omega/anchor_point.h>
#include <omega/marker_list.h>
#include <omega/region_list.h>
#include <omega/snap.h>
#include <omega/time_signature_map.h>

#include <catch2/catch_test_macros.hpp>

using namespace omega;

namespace
{

// Builds a 4/4 time signature map starting at tick 0.
TimeSignatureMap make_4_4()
{
    TimeSignatureMap m;
    m.insert(0u, 4u, 4u);
    return m;
}

}  // namespace

TEST_CASE("snap_to_nearest: snap to subdivision grid (tick 230 -> 240)", "[snap]")
{
    TimeSignatureMap tsm = make_4_4();
    MeterCursor cursor(tsm);
    MarkerList markers;
    RegionList regions;

    // 8th-note grid: subdiv = 240 ticks (480 PPQN / 2)
    SnapConfig config{SNAP_GRID, 240u, 0u};

    SnapResult r = snap_to_nearest(230u, config, cursor, markers, regions, nullptr);
    CHECK(r.did_snap == true);
    CHECK(r.snapped_tick == 240u);
    CHECK(r.source == SnapTarget::GRID);
}

TEST_CASE("snap_to_nearest: snap to bar boundary via converter (tick 1900 -> 1920)", "[snap]")
{
    TimeSignatureMap tsm = make_4_4();
    MeterCursor cursor(tsm);
    MarkerList markers;
    RegionList regions;

    // grid_subdiv_ticks == 0 -> use MeterCursor (next_boundary = next bar = 1920)
    SnapConfig config{SNAP_GRID, 0u, 0u};

    SnapResult r = snap_to_nearest(1900u, config, cursor, markers, regions, nullptr);
    CHECK(r.did_snap == true);
    CHECK(r.snapped_tick == 1920u);
    CHECK(r.source == SnapTarget::GRID);
}

TEST_CASE("snap_to_nearest: snap to nearest marker", "[snap]")
{
    TimeSignatureMap tsm = make_4_4();
    MeterCursor cursor(tsm);
    MarkerList markers;
    markers.add("section", 500u);
    markers.add("intro", 1000u);
    RegionList regions;

    SnapConfig config{SNAP_MARKERS, 0u, 0u};

    // tick 490 is closest to marker at 500
    SnapResult r = snap_to_nearest(490u, config, cursor, markers, regions, nullptr);
    CHECK(r.did_snap == true);
    CHECK(r.snapped_tick == 500u);
    CHECK(r.source == SnapTarget::MARKERS);

    // tick 900 is closest to marker at 1000 (dist 100) vs 500 (dist 400)
    r = snap_to_nearest(900u, config, cursor, markers, regions, nullptr);
    CHECK(r.did_snap == true);
    CHECK(r.snapped_tick == 1000u);
}

TEST_CASE("snap_to_nearest: snap to region boundary", "[snap]")
{
    TimeSignatureMap tsm = make_4_4();
    MeterCursor cursor(tsm);
    MarkerList markers;
    RegionList regions;
    regions.add("loop", 200u, 800u, RegionType::LOOP);

    SnapConfig config{SNAP_REGIONS, 0u, 0u};

    // tick 180: closer to start at 200 (dist 20) than end at 800 (dist 620)
    SnapResult r = snap_to_nearest(180u, config, cursor, markers, regions, nullptr);
    CHECK(r.did_snap == true);
    CHECK(r.snapped_tick == 200u);
    CHECK(r.source == SnapTarget::REGIONS);

    // tick 780: closer to end at 800 (dist 20) than start at 200 (dist 580)
    r = snap_to_nearest(780u, config, cursor, markers, regions, nullptr);
    CHECK(r.did_snap == true);
    CHECK(r.snapped_tick == 800u);
}

TEST_CASE("snap_to_nearest: anchor-aware snap (item 100 + offset 40 -> grid 480 -> result 440)",
          "[snap]")
{
    TimeSignatureMap tsm = make_4_4();
    MeterCursor cursor(tsm);
    MarkerList markers;
    RegionList regions;

    // SNAP anchor at offset 40
    AnchorList anchors;
    anchors.add("hot", 40u, ANCHOR_SNAP);

    // ANCHORS only, subdiv = 480 (one beat)
    SnapConfig config{SNAP_ANCHORS, 480u, 0u};

    // adjusted = 100 + 40 = 140; next multiple of 480 >= 140 is 480; candidate = 480 - 40 = 440
    SnapResult r = snap_to_nearest(100u, config, cursor, markers, regions, &anchors);
    CHECK(r.did_snap == true);
    CHECK(r.snapped_tick == 440u);
    CHECK(r.source == SnapTarget::ANCHORS);
}

TEST_CASE("snap_to_nearest: tolerance exceeded returns original tick", "[snap]")
{
    TimeSignatureMap tsm = make_4_4();
    MeterCursor cursor(tsm);
    MarkerList markers;
    markers.add("far", 500u);
    RegionList regions;

    // tolerance = 50 ticks; marker is 400 away from tick 100
    SnapConfig config{SNAP_MARKERS, 0u, 50u};

    SnapResult r = snap_to_nearest(100u, config, cursor, markers, regions, nullptr);
    CHECK(r.did_snap == false);
    CHECK(r.snapped_tick == 100u);
}

TEST_CASE("snap_to_nearest: combined GRID and MARKERS, closest wins", "[snap]")
{
    TimeSignatureMap tsm = make_4_4();
    MeterCursor cursor(tsm);
    MarkerList markers;
    // Marker at 350 -- closer to tick 340 than any beat grid point
    markers.add("mid", 350u);
    RegionList regions;

    // Grid: subdiv=480 -- prev=0, next=480; distances from 340 are 340 and 140
    // Marker at 350: distance from 340 is 10
    SnapConfig config{static_cast<uint8_t>(SNAP_GRID | SNAP_MARKERS), 480u, 0u};

    SnapResult r = snap_to_nearest(340u, config, cursor, markers, regions, nullptr);
    CHECK(r.did_snap == true);
    CHECK(r.snapped_tick == 350u);
    CHECK(r.source == SnapTarget::MARKERS);
}

TEST_CASE("snap_to_nearest: freeform mode GRID with no subdiv yields no snap", "[snap]")
{
    // Empty TimeSignatureMap = freeform; converter returns OMEGA_ERR_NO_METER
    TimeSignatureMap tsm;
    MeterCursor cursor(tsm);
    MarkerList markers;
    RegionList regions;

    SnapConfig config{SNAP_GRID, 0u, 0u};

    // No candidates can be added -- converter fails, no markers/regions/anchors
    SnapResult r = snap_to_nearest(100u, config, cursor, markers, regions, nullptr);
    CHECK(r.did_snap == false);
    CHECK(r.snapped_tick == 100u);
}

TEST_CASE("snap_to_nearest: no targets yields no snap", "[snap]")
{
    TimeSignatureMap tsm = make_4_4();
    MeterCursor cursor(tsm);
    MarkerList markers;
    markers.add("A", 480u);
    RegionList regions;

    SnapConfig config{0u, 0u, 0u};  // targets = 0

    SnapResult r = snap_to_nearest(100u, config, cursor, markers, regions, nullptr);
    CHECK(r.did_snap == false);
    CHECK(r.snapped_tick == 100u);
}
