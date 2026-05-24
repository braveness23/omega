add_executable(omega_tests
    unit/test_types.cpp
    unit/test_spsc_queue.cpp
    unit/test_commands.cpp
    unit/test_tempo_map.cpp
    unit/test_engine.cpp
    unit/test_clock.cpp
    unit/test_sink.cpp
    unit/test_timeline.cpp
    unit/test_pattern_library.cpp
    unit/test_song_arrangement.cpp
    unit/test_performance_source.cpp
    unit/test_event_input.cpp
    unit/test_modulation_bus.cpp
    unit/test_perf_context.cpp
    unit/test_custom_source.cpp
    unit/test_time_signature_map.cpp
    unit/test_smpte_converter.cpp
    unit/test_midi_io.cpp
    unit/test_marker_list.cpp
    unit/test_region_list.cpp
    unit/test_smf_import.cpp
    unit/test_smf_export.cpp
)

target_link_libraries(omega_tests
    PRIVATE
        omega::omega
        midifile
        Catch2::Catch2WithMain
)

target_include_directories(omega_tests
    PRIVATE
        ${midifile_SOURCE_DIR}/include
)

include(Catch)
catch_discover_tests(omega_tests)

# ── Integration tests (C API) ─────────────────────────────────────────────────
add_executable(omega_integration_tests
    integration/test_c_api_playback.cpp
    integration/test_c_api_patterns.cpp
    integration/test_c_api_timesig.cpp
)

target_link_libraries(omega_integration_tests
    PRIVATE
        omega::omega
        Catch2::Catch2WithMain
)

catch_discover_tests(omega_integration_tests)

# Benchmarks — separate target, run in CI on main pushes only.
if(OMEGA_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
