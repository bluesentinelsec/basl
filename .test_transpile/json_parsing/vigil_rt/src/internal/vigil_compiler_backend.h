#ifndef VIGIL_COMPILER_BACKEND_H
#define VIGIL_COMPILER_BACKEND_H

#include "vigil_compiler_types.h"

VIGIL_API vigil_status_t vigil_compile_materialize_lowered_function_body(vigil_program_state_t *program,
                                                                         const vigil_function_decl_t *decl,
                                                                         vigil_lowered_function_body_t *body,
                                                                         vigil_object_t **out_object);
VIGIL_API vigil_status_t vigil_compile_backend_try_materialize_special_function(vigil_program_state_t *program,
                                                                                size_t function_index, int *handled);
vigil_status_t vigil_backend_lowered_patch_u32(vigil_parser_state_t *state, size_t operand_offset, uint32_t value);
void vigil_backend_lowered_replace_last_opcode(vigil_parser_state_t *state, vigil_opcode_t opcode);
void vigil_backend_lowered_fuse_last_two_get_local(vigil_parser_state_t *state, vigil_opcode_t opcode);
void vigil_backend_lowered_rewrite_tail_call_self(vigil_parser_state_t *state);
void vigil_backend_lowered_rewrite_tail_call(vigil_parser_state_t *state);
void vigil_backend_lowered_rewrite_increment_local(vigil_parser_state_t *state, vigil_opcode_t opcode, int8_t delta);
void vigil_backend_lowered_rewrite_locals_store(vigil_parser_state_t *state, vigil_opcode_t opcode);
void vigil_backend_lowered_rewrite_f64_store(vigil_parser_state_t *state, vigil_opcode_t opcode);
void vigil_backend_lowered_rewrite_forloop(vigil_parser_state_t *state, size_t loop_start, vigil_opcode_t opcode,
                                           uint8_t cmp_type);

#endif
