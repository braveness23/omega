add_executable(omega_benchmarks
    bench_placeholder.cpp
)

target_link_libraries(omega_benchmarks
    PRIVATE
        omega::core
        Catch2::Catch2WithMain
)

# Benchmarks are not added to the default CTest run.
# The CI benchmark job runs them explicitly via ctest -R bench_.
include(Catch)
catch_discover_tests(omega_benchmarks
    TEST_PREFIX "bench."
)
