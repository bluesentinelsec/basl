# ── Core interpreter library ─────────────────────────────────────────
# Pure C11, no platform-specific headers.  Must compile on any
# conforming C11 toolchain (GCC, Clang, MSVC, Emscripten).
#
# When adding a new core source file, add it to this list in
# alphabetical order within its section.

add_library(vigil_core OBJECT
    # ── Allocator and data structures ──
    src/allocator.c
    src/array.c
    src/map.c
    src/string.c
    src/utf8.c
    src/xml.c

    # ── Value representation and runtime ──
    src/binding.c
    src/gc.c
    src/native_module.c
    src/runtime.c
    src/source.c
    src/status.c
    src/symbol.c
    src/token.c
    src/type.c
    src/value.c

    # ── Lexer and compiler ──
    src/checker.c
    src/compiler.c
    src/compiler_backend.c
    src/compiler_bindings.c
    src/compiler_builtins.c
    src/compiler_declarations.c
    src/compiler_program.c
    src/compiler_semantics.c
    src/compiler_strings.c
    src/compiler_typeparsing.c
    src/compiler_types.c
    src/lexer.c
    src/semantic.c

    # ── Bytecode and VM ──
    src/chunk.c
    src/vm.c
    src/regvm.c
    src/vm_ops_collection.c
    src/vm_ops_convert.c
    src/vm_ops_string.c

    # ── Diagnostics and formatting ──
    src/diagnostic.c
    src/fmt.c
    src/log.c

    # ── Serialization ──
    src/json.c
    src/jsonrpc.c
    src/toml.c

    # ── Tooling ──
    src/builtins.c
    src/cli_lib.c
    src/coverage.c
    src/dap.c
    src/debug_info.c
    src/debugger.c
    src/doc.c
    src/doc_registry.c
    src/easy.c
    src/editor.c
    src/embed.c
    src/ffi_callback.c
    src/lsp.c
    src/package.c
    src/pkg.c
)

target_include_directories(vigil_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
)

target_compile_features(vigil_core PUBLIC c_std_11)
target_compile_options(vigil_core PRIVATE ${VIGIL_WARNING_FLAGS})

# Objects may end up in a shared library, so they need PIC on Linux.
set_target_properties(vigil_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
