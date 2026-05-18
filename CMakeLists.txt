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

# ── Build options ────────────────────────────────────────────────────────────
option(OMEGA_BUILD_TESTS    "Build the test suite"                         ON)
option(OMEGA_BUILD_EXAMPLES "Build example applications"                   OFF)
option(OMEGA_WITH_LINK      "Enable Ableton Link sync (requires GPL v2+)"  OFF)
option(OMEGA_WITH_SANITIZERS "Build with AddressSanitizer and UBSanitizer" OFF)

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
include(FetchContent)

# libremidi — MIDI I/O (MIT)
FetchContent_Declare(libremidi
    GIT_REPOSITORY https://github.com/celtera/libremidi.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

# midifile — SMF import/export (BSD 2-Clause)
FetchContent_Declare(midifile
    GIT_REPOSITORY https://github.com/craigsapp/midifile.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

# Catch2 — test framework (BSL-1.0) — only when testing
if(OMEGA_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)
endif()

FetchContent_MakeAvailable(libremidi midifile)

# Ableton Link (GPL v2+) — optional
if(OMEGA_WITH_LINK)
    FetchContent_Declare(ableton_link
        GIT_REPOSITORY https://github.com/Ableton/link.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(ableton_link)
endif()

# ── Sanitizers ───────────────────────────────────────────────────────────────
if(OMEGA_WITH_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# ── Core library target ───────────────────────────────────────────────────────
# Placeholder until source files exist.
# Replace INTERFACE with STATIC when src/ is populated.
add_library(omega_core INTERFACE)
add_library(omega::core ALIAS omega_core)

target_include_directories(omega_core
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(omega_core INTERFACE cxx_std_17)

target_link_libraries(omega_core
    INTERFACE
        libremidi
)

if(OMEGA_WITH_LINK)
    target_link_libraries(omega_core INTERFACE Ableton::Link)
    target_compile_definitions(omega_core INTERFACE OMEGA_WITH_LINK=1)
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

# ── Examples ──────────────────────────────────────────────────────────────────
if(OMEGA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ── Install rules ─────────────────────────────────────────────────────────────
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

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
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/omega
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/OmegaConfigVersion.cmake"
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/OmegaConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/OmegaConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/omega
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/OmegaConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/OmegaConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/omega
)
