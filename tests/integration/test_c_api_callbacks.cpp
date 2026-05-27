#include <omega/omega.h>
#include <omega/test/capturing_sink.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

struct CbRec
{
    omega_engine_event_t event;
    uint32_t detail;
};

static void record_cb(omega_engine_event_t event, uint32_t detail, void* userdata)
{
    auto* out = static_cast<std::vector<CbRec>*>(userdata);
    out->push_back({event, detail});
}

static omega_sink_t* as_sink(omega::CapturingSink& s)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<omega_sink_t*>(&s);
}

TEST_CASE("C API: omega_engine_set_event_callback null engine returns OMEGA_ERR_INVALID")
{
    REQUIRE(omega_engine_set_event_callback(nullptr, record_cb, nullptr) == OMEGA_ERR_INVALID);
}

TEST_CASE("C API: omega_engine_set_event_callback receives SLOT_STARTED")
{
    omega_engine_t* e = omega_engine_create();
    omega::CapturingSink sink;
    omega_engine_add_sink(e, as_sink(sink));

    std::vector<CbRec> records;
    omega_engine_set_event_callback(e, record_cb, &records);

    omega_pattern_id_t pat = omega_pattern_create(e, "p", 480u);
    omega_event_t ev = omega_make_note_on(0u, sink.sink_id(), 0, 60, 100, 480u);
    omega_pattern_add_event(e, pat, &ev);
    omega_perf_assign(e, 0u, pat);
    omega_perf_cue(e, 0u, OMEGA_CUE_IMMEDIATE);
    omega_engine_play(e);
    omega_engine_process(e);
    omega_engine_process(e);

    bool found = false;
    for (const auto& r : records)
    {
        if (r.event == OMEGA_EVENT_SLOT_STARTED && r.detail == 0u)
        {
            found = true;
        }
    }
    REQUIRE(found);

    omega_engine_destroy(e);
}

TEST_CASE("C API: omega_engine_set_event_callback NULL cb clears callback")
{
    omega_engine_t* e = omega_engine_create();
    std::vector<CbRec> records;
    omega_engine_set_event_callback(e, record_cb, &records);

    omega_engine_play(e);
    omega_engine_process(e);

    omega_engine_set_event_callback(e, nullptr, nullptr);
    records.clear();

    omega_engine_stop(e);
    omega_engine_process(e);

    REQUIRE(records.empty());

    omega_engine_destroy(e);
}
