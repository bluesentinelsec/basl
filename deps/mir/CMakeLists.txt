add_library(mir_static STATIC
    mir.c
    mir-gen.c
)

target_include_directories(mir_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mir_static PUBLIC c_std_11)
set_target_properties(mir_static PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(mir_static PRIVATE /w)
else()
    target_compile_options(mir_static PRIVATE -w)
endif()
