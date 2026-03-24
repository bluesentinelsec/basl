/*
 * vm_ops_arith.c — Extracted arithmetic opcode handlers.
 */

#include "vm_ops_arith.h"

#include <limits.h>
#include <string.h>

/* ── Generic binary ops (ADD..EQUAL) ─────────────────────────── */

static vigil_status_t generic_binary_float(vigil_vm_t *vm, double dl, double dr, vigil_opcode_t op, vigil_value_t *out,
                                           vigil_error_t *error)
{
    switch (op)
    {
    case VIGIL_OPCODE_ADD:
        vigil_value_init_float(out, dl + dr);
        return VIGIL_STATUS_OK;
    case VIGIL_OPCODE_SUBTRACT:
        vigil_value_init_float(out, dl - dr);
        return VIGIL_STATUS_OK;
    case VIGIL_OPCODE_MULTIPLY:
        vigil_value_init_float(out, dl * dr);
        return VIGIL_STATUS_OK;
    case VIGIL_OPCODE_DIVIDE:
        vigil_value_init_float(out, dl / dr);
        return VIGIL_STATUS_OK;
    case VIGIL_OPCODE_GREATER:
        vigil_value_init_bool(out, dl > dr);
        return VIGIL_STATUS_OK;
    case VIGIL_OPCODE_LESS:
        vigil_value_init_bool(out, dl < dr);
        return VIGIL_STATUS_OK;
    default:
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                   "float operands are not supported for this opcode", error);
    }
}

static uint64_t bitwise_uint(uint64_t ul, uint64_t ur, vigil_opcode_t op)
{
    if (op == VIGIL_OPCODE_BITWISE_AND)
        return ul & ur;
    if (op == VIGIL_OPCODE_BITWISE_OR)
        return ul | ur;
    return ul ^ ur;
}

static int64_t bitwise_int(int64_t il, int64_t ir, vigil_opcode_t op)
{
    if (op == VIGIL_OPCODE_BITWISE_AND)
        return il & ir;
    if (op == VIGIL_OPCODE_BITWISE_OR)
        return il | ir;
    return il ^ ir;
}

static vigil_status_t generic_binary_uint(uint64_t ul, uint64_t ur, vigil_opcode_t op, uint64_t *result)
{
    switch (op)
    {
    case VIGIL_OPCODE_ADD:
        return vigil_vm_checked_uadd(ul, ur, result);
    case VIGIL_OPCODE_SUBTRACT:
        return vigil_vm_checked_usubtract(ul, ur, result);
    case VIGIL_OPCODE_MULTIPLY:
        return vigil_vm_checked_umultiply(ul, ur, result);
    case VIGIL_OPCODE_DIVIDE:
        return vigil_vm_checked_udivide(ul, ur, result);
    case VIGIL_OPCODE_MODULO:
        return vigil_vm_checked_umodulo(ul, ur, result);
    case VIGIL_OPCODE_SHIFT_LEFT:
        return vigil_vm_checked_ushift_left(ul, ur, result);
    case VIGIL_OPCODE_SHIFT_RIGHT:
        return vigil_vm_checked_ushift_right(ul, ur, result);
    default:
        *result = bitwise_uint(ul, ur, op);
        return VIGIL_STATUS_OK;
    }
}

static vigil_status_t generic_binary_int(int64_t il, int64_t ir, vigil_opcode_t op, int64_t *result)
{
    switch (op)
    {
    case VIGIL_OPCODE_ADD:
        return vigil_vm_checked_add(il, ir, result);
    case VIGIL_OPCODE_SUBTRACT:
        return vigil_vm_checked_subtract(il, ir, result);
    case VIGIL_OPCODE_MULTIPLY:
        return vigil_vm_checked_multiply(il, ir, result);
    case VIGIL_OPCODE_DIVIDE:
        return vigil_vm_checked_divide(il, ir, result);
    case VIGIL_OPCODE_MODULO:
        return vigil_vm_checked_modulo(il, ir, result);
    case VIGIL_OPCODE_SHIFT_LEFT:
        return vigil_vm_checked_shift_left(il, ir, result);
    case VIGIL_OPCODE_SHIFT_RIGHT:
        return vigil_vm_checked_shift_right(il, ir, result);
    default:
        *result = bitwise_int(il, ir, op);
        return VIGIL_STATUS_OK;
    }
}

