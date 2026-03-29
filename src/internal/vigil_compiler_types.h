#ifndef VIGIL_COMPILER_TYPES_H
#define VIGIL_COMPILER_TYPES_H

#include "vigil/chunk.h"
#include "vigil/diagnostic.h"
#include "vigil/native_module.h"
#include "vigil/source.h"
#include "vigil/token.h"
#include "vigil/value.h"
#include "vigil_binding.h"

typedef vigil_binding_type_t vigil_parser_type_t;
typedef vigil_binding_function_t vigil_function_decl_t;
typedef vigil_binding_function_param_t vigil_function_param_t;

typedef struct vigil_class_field
{
    const char *name;
    size_t name_length;
    vigil_source_span_t name_span;
    int is_public;
    vigil_parser_type_t type;
} vigil_class_field_t;

typedef struct vigil_class_method
{
    const char *name;
    size_t name_length;
    vigil_source_span_t name_span;
    int is_public;
    size_t function_index;
} vigil_class_method_t;

typedef struct vigil_interface_method
{
    const char *name;
    size_t name_length;
    vigil_source_span_t name_span;
    vigil_parser_type_t return_type;
    vigil_parser_type_t *return_types;
    size_t return_count;
    size_t return_capacity;
    vigil_parser_type_t *param_types;
    size_t param_count;
    size_t param_capacity;
} vigil_interface_method_t;

typedef struct vigil_class_interface_impl
{
    size_t interface_index;
    size_t *function_indices;
    size_t function_count;
} vigil_class_interface_impl_t;

typedef struct vigil_class_decl
{
    vigil_source_id_t source_id;
    const char *name;
    size_t name_length;
    vigil_source_span_t name_span;
    int is_public;
    vigil_class_field_t *fields;
    size_t field_count;
    size_t field_capacity;
    vigil_class_method_t *methods;
    size_t method_count;
    size_t method_capacity;
    size_t *implemented_interfaces;
    size_t implemented_interface_count;
    size_t implemented_interface_capacity;
    vigil_class_interface_impl_t *interface_impls;
    size_t interface_impl_count;
    size_t interface_impl_capacity;
    size_t constructor_function_index;
    const vigil_native_class_t *native_class; /* non-NULL = native */
} vigil_class_decl_t;

typedef struct vigil_interface_decl
{
    vigil_source_id_t source_id;
    const char *name;
    size_t name_length;
    vigil_source_span_t name_span;
    int is_public;
    vigil_interface_method_t *methods;
    size_t method_count;
    size_t method_capacity;
} vigil_interface_decl_t;

typedef struct vigil_enum_member
{
    const char *name;
    size_t name_length;
    vigil_source_span_t name_span;
    int64_t value;
} vigil_enum_member_t;

typedef struct vigil_enum_decl
{
    vigil_source_id_t source_id;
    const char *name;
    size_t name_length;
    vigil_source_span_t name_span;
    int is_public;
    vigil_enum_member_t *members;
    size_t member_count;
    size_t member_capacity;
} vigil_enum_decl_t;

typedef struct vigil_array_type_decl
{
    vigil_parser_type_t element_type;
} vigil_array_type_decl_t;

typedef struct vigil_map_type_decl
{
    vigil_parser_type_t key_type;
    vigil_parser_type_t value_type;
} vigil_map_type_decl_t;

typedef struct vigil_function_type_decl
{
    int is_any;
    vigil_parser_type_t return_type;
    vigil_parser_type_t *return_types;
    size_t return_count;
    size_t return_capacity;
    vigil_parser_type_t *param_types;
    size_t param_count;
    size_t param_capacity;
} vigil_function_type_decl_t;

typedef struct vigil_loop_jump
{
    size_t operand_offset;
    vigil_source_span_t span;
} vigil_loop_jump_t;

typedef struct vigil_loop_context
{
    size_t loop_start;
    size_t scope_depth;
    vigil_loop_jump_t *break_jumps;
    size_t break_count;
    size_t break_capacity;
} vigil_loop_context_t;

typedef enum vigil_module_compile_state
{
    VIGIL_MODULE_UNSEEN = 0,
    VIGIL_MODULE_PARSING = 1,
    VIGIL_MODULE_PARSED = 2
} vigil_module_compile_state_t;

