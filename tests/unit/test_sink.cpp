#include <omega/test/capturing_sink.h>

#include <catch2/catch_test_macros.hpp>

using namespace omega;

TEST_CASE("CapturingSink starts empty")
{
    CapturingSink sink;
    REQUIRE(sink.count() == 0u);
}

TEST_CASE("CapturingSink records send() calls in order")
{
    CapturingSink sink;
    Event e1 = omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 0);
    Event e2 = omega_make_note_on(480u, sink.sink_id(), 0, 62, 90, 0);
    sink.send(e1);
    sink.send(e2);

    REQUIRE(sink.count() == 2u);
    REQUIRE(sink.first().data[0] == 60);
    REQUIRE(sink.at(1).data[0] == 62);
}

TEST_CASE("CapturingSink has_note_on returns correct results")
{
    CapturingSink sink;
    Event e = omega_make_note_on(0u, sink.sink_id(), 3, 64, 80, 0);
    sink.send(e);

    REQUIRE(sink.has_note_on(64, 3));
    REQUIRE_FALSE(sink.has_note_on(64, 0));
    REQUIRE_FALSE(sink.has_note_on(65, 3));
}

TEST_CASE("CapturingSink has_note_off returns correct results")
{
    CapturingSink sink;
    Event e{};
    e.tick = 240u;
    e.sink_id = sink.sink_id();
    e.payload_tag = OMEGA_NOTE_OFF;
    e.channel = 1;
    e.data[0] = 60;
    e.data[1] = 64;
    sink.send(e);

    REQUIRE(sink.has_note_off(60, 1));
    REQUIRE_FALSE(sink.has_note_off(61, 1));
    REQUIRE_FALSE(sink.has_note_off(60, 0));
}

TEST_CASE("CapturingSink clear() discards all events")
{
    CapturingSink sink;
    sink.send(omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 0));
    REQUIRE(sink.count() == 1u);
    sink.clear();
    REQUIRE(sink.count() == 0u);
}

TEST_CASE("CapturingSink sink_id is unique per instance")
{
    CapturingSink s1;
    CapturingSink s2;
    CapturingSink s3;
    REQUIRE(s1.sink_id() != s2.sink_id());
    REQUIRE(s2.sink_id() != s3.sink_id());
    REQUIRE(s1.sink_id() != s3.sink_id());
}

TEST_CASE("CapturingSink sink_id is non-zero")
{
    CapturingSink sink;
    REQUIRE(sink.sink_id() != 0u);
}