static void generic_binary_compare(vigil_value_t *left, vigil_value_t *right, vigil_opcode_t op, vigil_value_t *out)
{
    if (vigil_nanbox_is_uint(*left))
    {
        uint64_t ul = vigil_value_as_uint(left);
        uint64_t ur = vigil_value_as_uint(right);
        vigil_value_init_bool(out, op == VIGIL_OPCODE_GREATER ? ul > ur : ul < ur);
    }
    else
    {
        int64_t il = vigil_value_as_int(left);
        int64_t ir = vigil_value_as_int(right);
        vigil_value_init_bool(out, op == VIGIL_OPCODE_GREATER ? il > ir : il < ir);
    }
}

static vigil_status_t generic_binary_string(vigil_vm_t *vm, vigil_value_t *left, vigil_value_t *right,
                                            vigil_opcode_t op, vigil_value_t *out, vigil_error_t *error)
{
    if (op == VIGIL_OPCODE_ADD)
        return vigil_vm_concat_strings(vm, left, right, out, error);
    if (op == VIGIL_OPCODE_GREATER || op == VIGIL_OPCODE_LESS)
    {
        vigil_object_t *ls = (vigil_object_t *)vigil_nanbox_decode_ptr(*left);
        vigil_object_t *rs = (vigil_object_t *)vigil_nanbox_decode_ptr(*right);
        int cmp = strcmp(vigil_string_object_c_str(ls), vigil_string_object_c_str(rs));
        vigil_value_init_bool(out, op == VIGIL_OPCODE_GREATER ? cmp > 0 : cmp < 0);
        return VIGIL_STATUS_OK;
    }
    (void)error;
    return VIGIL_STATUS_INVALID_ARGUMENT;
}

static bool both_strings(vigil_value_t left, vigil_value_t right)
{
    return vigil_nanbox_is_object(left) && vigil_nanbox_is_object(right) &&
           ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) != NULL &&
           ((vigil_object_t *)vigil_nanbox_decode_ptr(right)) != NULL &&
           vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) == VIGIL_OBJECT_STRING &&
           vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(right))) == VIGIL_OBJECT_STRING;
}

static vigil_status_t generic_binary_integer(vigil_value_t *left, vigil_value_t *right, vigil_opcode_t op,
                                             vigil_value_t *out)
{
    if (op == VIGIL_OPCODE_GREATER || op == VIGIL_OPCODE_LESS)
    {
        generic_binary_compare(left, right, op, out);
        return VIGIL_STATUS_OK;
    }
    if (vigil_nanbox_is_uint(*left))
    {
        uint64_t ur = 0;
        vigil_status_t status = generic_binary_uint(vigil_value_as_uint(left), vigil_value_as_uint(right), op, &ur);
        if (status == VIGIL_STATUS_OK)
            vigil_value_init_uint(out, ur);
        return status;
    }

    {
        int64_t ir = 0;
        vigil_status_t status = generic_binary_int(vigil_value_as_int(left), vigil_value_as_int(right), op, &ir);
        if (status == VIGIL_STATUS_OK)
            vigil_value_init_int(out, ir);
        return status;
    }
}

static bool both_integers_same_kind(vigil_value_t *left, vigil_value_t *right)
{
    return vigil_vm_value_is_integer(left) && vigil_vm_value_is_integer(right) &&
           vigil_value_kind(left) == vigil_value_kind(right);
}

