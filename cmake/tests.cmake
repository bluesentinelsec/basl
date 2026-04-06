# ── Test targets ─────────────────────────────────────────────────────
#
# When adding a new unit test file, add it to the vigil_tests source
# list below in alphabetical order.
#
# When adding a new integration test, add a vigil_add_integration_test()
# call at the bottom of this file.

include(CTest)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
target_compile_definitions(vigil_core PRIVATE VIGIL_VERIFY_LOWERED_IR)

if(VIGIL_STDLIB_HTTP)
    target_compile_definitions(vigil PRIVATE VIGIL_HTTP_TESTING)
endif()

# ── Unit tests ───────────────────────────────────────────────────────

add_executable(vigil_tests
    tests/test_main.c
    tests/array_test.c
    tests/backend_test.c
    tests/binding_test.c
    tests/checker_test.c
    tests/chunk_test.c
    tests/cli_frontend_test.c
    tests/cli_lib_test.c
    tests/compiler_test.c
    tests/dap_test.c
    tests/debug_info_test.c
    tests/debugger_test.c
    tests/diagnostic_test.c
    tests/doc_registry_test.c
    tests/doc_test.c
    tests/editor_test.c
    tests/embed_api_test.c
    tests/ffi_test.c
    tests/image_test.c
    tests/font_test.c
    plugins/sdl/vigil_image.c
    plugins/sdl/vigil_font.c
    tests/fs_test.c
    tests/gc_test.c
    tests/gui_test.c
    tests/audio_test.c
    tests/sysquery_test.c
    tests/json_test.c
    tests/lexer_test.c
    tests/line_editor_internal_test.c
    tests/log_test.c
    tests/lsp_test.c
    tests/map_test.c
    tests/platform_test.c
    tests/plugin_test.c
    tests/runtime_test.c
    tests/semantic_test.c
    tests/source_test.c
    tests/status_test.c
    tests/stdlib_test.c
    tests/string_test.c
    tests/symbol_test.c
    tests/toml_test.c
    tests/token_test.c
    tests/type_test.c
    tests/unsafe_test.c
    tests/url_test.c
    tests/utf8_test.c
    tests/xml_test.c
    tests/value_test.c
    tests/vigil_new_test.c
    tests/vm_ops_collection_test.c
    tests/vm_ops_string_test.c
    tests/vm_test.c
    tests/yaml_test.c
    src/cli_frontend.c
)

target_compile_options(vigil_tests PRIVATE ${VIGIL_WARNING_FLAGS})

if(VIGIL_STDLIB_HTTP)
    target_sources(vigil_tests PRIVATE tests/http_test.c)
endif()

if(VIGIL_STDLIB_THREAD)
    target_sources(vigil_tests PRIVATE tests/thread_test.c)
endif()

# ── BearSSL test certificate ─────────────────────────────────────────

if(VIGIL_ENABLE_BEARSSL_TLS AND VIGIL_STDLIB_HTTP)
    target_include_directories(vigil_tests PRIVATE "${CMAKE_SOURCE_DIR}/deps/bearssl/inc")
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
                target_compile_definitions(vigil_tests PRIVATE VIGIL_TLS_TEST_CERT_AVAILABLE)
            endif()
        endif()
    endif()
endif()

# ── Test link and include setup ──────────────────────────────────────

target_link_libraries(vigil_tests PRIVATE vigil)

# vigil_image.c (stb_image) needs libm on Linux/Unix.
if(UNIX)
    target_link_libraries(vigil_tests PRIVATE m)
endif()

# SDL3 contains C++ code (hidapi); Android NDK requires explicit libc++ linkage.
if(ANDROID)
    target_link_libraries(vigil_tests PRIVATE c++)
endif()
target_include_directories(vigil_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_BINARY_DIR}/generated
)

if(VIGIL_HAS_DESKTOP_PLATFORM)
    target_compile_definitions(vigil_tests PRIVATE VIGIL_HAS_DESKTOP_PLATFORM)
endif()

if(VIGIL_USE_LIBFFI AND VIGIL_STDLIB_FFI AND VIGIL_HAS_FFI)
    target_link_libraries(vigil_tests PRIVATE ffi_static)
    target_compile_definitions(vigil_tests PRIVATE VIGIL_HAS_LIBFFI)
endif()

# ── FFI test shared library ──────────────────────────────────────────

if(VIGIL_HAS_FFI AND NOT EMSCRIPTEN)
    add_library(ffi_testlib SHARED tests/ffi_testlib.c)
    target_compile_options(ffi_testlib PRIVATE ${VIGIL_WARNING_FLAGS})
    set_target_properties(ffi_testlib PROPERTIES PREFIX "")
    target_compile_definitions(vigil_tests PRIVATE
        FFI_TESTLIB_PATH="$<TARGET_FILE:ffi_testlib>"
    )
    add_dependencies(vigil_tests ffi_testlib)
endif()

add_test(NAME vigil_tests COMMAND vigil_tests)

# ── Integration tests ────────────────────────────────────────────────

if(NOT VIGIL_HAS_DESKTOP_PLATFORM)
    return()
endif()

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

# Build the disabled-modules environment variable for stdlib availability tests.
set(VIGIL_DISABLED_STDLIB_MODULES)
foreach(_mod FFI FS HTTP NET READLINE THREAD TIME)
    if(NOT VIGIL_STDLIB_${_mod})
        string(TOLOWER ${_mod} _mod_lower)
        list(APPEND VIGIL_DISABLED_STDLIB_MODULES ${_mod_lower})
    endif()
endforeach()
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
if(VIGIL_PLUGIN_TILED)
    vigil_add_integration_test(VigilTiledTest test_tiled.py)
endif()
if(VIGIL_PLUGIN_GUI)
    vigil_add_integration_test(VigilGuiTest test_gui.py)
endif()
vigil_add_integration_test(VigilYamlTest test_yaml.py)
