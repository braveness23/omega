cmake_minimum_required(VERSION 3.16)

project(omega
    VERSION 1.1.0
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
set(OMEGA_SOVERSION 1)

# ── Build options ────────────────────────────────────────────────────────────
option(OMEGA_BUILD_TESTS       "Build the test suite"                         ON)
option(OMEGA_BUILD_BENCHMARKS  "Build benchmark targets (requires test suite)" OFF)
option(OMEGA_BUILD_SHARED      "Build omega as a shared library (DLL/.so)"    OFF)
option(OMEGA_WITH_LINK         "Enable Ableton Link sync (requires GPL v2+)"  OFF)
option(OMEGA_WITH_SANITIZERS   "Build with AddressSanitizer and UBSanitizer"  OFF)
option(OMEGA_WITH_TSAN         "Build with ThreadSanitizer"                   OFF)
# OMEGA_BUILD_WASM: exclude host-coupled sources (libremidi, OmegaTimer) so the
# build produces a WASM-safe library. Auto-enabled when CMake detects Emscripten.
option(OMEGA_BUILD_WASM
    "Build without host-coupled sources (libremidi, OmegaTimer) — WASM/plugin-safe"
    OFF)

# Auto-enable when building under Emscripten.
if(EMSCRIPTEN)
    set(OMEGA_BUILD_WASM ON CACHE BOOL
        "WASM mode auto-enabled for Emscripten" FORCE)
endif()

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

# libremidi: WinRT/UWP (WinUWP) and WinMIDI backends require cppwinrt headers
# that are absent under mingw.  Disable them automatically so omega consumers
# don't need to add workarounds themselves; the WinMM backend remains available
# and is sufficient for classic MIDI I/O.
if(MINGW AND NOT OMEGA_BUILD_WASM)
    set(LIBREMIDI_NO_WINUWP  ON CACHE BOOL "" FORCE)
    set(LIBREMIDI_NO_WINMIDI ON CACHE BOOL "" FORCE)
endif()

# midifile declares cmake_minimum_required(VERSION 2.8), which CMake 4.x refuses.
# CMAKE_POLICY_VERSION_MINIMUM (CMake 3.27+) floors the accepted version so the
# sub-project declaration doesn't abort configure.  Has no effect on CMake < 3.27.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

if(OMEGA_BUILD_WASM)
    # In WASM mode, libremidi is excluded — it links OS MIDI libraries that do
    # not exist under Emscripten (or in plugin hosts). Only fetch midifile.
    FetchContent_MakeAvailable(midifile)
else()
    FetchContent_MakeAvailable(libremidi midifile)
    find_package(Threads REQUIRED)
endif()

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
# When OMEGA_BUILD_SHARED is ON, omega_core is compiled as an OBJECT library:
# its TUs are compiled once and bundled into the omega SHARED target, producing
# a single omega.dll with no separate omega_core dependency for consumers.
if(OMEGA_BUILD_SHARED)
    set(_omega_core_type OBJECT)
else()
    set(_omega_core_type STATIC)
endif()

# ── Source lists ─────────────────────────────────────────────────────────────
# WASM-safe sources compile under Emscripten and in plugin-host contexts.
# Host-coupled sources link OS MIDI (libremidi) or spawn threads (OmegaTimer)
# and are excluded when OMEGA_BUILD_WASM is ON.

set(_omega_wasm_safe_sources
    src/types.cpp
    src/tempo_map.cpp
    src/time_signature_map.cpp
    src/smpte_converter.cpp
    src/clock.cpp
    src/sink.cpp
    src/drain_sink.cpp
    src/timeline.cpp
    src/pattern_library.cpp
    src/song_arrangement_source.cpp
    src/performance_source.cpp
    src/modulation_bus.cpp
    src/modulators.cpp
    src/transform_source.cpp
    src/marker_list.cpp
    src/region_list.cpp
    src/anchor_point.cpp
    src/event_anchor_table.cpp
    src/snap.cpp
    src/smf_import.cpp
    src/smf_export.cpp
    src/session.cpp
    src/recorder.cpp
    src/control_sink.cpp
    src/engine.cpp
)

set(_omega_host_sources
    src/libremidi_sink.cpp
    src/libremidi_input.cpp
    src/timer.cpp
)

if(OMEGA_BUILD_WASM)
    add_library(omega_core ${_omega_core_type} ${_omega_wasm_safe_sources})
else()
    add_library(omega_core ${_omega_core_type}
        ${_omega_wasm_safe_sources}
        ${_omega_host_sources}
    )
endif()
add_library(omega::core ALIAS omega_core)

set_target_properties(omega_core PROPERTIES
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON
)
# OBJECT libraries produce no output artifact; VERSION/SOVERSION don't apply.
if(NOT OMEGA_BUILD_SHARED)
    set_target_properties(omega_core PROPERTIES
        VERSION   ${PROJECT_VERSION}
        SOVERSION ${OMEGA_SOVERSION}
    )
endif()

target_include_directories(omega_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(omega_core PUBLIC cxx_std_17)

if(OMEGA_BUILD_SHARED)
    # Compile with dllexport; stays PRIVATE so consumers see dllimport instead.
    target_compile_definitions(omega_core PRIVATE omega_core_EXPORTS)
else()
    # Static builds need no dllimport/dllexport decoration on Windows.
    # Propagates to consumers so export.h resolves OMEGA_API correctly.
    target_compile_definitions(omega_core PUBLIC OMEGA_STATIC_LIBRARY)
endif()

if(OMEGA_BUILD_WASM)
    # Expose OMEGA_NO_HOST_MIDI to all consumers so they know libremidi /
    # OmegaTimer APIs are absent. Also propagates through omega.h includes.
    target_compile_definitions(omega_core PUBLIC OMEGA_NO_HOST_MIDI)
    target_link_libraries(omega_core PRIVATE midifile)
else()
    target_link_libraries(omega_core
        PRIVATE
            libremidi
            midifile
            Threads::Threads
    )
endif()

target_include_directories(omega_core
    PRIVATE
        ${midifile_SOURCE_DIR}/include
)

if(OMEGA_WITH_LINK AND NOT OMEGA_BUILD_WASM)
    target_link_libraries(omega_core PRIVATE Ableton::Link)
    target_compile_definitions(omega_core PUBLIC OMEGA_WITH_LINK=1)
endif()

# ── WASM Embind target ────────────────────────────────────────────────────────
# omega_wasm: the JavaScript-facing Embind module. Built only when Emscripten
# is active. Consumers link omega::core + midifile; bindings live in wasm/.
# kcs-web/CMakeLists.txt switches to this target and drops its hand-curated
# source list (resolves W1).
if(EMSCRIPTEN)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/wasm/bindings.cpp")
        add_executable(omega_js wasm/bindings.cpp)
        target_link_libraries(omega_js PRIVATE omega::core midifile)
        target_compile_features(omega_js PRIVATE cxx_std_17)

        set_target_properties(omega_js PROPERTIES
            OUTPUT_NAME "omega"
            SUFFIX ".js"
        )

        target_link_options(omega_js PRIVATE
            -lembind
            -sMODULARIZE=1
            -sEXPORT_ES6=1
            "-sEXPORT_NAME=createOmegaModule"
            -sALLOW_MEMORY_GROWTH=1
            -sFORCE_FILESYSTEM=1
            -sNO_EXIT_RUNTIME=1
            -sEXPORTED_RUNTIME_METHODS=FS
            -O2
        )
    endif()
endif()

# ── C API target ──────────────────────────────────────────────────────────────
# Wraps omega_core with a stable C ABI.
# When OMEGA_BUILD_SHARED is ON, this is a SHARED library (DLL/.so) that bundles
# omega_core's object files. Consumers (e.g. P/Invoke from .NET) load one file.
if(OMEGA_BUILD_SHARED)
    add_library(omega SHARED src/omega_c.cpp)
    # omega_c.cpp also needs dllexport during the build pass.
    target_compile_definitions(omega PRIVATE omega_core_EXPORTS)
    set_target_properties(omega PROPERTIES
        CXX_VISIBILITY_PRESET     hidden
        VISIBILITY_INLINES_HIDDEN ON
        VERSION                   ${PROJECT_VERSION}
        SOVERSION                 ${OMEGA_SOVERSION}
    )
    # Windows: strip the lib prefix so [DllImport("omega")] resolves omega.dll.
    if(WIN32)
        set_target_properties(omega PROPERTIES PREFIX "")
    endif()
    # mingw: statically embed the C++ runtime so omega.dll is self-contained.
    if(MINGW)
        target_link_options(omega PRIVATE
            -static-libgcc
            -static-libstdc++
            -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive -Wl,-Bdynamic
        )
    endif()
    # PRIVATE: omega_core's objects are bundled into the DLL; no export-set entry
    # needed for omega_core. OBJECT lib PRIVATE deps don't auto-propagate, so
    # libremidi/midifile/Threads are listed here explicitly.
    target_link_libraries(omega PRIVATE omega::core libremidi midifile Threads::Threads)
    target_include_directories(omega PRIVATE ${midifile_SOURCE_DIR}/include)
    # Expose omega's public headers and C++ standard to consumers of the DLL.
    target_include_directories(omega
        INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    target_compile_features(omega INTERFACE cxx_std_17)
else()
    add_library(omega STATIC src/omega_c.cpp)
    # PUBLIC: omega_core's archive and INTERFACE (include dirs, cxx_std_17)
    # propagate to consumers through the link chain.
    target_link_libraries(omega PUBLIC omega::core)
endif()
add_library(omega::omega ALIAS omega)

# ── Tests ─────────────────────────────────────────────────────────────────────
if(OMEGA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(OMEGA_BUILD_BENCHMARKS AND NOT OMEGA_BUILD_TESTS)
    message(FATAL_ERROR "OMEGA_BUILD_BENCHMARKS requires OMEGA_BUILD_TESTS=ON (benchmarks use Catch2).")
endif()

# ── Install rules ─────────────────────────────────────────────────────────────
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# CMake package config files must go in a path that find_package() actually
# searches. On multiarch Ubuntu, CMAKE_INSTALL_LIBDIR is 'lib/x86_64-linux-gnu'
# which cmake does NOT search. Use a non-multiarch path explicitly.
set(OMEGA_INSTALL_CMAKEDIR "lib/cmake/omega"
    CACHE PATH "Installation directory for CMake package config files")

# omega_core is an OBJECT library when OMEGA_BUILD_SHARED=ON; OBJECT libraries
# have no installable artifact so they must be excluded from the install target.
if(OMEGA_BUILD_SHARED)
    set(_omega_install_targets omega midifile)
else()
    set(_omega_install_targets omega_core omega midifile)
endif()
install(TARGETS ${_omega_install_targets}
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
