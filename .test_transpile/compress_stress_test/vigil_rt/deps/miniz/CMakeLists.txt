# miniz - MIT licensed compression library
# https://github.com/richgel999/miniz

add_library(miniz STATIC
    miniz.c
    miniz_tdef.c
    miniz_tinfl.c
    miniz_zip.c
)

target_include_directories(miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Position independent code for shared library builds
set_target_properties(miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Disable warnings for third-party code
if(MSVC)
    target_compile_options(miniz PRIVATE /W0)
else()
    target_compile_options(miniz PRIVATE -w)
endif()
