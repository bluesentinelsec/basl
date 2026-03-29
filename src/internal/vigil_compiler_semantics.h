#ifndef VIGIL_COMPILER_SEMANTICS_H
#define VIGIL_COMPILER_SEMANTICS_H

#include "vigil_binding.h"
#include "vigil_compiler_types.h"

typedef struct
{
    const vigil_token_t *target_token;
    size_t global_index;
    const vigil_global_variable_t *global_decl;
    int is_global;
    vigil_parser_type_t type;
} import_assignment_result_t;

typedef struct
{
    const vigil_token_t *name_token;
    const vigil_token_t *target_token;
    size_t local_index;
    size_t capture_index;
    size_t global_index;
    size_t field_index;
    vigil_parser_type_t local_type;
    vigil_parser_type_t target_type;
    const vigil_class_field_t *field;
    const vigil_binding_local_t *local_decl;
    const vigil_global_variable_t *global_decl;
    int is_field_assignment;
    int is_index_assignment;
    int is_global_assignment;
    int is_const_local;
    int emitted_target_base;
    int is_capture_local;
} assignment_target_t;

typedef int (*vigil_semantic_stop_predicate_t)(const vigil_parser_state_t *state);
typedef vigil_status_t (*vigil_semantic_parse_step_t)(vigil_parser_state_t *state,
                                                      vigil_statement_result_t *out_result);

void vigil_statement_result_set_non_returning(vigil_statement_result_t *result);
int vigil_statement_result_guarantees_return(const vigil_statement_result_t *result);
void vigil_statement_result_merge_sequence(vigil_statement_result_t *result,
                                           const vigil_statement_result_t *next_result);
void vigil_statement_result_set_conditional(vigil_statement_result_t *result, int has_else_branch,
                                            const vigil_statement_result_t *then_result,
                                            const vigil_statement_result_t *else_result);
void vigil_statement_result_set_switch(vigil_statement_result_t *result, int has_default, int all_branches_return);
void vigil_return_analysis_merge_switch_branch(int *all_branches_return, const vigil_statement_result_t *branch_result);
vigil_status_t parse_bool_condition_expression(vigil_parser_state_t *state, vigil_source_span_t span,
                                               const char *scalar_message, const char *type_message,
                                               vigil_expression_result_t *condition_result);
vigil_status_t parse_parenthesized_bool_condition(vigil_parser_state_t *state, const vigil_token_t *keyword_token,
                                                  const char *lparen_message, const char *rparen_message,
                                                  const char *scalar_message, const char *type_message,
                                                  vigil_expression_result_t *condition_result);
vigil_status_t vigil_semantic_parse_statement_sequence(vigil_parser_state_t *state,
                                                       vigil_statement_result_t *out_result,
                                                       vigil_semantic_parse_step_t parse_step,
                                                       vigil_semantic_stop_predicate_t should_stop);

void assignment_target_init(assignment_target_t *t);
int assignment_target_is_composite(const assignment_target_t *t);
vigil_status_t check_non_assignable_member(vigil_parser_state_t *state, vigil_source_id_t import_source_id,
                                           const char *member_text, size_t member_length, vigil_source_span_t span);
vigil_status_t vigil_parser_resolve_import_assignment_target(vigil_parser_state_t *state,
                                                             vigil_source_id_t import_source_id,
                                                             import_assignment_result_t *out);
vigil_status_t resolve_nonlocal_target(vigil_parser_state_t *state, assignment_target_t *t);
vigil_status_t validate_assignment_target_writable(vigil_parser_state_t *state, const assignment_target_t *t);
vigil_status_t parse_assignment_operator(vigil_parser_state_t *state, const assignment_target_t *t,
                                         const vigil_token_t **out_operator_token);
vigil_status_t resolve_assignment_target(vigil_parser_state_t *state, assignment_target_t *t);
vigil_status_t resolve_assignment_field_target(vigil_parser_state_t *state, assignment_target_t *t);
vigil_status_t resolve_assignment_index_target(vigil_parser_state_t *state, assignment_target_t *t);
vigil_status_t parse_increment_decrement(vigil_parser_state_t *state, const assignment_target_t *t,
                                         const vigil_token_t *op, vigil_opcode_t *out_opcode);
vigil_status_t parse_compound_assignment_value(vigil_parser_state_t *state, const assignment_target_t *t,
                                               const vigil_token_t *op, vigil_expression_result_t *value_result);
vigil_status_t validate_compound_assignment_operation(vigil_parser_state_t *state, const assignment_target_t *t,
                                                      const vigil_token_t *op,
                                                      const vigil_expression_result_t *value_result,
                                                      vigil_opcode_t *out_opcode);
vigil_status_t parse_simple_assignment(vigil_parser_state_t *state, const assignment_target_t *t,
                                       const vigil_token_t *op, vigil_expression_result_t *value_result);

#endif
