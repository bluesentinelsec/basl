# ── Build options and platform detection ─────────────────────────────

option(VIGIL_BUILD_TESTS "Build VIGIL unit tests" ON)
option(VIGIL_USE_LIBFFI "Build with vendored libffi for full FFI support" ON)
option(VIGIL_ENABLE_COVERAGE "Build with coverage instrumentation" OFF)
option(VIGIL_ENABLE_BEARSSL_TLS "Enable BearSSL TLS for the fallback HTTPS client" ON)

# ── Platform detection ───────────────────────────────────────────────

set(VIGIL_HAS_DESKTOP_PLATFORM ON)
if(EMSCRIPTEN
   OR CMAKE_SYSTEM_NAME STREQUAL "iOS"
   OR CMAKE_SYSTEM_NAME STREQUAL "Android"
   OR (NOT WIN32 AND NOT UNIX))
    set(VIGIL_HAS_DESKTOP_PLATFORM OFF)
endif()

# ── Stdlib module options ────────────────────────────────────────────
# Desktop-only modules default to ON on desktop, OFF elsewhere.

option(VIGIL_STDLIB_FFI "Build ffi stdlib module" ${VIGIL_HAS_DESKTOP_PLATFORM})
option(VIGIL_STDLIB_FS "Build fs stdlib module" ${VIGIL_HAS_DESKTOP_PLATFORM})
option(VIGIL_STDLIB_HTTP "Build http stdlib module" ${VIGIL_HAS_DESKTOP_PLATFORM})
option(VIGIL_STDLIB_NET "Build net stdlib module" ${VIGIL_HAS_DESKTOP_PLATFORM})
option(VIGIL_STDLIB_READLINE "Build readline stdlib module" ${VIGIL_HAS_DESKTOP_PLATFORM})
option(VIGIL_STDLIB_THREAD "Build thread stdlib module" ${VIGIL_HAS_DESKTOP_PLATFORM})
option(VIGIL_STDLIB_TIME "Build time stdlib module" ${VIGIL_HAS_DESKTOP_PLATFORM})

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
