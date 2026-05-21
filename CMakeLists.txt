cmake_minimum_required(VERSION 3.16)

project(omega
    VERSION 0.1.0
    DESCRIPTION "A C++ sequencing engine and MIDI library"
    HOMEPAGE_URL "https://github.com/braveness23/omega"
    LANGUAGES CXX C
)

# ── C++ standard ────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── ABI versioning ───────────────────────────────────────────────────────────
# SOVERSION increments only on ABI-breaking changes; never automatically.
# Starts at 0 and is bumped manually as part of the release process.
set(OMEGA_SOVERSION 0)

# ── Build options ────────────────────────────────────────────────────────────
option(OMEGA_BUILD_TESTS       "Build the test suite"                         ON)
option(OMEGA_BUILD_BENCHMARKS  "Build benchmark targets (requires test suite)" OFF)
option(OMEGA_BUILD_EXAMPLES    "Build example applications"                   OFF)
option(OMEGA_WITH_LINK         "Enable Ableton Link sync (requires GPL v2+)"  OFF)
option(OMEGA_WITH_SANITIZERS   "Build with AddressSanitizer and UBSanitizer"  OFF)
option(OMEGA_WITH_TSAN         "Build with ThreadSanitizer"                   OFF)

if(OMEGA_WITH_SANITIZERS AND OMEGA_WITH_TSAN)
    message(FATAL_ERROR
        "OMEGA_WITH_SANITIZERS (ASan/UBSan) and OMEGA_WITH_TSAN (TSan) are "
        "mutually exclusive. Enable only one at a time."
    )
endif()

# ── Ableton Link license warning ─────────────────────────────────────────────
if(OMEGA_WITH_LINK)
    message(WARNING
        "OMEGA_WITH_LINK is ON. "
        "Ableton Link is licensed under GPL v2+. "
        "The combined Omega+Link build is subject to GPL v2+. "
        "See docs/design/07-extensions.md for details."
    )
endif()

# ── Dependencies (FetchContent) ───────────────────────────────────────────────
# All GIT_TAG values are pinned to specific tags or commit hashes.
# Never use branch names (master, main). Update deliberately via PR.
# See THIRD_PARTY_LICENSES.md for the full inventory.
include(FetchContent)

# libremidi — MIDI I/O (MIT)
FetchContent_Declare(libremidi
    GIT_REPOSITORY https://github.com/celtera/libremidi.git
    GIT_TAG        v5.1.0          # pinned — see THIRD_PARTY_LICENSES.md
    GIT_SHALLOW    TRUE
)

# midifile — SMF import/export (BSD 2-Clause)
# midifile has no version tags; pinned to a specific commit hash.
FetchContent_Declare(midifile
    GIT_REPOSITORY https://github.com/craigsapp/midifile.git
    GIT_TAG        98917df5b1bf0d6e8d4c0e5fff86d6b05343e793
    GIT_SHALLOW    TRUE
)

# Catch2 — test framework (BSL-1.0) — only when testing
if(OMEGA_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.2      # pinned — see THIRD_PARTY_LICENSES.md
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)
endif()

FetchContent_MakeAvailable(libremidi midifile)

# Ableton Link (GPL v2+) — optional
if(OMEGA_WITH_LINK)
    FetchContent_Declare(ableton_link
        GIT_REPOSITORY https://github.com/Ableton/link.git
        GIT_TAG        Link-3.1.5  # pinned — see THIRD_PARTY_LICENSES.md
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(ableton_link)
endif()

# ── Sanitizers ───────────────────────────────────────────────────────────────
# ASan+UBSan (dev preset) and TSan (tsan preset) are mutually exclusive.
if(OMEGA_WITH_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

if(OMEGA_WITH_TSAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
        add_link_options(-fsanitize=thread)
    endif()
endif()

# ── Core library target ───────────────────────────────────────────────────────
add_library(omega_core STATIC
    src/types.cpp
    src/tempo_map.cpp
    src/clock.cpp
    src/sink.cpp
    src/timeline.cpp
    src/engine.cpp
)
add_library(omega::core ALIAS omega_core)

set_target_properties(omega_core PROPERTIES
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION                   ${PROJECT_VERSION}
    SOVERSION                 ${OMEGA_SOVERSION}
)

target_include_directories(omega_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(omega_core PUBLIC cxx_std_17)

# Static builds need no dllimport/dllexport decoration on Windows.
# Propagates to consumers so export.h resolves OMEGA_API correctly.
target_compile_definitions(omega_core PUBLIC OMEGA_STATIC_LIBRARY)

target_link_libraries(omega_core
    PRIVATE
        libremidi
)

if(OMEGA_WITH_LINK)
    target_link_libraries(omega_core PRIVATE Ableton::Link)
    target_compile_definitions(omega_core PUBLIC OMEGA_WITH_LINK=1)
endif()

# ── C API target ──────────────────────────────────────────────────────────────
# Wraps omega_core with a stable C ABI.
# Placeholder until src/omega_c.cpp exists.
add_library(omega INTERFACE)
add_library(omega::omega ALIAS omega)
target_link_libraries(omega INTERFACE omega::core)

# ── Tests ─────────────────────────────────────────────────────────────────────
if(OMEGA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(OMEGA_BUILD_BENCHMARKS AND NOT OMEGA_BUILD_TESTS)
    message(FATAL_ERROR "OMEGA_BUILD_BENCHMARKS requires OMEGA_BUILD_TESTS=ON (benchmarks use Catch2).")
endif()

# ── Examples ──────────────────────────────────────────────────────────────────
if(OMEGA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ── Install rules ─────────────────────────────────────────────────────────────
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# CMake package config files must go in a path that find_package() actually
# searches. On multiarch Ubuntu, CMAKE_INSTALL_LIBDIR is 'lib/x86_64-linux-gnu'
# which cmake does NOT search. Use a non-multiarch path explicitly.
set(OMEGA_INSTALL_CMAKEDIR "lib/cmake/omega"
    CACHE PATH "Installation directory for CMake package config files")

install(TARGETS omega_core omega
    EXPORT OmegaTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/omega
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT OmegaTargets
    FILE        OmegaTargets.cmake
    NAMESPACE   omega::
    DESTINATION ${OMEGA_INSTALL_CMAKEDIR}
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/omegaConfigVersion.cmake"
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/omegaConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/omegaConfig.cmake"
    INSTALL_DESTINATION ${OMEGA_INSTALL_CMAKEDIR}
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/omegaConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/omegaConfigVersion.cmake"
    DESTINATION ${OMEGA_INSTALL_CMAKEDIR}
)

# ── pkg-config ────────────────────────────────────────────────────────────────
configure_file(cmake/omega.pc.in ${CMAKE_CURRENT_BINARY_DIR}/omega.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/omega.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
