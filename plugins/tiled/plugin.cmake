# Tiled map parser plugin — parses .tmj/.tmx Tiled map files.
# Off by default. Enable with: cmake -DVIGIL_PLUGIN_TILED=ON

option(VIGIL_PLUGIN_TILED "Build the Tiled map parser plugin" OFF)

if(NOT VIGIL_PLUGIN_TILED)
    message(STATUS "Plugin 'tiled': disabled (VIGIL_PLUGIN_TILED=OFF)")
    return()
endif()

vigil_add_plugin(
    NAME tiled
    SOURCES tiled.c
)
