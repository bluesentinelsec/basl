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
else()
    list(APPEND _gui_sources backends/gui_stub.c)
endif()

vigil_add_plugin(
    NAME gui
    SOURCES ${_gui_sources}
    LIBRARIES ${_gui_libs}
)
