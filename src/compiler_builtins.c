#include "internal/vigil_compiler_types.h"
#include "internal/vigil_internal.h"

#include "vigil/builtins.h"
#include "vigil/native_module.h"

#include <string.h>

vigil_status_t vigil_parser_emit_default_value(vigil_parser_state_t *state, vigil_parser_type_t type,
                                               vigil_source_span_t span)
{
    if (state == NULL)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (vigil_parser_type_is_bool(type))
    {
        return vigil_parser_emit_opcode(state, VIGIL_OPCODE_FALSE, span);
    }
    if (vigil_parser_type_is_integer(type) || vigil_parser_type_is_enum(type))
    {
        return vigil_parser_emit_integer_constant(state, type, 0, span);
    }
    if (vigil_parser_type_is_f64(type))
    {
        return vigil_parser_emit_f64_constant(state, 0.0, span);
    }
    if (vigil_parser_type_is_string(type))
    {
        return vigil_parser_emit_string_constant_text(state, span, "", 0U);
    }
    if (vigil_parser_type_is_err(type))
    {
        return vigil_parser_emit_ok_constant(state, span);
    }

    return vigil_parser_emit_opcode(state, VIGIL_OPCODE_NIL, span);
}

/* ── String method helpers ─────────────────────────────────────────── */

static vigil_status_t compile_string_set_result(vigil_parser_state_t *state,
                                                const vigil_string_method_descriptor_t *descriptor,
                                                vigil_expression_result_t *out_result)
{
    vigil_status_t status;
    vigil_parser_type_t array_type;

    if (descriptor->return_tuple_type_count == 2U)
    {
        vigil_expression_result_set_pair(
            out_result, vigil_binding_type_primitive((vigil_type_kind_t)descriptor->return_tuple_type_kinds[0]),
            vigil_binding_type_primitive((vigil_type_kind_t)descriptor->return_tuple_type_kinds[1]));
        return VIGIL_STATUS_OK;
    }

    if (descriptor->return_tuple_type_count == 3U)
    {
        vigil_expression_result_set_triple(
            out_result, vigil_binding_type_primitive((vigil_type_kind_t)descriptor->return_tuple_type_kinds[0]),
            vigil_binding_type_primitive((vigil_type_kind_t)descriptor->return_tuple_type_kinds[1]),
            vigil_binding_type_primitive((vigil_type_kind_t)descriptor->return_tuple_type_kinds[2]));
        return VIGIL_STATUS_OK;
    }

    if (descriptor->return_type_kind == VIGIL_TYPE_OBJECT && descriptor->return_object_kind == VIGIL_NATIVE_FIELD_ARRAY)
    {
        status = vigil_program_intern_array_type(
            (vigil_program_state_t *)state->program,
            vigil_binding_type_primitive((vigil_type_kind_t)descriptor->return_element_type_kind), &array_type);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        vigil_expression_result_set_type(out_result, array_type);
        return VIGIL_STATUS_OK;
    }

    vigil_expression_result_set_type(out_result,
                                     vigil_binding_type_primitive((vigil_type_kind_t)descriptor->return_type_kind));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_string_emit(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                          const vigil_string_method_descriptor_t *descriptor,
                                          vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_emit_opcode(state, descriptor->opcode, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    return compile_string_set_result(state, descriptor, out_result);
}

static vigil_status_t compile_string_require_arg_type(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                                      const vigil_string_method_descriptor_t *descriptor, size_t index,
                                                      vigil_parser_type_t actual_type)
{
    if (descriptor->arg_type_kinds[index] == VIGIL_TYPE_STRING)
    {
        return vigil_parser_require_type(state, method_token->span, actual_type,
                                         vigil_binding_type_primitive(VIGIL_TYPE_STRING),
                                         "string method argument must be string");
    }

    if (descriptor->arg_type_kinds[index] == VIGIL_TYPE_I32)
    {
        return vigil_parser_require_type(state, method_token->span, actual_type,
                                         vigil_binding_type_primitive(VIGIL_TYPE_I32),
                                         "string method argument must be i32");
    }

    if (descriptor->arg_type_kinds[index] == VIGIL_TYPE_OBJECT &&
        descriptor->arg_object_kinds[index] == VIGIL_NATIVE_FIELD_ARRAY)
    {
        vigil_status_t status;
        vigil_parser_type_t expected_array;

        status = vigil_program_intern_array_type(
            (vigil_program_state_t *)state->program,
            vigil_binding_type_primitive((vigil_type_kind_t)descriptor->arg_element_type_kinds[index]),
            &expected_array);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        return vigil_parser_require_type(state, method_token->span, actual_type, expected_array,
                                         "string method argument must match required array type");
    }

    return VIGIL_STATUS_OK;
}

/* ── Shared .any() / .none() helper ────────────────────────────────── */

/* Emit opcodes for a collection .any() or .none() method.
   For collections: GET_COLLECTION_SIZE + push 0 + EQUAL/NOT_EQUAL.
   is_any=1 for .any(), is_any=0 for .none(). */
static vigil_status_t compile_collection_any_none(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                                  int is_any, vigil_expression_result_t *out_result)
{
    vigil_status_t status;

    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_COLLECTION_SIZE, method_token->span);
    if (status != VIGIL_STATUS_OK)
        return status;
    status = vigil_parser_emit_i32_constant(state, 0, method_token->span);
    if (status != VIGIL_STATUS_OK)
        return status;
    status = vigil_parser_emit_opcode(state, is_any ? VIGIL_OPCODE_NOT_EQUAL_I32 : VIGIL_OPCODE_EQUAL_I32,
                                      method_token->span);
    if (status != VIGIL_STATUS_OK)
        return status;
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_BOOL));
    return VIGIL_STATUS_OK;
}

