#include "internal/vigil_compiler_types.h"
#include "internal/vigil_internal.h"

#include <stdlib.h>

static int vigil_parser_token_is_discard_identifier(const vigil_parser_state_t *state, const vigil_token_t *token)
{
    size_t length;
    const char *text;

    text = vigil_parser_token_text(state, token, &length);
    return text != NULL && length == 1U && text[0] == '_';
}

vigil_status_t vigil_parser_parse_binding_target_list(vigil_parser_state_t *state, const char *unsupported_type_message,
                                                      const char *non_void_message, const char *name_message,
                                                      vigil_binding_target_list_t *targets)
{
    vigil_status_t status;
    vigil_parser_type_t declared_type;
    const vigil_token_t *name_token = NULL;
    const vigil_token_t *type_token;

    if (state == NULL || targets == NULL)
    {
        vigil_error_set_literal(state == NULL ? NULL : state->program->error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "binding target list arguments are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    do
    {
        status = vigil_program_parse_type_reference(state->program, &state->current, unsupported_type_message,
                                                    &declared_type);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        type_token = vigil_parser_previous(state);
        status = vigil_program_require_non_void_type(
            state->program, type_token == NULL ? vigil_parser_fallback_span(state) : type_token->span, declared_type,
            non_void_message);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        status = vigil_parser_expect(state, VIGIL_TOKEN_IDENTIFIER, name_message, &name_token);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        status =
            vigil_binding_target_list_append((vigil_program_state_t *)state->program, targets, declared_type,
                                             name_token, vigil_parser_token_is_discard_identifier(state, name_token));
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    } while (vigil_parser_match(state, VIGIL_TOKEN_COMMA));

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_parser_require_binding_initializer_shape(vigil_parser_state_t *state, vigil_source_span_t span,
                                                              const vigil_binding_target_list_t *targets,
                                                              const vigil_expression_result_t *initializer_result,
                                                              const char *count_message, const char *type_message)
{
    vigil_status_t status;
    size_t index;

    if (targets == NULL || initializer_result == NULL)
    {
        vigil_error_set_literal(state == NULL ? NULL : state->program->error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "binding initializer arguments are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (initializer_result->type_count != targets->count)
    {
        return vigil_parser_report(state, span, count_message);
    }

    for (index = 0U; index < targets->count; index += 1U)
    {
        status = vigil_parser_require_type(state, span, vigil_expression_result_type_at(initializer_result, index),
                                           targets->items[index].type, type_message);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_parser_parse_variable_declaration(vigil_parser_state_t *state,
                                                       vigil_statement_result_t *out_result)
{
    vigil_status_t status;
    vigil_binding_target_list_t targets;
    vigil_expression_result_t initializer_result;

    vigil_binding_target_list_init(&targets);
    vigil_expression_result_clear(&initializer_result);

    status = vigil_parser_parse_binding_target_list(state, "unsupported local variable type",
                                                    "local variables cannot use type void",
                                                    "expected local variable name", &targets);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
        return status;
    }

    if (!vigil_parser_match(state, VIGIL_TOKEN_ASSIGN))
    {
        status = vigil_parser_report(state, targets.items[0].name_token->span,
                                     "variables must be initialized at declaration");
        vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
        return status;
    }

    status = vigil_parser_parse_expression_with_expected_type(
        state, targets.count == 1U ? targets.items[0].type : vigil_binding_type_invalid(), &initializer_result);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
        return status;
    }
    status = vigil_parser_require_binding_initializer_shape(
        state, targets.items[0].name_token->span, &targets, &initializer_result,
        targets.count == 1U ? "initializer must be a single value"
                            : "initializer return shape does not match declaration",
        targets.count == 1U ? "initializer type does not match local variable type"
                            : "initializer type does not match local binding type");
    if (status != VIGIL_STATUS_OK)
    {
        vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
        return status;
    }

    status = vigil_parser_expect_semi(state, "expected ';' after local declaration");
    if (status != VIGIL_STATUS_OK)
    {
        vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
        return status;
    }

    status = vigil_parser_bind_targets(state, &targets, 0, NULL);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
        return status;
    }
    vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
    vigil_statement_result_set_guaranteed_return(out_result, 0);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_parser_parse_const_declaration(vigil_parser_state_t *state, vigil_statement_result_t *out_result)
{
    vigil_status_t status;
    const vigil_token_t *const_token = NULL;
    const vigil_token_t *name_token = NULL;
    vigil_parser_type_t declared_type;
    vigil_expression_result_t initializer_result;

    vigil_expression_result_clear(&initializer_result);
    status = vigil_parser_expect(state, VIGIL_TOKEN_CONST, "expected 'const'", &const_token);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_program_parse_type_reference(state->program, &state->current, "unsupported local constant type",
                                                &declared_type);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_program_require_non_void_type(
        state->program, vigil_parser_previous(state) == NULL ? const_token->span : vigil_parser_previous(state)->span,
        declared_type, "local constants cannot use type void");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_expect(state, VIGIL_TOKEN_IDENTIFIER, "expected local constant name", &name_token);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_expect(state, VIGIL_TOKEN_ASSIGN, "constants must be initialized at declaration", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_parse_expression_with_expected_type(state, declared_type, &initializer_result);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_scalar_expression(state, name_token->span, &initializer_result,
                                                    "initializer must be a single value");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_type(state, name_token->span, initializer_result.type, declared_type,
                                       "initializer type does not match local constant type");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_expect_semi(state, "expected ';' after local constant declaration");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_parser_declare_local_symbol(state, name_token, declared_type, 1, NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_statement_result_set_guaranteed_return(out_result, 0);
    return VIGIL_STATUS_OK;
}

/* ── Walrus (:=) inferred declarations ─────────────────────────────── */

vigil_status_t vigil_parser_parse_walrus_declaration(vigil_parser_state_t *state, vigil_statement_result_t *out_result)
{
    vigil_status_t status;
    vigil_expression_result_t initializer_result;
    const vigil_token_t *name_tokens[8];
    size_t name_count = 0U;

    vigil_expression_result_clear(&initializer_result);

    /* Collect comma-separated identifiers before := */
    while (1)
    {
        const vigil_token_t *name_token = NULL;
        status = vigil_parser_expect(state, VIGIL_TOKEN_IDENTIFIER, "expected variable name", &name_token);
        if (status != VIGIL_STATUS_OK)
            return status;
        if (name_count >= 8U)
            return vigil_parser_report(state, name_token->span, "too many bindings in walrus declaration");
        name_tokens[name_count++] = name_token;
        if (!vigil_parser_match(state, VIGIL_TOKEN_COMMA))
            break;
    }

    status = vigil_parser_expect(state, VIGIL_TOKEN_WALRUS, "expected ':='", NULL);
    if (status != VIGIL_STATUS_OK)
        return status;

    /* Parse the initializer expression. */
    status = vigil_parser_parse_expression(state, &initializer_result);
    if (status != VIGIL_STATUS_OK)
        return status;

    status = vigil_parser_expect_semi(state, "expected ';' after walrus declaration");
    if (status != VIGIL_STATUS_OK)
        return status;

    /* Single binding: infer type from expression. */
    if (name_count == 1U)
    {
        status = vigil_parser_require_scalar_expression(state, name_tokens[0]->span, &initializer_result,
                                                        "walrus initializer must be a single value");
        if (status != VIGIL_STATUS_OK)
            return status;
        if (vigil_parser_type_is_void(initializer_result.type))
            return vigil_parser_report(state, name_tokens[0]->span, "cannot infer type from void expression");
        status = vigil_parser_declare_local_symbol(state, name_tokens[0], initializer_result.type, 0, NULL);
        if (status != VIGIL_STATUS_OK)
            return status;
        vigil_statement_result_set_guaranteed_return(out_result, 0);
        return VIGIL_STATUS_OK;
    }

    /* Multi-binding: infer types from multi-return expression. */
    if (initializer_result.type_count != name_count)
        return vigil_parser_report(state, name_tokens[0]->span,
                                   "walrus binding count does not match return value count");

    for (size_t i = 0U; i < name_count; i++)
    {
        vigil_parser_type_t inferred = vigil_expression_result_type_at(&initializer_result, i);
        if (vigil_parser_type_is_void(inferred))
            return vigil_parser_report(state, name_tokens[i]->span, "cannot infer type from void return value");
        status = vigil_parser_declare_local_symbol(state, name_tokens[i], inferred, 0, NULL);
        if (status != VIGIL_STATUS_OK)
            return status;
    }
    vigil_statement_result_set_guaranteed_return(out_result, 0);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_parser_parse_const_walrus_declaration(vigil_parser_state_t *state,
                                                           vigil_statement_result_t *out_result)
{
    vigil_status_t status;
    const vigil_token_t *const_token = NULL;
    const vigil_token_t *name_token = NULL;
    vigil_expression_result_t initializer_result;

    vigil_expression_result_clear(&initializer_result);

    status = vigil_parser_expect(state, VIGIL_TOKEN_CONST, "expected 'const'", &const_token);
    if (status != VIGIL_STATUS_OK)
        return status;

    status = vigil_parser_expect(state, VIGIL_TOKEN_IDENTIFIER, "expected constant name", &name_token);
    if (status != VIGIL_STATUS_OK)
        return status;

    status = vigil_parser_expect(state, VIGIL_TOKEN_WALRUS, "expected ':='", NULL);
    if (status != VIGIL_STATUS_OK)
        return status;

    status = vigil_parser_parse_expression(state, &initializer_result);
    if (status != VIGIL_STATUS_OK)
        return status;

    status = vigil_parser_require_scalar_expression(state, name_token->span, &initializer_result,
                                                    "constant initializer must be a single value");
    if (status != VIGIL_STATUS_OK)
        return status;

    if (vigil_parser_type_is_void(initializer_result.type))
        return vigil_parser_report(state, name_token->span, "cannot infer type from void expression");

    status = vigil_parser_expect_semi(state, "expected ';' after constant declaration");
    if (status != VIGIL_STATUS_OK)
        return status;

    status = vigil_parser_declare_local_symbol(state, name_token, initializer_result.type, 1, NULL);
    if (status != VIGIL_STATUS_OK)
        return status;

    vigil_statement_result_set_guaranteed_return(out_result, 0);
    return VIGIL_STATUS_OK;
}
