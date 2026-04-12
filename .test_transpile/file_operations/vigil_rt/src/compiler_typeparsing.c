#include "internal/vigil_compiler_types.h"
#include "internal/vigil_internal.h"
#include <string.h>

static vigil_source_span_t token_span_or_eof(const vigil_token_t *token, const vigil_program_state_t *program)
{
    return token == NULL ? vigil_program_eof_span(program) : token->span;
}

vigil_status_t vigil_program_parse_type_name(const vigil_program_state_t *program, const vigil_token_t *token,
                                             const char *unsupported_message, vigil_parser_type_t *out_type)
{
    const char *text;
    size_t length;
    vigil_type_kind_t type_kind;
    size_t object_index;

    text = vigil_program_token_text(program, token, &length);
    type_kind = vigil_type_kind_from_name(text, length);
    if (type_kind == VIGIL_TYPE_I32 || type_kind == VIGIL_TYPE_I64 || type_kind == VIGIL_TYPE_U8 ||
        type_kind == VIGIL_TYPE_U32 || type_kind == VIGIL_TYPE_U64 || type_kind == VIGIL_TYPE_F64 ||
        type_kind == VIGIL_TYPE_BOOL || type_kind == VIGIL_TYPE_STRING || type_kind == VIGIL_TYPE_ERR ||
        type_kind == VIGIL_TYPE_VOID)
    {
        *out_type = vigil_binding_type_primitive(type_kind);
        return VIGIL_STATUS_OK;
    }
    if (type_kind == VIGIL_TYPE_NIL)
    {
        return vigil_compile_report(program, token->span, unsupported_message);
    }
    if (program->source != NULL &&
        vigil_program_find_class_in_source(program, program->source->id, text, length, &object_index, NULL))
    {
        *out_type = vigil_binding_type_class(object_index);
        return VIGIL_STATUS_OK;
    }
    if (program->source != NULL &&
        vigil_program_find_interface_in_source(program, program->source->id, text, length, &object_index, NULL))
    {
        *out_type = vigil_binding_type_interface(object_index);
        return VIGIL_STATUS_OK;
    }
    if (program->source != NULL &&
        vigil_program_find_enum_in_source(program, program->source->id, text, length, &object_index, NULL))
    {
        *out_type = vigil_binding_type_enum(object_index);
        return VIGIL_STATUS_OK;
    }

    return vigil_compile_report(program, token->span, unsupported_message);
}

/* Track whether a '>>' token was split and the second '>' is pending. */
int vigil_type_close_pending = 0;

/* Consume a closing '>' in a generic type context.  When the lexer has
   produced a '>>' (SHIFT_RIGHT) token, consume the whole token and set
   a pending flag so the outer type parser's next close-check succeeds
   without consuming another token. */
int vigil_program_consume_type_close(const vigil_program_state_t *program, size_t *cursor)
{
    const vigil_token_t *t;
    if (vigil_type_close_pending)
    {
        vigil_type_close_pending = 0;
        return 1;
    }
    t = vigil_program_token_at(program, *cursor);
    if (t == NULL)
    {
        return 0;
    }
    if (t->kind == VIGIL_TOKEN_GREATER)
    {
        *cursor += 1U;
        return 1;
    }
    if (t->kind == VIGIL_TOKEN_SHIFT_RIGHT)
    {
        *cursor += 1U;
        vigil_type_close_pending = 1;
        return 1;
    }
    return 0;
}