/* ── Refactored string method dispatch ─────────────────────────────── */

vigil_status_t vigil_parser_parse_string_method_call(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                                     vigil_expression_result_t *out_result)
{
    vigil_status_t status;
    vigil_expression_result_t arg_results[2];
    const char *method_name;
    size_t method_length;
    const vigil_string_method_descriptor_t *descriptor;
    size_t arg_index;

    vigil_expression_result_clear(&arg_results[0]);
    vigil_expression_result_clear(&arg_results[1]);
    method_name = vigil_parser_token_text(state, method_token, &method_length);
    descriptor = vigil_string_method_find(method_name, method_length);

    status = vigil_parser_expect(state, VIGIL_TOKEN_LPAREN, "expected '(' after string method name", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    /* Deprecation warning for is_empty(). */
    if (vigil_program_names_equal(method_name, method_length, "is_empty", 8U))
    {
        vigil_diagnostic_list_append_cstr(state->program->diagnostics, VIGIL_DIAGNOSTIC_WARNING, method_token->span,
                                          "is_empty() is deprecated, use none() instead", state->program->error);
    }

    /* .any() — emit STRING_IS_EMPTY + NOT. */
    if (vigil_program_names_equal(method_name, method_length, "any", 3U))
    {
        status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "string method does not accept arguments", NULL);
        if (status != VIGIL_STATUS_OK)
            return status;
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_IS_EMPTY, method_token->span);
        if (status != VIGIL_STATUS_OK)
            return status;
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_NOT, method_token->span);
        if (status != VIGIL_STATUS_OK)
            return status;
        vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_BOOL));
        return VIGIL_STATUS_OK;
    }

    if (descriptor == NULL)
    {
        return vigil_parser_report(state, method_token->span, "unknown string method");
    }

    if (descriptor->arg_count == 0U)
    {
        status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "string method does not accept arguments", NULL);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        return compile_string_emit(state, method_token, descriptor, out_result);
    }

    for (arg_index = 0U; arg_index < descriptor->arg_count; arg_index += 1U)
    {
        if (arg_index != 0U)
        {
            status = vigil_parser_expect(state, VIGIL_TOKEN_COMMA, "string method expects more arguments", NULL);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }
        }

        status = vigil_parser_parse_expression(state, &arg_results[arg_index]);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        status = vigil_parser_require_scalar_expression(state, method_token->span, &arg_results[arg_index],
                                                        "string method arguments must be single values");
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        status =
            compile_string_require_arg_type(state, method_token, descriptor, arg_index, arg_results[arg_index].type);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }

    status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "expected ')' after string method arguments", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    return compile_string_emit(state, method_token, descriptor, out_result);
}

