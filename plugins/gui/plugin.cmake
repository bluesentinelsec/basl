# GUI plugin — SDL3-based cross-platform widget toolkit.
# Off by default. Enable with: cmake -DVIGIL_PLUGIN_GUI=ON
# Requires: VIGIL_PLUGIN_SDL=ON

option(VIGIL_PLUGIN_GUI "Build the GUI plugin" OFF)

if(NOT VIGIL_PLUGIN_GUI)
    message(STATUS "Plugin 'gui': disabled (VIGIL_PLUGIN_GUI=OFF)")
    return()
endif()

if(NOT VIGIL_PLUGIN_SDL)
    message(STATUS "Plugin 'gui': disabled (requires VIGIL_PLUGIN_SDL=ON)")
    return()
endif()

vigil_add_plugin(
    NAME gui
    SOURCES gui.c backends/gui_sdl.c
    LIBRARIES SDL3::SDL3-static
)