static vigil_status_t parse_fn_type_params(const vigil_program_state_t *program, size_t *cursor,
                                           const char *unsupported_message, const vigil_token_t *fn_token,
                                           vigil_function_type_decl_t *ft)
{
    vigil_status_t status;
    vigil_parser_type_t parsed_type;
    const vigil_token_t *next_token;

    next_token = vigil_program_token_at(program, *cursor);
    if (next_token != NULL && next_token->kind != VIGIL_TOKEN_RPAREN)
    {
        while (1)
        {
            status = vigil_program_parse_type_reference(program, cursor, unsupported_message, &parsed_type);
            if (status != VIGIL_STATUS_OK)
                return status;
            // clang-format off
            status = vigil_program_require_non_void_type(program, token_span_or_eof(vigil_program_token_at(program, *cursor - 1U), program), parsed_type, "function type parameters cannot use type void");
            // clang-format on
            if (status != VIGIL_STATUS_OK)
                return status;
            status = vigil_function_type_decl_add_param((vigil_program_state_t *)program, ft, parsed_type);
            if (status != VIGIL_STATUS_OK)
                return status;
            next_token = vigil_program_token_at(program, *cursor);
            if (next_token != NULL && next_token->kind == VIGIL_TOKEN_COMMA)
            {
                *cursor += 1U;
                continue;
            }
            break;
        }
    }
    next_token = vigil_program_token_at(program, *cursor);
    if (next_token == NULL || next_token->kind != VIGIL_TOKEN_RPAREN)
        return vigil_compile_report(program, next_token == NULL ? fn_token->span : next_token->span,
                                    "expected ')' after function type parameters");
    *cursor += 1U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t parse_fn_type_returns(const vigil_program_state_t *program, size_t *cursor,
                                            const char *unsupported_message, const vigil_token_t *fn_token,
                                            vigil_function_type_decl_t *ft)
{
    vigil_status_t status;
    vigil_parser_type_t parsed_type;
    const vigil_token_t *next_token;

    next_token = vigil_program_token_at(program, *cursor);
    if (next_token == NULL || next_token->kind != VIGIL_TOKEN_ARROW)
        return vigil_function_type_decl_add_return((vigil_program_state_t *)program, ft,
                                                   vigil_binding_type_primitive(VIGIL_TYPE_VOID));

    *cursor += 1U;
    next_token = vigil_program_token_at(program, *cursor);
    if (next_token != NULL && next_token->kind == VIGIL_TOKEN_LPAREN)
    {
        *cursor += 1U;
        while (1)
        {
            status = vigil_program_parse_type_reference(program, cursor, unsupported_message, &parsed_type);
            if (status != VIGIL_STATUS_OK)
                return status;
            status = vigil_function_type_decl_add_return((vigil_program_state_t *)program, ft, parsed_type);
            if (status != VIGIL_STATUS_OK)
                return status;
            next_token = vigil_program_token_at(program, *cursor);
            if (next_token != NULL && next_token->kind == VIGIL_TOKEN_COMMA)
            {
                *cursor += 1U;
                continue;
            }
            if (next_token != NULL && next_token->kind == VIGIL_TOKEN_RPAREN)
            {
                *cursor += 1U;
                return VIGIL_STATUS_OK;
            }
            return vigil_compile_report(program, next_token == NULL ? fn_token->span : next_token->span,
                                        "expected ')' after function type returns");
        }
    }

    status = vigil_program_parse_type_reference(program, cursor, unsupported_message, &parsed_type);
    if (status != VIGIL_STATUS_OK)
        return status;
    return vigil_function_type_decl_add_return((vigil_program_state_t *)program, ft, parsed_type);
}

static vigil_status_t parse_function_type_reference(const vigil_program_state_t *program, size_t *cursor,
                                                    const char *unsupported_message, const vigil_token_t *fn_token,
                                                    vigil_parser_type_t *out_type)
{
    vigil_status_t status;
    vigil_function_type_decl_t ft;
    const vigil_token_t *next_token;

    memset(&ft, 0, sizeof(ft));
    *cursor += 1U;
    next_token = vigil_program_token_at(program, *cursor);
    if (next_token == NULL || next_token->kind != VIGIL_TOKEN_LPAREN)
    {
        ft.is_any = 1;
        ft.return_type = vigil_binding_type_primitive(VIGIL_TYPE_VOID);
        status = vigil_program_intern_function_type((vigil_program_state_t *)program, &ft, out_type);
        vigil_function_type_decl_free((vigil_program_state_t *)program, &ft);
        return status;
    }
    *cursor += 1U;

    status = parse_fn_type_params(program, cursor, unsupported_message, fn_token, &ft);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_function_type_decl_free((vigil_program_state_t *)program, &ft);
        return status;
    }
    status = parse_fn_type_returns(program, cursor, unsupported_message, fn_token, &ft);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_function_type_decl_free((vigil_program_state_t *)program, &ft);
        return status;
    }
    status = vigil_program_intern_function_type((vigil_program_state_t *)program, &ft, out_type);
    vigil_function_type_decl_free((vigil_program_state_t *)program, &ft);
    return status;
}