/* ── Array method helpers ──────────────────────────────────────────── */

static vigil_status_t compile_array_len(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                        vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_COLLECTION_SIZE, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_I32));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_array_pop(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                        vigil_parser_type_t element_type, vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_emit_default_value(state, element_type, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ARRAY_POP, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_pair(out_result, element_type, vigil_binding_type_primitive(VIGIL_TYPE_ERR));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_array_push(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                         vigil_parser_type_t element_type, vigil_expression_result_t *first_arg,
                                         vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_require_type(state, method_token->span, first_arg->type, element_type,
                                                      "array push() argument must match array element type");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ARRAY_PUSH, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_VOID));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_array_get(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                        vigil_parser_type_t element_type, vigil_expression_result_t *first_arg,
                                        vigil_expression_result_t *out_result)
{
    vigil_status_t status =
        vigil_parser_require_type(state, method_token->span, first_arg->type,
                                  vigil_binding_type_primitive(VIGIL_TYPE_I32), "array get() index must be i32");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_default_value(state, element_type, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ARRAY_GET_SAFE, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_pair(out_result, element_type, vigil_binding_type_primitive(VIGIL_TYPE_ERR));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_array_contains(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                             vigil_parser_type_t element_type, vigil_expression_result_t *first_arg,
                                             vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_require_type(state, method_token->span, first_arg->type, element_type,
                                                      "array contains() argument must match array element type");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ARRAY_CONTAINS, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_BOOL));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_array_set(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                        vigil_parser_type_t element_type, vigil_expression_result_t *first_arg,
                                        vigil_expression_result_t *second_arg, vigil_expression_result_t *out_result)
{
    vigil_status_t status =
        vigil_parser_require_type(state, method_token->span, first_arg->type,
                                  vigil_binding_type_primitive(VIGIL_TYPE_I32), "array set() index must be i32");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_type(state, method_token->span, second_arg->type, element_type,
                                       "array set() value must match array element type");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ARRAY_SET_SAFE, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_ERR));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_array_slice(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                          vigil_parser_type_t receiver_type, vigil_expression_result_t *first_arg,
                                          vigil_expression_result_t *second_arg, vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_require_type(state, method_token->span, first_arg->type,
                                                      vigil_binding_type_primitive(VIGIL_TYPE_I32),
                                                      "array slice() start and end must be i32");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_type(state, method_token->span, second_arg->type,
                                       vigil_binding_type_primitive(VIGIL_TYPE_I32),
                                       "array slice() start and end must be i32");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ARRAY_SLICE, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, receiver_type);
    return VIGIL_STATUS_OK;
}

/* ── Refactored array method dispatch ──────────────────────────────── */

static vigil_status_t compile_array_parse_one_arg(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                                  vigil_expression_result_t *first_arg)
{
    vigil_status_t status = vigil_parser_parse_expression(state, first_arg);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_scalar_expression(state, method_token->span, first_arg,
                                                    "array method arguments must be single values");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    return vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "expected ')' after array method arguments", NULL);
}

static vigil_status_t compile_array_parse_two_args(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                                   vigil_expression_result_t *first_arg,
                                                   vigil_expression_result_t *second_arg)
{
    vigil_status_t status = vigil_parser_parse_expression(state, first_arg);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_scalar_expression(state, method_token->span, first_arg,
                                                    "array method arguments must be single values");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_expect(state, VIGIL_TOKEN_COMMA, "array method expects two arguments", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_parse_expression(state, second_arg);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_scalar_expression(state, method_token->span, second_arg,
                                                    "array method arguments must be single values");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    return vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "expected ')' after array method arguments", NULL);
}