static vigil_status_t generic_binary_dispatch(vigil_vm_t *vm, vigil_value_t *left, vigil_value_t *right,
                                              vigil_opcode_t op, vigil_value_t *out, vigil_error_t *error)
{
    if (op == VIGIL_OPCODE_EQUAL)
    {
        vigil_value_init_bool(out, vigil_vm_values_equal(left, right));
        return VIGIL_STATUS_OK;
    }
    if (both_strings(*left, *right))
        return generic_binary_string(vm, left, right, op, out, error);
    if (vigil_nanbox_is_double(*left) && vigil_nanbox_is_double(*right))
        return generic_binary_float(vm, vigil_nanbox_decode_double(*left), vigil_nanbox_decode_double(*right), op, out,
                                    error);
    if (both_integers_same_kind(left, right))
    {
        vigil_status_t status = generic_binary_integer(left, right, op, out);
        if (status != VIGIL_STATUS_OK)
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                       "integer arithmetic overflow or invalid operation", error);
        return VIGIL_STATUS_OK;
    }
    return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "integer operands are required", error);
}

vigil_status_t vigil_vm_op_generic_binary(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                          vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;

    VIGIL_VM_VALUE_INIT_NIL(&value);
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    status = generic_binary_dispatch(vm, &left, &right, (vigil_opcode_t)code[frame->ip], &value, error);
    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&right);
    if (status != VIGIL_STATUS_OK)
        return status;
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    if (status != VIGIL_STATUS_OK)
        return status;
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

/* ── Specialized i64 arithmetic ──────────────────────────────── */

static vigil_status_t checked_i64_add(int64_t a, int64_t b, int64_t *r)
{
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        return VIGIL_STATUS_INVALID_ARGUMENT;
    *r = a + b;
    return VIGIL_STATUS_OK;
}

static vigil_status_t checked_i64_sub(int64_t a, int64_t b, int64_t *r)
{
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b))
        return VIGIL_STATUS_INVALID_ARGUMENT;
    *r = a - b;
    return VIGIL_STATUS_OK;
}

static bool i64_mul_overflows(int64_t a, int64_t b)
{
    if (a == 0 || b == 0)
        return false;
    if (a > 0 && b > 0)
        return a > INT64_MAX / b;
    if (a > 0) /* b < 0 */
        return b < INT64_MIN / a;
    if (b > 0) /* a < 0 */
        return a < INT64_MIN / b;
    /* a < 0 && b < 0 */
    return a < INT64_MAX / b;
}

static vigil_status_t checked_i64_mul(int64_t a, int64_t b, int64_t *r)
{
    if (i64_mul_overflows(a, b))
        return VIGIL_STATUS_INVALID_ARGUMENT;
    *r = a * b;
    return VIGIL_STATUS_OK;
}

static vigil_status_t fuse_to_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code, size_t code_size,
                                  int64_t r, vigil_error_t *error)
{
    if (frame->ip < code_size && code[frame->ip] == VIGIL_OPCODE_TO_I32)
    {
        if (r < (int64_t)INT32_MIN || r > (int64_t)INT32_MAX)
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 conversion overflow or invalid value",
                                       error);
        frame->ip += 1U;
    }
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_add_sub_i64(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code, size_t code_size,
                                       vigil_error_t *error)
{
    int64_t a, b, r;
    vigil_status_t status;
    vm->stack_count -= 1U;
    b = vigil_nanbox_decode_int(vm->stack[vm->stack_count]);
    vm->stack_count -= 1U;
    a = vigil_nanbox_decode_int(vm->stack[vm->stack_count]);
    if ((vigil_opcode_t)code[frame->ip] == VIGIL_OPCODE_ADD_I64)
        status = checked_i64_add(a, b, &r);
    else
        status = checked_i64_sub(a, b, &r);
    if (status != VIGIL_STATUS_OK)
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "integer overflow", error);
    vm->stack[vm->stack_count] = vigil_nanbox_encode_int(r);
    vm->stack_count += 1U;
    frame->ip += 1U;
    return fuse_to_i32(vm, frame, code, code_size, r, error);
}