typedef struct vigil_program_module
{
    vigil_source_id_t source_id;
    const vigil_source_file_t *source;
    vigil_token_list_t *tokens;
    vigil_module_compile_state_t state;
    struct vigil_module_import *imports;
    size_t import_count;
    size_t import_capacity;
} vigil_program_module_t;

typedef struct vigil_module_import
{
    char *owned_alias;
    const char *alias;
    size_t alias_length;
    vigil_source_span_t alias_span;
    vigil_source_id_t source_id;
} vigil_module_import_t;

typedef struct vigil_global_constant
{
    vigil_source_id_t source_id;
    const char *name;
    size_t name_length;
    vigil_source_span_t name_span;
    int is_public;
    vigil_parser_type_t type;
    vigil_value_t value;
} vigil_global_constant_t;

typedef struct vigil_global_variable
{
    vigil_source_id_t source_id;
    const char *name;
    size_t name_length;
    vigil_source_span_t name_span;
    int is_public;
    vigil_parser_type_t type;
    const vigil_source_file_t *source;
    const vigil_token_list_t *tokens;
    size_t initializer_start;
    size_t initializer_end;
} vigil_global_variable_t;

/* ── Extern function declarations ─────────────────────────────────── */

typedef struct vigil_extern_fn_decl
{
    const char *name;
    size_t name_length;
    vigil_source_span_t name_span;
    int is_public;
    vigil_source_id_t source_id;
    /* Library path (from the "from" clause). */
    char lib_path[256];
    /* C function name (defaults to Vigil name). */
    char c_name[256];
    /* libffi signature string, e.g. "f64(f64,i32)". */
    char sig[128];
    /* Vigil-level parameter and return types for type checking. */
    vigil_binding_function_param_t *params;
    size_t param_count;
    vigil_binding_type_t return_type;
    /* Index into the function table (the wrapper stub). */
    size_t function_index;
} vigil_extern_fn_decl_t;

typedef struct vigil_program_state
{
    const vigil_source_registry_t *registry;
    const vigil_source_file_t *source;
    const vigil_token_list_t *tokens;
    vigil_diagnostic_list_t *diagnostics;
    vigil_error_t *error;
    vigil_binding_function_table_t functions;
    vigil_program_module_t *modules;
    size_t module_count;
    size_t module_capacity;
    vigil_class_decl_t *classes;
    size_t class_count;
    size_t class_capacity;
    vigil_interface_decl_t *interfaces;
    size_t interface_count;
    size_t interface_capacity;
    vigil_enum_decl_t *enums;
    size_t enum_count;
    size_t enum_capacity;
    vigil_array_type_decl_t *array_types;
    size_t array_type_count;
    size_t array_type_capacity;
    vigil_map_type_decl_t *map_types;
    size_t map_type_count;
    size_t map_type_capacity;
    vigil_function_type_decl_t *function_types;
    size_t function_type_count;
    size_t function_type_capacity;
    vigil_global_constant_t *constants;
    size_t constant_count;
    size_t constant_capacity;
    vigil_global_variable_t *globals;
    size_t global_count;
    size_t global_capacity;
    vigil_extern_fn_decl_t *extern_fns;
    size_t extern_fn_count;
    size_t extern_fn_capacity;
    const struct vigil_native_registry *natives;
    int compile_mode;
    size_t repl_stmts_start;
    size_t repl_stmts_end;
    int repl_has_statements;
} vigil_program_state_t;

typedef struct vigil_parser_state
{
    const vigil_program_state_t *program;
    struct vigil_parser_state *parent;
    size_t current;
    size_t body_end;
    size_t function_index;
    vigil_parser_type_t expected_return_type;
    const vigil_parser_type_t *expected_return_types;
    size_t expected_return_count;
    vigil_chunk_t chunk;
    vigil_binding_scope_stack_t locals;
    vigil_loop_context_t *loops;
    size_t loop_count;
    size_t loop_capacity;
    int defer_mode;
    int defer_emitted;
} vigil_parser_state_t;

