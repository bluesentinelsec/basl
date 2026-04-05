# GUI plugin — cross-platform native widget toolkit.
# Off by default. Enable with: cmake -DVIGIL_PLUGIN_GUI=ON

option(VIGIL_PLUGIN_GUI "Build the GUI plugin" OFF)

if(NOT VIGIL_PLUGIN_GUI)
    message(STATUS "Plugin 'gui': disabled (VIGIL_PLUGIN_GUI=OFF)")
    return()
endif()

set(_gui_sources gui.c)
set(_gui_libs "")

if(APPLE)
    list(APPEND _gui_sources backends/gui_cocoa.m)
    list(APPEND _gui_libs "-framework Foundation" objc)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND _gui_sources backends/gui_gtk.c)
    list(APPEND _gui_libs dl)
elseif(WIN32)
    list(APPEND _gui_sources backends/gui_win32.c)
    list(APPEND _gui_libs user32 gdi32 comdlg32 comctl32)
else()
    list(APPEND _gui_sources backends/gui_stub.c)
endif()

# SDL fallback backend — available when both GUI and SDL plugins are enabled.
if(VIGIL_PLUGIN_SDL)
    list(APPEND _gui_sources backends/gui_sdl.c)
    list(APPEND _gui_libs SDL3::SDL3-static)
    add_compile_definitions(VIGIL_GUI_SDL_BACKEND)
    message(STATUS "Plugin 'gui': SDL fallback backend enabled")
endif()

vigil_add_plugin(
    NAME gui
    SOURCES ${_gui_sources}
    LIBRARIES ${_gui_libs}
)