vigil_status_t vigil_parser_parse_array_method_call(vigil_parser_state_t *state, vigil_parser_type_t receiver_type,
                                                    const vigil_token_t *method_token,
                                                    vigil_expression_result_t *out_result)
{
    vigil_status_t status;
    vigil_expression_result_t first_arg;
    vigil_expression_result_t second_arg;
    vigil_parser_type_t element_type;
    const char *method_name;
    size_t method_length;

    vigil_expression_result_clear(&first_arg);
    vigil_expression_result_clear(&second_arg);
    element_type = vigil_program_array_type_element(state->program, receiver_type);
    method_name = vigil_parser_token_text(state, method_token, &method_length);

    status = vigil_parser_expect(state, VIGIL_TOKEN_LPAREN, "expected '(' after array method name", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (vigil_program_names_equal(method_name, method_length, "len", 3U) ||
        vigil_program_names_equal(method_name, method_length, "pop", 3U))
    {
        status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "array method does not accept arguments", NULL);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        if (vigil_program_names_equal(method_name, method_length, "len", 3U))
        {
            return compile_array_len(state, method_token, out_result);
        }
        return compile_array_pop(state, method_token, element_type, out_result);
    }

    if (vigil_program_names_equal(method_name, method_length, "any", 3U) ||
        vigil_program_names_equal(method_name, method_length, "none", 4U))
    {
        status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "array method does not accept arguments", NULL);
        if (status != VIGIL_STATUS_OK)
            return status;
        return compile_collection_any_none(
            state, method_token, vigil_program_names_equal(method_name, method_length, "any", 3U), out_result);
    }

    if (vigil_program_names_equal(method_name, method_length, "push", 4U) ||
        vigil_program_names_equal(method_name, method_length, "get", 3U) ||
        vigil_program_names_equal(method_name, method_length, "contains", 8U))
    {
        status = compile_array_parse_one_arg(state, method_token, &first_arg);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        if (vigil_program_names_equal(method_name, method_length, "push", 4U))
        {
            return compile_array_push(state, method_token, element_type, &first_arg, out_result);
        }
        if (vigil_program_names_equal(method_name, method_length, "get", 3U))
        {
            return compile_array_get(state, method_token, element_type, &first_arg, out_result);
        }
        return compile_array_contains(state, method_token, element_type, &first_arg, out_result);
    }

    if (vigil_program_names_equal(method_name, method_length, "set", 3U) ||
        vigil_program_names_equal(method_name, method_length, "slice", 5U))
    {
        status = compile_array_parse_two_args(state, method_token, &first_arg, &second_arg);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        if (vigil_program_names_equal(method_name, method_length, "set", 3U))
        {
            return compile_array_set(state, method_token, element_type, &first_arg, &second_arg, out_result);
        }
        return compile_array_slice(state, method_token, receiver_type, &first_arg, &second_arg, out_result);
    }

    return vigil_parser_report(state, method_token->span, "unknown array method");
}

/* ── Map method helpers ────────────────────────────────────────────── */