typedef struct vigil_expression_result
{
    vigil_parser_type_t type;
    const vigil_parser_type_t *types;
    size_t type_count;
    vigil_parser_type_t owned_types[3];
} vigil_expression_result_t;

typedef struct vigil_constant_result
{
    vigil_parser_type_t type;
    vigil_value_t value;
} vigil_constant_result_t;

typedef struct vigil_statement_result
{
    int guaranteed_return;
} vigil_statement_result_t;

typedef struct vigil_binding_target
{
    vigil_parser_type_t type;
    const vigil_token_t *name_token;
    int is_discard;
} vigil_binding_target_t;

typedef struct vigil_binding_target_list
{
    vigil_binding_target_t *items;
    size_t count;
    size_t capacity;
} vigil_binding_target_list_t;

/* --- compiler_types.c function declarations --- */

int vigil_parser_type_is_primitive(vigil_parser_type_t type);
int vigil_parser_type_is_class(vigil_parser_type_t type);
int vigil_parser_type_is_interface(vigil_parser_type_t type);
int vigil_parser_type_is_enum(vigil_parser_type_t type);
int vigil_parser_type_is_array(vigil_parser_type_t type);
int vigil_parser_type_is_map(vigil_parser_type_t type);
int vigil_parser_type_supports_map_key(vigil_parser_type_t type);
int vigil_parser_type_is_function(vigil_parser_type_t type);
int vigil_parser_type_is_void(vigil_parser_type_t type);
int vigil_parser_type_is_i32(vigil_parser_type_t type);
int vigil_parser_type_is_i64(vigil_parser_type_t type);
int vigil_parser_type_is_u8(vigil_parser_type_t type);
int vigil_parser_type_is_u32(vigil_parser_type_t type);
int vigil_parser_type_is_u64(vigil_parser_type_t type);
int vigil_parser_type_is_integer(vigil_parser_type_t type);
int vigil_parser_type_is_signed_integer(vigil_parser_type_t type);
int vigil_parser_type_is_unsigned_integer(vigil_parser_type_t type);
int vigil_parser_type_is_f64(vigil_parser_type_t type);
int vigil_parser_type_is_bool(vigil_parser_type_t type);
int vigil_parser_type_is_string(vigil_parser_type_t type);
int vigil_parser_type_is_err(vigil_parser_type_t type);
vigil_parser_type_t vigil_function_return_type_at(const vigil_function_decl_t *decl, size_t index);
const vigil_parser_type_t *vigil_function_return_types(const vigil_function_decl_t *decl);
vigil_parser_type_t vigil_interface_method_return_type_at(const vigil_interface_method_t *method, size_t index);
const vigil_parser_type_t *vigil_interface_method_return_types(const vigil_interface_method_t *method);
int vigil_parser_type_equal(vigil_parser_type_t left, vigil_parser_type_t right);
int vigil_program_type_is_assignable(const vigil_program_state_t *program, vigil_parser_type_t target_type,
                                     vigil_parser_type_t source_type);
int vigil_parser_type_supports_unary_operator(vigil_unary_operator_kind_t operator_kind,
                                              vigil_parser_type_t operand_type);
int vigil_parser_type_supports_binary_operator(vigil_binary_operator_kind_t operator_kind,
                                               vigil_parser_type_t left_type, vigil_parser_type_t right_type);

const vigil_function_type_decl_t *vigil_program_function_type_decl(const vigil_program_state_t *program,
                                                                   vigil_parser_type_t type);

int vigil_class_decl_implements_interface(const vigil_class_decl_t *decl, size_t interface_index);

/* compiler_program.c — grow/init/intern/free helpers */
void vigil_statement_result_clear(vigil_statement_result_t *result);
void vigil_statement_result_set_guaranteed_return(vigil_statement_result_t *result, int guaranteed_return);
void vigil_program_set_module_context(vigil_program_state_t *program, const vigil_source_file_t *source,
                                      const vigil_token_list_t *tokens);
vigil_status_t vigil_program_grow_modules(vigil_program_state_t *program, size_t minimum_capacity);
void vigil_program_import_default_alias(const char *path, size_t path_length, const char **out_alias,
                                        size_t *out_alias_length);
