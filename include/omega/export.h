#pragma once

/*
 * export.h — symbol visibility and deprecation macros.
 *
 * This file is a hand-written placeholder. Once omega_core is built as a
 * STATIC or SHARED library, replace it with the output of:
 *
 *   include(GenerateExportHeader)
 *   generate_export_header(omega_core
 *       BASE_NAME        OMEGA
 *       EXPORT_FILE_NAME include/omega/export.h
 *   )
 *
 * and add to the target:
 *   set_target_properties(omega_core PROPERTIES
 *       CXX_VISIBILITY_PRESET     hidden
 *       VISIBILITY_INLINES_HIDDEN ON
 *   )
 */

/* ── OMEGA_API ──────────────────────────────────────────────────────────────
 * Marks a symbol as part of the public ABI.
 * Every public C++ symbol and every C API function must be annotated.
 */
#if defined(omega_core_EXPORTS)
    #if defined(_WIN32) || defined(__CYGWIN__)
        #define OMEGA_API __declspec(dllexport)
    #elif defined(__GNUC__) || defined(__clang__)
        #define OMEGA_API __attribute__((visibility("default")))
    #else
        #define OMEGA_API
    #endif
#else
    #if defined(_WIN32) || defined(__CYGWIN__)
        #define OMEGA_API __declspec(dllimport)
    #else
        #define OMEGA_API
    #endif
#endif

/* ── OMEGA_DEPRECATED ───────────────────────────────────────────────────────
 * Annotates a symbol as deprecated with a migration message.
 *
 * Usage (C++ — annotate at declaration, not definition):
 *   [[deprecated("use new_function() — remove after v2.0.0")]]
 *   void old_function();
 *
 * Usage (C API):
 *   OMEGA_DEPRECATED("use omega_new_fn() — remove after v2.0.0")
 *   omega_status_t omega_old_fn(omega_engine_t* e);
 *
 * Deprecated symbols survive for at minimum one MAJOR version cycle.
 * The target removal version must appear in the annotation for grep-ability.
 */
#if defined(__GNUC__) || defined(__clang__)
    #define OMEGA_DEPRECATED(msg) __attribute__((deprecated(msg)))
#elif defined(_MSC_VER)
    #define OMEGA_DEPRECATED(msg) __declspec(deprecated(msg))
#else
    #define OMEGA_DEPRECATED(msg)
#endif
