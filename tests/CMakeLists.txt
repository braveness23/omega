add_executable(omega_tests
    unit/test_types.cpp
    unit/test_spsc_queue.cpp
    unit/test_commands.cpp
    unit/test_tempo_map.cpp
    unit/test_engine.cpp
    unit/test_clock.cpp
)

target_link_libraries(omega_tests
    PRIVATE
        omega::core
        Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(omega_tests)

# Benchmarks — separate target, run in CI on main pushes only.
if(OMEGA_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
