#include "internal/vigil_compiler_internal.h"
#include "internal/vigil_compiler_types.h"
#include "internal/vigil_internal.h"
#include <stdlib.h>
#include <string.h>

int vigil_program_token_is_identifier_text(const vigil_program_state_t *program, const vigil_token_t *token,
                                           const char *text, size_t text_length)
{
    const char *token_text;
    size_t token_length;

    if (token == NULL || token->kind != VIGIL_TOKEN_IDENTIFIER)
    {
        return 0;
    }

    token_text = vigil_program_token_text(program, token, &token_length);
    return vigil_program_names_equal(token_text, token_length, text, text_length);
}

static vigil_status_t vigil_program_add_binding_param(vigil_program_state_t *program, vigil_function_decl_t *function,
                                                      const char *name, size_t name_length, vigil_source_span_t span,
                                                      vigil_parser_type_t type)
{
    vigil_binding_function_param_spec_t param_spec = {0};

    param_spec.name = name;
    param_spec.name_length = name_length;
    param_spec.span = span;
    param_spec.type = type;
    return vigil_binding_function_add_param(program->registry->runtime, function, &param_spec, program->error);
}

vigil_status_t vigil_enum_decl_grow_members(vigil_program_state_t *program, vigil_enum_decl_t *decl,
                                            size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= decl->member_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = decl->member_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > SIZE_MAX / 2U)
        {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*decl->members))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY, "enum member table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->members;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*decl->members), &memory,
                                     program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory, next_capacity * sizeof(*decl->members),
                                       program->error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_enum_member_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*decl->members));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    decl->members = (vigil_enum_member_t *)memory;
    decl->member_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}
vigil_status_t vigil_interface_method_grow_params(vigil_program_state_t *program, vigil_interface_method_t *method,
                                                  size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= method->param_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = method->param_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > SIZE_MAX / 2U)
        {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*method->param_types))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY,
                                "interface parameter table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = method->param_types;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*method->param_types), &memory,
                                     program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory,
                                       next_capacity * sizeof(*method->param_types), program->error);
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    method->param_types = (vigil_parser_type_t *)memory;
    method->param_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_interface_method_grow_returns(vigil_program_state_t *program, vigil_interface_method_t *method,
                                                   size_t minimum_capacity)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= method->return_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    old_capacity = method->return_capacity;
    next_capacity = old_capacity == 0U ? 2U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > SIZE_MAX / 2U)
        {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*method->return_types))
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY,
                                "interface return type table allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = method->return_types;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(program->registry->runtime, next_capacity * sizeof(*method->return_types), &memory,
                                     program->error);
    }
    else
    {
        status = vigil_runtime_realloc(program->registry->runtime, &memory,
                                       next_capacity * sizeof(*method->return_types), program->error);
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    method->return_types = (vigil_parser_type_t *)memory;
    method->return_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}
vigil_status_t vigil_program_parse_interface_method_return_types(vigil_program_state_t *program, size_t *cursor,
                                                                 const char *unsupported_message,
                                                                 vigil_interface_method_t *method)
{
    vigil_status_t status;
    const vigil_token_t *token;
    vigil_parser_type_t return_type;

    token = vigil_program_token_at(program, *cursor);
    if (token != NULL && token->kind == VIGIL_TOKEN_LPAREN)
    {
        *cursor += 1U;
        while (1)
        {
            status = vigil_program_parse_type_reference(program, cursor, unsupported_message, &return_type);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }
            status = vigil_interface_method_grow_returns(program, method, method->return_count + 1U);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }
            method->return_types[method->return_count] = return_type;
            method->return_count += 1U;
            method->return_type = method->return_types[0];

            token = vigil_program_token_at(program, *cursor);
            if (token != NULL && token->kind == VIGIL_TOKEN_COMMA)
            {
                *cursor += 1U;
                continue;
            }
            if (token != NULL && token->kind == VIGIL_TOKEN_RPAREN)
            {
                *cursor += 1U;
                break;
            }
            return vigil_compile_report(program, token == NULL ? vigil_program_eof_span(program) : token->span,
                                        unsupported_message);
        }
        return VIGIL_STATUS_OK;
    }

    status = vigil_program_parse_type_reference(program, cursor, unsupported_message, &return_type);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_interface_method_grow_returns(program, method, 1U);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    method->return_types[0] = return_type;
    method->return_count = 1U;
    method->return_type = return_type;
    return VIGIL_STATUS_OK;
}

typedef struct
{
    const vigil_token_t *type_token;
    const vigil_token_t *name_token;
    vigil_parser_type_t declared_type;
    const char *name_text;
    size_t name_length;
} vigil_named_global_decl_t;

typedef struct
{
    const char *function_conflict;
    const char *interface_conflict;
    const char *enum_conflict;
    const char *class_conflict;
    const char *constant_conflict;
    const char *global_conflict;
} vigil_global_name_conflict_messages_t;

