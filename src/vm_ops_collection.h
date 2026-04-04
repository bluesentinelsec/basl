/*
 * vm_ops_collection.h — Collection opcode handler declarations.
 */

#ifndef VIGIL_VM_OPS_COLLECTION_H
#define VIGIL_VM_OPS_COLLECTION_H

#include "internal/vigil_vm_internal.h"

vigil_status_t vigil_vm_op_get_index(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_get_collection_size(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_array_push(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_array_pop(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_array_get_safe(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_array_set_safe(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_array_slice(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_array_contains(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_map_get_safe(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_map_set_safe(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_map_remove_safe(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_map_has(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_map_keys_values(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                           vigil_error_t *error);
vigil_status_t vigil_vm_op_get_map_key_at(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_get_map_value_at(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_set_index(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_array_sort(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_array_sort_desc(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_array_reverse(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_array_index_of(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_array_remove_at(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_array_insert_at(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_array_clear(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_map_clear(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);

#endif /* VIGIL_VM_OPS_COLLECTION_H */
