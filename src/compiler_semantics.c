#include "internal/vigil_compiler_semantics.h"
#include "internal/vigil_internal.h"

static int vigil_semantic_is_assignment_operator(vigil_token_kind_t kind)
{
    return kind == VIGIL_TOKEN_ASSIGN || kind == VIGIL_TOKEN_PLUS_ASSIGN || kind == VIGIL_TOKEN_MINUS_ASSIGN ||
           kind == VIGIL_TOKEN_STAR_ASSIGN || kind == VIGIL_TOKEN_SLASH_ASSIGN || kind == VIGIL_TOKEN_PERCENT_ASSIGN ||
           kind == VIGIL_TOKEN_PLUS_PLUS || kind == VIGIL_TOKEN_MINUS_MINUS;
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
