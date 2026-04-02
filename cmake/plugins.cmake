# ── Plugin support ───────────────────────────────────────────────────
# Plugins are discovered from plugins/*/plugin.cmake.  Each plugin
# calls vigil_add_plugin() to register sources and dependencies.

option(VIGIL_PLUGINS "Build plugins from plugins/ directory" ON)

set(VIGIL_PLUGIN_SOURCES "")
set(VIGIL_PLUGIN_EXTERN_DECLS "")
set(VIGIL_PLUGIN_TABLE_FILL "")
set(VIGIL_PLUGIN_COUNT 0)

if(VIGIL_PLUGINS)
    # Macro for plugin.cmake files to declare a plugin.
    # Usage: vigil_add_plugin(NAME <name> SOURCES <src...>
    #                         [LIBRARIES <lib...>] [FIND_PACKAGES <pkg...>])
    macro(vigil_add_plugin)
        cmake_parse_arguments(_PLUG "" "NAME" "SOURCES;LIBRARIES;FIND_PACKAGES" ${ARGN})

        set(_plug_ok TRUE)
        foreach(_pkg ${_PLUG_FIND_PACKAGES})
            find_package(${_pkg} QUIET)
            if(NOT ${_pkg}_FOUND)
                message(STATUS "Plugin '${_PLUG_NAME}': skipped (${_pkg} not found)")
                set(_plug_ok FALSE)
                break()
            endif()
        endforeach()

        if(_plug_ok)
            foreach(_src ${_PLUG_SOURCES})
                list(APPEND VIGIL_PLUGIN_SOURCES "${_plug_dir}/${_src}")
            endforeach()

            string(APPEND VIGIL_PLUGIN_EXTERN_DECLS
                "extern VIGIL_API const vigil_native_module_t vigil_plugin_${_PLUG_NAME};\n")
            string(APPEND VIGIL_PLUGIN_TABLE_FILL
                "    table[i].name = \"${_PLUG_NAME}\"; table[i].name_length = ${_plug_name_len}U; table[i].module = &vigil_plugin_${_PLUG_NAME}; i++;\n")

            foreach(_lib ${_PLUG_LIBRARIES})
                list(APPEND VIGIL_PLUGIN_LIBRARIES ${_lib})
            endforeach()

            math(EXPR VIGIL_PLUGIN_COUNT "${VIGIL_PLUGIN_COUNT} + 1")
            message(STATUS "Plugin '${_PLUG_NAME}': enabled")
        endif()
    endmacro()

    file(GLOB _plugin_cmake_files "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*/plugin.cmake")
    foreach(_plugin_cmake ${_plugin_cmake_files})
        get_filename_component(_plug_dir "${_plugin_cmake}" DIRECTORY)
        get_filename_component(_plug_name "${_plug_dir}" NAME)
        string(LENGTH "${_plug_name}" _plug_name_len)
        include("${_plugin_cmake}")
    endforeach()

    message(STATUS "Plugins: ${VIGIL_PLUGIN_COUNT} enabled")
endif()

# Generate plugin_registry.h (always — with zero plugins it's a no-op header).
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/plugin_registry.h.in"
    "${CMAKE_BINARY_DIR}/generated/plugin_registry.h"
    @ONLY
)