static vigil_status_t compile_map_len(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                      vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_COLLECTION_SIZE, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_I32));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_map_keys_or_values(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                                 int is_keys, vigil_parser_type_t elem_type,
                                                 vigil_expression_result_t *out_result)
{
    vigil_status_t status;
    vigil_parser_type_t array_type;

    status = vigil_program_intern_array_type((vigil_program_state_t *)state->program, elem_type, &array_type);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status =
        vigil_parser_emit_opcode(state, is_keys ? VIGIL_OPCODE_MAP_KEYS : VIGIL_OPCODE_MAP_VALUES, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, array_type);
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_map_get(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                      vigil_parser_type_t value_type, vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_emit_default_value(state, value_type, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_MAP_GET_SAFE, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_pair(out_result, value_type, vigil_binding_type_primitive(VIGIL_TYPE_BOOL));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_map_remove(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                         vigil_parser_type_t value_type, vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_emit_default_value(state, value_type, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_MAP_REMOVE_SAFE, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_pair(out_result, value_type, vigil_binding_type_primitive(VIGIL_TYPE_BOOL));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_map_has(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                      vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_MAP_HAS, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_BOOL));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_map_set(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                      vigil_parser_type_t value_type, vigil_expression_result_t *out_result)
{
    vigil_status_t status;
    vigil_expression_result_t second_arg;

    vigil_expression_result_clear(&second_arg);
    status = vigil_parser_expect(state, VIGIL_TOKEN_COMMA, "map set() expects two arguments", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_parse_expression(state, &second_arg);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_scalar_expression(state, method_token->span, &second_arg,
                                                    "map method arguments must be single values");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_type(state, method_token->span, second_arg.type, value_type,
                                       "map set() value must match map value type");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "expected ')' after map method arguments", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_MAP_SET_SAFE, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_ERR));
    return VIGIL_STATUS_OK;
}

/* ── Refactored map method dispatch ────────────────────────────────── */

vigil_status_t vigil_parser_parse_map_method_call(vigil_parser_state_t *state, vigil_parser_type_t receiver_type,
                                                  const vigil_token_t *method_token,
                                                  vigil_expression_result_t *out_result)
{
    vigil_status_t status;
    vigil_expression_result_t first_arg;
    vigil_parser_type_t key_type;
    vigil_parser_type_t value_type;
    const char *method_name;
    size_t method_length;

    vigil_expression_result_clear(&first_arg);
    key_type = vigil_program_map_type_key(state->program, receiver_type);
    value_type = vigil_program_map_type_value(state->program, receiver_type);
    method_name = vigil_parser_token_text(state, method_token, &method_length);

    status = vigil_parser_expect(state, VIGIL_TOKEN_LPAREN, "expected '(' after map method name", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (vigil_program_names_equal(method_name, method_length, "len", 3U) ||
        vigil_program_names_equal(method_name, method_length, "keys", 4U) ||
        vigil_program_names_equal(method_name, method_length, "values", 6U) ||
        vigil_program_names_equal(method_name, method_length, "any", 3U) ||
        vigil_program_names_equal(method_name, method_length, "none", 4U))
    {
        status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "map method does not accept arguments", NULL);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        if (vigil_program_names_equal(method_name, method_length, "len", 3U))
        {
            return compile_map_len(state, method_token, out_result);
        }
        if (vigil_program_names_equal(method_name, method_length, "any", 3U) ||
            vigil_program_names_equal(method_name, method_length, "none", 4U))
        {
            return compile_collection_any_none(
                state, method_token, vigil_program_names_equal(method_name, method_length, "any", 3U), out_result);
        }
        if (vigil_program_names_equal(method_name, method_length, "keys", 4U))
        {
            return compile_map_keys_or_values(state, method_token, 1, key_type, out_result);
        }
        return compile_map_keys_or_values(state, method_token, 0, value_type, out_result);
    }

    status = vigil_parser_parse_expression(state, &first_arg);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_scalar_expression(state, method_token->span, &first_arg,
                                                    "map method arguments must be single values");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_type(state, method_token->span, first_arg.type, key_type,
                                       "map method key must match map key type");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (vigil_program_names_equal(method_name, method_length, "get", 3U) ||
        vigil_program_names_equal(method_name, method_length, "remove", 6U) ||
        vigil_program_names_equal(method_name, method_length, "has", 3U))
    {
        status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "expected ')' after map method arguments", NULL);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        if (vigil_program_names_equal(method_name, method_length, "get", 3U))
        {
            return compile_map_get(state, method_token, value_type, out_result);
        }
        if (vigil_program_names_equal(method_name, method_length, "remove", 6U))
        {
            return compile_map_remove(state, method_token, value_type, out_result);
        }
        return compile_map_has(state, method_token, out_result);
    }

    if (vigil_program_names_equal(method_name, method_length, "set", 3U))
    {
        return compile_map_set(state, method_token, value_type, out_result);
    }

    return vigil_parser_report(state, method_token->span, "unknown map method");
}