static vigil_status_t parse_array_type_reference(const vigil_program_state_t *program, size_t *cursor,
                                                 const char *unsupported_message, const vigil_token_t *token,
                                                 vigil_parser_type_t *out_type)
{
    vigil_status_t status;
    vigil_parser_type_t element_type = vigil_binding_type_invalid();
    const vigil_token_t *next_token;

    *cursor += 2U;
    next_token = vigil_program_token_at(program, *cursor);
    if (next_token != NULL && (next_token->kind == VIGIL_TOKEN_GREATER || next_token->kind == VIGIL_TOKEN_SHIFT_RIGHT))
        return vigil_compile_report(program, token->span, "array types require an element type");
    status = vigil_program_parse_type_reference(program, cursor, unsupported_message, &element_type);
    if (status != VIGIL_STATUS_OK)
        return status;
    if (!vigil_program_consume_type_close(program, cursor))
    {
        next_token = vigil_program_token_at(program, *cursor);
        return vigil_compile_report(program, next_token == NULL ? token->span : next_token->span,
                                    "expected '>' after array element type");
    }
    return vigil_program_intern_array_type((vigil_program_state_t *)program, element_type, out_type);
}

static vigil_status_t parse_map_type_reference(const vigil_program_state_t *program, size_t *cursor,
                                               const char *unsupported_message, const vigil_token_t *token,
                                               vigil_parser_type_t *out_type)
{
    vigil_status_t status;
    vigil_parser_type_t key_type = vigil_binding_type_invalid();
    vigil_parser_type_t value_type = vigil_binding_type_invalid();
    const vigil_token_t *next_token;

    *cursor += 2U;
    status = vigil_program_parse_type_reference(program, cursor, unsupported_message, &key_type);
    if (status != VIGIL_STATUS_OK)
        return status;
    if (!vigil_parser_type_supports_map_key(key_type))
        return vigil_compile_report(program, token->span, "map keys must use an integer, bool, string, or enum type");
    next_token = vigil_program_token_at(program, *cursor);
    if (next_token == NULL || next_token->kind != VIGIL_TOKEN_COMMA)
        return vigil_compile_report(program, next_token == NULL ? token->span : next_token->span,
                                    "expected ',' after map key type");
    *cursor += 1U;
    status = vigil_program_parse_type_reference(program, cursor, unsupported_message, &value_type);
    if (status != VIGIL_STATUS_OK)
        return status;
    if (!vigil_program_consume_type_close(program, cursor))
    {
        next_token = vigil_program_token_at(program, *cursor);
        return vigil_compile_report(program, next_token == NULL ? token->span : next_token->span,
                                    "expected '>' after map value type");
    }
    return vigil_program_intern_map_type((vigil_program_state_t *)program, key_type, value_type, out_type);
}

