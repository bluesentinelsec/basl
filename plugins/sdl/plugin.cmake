# SDL3 plugin — fetched and statically linked via FetchContent.
# Off by default. Enable with: cmake -DVIGIL_PLUGIN_SDL=ON

# Auto-disable on cross-compilation targets where SDL3 won't work.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS"
   OR CMAKE_SYSTEM_NAME STREQUAL "Android"
   OR CMAKE_TOOLCHAIN_FILE MATCHES "android")
    message(STATUS "Plugin 'sdl': skipped (not supported on ${CMAKE_SYSTEM_NAME})")
    return()
endif()

option(VIGIL_PLUGIN_SDL "Build the SDL3 plugin" OFF)

if(NOT VIGIL_PLUGIN_SDL)
    message(STATUS "Plugin 'sdl': disabled (VIGIL_PLUGIN_SDL=OFF)")
    return()
endif()

include(FetchContent)

# Force static SDL3 — no shared library, no install rules.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.2
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(SDL3)

vigil_add_plugin(
    NAME sdl
    SOURCES sdl.c
    LIBRARIES SDL3::SDL3-static
)
