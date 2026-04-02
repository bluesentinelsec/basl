cmake_minimum_required(VERSION 3.21)

project(vigil VERSION 0.2.3 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Build configuration ──────────────────────────────────────────────
include(cmake/options.cmake)

# ── Targets ──────────────────────────────────────────────────────────
include(cmake/core.cmake)
include(cmake/stdlib.cmake)
include(cmake/plugins.cmake)
include(cmake/library.cmake)
include(cmake/cli.cmake)

if(VIGIL_BUILD_TESTS)
    include(cmake/tests.cmake)
endif()
