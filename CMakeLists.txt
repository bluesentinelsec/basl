cmake_minimum_required(VERSION 3.21)

project(vigil VERSION 0.2.1 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VIGIL_BUILD_TESTS "Build VIGIL unit tests" ON)
option(VIGIL_USE_LIBFFI "Build with vendored libffi for full FFI support" ON)
option(VIGIL_ENABLE_COVERAGE "Build with coverage instrumentation" OFF)
option(VIGIL_ENABLE_BEARSSL_TLS "Enable BearSSL TLS for the fallback HTTPS client" OFF)

set(VIGIL_DESKTOP_OPTIONAL_STDLIB_DEFAULT ON)
if(EMSCRIPTEN
   OR CMAKE_SYSTEM_NAME STREQUAL "iOS"
   OR CMAKE_SYSTEM_NAME STREQUAL "Android"
   OR (NOT WIN32 AND NOT UNIX))
    set(VIGIL_DESKTOP_OPTIONAL_STDLIB_DEFAULT OFF)
endif()

option(VIGIL_STDLIB_FFI "Build ffi stdlib module" ${VIGIL_DESKTOP_OPTIONAL_STDLIB_DEFAULT})
option(VIGIL_STDLIB_FS "Build fs stdlib module" ${VIGIL_DESKTOP_OPTIONAL_STDLIB_DEFAULT})
option(VIGIL_STDLIB_HTTP "Build http stdlib module" ${VIGIL_DESKTOP_OPTIONAL_STDLIB_DEFAULT})
option(VIGIL_STDLIB_NET "Build net stdlib module" ${VIGIL_DESKTOP_OPTIONAL_STDLIB_DEFAULT})
option(VIGIL_STDLIB_READLINE "Build readline stdlib module" ${VIGIL_DESKTOP_OPTIONAL_STDLIB_DEFAULT})
option(VIGIL_STDLIB_THREAD "Build thread stdlib module" ${VIGIL_DESKTOP_OPTIONAL_STDLIB_DEFAULT})
option(VIGIL_STDLIB_TIME "Build time stdlib module" ${VIGIL_DESKTOP_OPTIONAL_STDLIB_DEFAULT})

if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

if(VIGIL_ENABLE_COVERAGE)
    if(MSVC)
        message(FATAL_ERROR "VIGIL_ENABLE_COVERAGE is not supported with MSVC")
    elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-O0 -g --coverage -fprofile-abs-path)
        add_link_options(--coverage)
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
        add_compile_options(-O0 -g --coverage)
        add_link_options(--coverage)
    else()
        message(FATAL_ERROR "VIGIL_ENABLE_COVERAGE requires GCC or Clang")
    endif()
endif()


if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(VIGIL_LIBRARY_TYPE STATIC)
elseif(NOT DEFINED VIGIL_LIBRARY_TYPE)
    set(VIGIL_LIBRARY_TYPE SHARED)
endif()

# ── Core interpreter library ─────────────────────────────────────────
# Pure C11, no platform-specific headers.  Must compile on any
# conforming C11 toolchain (GCC, Clang, MSVC, Emscripten).
# When adding stdlib modules that need POSIX/Win32 APIs, put them in
# a separate vigil_stdlib target that links against vigil_core.
add_library(vigil_core OBJECT
    src/allocator.c
    src/array.c
    src/binding.c
    src/checker.c
    src/chunk.c
    src/cli_lib.c
    src/compiler.c
    src/dap.c
    src/doc.c
    src/doc_registry.c
    src/embed.c
    src/easy.c
    src/editor.c
    src/ffi_callback.c
    src/fmt.c
    src/pkg.c
    src/package.c
    src/debug_info.c
    src/semantic.c
    src/lsp.c
    src/debugger.c
    src/compiler_program.c
    src/compiler_backend.c
    src/compiler_builtins.c
    src/compiler_bindings.c
    src/compiler_semantics.c
    src/compiler_strings.c
    src/compiler_typeparsing.c
    src/compiler_declarations.c
    src/compiler_types.c
    src/diagnostic.c
    src/gc.c
    src/json.c
    src/jsonrpc.c
    src/toml.c
    src/lexer.c
    src/log.c
    src/map.c
    src/native_module.c
    src/runtime.c
    src/source.c
    src/string.c
    src/status.c
    src/symbol.c
    src/token.c
    src/type.c
    src/value.c
    src/vm.c
    src/regvm.c
    src/vm_ops_collection.c
    src/vm_ops_convert.c
    src/vm_ops_string.c
)