vigil_status_t vigil_vm_op_cmp_i64(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code, vigil_error_t *error)
{
    int64_t a, b;
    bool result;
    (void)error;
    vm->stack_count -= 1U;
    b = vigil_nanbox_decode_int(vm->stack[vm->stack_count]);
    vm->stack_count -= 1U;
    a = vigil_nanbox_decode_int(vm->stack[vm->stack_count]);
    switch ((vigil_opcode_t)code[frame->ip])
    {
    case VIGIL_OPCODE_LESS_I64:
        result = a < b;
        break;
    case VIGIL_OPCODE_LESS_EQUAL_I64:
        result = a <= b;
        break;
    case VIGIL_OPCODE_GREATER_I64:
        result = a > b;
        break;
    case VIGIL_OPCODE_GREATER_EQUAL_I64:
        result = a >= b;
        break;
    case VIGIL_OPCODE_EQUAL_I64:
        result = a == b;
        break;
    case VIGIL_OPCODE_NOT_EQUAL_I64:
        result = a != b;
        break;
    default:
        result = false;
        break;
    }
    vm->stack[vm->stack_count] = vigil_nanbox_from_bool(result);
    vm->stack_count += 1U;
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_mul_div_mod_i64(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                           size_t code_size, vigil_error_t *error)
{
    int64_t a, b, r;
    vigil_status_t status;
    vm->stack_count -= 1U;
    b = vigil_nanbox_decode_int(vm->stack[vm->stack_count]);
    vm->stack_count -= 1U;
    a = vigil_nanbox_decode_int(vm->stack[vm->stack_count]);
    switch ((vigil_opcode_t)code[frame->ip])
    {
    case VIGIL_OPCODE_MULTIPLY_I64:
        status = checked_i64_mul(a, b, &r);
        if (status != VIGIL_STATUS_OK)
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "integer overflow", error);
        break;
    case VIGIL_OPCODE_DIVIDE_I64:
        if (b == 0)
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "division by zero", error);
        if (a == INT64_MIN && b == -1)
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "integer overflow", error);
        r = a / b;
        break;
    default: /* MODULO_I64 */
        if (b == 0)
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "division by zero", error);
        r = a % b;
        break;
    }
    vm->stack[vm->stack_count] = vigil_nanbox_encode_int(r);
    vm->stack_count += 1U;
    frame->ip += 1U;
    return fuse_to_i32(vm, frame, code, code_size, r, error);
}

vigil_status_t vigil_vm_op_locals_add_sub_i64(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                              size_t code_size, vigil_error_t *error)
{
    uint32_t idx_a, idx_b;
    int64_t a, b, r;
    vigil_status_t status;
    VIGIL_VM_FAST_READ_U32(code, frame->ip, idx_a);
    VIGIL_VM_FAST_READ_RAW_U32(code, frame->ip, idx_b);
    a = vigil_nanbox_decode_int(vm->stack[frame->base_slot + idx_a]);
    b = vigil_nanbox_decode_int(vm->stack[frame->base_slot + idx_b]);
    if ((vigil_opcode_t)code[frame->ip - 9U] == VIGIL_OPCODE_LOCALS_ADD_I64)
        status = checked_i64_add(a, b, &r);
    else
        status = checked_i64_sub(a, b, &r);
    if (status != VIGIL_STATUS_OK)
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "integer overflow", error);
    vm->stack[vm->stack_count] = vigil_nanbox_encode_int(r);
    vm->stack_count += 1U;
    return fuse_to_i32(vm, frame, code, code_size, r, error);
}

vigil_status_t vigil_vm_op_locals_mul_mod_i64(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                              size_t code_size, vigil_error_t *error)
{
    uint32_t idx_a, idx_b;
    int64_t a, b, r;
    vigil_status_t status;
    VIGIL_VM_FAST_READ_U32(code, frame->ip, idx_a);
    VIGIL_VM_FAST_READ_RAW_U32(code, frame->ip, idx_b);
    a = vigil_nanbox_decode_int(vm->stack[frame->base_slot + idx_a]);
    b = vigil_nanbox_decode_int(vm->stack[frame->base_slot + idx_b]);
    if ((vigil_opcode_t)code[frame->ip - 9U] == VIGIL_OPCODE_LOCALS_MULTIPLY_I64)
    {
        status = checked_i64_mul(a, b, &r);
        if (status != VIGIL_STATUS_OK)
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "integer overflow", error);
    }
    else
    {
        if (b == 0)
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "division by zero", error);
        r = a % b;
    }
    vm->stack[vm->stack_count] = vigil_nanbox_encode_int(r);
    vm->stack_count += 1U;
    return fuse_to_i32(vm, frame, code, code_size, r, error);
}