static vigil_status_t vigil_program_check_global_name_conflicts(vigil_program_state_t *program,
                                                                const vigil_token_t *name_token, const char *name_text,
                                                                size_t name_length,
                                                                const vigil_global_name_conflict_messages_t *messages)
{
    const vigil_function_decl_t *existing_function;
    const vigil_global_constant_t *existing_constant;
    const vigil_global_variable_t *existing_global;

    if (program->compile_mode == VIGIL_COMPILE_MODE_REPL)
    {
        return VIGIL_STATUS_OK;
    }

    if (vigil_program_find_top_level_function_name_in_source(program, program->source->id, name_text, name_length, NULL,
                                                             &existing_function))
    {
        return vigil_compile_report(program, name_token->span, messages->function_conflict);
    }
    if (vigil_program_find_interface_in_source(program, program->source->id, name_text, name_length, NULL, NULL))
    {
        return vigil_compile_report(program, name_token->span, messages->interface_conflict);
    }
    if (vigil_program_find_enum_in_source(program, program->source->id, name_text, name_length, NULL, NULL))
    {
        return vigil_compile_report(program, name_token->span, messages->enum_conflict);
    }
    if (vigil_program_find_class_in_source(program, program->source->id, name_text, name_length, NULL, NULL))
    {
        return vigil_compile_report(program, name_token->span, messages->class_conflict);
    }
    if (vigil_program_find_constant_in_source(program, program->source->id, name_text, name_length, &existing_constant))
    {
        return vigil_compile_report(program, name_token->span, messages->constant_conflict);
    }
    if (vigil_program_find_global_in_source(program, program->source->id, name_text, name_length, NULL,
                                            &existing_global))
    {
        return vigil_compile_report(program, name_token->span, messages->global_conflict);
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_require_global_assignment(vigil_program_state_t *program, size_t *cursor,
                                                              const vigil_token_t *name_token, const char *message)
{
    const vigil_token_t *token = vigil_program_cursor_peek(program, *cursor);

    if (token == NULL || token->kind != VIGIL_TOKEN_ASSIGN)
    {
        return vigil_compile_report(program, name_token->span, message);
    }

    vigil_program_cursor_advance(program, cursor);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_parse_global_variable_header(vigil_program_state_t *program, size_t *cursor,
                                                                 vigil_named_global_decl_t *declaration)
{
    static const vigil_global_name_conflict_messages_t messages = {
        "global variable name conflicts with function",
        "global variable name conflicts with interface",
        "global variable name conflicts with enum",
        "global variable name conflicts with class",
        "global variable name conflicts with global constant",
        "global variable is already declared",
    };
    vigil_status_t status;

    declaration->type_token = vigil_program_cursor_peek(program, *cursor);
    status = vigil_program_parse_type_reference(program, cursor, "unsupported global variable type",
                                                &declaration->declared_type);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_program_require_non_void_type(
        program, declaration->type_token == NULL ? vigil_program_eof_span(program) : declaration->type_token->span,
        declaration->declared_type, "global variables cannot use type void");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    declaration->name_token = vigil_program_cursor_peek(program, *cursor);
    if (declaration->name_token == NULL || declaration->name_token->kind != VIGIL_TOKEN_IDENTIFIER)
    {
        return vigil_compile_report(
            program, declaration->type_token == NULL ? vigil_program_eof_span(program) : declaration->type_token->span,
            "expected global variable name");
    }

    declaration->name_text = vigil_program_token_text(program, declaration->name_token, &declaration->name_length);
    status = vigil_program_check_global_name_conflicts(program, declaration->name_token, declaration->name_text,
                                                       declaration->name_length, &messages);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vigil_program_cursor_advance(program, cursor);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_parse_global_variable_initializer_range(vigil_program_state_t *program,
                                                                            size_t *cursor,
                                                                            const vigil_token_t *name_token,
                                                                            size_t *initializer_start,
                                                                            size_t *initializer_end)
{
    const vigil_token_t *token;

    *initializer_start = *cursor;
    while (1)
    {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind == VIGIL_TOKEN_EOF)
        {
            return vigil_compile_report(program, vigil_program_eof_span(program),
                                        "expected ';' after global variable declaration");
        }
        if (token->kind == VIGIL_TOKEN_SEMICOLON)
        {
            break;
        }
        *cursor += 1U;
    }

    *initializer_end = *cursor;
    if (*initializer_start == *initializer_end)
    {
        return vigil_compile_report(program, name_token->span, "expected initializer expression for global variable");
    }

    vigil_program_cursor_advance(program, cursor);
    return VIGIL_STATUS_OK;
}

static void vigil_program_commit_global_variable(vigil_program_state_t *program,
                                                 const vigil_named_global_decl_t *declaration, int is_public,
                                                 size_t initializer_start, size_t initializer_end)
{
    vigil_global_variable_t *global = &program->globals[program->global_count];

    memset(global, 0, sizeof(*global));
    global->source_id = program->source->id;
    global->name = declaration->name_text;
    global->name_length = declaration->name_length;
    global->name_span = declaration->name_token->span;
    global->is_public = is_public;
    global->type = declaration->declared_type;
    global->source = program->source;
    global->tokens = program->tokens;
    global->initializer_start = initializer_start;
    global->initializer_end = initializer_end;
    program->global_count += 1U;
}

static vigil_status_t vigil_program_parse_constant_header(vigil_program_state_t *program, size_t *cursor,
                                                          vigil_named_global_decl_t *declaration)
{
    static const vigil_global_name_conflict_messages_t messages = {
        "global constant name conflicts with function", "global constant name conflicts with interface",
        "global constant name conflicts with enum",     "global constant name conflicts with class",
        "global constant is already declared",          "global constant name conflicts with global variable",
    };
    const vigil_token_t *const_token = vigil_program_cursor_peek(program, *cursor);
    vigil_status_t status;

    if (const_token == NULL || const_token->kind != VIGIL_TOKEN_CONST)
    {
        return vigil_compile_report(program, const_token == NULL ? vigil_program_eof_span(program) : const_token->span,
                                    "expected 'const'");
    }
    vigil_program_cursor_advance(program, cursor);

    declaration->type_token = vigil_program_cursor_peek(program, *cursor);
    status = vigil_program_parse_primitive_type_reference(
        program, cursor, "global constants must use a primitive value type", &declaration->declared_type);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    declaration->name_token = vigil_program_cursor_peek(program, *cursor);
    if (declaration->name_token == NULL || declaration->name_token->kind != VIGIL_TOKEN_IDENTIFIER)
    {
        return vigil_compile_report(program, declaration->type_token->span, "expected global constant name");
    }

    declaration->name_text = vigil_program_token_text(program, declaration->name_token, &declaration->name_length);
    status = vigil_program_check_global_name_conflicts(program, declaration->name_token, declaration->name_text,
                                                       declaration->name_length, &messages);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vigil_program_cursor_advance(program, cursor);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_parse_constant_initializer(vigil_program_state_t *program, size_t *cursor,
                                                               const vigil_token_t *name_token,
                                                               vigil_parser_type_t declared_type,
                                                               vigil_constant_result_t *value_result)
{
    const vigil_token_t *token;
    vigil_status_t status = vigil_program_parse_constant_expression(program, cursor, value_result);

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    if (!vigil_program_type_is_assignable(program, declared_type, value_result->type))
    {
        return vigil_compile_report(program, name_token->span, "initializer type does not match global constant type");
    }

    token = vigil_program_cursor_peek(program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_SEMICOLON)
    {
        return vigil_compile_report(program, token == NULL ? vigil_program_eof_span(program) : token->span,
                                    "expected ';' after global constant declaration");
    }

    vigil_program_cursor_advance(program, cursor);
    return VIGIL_STATUS_OK;
}

static void vigil_program_commit_global_constant(vigil_program_state_t *program,
                                                 const vigil_named_global_decl_t *declaration, int is_public,
                                                 vigil_constant_result_t *value_result)
{
    vigil_global_constant_t *constant = &program->constants[program->constant_count];

    memset(constant, 0, sizeof(*constant));
    constant->source_id = program->source->id;
    constant->name = declaration->name_text;
    constant->name_length = declaration->name_length;
    constant->name_span = declaration->name_token->span;
    constant->is_public = is_public;
    constant->type = declaration->declared_type;
    constant->value = value_result->value;
    value_result->type = vigil_binding_type_invalid();
    vigil_value_init_nil(&value_result->value);
    program->constant_count += 1U;
}

static vigil_status_t vigil_program_parse_enum_header(vigil_program_state_t *program, size_t *cursor, int is_public,
                                                      vigil_enum_decl_t **out_decl,
                                                      const vigil_token_t **out_name_token)
{
    static const vigil_global_name_conflict_messages_t messages = {
        "enum name conflicts with function",
        "enum name conflicts with interface",
        "enum is already declared",
        "enum name conflicts with class",
        "enum name conflicts with global constant",
        "enum name conflicts with global variable",
    };
    const vigil_token_t *enum_token;
    const vigil_token_t *name_token = NULL;
    const char *name_text;
    size_t name_length;
    vigil_enum_decl_t *decl;
    vigil_status_t status;

    enum_token = vigil_program_cursor_peek(program, *cursor);
    if (enum_token == NULL || enum_token->kind != VIGIL_TOKEN_ENUM)
    {
        return vigil_compile_report(program, enum_token == NULL ? vigil_program_eof_span(program) : enum_token->span,
                                    "expected 'enum'");
    }
    vigil_program_cursor_advance(program, cursor);

    name_token = vigil_program_cursor_peek(program, *cursor);
    if (name_token == NULL || name_token->kind != VIGIL_TOKEN_IDENTIFIER)
    {
        return vigil_compile_report(program, enum_token->span, "expected enum name");
    }

    name_text = vigil_program_token_text(program, name_token, &name_length);
    status = vigil_program_check_global_name_conflicts(program, name_token, name_text, name_length, &messages);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_program_grow_enums(program, program->enum_count + 1U);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    decl = &program->enums[program->enum_count];
    memset(decl, 0, sizeof(*decl));
    decl->source_id = program->source->id;
    decl->name = name_text;
    decl->name_length = name_length;
    decl->name_span = name_token->span;
    decl->is_public = is_public;
    program->enum_count += 1U;
    vigil_program_cursor_advance(program, cursor);

    *out_decl = decl;
    *out_name_token = name_token;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_expect_enum_body_start(vigil_program_state_t *program, size_t *cursor,
                                                           const vigil_token_t *name_token)
{
    const vigil_token_t *token = vigil_program_cursor_peek(program, *cursor);

    if (token == NULL || token->kind != VIGIL_TOKEN_LBRACE)
    {
        return vigil_compile_report(program, name_token->span, "expected '{' after enum name");
    }
    vigil_program_cursor_advance(program, cursor);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_parse_enum_member_value(vigil_program_state_t *program, size_t *cursor,
                                                            const vigil_token_t *member_token,
                                                            vigil_constant_result_t *value_result, int64_t *next_value)
{
    vigil_status_t status;

    vigil_program_cursor_advance(program, cursor);
    vigil_constant_result_release(value_result);
    vigil_constant_result_clear(value_result);
    status = vigil_program_parse_constant_expression(program, cursor, value_result);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    if (!vigil_parser_type_equal(value_result->type, vigil_binding_type_primitive(VIGIL_TYPE_I32)))
    {
        vigil_constant_result_release(value_result);
        vigil_constant_result_clear(value_result);
        return vigil_compile_report(program, member_token->span, "enum member value must be i32");
    }

    *next_value = vigil_value_as_int(&value_result->value);
    vigil_constant_result_release(value_result);
    vigil_constant_result_clear(value_result);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_append_enum_member(vigil_program_state_t *program, vigil_enum_decl_t *decl,
                                                       const vigil_token_t *member_token, const char *name_text,
                                                       size_t name_length, int64_t value)
{
    vigil_enum_member_t *member;
    vigil_status_t status;

    status = vigil_enum_decl_grow_members(program, decl, decl->member_count + 1U);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    member = &decl->members[decl->member_count];
    memset(member, 0, sizeof(*member));
    member->name = name_text;
    member->name_length = name_length;
    member->name_span = member_token->span;
    member->value = value;
    decl->member_count += 1U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_parse_enum_member_separator(vigil_program_state_t *program, size_t *cursor)
{
    const vigil_token_t *token = vigil_program_cursor_peek(program, *cursor);

    if (token == NULL)
    {
        return vigil_compile_report(program, vigil_program_eof_span(program), "expected '}' after enum body");
    }
    if (token->kind == VIGIL_TOKEN_COMMA)
    {
        vigil_program_cursor_advance(program, cursor);
        return VIGIL_STATUS_OK;
    }
    if (token->kind == VIGIL_TOKEN_RBRACE)
    {
        return VIGIL_STATUS_OK;
    }
    return vigil_compile_report(program, token->span, "expected ',' or '}' after enum member");
}

static vigil_status_t vigil_program_parse_enum_member(vigil_program_state_t *program, size_t *cursor,
                                                      vigil_enum_decl_t *decl, vigil_constant_result_t *value_result,
                                                      int64_t *next_value)
{
    const vigil_token_t *member_token;
    const vigil_token_t *token;
    const char *name_text;
    size_t name_length;
    vigil_status_t status;

    token = vigil_program_cursor_peek(program, *cursor);
    if (token == NULL)
    {
        return vigil_compile_report(program, vigil_program_eof_span(program), "expected '}' after enum body");
    }
    if (token->kind != VIGIL_TOKEN_IDENTIFIER)
    {
        return vigil_compile_report(program, token->span, "expected enum member name");
    }

    member_token = token;
    name_text = vigil_program_token_text(program, member_token, &name_length);
    if (vigil_enum_decl_find_member(decl, name_text, name_length, NULL, NULL))
    {
        return vigil_compile_report(program, member_token->span, "enum member is already declared");
    }
    vigil_program_cursor_advance(program, cursor);

    token = vigil_program_cursor_peek(program, *cursor);
    if (token != NULL && token->kind == VIGIL_TOKEN_ASSIGN)
    {
        status = vigil_program_parse_enum_member_value(program, cursor, member_token, value_result, next_value);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }

    status = vigil_program_append_enum_member(program, decl, member_token, name_text, name_length, *next_value);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    *next_value += 1;
    return vigil_program_parse_enum_member_separator(program, cursor);
}

vigil_status_t vigil_program_parse_global_variable_declaration(vigil_program_state_t *program, size_t *cursor,
                                                               int is_public)
{
    vigil_named_global_decl_t declaration;
    vigil_status_t status;
    size_t initializer_start = 0U;
    size_t initializer_end = 0U;

    memset(&declaration, 0, sizeof(declaration));
    status = vigil_program_parse_global_variable_header(program, cursor, &declaration);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_program_require_global_assignment(program, cursor, declaration.name_token,
                                                     "expected '=' in global variable declaration");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_program_parse_global_variable_initializer_range(program, cursor, declaration.name_token,
                                                                   &initializer_start, &initializer_end);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_program_grow_globals(program, program->global_count + 1U);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vigil_program_commit_global_variable(program, &declaration, is_public, initializer_start, initializer_end);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_program_parse_constant_declaration(vigil_program_state_t *program, size_t *cursor, int is_public)
{
    vigil_named_global_decl_t declaration;
    vigil_status_t status;
    vigil_constant_result_t value_result;

    vigil_constant_result_clear(&value_result);
    memset(&declaration, 0, sizeof(declaration));
    status = vigil_program_parse_constant_header(program, cursor, &declaration);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_program_require_global_assignment(program, cursor, declaration.name_token,
                                                     "expected '=' in global constant declaration");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_program_parse_constant_initializer(program, cursor, declaration.name_token,
                                                      declaration.declared_type, &value_result);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_constant_result_release(&value_result);
        return status;
    }

    status = vigil_program_grow_constants(program, program->constant_count + 1U);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_constant_result_release(&value_result);
        return status;
    }

    vigil_program_commit_global_constant(program, &declaration, is_public, &value_result);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_program_parse_enum_declaration(vigil_program_state_t *program, size_t *cursor, int is_public)
{
    vigil_status_t status;
    const vigil_token_t *name_token = NULL;
    const vigil_token_t *token;
    vigil_enum_decl_t *decl = NULL;
    vigil_constant_result_t value_result;
    int64_t next_value;

    vigil_constant_result_clear(&value_result);
    next_value = 0;
    status = vigil_program_parse_enum_header(program, cursor, is_public, &decl, &name_token);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_program_expect_enum_body_start(program, cursor, name_token);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    while (1)
    {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL)
        {
            return vigil_compile_report(program, vigil_program_eof_span(program), "expected '}' after enum body");
        }
        if (token->kind == VIGIL_TOKEN_RBRACE)
        {
            vigil_program_cursor_advance(program, cursor);
            break;
        }
        status = vigil_program_parse_enum_member(program, cursor, decl, &value_result, &next_value);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_constant_result_release(&value_result);
            return status;
        }
    }

    vigil_constant_result_release(&value_result);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_parse_interface_header(vigil_program_state_t *program, size_t *cursor,
                                                           int is_public, vigil_interface_decl_t **decl_out)
{
    static const vigil_global_name_conflict_messages_t messages = {
        "interface name conflicts with function",
        "interface is already declared",
        "interface name conflicts with enum",
        "interface name conflicts with class",
        "interface name conflicts with global constant",
        "interface name conflicts with global variable",
    };
    vigil_status_t status;
    const vigil_token_t *interface_token;
    const vigil_token_t *name_token = NULL;
    const char *name_text;
    size_t name_length;
    vigil_interface_decl_t *decl;

    interface_token = vigil_program_cursor_peek(program, *cursor);
    if (interface_token == NULL || interface_token->kind != VIGIL_TOKEN_INTERFACE)
    {
        return vigil_compile_report(program,
                                    interface_token == NULL ? vigil_program_eof_span(program) : interface_token->span,
                                    "expected 'interface'");
    }
    vigil_program_cursor_advance(program, cursor);

    name_token = vigil_program_cursor_peek(program, *cursor);
    if (name_token == NULL || name_token->kind != VIGIL_TOKEN_IDENTIFIER)
    {
        return vigil_compile_report(program, interface_token->span, "expected interface name");
    }

    name_text = vigil_program_token_text(program, name_token, &name_length);
    status = vigil_program_check_global_name_conflicts(program, name_token, name_text, name_length, &messages);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_program_grow_interfaces(program, program->interface_count + 1U);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    decl = &program->interfaces[program->interface_count];
    memset(decl, 0, sizeof(*decl));
    decl->source_id = program->source->id;
    decl->name = name_text;
    decl->name_length = name_length;
    decl->name_span = name_token->span;
    decl->is_public = is_public;
    program->interface_count += 1U;
    vigil_program_cursor_advance(program, cursor);

    *decl_out = decl;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_expect_interface_body_start(vigil_program_state_t *program, size_t *cursor,
                                                                const vigil_interface_decl_t *decl)
{
    const vigil_token_t *token = vigil_program_cursor_peek(program, *cursor);

    if (token == NULL || token->kind != VIGIL_TOKEN_LBRACE)
    {
        return vigil_compile_report(program, decl->name_span, "expected '{' after interface name");
    }

    vigil_program_cursor_advance(program, cursor);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_expect_interface_method_token(vigil_program_state_t *program, size_t *cursor,
                                                                  vigil_token_kind_t expected_kind,
                                                                  vigil_source_span_t error_span, const char *message)
{
    const vigil_token_t *token = vigil_program_cursor_peek(program, *cursor);

    if (token == NULL || token->kind != expected_kind)
    {
        return vigil_compile_report(program, error_span, message);
    }

    vigil_program_cursor_advance(program, cursor);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_parse_interface_method_header(vigil_program_state_t *program, size_t *cursor,
                                                                  vigil_interface_decl_t *decl,
                                                                  const vigil_token_t **method_name_token_out,
                                                                  vigil_interface_method_t **method_out)
{
    vigil_status_t status;
    const vigil_token_t *token;
    const vigil_token_t *method_name_token = NULL;
    const char *name_text;
    size_t name_length;
    vigil_interface_method_t *method;

    token = vigil_program_cursor_peek(program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_FN)
    {
        return vigil_compile_report(program, token == NULL ? vigil_program_eof_span(program) : token->span,
                                    "expected interface method declaration");
    }
    vigil_program_cursor_advance(program, cursor);

    method_name_token = vigil_program_cursor_peek(program, *cursor);
    if (method_name_token == NULL || method_name_token->kind != VIGIL_TOKEN_IDENTIFIER)
    {
        return vigil_compile_report(program, token->span, "expected method name");
    }

    name_text = vigil_program_token_text(program, method_name_token, &name_length);
    if (vigil_interface_decl_find_method(decl, name_text, name_length, NULL, NULL))
    {
        return vigil_compile_report(program, method_name_token->span, "interface method is already declared");
    }
    vigil_program_cursor_advance(program, cursor);

    status = vigil_interface_decl_grow_methods(program, decl, decl->method_count + 1U);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    method = &decl->methods[decl->method_count];
    memset(method, 0, sizeof(*method));
    method->name = name_text;
    method->name_length = name_length;
    method->name_span = method_name_token->span;
    decl->method_count += 1U;

    *method_name_token_out = method_name_token;
    *method_out = method;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_parse_interface_method_param(vigil_program_state_t *program, size_t *cursor,
                                                                 const vigil_token_t *method_name_token,
                                                                 vigil_interface_method_t *method)
{
    vigil_status_t status;
    const vigil_token_t *param_type_token;
    const vigil_token_t *param_name_token;
    vigil_parser_type_t param_type;

    param_type_token = vigil_program_cursor_peek(program, *cursor);
    status = vigil_program_parse_type_reference(program, cursor, "unsupported function parameter type", &param_type);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_program_require_non_void_type(
        program, param_type_token == NULL ? method_name_token->span : param_type_token->span, param_type,
        "function parameters cannot use type void");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    param_name_token = vigil_program_cursor_peek(program, *cursor);
    if (param_name_token == NULL || param_name_token->kind != VIGIL_TOKEN_IDENTIFIER)
    {
        return vigil_compile_report(program,
                                    param_type_token == NULL ? method_name_token->span : param_type_token->span,
                                    "expected parameter name");
    }
    vigil_program_cursor_advance(program, cursor);

    status = vigil_interface_method_grow_params(program, method, method->param_count + 1U);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    method->param_types[method->param_count] = param_type;
    method->param_count += 1U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_parse_interface_method_params(vigil_program_state_t *program, size_t *cursor,
                                                                  const vigil_token_t *method_name_token,
                                                                  vigil_interface_method_t *method)
{
    vigil_status_t status;
    const vigil_token_t *token;

    token = vigil_program_cursor_peek(program, *cursor);
    if (token == NULL || token->kind == VIGIL_TOKEN_RPAREN)
    {
        return VIGIL_STATUS_OK;
    }

    while (1)
    {
        status = vigil_program_parse_interface_method_param(program, cursor, method_name_token, method);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_COMMA)
        {
            break;
        }
        vigil_program_cursor_advance(program, cursor);
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_parse_interface_method(vigil_program_state_t *program, size_t *cursor,
                                                           vigil_interface_decl_t *decl)
{
    vigil_status_t status;
    const vigil_token_t *method_name_token = NULL;
    vigil_interface_method_t *method;

    method_name_token = NULL;
    method = NULL;
    status = vigil_program_parse_interface_method_header(program, cursor, decl, &method_name_token, &method);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_program_expect_interface_method_token(program, cursor, VIGIL_TOKEN_LPAREN, method_name_token->span,
                                                         "expected '(' after method name");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_program_parse_interface_method_params(program, cursor, method_name_token, method);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_program_expect_interface_method_token(program, cursor, VIGIL_TOKEN_RPAREN, method_name_token->span,
                                                         "expected ')' after parameter list");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_program_expect_interface_method_token(program, cursor, VIGIL_TOKEN_ARROW, method_name_token->span,
                                                         "expected '->' after method signature");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status =
        vigil_program_parse_interface_method_return_types(program, cursor, "unsupported function return type", method);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    return vigil_program_expect_interface_method_token(program, cursor, VIGIL_TOKEN_SEMICOLON, method_name_token->span,
                                                       "expected ';' after interface method");
}

vigil_status_t vigil_program_parse_interface_declaration(vigil_program_state_t *program, size_t *cursor, int is_public)
{
    vigil_status_t status;
    const vigil_token_t *token;
    vigil_interface_decl_t *decl = NULL;

    status = vigil_program_parse_interface_header(program, cursor, is_public, &decl);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    if (decl == NULL)
    {
        return vigil_compile_report(program, vigil_program_eof_span(program), "interface declaration is missing");
    }
    status = vigil_program_expect_interface_body_start(program, cursor, decl);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    while (1)
    {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL)
        {
            return vigil_compile_report(program, vigil_program_eof_span(program), "expected '}' after interface body");
        }
        if (token->kind == VIGIL_TOKEN_RBRACE)
        {
            vigil_program_cursor_advance(program, cursor);
            break;
        }
        status = vigil_program_parse_interface_method(program, cursor, decl);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_program_validate_class_interface_conformance(vigil_program_state_t *program,
                                                                  vigil_class_decl_t *decl)
{
    vigil_status_t status;
    size_t i;

    for (i = 0U; i < decl->implemented_interface_count; i += 1U)
    {
        const vigil_interface_decl_t *interface_decl;
        vigil_class_interface_impl_t *impl;
        size_t j;

        interface_decl = &program->interfaces[decl->implemented_interfaces[i]];
        status = vigil_class_decl_grow_interface_impls(program, decl, decl->interface_impl_count + 1U);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        impl = &decl->interface_impls[decl->interface_impl_count];
        memset(impl, 0, sizeof(*impl));
        impl->interface_index = decl->implemented_interfaces[i];
        impl->function_count = interface_decl->method_count;
        decl->interface_impl_count += 1U;
        if (impl->function_count != 0U)
        {
            void *memory = NULL;
            status =
                vigil_runtime_alloc(program->registry->runtime, impl->function_count * sizeof(*impl->function_indices),
                                    &memory, program->error);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }
            impl->function_indices = (size_t *)memory;
        }

        for (j = 0U; j < interface_decl->method_count; j += 1U)
        {
            const vigil_interface_method_t *interface_method = &interface_decl->methods[j];
            const vigil_class_method_t *class_method;
            const vigil_function_decl_t *method_decl;
            size_t param_index;

            if (!vigil_class_decl_find_method(decl, interface_method->name, interface_method->name_length, NULL,
                                              &class_method))
            {
                return vigil_compile_report(program, decl->name_span,
                                            "class does not implement required interface method");
            }

            method_decl = vigil_binding_function_table_get(&program->functions, class_method->function_index);
            if (method_decl == NULL || method_decl->return_count != interface_method->return_count ||
                method_decl->param_count != interface_method->param_count + 1U)
            {
                return vigil_compile_report(program, class_method->name_span,
                                            "class method signature does not match interface");
            }

            for (param_index = 0U; param_index < interface_method->return_count; param_index += 1U)
            {
                if (!vigil_parser_type_equal(vigil_function_return_type_at(method_decl, param_index),
                                             vigil_interface_method_return_type_at(interface_method, param_index)))
                {
                    return vigil_compile_report(program, class_method->name_span,
                                                "class method signature does not match interface");
                }
            }

            for (param_index = 0U; param_index < interface_method->param_count; param_index += 1U)
            {
                if (!vigil_parser_type_equal(method_decl->params[param_index + 1U].type,
                                             interface_method->param_types[param_index]))
                {
                    return vigil_compile_report(program, class_method->name_span,
                                                "class method signature does not match interface");
                }
            }

            impl->function_indices[j] = class_method->function_index;
        }
    }

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_program_synthesize_class_constructor(vigil_program_state_t *program, vigil_class_decl_t *decl)
{
    vigil_status_t status;
    const vigil_class_method_t *init_method;
    const vigil_function_decl_t *init_decl;
    vigil_function_decl_t *ctor_decl;
    size_t class_index;
    size_t ctor_index;
    size_t param_index;

    init_method = NULL;
    init_decl = NULL;
    class_index = (size_t)(decl - program->classes);
    if (!vigil_class_decl_find_method(decl, "init", 4U, NULL, &init_method))
    {
        return VIGIL_STATUS_OK;
    }

    init_decl = vigil_binding_function_table_get(&program->functions, init_method->function_index);
    if (init_decl == NULL)
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_INTERNAL, "class init declaration is missing");
        return VIGIL_STATUS_INTERNAL;
    }
    if (init_decl->return_count != 1U ||
        (!vigil_parser_type_is_void(init_decl->return_type) && !vigil_parser_type_is_err(init_decl->return_type)))
    {
        return vigil_compile_report(program, init_method->name_span, "init methods must return void or err");
    }

    status = vigil_program_grow_functions(program, program->functions.count + 1U);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    ctor_index = program->functions.count;
    ctor_decl = &program->functions.functions[ctor_index];
    vigil_binding_function_init(ctor_decl);
    ctor_decl->name = decl->name;
    ctor_decl->name_length = decl->name_length;
    ctor_decl->name_span = decl->name_span;
    ctor_decl->is_public = decl->is_public;
    status = vigil_binding_function_add_return_type(program->registry->runtime, ctor_decl,
                                                    vigil_binding_type_class(class_index), program->error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_binding_function_free(program->registry->runtime, ctor_decl);
        return status;
    }
    if (vigil_parser_type_is_err(init_decl->return_type))
    {
        status = vigil_binding_function_add_return_type(program->registry->runtime, ctor_decl,
                                                        vigil_binding_type_primitive(VIGIL_TYPE_ERR), program->error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_binding_function_free(program->registry->runtime, ctor_decl);
            return status;
        }
    }
    ctor_decl->source = program->source;
    ctor_decl->tokens = program->tokens;

    init_decl = vigil_binding_function_table_get(&program->functions, init_method->function_index);
    if (init_decl == NULL)
    {
        vigil_binding_function_free(program->registry->runtime, ctor_decl);
        vigil_error_set_literal(program->error, VIGIL_STATUS_INTERNAL, "class init declaration is missing");
        return VIGIL_STATUS_INTERNAL;
    }

    for (param_index = 1U; param_index < init_decl->param_count; param_index += 1U)
    {
        status = vigil_program_add_binding_param(
            program, ctor_decl, init_decl->params[param_index].name, init_decl->params[param_index].length,
            init_decl->params[param_index].span, init_decl->params[param_index].type);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_binding_function_free(program->registry->runtime, ctor_decl);
            return status;
        }
    }

    decl->constructor_function_index = ctor_index;
    program->functions.count += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_program_parse_class_declaration(vigil_program_state_t *program, size_t *cursor, int is_public)
{
    vigil_status_t status;
    const vigil_token_t *class_token;
    const vigil_token_t *name_token = NULL;
    const vigil_token_t *type_token = NULL;
    const vigil_token_t *field_name_token;
    const vigil_token_t *param_name_token;
    const char *name_text;
    size_t name_length;
    size_t class_index;
    size_t interface_index;
    size_t body_depth;
    vigil_class_decl_t *decl;
    vigil_class_field_t *field;
    vigil_class_method_t *method;
    vigil_function_decl_t *method_decl;
    vigil_parser_type_t field_type;
    int member_is_public;

    class_token = vigil_program_cursor_peek(program, *cursor);
    if (class_token == NULL || class_token->kind != VIGIL_TOKEN_CLASS)
    {
        return vigil_compile_report(program, class_token == NULL ? vigil_program_eof_span(program) : class_token->span,
                                    "expected 'class'");
    }
    vigil_program_cursor_advance(program, cursor);

    name_token = vigil_program_cursor_peek(program, *cursor);
    if (name_token == NULL || name_token->kind != VIGIL_TOKEN_IDENTIFIER)
    {
        return vigil_compile_report(program, class_token->span, "expected class name");
    }
    name_text = vigil_program_token_text(program, name_token, &name_length);
    if (program->compile_mode != VIGIL_COMPILE_MODE_REPL)
    {
        if (vigil_program_find_class_in_source(program, program->source->id, name_text, name_length, NULL, NULL))
        {
            return vigil_compile_report(program, name_token->span, "class is already declared");
        }
        if (vigil_program_find_constant_in_source(program, program->source->id, name_text, name_length, NULL))
        {
            return vigil_compile_report(program, name_token->span, "class name conflicts with global constant");
        }
        if (vigil_program_find_enum_in_source(program, program->source->id, name_text, name_length, NULL, NULL))
        {
            return vigil_compile_report(program, name_token->span, "class name conflicts with enum");
        }
        if (vigil_program_find_global_in_source(program, program->source->id, name_text, name_length, NULL, NULL))
        {
            return vigil_compile_report(program, name_token->span, "class name conflicts with global variable");
        }
        if (vigil_program_find_top_level_function_name_in_source(program, program->source->id, name_text, name_length,
                                                                 NULL, NULL))
        {
            return vigil_compile_report(program, name_token->span, "class name conflicts with function");
        }
        if (vigil_program_find_interface_in_source(program, program->source->id, name_text, name_length, NULL, NULL))
        {
            return vigil_compile_report(program, name_token->span, "class name conflicts with interface");
        }
    } /* end REPL redefinition guard */

    status = vigil_program_grow_classes(program, program->class_count + 1U);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    class_index = program->class_count;
    decl = &program->classes[program->class_count];
    memset(decl, 0, sizeof(*decl));
    decl->constructor_function_index = (size_t)-1;
    decl->source_id = program->source->id;
    decl->name = name_text;
    decl->name_length = name_length;
    decl->name_span = name_token->span;
    decl->is_public = is_public;
    program->class_count += 1U;
    vigil_program_cursor_advance(program, cursor);

    type_token = vigil_program_cursor_peek(program, *cursor);
    if (vigil_program_token_is_identifier_text(program, type_token, "implements", 10U))
    {
        vigil_program_cursor_advance(program, cursor);
        while (1)
        {
            status = vigil_program_parse_type_reference(program, cursor, "unknown interface", &field_type);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }
            if (!vigil_parser_type_is_interface(field_type))
            {
                type_token = vigil_program_cursor_peek(program, *cursor);
                return vigil_compile_report(program, type_token == NULL ? name_token->span : type_token->span,
                                            "unknown interface");
            }
            interface_index = field_type.object_index;
            if (vigil_class_decl_implements_interface(decl, interface_index))
            {
                return vigil_compile_report(program, vigil_program_cursor_peek(program, *cursor - 1U)->span,
                                            "interface is already implemented");
            }
            status =
                vigil_class_decl_grow_implemented_interfaces(program, decl, decl->implemented_interface_count + 1U);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }
            decl->implemented_interfaces[decl->implemented_interface_count] = interface_index;
            decl->implemented_interface_count += 1U;

            type_token = vigil_program_cursor_peek(program, *cursor);
            if (type_token == NULL || type_token->kind != VIGIL_TOKEN_COMMA)
            {
                break;
            }
            vigil_program_cursor_advance(program, cursor);
        }
    }

    type_token = vigil_program_cursor_peek(program, *cursor);
    if (type_token == NULL || type_token->kind != VIGIL_TOKEN_LBRACE)
    {
        return vigil_compile_report(program, name_token->span, "expected '{' after class name");
    }
    vigil_program_cursor_advance(program, cursor);

    while (1)
    {
        type_token = vigil_program_cursor_peek(program, *cursor);
        if (type_token == NULL)
        {
            return vigil_compile_report(program, vigil_program_eof_span(program), "expected '}' after class body");
        }
        if (type_token->kind == VIGIL_TOKEN_RBRACE)
        {
            vigil_program_cursor_advance(program, cursor);
            break;
        }

        member_is_public = vigil_program_parse_optional_pub(program, cursor);
        type_token = vigil_program_cursor_peek(program, *cursor);
        if (type_token == NULL)
        {
            return vigil_compile_report(program, vigil_program_eof_span(program), "expected class member declaration");
        }

        if (type_token->kind == VIGIL_TOKEN_FN && vigil_program_token_at(program, *cursor + 1U) != NULL &&
            vigil_program_token_at(program, *cursor + 1U)->kind == VIGIL_TOKEN_IDENTIFIER &&
            vigil_program_token_at(program, *cursor + 2U) != NULL &&
            vigil_program_token_at(program, *cursor + 2U)->kind == VIGIL_TOKEN_LPAREN)
        {
            vigil_program_cursor_advance(program, cursor);

            name_token = vigil_program_cursor_peek(program, *cursor);
            if (name_token == NULL || name_token->kind != VIGIL_TOKEN_IDENTIFIER)
            {
                return vigil_compile_report(program, type_token->span, "expected method name");
            }
            name_text = vigil_program_token_text(program, name_token, &name_length);
            if (vigil_class_decl_find_method(decl, name_text, name_length, NULL, NULL))
            {
                return vigil_compile_report(program, name_token->span, "class method is already declared");
            }
            if (vigil_class_decl_find_field(decl, name_text, name_length, NULL, NULL))
            {
                return vigil_compile_report(program, name_token->span, "class method conflicts with field");
            }

            status = vigil_program_grow_functions(program, program->functions.count + 1U);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }
            method_decl = &program->functions.functions[program->functions.count];
            vigil_binding_function_init(method_decl);
            method_decl->name = name_text;
            method_decl->name_length = name_length;
            method_decl->name_span = name_token->span;
            method_decl->is_public = member_is_public;
            method_decl->owner_class_index = class_index;
            method_decl->source = program->source;
            method_decl->tokens = program->tokens;
            cursor[0] += 1U;

            status = vigil_program_add_binding_param(program, method_decl, "self", 4U, name_token->span,
                                                     vigil_binding_type_class(class_index));
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }

            type_token = vigil_program_token_at(program, *cursor);
            if (type_token == NULL || type_token->kind != VIGIL_TOKEN_LPAREN)
            {
                return vigil_compile_report(program, name_token->span, "expected '(' after method name");
            }
            cursor[0] += 1U;

            type_token = vigil_program_token_at(program, *cursor);
            if (type_token != NULL && type_token->kind != VIGIL_TOKEN_RPAREN)
            {
                while (1)
                {
                    status = vigil_program_parse_type_reference(program, cursor, "unsupported function parameter type",
                                                                &field_type);
                    if (status != VIGIL_STATUS_OK)
                    {
                        return status;
                    }
                    status = vigil_program_require_non_void_type(
                        program, type_token == NULL ? name_token->span : type_token->span, field_type,
                        "function parameters cannot use type void");
                    if (status != VIGIL_STATUS_OK)
                    {
                        return status;
                    }

                    param_name_token = vigil_program_token_at(program, *cursor);
                    if (param_name_token == NULL || param_name_token->kind != VIGIL_TOKEN_IDENTIFIER)
                    {
                        return vigil_compile_report(program, type_token->span, "expected parameter name");
                    }

                    status = vigil_program_add_param(program, method_decl, field_type, param_name_token);
                    if (status != VIGIL_STATUS_OK)
                    {
                        return status;
                    }
                    cursor[0] += 1U;

                    type_token = vigil_program_token_at(program, *cursor);
                    if (type_token != NULL && type_token->kind == VIGIL_TOKEN_COMMA)
                    {
                        cursor[0] += 1U;
                        type_token = vigil_program_token_at(program, *cursor);
                        continue;
                    }
                    break;
                }
            }

            type_token = vigil_program_token_at(program, *cursor);
            if (type_token == NULL || type_token->kind != VIGIL_TOKEN_RPAREN)
            {
                return vigil_compile_report(program, name_token->span, "expected ')' after parameter list");
            }
            cursor[0] += 1U;

            type_token = vigil_program_token_at(program, *cursor);
            if (type_token == NULL || type_token->kind != VIGIL_TOKEN_ARROW)
            {
                return vigil_compile_report(program, name_token->span, "expected '->' after method signature");
            }
            cursor[0] += 1U;

            status = vigil_program_parse_function_return_types(program, cursor, "unsupported function return type",
                                                               method_decl);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }

            type_token = vigil_program_token_at(program, *cursor);
            if (type_token == NULL || type_token->kind != VIGIL_TOKEN_LBRACE)
            {
                return vigil_compile_report(program, name_token->span, "expected '{' before method body");
            }
            cursor[0] += 1U;
            method_decl->body_start = *cursor;

            body_depth = 1U;
            while (body_depth > 0U)
            {
                type_token = vigil_program_token_at(program, *cursor);
                if (type_token == NULL || type_token->kind == VIGIL_TOKEN_EOF)
                {
                    return vigil_compile_report(program, vigil_program_eof_span(program),
                                                "expected '}' after method body");
                }

                if (type_token->kind == VIGIL_TOKEN_LBRACE)
                {
                    body_depth += 1U;
                }
                else if (type_token->kind == VIGIL_TOKEN_RBRACE)
                {
                    body_depth -= 1U;
                    if (body_depth == 0U)
                    {
                        method_decl->body_end = *cursor;
                        cursor[0] += 1U;
                        break;
                    }
                }
                cursor[0] += 1U;
            }

            status = vigil_class_decl_grow_methods(program, decl, decl->method_count + 1U);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }
            method = &decl->methods[decl->method_count];
            memset(method, 0, sizeof(*method));
            method->name = name_text;
            method->name_length = name_length;
            method->name_span = name_token->span;
            method->is_public = member_is_public;
            method->function_index = program->functions.count;
            decl->method_count += 1U;
            program->functions.count += 1U;
            continue;
        }

        status = vigil_program_parse_type_reference(program, cursor, "unsupported class field type", &field_type);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        status = vigil_program_require_non_void_type(program, type_token == NULL ? name_token->span : type_token->span,
                                                     field_type, "class fields cannot use type void");
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        field_name_token = vigil_program_cursor_peek(program, *cursor);
        if (field_name_token == NULL || field_name_token->kind != VIGIL_TOKEN_IDENTIFIER)
        {
            return vigil_compile_report(program, type_token->span, "expected field name");
        }
        name_text = vigil_program_token_text(program, field_name_token, &name_length);
        if (vigil_class_decl_find_field(decl, name_text, name_length, NULL, NULL))
        {
            return vigil_compile_report(program, field_name_token->span, "class field is already declared");
        }
        vigil_program_cursor_advance(program, cursor);

        type_token = vigil_program_cursor_peek(program, *cursor);
        if (type_token == NULL || type_token->kind != VIGIL_TOKEN_SEMICOLON)
        {
            return vigil_compile_report(program, field_name_token->span, "expected ';' after class field");
        }
        vigil_program_cursor_advance(program, cursor);

        status = vigil_class_decl_grow_fields(program, decl, decl->field_count + 1U);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        field = &decl->fields[decl->field_count];
        memset(field, 0, sizeof(*field));
        field->name = name_text;
        field->name_length = name_length;
        field->name_span = field_name_token->span;
        field->is_public = member_is_public;
        field->type = field_type;
        decl->field_count += 1U;
    }

    status = vigil_program_validate_class_interface_conformance(program, decl);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    return vigil_program_synthesize_class_constructor(program, decl);
}