void vigil_class_decl_free(vigil_program_state_t *program, vigil_class_decl_t *decl);
vigil_status_t vigil_program_grow_classes(vigil_program_state_t *program, size_t minimum_capacity);
void vigil_interface_decl_free(vigil_program_state_t *program, vigil_interface_decl_t *decl);
vigil_status_t vigil_program_grow_interfaces(vigil_program_state_t *program, size_t minimum_capacity);
void vigil_enum_decl_free(vigil_program_state_t *program, vigil_enum_decl_t *decl);
vigil_status_t vigil_program_grow_enums(vigil_program_state_t *program, size_t minimum_capacity);
vigil_status_t vigil_program_grow_array_types(vigil_program_state_t *program, size_t minimum_capacity);
int vigil_program_find_array_type(const vigil_program_state_t *program, vigil_parser_type_t element_type,
                                  size_t *out_index);
vigil_status_t vigil_program_intern_array_type(vigil_program_state_t *program, vigil_parser_type_t element_type,
                                               vigil_parser_type_t *out_type);
vigil_status_t vigil_program_grow_map_types(vigil_program_state_t *program, size_t minimum_capacity);
int vigil_program_find_map_type(const vigil_program_state_t *program, vigil_parser_type_t key_type,
                                vigil_parser_type_t value_type, size_t *out_index);
vigil_status_t vigil_program_intern_map_type(vigil_program_state_t *program, vigil_parser_type_t key_type,
                                             vigil_parser_type_t value_type, vigil_parser_type_t *out_type);
void vigil_function_type_decl_free(vigil_program_state_t *program, vigil_function_type_decl_t *decl);
vigil_status_t vigil_function_type_decl_grow_params(vigil_program_state_t *program, vigil_function_type_decl_t *decl,
                                                    size_t minimum_capacity);
vigil_status_t vigil_function_type_decl_add_param(vigil_program_state_t *program, vigil_function_type_decl_t *decl,
                                                  vigil_parser_type_t type);
vigil_status_t vigil_function_type_decl_grow_returns(vigil_program_state_t *program, vigil_function_type_decl_t *decl,
                                                     size_t minimum_capacity);
vigil_status_t vigil_function_type_decl_add_return(vigil_program_state_t *program, vigil_function_type_decl_t *decl,
                                                   vigil_parser_type_t type);
vigil_status_t vigil_program_grow_function_types(vigil_program_state_t *program, size_t minimum_capacity);
int vigil_program_find_function_type(const vigil_program_state_t *program, const vigil_function_type_decl_t *needle,
                                     size_t *out_index);
vigil_status_t vigil_program_intern_function_type(vigil_program_state_t *program,
                                                  const vigil_function_type_decl_t *decl,
                                                  vigil_parser_type_t *out_type);
vigil_status_t vigil_program_intern_function_type_from_decl(vigil_program_state_t *program,
                                                            const vigil_function_decl_t *decl,
                                                            vigil_parser_type_t *out_type);
vigil_status_t vigil_class_decl_grow_fields(vigil_program_state_t *program, vigil_class_decl_t *decl,
                                            size_t minimum_capacity);
vigil_status_t vigil_class_decl_grow_methods(vigil_program_state_t *program, vigil_class_decl_t *decl,
                                             size_t minimum_capacity);
vigil_status_t vigil_class_decl_grow_implemented_interfaces(vigil_program_state_t *program, vigil_class_decl_t *decl,
                                                            size_t minimum_capacity);
vigil_status_t vigil_class_decl_grow_interface_impls(vigil_program_state_t *program, vigil_class_decl_t *decl,
                                                     size_t minimum_capacity);
vigil_status_t vigil_interface_decl_grow_methods(vigil_program_state_t *program, vigil_interface_decl_t *decl,
                                                 size_t minimum_capacity);
vigil_status_t vigil_program_grow_constants(vigil_program_state_t *program, size_t minimum_capacity);
vigil_status_t vigil_program_grow_globals(vigil_program_state_t *program, size_t minimum_capacity);
void vigil_program_trim_text_range(const char *text, size_t start, size_t end, size_t *out_start, size_t *out_end);
vigil_status_t vigil_program_grow_functions(vigil_program_state_t *program, size_t minimum_capacity);
void vigil_program_free(vigil_program_state_t *program);

