# SDL3 plugin — built from vendored source in deps/sdl3.
# Off by default. Enable with: cmake -DVIGIL_PLUGIN_SDL=ON

option(VIGIL_PLUGIN_SDL "Build the SDL3 plugin" OFF)

if(NOT VIGIL_PLUGIN_SDL)
    message(STATUS "Plugin 'sdl': disabled (VIGIL_PLUGIN_SDL=OFF)")
    return()
endif()

# Build SDL3 as a static library from vendored source.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)

add_subdirectory("${CMAKE_SOURCE_DIR}/deps/sdl3" "${CMAKE_BINARY_DIR}/deps/sdl3")

vigil_add_plugin(
    NAME sdl
    SOURCES sdl.c vigil_image.c
    LIBRARIES SDL3::SDL3-static
)
