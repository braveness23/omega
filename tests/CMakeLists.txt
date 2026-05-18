add_executable(omega_tests
    # Unit tests — add files here as they're written
    # unit/test_timing_model.cpp
    # unit/test_spsc_queue.cpp
    # unit/test_event_dispatch.cpp
    # ...
    placeholder.cpp
)

target_link_libraries(omega_tests
    PRIVATE
        omega::core
        Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(omega_tests)