vigil_status_t vigil_vm_op_locals_cmp_i64(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                          vigil_error_t *error)
{
    uint32_t idx_a, idx_b;
    int64_t a, b;
    bool result;
    (void)error;
    VIGIL_VM_FAST_READ_U32(code, frame->ip, idx_a);
    VIGIL_VM_FAST_READ_RAW_U32(code, frame->ip, idx_b);
    a = vigil_nanbox_decode_int(vm->stack[frame->base_slot + idx_a]);
    b = vigil_nanbox_decode_int(vm->stack[frame->base_slot + idx_b]);
    switch ((vigil_opcode_t)code[frame->ip - 9U])
    {
    case VIGIL_OPCODE_LOCALS_LESS_I64:
        result = a < b;
        break;
    case VIGIL_OPCODE_LOCALS_LESS_EQUAL_I64:
        result = a <= b;
        break;
    case VIGIL_OPCODE_LOCALS_GREATER_I64:
        result = a > b;
        break;
    case VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I64:
        result = a >= b;
        break;
    case VIGIL_OPCODE_LOCALS_EQUAL_I64:
        result = a == b;
        break;
    case VIGIL_OPCODE_LOCALS_NOT_EQUAL_I64:
        result = a != b;
        break;
    default:
        result = false;
        break;
    }
    vm->stack[vm->stack_count] = vigil_nanbox_from_bool(result);
    vm->stack_count += 1U;
    return VIGIL_STATUS_OK;
}

/* ── Specialized i32 arithmetic ──────────────────────────────── */

