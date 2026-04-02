#define VIGIL_TEST_IMPLEMENTATION
#include "vigil_test.h"

extern void register_stdlib_regex_tests(void);
extern void register_stdlib_parse_tests(void);
extern void register_stdlib_csv_tests(void);
extern void register_stdlib_json_tests(void);
extern void register_array_tests(void);
extern void register_vigil_new_tests(void);
extern void register_binding_tests(void);
extern void register_backend_tests(void);
extern void register_checker_tests(void);
extern void register_chunk_tests(void);
extern void register_cli_frontend_tests(void);
extern void register_cli_lib_tests(void);
extern void register_compiler_tests(void);
extern void register_dap_tests(void);
extern void register_debug_info_tests(void);
extern void register_debugger_tests(void);
extern void register_diagnostic_tests(void);
extern void register_doc_tests(void);
extern void register_ffi_tests(void);
extern void register_fs_tests(void);
extern void register_gc_tests(void);
#ifdef VIGIL_HAS_STDLIB_HTTP
extern void register_http_tests(void);
#endif
#ifdef VIGIL_HAS_STDLIB_THREAD
extern void register_thread_tests(void);
#endif
extern void register_unsafe_tests(void);
extern void register_json_tests(void);
extern void register_lexer_tests(void);
extern void register_line_editor_internal_tests(void);
extern void register_log_tests(void);
extern void register_map_tests(void);
extern void register_platform_tests(void);
extern void register_runtime_tests(void);
extern void register_source_tests(void);
extern void register_status_tests(void);
extern void register_stdlib_tests(void);
extern void register_string_tests(void);
extern void register_symbol_tests(void);
extern void register_token_tests(void);
extern void register_toml_tests(void);
extern void register_url_tests(void);
extern void register_utf8_tests(void);
extern void register_yaml_tests(void);
extern void register_semantic_tests(void);
extern void register_lsp_tests(void);
extern void register_doc_registry_tests(void);
extern void register_type_tests(void);
extern void register_value_tests(void);
extern void register_vm_ops_collection_tests(void);
extern void register_vm_ops_string_tests(void);
extern void register_vm_tests(void);
extern void register_plugin_tests(void);
extern void register_embed_api_tests(void);
extern void register_editor_tests(void);

int main(void)
{
    register_stdlib_regex_tests();
    register_stdlib_parse_tests();
    register_stdlib_csv_tests();
    register_stdlib_json_tests();
    register_array_tests();
    register_vigil_new_tests();
    register_binding_tests();
    register_backend_tests();
    register_checker_tests();
    register_chunk_tests();
    register_cli_frontend_tests();
    register_cli_lib_tests();
    register_compiler_tests();
    register_dap_tests();
    register_debug_info_tests();
    register_debugger_tests();
    register_diagnostic_tests();
    register_doc_tests();
    register_ffi_tests();
    register_fs_tests();
    register_gc_tests();
#ifdef VIGIL_HAS_STDLIB_HTTP
    register_http_tests();
#endif
#ifdef VIGIL_HAS_STDLIB_THREAD
    register_thread_tests();
#endif
    register_unsafe_tests();
    register_json_tests();
    register_lexer_tests();
    register_line_editor_internal_tests();
    register_log_tests();
    register_map_tests();
    register_platform_tests();
    register_runtime_tests();
    register_source_tests();
    register_status_tests();
    register_stdlib_tests();
    register_string_tests();
    register_symbol_tests();
    register_token_tests();
    register_toml_tests();
    register_url_tests();
    register_utf8_tests();
    register_yaml_tests();
    register_semantic_tests();
    register_lsp_tests();
    register_doc_registry_tests();
    register_type_tests();
    register_value_tests();
    register_vm_ops_collection_tests();
    register_vm_ops_string_tests();
    register_vm_tests();
    register_plugin_tests();
    register_embed_api_tests();
    register_editor_tests();
    return vigil_test_run_all_();
}