/* compiler.c — shared helpers used by extracted modules */
void vigil_expression_result_clear(vigil_expression_result_t *result);
void vigil_expression_result_set_type(vigil_expression_result_t *result, vigil_parser_type_t type);
vigil_status_t vigil_parser_emit_f64_constant(vigil_parser_state_t *state, double value, vigil_source_span_t span);
vigil_status_t vigil_parser_emit_integer_constant(vigil_parser_state_t *state, vigil_parser_type_t target_type,
                                                  int64_t value, vigil_source_span_t span);
vigil_status_t vigil_parser_emit_ok_constant(vigil_parser_state_t *state, vigil_source_span_t span);
vigil_status_t vigil_parser_emit_opcode(vigil_parser_state_t *state, vigil_opcode_t opcode, vigil_source_span_t span);
vigil_status_t vigil_parser_emit_string_constant_text(vigil_parser_state_t *state, vigil_source_span_t span,
                                                      const char *text, size_t length);
vigil_status_t vigil_parser_expect(vigil_parser_state_t *state, vigil_token_kind_t kind, const char *message,
                                   const vigil_token_t **out_token);
const char *vigil_parser_token_text(const vigil_parser_state_t *state, const vigil_token_t *token, size_t *out_length);
int vigil_program_names_equal(const char *left, size_t left_length, const char *right, size_t right_length);
void vigil_expression_result_set_pair(vigil_expression_result_t *result, vigil_parser_type_t first_type,
                                      vigil_parser_type_t second_type);
void vigil_expression_result_set_triple(vigil_expression_result_t *result, vigil_parser_type_t first_type,
                                        vigil_parser_type_t second_type, vigil_parser_type_t third_type);
vigil_parser_type_t vigil_expression_result_type_at(const vigil_expression_result_t *result, size_t index);
vigil_status_t vigil_parser_parse_expression(vigil_parser_state_t *state, vigil_expression_result_t *out_result);
vigil_status_t vigil_parser_parse_expression_with_expected_type(vigil_parser_state_t *state,
                                                                vigil_parser_type_t expected_type,
                                                                vigil_expression_result_t *out_result);
vigil_status_t vigil_parser_report(vigil_parser_state_t *state, vigil_source_span_t span, const char *message);
vigil_status_t vigil_parser_require_scalar_expression(vigil_parser_state_t *state, vigil_source_span_t span,
                                                      const vigil_expression_result_t *result, const char *message);
vigil_status_t vigil_parser_require_type(vigil_parser_state_t *state, vigil_source_span_t span,
                                         vigil_parser_type_t actual_type, vigil_parser_type_t expected_type,
                                         const char *message);
vigil_parser_type_t vigil_program_array_type_element(const vigil_program_state_t *program,
                                                     vigil_parser_type_t array_type);
vigil_parser_type_t vigil_program_map_type_key(const vigil_program_state_t *program, vigil_parser_type_t map_type);
vigil_parser_type_t vigil_program_map_type_value(const vigil_program_state_t *program, vigil_parser_type_t map_type);
const vigil_token_t *vigil_parser_peek(const vigil_parser_state_t *state);
const vigil_token_t *vigil_parser_advance(vigil_parser_state_t *state);
const vigil_token_t *vigil_parser_previous(const vigil_parser_state_t *state);
int vigil_parser_check(const vigil_parser_state_t *state, vigil_token_kind_t kind);
int vigil_parser_match(vigil_parser_state_t *state, vigil_token_kind_t kind);
vigil_source_span_t vigil_parser_fallback_span(const vigil_parser_state_t *state);
vigil_status_t vigil_parser_emit_u32(vigil_parser_state_t *state, uint32_t value, vigil_source_span_t span);
vigil_status_t vigil_compile_report(const vigil_program_state_t *program, vigil_source_span_t span,
                                    const char *message);
const vigil_token_t *vigil_program_token_at(const vigil_program_state_t *program, size_t index);

