# Sysquery plugin — OS metadata queries and system enumeration.
# Off by default. Enable with: cmake -DVIGIL_PLUGIN_SYSQUERY=ON

option(VIGIL_PLUGIN_SYSQUERY "Build the sysquery plugin" OFF)

if(NOT VIGIL_PLUGIN_SYSQUERY)
    message(STATUS "Plugin 'sysquery': disabled (VIGIL_PLUGIN_SYSQUERY=OFF)")
    return()
endif()

set(_sysquery_sources sysquery.c sysquery_common.c)
set(_sysquery_libs "")

if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    list(APPEND _sysquery_sources sysquery_darwin.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
    list(APPEND _sysquery_sources sysquery_linux.c)
elseif(WIN32)
    list(APPEND _sysquery_sources sysquery_win32.c)
    list(APPEND _sysquery_libs iphlpapi ws2_32 advapi32)
else()
    list(APPEND _sysquery_sources sysquery_stub.c)
endif()

vigil_add_plugin(
    NAME sysquery
    SOURCES ${_sysquery_sources}
    LIBRARIES ${_sysquery_libs}
)