static vigil_status_t parse_qualified_type_reference(const vigil_program_state_t *program, size_t *cursor,
                                                     const char *unsupported_message, const vigil_token_t *token,
                                                     const char *name_text, size_t name_length,
                                                     vigil_parser_type_t *out_type)
{
    const vigil_token_t *member_token;
    const char *member_text;
    size_t member_length;
    size_t object_index;
    vigil_source_id_t source_id;

    member_token = vigil_program_token_at(program, *cursor + 2U);
    if (member_token == NULL || member_token->kind != VIGIL_TOKEN_IDENTIFIER)
        return vigil_compile_report(program, vigil_program_token_at(program, *cursor + 1U)->span, unsupported_message);

    if (!vigil_program_resolve_import_alias(program, name_text, name_length, &source_id))
        return vigil_compile_report(program, token->span, unsupported_message);

    member_text = vigil_program_token_text(program, member_token, &member_length);
    if (vigil_program_find_class_in_source(program, source_id, member_text, member_length, &object_index, NULL))
    {
        if (!vigil_program_is_class_public(&program->classes[object_index]))
            return vigil_compile_report(program, member_token->span, "module member is not public");
        *out_type = vigil_binding_type_class(object_index);
        *cursor += 3U;
        return VIGIL_STATUS_OK;
    }
    if (vigil_program_find_interface_in_source(program, source_id, member_text, member_length, &object_index, NULL))
    {
        if (!vigil_program_is_interface_public(&program->interfaces[object_index]))
            return vigil_compile_report(program, member_token->span, "module member is not public");
        *out_type = vigil_binding_type_interface(object_index);
        *cursor += 3U;
        return VIGIL_STATUS_OK;
    }
    if (vigil_program_find_enum_in_source(program, source_id, member_text, member_length, &object_index, NULL))
    {
        if (!vigil_program_is_enum_public(&program->enums[object_index]))
            return vigil_compile_report(program, member_token->span, "module member is not public");
        *out_type = vigil_binding_type_enum(object_index);
        *cursor += 3U;
        return VIGIL_STATUS_OK;
    }
    return vigil_compile_report(program, member_token->span, unsupported_message);
}

vigil_status_t vigil_program_parse_type_reference(const vigil_program_state_t *program, size_t *cursor,
                                                  const char *unsupported_message, vigil_parser_type_t *out_type)
{
    vigil_status_t status;
    const vigil_token_t *token;
    const vigil_token_t *next_token;
    const char *name_text;
    size_t name_length;

    token = vigil_program_token_at(program, *cursor);
    if (token != NULL && token->kind == VIGIL_TOKEN_FN)
        return parse_function_type_reference(program, cursor, unsupported_message, token, out_type);

    if (token == NULL || token->kind != VIGIL_TOKEN_IDENTIFIER)
        return vigil_compile_report(program, token == NULL ? vigil_program_eof_span(program) : token->span,
                                    unsupported_message);

    name_text = vigil_program_token_text(program, token, &name_length);
    next_token = vigil_program_token_at(program, *cursor + 1U);

    if (next_token != NULL && next_token->kind == VIGIL_TOKEN_LESS &&
        vigil_program_names_equal(name_text, name_length, "array", 5U))
        return parse_array_type_reference(program, cursor, unsupported_message, token, out_type);

    if (next_token != NULL && next_token->kind == VIGIL_TOKEN_LESS &&
        vigil_program_names_equal(name_text, name_length, "map", 3U))
        return parse_map_type_reference(program, cursor, unsupported_message, token, out_type);

    if (next_token == NULL || next_token->kind != VIGIL_TOKEN_DOT)
    {
        status = vigil_program_parse_type_name(program, token, unsupported_message, out_type);
        if (status != VIGIL_STATUS_OK)
            return status;
        *cursor += 1U;
        return VIGIL_STATUS_OK;
    }

    return parse_qualified_type_reference(program, cursor, unsupported_message, token, name_text, name_length,
                                          out_type);
}