/* compiler_typeparsing.c — type reference parsing */
extern int vigil_type_close_pending;
int vigil_program_consume_type_close(const vigil_program_state_t *program, size_t *cursor);
vigil_status_t vigil_program_parse_type_reference(const vigil_program_state_t *program, size_t *cursor,
                                                  const char *unsupported_message, vigil_parser_type_t *out_type);
vigil_status_t vigil_program_parse_primitive_type_reference(const vigil_program_state_t *program, size_t *cursor,
                                                            const char *unsupported_message,
                                                            vigil_parser_type_t *out_type);
int vigil_program_skip_type_reference_syntax(const vigil_program_state_t *program, size_t *cursor);
vigil_source_span_t vigil_program_eof_span(const vigil_program_state_t *program);
int vigil_program_find_class_in_source(const vigil_program_state_t *program, vigil_source_id_t source_id,
                                       const char *name, size_t name_length, size_t *out_index,
                                       const vigil_class_decl_t **out_class);
int vigil_program_find_enum_in_source(const vigil_program_state_t *program, vigil_source_id_t source_id,
                                      const char *name, size_t name_length, size_t *out_index,
                                      const vigil_enum_decl_t **out_decl);
int vigil_program_find_interface_in_source(const vigil_program_state_t *program, vigil_source_id_t source_id,
                                           const char *name, size_t name_length, size_t *out_index,
                                           const vigil_interface_decl_t **out_interface);
int vigil_program_is_class_public(const vigil_class_decl_t *decl);
int vigil_program_is_enum_public(const vigil_enum_decl_t *decl);
int vigil_program_is_interface_public(const vigil_interface_decl_t *decl);
int vigil_program_is_function_public(const vigil_function_decl_t *decl);
int vigil_program_is_constant_public(const vigil_global_constant_t *decl);
int vigil_program_is_global_public(const vigil_global_variable_t *decl);
vigil_status_t vigil_program_require_non_void_type(const vigil_program_state_t *program, vigil_source_span_t span,
                                                   vigil_parser_type_t type, const char *message);
int vigil_program_resolve_import_alias(const vigil_program_state_t *program, const char *alias, size_t alias_length,
                                       vigil_source_id_t *out_source_id);
const char *vigil_program_token_text(const vigil_program_state_t *program, const vigil_token_t *token,
                                     size_t *out_length);

/* compiler_declarations.c — declaration parsing */
void vigil_constant_result_clear(vigil_constant_result_t *result);
const vigil_token_t *vigil_program_cursor_advance(const vigil_program_state_t *program, size_t *cursor);
const vigil_token_t *vigil_program_cursor_peek(const vigil_program_state_t *program, size_t cursor);
int vigil_program_find_constant_in_source(const vigil_program_state_t *program, vigil_source_id_t source_id,
                                          const char *name, size_t name_length,
                                          const vigil_global_constant_t **out_constant);
int vigil_program_find_global_in_source(const vigil_program_state_t *program, vigil_source_id_t source_id,
                                        const char *name, size_t name_length, size_t *out_index,
                                        const vigil_global_variable_t **out_global);
int vigil_program_find_top_level_function_name_in_source(const vigil_program_state_t *program,
                                                         vigil_source_id_t source_id, const char *name_text,
                                                         size_t name_length, size_t *out_index,
                                                         const vigil_function_decl_t **out_decl);
int vigil_class_decl_find_field(const vigil_class_decl_t *decl, const char *name, size_t name_length, size_t *out_index,
                                const vigil_class_field_t **out_field);
int vigil_class_decl_find_method(const vigil_class_decl_t *decl, const char *name, size_t name_length,
                                 size_t *out_index, const vigil_class_method_t **out_method);
void vigil_constant_result_release(vigil_constant_result_t *result);
int vigil_enum_decl_find_member(const vigil_enum_decl_t *decl, const char *name, size_t name_length, size_t *out_index,
                                const vigil_enum_member_t **out_member);
int vigil_interface_decl_find_method(const vigil_interface_decl_t *decl, const char *name, size_t name_length,
                                     size_t *out_index, const vigil_interface_method_t **out_method);