target_include_directories(vigil_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
)

target_compile_features(vigil_core PUBLIC c_std_11)

# Objects may end up in a shared library, so they need PIC on Linux.
set_target_properties(vigil_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ── Public shared/static library ────────────────────────────────────
# Today this is the core plus stdlib modules.  Stdlib modules may use
# platform-specific headers; the core (vigil_core) stays pure C11.
set(VIGIL_STDLIB_ALWAYS_SOURCES
    src/stdlib/args.c
    src/stdlib/atomic.c
    src/stdlib/compress.c
    src/stdlib/crypto.c
    src/stdlib/csv.c
    src/stdlib/fmt.c
    src/stdlib/log.c
    src/stdlib/math.c
    src/stdlib/parse.c
    src/stdlib/random.c
    src/stdlib/regex.c
    src/stdlib/regex_engine.c
    src/stdlib/test.c
    src/stdlib/unsafe.c
    src/stdlib/url.c
    src/stdlib/yaml.c
)

set(VIGIL_STDLIB_OPTIONAL_SOURCES)
set(VIGIL_STDLIB_COMPILE_DEFINITIONS)

if(VIGIL_STDLIB_FFI)
    list(APPEND VIGIL_STDLIB_OPTIONAL_SOURCES src/stdlib/ffi.c)
    list(APPEND VIGIL_STDLIB_COMPILE_DEFINITIONS VIGIL_HAS_STDLIB_FFI)
endif()

if(VIGIL_STDLIB_FS)
    list(APPEND VIGIL_STDLIB_OPTIONAL_SOURCES src/stdlib/fs.c)
    list(APPEND VIGIL_STDLIB_COMPILE_DEFINITIONS VIGIL_HAS_STDLIB_FS)
endif()

if(VIGIL_STDLIB_HTTP)
    list(APPEND VIGIL_STDLIB_OPTIONAL_SOURCES src/stdlib/http.c)
    list(APPEND VIGIL_STDLIB_COMPILE_DEFINITIONS VIGIL_HAS_STDLIB_HTTP)
endif()

if(VIGIL_STDLIB_NET)
    list(APPEND VIGIL_STDLIB_OPTIONAL_SOURCES src/stdlib/net.c)
    list(APPEND VIGIL_STDLIB_COMPILE_DEFINITIONS VIGIL_HAS_STDLIB_NET)
endif()

if(VIGIL_STDLIB_READLINE)
    list(APPEND VIGIL_STDLIB_OPTIONAL_SOURCES src/stdlib/readline.c)
    list(APPEND VIGIL_STDLIB_COMPILE_DEFINITIONS VIGIL_HAS_STDLIB_READLINE)
endif()

if(VIGIL_STDLIB_THREAD)
    list(APPEND VIGIL_STDLIB_OPTIONAL_SOURCES src/stdlib/thread.c)
    list(APPEND VIGIL_STDLIB_COMPILE_DEFINITIONS VIGIL_HAS_STDLIB_THREAD)
endif()

if(VIGIL_STDLIB_TIME)
    list(APPEND VIGIL_STDLIB_OPTIONAL_SOURCES src/stdlib/time.c)
    list(APPEND VIGIL_STDLIB_COMPILE_DEFINITIONS VIGIL_HAS_STDLIB_TIME)
endif()

# ── Plugin support ─────────────────────────────────────────────────
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
            # Resolve source paths relative to the plugin directory.
            foreach(_src ${_PLUG_SOURCES})
                list(APPEND VIGIL_PLUGIN_SOURCES "${_plug_dir}/${_src}")
            endforeach()

            string(APPEND VIGIL_PLUGIN_EXTERN_DECLS
                "extern VIGIL_API const vigil_native_module_t vigil_plugin_${_PLUG_NAME};\n")
            string(APPEND VIGIL_PLUGIN_TABLE_FILL
                "    table[i].name = \"${_PLUG_NAME}\"; table[i].name_length = ${_plug_name_len}U; table[i].module = &vigil_plugin_${_PLUG_NAME}; i++;\n")

            # Track libraries to link later.
            foreach(_lib ${_PLUG_LIBRARIES})
                list(APPEND VIGIL_PLUGIN_LIBRARIES ${_lib})
            endforeach()

            math(EXPR VIGIL_PLUGIN_COUNT "${VIGIL_PLUGIN_COUNT} + 1")
            message(STATUS "Plugin '${_PLUG_NAME}': enabled")
        endif()
    endmacro()

    # Scan plugins/*/plugin.cmake
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

add_library(vigil ${VIGIL_LIBRARY_TYPE}
    $<TARGET_OBJECTS:vigil_core>
    ${VIGIL_STDLIB_ALWAYS_SOURCES}
    ${VIGIL_STDLIB_OPTIONAL_SOURCES}
    ${VIGIL_PLUGIN_SOURCES}
    src/url.c
    src/yaml.c
)

# ── Platform abstraction layer ──────────────────────────────────────
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

target_include_directories(vigil
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(vigil PUBLIC c_std_11)

# Link plugin libraries (if any plugins declared LIBRARIES).
if(VIGIL_PLUGIN_LIBRARIES)
    target_link_libraries(vigil PRIVATE ${VIGIL_PLUGIN_LIBRARIES})
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

# Link libm where needed (Linux/glibc; MSVC/macOS/Emscripten have it built-in).
include(CheckLibraryExists)
check_library_exists(m floor "" VIGIL_NEEDS_LIBM)
if(VIGIL_NEEDS_LIBM)
    target_link_libraries(vigil PRIVATE m)
endif()

# Link libdl where needed (Linux; macOS has it in libSystem).
check_library_exists(dl dlopen "" VIGIL_NEEDS_LIBDL)
if(VIGIL_NEEDS_LIBDL)
    target_link_libraries(vigil PRIVATE dl)
endif()

# Vendored libffi for full FFI support.
# Disabled on platforms that lack dynamic linking or cross-compile.
if(VIGIL_USE_LIBFFI AND VIGIL_STDLIB_FFI AND NOT EMSCRIPTEN
   AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS"
   AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    add_subdirectory(deps/libffi)
    target_link_libraries(vigil PRIVATE ffi_static)
    target_compile_definitions(vigil PRIVATE VIGIL_HAS_LIBFFI)
    target_compile_definitions(vigil_core PRIVATE VIGIL_HAS_LIBFFI)
endif()

# Vendored miniz for compression support.
add_subdirectory(deps/miniz)
target_link_libraries(vigil PRIVATE miniz)

# Vendored lz4 for fast compression.
add_subdirectory(deps/lz4)
target_link_libraries(vigil PRIVATE lz4)

# Vendored crypto for cryptographic operations.
add_subdirectory(deps/crypto)
target_link_libraries(vigil PRIVATE vigil_crypto)

# Vendored BearSSL for TLS support in the fallback HTTP/S client.
# Disabled on platforms without desktop TCP stacks.
if(VIGIL_ENABLE_BEARSSL_TLS AND VIGIL_STDLIB_HTTP
   AND NOT EMSCRIPTEN
   AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS"
   AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    add_subdirectory(deps/bearssl)
    target_link_libraries(vigil PRIVATE bearssl_static)
    target_compile_definitions(vigil PRIVATE VIGIL_ENABLE_BEARSSL_TLS)
endif()

# On MSVC multi-config generators, an OBJECT-library-backed shared
# library may place its import library (.lib) in a different directory
# than dependents expect.  Pin all output directories so the DLL,
# import lib, and executables land together.
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

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
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
endif()

if(VIGIL_BUILD_TESTS)
    include(CTest)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    # Expose http.c internals for unit testing.
    if(VIGIL_STDLIB_HTTP)
        target_compile_definitions(vigil PRIVATE VIGIL_HTTP_TESTING)
    endif()

    add_executable(vigil_tests
        tests/test_main.c
        tests/array_test.c
        tests/vigil_new_test.c
        tests/binding_test.c
        tests/checker_test.c
        tests/chunk_test.c
        tests/cli_frontend_test.c
        tests/cli_lib_test.c
        tests/compiler_test.c
        tests/dap_test.c
        tests/doc_test.c
        tests/embed_api_test.c
        tests/editor_test.c
        tests/debug_info_test.c
        tests/debugger_test.c
        tests/diagnostic_test.c
        tests/ffi_test.c
        tests/fs_test.c
        tests/gc_test.c
        tests/unsafe_test.c
        tests/json_test.c
        tests/lexer_test.c
        tests/line_editor_internal_test.c
        tests/log_test.c
        tests/toml_test.c
        tests/url_test.c
        tests/yaml_test.c
        tests/map_test.c
        tests/platform_test.c
        tests/runtime_test.c
        tests/semantic_test.c
        tests/lsp_test.c
        tests/doc_registry_test.c
        tests/source_test.c
        tests/stdlib_test.c
        tests/string_test.c
        tests/status_test.c
        tests/symbol_test.c
        tests/token_test.c
        tests/type_test.c
        tests/value_test.c
        tests/vm_ops_string_test.c
        tests/vm_test.c
        tests/plugin_test.c
        src/cli_frontend.c
    )

    if(VIGIL_STDLIB_HTTP)
        target_sources(vigil_tests PRIVATE tests/http_test.c)
    endif()

    if(VIGIL_STDLIB_THREAD)
        target_sources(vigil_tests PRIVATE tests/thread_test.c)
    endif()

    # BearSSL: expose headers to tests and generate a loopback test certificate.
    if(VIGIL_ENABLE_BEARSSL_TLS AND VIGIL_STDLIB_HTTP)
        target_link_libraries(vigil_tests PRIVATE bearssl_static)
        target_compile_definitions(vigil_tests PRIVATE VIGIL_ENABLE_BEARSSL_TLS)

        find_program(VIGIL_OPENSSL_EXEC openssl)
        if(VIGIL_OPENSSL_EXEC)
            set(_tls_dir "${CMAKE_BINARY_DIR}/test_tls")
            file(MAKE_DIRECTORY "${_tls_dir}")
            execute_process(
                COMMAND "${VIGIL_OPENSSL_EXEC}" req -x509 -newkey ec
                        -pkeyopt ec_paramgen_curve:P-256
                        -days 36500 -nodes
                        -keyout "${_tls_dir}/key.pem"
                        -out    "${_tls_dir}/cert.pem"
                        -subj "/CN=vigil-test"
                RESULT_VARIABLE _ssl_ret
                ERROR_QUIET OUTPUT_QUIET
            )
            if(_ssl_ret EQUAL 0)
                execute_process(
                    COMMAND "${Python3_EXECUTABLE}"
                            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cert_to_header.py"
                            "${_tls_dir}/cert.pem"
                            "${_tls_dir}/key.pem"
                            "${_tls_dir}/http_tls_test_cert.h"
                    RESULT_VARIABLE _py_ret
                )
                if(_py_ret EQUAL 0)
                    target_include_directories(vigil_tests PRIVATE "${_tls_dir}")
                    target_compile_definitions(vigil_tests PRIVATE
                        VIGIL_TLS_TEST_CERT_AVAILABLE)
                endif()
            endif()
        endif()
    endif()

    target_link_libraries(vigil_tests PRIVATE vigil)
    target_include_directories(vigil_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_BINARY_DIR}/generated
    )

    # Vendored libffi for tests.
    if(VIGIL_USE_LIBFFI AND VIGIL_STDLIB_FFI AND NOT EMSCRIPTEN
       AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS"
       AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
        target_link_libraries(vigil_tests PRIVATE ffi_static)
        target_compile_definitions(vigil_tests PRIVATE VIGIL_HAS_LIBFFI)
    endif()

    # Small shared library for FFI dlopen tests.
    if(NOT EMSCRIPTEN)
        add_library(ffi_testlib SHARED tests/ffi_testlib.c)
        set_target_properties(ffi_testlib PROPERTIES
            PREFIX ""   # no "lib" prefix — predictable name
        )
        target_compile_definitions(vigil_tests PRIVATE
            FFI_TESTLIB_PATH="$<TARGET_FILE:ffi_testlib>"
        )
        add_dependencies(vigil_tests ffi_testlib)
    endif()

    add_test(NAME vigil_tests COMMAND vigil_tests)

    if (NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
        function(vigil_add_integration_test test_name script_name)
            add_test(
                NAME ${test_name}
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/integration_tests/${script_name}
            )
            set_tests_properties(
                ${test_name}
                PROPERTIES ENVIRONMENT "VIGIL_BIN=$<TARGET_FILE:vigil_cli>"
            )
        endfunction()

        set(VIGIL_DISABLED_STDLIB_MODULES)
        if(NOT VIGIL_STDLIB_FFI)
            list(APPEND VIGIL_DISABLED_STDLIB_MODULES ffi)
        endif()
        if(NOT VIGIL_STDLIB_FS)
            list(APPEND VIGIL_DISABLED_STDLIB_MODULES fs)
        endif()
        if(NOT VIGIL_STDLIB_HTTP)
            list(APPEND VIGIL_DISABLED_STDLIB_MODULES http)
        endif()
        if(NOT VIGIL_STDLIB_NET)
            list(APPEND VIGIL_DISABLED_STDLIB_MODULES net)
        endif()
        if(NOT VIGIL_STDLIB_READLINE)
            list(APPEND VIGIL_DISABLED_STDLIB_MODULES readline)
        endif()
        if(NOT VIGIL_STDLIB_THREAD)
            list(APPEND VIGIL_DISABLED_STDLIB_MODULES thread)
        endif()
        if(NOT VIGIL_STDLIB_TIME)
            list(APPEND VIGIL_DISABLED_STDLIB_MODULES time)
        endif()
        list(JOIN VIGIL_DISABLED_STDLIB_MODULES "," VIGIL_DISABLED_STDLIB_ENV)

        vigil_add_integration_test(VigilArgsTest test_args.py)
        vigil_add_integration_test(VigilAtomicTest test_atomic.py)
        vigil_add_integration_test(VigilCheckTest test_check.py)
        vigil_add_integration_test(VigilCompressTest test_compress.py)
        vigil_add_integration_test(VigilCryptoTest test_crypto.py)
        vigil_add_integration_test(VigilCsvTest test_csv.py)
        vigil_add_integration_test(VigilDebugDAPTest test_debug_dap.py)
        vigil_add_integration_test(VigilDebugInteractiveTest test_debug.py)
        vigil_add_integration_test(VigilDocTest test_doc.py)
        vigil_add_integration_test(VigilEmbedTest test_embed.py)
        vigil_add_integration_test(VigilFmtTest test_fmt.py)
        if(VIGIL_STDLIB_FS)
            vigil_add_integration_test(VigilFsTest test_fs.py)
        endif()
        vigil_add_integration_test(VigilLogTest test_log.py)
        vigil_add_integration_test(VigilMathTest test_math.py)
        if(VIGIL_STDLIB_NET AND VIGIL_STDLIB_TIME)
            vigil_add_integration_test(VigilNetTest test_net.py)
        endif()
        vigil_add_integration_test(VigilNewIntegrationTest test_new.py)
        vigil_add_integration_test(VigilParseTest test_parse.py)
        vigil_add_integration_test(VigilPackageTest test_package.py)
        vigil_add_integration_test(VigilPkgTest test_pkg.py)
        vigil_add_integration_test(VigilRandomTest test_random.py)
        vigil_add_integration_test(VigilRegexTest test_regex.py)
        vigil_add_integration_test(VigilRegressionTest test_regression.py)
        vigil_add_integration_test(VigilReplTest test_repl.py)
        vigil_add_integration_test(VigilStringMethodsTest test_string_methods.py)
        vigil_add_integration_test(VigilStdlibAvailabilityTest test_stdlib_availability.py)
        set_tests_properties(
            VigilStdlibAvailabilityTest
            PROPERTIES ENVIRONMENT
                "VIGIL_BIN=$<TARGET_FILE:vigil_cli>;VIGIL_DISABLED_STDLIB=${VIGIL_DISABLED_STDLIB_ENV}"
        )
        vigil_add_integration_test(VigilSyntaxIntegrationTest test_syntax_integration.py)
        vigil_add_integration_test(VigilTestTest test_test.py)
        if(VIGIL_STDLIB_THREAD AND VIGIL_STDLIB_TIME)
            vigil_add_integration_test(VigilThreadTest test_thread.py)
        endif()
        if(VIGIL_STDLIB_TIME)
            vigil_add_integration_test(VigilTimeTest test_time.py)
        endif()
        vigil_add_integration_test(VigilUrlTest test_url.py)
        vigil_add_integration_test(VigilYamlTest test_yaml.py)
    endif()
endif()
