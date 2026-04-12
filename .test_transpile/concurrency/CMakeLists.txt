cmake_minimum_required(VERSION 3.14)
project(vigil_generated C)
set(CMAKE_C_STANDARD 11)
include(FetchContent)

# Vigil runtime from embedded sources
file(GLOB VIGIL_RT_SOURCES vigil_rt/src/*.c)
file(GLOB VIGIL_RT_STDLIB vigil_rt/src/stdlib/*.c)
file(GLOB VIGIL_RT_PLATFORM vigil_rt/src/platform/*.c)
file(GLOB VIGIL_PLUGIN_SOURCES vigil_rt/plugins/*/*.c)
# Exclude platform files for other OSes
if(WIN32)
    list(FILTER VIGIL_RT_PLATFORM EXCLUDE REGEX "platform_posix\.c$")
    list(FILTER VIGIL_RT_PLATFORM EXCLUDE REGEX "platform_stub\.c$")
elseif(UNIX)
    list(FILTER VIGIL_RT_PLATFORM EXCLUDE REGEX "platform_win32\.c$")
    list(FILTER VIGIL_RT_PLATFORM EXCLUDE REGEX "platform_stub\.c$")
else()
    list(FILTER VIGIL_RT_PLATFORM EXCLUDE REGEX "platform_posix\.c$")
    list(FILTER VIGIL_RT_PLATFORM EXCLUDE REGEX "platform_win32\.c$")
endif()
# Exclude sysquery platform files for other OSes
if(NOT WIN32)
    list(FILTER VIGIL_PLUGIN_SOURCES EXCLUDE REGEX "sysquery_win32\.c$")
endif()
if(NOT APPLE)
    list(FILTER VIGIL_PLUGIN_SOURCES EXCLUDE REGEX "sysquery_darwin\.c$")
    list(FILTER VIGIL_PLUGIN_SOURCES EXCLUDE REGEX "gui_cocoa\.m$")
endif()
if(NOT UNIX OR APPLE)
    list(FILTER VIGIL_PLUGIN_SOURCES EXCLUDE REGEX "sysquery_linux\.c$")
    list(FILTER VIGIL_PLUGIN_SOURCES EXCLUDE REGEX "gui_gtk\.c$")
endif()
# Exclude stubs when real platform impl is available
if(WIN32 OR APPLE OR UNIX)
    list(FILTER VIGIL_PLUGIN_SOURCES EXCLUDE REGEX "sysquery_stub\.c$")
endif()
# Exclude complex sources not needed for basic transpilation
list(FILTER VIGIL_RT_SOURCES EXCLUDE REGEX "(dap|debugger|lsp|editor|embed|package|pkg|coverage|cli_test)\.c$")
list(FILTER VIGIL_PLUGIN_SOURCES EXCLUDE REGEX "gui_sdl\.c$")
# Exclude plugins that need external deps not yet available
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vigil_rt/NEEDS_AUDIO)
    list(FILTER VIGIL_PLUGIN_SOURCES EXCLUDE REGEX "audio\.c$")
endif()
# Exclude SDL if not needed
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vigil_rt/NEEDS_SDL)
    list(FILTER VIGIL_PLUGIN_SOURCES EXCLUDE REGEX "sdl\\.c$")
    list(FILTER VIGIL_PLUGIN_SOURCES EXCLUDE REGEX "vigil_image\\.c$")
    list(FILTER VIGIL_PLUGIN_SOURCES EXCLUDE REGEX "vigil_font\\.c$")
    list(FILTER VIGIL_PLUGIN_SOURCES EXCLUDE REGEX "gui")
endif()
# Exclude plugins that need external deps not yet available
add_library(vigil_rt STATIC ${VIGIL_RT_SOURCES} ${VIGIL_RT_STDLIB}
    ${VIGIL_RT_PLATFORM} ${VIGIL_PLUGIN_SOURCES})
target_include_directories(vigil_rt PUBLIC
    vigil_rt/include vigil_rt/src vigil_rt)
