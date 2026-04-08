# ── Build options and platform detection ─────────────────────────────

option(VIGIL_BUILD_TESTS "Build VIGIL unit tests" ON)
option(VIGIL_USE_LIBFFI "Build with vendored libffi for full FFI support" ON)
option(VIGIL_ENABLE_COVERAGE "Build with coverage instrumentation" OFF)
option(VIGIL_ENABLE_BEARSSL_TLS "Enable BearSSL TLS for the fallback HTTPS client" ON)
option(VIGIL_OPCODE_PROFILE "Instrument regvm with per-opcode dispatch counters" OFF)
option(VIGIL_ENABLE_AOT "Build with MIR-backed load-time AOT support" ON)

# ── Platform detection ───────────────────────────────────────────────

set(VIGIL_HAS_DESKTOP_PLATFORM ON)
if(EMSCRIPTEN
   OR CMAKE_SYSTEM_NAME STREQUAL "iOS"
   OR CMAKE_SYSTEM_NAME STREQUAL "Android"
   OR (NOT WIN32 AND NOT UNIX))
    set(VIGIL_HAS_DESKTOP_PLATFORM OFF)
endif()

if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(VIGIL_ENABLE_AOT OFF CACHE BOOL "Build with MIR-backed load-time AOT support" FORCE)
endif()

# ── Per-capability flags ─────────────────────────────────────────────
# Android and iOS use platform_posix.c which supports fs, threads, time,
# sockets. Only ffi (dlopen) and readline (interactive stdin) are truly
# desktop-only. Emscripten gets its own platform file in Phase 2.

if(EMSCRIPTEN)
    # fs uses MEMFS (ephemeral). See docs/stdlib-portability.md for
    # IDBFS/NODEFS persistence options.
    set(VIGIL_HAS_FILESYSTEM  ON)
    set(VIGIL_HAS_THREADS     ON)
    set(VIGIL_HAS_TIME        ON)
    set(VIGIL_HAS_NETWORK     ON)
    set(VIGIL_HAS_HTTP        ON)
    set(VIGIL_HAS_FFI         OFF)
    set(VIGIL_HAS_READLINE    OFF)
elseif(VIGIL_HAS_DESKTOP_PLATFORM)
    set(VIGIL_HAS_FILESYSTEM  ON)
    set(VIGIL_HAS_THREADS     ON)
    set(VIGIL_HAS_TIME        ON)
    set(VIGIL_HAS_NETWORK     ON)
    set(VIGIL_HAS_HTTP        ON)
    set(VIGIL_HAS_FFI         ON)
    set(VIGIL_HAS_READLINE    ON)
else()
    # Android, iOS — POSIX capable
    set(VIGIL_HAS_FILESYSTEM  ON)
    set(VIGIL_HAS_THREADS     ON)
    set(VIGIL_HAS_TIME        ON)
    set(VIGIL_HAS_NETWORK     ON)
    set(VIGIL_HAS_HTTP        ON)
    set(VIGIL_HAS_FFI         OFF)
    set(VIGIL_HAS_READLINE    OFF)
endif()

# ── Stdlib module options ────────────────────────────────────────────

option(VIGIL_STDLIB_FFI "Build ffi stdlib module" ${VIGIL_HAS_FFI})
option(VIGIL_STDLIB_FS "Build fs stdlib module" ${VIGIL_HAS_FILESYSTEM})
option(VIGIL_STDLIB_HTTP "Build http stdlib module" ${VIGIL_HAS_HTTP})
option(VIGIL_STDLIB_NET "Build net stdlib module" ${VIGIL_HAS_NETWORK})
option(VIGIL_STDLIB_READLINE "Build readline stdlib module" ${VIGIL_HAS_READLINE})
option(VIGIL_STDLIB_THREAD "Build thread stdlib module" ${VIGIL_HAS_THREADS})
option(VIGIL_STDLIB_TIME "Build time stdlib module" ${VIGIL_HAS_TIME})

# ── Compiler warning flags ───────────────────────────────────────────

if(MSVC)
    set(VIGIL_WARNING_FLAGS /W4 /WX)
else()
    set(VIGIL_WARNING_FLAGS -Wall -Wextra -Wpedantic -Werror)
endif()

# ── Coverage instrumentation ─────────────────────────────────────────

if(VIGIL_ENABLE_COVERAGE)
    if(MSVC)
        message(FATAL_ERROR "VIGIL_ENABLE_COVERAGE is not supported with MSVC")
    elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-O0 -g --coverage -fprofile-abs-path)
        add_link_options(--coverage)
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
        add_compile_options(-O0 -g --coverage)
        add_link_options(--coverage)
    else()
        message(FATAL_ERROR "VIGIL_ENABLE_COVERAGE requires GCC or Clang")
    endif()
endif()

# ── Library type ─────────────────────────────────────────────────────

if(NOT VIGIL_HAS_DESKTOP_PLATFORM)
    set(VIGIL_LIBRARY_TYPE STATIC)
elseif(NOT DEFINED VIGIL_LIBRARY_TYPE)
    set(VIGIL_LIBRARY_TYPE SHARED)
endif()
