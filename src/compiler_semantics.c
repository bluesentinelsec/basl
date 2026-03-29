#include <stdio.h>
#include <string.h>

#include "internal/vigil_compiler_internal.h"
#include "internal/vigil_compiler_semantics.h"
#include "internal/vigil_internal.h"
#include "vigil/stdlib.h"

static int vigil_semantic_is_assignment_operator(vigil_token_kind_t kind)
{
    return kind == VIGIL_TOKEN_ASSIGN || kind == VIGIL_TOKEN_PLUS_ASSIGN || kind == VIGIL_TOKEN_MINUS_ASSIGN ||
           kind == VIGIL_TOKEN_STAR_ASSIGN || kind == VIGIL_TOKEN_SLASH_ASSIGN || kind == VIGIL_TOKEN_PERCENT_ASSIGN ||
           kind == VIGIL_TOKEN_PLUS_PLUS || kind == VIGIL_TOKEN_MINUS_MINUS;
}

static vigil_status_t vigil_program_parse_import_target(const vigil_program_state_t *program,
                                                        const vigil_token_t *token, vigil_string_t *out_path)
{
    const char *text;
    size_t length;

    if (token == NULL || (token->kind != VIGIL_TOKEN_STRING_LITERAL && token->kind != VIGIL_TOKEN_RAW_STRING_LITERAL))
    {
        return vigil_compile_report(program, token == NULL ? vigil_program_eof_span(program) : token->span,
                                    "expected import path string literal");
    }

    text = vigil_program_token_text(program, token, &length);
    if (text == NULL || length < 2U)
        return vigil_compile_report(program, token->span, "import path is invalid");

    return vigil_program_resolve_import_path(program, text + 1U, length - 2U, out_path);
}

static vigil_status_t check_stdlib_availability(vigil_program_state_t *program, const vigil_token_t *target_token)
{
    char message[128];
    const char *raw_import = NULL;
    size_t raw_import_len = 0U;
    int written;

    if (program->natives == NULL || target_token == NULL)
        return VIGIL_STATUS_OK;

    raw_import = vigil_program_token_text(program, target_token, &raw_import_len);
    if (raw_import != NULL && raw_import_len >= 2U)
    {
        raw_import += 1U;
        raw_import_len -= 2U;
    }
    else
    {
        raw_import = "";
        raw_import_len = 0U;
    }

    if (!vigil_stdlib_is_known_module(raw_import, raw_import_len))
        return VIGIL_STATUS_OK;

    written = snprintf(message, sizeof(message), "stdlib module '%.*s' is not available in this build",
                       (int)raw_import_len, raw_import);
    if (written < 0 || (size_t)written >= sizeof(message))
        return vigil_compile_report(program, target_token->span, "stdlib module is not available in this build");
    return vigil_compile_report(program, target_token->span, message);
}

static vigil_status_t register_native_import(vigil_program_state_t *program, size_t native_idx,
                                             vigil_source_id_t source_id)
{
    vigil_status_t status;

    status = vigil_program_register_native_function_types(program, program->natives->modules[native_idx]);
    if (status != VIGIL_STATUS_OK)
        return status;
    if (program->natives->modules[native_idx]->class_count > 0U)
        return vigil_program_register_native_classes(program, program->natives->modules[native_idx], source_id);
    return VIGIL_STATUS_OK;
}

static int resolve_native_import(vigil_program_state_t *program, const vigil_token_t *target_token, size_t *native_idx,
                                 vigil_source_id_t *source_id)
{
    const char *raw_import;
    size_t raw_import_len;

    if (program->natives == NULL)
        return 0;
    raw_import = vigil_program_token_text(program, target_token, &raw_import_len);
    if (raw_import != NULL && raw_import_len >= 2U)
    {
        raw_import += 1U;
        raw_import_len -= 2U;
    }
    if (raw_import != NULL &&
        vigil_native_registry_find_index(program->natives, raw_import, raw_import_len, native_idx))
    {
        *source_id = VIGIL_NATIVE_SOURCE_ID(*native_idx);
        return 1;
    }
    return 0;
}

static vigil_status_t parse_import_alias(vigil_program_state_t *program, size_t *cursor,
                                         const vigil_token_t **out_alias_token, const char **out_alias_text,
                                         size_t *out_alias_length)
{
    const vigil_token_t *token = vigil_program_token_at(program, *cursor);

    if (token == NULL || token->kind != VIGIL_TOKEN_AS)
        return VIGIL_STATUS_OK;
    *cursor += 1U;
    *out_alias_token = vigil_program_token_at(program, *cursor);
    if (*out_alias_token == NULL || (*out_alias_token)->kind != VIGIL_TOKEN_IDENTIFIER)
        return vigil_compile_report(program, token->span, "expected import alias name");
    *out_alias_text = vigil_program_token_text(program, *out_alias_token, out_alias_length);
    *cursor += 1U;
    return VIGIL_STATUS_OK;
}

static void resolve_import_alias_default(const vigil_program_state_t *program, int native_found, size_t native_idx,
                                         const vigil_string_t *import_path, const char **alias_text,
                                         size_t *alias_length)
{
    if (*alias_text != NULL)
        return;
    if (native_found)
    {
        *alias_text = program->natives->modules[native_idx]->name;
        *alias_length = program->natives->modules[native_idx]->name_length;
        return;
    }

    vigil_program_import_default_alias(vigil_string_c_str(import_path), vigil_string_length(import_path), alias_text,
                                       alias_length);
}

