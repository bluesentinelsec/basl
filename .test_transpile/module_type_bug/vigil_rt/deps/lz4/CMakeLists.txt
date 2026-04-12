# lz4 - BSD-2 licensed fast compression library
# https://github.com/lz4/lz4

add_library(lz4 STATIC lz4.c)
target_include_directories(lz4 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(lz4 PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(lz4 PRIVATE /W0)
else()
    target_compile_options(lz4 PRIVATE -w)
endif()
