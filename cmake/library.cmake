# ── Public shared/static library ─────────────────────────────────────
# Assembles vigil_core objects, stdlib modules, plugins, and the
# platform abstraction layer into the final vigil library.

add_library(vigil ${VIGIL_LIBRARY_TYPE}
    $<TARGET_OBJECTS:vigil_core>
    ${VIGIL_STDLIB_ALWAYS_SOURCES}
    ${VIGIL_STDLIB_OPTIONAL_SOURCES}
    ${VIGIL_PLUGIN_SOURCES}
    src/url.c
    src/yaml.c
)

# ── Platform abstraction layer ───────────────────────────────────────

if(WIN32)
    target_sources(vigil PRIVATE src/platform/platform_win32.c src/platform/line_editor.c)
    target_link_libraries(vigil PRIVATE crypt32)
elseif(EMSCRIPTEN)
    target_sources(vigil PRIVATE src/platform/platform_stub.c src/platform/line_editor.c)
elseif(UNIX)
    target_sources(vigil PRIVATE src/platform/platform_posix.c src/platform/line_editor.c)
    if(APPLE)
        target_link_libraries(vigil PRIVATE "-framework Security" "-framework CoreFoundation")
    endif()
else()
    target_sources(vigil PRIVATE src/platform/platform_stub.c src/platform/line_editor.c)
endif()

# ── Library properties ───────────────────────────────────────────────

target_include_directories(vigil
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(vigil PUBLIC c_std_11)
target_compile_options(vigil PRIVATE ${VIGIL_WARNING_FLAGS})

if(VIGIL_PLUGIN_LIBRARIES)
    target_link_libraries(vigil PRIVATE ${VIGIL_PLUGIN_LIBRARIES})
endif()

if(VIGIL_MINIAUDIO_INCLUDE)
    target_include_directories(vigil PRIVATE ${VIGIL_MINIAUDIO_INCLUDE})
endif()

target_compile_definitions(vigil PUBLIC VIGIL_VERSION="${PROJECT_VERSION}")
target_compile_definitions(vigil PUBLIC ${VIGIL_STDLIB_COMPILE_DEFINITIONS})
target_compile_definitions(vigil_core PRIVATE ${VIGIL_STDLIB_COMPILE_DEFINITIONS})

if(NOT EMSCRIPTEN)
    target_compile_definitions(vigil
        PUBLIC VIGIL_SHARED
        PRIVATE VIGIL_EXPORTS
    )
    target_compile_definitions(vigil_core PRIVATE VIGIL_EXPORTS VIGIL_SHARED)
endif()

# ── System library detection ─────────────────────────────────────────

include(CheckLibraryExists)

check_library_exists(m floor "" VIGIL_NEEDS_LIBM)
if(VIGIL_NEEDS_LIBM)
    target_link_libraries(vigil PRIVATE m)
endif()

check_library_exists(dl dlopen "" VIGIL_NEEDS_LIBDL)
if(VIGIL_NEEDS_LIBDL)
    target_link_libraries(vigil PRIVATE dl)
endif()

# ── Vendored dependencies ────────────────────────────────────────────

if(VIGIL_USE_LIBFFI AND VIGIL_STDLIB_FFI AND VIGIL_HAS_DESKTOP_PLATFORM)
    add_subdirectory(deps/libffi)
    target_link_libraries(vigil PRIVATE ffi_static)
    target_compile_definitions(vigil PRIVATE VIGIL_HAS_LIBFFI)
    target_compile_definitions(vigil_core PRIVATE VIGIL_HAS_LIBFFI)
endif()

add_subdirectory(deps/miniz)
target_link_libraries(vigil PRIVATE miniz)

add_subdirectory(deps/lz4)
target_link_libraries(vigil PRIVATE lz4)

add_subdirectory(deps/crypto)
target_link_libraries(vigil PRIVATE vigil_crypto)

if(VIGIL_ENABLE_BEARSSL_TLS AND VIGIL_STDLIB_HTTP AND VIGIL_HAS_DESKTOP_PLATFORM)
    add_subdirectory(deps/bearssl)
    target_link_libraries(vigil PRIVATE bearssl_static)
    target_compile_definitions(vigil PRIVATE VIGIL_ENABLE_BEARSSL_TLS)
endif()

# ── MSVC multi-config output directory fix ───────────────────────────

if(MSVC)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    foreach(CFG RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} "${CMAKE_BINARY_DIR}/${CFG}")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG} "${CMAKE_BINARY_DIR}/${CFG}")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CFG} "${CMAKE_BINARY_DIR}/${CFG}")
    endforeach()
endif()