void vigil_binding_target_list_init(vigil_binding_target_list_t *list);
void vigil_binding_target_list_free(vigil_program_state_t *program, vigil_binding_target_list_t *list);
vigil_status_t vigil_binding_target_list_append(vigil_program_state_t *program, vigil_binding_target_list_t *list,
                                                vigil_parser_type_t type, const vigil_token_t *name_token,
                                                int is_discard);
vigil_status_t vigil_parser_parse_binding_target_list(vigil_parser_state_t *state, const char *unsupported_type_message,
                                                      const char *non_void_message, const char *name_message,
                                                      vigil_binding_target_list_t *targets);
vigil_status_t vigil_parser_require_binding_initializer_shape(vigil_parser_state_t *state, vigil_source_span_t span,
                                                              const vigil_binding_target_list_t *targets,
                                                              const vigil_expression_result_t *initializer_result,
                                                              const char *count_message, const char *type_message);
vigil_status_t vigil_parser_parse_variable_declaration(vigil_parser_state_t *state,
                                                       vigil_statement_result_t *out_result);
vigil_status_t vigil_parser_parse_const_declaration(vigil_parser_state_t *state, vigil_statement_result_t *out_result);
vigil_status_t vigil_parser_declare_local_symbol(vigil_parser_state_t *state, const vigil_token_t *name_token,
                                                 vigil_parser_type_t type, int is_const, size_t *out_index);
vigil_status_t vigil_parser_bind_targets(vigil_parser_state_t *state, const vigil_binding_target_list_t *targets,
                                         int is_const, size_t *out_last_slot);
vigil_status_t vigil_program_add_param(vigil_program_state_t *program, vigil_function_decl_t *decl,
                                       vigil_parser_type_t type, const vigil_token_t *name_token);
vigil_status_t vigil_parser_resolve_local_symbol(vigil_parser_state_t *state, const vigil_token_t *name_token,
                                                 size_t *out_index, vigil_parser_type_t *out_type, int *out_is_capture,
                                                 size_t *out_capture_index, int *out_found);
vigil_status_t vigil_parser_lookup_field(vigil_parser_state_t *state, vigil_parser_type_t receiver_type,
                                         const vigil_token_t *field_token, size_t *out_index,
                                         const vigil_class_field_t **out_field);
vigil_status_t vigil_program_parse_constant_expression(vigil_program_state_t *program, size_t *cursor,
                                                       vigil_constant_result_t *out_result);
vigil_status_t vigil_program_parse_function_return_types(vigil_program_state_t *program, size_t *cursor,
                                                         const char *unsupported_message, vigil_function_decl_t *decl);
int vigil_program_parse_optional_pub(const vigil_program_state_t *program, size_t *cursor);
vigil_status_t vigil_program_parse_global_variable_declaration(vigil_program_state_t *program, size_t *cursor,
                                                               int is_public);
vigil_status_t vigil_program_parse_constant_declaration(vigil_program_state_t *program, size_t *cursor, int is_public);
vigil_status_t vigil_program_parse_enum_declaration(vigil_program_state_t *program, size_t *cursor, int is_public);
vigil_status_t vigil_program_parse_interface_declaration(vigil_program_state_t *program, size_t *cursor, int is_public);
vigil_status_t vigil_program_parse_class_declaration(vigil_program_state_t *program, size_t *cursor, int is_public);

/* compiler_strings.c — string/f-string parsing */
vigil_status_t vigil_parser_parse_fstring_literal(vigil_parser_state_t *state, const vigil_token_t *token,
                                                  vigil_expression_result_t *out_result);

/* compiler_builtins.c — built-in method dispatch */
vigil_status_t vigil_parser_emit_default_value(vigil_parser_state_t *state, vigil_parser_type_t type,
                                               vigil_source_span_t span);
vigil_status_t vigil_parser_parse_string_method_call(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                                     vigil_expression_result_t *out_result);
vigil_status_t vigil_parser_parse_array_method_call(vigil_parser_state_t *state, vigil_parser_type_t receiver_type,
                                                    const vigil_token_t *method_token,
                                                    vigil_expression_result_t *out_result);
vigil_status_t vigil_parser_parse_map_method_call(vigil_parser_state_t *state, vigil_parser_type_t receiver_type,
                                                  const vigil_token_t *method_token,
                                                  vigil_expression_result_t *out_result);

#endif
