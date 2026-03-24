/*
 * vm_ops_arith.h — Extracted arithmetic opcode handlers.
 */

#ifndef VIGIL_VM_OPS_ARITH_H
#define VIGIL_VM_OPS_ARITH_H

#include "internal/vigil_vm_internal.h"

/* Generic binary ops (ADD..EQUAL) — type-dispatched. */
vigil_status_t vigil_vm_op_generic_binary(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                          vigil_error_t *error);

/* Specialized i64 arithmetic. */
vigil_status_t vigil_vm_op_add_sub_i64(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code, size_t code_size,
                                       vigil_error_t *error);
vigil_status_t vigil_vm_op_cmp_i64(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code, vigil_error_t *error);
vigil_status_t vigil_vm_op_mul_div_mod_i64(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                           size_t code_size, vigil_error_t *error);
vigil_status_t vigil_vm_op_locals_add_sub_i64(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                              size_t code_size, vigil_error_t *error);
vigil_status_t vigil_vm_op_locals_mul_mod_i64(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                              size_t code_size, vigil_error_t *error);
vigil_status_t vigil_vm_op_locals_cmp_i64(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                          vigil_error_t *error);

/* Specialized i32 arithmetic. */
vigil_status_t vigil_vm_op_add_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_sub_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_mul_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_div_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_mod_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_cmp_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code, vigil_error_t *error);
vigil_status_t vigil_vm_op_locals_arith_i32_store(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                                  vigil_error_t *error);
vigil_status_t vigil_vm_op_locals_cmp_i32_store(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                                vigil_error_t *error);
vigil_status_t vigil_vm_op_increment_local_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                               vigil_error_t *error);
vigil_status_t vigil_vm_op_forloop_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                       vigil_error_t *error);
vigil_status_t vigil_vm_op_increment_local_i64(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                               vigil_error_t *error);
vigil_status_t vigil_vm_op_forloop_i64(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                       vigil_error_t *error);

#endif /* VIGIL_VM_OPS_ARITH_H */
