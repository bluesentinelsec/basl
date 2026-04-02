# ── CLI executable ───────────────────────────────────────────────────

if(NOT VIGIL_HAS_DESKTOP_PLATFORM)
    return()
endif()

add_executable(vigil_cli
    src/cli/main.c
    src/cli_test.c
    src/cli_frontend.c
)

set_target_properties(vigil_cli PROPERTIES OUTPUT_NAME vigil)
target_link_libraries(vigil_cli PRIVATE vigil)
target_include_directories(vigil_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)
target_compile_features(vigil_cli PRIVATE c_std_11)
target_compile_options(vigil_cli PRIVATE ${VIGIL_WARNING_FLAGS})
