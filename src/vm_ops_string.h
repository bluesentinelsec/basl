/*
 * vm_ops_string.h — String opcode handler declarations.
 */

#ifndef VIGIL_VM_OPS_STRING_H
#define VIGIL_VM_OPS_STRING_H

#include "internal/vigil_vm_internal.h"

vigil_status_t vigil_vm_op_get_string_size(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_search(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                         vigil_error_t *error);
vigil_status_t vigil_vm_op_string_transform(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                            vigil_error_t *error);
vigil_status_t vigil_vm_op_string_replace(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_split(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_index_of(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_substr(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_bytes(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_char_at(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_trim_dir(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                           vigil_error_t *error);
vigil_status_t vigil_vm_op_string_reverse(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_is_empty(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_char_count(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_repeat(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_count(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_last_index_of(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_trim_affix(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                             vigil_error_t *error);
vigil_status_t vigil_vm_op_char_from_int(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_to_c(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_fields(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_equal_fold(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_cut(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_join(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_next_char(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_pad_left(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_string_pad_right(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);

#endif /* VIGIL_VM_OPS_STRING_H */
