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
)

target_link_libraries(omega_tests
    PRIVATE
        omega::core
        Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(omega_tests)

# ── Integration tests (C API) ─────────────────────────────────────────────────
add_executable(omega_integration_tests
    integration/test_c_api_playback.cpp
    integration/test_c_api_patterns.cpp
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