target_compile_definitions(vigil_rt PRIVATE VIGIL_VERSION="0.0.0")
target_compile_definitions(vigil_rt PUBLIC
    VIGIL_HAS_STDLIB_FS VIGIL_HAS_STDLIB_NET VIGIL_HAS_STDLIB_HTTP
    VIGIL_HAS_STDLIB_THREAD VIGIL_HAS_STDLIB_TIME VIGIL_HAS_STDLIB_READLINE
    VIGIL_HAS_STDLIB_FFI)
# Use the embedded plugin registry (no plugins in self-contained build)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/vigil_rt/generated/transpile_plugin_registry.h
     DESTINATION ${CMAKE_BINARY_DIR}/generated/
     FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
file(RENAME ${CMAKE_BINARY_DIR}/generated/transpile_plugin_registry.h
     ${CMAKE_BINARY_DIR}/generated/plugin_registry.h)
target_include_directories(vigil_rt PUBLIC ${CMAKE_BINARY_DIR} ${CMAKE_BINARY_DIR}/generated)
set_target_properties(vigil_rt PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Main executable
add_executable(vigil_app vigil_main.c vigil_generated.c)
target_link_libraries(vigil_app PRIVATE vigil_rt)
target_include_directories(vigil_app PRIVATE vigil_rt/include vigil_rt/src)

# System libraries
include(CheckLibraryExists)
check_library_exists(m floor "" NEEDS_LIBM)
if(NEEDS_LIBM)
    target_link_libraries(vigil_rt PRIVATE m)
endif()
check_library_exists(dl dlopen "" NEEDS_LIBDL)
if(NEEDS_LIBDL)
    target_link_libraries(vigil_rt PRIVATE dl)
endif()
if(WIN32)
    target_link_libraries(vigil_rt PRIVATE crypt32)
endif()
if(APPLE)
    target_link_libraries(vigil_rt PRIVATE "-framework Security" "-framework CoreFoundation")
endif()

# Vendored deps bundled with vigil_rt
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vigil_rt/deps/miniz)
    add_subdirectory(vigil_rt/deps/miniz)
    target_link_libraries(vigil_rt PRIVATE miniz)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vigil_rt/deps/lz4)
    add_subdirectory(vigil_rt/deps/lz4)
    target_link_libraries(vigil_rt PRIVATE lz4)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vigil_rt/deps/crypto)
    add_subdirectory(vigil_rt/deps/crypto)
    target_link_libraries(vigil_rt PRIVATE vigil_crypto)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vigil_rt/deps/libffi/CMakeLists.txt)
    add_subdirectory(vigil_rt/deps/libffi)
    target_link_libraries(vigil_rt PRIVATE ffi_static)
    target_compile_definitions(vigil_rt PRIVATE VIGIL_HAS_LIBFFI)
endif()

# SDL3 via FetchContent (only if sdl plugin sources are present)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vigil_rt/NEEDS_SDL)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDL_PIPEWIRE OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.2
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL3)
    target_link_libraries(vigil_rt PRIVATE SDL3::SDL3-static)
    target_include_directories(vigil_rt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vigil_rt/deps)
    target_compile_definitions(vigil_rt PUBLIC VIGIL_PLUGIN_SDL_ENABLED)
else()
    # No SDL — exclude SDL plugin sources
    list(FILTER VIGIL_PLUGIN_SOURCES EXCLUDE REGEX "sdl\\.c$")
    list(FILTER VIGIL_PLUGIN_SOURCES EXCLUDE REGEX "vigil_image\\.c$")
    list(FILTER VIGIL_PLUGIN_SOURCES EXCLUDE REGEX "vigil_font\\.c$")
endif()
# stb headers for image/font loading
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vigil_rt/deps/stb)
    target_include_directories(vigil_rt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vigil_rt/deps)
endif()
# miniaudio via FetchContent (for audio plugin)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vigil_rt/NEEDS_AUDIO)
    include(FetchContent)
    FetchContent_Declare(miniaudio
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG 0.11.21
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(miniaudio)
    target_include_directories(vigil_rt PRIVATE ${miniaudio_SOURCE_DIR})
    target_include_directories(vigil_rt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vigil_rt/deps/stb)
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/vigil_rt/plugins/audio/audio.c
        PROPERTIES COMPILE_OPTIONS "-w")
    target_compile_definitions(vigil_rt PUBLIC VIGIL_PLUGIN_AUDIO_ENABLED)
endif()