vigil_status_t vigil_program_parse_primitive_type_reference(const vigil_program_state_t *program, size_t *cursor,
                                                            const char *unsupported_message,
                                                            vigil_parser_type_t *out_type)
{
    vigil_status_t status;
    const vigil_token_t *type_token;

    type_token = vigil_program_token_at(program, *cursor);
    status = vigil_program_parse_type_reference(program, cursor, unsupported_message, out_type);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    if (out_type->kind == VIGIL_TYPE_I32 || out_type->kind == VIGIL_TYPE_I64 || out_type->kind == VIGIL_TYPE_U8 ||
        out_type->kind == VIGIL_TYPE_U32 || out_type->kind == VIGIL_TYPE_U64 || out_type->kind == VIGIL_TYPE_F64 ||
        out_type->kind == VIGIL_TYPE_BOOL || out_type->kind == VIGIL_TYPE_STRING)
    {
        return VIGIL_STATUS_OK;
    }

    return vigil_compile_report(program, type_token == NULL ? vigil_program_eof_span(program) : type_token->span,
                                unsupported_message);
}

static int skip_comma_separated_types(const vigil_program_state_t *program, size_t *cursor)
{
    const vigil_token_t *token;
    while (1)
    {
        if (!vigil_program_skip_type_reference_syntax(program, cursor))
            return 0;
        token = vigil_program_token_at(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_COMMA)
            break;
        *cursor += 1U;
    }
    return 1;
}

static int skip_fn_type_syntax(const vigil_program_state_t *program, size_t *cursor)
{
    const vigil_token_t *token;

    *cursor += 1U; /* skip 'fn' */
    token = vigil_program_token_at(program, *cursor);
    if (token == NULL)
        return 0;
    if (token->kind != VIGIL_TOKEN_LPAREN)
        return 1; /* bare 'fn' type */

    /* Skip params */
    *cursor += 1U;
    token = vigil_program_token_at(program, *cursor);
    if (token == NULL)
        return 0;
    if (token->kind != VIGIL_TOKEN_RPAREN)
    {
        if (!skip_comma_separated_types(program, cursor))
            return 0;
    }
    token = vigil_program_token_at(program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_RPAREN)
        return 0;
    *cursor += 1U;

    /* Skip return types */
    token = vigil_program_token_at(program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_ARROW)
        return 1;
    *cursor += 1U;

    token = vigil_program_token_at(program, *cursor);
    if (token == NULL)
        return 0;
    if (token->kind == VIGIL_TOKEN_LPAREN)
    {
        *cursor += 1U;
        token = vigil_program_token_at(program, *cursor);
        if (token == NULL)
            return 0;
        if (token->kind != VIGIL_TOKEN_RPAREN)
        {
            if (!skip_comma_separated_types(program, cursor))
                return 0;
        }
        token = vigil_program_token_at(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_RPAREN)
            return 0;
        *cursor += 1U;
        return 1;
    }
    return vigil_program_skip_type_reference_syntax(program, cursor);
}

int vigil_program_skip_type_reference_syntax(const vigil_program_state_t *program, size_t *cursor)
{
    const vigil_token_t *token;

    token = vigil_program_token_at(program, *cursor);
    if (token == NULL)
        return 0;

    if (token->kind == VIGIL_TOKEN_FN)
        return skip_fn_type_syntax(program, cursor);

    if (token->kind != VIGIL_TOKEN_IDENTIFIER)
        return 0;
    *cursor += 1U;

    token = vigil_program_token_at(program, *cursor);
    if (token != NULL && token->kind == VIGIL_TOKEN_DOT)
    {
        *cursor += 1U;
        token = vigil_program_token_at(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_IDENTIFIER)
            return 0;
        *cursor += 1U;
    }

    token = vigil_program_token_at(program, *cursor);
    if (token != NULL && token->kind == VIGIL_TOKEN_LESS)
    {
        *cursor += 1U;
        if (!vigil_program_skip_type_reference_syntax(program, cursor))
            return 0;
        token = vigil_program_token_at(program, *cursor);
        if (token != NULL && token->kind == VIGIL_TOKEN_COMMA)
        {
            *cursor += 1U;
            if (!vigil_program_skip_type_reference_syntax(program, cursor))
                return 0;
        }
        if (!vigil_program_consume_type_close(program, cursor))
            return 0;
    }

    return 1;
}
