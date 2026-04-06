# Audio plugin — miniaudio-based audio for games and multimedia.
# Off by default. Enable with: cmake -DVIGIL_PLUGIN_AUDIO=ON

option(VIGIL_PLUGIN_AUDIO "Build the audio plugin" OFF)

if(NOT VIGIL_PLUGIN_AUDIO)
    message(STATUS "Plugin 'audio': disabled (VIGIL_PLUGIN_AUDIO=OFF)")
    return()
endif()

vigil_add_plugin(
    NAME audio
    SOURCES audio.c
    LIBRARIES ""
)

# On Apple platforms, miniaudio uses Core Audio which requires ObjC.
# Compile audio.c as ObjC on Apple so framework headers parse correctly.
if(APPLE)
    enable_language(OBJC)
    set_source_files_properties(
        "${CMAKE_CURRENT_LIST_DIR}/audio.c"
        PROPERTIES LANGUAGE OBJC
    )
endif()

# miniaudio is a vendored header-only library with warnings that
# trigger -Werror on strict compilers (Android NDK clang, Emscripten).
# Suppress warnings for the audio.c translation unit.
set_source_files_properties(
    "${CMAKE_CURRENT_LIST_DIR}/audio.c"
    PROPERTIES COMPILE_OPTIONS "-w"
)

# miniaudio is header-only; stb_vorbis for OGG support.
set(VIGIL_MINIAUDIO_INCLUDE "${CMAKE_SOURCE_DIR}/deps/miniaudio;${CMAKE_SOURCE_DIR}/deps/stb" CACHE INTERNAL "")