static vigil_status_t resolve_import_source(vigil_program_state_t *program, int native_found,
                                            const vigil_token_t *import_target_token, const vigil_token_t *semi_token,
                                            const vigil_string_t *import_path, vigil_source_id_t *imported_source_id)
{
    vigil_status_t status;

    if (!native_found && program->natives != NULL && import_target_token != NULL)
    {
        status = check_stdlib_availability(program, import_target_token);
        if (status != VIGIL_STATUS_OK)
            return status;
    }
    if (!native_found && !vigil_program_find_source_by_path(program, vigil_string_c_str(import_path),
                                                            vigil_string_length(import_path), imported_source_id))
    {
        return vigil_compile_report(program, import_target_token == NULL ? semi_token->span : import_target_token->span,
                                    "imported source is not registered");
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_semantic_synthesize_repl_main(vigil_program_state_t *program, vigil_source_id_t source_id)
{
    vigil_status_t status;
    vigil_binding_function_t *decl;
    vigil_binding_type_t ret_type;
    size_t mod_idx = 0U;
    const vigil_source_file_t *source = program->source;

    status = vigil_program_grow_functions(program, program->functions.count + 1U);
    if (status != VIGIL_STATUS_OK)
        return status;

    decl = &program->functions.functions[program->functions.count];
    vigil_binding_function_init(decl);
    decl->name = "main";
    decl->name_length = 4U;
    decl->name_span = vigil_program_eof_span(program);
    decl->is_public = 0;
    decl->source = source;
    if (vigil_program_module_find(program, source_id, &mod_idx))
        decl->tokens = program->modules[mod_idx].tokens;
    decl->body_start = program->repl_stmts_start;
    decl->body_end = program->repl_stmts_end;
    memset(&ret_type, 0, sizeof(ret_type));
    ret_type.kind = VIGIL_TYPE_I32;
    decl->return_type = ret_type;
    decl->return_count = 1U;
    program->functions.main_index = program->functions.count;
    program->functions.has_main = 1;
    program->repl_has_statements = (program->repl_stmts_start < program->repl_stmts_end) ? 1 : 0;
    program->functions.count += 1U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_semantic_validate_prepare_inputs(const vigil_source_registry_t *registry,
                                                             vigil_diagnostic_list_t *diagnostics,
                                                             vigil_program_state_t *out_program, vigil_error_t *error)
{
    if (registry == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "source registry must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (diagnostics == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "diagnostic list must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (out_program == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_program must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_semantic_init_program(vigil_program_state_t *program,
                                                  const vigil_source_registry_t *registry,
                                                  const vigil_source_file_t *source,
                                                  const vigil_native_registry_t *natives,
                                                  vigil_diagnostic_list_t *diagnostics, vigil_error_t *error,
                                                  vigil_compile_mode_t mode)
{
    memset(program, 0, sizeof(*program));
    program->registry = registry;
    program->diagnostics = diagnostics;
    program->error = error;
    program->natives = natives;
    program->compile_mode = (int)mode;
    vigil_binding_function_table_init(&program->functions, registry->runtime);
    vigil_program_set_module_context(program, source, NULL);
    return VIGIL_STATUS_OK;
}

static vigil_status_t parse_repl_trailing_statements(vigil_program_state_t *program, size_t cursor)
{
    size_t end_cursor = cursor;
    size_t depth = 0U;

    while (1)
    {
        const vigil_token_t *t = vigil_program_token_at(program, end_cursor);

        if (t == NULL || t->kind == VIGIL_TOKEN_EOF)
            break;
        if (t->kind == VIGIL_TOKEN_LBRACE)
            depth += 1U;
        else if (t->kind == VIGIL_TOKEN_RBRACE && depth != 0U)
            depth -= 1U;
        end_cursor += 1U;
    }
    program->repl_stmts_start = cursor;
    program->repl_stmts_end = end_cursor;
    return VIGIL_STATUS_OK;
}

static vigil_status_t parse_one_declaration(vigil_program_state_t *program, size_t *cursor, int *done)
{
    vigil_status_t status;
    const vigil_token_t *token;
    int is_public;

    *done = 0;
    token = vigil_program_token_at(program, *cursor);
    if (token == NULL || token->kind == VIGIL_TOKEN_EOF)
    {
        *done = 1;
        return VIGIL_STATUS_OK;
    }
    is_public = vigil_program_parse_optional_pub(program, cursor);
    token = vigil_program_token_at(program, *cursor);
    if (token == NULL || token->kind == VIGIL_TOKEN_EOF)
        return vigil_compile_report(program, vigil_program_eof_span(program), "expected declaration after 'pub'");

    if (token->kind == VIGIL_TOKEN_IMPORT)
    {
        if (is_public)
            return vigil_compile_report(program, token->span, "imports cannot be declared 'pub'");
        return vigil_program_parse_import(program, cursor);
    }
    if (token->kind == VIGIL_TOKEN_CONST)
        return vigil_program_parse_constant_declaration(program, cursor, is_public);
    if (token->kind == VIGIL_TOKEN_ENUM)
        return vigil_program_parse_enum_declaration(program, cursor, is_public);
    if (token->kind == VIGIL_TOKEN_INTERFACE)
        return vigil_program_parse_interface_declaration(program, cursor, is_public);
    if (token->kind == VIGIL_TOKEN_CLASS)
        return vigil_program_parse_class_declaration(program, cursor, is_public);
    if (vigil_program_is_global_variable_declaration_start(program, *cursor))
        return vigil_program_parse_global_variable_declaration(program, cursor, is_public);
    if (token->kind == VIGIL_TOKEN_EXTERN)
        return vigil_program_parse_extern_fn(program, cursor, is_public);
    if (token->kind == VIGIL_TOKEN_FN)
        return parse_fn_declaration(program, cursor, is_public);

    if (program->compile_mode == VIGIL_COMPILE_MODE_REPL && !is_public)
    {
        status = parse_repl_trailing_statements(program, *cursor);
        *done = 1;
        return status;
    }

    return vigil_compile_report(program, token->span,
                                "expected top-level 'import', 'const', 'enum', 'interface', 'class', variable "
                                "declaration, 'extern fn', or 'fn'");
}

static vigil_status_t emit_implicit_void_return(vigil_parser_state_t *state, vigil_source_span_t span)
{
    return emit_opcode_u32(state, VIGIL_OPCODE_RETURN, 0U, span);
}

static vigil_status_t emit_repl_synthetic_return(vigil_parser_state_t *state, vigil_program_state_t *program,
                                                 vigil_source_span_t span)
{
    vigil_status_t status;
    vigil_value_t zero_val;
    size_t const_index;

    vigil_value_init_int(&zero_val, 0);
    status = vigil_chunk_add_constant(&state->chunk, &zero_val, &const_index, program->error);
    if (status != VIGIL_STATUS_OK)
        return status;
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_CONSTANT, span);
    if (status != VIGIL_STATUS_OK)
        return status;
    status = vigil_parser_emit_u32(state, (uint32_t)const_index, span);
    if (status != VIGIL_STATUS_OK)
        return status;
    return emit_opcode_u32(state, VIGIL_OPCODE_RETURN, 1U, span);
}

static vigil_status_t finalize_function_body_return_analysis(vigil_program_state_t *program,
                                                             vigil_parser_state_t *state, vigil_function_decl_t *decl,
                                                             size_t function_index,
                                                             vigil_statement_result_t *body_result)
{
    vigil_status_t status;

    if (!vigil_statement_result_guarantees_return(body_result) && decl->return_count == 1U &&
        vigil_parser_type_is_void(decl->return_type))
    {
        status = emit_implicit_void_return(state, decl->name_span);
        if (status != VIGIL_STATUS_OK)
            return status;
        vigil_statement_result_set_guaranteed_return(body_result, 1);
    }

    if (!vigil_statement_result_guarantees_return(body_result) && program->compile_mode == VIGIL_COMPILE_MODE_REPL &&
        function_index == program->functions.main_index)
    {
        status = emit_repl_synthetic_return(state, program, decl->name_span);
        if (status != VIGIL_STATUS_OK)
            return status;
        vigil_statement_result_set_guaranteed_return(body_result, 1);
    }

    return vigil_compile_require_function_returns(program, decl, function_index,
                                                  vigil_statement_result_guarantees_return(body_result));
}

static vigil_status_t try_check_constructor_or_extern(vigil_program_state_t *program, size_t function_index,
                                                      int *handled)
{
    size_t class_index;

    *handled = 0;
    for (class_index = 0U; class_index < program->class_count; class_index += 1U)
    {
        const vigil_class_decl_t *class_decl = &program->classes[class_index];
        const vigil_class_method_t *init_method;

        if (class_decl->constructor_function_index != function_index)
            continue;
        init_method = NULL;
        if (!vigil_class_decl_find_method(class_decl, "init", 4U, NULL, &init_method) || init_method == NULL)
        {
            vigil_error_set_literal(program->error, VIGIL_STATUS_INTERNAL, "class init declaration is missing");
            return VIGIL_STATUS_INTERNAL;
        }
        *handled = 1;
        return VIGIL_STATUS_OK;
    }

    for (size_t ei = 0; ei < program->extern_fn_count; ei++)
    {
        if (program->extern_fns[ei].function_index == function_index)
        {
            *handled = 1;
            return VIGIL_STATUS_OK;
        }
    }

    return VIGIL_STATUS_OK;
}

void vigil_statement_result_set_non_returning(vigil_statement_result_t *result)
{
    vigil_statement_result_set_guaranteed_return(result, 0);
}

int vigil_statement_result_guarantees_return(const vigil_statement_result_t *result)
{
    return result != NULL && result->guaranteed_return;
}

void vigil_statement_result_merge_sequence(vigil_statement_result_t *result,
                                           const vigil_statement_result_t *next_result)
{
    vigil_statement_result_set_guaranteed_return(result, vigil_statement_result_guarantees_return(result) ||
                                                             vigil_statement_result_guarantees_return(next_result));
}

void vigil_statement_result_set_conditional(vigil_statement_result_t *result, int has_else_branch,
                                            const vigil_statement_result_t *then_result,
                                            const vigil_statement_result_t *else_result)
{
    vigil_statement_result_set_guaranteed_return(result, has_else_branch &&
                                                             vigil_statement_result_guarantees_return(then_result) &&
                                                             vigil_statement_result_guarantees_return(else_result));
}

void vigil_statement_result_set_switch(vigil_statement_result_t *result, int has_default, int all_branches_return)
{
    vigil_statement_result_set_guaranteed_return(result, has_default && all_branches_return);
}

void vigil_return_analysis_merge_switch_branch(int *all_branches_return, const vigil_statement_result_t *branch_result)
{
    *all_branches_return = *all_branches_return && vigil_statement_result_guarantees_return(branch_result);
}

vigil_status_t parse_bool_condition_expression(vigil_parser_state_t *state, vigil_source_span_t span,
                                               const char *scalar_message, const char *type_message,
                                               vigil_expression_result_t *condition_result)
{
    vigil_status_t status;

    status = vigil_parser_parse_expression(state, condition_result);
    if (status != VIGIL_STATUS_OK)
        return status;
    status = vigil_parser_require_scalar_expression(state, span, condition_result, scalar_message);
    if (status != VIGIL_STATUS_OK)
        return status;
    return vigil_parser_require_type(state, span, condition_result->type, vigil_binding_type_primitive(VIGIL_TYPE_BOOL),
                                     type_message);
}

vigil_status_t parse_parenthesized_bool_condition(vigil_parser_state_t *state, const vigil_token_t *keyword_token,
                                                  const char *lparen_message, const char *rparen_message,
                                                  const char *scalar_message, const char *type_message,
                                                  vigil_expression_result_t *condition_result)
{
    vigil_status_t status;

    status = vigil_parser_expect(state, VIGIL_TOKEN_LPAREN, lparen_message, NULL);
    if (status != VIGIL_STATUS_OK)
        return status;
    status =
        parse_bool_condition_expression(state, keyword_token->span, scalar_message, type_message, condition_result);
    if (status != VIGIL_STATUS_OK)
        return status;
    return vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, rparen_message, NULL);
}

vigil_status_t vigil_semantic_parse_statement_sequence(vigil_parser_state_t *state,
                                                       vigil_statement_result_t *out_result,
                                                       vigil_semantic_parse_step_t parse_step,
                                                       vigil_semantic_stop_predicate_t should_stop)
{
    vigil_status_t status;
    vigil_statement_result_t step_result;
    vigil_statement_result_t block_result;

    if (state == NULL || parse_step == NULL || should_stop == NULL)
    {
        vigil_error_set_literal(state == NULL ? NULL : state->program->error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "semantic statement sequence arguments are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_statement_result_clear(&step_result);
    vigil_statement_result_clear(&block_result);

    while (!vigil_parser_is_at_end(state) && !should_stop(state))
    {
        status = parse_step(state, &step_result);
        if (status != VIGIL_STATUS_OK)
            return status;
        vigil_statement_result_merge_sequence(&block_result, &step_result);
    }

    vigil_statement_result_set_guaranteed_return(out_result, block_result.guaranteed_return);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_program_parse_import(vigil_program_state_t *program, size_t *cursor)
{
    vigil_status_t status;
    const vigil_token_t *token;
    const vigil_token_t *import_target_token;
    const vigil_token_t *alias_token = NULL;
    const char *alias_text = NULL;
    size_t alias_length = 0U;
    vigil_string_t import_path;
    vigil_source_id_t imported_source_id = 0U;
    vigil_program_module_t *module;
    size_t native_idx = 0U;
    int native_found = 0;

    vigil_string_init(&import_path, program->registry->runtime);

    token = vigil_program_token_at(program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_IMPORT)
    {
        status = vigil_compile_report(program, token == NULL ? vigil_program_eof_span(program) : token->span,
                                      "expected 'import'");
        goto cleanup;
    }
    *cursor += 1U;

    import_target_token = vigil_program_token_at(program, *cursor);
    native_found = resolve_native_import(program, import_target_token, &native_idx, &imported_source_id);
    status = vigil_program_parse_import_target(program, import_target_token, &import_path);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;
    *cursor += 1U;

    status = parse_import_alias(program, cursor, &alias_token, &alias_text, &alias_length);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    token = vigil_program_token_at(program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_SEMICOLON)
    {
        status = vigil_compile_report(program, token == NULL ? vigil_program_eof_span(program) : token->span,
                                      "expected ';' after import");
        goto cleanup;
    }
    *cursor += 1U;

    status =
        resolve_import_source(program, native_found, import_target_token, token, &import_path, &imported_source_id);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    module = vigil_program_current_module(program);
    if (module == NULL)
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_INTERNAL,
                                "current module must be available while parsing imports");
        status = VIGIL_STATUS_INTERNAL;
        goto cleanup;
    }
    resolve_import_alias_default(program, native_found, native_idx, &import_path, &alias_text, &alias_length);
    if (alias_token != NULL && program->natives != NULL && vigil_stdlib_is_known_module(alias_text, alias_length))
    {
        status = vigil_compile_report(program, alias_token->span, "import alias shadows a standard library module");
        goto cleanup;
    }
    status = vigil_program_add_module_import(program, module, alias_text, alias_length,
                                             alias_token == NULL ? token->span : alias_token->span, imported_source_id);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    status = native_found ? register_native_import(program, native_idx, imported_source_id)
                          : vigil_program_parse_source(program, imported_source_id);

cleanup:
    vigil_string_free(&import_path);
    return status;
}

vigil_status_t vigil_semantic_parse_program_declarations(vigil_program_state_t *program)
{
    vigil_status_t status;
    size_t cursor = 0U;
    int done = 0;

    while (!done)
    {
        status = parse_one_declaration(program, &cursor, &done);
        if (status != VIGIL_STATUS_OK)
            return status;
    }
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_semantic_prepare_program(vigil_program_state_t *program, vigil_source_id_t source_id,
                                              vigil_compile_mode_t mode, int allow_repl_main_synthesis)
{
    vigil_status_t status;
    const vigil_source_file_t *source = program->source;

    status = vigil_program_parse_source(program, source_id);
    if (status != VIGIL_STATUS_OK)
        return status;

    vigil_program_set_module_context(program, source, NULL);

    if (allow_repl_main_synthesis && mode == VIGIL_COMPILE_MODE_REPL && !program->functions.has_main &&
        (program->repl_stmts_start < program->repl_stmts_end || program->global_count > 0U))
    {
        status = vigil_semantic_synthesize_repl_main(program, source_id);
        if (status != VIGIL_STATUS_OK)
            return status;
    }

    if (!program->functions.has_main)
    {
        if (mode == VIGIL_COMPILE_MODE_REPL)
            return VIGIL_STATUS_OK;
        return vigil_compile_report(program, vigil_program_eof_span(program), "expected top-level function 'main'");
    }

    return VIGIL_STATUS_OK;
}

void vigil_lowered_function_body_init(vigil_lowered_function_body_t *body)
{
    if (body == NULL)
        return;

    memset(body, 0, sizeof(*body));
}

void vigil_lowered_function_body_free(vigil_lowered_function_body_t *body)
{
    if (body == NULL)
        return;

    vigil_chunk_free(&body->chunk);
    body->function_index = 0U;
    body->guaranteed_return = 0;
}

vigil_status_t vigil_semantic_analyze_function_body(vigil_program_state_t *program, size_t function_index,
                                                    const vigil_parser_state_t *parent_state,
                                                    vigil_parser_state_t *state,
                                                    vigil_statement_result_t *out_body_result)
{
    vigil_status_t status;
    vigil_function_decl_t *decl = &program->functions.functions[function_index];

    vigil_program_set_module_context(program, decl->source, decl->tokens);
    memset(state, 0, sizeof(*state));
    state->program = program;
    state->parent = (vigil_parser_state_t *)parent_state;
    state->current = decl->body_start;
    state->body_end = decl->body_end;
    state->function_index = function_index;
    state->expected_return_type = decl->return_type;
    state->expected_return_types = vigil_function_return_types(decl);
    state->expected_return_count = decl->return_count;
    vigil_chunk_init(&state->chunk, program->registry->runtime);
    vigil_binding_scope_stack_init(&state->locals, program->registry->runtime);
    vigil_binding_scope_stack_begin_scope(&state->locals);
    vigil_statement_result_clear(out_body_result);

    status = vigil_compile_seed_parameter_symbols(state, decl);
    if (status != VIGIL_STATUS_OK)
        return status;

    if (function_index == program->functions.main_index)
    {
        status = vigil_compile_emit_global_initializers(program, state);
        if (status != VIGIL_STATUS_OK)
            return status;
    }

    status = vigil_parser_parse_block_contents(state, out_body_result);
    if (status != VIGIL_STATUS_OK)
        return status;

    decl = &program->functions.functions[function_index];
    return finalize_function_body_return_analysis(program, state, decl, function_index, out_body_result);
}

vigil_status_t vigil_semantic_lower_function_body(vigil_program_state_t *program, size_t function_index,
                                                  const vigil_parser_state_t *parent_state,
                                                  vigil_lowered_function_body_t *out_body)
{
    vigil_status_t status;
    vigil_parser_state_t state;
    vigil_statement_result_t body_result;

    if (out_body == NULL)
    {
        vigil_error_set_literal(program == NULL ? NULL : program->error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "out_body must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_lowered_function_body_init(out_body);
    status = vigil_semantic_analyze_function_body(program, function_index, parent_state, &state, &body_result);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_chunk_free(&state.chunk);
        vigil_parser_state_free(&state);
        return status;
    }

    out_body->chunk = state.chunk;
    memset(&state.chunk, 0, sizeof(state.chunk));
    out_body->function_index = function_index;
    out_body->guaranteed_return = body_result.guaranteed_return;
    vigil_parser_state_free(&state);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_semantic_check_function_with_parent(vigil_program_state_t *program, size_t function_index,
                                                         const vigil_parser_state_t *parent_state)
{
    vigil_status_t status;
    vigil_parser_state_t state;
    vigil_statement_result_t body_result;
    int handled = 0;

    status = try_check_constructor_or_extern(program, function_index, &handled);
    if (handled || status != VIGIL_STATUS_OK)
        return status;

    status = vigil_semantic_analyze_function_body(program, function_index, parent_state, &state, &body_result);
    vigil_chunk_free(&state.chunk);
    vigil_parser_state_free(&state);
    return status;
}

vigil_status_t vigil_semantic_prepare_source_internal(const vigil_source_registry_t *registry,
                                                      vigil_source_id_t source_id, vigil_compile_mode_t mode,
                                                      const vigil_native_registry_t *natives,
                                                      vigil_diagnostic_list_t *diagnostics,
                                                      vigil_program_state_t *out_program, vigil_error_t *error)
{
    vigil_status_t status;
    const vigil_source_file_t *source;

    vigil_error_clear(error);
    status = vigil_semantic_validate_prepare_inputs(registry, diagnostics, out_program, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    source = vigil_source_registry_get(registry, source_id);
    if (source == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "source_id must reference a registered source file");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_semantic_init_program(out_program, registry, source, natives, diagnostics, error, mode);
    if (status != VIGIL_STATUS_OK)
        return status;

    status = vigil_semantic_prepare_program(out_program, source_id, mode, mode == VIGIL_COMPILE_MODE_REPL);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_program_free(out_program);
        return status;
    }

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_semantic_validate_program_internal(vigil_program_state_t *program)
{
    vigil_status_t status;
    size_t i;

    if (program == NULL)
        return VIGIL_STATUS_INVALID_ARGUMENT;

    for (i = 0U; i < program->functions.count; ++i)
    {
        status = vigil_semantic_check_function_with_parent(program, i, NULL);
        if (status != VIGIL_STATUS_OK)
            return status;
    }

    return VIGIL_STATUS_OK;
}

void assignment_target_init(assignment_target_t *t)
{
    t->name_token = NULL;
    t->target_token = NULL;
    t->local_index = 0U;
    t->capture_index = 0U;
    t->global_index = 0U;
    t->field_index = 0U;
    t->local_type = vigil_binding_type_invalid();
    t->target_type = vigil_binding_type_invalid();
    t->field = NULL;
    t->local_decl = NULL;
    t->global_decl = NULL;
    t->is_field_assignment = 0;
    t->is_index_assignment = 0;
    t->is_global_assignment = 0;
    t->is_const_local = 0;
    t->emitted_target_base = 0;
    t->is_capture_local = 0;
}

int assignment_target_is_composite(const assignment_target_t *t)
{
    return t->is_field_assignment || t->is_index_assignment;
}

vigil_status_t check_non_assignable_member(vigil_parser_state_t *state, vigil_source_id_t import_source_id,
                                           const char *member_text, size_t member_length, vigil_source_span_t span)
{
    size_t dummy_index = 0U;
    const vigil_function_decl_t *f = NULL;
    const vigil_class_decl_t *cl = NULL;
    const vigil_interface_decl_t *iface = NULL;
    const vigil_enum_decl_t *en = NULL;

    if (vigil_program_find_top_level_function_name_in_source(state->program, import_source_id, member_text,
                                                             member_length, &dummy_index, &f))
    {
        if (!vigil_program_is_function_public(f))
            return vigil_parser_report(state, span, "module member is not public");
        return vigil_parser_report(state, span, "module member is not assignable");
    }
    if (vigil_program_find_class_in_source(state->program, import_source_id, member_text, member_length, &dummy_index,
                                           &cl))
    {
        if (!vigil_program_is_class_public(cl))
            return vigil_parser_report(state, span, "module member is not public");
        return vigil_parser_report(state, span, "module member is not assignable");
    }
    if (vigil_program_find_interface_in_source(state->program, import_source_id, member_text, member_length,
                                               &dummy_index, &iface))
    {
        if (!vigil_program_is_interface_public(iface))
            return vigil_parser_report(state, span, "module member is not public");
        return vigil_parser_report(state, span, "module member is not assignable");
    }
    if (vigil_program_find_enum_in_source(state->program, import_source_id, member_text, member_length, &dummy_index,
                                          &en))
    {
        if (!vigil_program_is_enum_public(en))
            return vigil_parser_report(state, span, "module member is not public");
        return vigil_parser_report(state, span, "module member is not assignable");
    }
    return vigil_parser_report(state, span, "unknown module member");
}

vigil_status_t vigil_parser_resolve_import_assignment_target(vigil_parser_state_t *state,
                                                             vigil_source_id_t import_source_id,
                                                             import_assignment_result_t *out)
{
    vigil_status_t status;
    const vigil_token_t *member_token = NULL;
    const char *member_text;
    size_t member_length;

    vigil_parser_advance(state);
    status = vigil_parser_expect(state, VIGIL_TOKEN_IDENTIFIER, "expected module member name after '.'", &member_token);
    if (status != VIGIL_STATUS_OK)
        return status;
    out->target_token = member_token;
    member_text = vigil_parser_token_text(state, member_token, &member_length);

    if (vigil_program_find_global_in_source(state->program, import_source_id, member_text, member_length,
                                            &out->global_index, &out->global_decl))
    {
        if (!vigil_program_is_global_public(out->global_decl))
            return vigil_parser_report(state, member_token->span, "module member is not public");
        out->is_global = 1;
        out->type = out->global_decl->type;
        return VIGIL_STATUS_OK;
    }
    {
        const vigil_global_constant_t *c = NULL;

        if (vigil_program_find_constant_in_source(state->program, import_source_id, member_text, member_length, &c))
        {
            if (!vigil_program_is_constant_public(c))
                return vigil_parser_report(state, member_token->span, "module member is not public");
            return vigil_parser_report(state, member_token->span, "cannot assign to module constant");
        }
    }
    return check_non_assignable_member(state, import_source_id, member_text, member_length, member_token->span);
}

vigil_status_t resolve_nonlocal_target(vigil_parser_state_t *state, assignment_target_t *t)
{
    vigil_status_t status;
    vigil_source_id_t import_source_id = 0U;
    const char *name_text;
    size_t name_length;

    name_text = vigil_parser_token_text(state, t->name_token, &name_length);
    if (vigil_parser_check(state, VIGIL_TOKEN_DOT) &&
        vigil_program_resolve_import_alias(state->program, name_text, name_length, &import_source_id))
    {
        import_assignment_result_t import_result = {0};

        status = vigil_parser_resolve_import_assignment_target(state, import_source_id, &import_result);
        if (status != VIGIL_STATUS_OK)
            return status;
        t->target_token = import_result.target_token;
        t->global_index = import_result.global_index;
        t->global_decl = import_result.global_decl;
        t->is_global_assignment = import_result.is_global;
        t->local_type = import_result.type;
    }
    else if (!vigil_program_find_global_in_source(state->program,
                                                  state->program->source == NULL ? 0U : state->program->source->id,
                                                  name_text, name_length, &t->global_index, &t->global_decl))
    {
        return vigil_parser_report(state, t->name_token->span, "unknown local variable");
    }
    if (!t->is_global_assignment)
    {
        t->is_global_assignment = 1;
        t->local_type = t->global_decl->type;
    }
    return VIGIL_STATUS_OK;
}

vigil_status_t validate_assignment_target_writable(vigil_parser_state_t *state, const assignment_target_t *t)
{
    if (t->is_const_local && !assignment_target_is_composite(t))
        return vigil_parser_report(state, t->target_token->span, "cannot assign to const local variable");
    return VIGIL_STATUS_OK;
}

vigil_status_t parse_assignment_operator(vigil_parser_state_t *state, const assignment_target_t *t,
                                         const vigil_token_t **out_operator_token)
{
    const vigil_token_t *operator_token;

    operator_token = vigil_parser_peek(state);
    if (operator_token == NULL || !vigil_semantic_is_assignment_operator(operator_token->kind))
        return vigil_parser_report(state, t->target_token->span, "expected assignment operator");

    vigil_parser_advance(state);
    *out_operator_token = operator_token;
    return VIGIL_STATUS_OK;
}

vigil_status_t resolve_assignment_target(vigil_parser_state_t *state, assignment_target_t *t)
{
    vigil_status_t status;
    int local_found = 0;

    status = vigil_parser_expect(state, VIGIL_TOKEN_IDENTIFIER, "expected local variable name", &t->name_token);
    if (status != VIGIL_STATUS_OK)
        return status;
    t->target_token = t->name_token;

    status = vigil_parser_resolve_local_symbol(state, t->name_token, &t->local_index, &t->local_type,
                                               &t->is_capture_local, &t->capture_index, &local_found);
    if (status != VIGIL_STATUS_OK)
        return status;

    if (local_found)
    {
        if (!t->is_capture_local)
        {
            t->local_decl = vigil_binding_scope_stack_local_at(&state->locals, t->local_index);
            t->local_type = t->local_decl->type;
            t->is_const_local = t->local_decl->is_const;
        }
    }
    else
    {
        status = resolve_nonlocal_target(state, t);
        if (status != VIGIL_STATUS_OK)
            return status;
    }
    t->target_type = t->local_type;
    return VIGIL_STATUS_OK;
}

vigil_status_t resolve_assignment_field_target(vigil_parser_state_t *state, assignment_target_t *t)
{
    vigil_status_t status;
    const vigil_token_t *field_token = NULL;

    status = vigil_parser_expect(state, VIGIL_TOKEN_IDENTIFIER, "expected field name after '.'", &field_token);
    if (status != VIGIL_STATUS_OK)
        return status;

    t->field = NULL;
    t->field_index = 0U;
    status = vigil_parser_lookup_field(state, t->target_type, field_token, &t->field_index, &t->field);
    if (status != VIGIL_STATUS_OK)
        return status;

    t->target_type = t->field->type;
    t->is_field_assignment = 1;
    t->is_index_assignment = 0;
    return VIGIL_STATUS_OK;
}

vigil_status_t resolve_assignment_index_target(vigil_parser_state_t *state, assignment_target_t *t)
{
    vigil_status_t status;
    vigil_expression_result_t index_result;
    vigil_parser_type_t indexed_type;

    vigil_expression_result_clear(&index_result);
    indexed_type = vigil_binding_type_invalid();
    status = vigil_parser_parse_expression(state, &index_result);
    if (status != VIGIL_STATUS_OK)
        return status;
    status = vigil_parser_require_scalar_expression(
        state, vigil_parser_previous(state) == NULL ? t->name_token->span : vigil_parser_previous(state)->span,
        &index_result, "index expressions must evaluate to a single value");
    if (status != VIGIL_STATUS_OK)
        return status;
    status = vigil_parser_expect(state, VIGIL_TOKEN_RBRACKET, "expected ']' after index expression", NULL);
    if (status != VIGIL_STATUS_OK)
        return status;

    if (vigil_parser_type_is_array(t->target_type))
    {
        status = vigil_parser_require_type(state, t->name_token->span, index_result.type,
                                           vigil_binding_type_primitive(VIGIL_TYPE_I32), "array index must be i32");
        if (status != VIGIL_STATUS_OK)
            return status;
        indexed_type = vigil_program_array_type_element(state->program, t->target_type);
    }
    else if (vigil_parser_type_is_map(t->target_type))
    {
        status = vigil_parser_require_type(state, t->name_token->span, index_result.type,
                                           vigil_program_map_type_key(state->program, t->target_type),
                                           "map index must match map key type");
        if (status != VIGIL_STATUS_OK)
            return status;
        indexed_type = vigil_program_map_type_value(state->program, t->target_type);
    }
    else
    {
        return vigil_parser_report(state, t->name_token->span, "index assignment requires an array or map");
    }
    t->target_type = indexed_type;
    t->is_field_assignment = 0;
    t->is_index_assignment = 1;
    return VIGIL_STATUS_OK;
}

vigil_status_t parse_increment_decrement(vigil_parser_state_t *state, const assignment_target_t *t,
                                         const vigil_token_t *op, vigil_opcode_t *out_opcode)
{
    vigil_status_t status;

    if (vigil_parser_type_is_integer(t->target_type))
        status = vigil_parser_emit_integer_constant(state, t->target_type, 1, op->span);
    else if (vigil_parser_type_is_f64(t->target_type))
        status = vigil_parser_emit_f64_constant(state, 1.0, op->span);
    else
        status = vigil_parser_report(state, op->span, "increment and decrement require an integer or f64 target");
    if (status != VIGIL_STATUS_OK)
        return status;
    *out_opcode = op->kind == VIGIL_TOKEN_PLUS_PLUS ? VIGIL_OPCODE_ADD : VIGIL_OPCODE_SUBTRACT;
    return VIGIL_STATUS_OK;
}

static vigil_status_t resolve_compound_operator(vigil_parser_state_t *state, const assignment_target_t *t,
                                                const vigil_token_t *op, const vigil_expression_result_t *value_result,
                                                vigil_opcode_t *out_opcode)
{
    vigil_binary_operator_kind_t operator_kind;

    switch (op->kind)
    {
    case VIGIL_TOKEN_PLUS_ASSIGN:
        operator_kind = VIGIL_BINARY_OPERATOR_ADD;
        *out_opcode = VIGIL_OPCODE_ADD;
        break;
    case VIGIL_TOKEN_MINUS_ASSIGN:
        operator_kind = VIGIL_BINARY_OPERATOR_SUBTRACT;
        *out_opcode = VIGIL_OPCODE_SUBTRACT;
        break;
    case VIGIL_TOKEN_STAR_ASSIGN:
        operator_kind = VIGIL_BINARY_OPERATOR_MULTIPLY;
        *out_opcode = VIGIL_OPCODE_MULTIPLY;
        break;
    case VIGIL_TOKEN_SLASH_ASSIGN:
        operator_kind = VIGIL_BINARY_OPERATOR_DIVIDE;
        *out_opcode = VIGIL_OPCODE_DIVIDE;
        break;
    case VIGIL_TOKEN_PERCENT_ASSIGN:
        operator_kind = VIGIL_BINARY_OPERATOR_MODULO;
        *out_opcode = VIGIL_OPCODE_MODULO;
        break;
    default:
        return vigil_parser_report(state, op->span, "unsupported assignment operator");
    }

    if (!vigil_parser_type_supports_binary_operator(operator_kind, t->target_type, value_result->type))
    {
        return vigil_parser_report(state, op->span,
                                   operator_kind == VIGIL_BINARY_OPERATOR_ADD
                                       ? "compound assignment requires matching integer, f64, or string operands"
                                       : (operator_kind == VIGIL_BINARY_OPERATOR_MODULO
                                              ? "compound assignment modulo requires matching integer operands"
                                              : "compound assignment requires matching integer or f64 operands"));
    }
    return VIGIL_STATUS_OK;
}

vigil_status_t parse_compound_assignment_value(vigil_parser_state_t *state, const assignment_target_t *t,
                                               const vigil_token_t *op, vigil_expression_result_t *value_result)
{
    vigil_status_t status;

    if (op->kind == VIGIL_TOKEN_PLUS_PLUS || op->kind == VIGIL_TOKEN_MINUS_MINUS)
    {
        value_result->type = t->target_type;
        return VIGIL_STATUS_OK;
    }

    status = vigil_parser_parse_expression(state, value_result);
    if (status != VIGIL_STATUS_OK)
        return status;
    return vigil_parser_require_scalar_expression(state, op->span, value_result,
                                                  "assigned expression must be a single value");
}

vigil_status_t validate_compound_assignment_operation(vigil_parser_state_t *state, const assignment_target_t *t,
                                                      const vigil_token_t *op,
                                                      const vigil_expression_result_t *value_result,
                                                      vigil_opcode_t *out_opcode)
{
    if (op->kind == VIGIL_TOKEN_PLUS_PLUS || op->kind == VIGIL_TOKEN_MINUS_MINUS)
        return parse_increment_decrement(state, t, op, out_opcode);
    return resolve_compound_operator(state, t, op, value_result, out_opcode);
}

static const char *assign_type_mismatch_message(const assignment_target_t *t)
{
    if (t->is_index_assignment)
        return "assigned expression type does not match indexed value type";
    if (t->is_field_assignment)
        return "assigned expression type does not match field type";
    if (t->is_global_assignment)
        return "assigned expression type does not match global variable type";
    return "assigned expression type does not match local variable type";
}

vigil_status_t parse_simple_assignment(vigil_parser_state_t *state, const assignment_target_t *t,
                                       const vigil_token_t *op, vigil_expression_result_t *value_result)
{
    vigil_status_t status;

    status = vigil_parser_parse_expression_with_expected_type(state, t->target_type, value_result);
    if (status != VIGIL_STATUS_OK)
        return status;
    status = vigil_parser_require_scalar_expression(state, op->span, value_result,
                                                    "assigned expression must be a single value");
    if (status != VIGIL_STATUS_OK)
        return status;
    return vigil_parser_require_type(state, t->target_token->span, value_result->type, t->target_type,
                                     assign_type_mismatch_message(t));
}