vigil_status_t vigil_vm_op_add_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    int32_t a, b, r;
    vm->stack_count -= 1U;
    b = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
    vm->stack_count -= 1U;
    a = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
    if (VIGIL_I32_ADD_OVERFLOW(a, b, &r))
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
    vm->stack[vm->stack_count] = vigil_nanbox_encode_i32(r);
    vm->stack_count += 1U;
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_sub_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    int32_t a, b, r;
    vm->stack_count -= 1U;
    b = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
    vm->stack_count -= 1U;
    a = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
    if (VIGIL_I32_SUB_OVERFLOW(a, b, &r))
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
    vm->stack[vm->stack_count] = vigil_nanbox_encode_i32(r);
    vm->stack_count += 1U;
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_mul_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    int32_t a, b, r;
    vm->stack_count -= 1U;
    b = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
    vm->stack_count -= 1U;
    a = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
    if (VIGIL_I32_MUL_OVERFLOW(a, b, &r))
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
    vm->stack[vm->stack_count] = vigil_nanbox_encode_i32(r);
    vm->stack_count += 1U;
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_div_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    int32_t a, b, r;
    vm->stack_count -= 1U;
    b = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
    vm->stack_count -= 1U;
    a = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
    if (b == 0 || (a == INT32_MIN && b == -1))
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
    r = a / b;
    vm->stack[vm->stack_count] = vigil_nanbox_encode_i32(r);
    vm->stack_count += 1U;
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_mod_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    int32_t a, b, r;
    vm->stack_count -= 1U;
    b = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
    vm->stack_count -= 1U;
    a = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
    if (b == 0 || (a == INT32_MIN && b == -1))
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
    r = a % b;
    vm->stack[vm->stack_count] = vigil_nanbox_encode_i32(r);
    vm->stack_count += 1U;
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_cmp_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code, vigil_error_t *error)
{
    int32_t a, b;
    bool result;
    (void)error;
    vm->stack_count -= 1U;
    b = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
    vm->stack_count -= 1U;
    a = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
    switch ((vigil_opcode_t)code[frame->ip])
    {
    case VIGIL_OPCODE_LESS_I32:
        result = a < b;
        break;
    case VIGIL_OPCODE_LESS_EQUAL_I32:
        result = a <= b;
        break;
    case VIGIL_OPCODE_GREATER_I32:
        result = a > b;
        break;
    case VIGIL_OPCODE_GREATER_EQUAL_I32:
        result = a >= b;
        break;
    case VIGIL_OPCODE_EQUAL_I32:
        result = a == b;
        break;
    case VIGIL_OPCODE_NOT_EQUAL_I32:
        result = a != b;
        break;
    default:
        result = false;
        break;
    }
    vm->stack[vm->stack_count] = vigil_nanbox_from_bool(result);
    vm->stack_count += 1U;
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_locals_arith_i32_store(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                                  vigil_error_t *error)
{
    uint32_t dst, idx_a, idx_b;
    int32_t a, b, r;
    vigil_opcode_t op;
    VIGIL_VM_FAST_READ_U32(code, frame->ip, dst);
    VIGIL_VM_FAST_READ_RAW_U32(code, frame->ip, idx_a);
    VIGIL_VM_FAST_READ_RAW_U32(code, frame->ip, idx_b);
    a = vigil_nanbox_decode_i32(vm->stack[frame->base_slot + idx_a]);
    b = vigil_nanbox_decode_i32(vm->stack[frame->base_slot + idx_b]);
    op = (vigil_opcode_t)code[frame->ip - 13U];
    switch (op)
    {
    case VIGIL_OPCODE_LOCALS_ADD_I32_STORE:
        if (VIGIL_I32_ADD_OVERFLOW(a, b, &r))
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
        break;
    case VIGIL_OPCODE_LOCALS_SUBTRACT_I32_STORE:
        if (VIGIL_I32_SUB_OVERFLOW(a, b, &r))
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
        break;
    case VIGIL_OPCODE_LOCALS_MULTIPLY_I32_STORE:
        if (VIGIL_I32_MUL_OVERFLOW(a, b, &r))
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
        break;
    default: /* LOCALS_MODULO_I32_STORE */
        if (b == 0 || (a == INT32_MIN && b == -1))
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
        r = a % b;
        break;
    }
    vm->stack[frame->base_slot + dst] = vigil_nanbox_encode_i32(r);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_locals_cmp_i32_store(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                                vigil_error_t *error)
{
    uint32_t dst, idx_a, idx_b;
    int32_t a, b;
    bool result;
    (void)error;
    VIGIL_VM_FAST_READ_U32(code, frame->ip, dst);
    VIGIL_VM_FAST_READ_RAW_U32(code, frame->ip, idx_a);
    VIGIL_VM_FAST_READ_RAW_U32(code, frame->ip, idx_b);
    a = vigil_nanbox_decode_i32(vm->stack[frame->base_slot + idx_a]);
    b = vigil_nanbox_decode_i32(vm->stack[frame->base_slot + idx_b]);
    switch ((vigil_opcode_t)code[frame->ip - 13U])
    {
    case VIGIL_OPCODE_LOCALS_LESS_I32_STORE:
        result = a < b;
        break;
    case VIGIL_OPCODE_LOCALS_LESS_EQUAL_I32_STORE:
        result = a <= b;
        break;
    case VIGIL_OPCODE_LOCALS_GREATER_I32_STORE:
        result = a > b;
        break;
    case VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I32_STORE:
        result = a >= b;
        break;
    case VIGIL_OPCODE_LOCALS_EQUAL_I32_STORE:
        result = a == b;
        break;
    case VIGIL_OPCODE_LOCALS_NOT_EQUAL_I32_STORE:
        result = a != b;
        break;
    default:
        result = false;
        break;
    }
    vm->stack[frame->base_slot + dst] = vigil_nanbox_from_bool(result);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_increment_local_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                               vigil_error_t *error)
{
    uint32_t idx;
    int32_t val, delta, r;
    VIGIL_VM_FAST_READ_U32(code, frame->ip, idx);
    delta = (int8_t)code[frame->ip];
    frame->ip += 1U;
    val = vigil_nanbox_decode_i32(vm->stack[frame->base_slot + idx]);
    if (VIGIL_I32_ADD_OVERFLOW(val, delta, &r))
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
    vm->stack[frame->base_slot + idx] = vigil_nanbox_encode_i32(r);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_forloop_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                       vigil_error_t *error)
{
    uint32_t idx, ci, back;
    int32_t val, delta, r, limit;
    uint8_t cmp;
    int cont;
    const vigil_value_t *cv;

    VIGIL_VM_FAST_READ_U32(code, frame->ip, idx);
    delta = (int8_t)code[frame->ip];
    frame->ip += 1U;
    VIGIL_VM_FAST_READ_RAW_U32(code, frame->ip, ci);
    cmp = code[frame->ip];
    frame->ip += 1U;
    VIGIL_VM_FAST_READ_RAW_U32(code, frame->ip, back);

    val = vigil_nanbox_decode_i32(vm->stack[frame->base_slot + idx]);
    if (VIGIL_I32_ADD_OVERFLOW(val, delta, &r))
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
    vm->stack[frame->base_slot + idx] = vigil_nanbox_encode_i32(r);

    cv = VIGIL_VM_CHUNK_CONSTANT(frame->chunk, (size_t)ci);
    limit = vigil_nanbox_decode_i32(*cv);

    switch (cmp)
    {
    case 0:
        cont = r < limit;
        break;
    case 1:
        cont = r <= limit;
        break;
    case 2:
        cont = r > limit;
        break;
    case 3:
        cont = r >= limit;
        break;
    case 4:
        cont = r != limit;
        break;
    default:
        cont = 0;
        break;
    }
    if (cont)
        frame->ip -= (size_t)back;
    else
    {
        vm->stack[vm->stack_count] = VIGIL_NANBOX_FALSE;
        vm->stack_count += 1U;
    }
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_increment_local_i64(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                               vigil_error_t *error)
{
    uint32_t idx;
    int64_t val, r;
    int64_t delta;
    VIGIL_VM_FAST_READ_U32(code, frame->ip, idx);
    delta = (int64_t)(int8_t)code[frame->ip];
    frame->ip += 1U;
    val = vigil_nanbox_decode_int(vm->stack[frame->base_slot + idx]);
    if (checked_i64_add(val, delta, &r) != VIGIL_STATUS_OK)
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i64 overflow", error);
    vm->stack[frame->base_slot + idx] = vigil_nanbox_encode_int(r);
    return VIGIL_STATUS_OK;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_forloop_i64(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                       vigil_error_t *error)
{
    uint32_t idx, ci, back;
    int64_t val, delta, r, limit;
    uint8_t cmp;
    int cont;
    const vigil_value_t *cv;

    VIGIL_VM_FAST_READ_U32(code, frame->ip, idx);
    delta = (int64_t)(int8_t)code[frame->ip];
    frame->ip += 1U;
    VIGIL_VM_FAST_READ_RAW_U32(code, frame->ip, ci);
    cmp = code[frame->ip];
    frame->ip += 1U;
    VIGIL_VM_FAST_READ_RAW_U32(code, frame->ip, back);

    val = vigil_nanbox_decode_int(vm->stack[frame->base_slot + idx]);
    if (checked_i64_add(val, delta, &r) != VIGIL_STATUS_OK)
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i64 overflow", error);
    vm->stack[frame->base_slot + idx] = vigil_nanbox_encode_int(r);

    cv = VIGIL_VM_CHUNK_CONSTANT(frame->chunk, (size_t)ci);
    limit = vigil_nanbox_decode_int(*cv);

    switch (cmp)
    {
    case 0:
        cont = r < limit;
        break;
    case 1:
        cont = r <= limit;
        break;
    case 2:
        cont = r > limit;
        break;
    case 3:
        cont = r >= limit;
        break;
    case 4:
        cont = r != limit;
        break;
    default:
        cont = 0;
        break;
    }
    if (cont)
        frame->ip -= (size_t)back;
    else
    {
        vm->stack[vm->stack_count] = VIGIL_NANBOX_FALSE;
        vm->stack_count += 1U;
    }
    return VIGIL_STATUS_OK;
}
