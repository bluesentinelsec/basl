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

# miniaudio is header-only; stb_vorbis for OGG support.
set(VIGIL_MINIAUDIO_INCLUDE "${CMAKE_SOURCE_DIR}/deps/miniaudio;${CMAKE_SOURCE_DIR}/deps/stb" CACHE INTERNAL "")
