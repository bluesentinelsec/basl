/*
 * vigil_regvm.c — Stack-to-register bytecode translator and register VM.
 *
 * Translation strategy:
 *   1. First pass: scan stack bytecode to build a jump-target set and
 *      count locals.
 *   2. Second pass: abstract-interpret the stack, emitting register
 *      instructions.  Maintain a mapping from stack-bytecode offset
 *      to register-instruction index for jump patching.
 *   3. Third pass: patch jump targets.
 */

#include "internal/vigil_regvm.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_nanbox.h"
#include "internal/vigil_vm_internal.h"
#include "value_internal.h"
#include "vigil/chunk.h"
#include "vm_ops_collection.h"
#include "vm_ops_convert.h"
#include "vm_ops_string.h"

/* ── Chunk lifecycle ───────────────────────────────────────────── */

void vigil_reg_chunk_init(vigil_reg_chunk_t *rc)
{
    memset(rc, 0, sizeof(*rc));
}

void vigil_reg_chunk_free(vigil_reg_chunk_t *rc, vigil_runtime_t *runtime)
{
    (void)runtime;
    free(rc->code);
    free(rc->span_map);
    memset(rc, 0, sizeof(*rc));
}

/* ── Emit helper ───────────────────────────────────────────────── */

static vigil_status_t emit(vigil_reg_chunk_t *rc, vigil_reg_instr_t instr, size_t span_idx)
{
    if (rc->code_count >= rc->code_capacity)
    {
        size_t new_cap = rc->code_capacity < 128 ? 128 : rc->code_capacity * 2;
        vigil_reg_instr_t *nc = realloc(rc->code, new_cap * sizeof(*nc));
        if (!nc)
            return VIGIL_STATUS_OUT_OF_MEMORY;
        size_t *ns = realloc(rc->span_map, new_cap * sizeof(*ns));
        if (!ns)
        {
            rc->code = nc;
            rc->code_capacity = new_cap;
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        rc->code = nc;
        rc->span_map = ns;
        rc->code_capacity = new_cap;
    }
    rc->code[rc->code_count] = instr;
    rc->span_map[rc->code_count] = span_idx;
    rc->code_count++;
    return VIGIL_STATUS_OK;
}

static void regvm_release_value_range(vigil_value_t *values, size_t count)
{
    size_t i;

    if (values == NULL)
        return;

    for (i = 0U; i < count; i++)
        vigil_value_release(&values[i]);
}

static int regvm_compare_strings(vigil_value_t left, vigil_value_t right, int *out_cmp)
{
    const char *left_text;
    const char *right_text;
    size_t left_length;
    size_t right_length;
    size_t common_length;
    int compared;

    if (out_cmp == NULL)
        return 0;

    if (!vigil_vm_get_string_parts(&left, &left_text, &left_length) ||
        !vigil_vm_get_string_parts(&right, &right_text, &right_length))
        return 0;

    common_length = left_length < right_length ? left_length : right_length;
    compared = memcmp(left_text, right_text, common_length);
    if (compared == 0)
    {
        if (left_length < right_length)
            compared = -1;
        else if (left_length > right_length)
            compared = 1;
    }

    *out_cmp = compared;
    return 1;
}

static void regvm_clear_value_range(vigil_value_t *values, size_t count)
{
    size_t i;

    if (values == NULL)
        return;

    for (i = 0U; i < count; i++)
        values[i] = VIGIL_NANBOX_NIL;
}

static void regvm_release_and_clear_value_range(vigil_value_t *values, size_t count)
{
    regvm_release_value_range(values, count);
    regvm_clear_value_range(values, count);
}

static void regvm_discard_isolated_call_values(vigil_vm_t *vm, size_t orig_base, size_t arg_base, size_t count)
{
    if (vm == NULL || arg_base == orig_base || count == 0U)
        return;

    regvm_release_and_clear_value_range(&vm->stack[arg_base], count);
}

static void regvm_swap_u8(uint8_t *left, uint8_t *right)
{
    uint8_t tmp;

    if (left == NULL || right == NULL)
        return;

    tmp = *left;
    *left = *right;
    *right = tmp;
}

static void regvm_move_call_results(vigil_vm_t *vm, size_t orig_base, size_t arg_base, size_t consumed_count,
                                    size_t temp_count, size_t ret_count)
{
    size_t consumed_tail;
    size_t temp_tail;

    if (vm == NULL)
        return;

    consumed_tail = consumed_count > ret_count ? consumed_count - ret_count : 0U;
    temp_tail = temp_count > ret_count ? temp_count - ret_count : 0U;

    if (arg_base == orig_base)
    {
        regvm_release_and_clear_value_range(&vm->stack[orig_base + ret_count], consumed_tail);
        vm->stack_count = orig_base + ret_count;
        return;
    }

    regvm_release_and_clear_value_range(&vm->stack[orig_base], consumed_count);
    regvm_release_and_clear_value_range(&vm->stack[arg_base + ret_count], temp_tail);
    if (ret_count > 0U)
        memmove(&vm->stack[orig_base], &vm->stack[arg_base], ret_count * sizeof(vigil_value_t));
    regvm_clear_value_range(&vm->stack[arg_base], ret_count);
    vm->stack_count = orig_base + ret_count;
}

static uint8_t regvm_helper_result_base(uint8_t top_reg, uint8_t pop_count)
{
    return (uint8_t)(top_reg + 1U - pop_count);
}

static void regvm_move_helper_results(vigil_vm_t *vm, size_t base, uint8_t src_base, uint8_t dst_base,
                                      uint8_t result_count)
{
    vigil_value_t moved[8];
    size_t source_end;
    size_t i;

    if (vm == NULL || result_count == 0U || src_base == dst_base)
        return;

    source_end = (size_t)src_base + (size_t)result_count;
    for (i = 0U; i < (size_t)result_count; i++)
    {
        moved[i] = vm->stack[base + (size_t)src_base + i];
        vm->stack[base + (size_t)src_base + i] = VIGIL_NANBOX_NIL;
    }

    for (i = 0U; i < (size_t)result_count; i++)
    {
        size_t dst = (size_t)dst_base + i;

        if (dst < (size_t)src_base || dst >= source_end)
            vigil_value_release(&vm->stack[base + dst]);

        vm->stack[base + dst] = moved[i];
    }
}

/* ── Virtual stack ─────────────────────────────────────────────── */

typedef struct
{
    uint8_t regs[256];
    int top;
    uint8_t next_reg;
    uint8_t local_count;
    int locals_done;
    uint8_t need_release; /* 255 = none, else register to release before write */
    uint8_t last_pop[2]; /* last two popped registers (inputs to current op) */
    uint32_t obj_written[8]; /* bitmap: registers that may hold objects */
} vstack_t;

static void vs_init(vstack_t *vs, uint8_t lc)
{
    vs->top = 0;
    vs->next_reg = lc > 0 ? lc : 1;
    vs->local_count = lc;
    vs->locals_done = (lc == 0);
    vs->need_release = 255;
    vs->last_pop[0] = 255; vs->last_pop[1] = 255;
    memset(vs->regs, 0, sizeof(vs->regs));
    memset(vs->obj_written, 0, sizeof(vs->obj_written));
}

static int vs_is_obj(const vstack_t *vs, uint8_t reg)
{
    return (vs->obj_written[reg / 32] >> (reg % 32)) & 1;
}

static void vs_mark_obj(vstack_t *vs, uint8_t reg)
{
    vs->obj_written[reg / 32] |= (uint32_t)1 << (reg % 32);
}

static void vs_clear_obj(vstack_t *vs, uint8_t reg)
{
    vs->obj_written[reg / 32] &= ~((uint32_t)1 << (reg % 32));
}

static uint8_t vs_push(vstack_t *vs)
{
    if (!vs->locals_done && vs->top >= (int)vs->local_count)
        vs->locals_done = 1;
    uint8_t r;
    if (vs->top < (int)vs->local_count || vs->top >= (int)vs->next_reg)
        r = (uint8_t)vs->top;                /* local slot or frontier: identity */
    else
        r = vs->next_reg;                    /* below frontier: fresh */
    vs->regs[vs->top] = r;
    vs->top++;
    if (r >= vs->next_reg)
        vs->next_reg = r + 1;
    /* Emit release if register was previously written and is NOT a current input. */
    if (vs_is_obj(vs, r) && r != vs->last_pop[0] && r != vs->last_pop[1])
        vs->need_release = r;
    else
        vs->need_release = 255;
    vs_mark_obj(vs, r);
    /* Reset pop tracking after push consumes the inputs. */
    vs->last_pop[0] = 255; vs->last_pop[1] = 255;
    return r;
}

static uint8_t vs_push_at(vstack_t *vs, uint8_t reg)
{
    vs->regs[vs->top] = reg;
    vs->top++;
    if (reg >= vs->next_reg)
        vs->next_reg = reg + 1;
    /* Don't release — the op reads from this register before writing. */
    vs->need_release = 255;
    vs_mark_obj(vs, reg);
    return reg;
}

static uint8_t vs_pop(vstack_t *vs)
{
    vs->top--;
    uint8_t r = vs->regs[vs->top];
    /* Track last two pops so vs_push can avoid releasing inputs. */
    vs->last_pop[1] = vs->last_pop[0];
    vs->last_pop[0] = r;
    return r;
}

static uint8_t vs_peek(const vstack_t *vs, int depth)
{
    return vs->regs[vs->top - 1 - depth];
}

static uint8_t vs_result_base(const vstack_t *vs, uint8_t preferred)
{
    if (vs != NULL && vs->top < (int)vs->local_count)
        return (uint8_t)vs->top;

    return preferred;
}

static uint8_t vs_push_result(vstack_t *vs, uint8_t preferred)
{
    return vs_push_at(vs, vs_result_base(vs, preferred));
}

static int vs_uses_reg(const vstack_t *vs, uint8_t reg)
{
    size_t i;
    size_t top;

    if (vs == NULL || vs->top <= 0)
        return 0;

    top = (size_t)vs->top;
    for (i = 0U; i < top; i++)
    {
        if (vs->regs[i] == reg)
            return 1;
    }

    return 0;
}

static int regvm_find_scratch_reg(const vstack_t *current, const vstack_t *target)
{
    int reg;

    for (reg = 0; reg <= UINT8_MAX; reg++)
    {
        uint8_t candidate = (uint8_t)reg;

        if (!vs_uses_reg(current, candidate) && !vs_uses_reg(target, candidate))
            return reg;
    }

    return -1;
}

static vigil_status_t regvm_normalize_stack(vigil_reg_chunk_t *rc, size_t span_idx, vstack_t *current,
                                            const vstack_t *target, uint8_t *max_next_reg, vigil_error_t *error)
{
    uint8_t pending[256];
    size_t slot_count;
    size_t pending_count;
    int scratch_reg;
    int scratch_used;
    size_t i;

    if (current == NULL || target == NULL)
        return VIGIL_STATUS_OK;

    if (current->top != target->top)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "regvm jump target stack depth mismatch");
        return VIGIL_STATUS_INTERNAL;
    }

    slot_count = current->top > 0 ? (size_t)current->top : 0U;
    memset(pending, 0, sizeof(pending));

    pending_count = 0U;
    for (i = 0U; i < slot_count; i++)
    {
        if (current->regs[i] != target->regs[i])
        {
            pending[i] = 1U;
            pending_count += 1U;
        }
    }

    scratch_reg = -1;
    scratch_used = 0;

    while (pending_count > 0U)
    {
        int progress = 0;

        for (i = 0U; i < slot_count; i++)
        {
            size_t j;
            uint8_t src;
            uint8_t dst;
            int dst_is_live_source;
            vigil_status_t status;

            if (pending[i] == 0U)
                continue;

            src = current->regs[i];
            dst = target->regs[i];
            dst_is_live_source = 0;
            for (j = 0U; j < slot_count; j++)
            {
                if (j != i && pending[j] != 0U && current->regs[j] == dst)
                {
                    dst_is_live_source = 1;
                    break;
                }
            }
            if (dst_is_live_source)
                continue;

            status = emit(rc, vigil_reg_abc(VREG_MOVE, dst, src, 0), span_idx);
            if (status != VIGIL_STATUS_OK)
                return status;

            current->regs[i] = dst;
            pending[i] = 0U;
            pending_count -= 1U;
            progress = 1;
        }

        if (progress)
            continue;

        if (scratch_reg < 0)
        {
            scratch_reg = regvm_find_scratch_reg(current, target);
            if (scratch_reg < 0)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL,
                                        "regvm could not allocate a scratch register for join normalization");
                return VIGIL_STATUS_INTERNAL;
            }
            if (max_next_reg != NULL && (uint8_t)(scratch_reg + 1) > *max_next_reg)
                *max_next_reg = (uint8_t)(scratch_reg + 1);
        }

        for (i = 0U; i < slot_count; i++)
        {
            uint8_t saved_src;
            size_t j;
            vigil_status_t status;

            if (pending[i] == 0U)
                continue;

            saved_src = current->regs[i];
            status = emit(rc, vigil_reg_abc(VREG_MOVE, (uint8_t)scratch_reg, saved_src, 0), span_idx);
            if (status != VIGIL_STATUS_OK)
                return status;

            for (j = 0U; j < slot_count; j++)
            {
                if (pending[j] != 0U && current->regs[j] == saved_src)
                    current->regs[j] = (uint8_t)scratch_reg;
            }

            scratch_used = 1;
            break;
        }
    }

    if (scratch_used)
    {
        vigil_status_t status = emit(rc, vigil_reg_abc(VREG_LOAD_NIL, (uint8_t)scratch_reg, 0, 0), span_idx);

        if (status != VIGIL_STATUS_OK)
            return status;
    }

    return VIGIL_STATUS_OK;
}

/* ── Bytecode readers ──────────────────────────────────────────── */

static uint32_t rd_u32(const uint8_t *c, size_t *ip)
{
    uint32_t v = (uint32_t)c[*ip + 1] | ((uint32_t)c[*ip + 2] << 8) | ((uint32_t)c[*ip + 3] << 16) |
                 ((uint32_t)c[*ip + 4] << 24);
    *ip += 5;
    return v;
}

static uint32_t rd_raw_u32(const uint8_t *c, size_t *ip)
{
    uint32_t v =
        (uint32_t)c[*ip] | ((uint32_t)c[*ip + 1] << 8) | ((uint32_t)c[*ip + 2] << 16) | ((uint32_t)c[*ip + 3] << 24);
    *ip += 4;
    return v;
}

/* Compute the byte size of a stack opcode (including operands). */
static size_t stack_op_size(const uint8_t *code, size_t ip, size_t code_size)
{
    if (ip >= code_size)
        return 1;
    uint8_t op = code[ip];
    switch (op)
    {
    /* 1-byte opcodes (no operands) */
    case VIGIL_OPCODE_NIL:
    case VIGIL_OPCODE_TRUE:
    case VIGIL_OPCODE_FALSE:
    case VIGIL_OPCODE_POP:
    case VIGIL_OPCODE_DUP:
    case VIGIL_OPCODE_DUP_TWO:
    case VIGIL_OPCODE_ADD:
    case VIGIL_OPCODE_SUBTRACT:
    case VIGIL_OPCODE_MULTIPLY:
    case VIGIL_OPCODE_DIVIDE:
    case VIGIL_OPCODE_MODULO:
    case VIGIL_OPCODE_BITWISE_AND:
    case VIGIL_OPCODE_BITWISE_OR:
    case VIGIL_OPCODE_BITWISE_XOR:
    case VIGIL_OPCODE_SHIFT_LEFT:
    case VIGIL_OPCODE_SHIFT_RIGHT:
    case VIGIL_OPCODE_NEGATE:
    case VIGIL_OPCODE_NOT:
    case VIGIL_OPCODE_BITWISE_NOT:
    case VIGIL_OPCODE_TO_I32:
    case VIGIL_OPCODE_TO_I64:
    case VIGIL_OPCODE_TO_U8:
    case VIGIL_OPCODE_TO_U32:
    case VIGIL_OPCODE_TO_U64:
    case VIGIL_OPCODE_TO_F64:
    case VIGIL_OPCODE_TO_STRING:
    case VIGIL_OPCODE_EQUAL:
    case VIGIL_OPCODE_GREATER:
    case VIGIL_OPCODE_LESS:
    case VIGIL_OPCODE_NEW_ERROR:
    case VIGIL_OPCODE_GET_ERROR_KIND:
    case VIGIL_OPCODE_GET_ERROR_MESSAGE:
    case VIGIL_OPCODE_GET_COLLECTION_SIZE:
    case VIGIL_OPCODE_GET_STRING_SIZE:
    case VIGIL_OPCODE_ADD_I64:
    case VIGIL_OPCODE_SUBTRACT_I64:
    case VIGIL_OPCODE_MULTIPLY_I64:
    case VIGIL_OPCODE_DIVIDE_I64:
    case VIGIL_OPCODE_MODULO_I64:
    case VIGIL_OPCODE_LESS_I64:
    case VIGIL_OPCODE_LESS_EQUAL_I64:
    case VIGIL_OPCODE_GREATER_I64:
    case VIGIL_OPCODE_GREATER_EQUAL_I64:
    case VIGIL_OPCODE_EQUAL_I64:
    case VIGIL_OPCODE_NOT_EQUAL_I64:
    case VIGIL_OPCODE_ADD_I32:
    case VIGIL_OPCODE_SUBTRACT_I32:
    case VIGIL_OPCODE_MULTIPLY_I32:
    case VIGIL_OPCODE_DIVIDE_I32:
    case VIGIL_OPCODE_MODULO_I32:
    case VIGIL_OPCODE_LESS_I32:
    case VIGIL_OPCODE_LESS_EQUAL_I32:
    case VIGIL_OPCODE_GREATER_I32:
    case VIGIL_OPCODE_GREATER_EQUAL_I32:
    case VIGIL_OPCODE_EQUAL_I32:
    case VIGIL_OPCODE_NOT_EQUAL_I32:
    case VIGIL_OPCODE_ADD_F64:
    case VIGIL_OPCODE_SUBTRACT_F64:
    case VIGIL_OPCODE_MULTIPLY_F64:
    case VIGIL_OPCODE_DIVIDE_F64:
    case VIGIL_OPCODE_MATH_SIN_F64:
    case VIGIL_OPCODE_MATH_COS_F64:
    case VIGIL_OPCODE_MATH_SQRT_F64:
    case VIGIL_OPCODE_MATH_LOG_F64:
    case VIGIL_OPCODE_MATH_POW_F64:
    case VIGIL_OPCODE_CHAR_FROM_INT:
    case VIGIL_OPCODE_STRING_TO_C:
    case VIGIL_OPCODE_STRING_CONTAINS:
    case VIGIL_OPCODE_STRING_STARTS_WITH:
    case VIGIL_OPCODE_STRING_ENDS_WITH:
    case VIGIL_OPCODE_STRING_TRIM:
    case VIGIL_OPCODE_STRING_TRIM_LEFT:
    case VIGIL_OPCODE_STRING_TRIM_RIGHT:
    case VIGIL_OPCODE_STRING_TO_UPPER:
    case VIGIL_OPCODE_STRING_TO_LOWER:
    case VIGIL_OPCODE_STRING_REPLACE:
    case VIGIL_OPCODE_STRING_SPLIT:
    case VIGIL_OPCODE_STRING_INDEX_OF:
    case VIGIL_OPCODE_STRING_LAST_INDEX_OF:
    case VIGIL_OPCODE_STRING_SUBSTR:
    case VIGIL_OPCODE_STRING_BYTES:
    case VIGIL_OPCODE_STRING_CHAR_AT:
    case VIGIL_OPCODE_STRING_CHAR_COUNT:
    case VIGIL_OPCODE_STRING_REPEAT:
    case VIGIL_OPCODE_STRING_REVERSE:
    case VIGIL_OPCODE_STRING_IS_EMPTY:
    case VIGIL_OPCODE_STRING_COUNT:
    case VIGIL_OPCODE_STRING_TRIM_PREFIX:
    case VIGIL_OPCODE_STRING_TRIM_SUFFIX:
    case VIGIL_OPCODE_STRING_JOIN:
    case VIGIL_OPCODE_STRING_CUT:
    case VIGIL_OPCODE_STRING_FIELDS:
    case VIGIL_OPCODE_STRING_EQUAL_FOLD:
    case VIGIL_OPCODE_GET_INDEX:
    case VIGIL_OPCODE_SET_INDEX:
    case VIGIL_OPCODE_ARRAY_PUSH:
    case VIGIL_OPCODE_ARRAY_POP:
    case VIGIL_OPCODE_ARRAY_GET_SAFE:
    case VIGIL_OPCODE_ARRAY_SET_SAFE:
    case VIGIL_OPCODE_ARRAY_SLICE:
    case VIGIL_OPCODE_ARRAY_CONTAINS:
    case VIGIL_OPCODE_MAP_GET_SAFE:
    case VIGIL_OPCODE_MAP_SET_SAFE:
    case VIGIL_OPCODE_MAP_REMOVE_SAFE:
    case VIGIL_OPCODE_MAP_HAS:
    case VIGIL_OPCODE_MAP_KEYS:
    case VIGIL_OPCODE_MAP_VALUES:
    case VIGIL_OPCODE_GET_MAP_KEY_AT:
    case VIGIL_OPCODE_GET_MAP_VALUE_AT:
    case VIGIL_OPCODE_PARSE_I32:
    case VIGIL_OPCODE_PARSE_F64:
    case VIGIL_OPCODE_PARSE_BOOL:
        return 1;

    /* 5-byte opcodes (opcode + u32) */
    case VIGIL_OPCODE_CONSTANT:
    case VIGIL_OPCODE_GET_LOCAL:
    case VIGIL_OPCODE_SET_LOCAL:
    case VIGIL_OPCODE_GET_GLOBAL:
    case VIGIL_OPCODE_SET_GLOBAL:
    case VIGIL_OPCODE_GET_FUNCTION:
    case VIGIL_OPCODE_GET_CAPTURE:
    case VIGIL_OPCODE_SET_CAPTURE:
    case VIGIL_OPCODE_JUMP:
    case VIGIL_OPCODE_JUMP_IF_FALSE:
    case VIGIL_OPCODE_LOOP:
    case VIGIL_OPCODE_GET_FIELD:
    case VIGIL_OPCODE_SET_FIELD:
    case VIGIL_OPCODE_FORMAT_F64:
    case VIGIL_OPCODE_ADD_F64_STORE:
    case VIGIL_OPCODE_SUBTRACT_F64_STORE:
    case VIGIL_OPCODE_MULTIPLY_F64_STORE:
        return 5;

    /* 5-byte: RETURN has opcode + u32 return count (if present) */
    case VIGIL_OPCODE_RETURN:
        return (ip + 5 <= code_size) ? 5 : 1;

    /* 6-byte: opcode + u32 + u8 */
    case VIGIL_OPCODE_INCREMENT_LOCAL_I32:
    case VIGIL_OPCODE_INCREMENT_LOCAL_I64:
        return 6;

    /* 5-byte: opcode + u32 (jump offset) */
    case VIGIL_OPCODE_LESS_I32_JUMP_IF_FALSE:
    case VIGIL_OPCODE_LESS_EQUAL_I32_JUMP_IF_FALSE:
    case VIGIL_OPCODE_GREATER_I32_JUMP_IF_FALSE:
    case VIGIL_OPCODE_GREATER_EQUAL_I32_JUMP_IF_FALSE:
    case VIGIL_OPCODE_EQUAL_I32_JUMP_IF_FALSE:
    case VIGIL_OPCODE_NOT_EQUAL_I32_JUMP_IF_FALSE:
    case VIGIL_OPCODE_LESS_I64_JUMP_IF_FALSE:
    case VIGIL_OPCODE_LESS_EQUAL_I64_JUMP_IF_FALSE:
    case VIGIL_OPCODE_GREATER_I64_JUMP_IF_FALSE:
    case VIGIL_OPCODE_GREATER_EQUAL_I64_JUMP_IF_FALSE:
    case VIGIL_OPCODE_EQUAL_I64_JUMP_IF_FALSE:
    case VIGIL_OPCODE_NOT_EQUAL_I64_JUMP_IF_FALSE:
        return 5;

    /* 9-byte: opcode + u32 + u32 */
    case VIGIL_OPCODE_NEW_ARRAY:
    case VIGIL_OPCODE_NEW_MAP:
    case VIGIL_OPCODE_NEW_INSTANCE:
    case VIGIL_OPCODE_CALL_SELF:
    case VIGIL_OPCODE_TAIL_CALL:
    case VIGIL_OPCODE_NEW_CLOSURE:
    case VIGIL_OPCODE_LOCALS_ADD_I64:
    case VIGIL_OPCODE_LOCALS_SUBTRACT_I64:
    case VIGIL_OPCODE_LOCALS_MULTIPLY_I64:
    case VIGIL_OPCODE_LOCALS_MODULO_I64:
    case VIGIL_OPCODE_LOCALS_LESS_I64:
    case VIGIL_OPCODE_LOCALS_LESS_EQUAL_I64:
    case VIGIL_OPCODE_LOCALS_GREATER_I64:
    case VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I64:
    case VIGIL_OPCODE_LOCALS_EQUAL_I64:
    case VIGIL_OPCODE_LOCALS_NOT_EQUAL_I64:
    case VIGIL_OPCODE_LOCALS_ADD_F64:
    case VIGIL_OPCODE_LOCALS_SUBTRACT_F64:
    case VIGIL_OPCODE_LOCALS_MULTIPLY_F64:
    case VIGIL_OPCODE_FORMAT_SPEC:
    case VIGIL_OPCODE_DEFER_NEW_INSTANCE:
        return 9;

    /* 5-byte defer: opcode + u32 */
    case VIGIL_OPCODE_DEFER_CALL_VALUE:
        return 5;

    /* 9-byte: opcode + u32 + u32 */
    case VIGIL_OPCODE_CALL_VALUE:
    case VIGIL_OPCODE_CALL_EXTERN:
        return 9;

    /* 13-byte: opcode + u32 + u32 + u32 */
    case VIGIL_OPCODE_CALL:
    case VIGIL_OPCODE_DEFER_CALL:
    case VIGIL_OPCODE_DEFER_CALL_INTERFACE:
    case VIGIL_OPCODE_CALL_NATIVE:
    case VIGIL_OPCODE_DEFER_CALL_NATIVE:
    case VIGIL_OPCODE_LOCALS_ADD_I32_STORE:
    case VIGIL_OPCODE_LOCALS_SUBTRACT_I32_STORE:
    case VIGIL_OPCODE_LOCALS_MULTIPLY_I32_STORE:
    case VIGIL_OPCODE_LOCALS_LESS_I32_STORE:
    case VIGIL_OPCODE_LOCALS_LESS_EQUAL_I32_STORE:
    case VIGIL_OPCODE_LOCALS_GREATER_I32_STORE:
    case VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I32_STORE:
    case VIGIL_OPCODE_LOCALS_EQUAL_I32_STORE:
    case VIGIL_OPCODE_LOCALS_NOT_EQUAL_I32_STORE:
    case VIGIL_OPCODE_LOCALS_MODULO_I32_STORE:
    case VIGIL_OPCODE_LOCALS_ADD_F64_STORE:
    case VIGIL_OPCODE_LOCALS_SUBTRACT_F64_STORE:
    case VIGIL_OPCODE_LOCALS_MULTIPLY_F64_STORE:
        return 13;

    /* 17-byte: opcode + u32 + u32 + u32 + u32 */
    case VIGIL_OPCODE_CALL_INTERFACE:
        return 17;

    /* FORLOOP: opcode + u32 + i8 + u32 + u8 + u32 = 15 bytes */
    case VIGIL_OPCODE_FORLOOP_I32:
    case VIGIL_OPCODE_FORLOOP_I64:
        return 15;

    default:
        return 1; /* unknown — skip 1 byte */
    }
}

/* ── Jump target collection (pass 1) ──────────────────────────── */

/* Build a sorted array of stack-bytecode offsets that are jump targets. */
static size_t *collect_jump_targets(const uint8_t *code, size_t code_size, size_t *out_count)
{
    size_t cap = 64, count = 0;
    size_t *targets = malloc(cap * sizeof(*targets));
    if (!targets)
        return NULL;

    size_t ip = 0;
    while (ip < code_size)
    {
        uint8_t op = code[ip];
        size_t sz = stack_op_size(code, ip, code_size);
        size_t target = 0;
        int has_target = 0;

        if (op == VIGIL_OPCODE_JUMP || op == VIGIL_OPCODE_JUMP_IF_FALSE)
        {
            uint32_t off = (uint32_t)code[ip + 1] | ((uint32_t)code[ip + 2] << 8) | ((uint32_t)code[ip + 3] << 16) |
                           ((uint32_t)code[ip + 4] << 24);
            target = ip + 5 + (size_t)off;
            has_target = 1;
        }
        else if (op == VIGIL_OPCODE_LOOP)
        {
            uint32_t off = (uint32_t)code[ip + 1] | ((uint32_t)code[ip + 2] << 8) | ((uint32_t)code[ip + 3] << 16) |
                           ((uint32_t)code[ip + 4] << 24);
            target = ip + 5 - (size_t)off;
            has_target = 1;
        }
        else if (op >= VIGIL_OPCODE_LESS_I32_JUMP_IF_FALSE && op <= VIGIL_OPCODE_NOT_EQUAL_I64_JUMP_IF_FALSE)
        {
            uint32_t off = (uint32_t)code[ip + 1] | ((uint32_t)code[ip + 2] << 8) | ((uint32_t)code[ip + 3] << 16) |
                           ((uint32_t)code[ip + 4] << 24);
            target = ip + 5 + (size_t)off;
            has_target = 1;
        }
        else if (op == VIGIL_OPCODE_FORLOOP_I32 || op == VIGIL_OPCODE_FORLOOP_I64)
        {
            /* Back offset is at bytes 11..14 */
            uint32_t back = (uint32_t)code[ip + 11] | ((uint32_t)code[ip + 12] << 8) | ((uint32_t)code[ip + 13] << 16) |
                            ((uint32_t)code[ip + 14] << 24);
            target = ip + 15 - (size_t)back;
            has_target = 1;
        }

        if (has_target)
        {
            if (count >= cap)
            {
                cap *= 2;
                size_t *nt = realloc(targets, cap * sizeof(*nt));
                if (!nt)
                {
                    free(targets);
                    return NULL;
                }
                targets = nt;
            }
            targets[count++] = target;
        }
        ip += sz;
    }
    *out_count = count;
    return targets;
}

static int jump_target_contains(const size_t *targets, size_t target_count, size_t target)
{
    size_t i;

    for (i = 0U; i < target_count; i++)
    {
        if (targets[i] == target)
            return 1;
    }

    return 0;
}

/* ── Offset map: stack bytecode offset → register instruction index ── */

typedef struct
{
    size_t *stack_offsets; /* stack bytecode offset for each entry */
    size_t *reg_indices;   /* corresponding register instruction index */
    size_t count;
    size_t capacity;
} offset_map_t;

static void omap_init(offset_map_t *m)
{
    memset(m, 0, sizeof(*m));
}

static void omap_free(offset_map_t *m)
{
    free(m->stack_offsets);
    free(m->reg_indices);
}

static int omap_add(offset_map_t *m, size_t stack_off, size_t reg_idx)
{
    if (m->count >= m->capacity)
    {
        size_t nc = m->capacity < 256 ? 256 : m->capacity * 2;
        size_t *so = realloc(m->stack_offsets, nc * sizeof(*so));
        size_t *ri = realloc(m->reg_indices, nc * sizeof(*ri));
        if (!so || !ri)
        {
            if (so)
                m->stack_offsets = so;
            if (ri)
                m->reg_indices = ri;
            return 0;
        }
        m->stack_offsets = so;
        m->reg_indices = ri;
        m->capacity = nc;
    }
    m->stack_offsets[m->count] = stack_off;
    m->reg_indices[m->count] = reg_idx;
    m->count++;
    return 1;
}

static size_t omap_lookup(const offset_map_t *m, size_t stack_off)
{
    for (size_t i = 0; i < m->count; i++)
        if (m->stack_offsets[i] == stack_off)
            return m->reg_indices[i];
    return (size_t)-1;
}

/* ── Jump patch list ───────────────────────────────────────────── */

typedef struct
{
    size_t reg_instr_idx;    /* index of the register instruction to patch */
    size_t target_stack_off; /* target stack bytecode offset */
    int is_backward;         /* 1 = backward jump (LOOP), 0 = forward */
    int stack_depth;         /* virtual stack depth when jump was emitted */
} jump_patch_t;

typedef struct
{
    jump_patch_t *items;
    size_t count;
    size_t capacity;
} jump_patch_list_t;

static void jpatch_init(jump_patch_list_t *l)
{
    memset(l, 0, sizeof(*l));
}

static void jpatch_free(jump_patch_list_t *l)
{
    free(l->items);
}

static int jpatch_add(jump_patch_list_t *l, size_t reg_idx, size_t target_off, int backward, int stack_depth)
{
    if (l->count >= l->capacity)
    {
        size_t nc = l->capacity < 64 ? 64 : l->capacity * 2;
        jump_patch_t *ni = realloc(l->items, nc * sizeof(*ni));
        if (!ni)
            return 0;
        l->items = ni;
        l->capacity = nc;
    }
    l->items[l->count].reg_instr_idx = reg_idx;
    l->items[l->count].target_stack_off = target_off;
    l->items[l->count].is_backward = backward;
    l->items[l->count].stack_depth = stack_depth;
    l->count++;
    return 1;
}

/* ── Helper: map stack binary op to register op ────────────────── */

static uint8_t map_binop(uint8_t stack_op)
{
    switch (stack_op)
    {
    case VIGIL_OPCODE_ADD:
        return VREG_ADD;
    case VIGIL_OPCODE_SUBTRACT:
        return VREG_SUB;
    case VIGIL_OPCODE_MULTIPLY:
        return VREG_MUL;
    case VIGIL_OPCODE_DIVIDE:
        return VREG_DIV;
    case VIGIL_OPCODE_MODULO:
        return VREG_MOD;
    case VIGIL_OPCODE_ADD_I32:
        return VREG_ADD_I32;
    case VIGIL_OPCODE_SUBTRACT_I32:
        return VREG_SUB_I32;
    case VIGIL_OPCODE_MULTIPLY_I32:
        return VREG_MUL_I32;
    case VIGIL_OPCODE_DIVIDE_I32:
        return VREG_DIV_I32;
    case VIGIL_OPCODE_MODULO_I32:
        return VREG_MOD_I32;
    case VIGIL_OPCODE_ADD_I64:
        return VREG_ADD_I64;
    case VIGIL_OPCODE_SUBTRACT_I64:
        return VREG_SUB_I64;
    case VIGIL_OPCODE_MULTIPLY_I64:
        return VREG_MUL_I64;
    case VIGIL_OPCODE_DIVIDE_I64:
        return VREG_DIV_I64;
    case VIGIL_OPCODE_MODULO_I64:
        return VREG_MOD_I64;
    case VIGIL_OPCODE_ADD_F64:
        return VREG_ADD_F64;
    case VIGIL_OPCODE_SUBTRACT_F64:
        return VREG_SUB_F64;
    case VIGIL_OPCODE_MULTIPLY_F64:
        return VREG_MUL_F64;
    case VIGIL_OPCODE_DIVIDE_F64:
        return VREG_DIV_F64;
    case VIGIL_OPCODE_EQUAL:
        return VREG_EQ;
    case VIGIL_OPCODE_LESS:
        return VREG_LT;
    case VIGIL_OPCODE_LESS_I32:
        return VREG_LT_I32;
    case VIGIL_OPCODE_LESS_EQUAL_I32:
        return VREG_LE_I32;
    case VIGIL_OPCODE_GREATER_I32:
        return VREG_GT_I32;
    case VIGIL_OPCODE_GREATER_EQUAL_I32:
        return VREG_GE_I32;
    case VIGIL_OPCODE_EQUAL_I32:
        return VREG_EQ_I32;
    case VIGIL_OPCODE_NOT_EQUAL_I32:
        return VREG_NE_I32;
    case VIGIL_OPCODE_LESS_I64:
        return VREG_LT_I64;
    case VIGIL_OPCODE_LESS_EQUAL_I64:
        return VREG_LE_I64;
    case VIGIL_OPCODE_GREATER_I64:
        return VREG_GT_I64;
    case VIGIL_OPCODE_GREATER_EQUAL_I64:
        return VREG_GE_I64;
    case VIGIL_OPCODE_EQUAL_I64:
        return VREG_EQ_I64;
    case VIGIL_OPCODE_NOT_EQUAL_I64:
        return VREG_NE_I64;
    case VIGIL_OPCODE_BITWISE_AND:
        return VREG_BAND;
    case VIGIL_OPCODE_BITWISE_OR:
        return VREG_BOR;
    case VIGIL_OPCODE_BITWISE_XOR:
        return VREG_BXOR;
    case VIGIL_OPCODE_SHIFT_LEFT:
        return VREG_SHL;
    case VIGIL_OPCODE_SHIFT_RIGHT:
        return VREG_SHR;
    default:
        return VREG_ADD; /* fallback */
    }
}

static uint8_t map_unary(uint8_t stack_op)
{
    switch (stack_op)
    {
    case VIGIL_OPCODE_NEGATE:
        return VREG_NEG;
    case VIGIL_OPCODE_NOT:
        return VREG_NOT;
    case VIGIL_OPCODE_BITWISE_NOT:
        return VREG_BNOT;
    default:
        return VREG_NEG;
    }
}

static uint8_t map_conv(uint8_t stack_op)
{
    switch (stack_op)
    {
    case VIGIL_OPCODE_TO_I32:
        return VREG_TO_I32;
    case VIGIL_OPCODE_TO_I64:
        return VREG_TO_I64;
    case VIGIL_OPCODE_TO_U8:
        return VREG_TO_U8;
    case VIGIL_OPCODE_TO_U32:
        return VREG_TO_U32;
    case VIGIL_OPCODE_TO_U64:
        return VREG_TO_U64;
    case VIGIL_OPCODE_TO_F64:
        return VREG_TO_F64;
    case VIGIL_OPCODE_TO_STRING:
        return VREG_TO_STRING;
    default:
        return VREG_TO_I32;
    }
}

static uint8_t map_math(uint8_t stack_op)
{
    switch (stack_op)
    {
    case VIGIL_OPCODE_MATH_SIN_F64:
        return VREG_MATH_SIN;
    case VIGIL_OPCODE_MATH_COS_F64:
        return VREG_MATH_COS;
    case VIGIL_OPCODE_MATH_SQRT_F64:
        return VREG_MATH_SQRT;
    case VIGIL_OPCODE_MATH_LOG_F64:
        return VREG_MATH_LOG;
    default:
        return VREG_MATH_SIN;
    }
}

/* Map LOCALS_*_I32_STORE three-address ops to register ops. */
static uint8_t map_locals_i32_store(uint8_t stack_op)
{
    switch (stack_op)
    {
    case VIGIL_OPCODE_LOCALS_ADD_I32_STORE:
        return VREG_ADD_I32;
    case VIGIL_OPCODE_LOCALS_SUBTRACT_I32_STORE:
        return VREG_SUB_I32;
    case VIGIL_OPCODE_LOCALS_MULTIPLY_I32_STORE:
        return VREG_MUL_I32;
    case VIGIL_OPCODE_LOCALS_MODULO_I32_STORE:
        return VREG_MOD_I32;
    case VIGIL_OPCODE_LOCALS_LESS_I32_STORE:
        return VREG_LT_I32;
    case VIGIL_OPCODE_LOCALS_LESS_EQUAL_I32_STORE:
        return VREG_LE_I32;
    case VIGIL_OPCODE_LOCALS_GREATER_I32_STORE:
        return VREG_GT_I32;
    case VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I32_STORE:
        return VREG_GE_I32;
    case VIGIL_OPCODE_LOCALS_EQUAL_I32_STORE:
        return VREG_EQ_I32;
    case VIGIL_OPCODE_LOCALS_NOT_EQUAL_I32_STORE:
        return VREG_NE_I32;
    default:
        return VREG_ADD_I32;
    }
}

/* Map LOCALS_*_I64 two-address ops to register ops. */
static uint8_t map_locals_i64(uint8_t stack_op)
{
    switch (stack_op)
    {
    case VIGIL_OPCODE_LOCALS_ADD_I64:
        return VREG_ADD_I64;
    case VIGIL_OPCODE_LOCALS_SUBTRACT_I64:
        return VREG_SUB_I64;
    case VIGIL_OPCODE_LOCALS_MULTIPLY_I64:
        return VREG_MUL_I64;
    case VIGIL_OPCODE_LOCALS_MODULO_I64:
        return VREG_MOD_I64;
    case VIGIL_OPCODE_LOCALS_LESS_I64:
        return VREG_LT_I64;
    case VIGIL_OPCODE_LOCALS_LESS_EQUAL_I64:
        return VREG_LE_I64;
    case VIGIL_OPCODE_LOCALS_GREATER_I64:
        return VREG_GT_I64;
    case VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I64:
        return VREG_GE_I64;
    case VIGIL_OPCODE_LOCALS_EQUAL_I64:
        return VREG_EQ_I64;
    case VIGIL_OPCODE_LOCALS_NOT_EQUAL_I64:
        return VREG_NE_I64;
    default:
        return VREG_ADD_I64;
    }
}

/* Map LOCALS_*_F64 two-address ops. */
static uint8_t map_locals_f64(uint8_t stack_op)
{
    switch (stack_op)
    {
    case VIGIL_OPCODE_LOCALS_ADD_F64:
        return VREG_ADD_F64;
    case VIGIL_OPCODE_LOCALS_SUBTRACT_F64:
        return VREG_SUB_F64;
    case VIGIL_OPCODE_LOCALS_MULTIPLY_F64:
        return VREG_MUL_F64;
    default:
        return VREG_ADD_F64;
    }
}

/* Map LOCALS_*_F64_STORE three-address ops. */
static uint8_t map_locals_f64_store(uint8_t stack_op)
{
    switch (stack_op)
    {
    case VIGIL_OPCODE_LOCALS_ADD_F64_STORE:
        return VREG_ADD_F64;
    case VIGIL_OPCODE_LOCALS_SUBTRACT_F64_STORE:
        return VREG_SUB_F64;
    case VIGIL_OPCODE_LOCALS_MULTIPLY_F64_STORE:
        return VREG_MUL_F64;
    default:
        return VREG_ADD_F64;
    }
}

/* Map ADD_F64_STORE etc. */
static uint8_t map_f64_store(uint8_t stack_op)
{
    switch (stack_op)
    {
    case VIGIL_OPCODE_ADD_F64_STORE:
        return VREG_ADD_F64;
    case VIGIL_OPCODE_SUBTRACT_F64_STORE:
        return VREG_SUB_F64;
    case VIGIL_OPCODE_MULTIPLY_F64_STORE:
        return VREG_MUL_F64;
    default:
        return VREG_ADD_F64;
    }
}

/* ── Main translation pass ─────────────────────────────────────── */

/* ── Translatability check ──────────────────────────────────────
   Returns 1 if the chunk uses only opcodes the register VM handles.
   This is a conservative check — we reject any function that calls
   other Vigil functions, uses string ops, or other complex features. */

int vigil_reg_chunk_is_translatable(const vigil_chunk_t *stack_chunk)
{
    const uint8_t *code = stack_chunk->code.data;
    size_t code_size = stack_chunk->code.length;
    size_t ip = 0;

    while (ip < code_size)
    {
        uint8_t op = code[ip];
        switch (op)
        {
        /* Supported opcodes */
        case VIGIL_OPCODE_CONSTANT:
        case VIGIL_OPCODE_NIL:
        case VIGIL_OPCODE_TRUE:
        case VIGIL_OPCODE_FALSE:
        case VIGIL_OPCODE_RETURN: {
            /* Reject multi-value return (count > 1). */
            if (op == VIGIL_OPCODE_RETURN && ip + 5 <= code_size)
            {
                uint32_t rc = (uint32_t)code[ip + 1] | ((uint32_t)code[ip + 2] << 8) | ((uint32_t)code[ip + 3] << 16) |
                              ((uint32_t)code[ip + 4] << 24);
                if (rc > 1)
                    return 0;
            }
            break;
        }
        case VIGIL_OPCODE_POP:
        case VIGIL_OPCODE_GET_LOCAL:
        case VIGIL_OPCODE_SET_LOCAL:
        case VIGIL_OPCODE_JUMP:
        case VIGIL_OPCODE_LOOP:
        case VIGIL_OPCODE_ADD_I32:
        case VIGIL_OPCODE_SUBTRACT_I32:
        case VIGIL_OPCODE_MULTIPLY_I32:
        case VIGIL_OPCODE_DIVIDE_I32:
        case VIGIL_OPCODE_MODULO_I32:
        case VIGIL_OPCODE_LESS_I32:
        case VIGIL_OPCODE_LESS_EQUAL_I32:
        case VIGIL_OPCODE_GREATER_I32:
        case VIGIL_OPCODE_GREATER_EQUAL_I32:
        case VIGIL_OPCODE_EQUAL_I32:
        case VIGIL_OPCODE_NOT_EQUAL_I32:
        case VIGIL_OPCODE_ADD_I64:
        case VIGIL_OPCODE_SUBTRACT_I64:
        case VIGIL_OPCODE_MULTIPLY_I64:
        case VIGIL_OPCODE_DIVIDE_I64:
        case VIGIL_OPCODE_MODULO_I64:
        case VIGIL_OPCODE_LESS_I64:
        case VIGIL_OPCODE_LESS_EQUAL_I64:
        case VIGIL_OPCODE_GREATER_I64:
        case VIGIL_OPCODE_GREATER_EQUAL_I64:
        case VIGIL_OPCODE_EQUAL_I64:
        case VIGIL_OPCODE_NOT_EQUAL_I64:
        case VIGIL_OPCODE_ADD_F64:
        case VIGIL_OPCODE_SUBTRACT_F64:
        case VIGIL_OPCODE_MULTIPLY_F64:
        case VIGIL_OPCODE_DIVIDE_F64:
        case VIGIL_OPCODE_LOCALS_ADD_I64:
        case VIGIL_OPCODE_LOCALS_SUBTRACT_I64:
        case VIGIL_OPCODE_LOCALS_MULTIPLY_I64:
        case VIGIL_OPCODE_LOCALS_MODULO_I64:
        case VIGIL_OPCODE_LOCALS_LESS_I64:
        case VIGIL_OPCODE_LOCALS_LESS_EQUAL_I64:
        case VIGIL_OPCODE_LOCALS_GREATER_I64:
        case VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I64:
        case VIGIL_OPCODE_LOCALS_EQUAL_I64:
        case VIGIL_OPCODE_LOCALS_NOT_EQUAL_I64:
        case VIGIL_OPCODE_LOCALS_ADD_F64:
        case VIGIL_OPCODE_LOCALS_SUBTRACT_F64:
        case VIGIL_OPCODE_LOCALS_MULTIPLY_F64:
        case VIGIL_OPCODE_LOCALS_ADD_I32_STORE:
        case VIGIL_OPCODE_LOCALS_SUBTRACT_I32_STORE:
        case VIGIL_OPCODE_LOCALS_MULTIPLY_I32_STORE:
        case VIGIL_OPCODE_LOCALS_LESS_I32_STORE:
        case VIGIL_OPCODE_LOCALS_LESS_EQUAL_I32_STORE:
        case VIGIL_OPCODE_LOCALS_GREATER_I32_STORE:
        case VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I32_STORE:
        case VIGIL_OPCODE_LOCALS_EQUAL_I32_STORE:
        case VIGIL_OPCODE_LOCALS_NOT_EQUAL_I32_STORE:
        case VIGIL_OPCODE_LOCALS_MODULO_I32_STORE:
        case VIGIL_OPCODE_LOCALS_ADD_F64_STORE:
        case VIGIL_OPCODE_LOCALS_SUBTRACT_F64_STORE:
        case VIGIL_OPCODE_LOCALS_MULTIPLY_F64_STORE:
        case VIGIL_OPCODE_ADD_F64_STORE:
        case VIGIL_OPCODE_SUBTRACT_F64_STORE:
        case VIGIL_OPCODE_MULTIPLY_F64_STORE:
        case VIGIL_OPCODE_INCREMENT_LOCAL_I32:
        case VIGIL_OPCODE_INCREMENT_LOCAL_I64:
        case VIGIL_OPCODE_FORLOOP_I32:
        case VIGIL_OPCODE_FORLOOP_I64:
        case VIGIL_OPCODE_LESS_I32_JUMP_IF_FALSE:
        case VIGIL_OPCODE_LESS_EQUAL_I32_JUMP_IF_FALSE:
        case VIGIL_OPCODE_GREATER_I32_JUMP_IF_FALSE:
        case VIGIL_OPCODE_GREATER_EQUAL_I32_JUMP_IF_FALSE:
        case VIGIL_OPCODE_EQUAL_I32_JUMP_IF_FALSE:
        case VIGIL_OPCODE_NOT_EQUAL_I32_JUMP_IF_FALSE:
        case VIGIL_OPCODE_LESS_I64_JUMP_IF_FALSE:
        case VIGIL_OPCODE_LESS_EQUAL_I64_JUMP_IF_FALSE:
        case VIGIL_OPCODE_GREATER_I64_JUMP_IF_FALSE:
        case VIGIL_OPCODE_GREATER_EQUAL_I64_JUMP_IF_FALSE:
        case VIGIL_OPCODE_EQUAL_I64_JUMP_IF_FALSE:
        case VIGIL_OPCODE_NOT_EQUAL_I64_JUMP_IF_FALSE:
        case VIGIL_OPCODE_TO_I32:
        case VIGIL_OPCODE_TO_I64:
        case VIGIL_OPCODE_TO_F64:
        case VIGIL_OPCODE_MATH_SIN_F64:
        case VIGIL_OPCODE_MATH_COS_F64:
        case VIGIL_OPCODE_MATH_SQRT_F64:
        case VIGIL_OPCODE_MATH_LOG_F64:
        case VIGIL_OPCODE_MATH_POW_F64:
        case VIGIL_OPCODE_NEGATE:
        case VIGIL_OPCODE_NOT:
        case VIGIL_OPCODE_BITWISE_NOT:
        case VIGIL_OPCODE_BITWISE_AND:
        case VIGIL_OPCODE_BITWISE_OR:
        case VIGIL_OPCODE_BITWISE_XOR:
        case VIGIL_OPCODE_SHIFT_LEFT:
        case VIGIL_OPCODE_SHIFT_RIGHT:
        case VIGIL_OPCODE_EQUAL:
        case VIGIL_OPCODE_GREATER:
        case VIGIL_OPCODE_LESS:
        case VIGIL_OPCODE_DUP:
        case VIGIL_OPCODE_JUMP_IF_FALSE:
        case VIGIL_OPCODE_CALL_NATIVE:
        case VIGIL_OPCODE_TO_STRING:
        case VIGIL_OPCODE_CALL:
        case VIGIL_OPCODE_GET_GLOBAL:
        case VIGIL_OPCODE_SET_GLOBAL:
        case VIGIL_OPCODE_GET_CAPTURE:
        case VIGIL_OPCODE_SET_CAPTURE:
        case VIGIL_OPCODE_GET_FUNCTION:
        case VIGIL_OPCODE_NEW_CLOSURE:
            break;
        default:
            return 0; /* unsupported opcode */
        }
        ip += stack_op_size(code, ip, code_size);
    }

    /* Reject functions with multiple forward JUMPs to the same target
       (nested ternary where branches have different stack depths). */
    {
        size_t fwd_targets[64];
        size_t fwd_count = 0;
        size_t sip = 0;
        while (sip < code_size && fwd_count < 64)
        {
            if (code[sip] == VIGIL_OPCODE_JUMP && sip + 4 < code_size)
            {
                uint32_t off = (uint32_t)code[sip + 1] | ((uint32_t)code[sip + 2] << 8) |
                               ((uint32_t)code[sip + 3] << 16) | ((uint32_t)code[sip + 4] << 24);
                size_t target = sip + 5 + (size_t)off;
                for (size_t fi = 0; fi < fwd_count; fi++)
                    if (fwd_targets[fi] == target)
                        return 0;
                fwd_targets[fwd_count++] = target;
            }
            sip += stack_op_size(code, sip, code_size);
        }
    }

    return 1;
}

#define TR_EMIT(instr)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        if (vs.need_release != 255)                                                                                    \
        {                                                                                                              \
            vigil_status_t _release_status = emit(rc, vigil_reg_abc(VREG_RELEASE, vs.need_release, 0, 0), start_ip);   \
            vs_clear_obj(&vs, vs.need_release);                                                                        \
            vs.need_release = 255;                                                                                     \
            if (_release_status != VIGIL_STATUS_OK) goto tr_fail;                                                      \
        }                                                                                                              \
        vigil_status_t _emit_status = emit(rc, (instr), start_ip);                                                     \
        if (_emit_status != VIGIL_STATUS_OK)                                                                           \
            goto tr_fail;                                                                                              \
    } while (0)

/* Emit RELEASE for register dst if it may hold an object. */
#define TR_RELEASE(dst)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        if (vs_is_obj(&vs, (dst)))                                                                                     \
            TR_EMIT(vigil_reg_abc(VREG_RELEASE, (dst), 0, 0));                                                         \
    } while (0)

/* Mark register as holding an object (for future RELEASE). */
#define TR_MARK_OBJ(dst) vs_mark_obj(&vs, (dst))

/* Pack top n virtual stack values into consecutive registers ending at
   the highest. Emits MOV instructions for any gaps. */
#if defined(_MSC_VER)
#define SYNC_PACK(n) \
    do { \
        __pragma(warning(push)) \
        __pragma(warning(disable:4127)) \
        if ((n) > 1) { \
            uint8_t _hi = vs.regs[vs.top - 1]; \
            for (int _si = 2; _si <= (int)(n); _si++) { \
                uint8_t _exp = (uint8_t)(_hi - (_si - 1)); \
                uint8_t _act = vs.regs[vs.top - _si]; \
                if (_act != _exp) { \
                    TR_EMIT(vigil_reg_abc(VREG_MOVE, _exp, _act, 0)); \
                    vs.regs[vs.top - _si] = _exp; \
                    if (_exp >= vs.next_reg) vs.next_reg = _exp + 1; \
                } \
            } \
        } \
        __pragma(warning(pop)) \
    } while (0)
#else
#define SYNC_PACK(n) \
    do { \
        if ((n) > 1) { \
            uint8_t _hi = vs.regs[vs.top - 1]; \
            for (int _si = 2; _si <= (int)(n); _si++) { \
                uint8_t _exp = (uint8_t)(_hi - (_si - 1)); \
                uint8_t _act = vs.regs[vs.top - _si]; \
                if (_act != _exp) { \
                    TR_EMIT(vigil_reg_abc(VREG_MOVE, _exp, _act, 0)); \
                    vs.regs[vs.top - _si] = _exp; \
                    if (_exp >= vs.next_reg) vs.next_reg = _exp + 1; \
                } \
            } \
        } \
    } while (0)
#endif

/* Pack top n virtual stack values into consecutive registers starting at
   the current first register. This preserves the aggregate result base. */
#if defined(_MSC_VER)
#define PACK_TOP_FROM_FIRST(n) \
    do { \
        __pragma(warning(push)) \
        __pragma(warning(disable:4127)) \
        if ((n) > 1) { \
            uint8_t _base = vs.regs[vs.top - (int)(n)]; \
            for (uint32_t _pi = 1; _pi < (uint32_t)(n); _pi++) { \
                uint8_t _exp = (uint8_t)(_base + (uint8_t)_pi); \
                int _slot = vs.top - (int)(n) + (int)_pi; \
                uint8_t _act = vs.regs[_slot]; \
                if (_act != _exp) { \
                    TR_EMIT(vigil_reg_abc(VREG_MOVE, _exp, _act, 0)); \
                    vs.regs[_slot] = _exp; \
                    if (_exp >= vs.next_reg) vs.next_reg = _exp + 1; \
                } \
            } \
        } \
        __pragma(warning(pop)) \
    } while (0)
#else
#define PACK_TOP_FROM_FIRST(n) \
    do { \
        if ((n) > 1) { \
            uint8_t _base = vs.regs[vs.top - (int)(n)]; \
            for (uint32_t _pi = 1; _pi < (uint32_t)(n); _pi++) { \
                uint8_t _exp = (uint8_t)(_base + (uint8_t)_pi); \
                int _slot = vs.top - (int)(n) + (int)_pi; \
                uint8_t _act = vs.regs[_slot]; \
                if (_act != _exp) { \
                    TR_EMIT(vigil_reg_abc(VREG_MOVE, _exp, _act, 0)); \
                    vs.regs[_slot] = _exp; \
                    if (_exp >= vs.next_reg) vs.next_reg = _exp + 1; \
                } \
            } \
        } \
    } while (0)
#endif

/* Count locals by scanning for the highest GET_LOCAL/SET_LOCAL operand. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint8_t count_locals(const uint8_t *code, size_t code_size)
{
    uint32_t max_local = 0;
    size_t ip = 0;
    while (ip < code_size)
    {
        uint8_t op = code[ip];
        size_t sz = stack_op_size(code, ip, code_size);
        if ((op == VIGIL_OPCODE_GET_LOCAL || op == VIGIL_OPCODE_SET_LOCAL) && ip + 4 < code_size)
        {
            uint32_t idx = (uint32_t)code[ip + 1] | ((uint32_t)code[ip + 2] << 8) | ((uint32_t)code[ip + 3] << 16) |
                           ((uint32_t)code[ip + 4] << 24);
            if (idx + 1 > max_local)
                max_local = idx + 1;
        }
        /* Also check superinstructions that reference locals */
        if (op == VIGIL_OPCODE_INCREMENT_LOCAL_I32 || op == VIGIL_OPCODE_INCREMENT_LOCAL_I64 ||
            op == VIGIL_OPCODE_FORLOOP_I32 || op == VIGIL_OPCODE_FORLOOP_I64)
        {
            uint32_t idx = (uint32_t)code[ip + 1] | ((uint32_t)code[ip + 2] << 8) | ((uint32_t)code[ip + 3] << 16) |
                           ((uint32_t)code[ip + 4] << 24);
            if (idx + 1 > max_local)
                max_local = idx + 1;
        }
        /* LOCALS_* superinstructions reference two locals */
        if ((op >= VIGIL_OPCODE_LOCALS_ADD_I64 && op <= VIGIL_OPCODE_LOCALS_NOT_EQUAL_I64) ||
            (op >= VIGIL_OPCODE_LOCALS_ADD_F64 && op <= VIGIL_OPCODE_LOCALS_MULTIPLY_F64))
        {
            uint32_t a = (uint32_t)code[ip + 1] | ((uint32_t)code[ip + 2] << 8) | ((uint32_t)code[ip + 3] << 16) |
                         ((uint32_t)code[ip + 4] << 24);
            uint32_t b = (uint32_t)code[ip + 5] | ((uint32_t)code[ip + 6] << 8) | ((uint32_t)code[ip + 7] << 16) |
                         ((uint32_t)code[ip + 8] << 24);
            if (a + 1 > max_local)
                max_local = a + 1;
            if (b + 1 > max_local)
                max_local = b + 1;
        }
        /* Three-address LOCALS_*_STORE reference three locals */
        if ((op >= VIGIL_OPCODE_LOCALS_ADD_I32_STORE && op <= VIGIL_OPCODE_LOCALS_MODULO_I32_STORE) ||
            (op >= VIGIL_OPCODE_LOCALS_ADD_F64_STORE && op <= VIGIL_OPCODE_LOCALS_MULTIPLY_F64_STORE))
        {
            uint32_t dst = (uint32_t)code[ip + 1] | ((uint32_t)code[ip + 2] << 8) | ((uint32_t)code[ip + 3] << 16) |
                           ((uint32_t)code[ip + 4] << 24);
            uint32_t a = (uint32_t)code[ip + 5] | ((uint32_t)code[ip + 6] << 8) | ((uint32_t)code[ip + 7] << 16) |
                         ((uint32_t)code[ip + 8] << 24);
            uint32_t b = (uint32_t)code[ip + 9] | ((uint32_t)code[ip + 10] << 8) | ((uint32_t)code[ip + 11] << 16) |
                         ((uint32_t)code[ip + 12] << 24);
            if (dst + 1 > max_local)
                max_local = dst + 1;
            if (a + 1 > max_local)
                max_local = a + 1;
            if (b + 1 > max_local)
                max_local = b + 1;
        }
        /* ADD_F64_STORE etc reference one local */
        if (op >= VIGIL_OPCODE_ADD_F64_STORE && op <= VIGIL_OPCODE_MULTIPLY_F64_STORE)
        {
            uint32_t idx = (uint32_t)code[ip + 1] | ((uint32_t)code[ip + 2] << 8) | ((uint32_t)code[ip + 3] << 16) |
                           ((uint32_t)code[ip + 4] << 24);
            if (idx + 1 > max_local)
                max_local = idx + 1;
        }
        ip += sz;
    }
    return max_local > 250 ? 250 : (uint8_t)max_local;
}

/* Count how many locals are initialized at the start of the function
   (consecutive CONSTANT/NIL/TRUE/FALSE pushes before any control flow). */

/* ── Translation: main pass ────────────────────────────────────── */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_reg_translate(const vigil_chunk_t *stack_chunk, vigil_reg_chunk_t *rc, vigil_runtime_t *runtime,
                                   vigil_error_t *error)
{
    (void)runtime;

    const uint8_t *code = stack_chunk->code.data;
    size_t code_size = stack_chunk->code.length;
    vigil_status_t tr_status = VIGIL_STATUS_OK;

    uint8_t saved_arity = rc->arity;
    vigil_reg_chunk_init(rc);
    rc->arity = saved_arity;
    rc->stack_chunk = stack_chunk;

    uint8_t lc = count_locals(code, code_size);
    vstack_t vs;
    /* Use the arity from the function object (set by the caller). */
    uint8_t arity = rc->arity;
    uint8_t max_next_reg;
    vs_init(&vs, lc);
    /* Pre-initialize param slots: params are at R[0..arity-1]. */
    if (arity > 0)
    {
        vs.top = (int)arity;
        vs.next_reg = arity;
        for (uint8_t i = 0; i < arity; i++)
            vs.regs[i] = i;
    }
    max_next_reg = vs.next_reg;

    /* Collect jump targets for stack-state reset at join points. */
    size_t jt_count = 0;
    size_t *jt = collect_jump_targets(code, code_size, &jt_count);

    offset_map_t omap;
    omap_init(&omap);
    jump_patch_list_t patches;
    jpatch_init(&patches);

    /* Map from stack bytecode offset → expected virtual stack depth.
       Populated when jumps are emitted; consulted at each instruction
       to restore the correct depth at jump targets. -1 = no entry. */
    int *depth_at = calloc(code_size + 1, sizeof(int));
    size_t *origin_at = calloc(code_size + 1, sizeof(size_t));
    vstack_t *state_at = calloc(code_size + 1, sizeof(vstack_t));
    if (!depth_at || !origin_at || !state_at)
    {
        free(depth_at);
        free(origin_at);
        free(state_at);
        free(jt);
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    for (size_t di = 0; di <= code_size; di++)
    {
        depth_at[di] = -1;
        origin_at[di] = (size_t)-1;
    }

#define RECORD_DEPTH(off, d)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((off) <= code_size && depth_at[(off)] == -1)                                                               \
        {                                                                                                              \
            depth_at[(off)] = (d);                                                                                     \
            origin_at[(off)] = start_ip;                                                                               \
            state_at[(off)] = vs;                                                                                      \
        }                                                                                                              \
    } while (0)

    size_t ip = 0;
    int reachable = 1;
#define NORMALIZE_TO_TARGET(target_off)                                                                               \
    do                                                                                                                \
    {                                                                                                                 \
        if (depth_at[(target_off)] >= 0)                                                                              \
        {                                                                                                             \
            vstack_t target_state = state_at[(target_off)];                                                           \
            vigil_status_t normalize_status =                                                                         \
                regvm_normalize_stack(rc, start_ip, &vs, &target_state, &max_next_reg, error);                       \
                                                                                                                      \
            if (normalize_status != VIGIL_STATUS_OK)                                                                  \
            {                                                                                                         \
                tr_status = normalize_status;                                                                         \
                goto tr_status_fail;                                                                                  \
            }                                                                                                         \
            vs = target_state;                                                                                        \
        }                                                                                                             \
    } while (0)
    while (ip < code_size)
    {
        size_t start_ip = ip;
        uint8_t op = code[ip];
        /* Restore stack depth at jump targets. */
        if (depth_at[start_ip] >= 0)
        {
            if (reachable)
            {
                NORMALIZE_TO_TARGET(start_ip);
            }
            vs = state_at[start_ip];
            reachable = 1;
        }
        else if (!reachable)
        {
            ip += stack_op_size(code, ip, code_size);
            continue;
        }

        /* Record offset mapping (after any normalization MOV). */
        omap_add(&omap, start_ip, rc->code_count);

        switch (op)
        {
        /* ── Data movement ─────────────────────────────────────── */
        case VIGIL_OPCODE_CONSTANT: {
            uint32_t ci = rd_u32(code, &ip);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abx(VREG_LOAD_K, r, (uint16_t)ci));
            break;
        }
        case VIGIL_OPCODE_NIL: {
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_LOAD_NIL, r, 0, 0));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_TRUE: {
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_LOAD_TRUE, r, 0, 0));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_FALSE: {
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_LOAD_FALSE, r, 0, 0));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_GET_LOCAL: {
            uint32_t idx = rd_u32(code, &ip);
            uint8_t dst = vs_push(&vs);
            if (dst != (uint8_t)idx)
                TR_EMIT(vigil_reg_abc(VREG_MOVE, dst, (uint8_t)idx, 0));
            break;
        }
        case VIGIL_OPCODE_SET_LOCAL: {
            uint32_t idx = rd_u32(code, &ip);
            uint8_t src = vs_peek(&vs, 0);
            if (src != (uint8_t)idx)
                TR_EMIT(vigil_reg_abc(VREG_MOVE, (uint8_t)idx, src, 0));
            /* SET_LOCAL doesn't pop — the value stays on the virtual stack.
               The subsequent POP will handle cleanup. */
            break;
        }
        case VIGIL_OPCODE_POP: {
            uint8_t popped = vs_pop(&vs);
            (void)popped;
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_DUP: {
            uint8_t src = vs_peek(&vs, 0);
            uint8_t dst = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_DUP, dst, src, 0));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_DUP_TWO: {
            uint8_t s1 = vs_peek(&vs, 1);
            uint8_t s0 = vs_peek(&vs, 0);
            uint8_t d1 = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_DUP, d1, s1, 0));
            uint8_t d0 = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_DUP, d0, s0, 0));
            ip += 1;
            break;
        }

        /* ── Binary arithmetic ─────────────────────────────────── */
        case VIGIL_OPCODE_ADD:
        case VIGIL_OPCODE_SUBTRACT:
        case VIGIL_OPCODE_MULTIPLY:
        case VIGIL_OPCODE_DIVIDE:
        case VIGIL_OPCODE_MODULO:
        case VIGIL_OPCODE_BITWISE_AND:
        case VIGIL_OPCODE_BITWISE_OR:
        case VIGIL_OPCODE_BITWISE_XOR:
        case VIGIL_OPCODE_SHIFT_LEFT:
        case VIGIL_OPCODE_SHIFT_RIGHT:
        case VIGIL_OPCODE_EQUAL:
        case VIGIL_OPCODE_LESS:
        case VIGIL_OPCODE_ADD_I32:
        case VIGIL_OPCODE_SUBTRACT_I32:
        case VIGIL_OPCODE_MULTIPLY_I32:
        case VIGIL_OPCODE_DIVIDE_I32:
        case VIGIL_OPCODE_MODULO_I32:
        case VIGIL_OPCODE_LESS_I32:
        case VIGIL_OPCODE_LESS_EQUAL_I32:
        case VIGIL_OPCODE_GREATER_I32:
        case VIGIL_OPCODE_GREATER_EQUAL_I32:
        case VIGIL_OPCODE_EQUAL_I32:
        case VIGIL_OPCODE_NOT_EQUAL_I32:
        case VIGIL_OPCODE_ADD_I64:
        case VIGIL_OPCODE_SUBTRACT_I64:
        case VIGIL_OPCODE_MULTIPLY_I64:
        case VIGIL_OPCODE_DIVIDE_I64:
        case VIGIL_OPCODE_MODULO_I64:
        case VIGIL_OPCODE_LESS_I64:
        case VIGIL_OPCODE_LESS_EQUAL_I64:
        case VIGIL_OPCODE_GREATER_I64:
        case VIGIL_OPCODE_GREATER_EQUAL_I64:
        case VIGIL_OPCODE_EQUAL_I64:
        case VIGIL_OPCODE_NOT_EQUAL_I64:
        case VIGIL_OPCODE_ADD_F64:
        case VIGIL_OPCODE_SUBTRACT_F64:
        case VIGIL_OPCODE_MULTIPLY_F64:
        case VIGIL_OPCODE_DIVIDE_F64: {
            uint8_t rb = vs_pop(&vs);
            uint8_t ra = vs_pop(&vs);
            uint8_t rd = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(map_binop(op), rd, ra, rb));
            ip += 1;
            break;
        }

        /* GREATER: swap operands so a > b becomes b < a */
        case VIGIL_OPCODE_GREATER: {
            uint8_t rb = vs_pop(&vs);
            uint8_t ra = vs_pop(&vs);
            uint8_t rd = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_LT, rd, rb, ra));
            ip += 1;
            break;
        }

        /* ── Unary ops ─────────────────────────────────────────── */
        case VIGIL_OPCODE_NEGATE:
        case VIGIL_OPCODE_NOT:
        case VIGIL_OPCODE_BITWISE_NOT: {
            uint8_t src = vs_pop(&vs);
            uint8_t dst = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(map_unary(op), dst, src, 0));
            ip += 1;
            break;
        }

        /* ── Type conversions ──────────────────────────────────── */
        case VIGIL_OPCODE_TO_I32:
        case VIGIL_OPCODE_TO_I64:
        case VIGIL_OPCODE_TO_U8:
        case VIGIL_OPCODE_TO_U32:
        case VIGIL_OPCODE_TO_U64:
        case VIGIL_OPCODE_TO_F64:
        case VIGIL_OPCODE_TO_STRING: {
            uint8_t src = vs_pop(&vs);
            uint8_t dst = vs_push_result(&vs, src);
            TR_EMIT(vigil_reg_abc(map_conv(op), dst, src, 0));
            ip += 1;
            break;
        }

        /* ── Math intrinsics (unary) ───────────────────────────── */
        case VIGIL_OPCODE_MATH_SIN_F64:
        case VIGIL_OPCODE_MATH_COS_F64:
        case VIGIL_OPCODE_MATH_SQRT_F64:
        case VIGIL_OPCODE_MATH_LOG_F64: {
            uint8_t src = vs_pop(&vs);
            uint8_t dst = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(map_math(op), dst, src, 0));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_MATH_POW_F64: {
            uint8_t rb = vs_pop(&vs);
            uint8_t ra = vs_pop(&vs);
            uint8_t rd = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_MATH_POW, rd, ra, rb));
            ip += 1;
            break;
        }

        /* ── Superinstructions: LOCALS_*_I64 (two-address) ─────── */
        case VIGIL_OPCODE_LOCALS_ADD_I64:
        case VIGIL_OPCODE_LOCALS_SUBTRACT_I64:
        case VIGIL_OPCODE_LOCALS_MULTIPLY_I64:
        case VIGIL_OPCODE_LOCALS_MODULO_I64:
        case VIGIL_OPCODE_LOCALS_LESS_I64:
        case VIGIL_OPCODE_LOCALS_LESS_EQUAL_I64:
        case VIGIL_OPCODE_LOCALS_GREATER_I64:
        case VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I64:
        case VIGIL_OPCODE_LOCALS_EQUAL_I64:
        case VIGIL_OPCODE_LOCALS_NOT_EQUAL_I64: {
            uint32_t a = rd_u32(code, &ip);
            uint32_t b = rd_raw_u32(code, &ip);
            uint8_t rd = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(map_locals_i64(op), rd, (uint8_t)a, (uint8_t)b));
            break;
        }

        /* ── Superinstructions: LOCALS_*_F64 (two-address) ─────── */
        case VIGIL_OPCODE_LOCALS_ADD_F64:
        case VIGIL_OPCODE_LOCALS_SUBTRACT_F64:
        case VIGIL_OPCODE_LOCALS_MULTIPLY_F64: {
            uint32_t a = rd_u32(code, &ip);
            uint32_t b = rd_raw_u32(code, &ip);
            uint8_t rd = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(map_locals_f64(op), rd, (uint8_t)a, (uint8_t)b));
            break;
        }

        /* ── Three-address: LOCALS_*_I32_STORE ─────────────────── */
        case VIGIL_OPCODE_LOCALS_ADD_I32_STORE:
        case VIGIL_OPCODE_LOCALS_SUBTRACT_I32_STORE:
        case VIGIL_OPCODE_LOCALS_MULTIPLY_I32_STORE:
        case VIGIL_OPCODE_LOCALS_LESS_I32_STORE:
        case VIGIL_OPCODE_LOCALS_LESS_EQUAL_I32_STORE:
        case VIGIL_OPCODE_LOCALS_GREATER_I32_STORE:
        case VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I32_STORE:
        case VIGIL_OPCODE_LOCALS_EQUAL_I32_STORE:
        case VIGIL_OPCODE_LOCALS_NOT_EQUAL_I32_STORE:
        case VIGIL_OPCODE_LOCALS_MODULO_I32_STORE: {
            uint32_t dst = rd_u32(code, &ip);
            uint32_t a = rd_raw_u32(code, &ip);
            uint32_t b = rd_raw_u32(code, &ip);
            TR_EMIT(vigil_reg_abc(map_locals_i32_store(op), (uint8_t)dst, (uint8_t)a, (uint8_t)b));
            break;
        }

        /* ── Three-address: LOCALS_*_F64_STORE ─────────────────── */
        case VIGIL_OPCODE_LOCALS_ADD_F64_STORE:
        case VIGIL_OPCODE_LOCALS_SUBTRACT_F64_STORE:
        case VIGIL_OPCODE_LOCALS_MULTIPLY_F64_STORE: {
            uint32_t dst = rd_u32(code, &ip);
            uint32_t a = rd_raw_u32(code, &ip);
            uint32_t b = rd_raw_u32(code, &ip);
            TR_EMIT(vigil_reg_abc(map_locals_f64_store(op), (uint8_t)dst, (uint8_t)a, (uint8_t)b));
            break;
        }

        /* ── Fused arith+store: ADD_F64_STORE etc ──────────────── */
        case VIGIL_OPCODE_ADD_F64_STORE:
        case VIGIL_OPCODE_SUBTRACT_F64_STORE:
        case VIGIL_OPCODE_MULTIPLY_F64_STORE: {
            uint32_t dst_local = rd_u32(code, &ip);
            uint8_t rb = vs_pop(&vs);
            uint8_t ra = vs_pop(&vs);
            TR_EMIT(vigil_reg_abc(map_f64_store(op), (uint8_t)dst_local, ra, rb));
            break;
        }

        /* ── Increment local ───────────────────────────────────── */
        case VIGIL_OPCODE_INCREMENT_LOCAL_I32: {
            uint32_t idx = rd_u32(code, &ip);
            int8_t delta = (int8_t)code[ip];
            ip += 1;
            TR_EMIT(vigil_reg_abc(VREG_INC_I32, (uint8_t)idx, (uint8_t)delta, 0));
            break;
        }
        case VIGIL_OPCODE_INCREMENT_LOCAL_I64: {
            uint32_t idx = rd_u32(code, &ip);
            int8_t delta = (int8_t)code[ip];
            ip += 1;
            TR_EMIT(vigil_reg_abc(VREG_INC_I64, (uint8_t)idx, (uint8_t)delta, 0));
            break;
        }

        /* ── Control flow ──────────────────────────────────────── */
        case VIGIL_OPCODE_JUMP: {
            uint32_t off = rd_u32(code, &ip);
            size_t target = ip + (size_t)off;
            NORMALIZE_TO_TARGET(target);
            /* Emit placeholder — will be patched. */
            jpatch_add(&patches, rc->code_count, target, 0, vs.top);
            RECORD_DEPTH(target, vs.top);
            TR_EMIT(vigil_reg_asbx(VREG_JMP, 0, 0));
            reachable = depth_at[ip] < 0 && jump_target_contains(jt, jt_count, ip);
            break;
        }
        case VIGIL_OPCODE_LOOP: {
            uint32_t off = rd_u32(code, &ip);
            size_t target = ip - (size_t)off;
            NORMALIZE_TO_TARGET(target);
            jpatch_add(&patches, rc->code_count, target, 1, vs.top);
            RECORD_DEPTH(target, vs.top);
            TR_EMIT(vigil_reg_asbx(VREG_JMP, 0, 0));
            reachable = 0;
            break;
        }
        case VIGIL_OPCODE_JUMP_IF_FALSE: {
            uint32_t off = rd_u32(code, &ip);
            size_t target = ip + (size_t)off;
            NORMALIZE_TO_TARGET(target);
            TR_EMIT(vigil_reg_abc(VREG_TEST, vs_peek(&vs, 0), 0, 0));
            jpatch_add(&patches, rc->code_count, target, 0, vs.top);
            RECORD_DEPTH(target, vs.top);
            TR_EMIT(vigil_reg_asbx(VREG_JMP, 0, 0));
            break;
        }

        /* ── Fused compare+jump ────────────────────────────────── */
        case VIGIL_OPCODE_LESS_I32_JUMP_IF_FALSE:
        case VIGIL_OPCODE_LESS_EQUAL_I32_JUMP_IF_FALSE:
        case VIGIL_OPCODE_GREATER_I32_JUMP_IF_FALSE:
        case VIGIL_OPCODE_GREATER_EQUAL_I32_JUMP_IF_FALSE:
        case VIGIL_OPCODE_EQUAL_I32_JUMP_IF_FALSE:
        case VIGIL_OPCODE_NOT_EQUAL_I32_JUMP_IF_FALSE:
        case VIGIL_OPCODE_LESS_I64_JUMP_IF_FALSE:
        case VIGIL_OPCODE_LESS_EQUAL_I64_JUMP_IF_FALSE:
        case VIGIL_OPCODE_GREATER_I64_JUMP_IF_FALSE:
        case VIGIL_OPCODE_GREATER_EQUAL_I64_JUMP_IF_FALSE:
        case VIGIL_OPCODE_EQUAL_I64_JUMP_IF_FALSE:
        case VIGIL_OPCODE_NOT_EQUAL_I64_JUMP_IF_FALSE: {
            uint32_t off = rd_u32(code, &ip);
            uint8_t rb = vs_pop(&vs);
            uint8_t ra = vs_pop(&vs);
            size_t target = ip + (size_t)off;
            uint8_t res = vs_push(&vs);
            uint8_t cmp_op = VREG_EQ;
            int reverse_operands = 0;

            switch (op)
            {
            case VIGIL_OPCODE_LESS_I32_JUMP_IF_FALSE:
            case VIGIL_OPCODE_LESS_I64_JUMP_IF_FALSE:
                cmp_op = VREG_LT;
                break;
            case VIGIL_OPCODE_LESS_EQUAL_I32_JUMP_IF_FALSE:
            case VIGIL_OPCODE_LESS_EQUAL_I64_JUMP_IF_FALSE:
                cmp_op = VREG_LE;
                break;
            case VIGIL_OPCODE_GREATER_I32_JUMP_IF_FALSE:
            case VIGIL_OPCODE_GREATER_I64_JUMP_IF_FALSE:
                cmp_op = VREG_LT;
                reverse_operands = 1;
                break;
            case VIGIL_OPCODE_GREATER_EQUAL_I32_JUMP_IF_FALSE:
            case VIGIL_OPCODE_GREATER_EQUAL_I64_JUMP_IF_FALSE:
                cmp_op = VREG_LE;
                reverse_operands = 1;
                break;
            default:
                cmp_op = VREG_EQ;
                break;
            }

            if (reverse_operands)
                regvm_swap_u8(&ra, &rb);

            TR_EMIT(vigil_reg_abc(cmp_op, res, ra, rb));
            if (op == VIGIL_OPCODE_NOT_EQUAL_I32_JUMP_IF_FALSE || op == VIGIL_OPCODE_NOT_EQUAL_I64_JUMP_IF_FALSE)
                TR_EMIT(vigil_reg_abc(VREG_NOT, res, res, 0));

            NORMALIZE_TO_TARGET(target);

            TR_EMIT(vigil_reg_abc(VREG_TEST, vs_peek(&vs, 0), 0, 0));
            jpatch_add(&patches, rc->code_count, target, 0, vs.top);
            RECORD_DEPTH(target, vs.top);
            TR_EMIT(vigil_reg_asbx(VREG_JMP, 0, 0));
            break;
        }

        /* ── FORLOOP ───────────────────────────────────────────── */
        case VIGIL_OPCODE_FORLOOP_I32:
        case VIGIL_OPCODE_FORLOOP_I64: {
            uint32_t idx = rd_u32(code, &ip);
            int8_t delta = (int8_t)code[ip];
            ip += 1;
            uint32_t ci = rd_raw_u32(code, &ip);
            uint8_t cmp = code[ip];
            ip += 1;
            uint32_t back = rd_raw_u32(code, &ip);
            size_t target = ip - (size_t)back;
            uint8_t rop = (op == VIGIL_OPCODE_FORLOOP_I32) ? VREG_FORLOOP_I32 : VREG_FORLOOP_I64;
            NORMALIZE_TO_TARGET(target);
            /* Encode: A=local, B=delta, C=cmp. Next word: constant index.
               Third word: jump offset (patched). */
            TR_EMIT(vigil_reg_abc(rop, (uint8_t)idx, (uint8_t)delta, cmp));
            TR_EMIT(vigil_reg_abx(VREG_LOAD_K, 0, (uint16_t)ci)); /* constant index */
            jpatch_add(&patches, rc->code_count, target, 1, vs.top);
            RECORD_DEPTH(target, vs.top);
            TR_EMIT(vigil_reg_asbx(VREG_JMP, 0, 0)); /* back jump */
            /* FORLOOP pushes FALSE on exit. */
            uint8_t fr = vs_push(&vs);
            (void)fr; /* The FALSE is consumed by the next JUMP_IF_FALSE */
            break;
        }


#define PACK_CALL_ARGS(n) \
    do { \
        if ((n) > 0) { \
            uint8_t _base = vs.next_reg; \
            for (uint32_t _ai = 0; _ai < (uint32_t)(n); _ai++) { \
                uint8_t _exp = (uint8_t)(_base + (uint8_t)_ai); \
                uint8_t _act = vs.regs[vs.top - (int)(n) + (int)_ai]; \
                if (_act != _exp) \
                    TR_EMIT(vigil_reg_abc(VREG_MOVE, _exp, _act, 0)); \
                vs.regs[vs.top - (int)(n) + (int)_ai] = _exp; \
            } \
            vs.next_reg = (uint8_t)(_base + (uint8_t)(n)); \
        } \
    } while (0)

#define CALL_RET_PUSH(base_reg, count) \
    do { \
        for (uint32_t _i = 0; _i < (count); _i++) { \
            uint8_t _br = (uint8_t)((base_reg) + _i); \
            if (vs.top < (int)vs.local_count) { \
                /* Push-to-position local: force identity register. */ \
                uint8_t _ret_reg = vs_push_at(&vs, (uint8_t)vs.top); \
                if (_ret_reg != _br) \
                    TR_EMIT(vigil_reg_abc(VREG_MOVE, _ret_reg, _br, 0)); \
            } else { \
                vs_push_at(&vs, _br); \
            } \
        } \
    } while (0)

#define PUSH_RESULT_REGS(preferred, count, out_base) \
    do { \
        uint8_t _result_base = vs_result_base(&vs, (preferred)); \
        for (uint32_t _ri = 0; _ri < (uint32_t)(count); _ri++) \
            vs_push_at(&vs, (uint8_t)(_result_base + (uint8_t)_ri)); \
        (out_base) = _result_base; \
    } while (0)

        /* ── Calls ─────────────────────────────────────────────── */
        case VIGIL_OPCODE_CALL: {
            uint32_t func_idx = rd_u32(code, &ip);
            uint32_t arg_count = rd_raw_u32(code, &ip);
            uint32_t ret_count = rd_raw_u32(code, &ip);
            PACK_CALL_ARGS(arg_count);
            uint8_t arg_base_r = (arg_count > 0) ? vs.regs[vs.top - (int)arg_count] : (uint8_t)vs.top;
            TR_EMIT(vigil_reg_abc(VREG_CALL, arg_base_r, (uint8_t)func_idx, (uint8_t)arg_count));
            for (uint32_t i = 0; i < arg_count; i++)
                vs_pop(&vs);
            CALL_RET_PUSH(arg_base_r, ret_count);
            break;
        }
        case VIGIL_OPCODE_CALL_SELF: {
            uint32_t arg_count = rd_u32(code, &ip);
            uint32_t ret_count = rd_raw_u32(code, &ip);
            PACK_CALL_ARGS(arg_count);
            uint8_t base_r = (uint8_t)vs.top;
            if (arg_count > 0)
            {
                base_r = vs.regs[vs.top - (int)arg_count];
                for (uint32_t i = 0; i < arg_count; i++)
                    vs_pop(&vs);
            }
            for (uint32_t i = 0; i < ret_count; i++)
            {
                /* CALL_SELF returns must use fresh registers to avoid
                   clobbering locals when top drops below lc. */
                uint8_t fr = vs.next_reg;
                vs_push_at(&vs, fr);
            }
            uint8_t ret = vs.regs[vs.top - 1];
            TR_EMIT(vigil_reg_abc(VREG_CALL_SELF, ret, (uint8_t)arg_count, (uint8_t)ret_count));
            TR_EMIT((uint32_t)base_r);
            break;
        }
        case VIGIL_OPCODE_TAIL_CALL: {
            uint32_t func_idx = rd_u32(code, &ip);
            uint32_t arg_count = rd_raw_u32(code, &ip);
            PACK_CALL_ARGS(arg_count);
            uint8_t tc_base = (arg_count > 0) ? vs.regs[vs.top - (int)arg_count] : (uint8_t)vs.top;
            for (uint32_t i = 0; i < arg_count; i++)
                vs_pop(&vs);
            {
                uint8_t ret = vs_push_at(&vs, tc_base);

                TR_EMIT(vigil_reg_abc(VREG_TAIL_CALL, ret, (uint8_t)func_idx, (uint8_t)arg_count));
            }
            /* TAIL_CALL does not fall through. Any following jump/cleanup bytecode is dead. */
            reachable = 0;
            break;
        }
        case VIGIL_OPCODE_CALL_NATIVE: {
            uint32_t ci = rd_u32(code, &ip);
            uint32_t arg_count = rd_raw_u32(code, &ip);
            uint32_t ret_count = rd_raw_u32(code, &ip);
            PACK_CALL_ARGS(arg_count);
            uint8_t arg_base_r = (arg_count > 0) ? vs.regs[vs.top - (int)arg_count] : (uint8_t)vs.top;
            TR_EMIT(vigil_reg_abc(VREG_CALL_NATIVE, arg_base_r, 0, (uint8_t)arg_count));
            TR_EMIT(ci);
            for (uint32_t i = 0; i < arg_count; i++)
                vs_pop(&vs);
            CALL_RET_PUSH(arg_base_r, ret_count);
            break;
        }
        case VIGIL_OPCODE_CALL_VALUE: {
            uint32_t arg_count = rd_u32(code, &ip);
            uint32_t ret_count = rd_raw_u32(code, &ip);
            PACK_CALL_ARGS(arg_count + 1);
            uint8_t callable_r = vs.regs[vs.top - (int)arg_count - 1];
            int result_top = vs.top - (int)arg_count - 1;
            uint8_t ret = callable_r;
            for (uint32_t i = 0; i < arg_count + 1; i++)
                vs_pop(&vs);
            if (ret_count > 0)
            {
                CALL_RET_PUSH(callable_r, ret_count);
                ret = vs.regs[result_top];
            }
            TR_EMIT(vigil_reg_abx(VREG_CALL_VALUE, ret, (uint16_t)arg_count));
            TR_EMIT((uint32_t)callable_r);
            break;
        }
        case VIGIL_OPCODE_CALL_INTERFACE: {
            uint32_t iface_idx = rd_u32(code, &ip);
            uint32_t method_idx = rd_raw_u32(code, &ip);
            uint32_t arg_count = rd_raw_u32(code, &ip);
            uint32_t ret_count = rd_raw_u32(code, &ip);
            PACK_CALL_ARGS(arg_count + 1);
            uint8_t receiver_r = vs.regs[vs.top - (int)arg_count - 1];
            int result_top = vs.top - (int)arg_count - 1;
            uint8_t ret = receiver_r;
            for (uint32_t i = 0; i < arg_count + 1; i++)
                vs_pop(&vs);
            if (ret_count > 0)
            {
                CALL_RET_PUSH(receiver_r, ret_count);
                ret = vs.regs[result_top];
            }
            TR_EMIT(vigil_reg_abc(VREG_CALL_INTERFACE, ret, (uint8_t)iface_idx, (uint8_t)arg_count));
            TR_EMIT((uint32_t)method_idx);
            TR_EMIT((uint32_t)receiver_r);
            break;
        }
        case VIGIL_OPCODE_CALL_EXTERN: {
            uint32_t ci = rd_u32(code, &ip);
            uint32_t arg_count = rd_raw_u32(code, &ip);
            uint32_t ret_count = rd_raw_u32(code, &ip);
            PACK_CALL_ARGS(arg_count);
            uint8_t ext_base = (arg_count > 0) ? vs.regs[vs.top - (int)arg_count] : (uint8_t)vs.top;
            int result_top = vs.top - (int)arg_count;
            uint8_t ret = ext_base;
            for (uint32_t i = 0; i < arg_count; i++)
                vs_pop(&vs);
            if (ret_count > 0)
            {
                CALL_RET_PUSH(ext_base, ret_count);
                ret = vs.regs[result_top];
            }
            TR_EMIT(vigil_reg_abc(VREG_CALL_EXTERN, ret, (uint8_t)ci, (uint8_t)arg_count));
            TR_EMIT((uint32_t)ext_base);
            break;
        }

        /* ── Return ────────────────────────────────────────────── */
        case VIGIL_OPCODE_RETURN: {
            uint32_t ret_count;
            if (ip + 5 <= code_size)
            {
                ret_count = rd_u32(code, &ip);
            }
            else
            {
                ret_count = 1;
                ip += 1;
            }
            if (ret_count > (uint32_t)vs.top)
                ret_count = (uint32_t)(vs.top > 0 ? vs.top : 0);
            PACK_TOP_FROM_FIRST(ret_count);
            uint8_t base_r = 0;
            if (ret_count > 0 && vs.top > 0)
                base_r = vs.regs[vs.top - (int)ret_count];
            for (uint32_t i = 0; i < ret_count && vs.top > 0; i++)
                vs_pop(&vs);
            /* Move return values to R[0..count-1] so callers always find
               them at arg_base. Emit MOVs in the translator so the runtime
               RETURN handler just reads from R[0]. */
            if (base_r != 0 && ret_count > 0)
            {
                for (uint32_t ri = 0; ri < ret_count; ri++)
                    TR_EMIT(vigil_reg_abc(VREG_MOVE, (uint8_t)ri, (uint8_t)(base_r + ri), 0));
                base_r = 0;
            }
            TR_EMIT(vigil_reg_abc(VREG_RETURN, base_r, (uint8_t)ret_count, 0));
            /* Don't reset vs.top — the jump target restoration at the
               next reachable instruction will set the correct depth. */
            reachable = 0;
            break;
        }

        /* ── Globals and captures ──────────────────────────────── */
        case VIGIL_OPCODE_GET_GLOBAL: {
            uint32_t idx = rd_u32(code, &ip);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abx(VREG_GET_GLOBAL, r, (uint16_t)idx));
            break;
        }
        case VIGIL_OPCODE_SET_GLOBAL: {
            uint32_t idx = rd_u32(code, &ip);
            uint8_t src = vs_peek(&vs, 0);
            TR_EMIT(vigil_reg_abx(VREG_SET_GLOBAL, src, (uint16_t)idx));
            break;
        }
        case VIGIL_OPCODE_GET_CAPTURE: {
            uint32_t idx = rd_u32(code, &ip);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abx(VREG_GET_CAPTURE, r, (uint16_t)idx));
            break;
        }
        case VIGIL_OPCODE_SET_CAPTURE: {
            uint32_t idx = rd_u32(code, &ip);
            uint8_t src = vs_peek(&vs, 0);
            TR_EMIT(vigil_reg_abx(VREG_SET_CAPTURE, src, (uint16_t)idx));
            break;
        }
        case VIGIL_OPCODE_GET_FUNCTION: {
            uint32_t idx = rd_u32(code, &ip);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abx(VREG_GET_FUNCTION, r, (uint16_t)idx));
            break;
        }
        case VIGIL_OPCODE_NEW_CLOSURE: {
            uint32_t func_idx = rd_u32(code, &ip);
            uint32_t cap_count = rd_raw_u32(code, &ip);
            uint8_t a;
            if (cap_count > 0)
            {
                SYNC_PACK(cap_count);
                a = vs.regs[vs.top - (int)cap_count];
                for (uint32_t i = 0; i < cap_count; i++)
                    vs_pop(&vs);
                vs_push_at(&vs, a);
            }
            else
            {
                a = vs_push(&vs);
            }
            TR_EMIT(vigil_reg_abc(VREG_NEW_CLOSURE, a, (uint8_t)func_idx, (uint8_t)cap_count));
            break;
        }

        /* ── Objects ───────────────────────────────────────────── */
        case VIGIL_OPCODE_NEW_INSTANCE: {
            uint32_t ci = rd_u32(code, &ip);
            uint32_t field_count = rd_raw_u32(code, &ip);
            PACK_TOP_FROM_FIRST(field_count);
            uint8_t fields_base = (field_count > 0) ? vs.regs[vs.top - (int)field_count] : 0;
            for (uint32_t i = 0; i < field_count; i++)
                vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abx(VREG_NEW_INSTANCE, r, (uint16_t)ci));
            TR_EMIT(vigil_reg_abc(0, fields_base, (uint8_t)field_count, 0));
            break;
        }
        case VIGIL_OPCODE_GET_FIELD: {
            uint32_t fi = rd_u32(code, &ip);
            uint8_t obj = vs_pop(&vs);
            uint8_t r = vs_push_result(&vs, obj);
            TR_EMIT(vigil_reg_abc(VREG_GET_FIELD, r, obj, (uint8_t)fi));
            break;
        }
        case VIGIL_OPCODE_SET_FIELD: {
            uint32_t fi = rd_u32(code, &ip);
            SYNC_PACK(2); uint8_t val =
            vs_pop(&vs);
            uint8_t obj = vs_pop(&vs);
            TR_EMIT(vigil_reg_abc(VREG_SET_FIELD, obj, (uint8_t)fi, val));
            break;
        }
        case VIGIL_OPCODE_NEW_ARRAY: {
            uint32_t type_idx = rd_u32(code, &ip);
            uint32_t count = rd_raw_u32(code, &ip);
            (void)type_idx;
            PACK_TOP_FROM_FIRST(count);
            uint8_t first = (count > 0) ? vs.regs[vs.top - (int)count] : 0;
            for (uint32_t i = 0; i < count; i++)
                vs_pop(&vs);
            uint8_t r = (count > 0) ? vs_push_at(&vs, first) : vs_push(&vs);
            TR_EMIT(vigil_reg_abx(VREG_NEW_ARRAY, r, (uint16_t)count));
            break;
        }
        case VIGIL_OPCODE_NEW_MAP: {
            uint32_t type_idx = rd_u32(code, &ip);
            uint32_t count = rd_raw_u32(code, &ip);
            (void)type_idx;
            PACK_TOP_FROM_FIRST(count * 2U);
            uint8_t first = (count > 0) ? vs.regs[vs.top - (int)(count * 2)] : 0;
            for (uint32_t i = 0; i < count * 2; i++)
                vs_pop(&vs);
            uint8_t r = (count > 0) ? vs_push_at(&vs, first) : vs_push(&vs);
            TR_EMIT(vigil_reg_abx(VREG_NEW_MAP, r, (uint16_t)count));
            break;
        }
        case VIGIL_OPCODE_GET_INDEX: {
            SYNC_PACK(2); uint8_t idx =
            vs_pop(&vs);
            uint8_t obj = vs_pop(&vs);
            uint8_t r = vs_push_result(&vs, obj);
            TR_EMIT(vigil_reg_abc(VREG_GET_INDEX, r, obj, idx));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_SET_INDEX: {
            SYNC_PACK(3); uint8_t val =
            vs_pop(&vs);
            uint8_t idx = vs_pop(&vs);
            uint8_t obj = vs_pop(&vs);
            TR_EMIT(vigil_reg_abc(VREG_SET_INDEX, obj, idx, val));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_GET_COLLECTION_SIZE: {
            uint8_t obj = vs_pop(&vs);
            uint8_t r = vs_push_result(&vs, obj);
            TR_EMIT(vigil_reg_abc(VREG_COLLECTION_SIZE, r, obj, 0));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_GET_STRING_SIZE: {
            uint8_t obj = vs_pop(&vs);
            uint8_t r = vs_push_result(&vs, obj);
            TR_EMIT(vigil_reg_abc(VREG_COLLECTION_SIZE, r, obj, 1));
            ip += 1;
            break;
        }

        /* ── Error handling ────────────────────────────────────── */
        case VIGIL_OPCODE_NEW_ERROR: {
            SYNC_PACK(2); uint8_t msg =
            vs_pop(&vs);
            uint8_t kind = vs_pop(&vs);
            uint8_t r = vs_push_at(&vs, kind);
            TR_EMIT(vigil_reg_abc(VREG_NEW_ERROR, r, kind, msg));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_GET_ERROR_KIND: {
            uint8_t err = vs_pop(&vs);
            uint8_t r = vs_push_at(&vs, err);
            TR_EMIT(vigil_reg_abc(VREG_GET_ERROR_KIND, r, err, 0));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_GET_ERROR_MESSAGE: {
            uint8_t err = vs_pop(&vs);
            uint8_t r = vs_push_at(&vs, err);
            TR_EMIT(vigil_reg_abc(VREG_GET_ERROR_MSG, r, err, 0));
            ip += 1;
            break;
        }

        /* ── Format ────────────────────────────────────────────── */
        case VIGIL_OPCODE_FORMAT_F64: {
            uint32_t prec = rd_u32(code, &ip);
            uint8_t val = vs_pop(&vs);
            uint8_t r = vs_push_at(&vs, val);
            TR_EMIT(vigil_reg_abc(VREG_FORMAT_F64, r, val, (uint8_t)prec));
            break;
        }
        case VIGIL_OPCODE_FORMAT_SPEC: {
            uint32_t w1 = rd_u32(code, &ip);
            uint32_t w2 = rd_raw_u32(code, &ip);
            uint8_t val = vs_pop(&vs);
            uint8_t r = vs_push_at(&vs, val);
            /* Encode as two instructions. */
            TR_EMIT(vigil_reg_abc(VREG_FORMAT_SPEC, r, val, 0));
            TR_EMIT((uint32_t)w1);
            TR_EMIT((uint32_t)w2);
            break;
        }

        /* ── Parse intrinsics ──────────────────────────────────── */
        case VIGIL_OPCODE_PARSE_I32: {
            uint8_t str = vs_pop(&vs);
            uint8_t result_base;
            PUSH_RESULT_REGS(str, 2, result_base);
            TR_EMIT(vigil_reg_abc(VREG_PARSE_I32, result_base, str, (uint8_t)(result_base + 1U)));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_PARSE_F64: {
            uint8_t str = vs_pop(&vs);
            uint8_t result_base;
            PUSH_RESULT_REGS(str, 2, result_base);
            TR_EMIT(vigil_reg_abc(VREG_PARSE_F64, result_base, str, (uint8_t)(result_base + 1U)));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_PARSE_BOOL: {
            uint8_t str = vs_pop(&vs);
            uint8_t result_base;
            PUSH_RESULT_REGS(str, 2, result_base);
            TR_EMIT(vigil_reg_abc(VREG_PARSE_BOOL, result_base, str, (uint8_t)(result_base + 1U)));
            ip += 1;
            break;
        }

        case VIGIL_OPCODE_CHAR_FROM_INT: {
            uint8_t val = vs_pop(&vs);
            uint8_t r = vs_push_result(&vs, val);
            TR_EMIT(vigil_reg_abc(VREG_CHAR_FROM_INT, r, val, 0));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_STRING_TO_C: {
            /* Pops string, pushes i32 codepoint. */
            uint8_t val = vs_pop(&vs);
            uint8_t r = vs_push_result(&vs, val);
            TR_EMIT(vigil_reg_abc(VREG_CHAR_FROM_INT, r, val, 1)); /* reuse with flag */
            ip += 1;
            break;
        }

        /* ── String/collection ops — encode as VREG_STRING_OP ──── */
        case VIGIL_OPCODE_STRING_CONTAINS:
        case VIGIL_OPCODE_STRING_STARTS_WITH:
        case VIGIL_OPCODE_STRING_ENDS_WITH:
        case VIGIL_OPCODE_STRING_EQUAL_FOLD: {
            /* Pop 2 (str, arg), push 1 (bool). */
            SYNC_PACK(2); uint8_t arg =
            vs_pop(&vs);
            uint8_t str = vs_pop(&vs);
            uint8_t r = vs_push_result(&vs, str);
            TR_EMIT(vigil_reg_abc(VREG_STRING_OP, r, arg, op));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_STRING_INDEX_OF:
        case VIGIL_OPCODE_STRING_LAST_INDEX_OF: {
            /* Pop 2 (str, arg), push 2 (idx, found). */
            SYNC_PACK(2); uint8_t arg =
            vs_pop(&vs);
            uint8_t str = vs_pop(&vs);
            uint8_t result_base;
            PUSH_RESULT_REGS(str, 2, result_base);
            TR_EMIT(vigil_reg_abc(VREG_STRING_OP, result_base, arg, op));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_STRING_REPLACE: {
            SYNC_PACK(3); uint8_t arg2 =
            vs_pop(&vs);
            uint8_t arg1 = vs_pop(&vs);
            (void)arg1;
            uint8_t str = vs_pop(&vs);
            uint8_t r = vs_push_result(&vs, str);
            TR_EMIT(vigil_reg_abc(VREG_STRING_OP, r, arg2, op));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_STRING_TRIM:
        case VIGIL_OPCODE_STRING_TRIM_LEFT:
        case VIGIL_OPCODE_STRING_TRIM_RIGHT:
        case VIGIL_OPCODE_STRING_TO_UPPER:
        case VIGIL_OPCODE_STRING_TO_LOWER:
        case VIGIL_OPCODE_STRING_REVERSE:
        case VIGIL_OPCODE_STRING_IS_EMPTY:
        case VIGIL_OPCODE_STRING_BYTES:
        case VIGIL_OPCODE_STRING_CHAR_COUNT:
        case VIGIL_OPCODE_STRING_FIELDS: {
            /* One-arg string ops: pop 1, push 1. */
            uint8_t str = vs_pop(&vs);
            uint8_t r = vs_push_result(&vs, str);
            TR_EMIT(vigil_reg_abc(VREG_STRING_OP, r, str, op));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_STRING_SPLIT:
        case VIGIL_OPCODE_STRING_TRIM_PREFIX:
        case VIGIL_OPCODE_STRING_TRIM_SUFFIX: {
            SYNC_PACK(2); uint8_t arg =
            vs_pop(&vs);
            uint8_t str = vs_pop(&vs);
            uint8_t r = vs_push_result(&vs, str);
            TR_EMIT(vigil_reg_abc(VREG_STRING_OP, r, arg, op));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_STRING_SUBSTR: {
            /* Pop 3 (str, start, end), push 2 (result, err). */
            SYNC_PACK(3); uint8_t end_r =
            vs_pop(&vs);
            uint8_t start_r = vs_pop(&vs);
            uint8_t str = vs_pop(&vs);
            (void)start_r; (void)end_r;
            uint8_t result_base;
            PUSH_RESULT_REGS(str, 2, result_base);
            TR_EMIT(vigil_reg_abc(VREG_STRING_OP, result_base, end_r, op));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_STRING_CUT: {
            /* Pop 2 (str, sep), push 3 (before, after, found). */
            SYNC_PACK(2); uint8_t arg =
            vs_pop(&vs);
            uint8_t str = vs_pop(&vs);
            uint8_t result_base;
            PUSH_RESULT_REGS(str, 3, result_base);
            TR_EMIT(vigil_reg_abc(VREG_STRING_OP, result_base, arg, op));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_STRING_REPEAT:
        case VIGIL_OPCODE_STRING_COUNT:
        case VIGIL_OPCODE_STRING_JOIN: {
            SYNC_PACK(2); uint8_t arg = vs_pop(&vs);
            uint8_t str = vs_pop(&vs);
            uint8_t r = vs_push_result(&vs, str);
            TR_EMIT(vigil_reg_abc(VREG_STRING_OP, r, arg, op));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_STRING_CHAR_AT: {
            /* Pop 2 (str, idx), push 2 (char, err). */
            SYNC_PACK(2); uint8_t arg =
            vs_pop(&vs);
            uint8_t str = vs_pop(&vs);
            uint8_t result_base;
            PUSH_RESULT_REGS(str, 2, result_base);
            TR_EMIT(vigil_reg_abc(VREG_STRING_OP, result_base, arg, op));
            ip += 1;
            break;
        }

        /* ── Collection method ops ─────────────────────────────── */
        case VIGIL_OPCODE_ARRAY_PUSH: {
            SYNC_PACK(2); uint8_t val =
            vs_pop(&vs);
            uint8_t arr = vs_pop(&vs);
            TR_EMIT(vigil_reg_abc(VREG_ARRAY_PUSH, arr, arr, val));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_ARRAY_POP: {
            /* Stack: arr, default. Pop 2, push 2 (value, err). */
            SYNC_PACK(2);
            uint8_t def = vs_pop(&vs);
            uint8_t arr = vs_pop(&vs);
            uint8_t r1 = vs_push_at(&vs, arr);
            uint8_t r2 = vs_push_at(&vs, (uint8_t)(arr + 1));
            (void)r1; (void)r2;
            TR_EMIT(vigil_reg_abc(VREG_ARRAY_POP, arr, arr, def));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_ARRAY_GET_SAFE: {
            /* Stack: arr, idx, default. Pop 3, push 2 (value, err). */
            SYNC_PACK(3);
            uint8_t third = vs_pop(&vs);
            uint8_t idx = vs_pop(&vs);
            uint8_t arr = vs_pop(&vs);
            uint8_t result_base;
            (void)idx;
            PUSH_RESULT_REGS(arr, 2, result_base);
            TR_EMIT(vigil_reg_abc(VREG_ARRAY_GET_SAFE, result_base, arr, third));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_ARRAY_SET_SAFE: {
            /* Stack: arr, idx, value. Pop 3, push 1 (err). */
            SYNC_PACK(3);
            uint8_t third = vs_pop(&vs);
            uint8_t idx = vs_pop(&vs);
            uint8_t arr = vs_pop(&vs);
            uint8_t r1 = vs_push_result(&vs, arr);
            (void)idx;
            TR_EMIT(vigil_reg_abc(VREG_ARRAY_SET_SAFE, r1, arr, third));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_ARRAY_SLICE: {
            SYNC_PACK(3); uint8_t end =
            vs_pop(&vs);
            uint8_t start = vs_pop(&vs);
            (void)start;
            uint8_t arr = vs_pop(&vs);
            uint8_t r1 = vs_push_result(&vs, arr);
            TR_EMIT(vigil_reg_abc(VREG_ARRAY_SLICE, r1, arr, end));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_ARRAY_CONTAINS: {
            SYNC_PACK(2);
            uint8_t val = vs_pop(&vs);
            uint8_t arr = vs_pop(&vs);
            uint8_t r = vs_push_result(&vs, arr);
            TR_EMIT(vigil_reg_abc(VREG_ARRAY_CONTAINS, r, arr, val));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_MAP_GET_SAFE:
        case VIGIL_OPCODE_MAP_REMOVE_SAFE: {
            /* Stack: map, key, default. Pop 3, push 2 (value, err). */
            SYNC_PACK(3);
            uint8_t third = vs_pop(&vs);
            uint8_t key = vs_pop(&vs);
            uint8_t map = vs_pop(&vs);
            uint8_t result_base;
            (void)key;
            PUSH_RESULT_REGS(map, 2, result_base);
            uint8_t rop = (op == VIGIL_OPCODE_MAP_GET_SAFE) ? VREG_MAP_GET_SAFE : VREG_MAP_REMOVE_SAFE;
            TR_EMIT(vigil_reg_abc(rop, result_base, map, third));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_MAP_SET_SAFE: {
            /* Stack: map, key, value. Pop 3, push 1 (err). */
            SYNC_PACK(3);
            uint8_t third = vs_pop(&vs);
            uint8_t key = vs_pop(&vs);
            uint8_t map = vs_pop(&vs);
            uint8_t r1 = vs_push_result(&vs, map);
            (void)key;
            TR_EMIT(vigil_reg_abc(VREG_MAP_SET_SAFE, r1, map, third));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_MAP_HAS: {
            SYNC_PACK(2);
            uint8_t key = vs_pop(&vs);
            uint8_t map = vs_pop(&vs);
            uint8_t r = vs_push_result(&vs, map);
            TR_EMIT(vigil_reg_abc(VREG_MAP_HAS, r, map, key));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_MAP_KEYS:
        case VIGIL_OPCODE_MAP_VALUES: {
            uint8_t map = vs_pop(&vs);
            uint8_t r = vs_push_result(&vs, map);
            uint8_t rop = (op == VIGIL_OPCODE_MAP_KEYS) ? VREG_MAP_KEYS : VREG_MAP_VALUES;
            TR_EMIT(vigil_reg_abc(rop, r, map, 0));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_GET_MAP_KEY_AT:
        case VIGIL_OPCODE_GET_MAP_VALUE_AT: {
            SYNC_PACK(2);
            uint8_t idx = vs_pop(&vs);
            uint8_t map = vs_pop(&vs);
            uint8_t r = vs_push_result(&vs, map);
            uint8_t rop = (op == VIGIL_OPCODE_GET_MAP_KEY_AT) ? VREG_MAP_KEY_AT : VREG_MAP_VALUE_AT;
            TR_EMIT(vigil_reg_abc(rop, r, map, idx));
            ip += 1;
            break;
        }

        /* ── Defer ops — encode generically ────────────────────── */
        case VIGIL_OPCODE_DEFER_CALL: {
            uint32_t a = rd_u32(code, &ip);
            uint32_t b = rd_raw_u32(code, &ip);
            uint32_t c = rd_raw_u32(code, &ip);
            (void)c;
            /* Pack and pop the deferred args so SYNC_PRE can find them. */
            SYNC_PACK(b);
            uint8_t top_r = (b > 0) ? vs.regs[vs.top - 1] : 0;
            for (uint32_t di = 0; di < b; di++) vs_pop(&vs);
            TR_EMIT(vigil_reg_abc(VREG_DEFER, op, (uint8_t)a, top_r));
            TR_EMIT((uint32_t)a);
            TR_EMIT((uint32_t)b);
            break;
        }
        case VIGIL_OPCODE_DEFER_CALL_VALUE: {
            uint32_t a = rd_u32(code, &ip); /* arg_count (excl callee) */
            uint32_t val_count = a + 1; /* include the callee */
            SYNC_PACK(val_count);
            uint8_t top_r = (val_count > 0) ? vs.regs[vs.top - 1] : 0;
            for (uint32_t di = 0; di < val_count; di++) vs_pop(&vs);
            TR_EMIT(vigil_reg_abc(VREG_DEFER, op, 0, top_r));
            TR_EMIT((uint32_t)0);
            TR_EMIT((uint32_t)val_count);
            break;
        }
        case VIGIL_OPCODE_DEFER_NEW_INSTANCE:
        case VIGIL_OPCODE_DEFER_CALL_NATIVE: {
            uint32_t a = rd_u32(code, &ip);
            uint32_t b = rd_raw_u32(code, &ip);
            if (op == VIGIL_OPCODE_DEFER_CALL_NATIVE)
                rd_raw_u32(code, &ip); /* skip return_count */
            SYNC_PACK(b);
            uint8_t top_r = (b > 0) ? vs.regs[vs.top - 1] : 0;
            for (uint32_t di = 0; di < b; di++) vs_pop(&vs);
            TR_EMIT(vigil_reg_abc(VREG_DEFER, op, (uint8_t)a, top_r));
            TR_EMIT((uint32_t)a);
            TR_EMIT((uint32_t)b);
            break;
        }
        case VIGIL_OPCODE_DEFER_CALL_INTERFACE: {
            uint32_t a = rd_u32(code, &ip);  /* iface_index */
            uint32_t b = rd_raw_u32(code, &ip);  /* method_index */
            uint32_t c = rd_raw_u32(code, &ip);  /* arg_count (excl receiver) */
            uint32_t total = c + 1; /* include receiver */
            SYNC_PACK(total);
            uint8_t top_r = (total > 0) ? vs.regs[vs.top - 1] : 0;
            for (uint32_t di = 0; di < total; di++) vs_pop(&vs);
            TR_EMIT(vigil_reg_abc(VREG_DEFER, op, (uint8_t)a, top_r));
            TR_EMIT((uint32_t)a);      /* operand_a = iface_index */
            TR_EMIT((uint32_t)total);  /* operand_b = total values (used as val_count) */
            TR_EMIT((uint32_t)b);      /* extra word: method_index */
            break;
        }

        default:
            /* Unknown opcode — skip it. This shouldn't happen for valid bytecode. */
            ip += stack_op_size(code, ip, code_size);
            break;
        }

        if (vs.next_reg > max_next_reg)
            max_next_reg = vs.next_reg;
    }

    /* Record final offset mapping. */
    omap_add(&omap, ip, rc->code_count);

    /* ── Pass 3: Patch jumps ───────────────────────────────────── */
    for (size_t i = 0; i < patches.count; i++)
    {
        size_t ri = patches.items[i].reg_instr_idx;
        size_t target_off = patches.items[i].target_stack_off;
        size_t target_ri = omap_lookup(&omap, target_off);
        if (target_ri == (size_t)-1)
        {
            /* Target not found — find nearest. */
            size_t best = rc->code_count;
            size_t best_diff = (size_t)-1;
            for (size_t j = 0; j < omap.count; j++)
            {
                size_t diff = (omap.stack_offsets[j] > target_off) ? omap.stack_offsets[j] - target_off
                                                                   : target_off - omap.stack_offsets[j];
                if (diff < best_diff)
                {
                    best_diff = diff;
                    best = omap.reg_indices[j];
                }
            }
            target_ri = best;
        }
        int32_t offset = (int32_t)target_ri - (int32_t)(ri + 1);
        int16_t sbx = (int16_t)offset;
        rc->code[ri] = vigil_reg_asbx(VREG_GET_OP(rc->code[ri]), VREG_GET_A(rc->code[ri]), sbx);
    }

    rc->max_registers = max_next_reg;
    rc->span_map_count = rc->code_count;

    free(jt);
    free(depth_at);
    free(origin_at);
    free(state_at);
    omap_free(&omap);
    jpatch_free(&patches);
    return VIGIL_STATUS_OK;

tr_status_fail:
    free(jt);
    free(depth_at);
    free(origin_at);
    free(state_at);
    omap_free(&omap);
    jpatch_free(&patches);
    vigil_reg_chunk_free(rc, runtime);
    return tr_status;

tr_fail:
    tr_status = VIGIL_STATUS_OUT_OF_MEMORY;
    goto tr_status_fail;
}

/* ══════════════════════════════════════════════════════════════════
 * Register VM Dispatch Loop
 * ══════════════════════════════════════════════════════════════════ */

/* Computed-goto detection. */
#if defined(__GNUC__) || defined(__clang__)
#define REGVM_COMPUTED_GOTO 1
#else
#define REGVM_COMPUTED_GOTO 0
#endif

/* ── Defer drain helper ────────────────────────────────────────── */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static vigil_status_t regvm_drain_defers(vigil_vm_t *vm, size_t frame_idx, vigil_error_t *error)
{
    while (vm->frames[frame_idx].defer_count > 0U)
    {
        vigil_vm_frame_t *frame = &vm->frames[frame_idx];
        vigil_vm_defer_action_t action = frame->defers[frame->defer_count - 1U];
        memset(&frame->defers[frame->defer_count - 1U], 0, sizeof(action));
        frame->defer_count -= 1U;

        /* Push deferred argument values onto the stack. */
        for (size_t i = 0; i < action.value_count; i++)
        {
            vigil_status_t s = vigil_vm_push(vm, &action.values[i], error);
            if (s != VIGIL_STATUS_OK) { free(action.values); return s; }
        }

        vigil_status_t s = VIGIL_STATUS_OK;
        switch (action.kind)
        {
        case VIGIL_VM_DEFER_CALL: {
            const vigil_object_t *callee = vigil_vm_function_sibling(frame->function, (size_t)action.operand_a);
            if (callee)
                s = vigil_vm_execute_call(vm, callee, action.arg_count, error);
            break;
        }
        case VIGIL_VM_DEFER_CALL_NATIVE: {
            const vigil_value_t *nval = VIGIL_VM_CHUNK_CONSTANT(frame->chunk, (size_t)action.operand_a);
            vigil_object_t *nobj = (nval && vigil_nanbox_has_object(*nval))
                ? (vigil_object_t *)vigil_nanbox_decode_ptr(*nval) : NULL;
            vigil_native_fn_t nfn = nobj ? vigil_native_function_get(nobj) : NULL;
            if (nfn)
                s = nfn(vm, action.arg_count, error);
            else {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL,
                                        "deferred call target is not a native function");
                s = VIGIL_STATUS_INTERNAL;
            }
            break;
        }
        case VIGIL_VM_DEFER_CALL_VALUE: {
            /* The callee is the first pushed value; args follow. */
            size_t total = action.arg_count;
            if (total > 0 && vm->stack_count >= total)
            {
                size_t callee_slot = vm->stack_count - total;
                vigil_value_t cv = vm->stack[callee_slot];
                if (vigil_nanbox_is_object(cv))
                {
                    vigil_object_t *callee_obj = (vigil_object_t *)vigil_nanbox_decode_ptr(cv);
                    /* Shift args down over the callee slot. */
                    size_t real_args = total - 1;
                    vigil_value_release(&vm->stack[callee_slot]);
                    if (real_args > 0)
                        memmove(&vm->stack[callee_slot], &vm->stack[callee_slot + 1], real_args * sizeof(vigil_value_t));
                    vm->stack[callee_slot + real_args] = VIGIL_NANBOX_NIL;
                    vm->stack_count -= 1;
                    s = vigil_vm_execute_call(vm, callee_obj, real_args, error);
                }
            }
            break;
        }
        case VIGIL_VM_DEFER_CALL_INTERFACE: {
            size_t total = action.arg_count;
            if (total > 0 && vm->stack_count >= total)
            {
                size_t recv_slot = vm->stack_count - total;
                vigil_value_t rv = vm->stack[recv_slot];
                if (vigil_nanbox_is_object(rv))
                {
                    vigil_object_t *recv_obj = (vigil_object_t *)vigil_nanbox_decode_ptr(rv);
                    if (vigil_object_type(recv_obj) == VIGIL_OBJECT_INSTANCE)
                    {
                        frame = &vm->frames[frame_idx];
                        size_t ci = vigil_instance_object_class_index(recv_obj);
                        const vigil_object_t *callee = vigil_function_object_resolve_interface_method(
                            frame->function, ci, (size_t)action.operand_a, (size_t)action.operand_b);
                        if (callee)
                            s = vigil_vm_execute_call(vm, callee, total, error);
                    }
                }
            }
            break;
        }
        default:
            break;
        }
        for (size_t i = 0; i < action.value_count; i++)
            vigil_value_release(&action.values[i]);
        free(action.values);
        if (s != VIGIL_STATUS_OK) return s;
    }
    return VIGIL_STATUS_OK;
}

/* Decode int64 from a nanbox — handles both inline and bigint encoding.
   For bigint objects we must go through the value-layer API to read the
   full 64-bit value stored on the heap. */
static inline int64_t regvm_decode_int(uint64_t v)
{
    if (VIGIL_UNLIKELY(vigil_nanbox_is_bigint(v)))
        return vigil_value_as_int((const vigil_value_t *)&v);
    return vigil_nanbox_decode_int(v);
}

static inline uint64_t regvm_decode_uint(uint64_t v)
{
    if (VIGIL_UNLIKELY(vigil_nanbox_is_biguint(v)))
        return vigil_value_as_uint((const vigil_value_t *)&v);
    return vigil_nanbox_decode_uint(v);
}

/* Encode int64/uint64 safely — falls back to bigint/biguint for values
   that do not fit in the 48-bit inline nanbox payload. */
static inline vigil_value_t regvm_encode_int(int64_t v)
{
    vigil_value_t val;
    vigil_value_init_int(&val, v);
    return val;
}

static inline vigil_value_t regvm_encode_uint(uint64_t v)
{
    vigil_value_t val;
    vigil_value_init_uint(&val, v);
    return val;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_regvm_execute(vigil_vm_t *vm, const vigil_reg_chunk_t *rc, vigil_value_t *out_value,
                                   vigil_error_t *error)
{
    if (!vm || !rc || !out_value)
        return VIGIL_STATUS_INVALID_ARGUMENT;

    const vigil_reg_instr_t *code = rc->code;
    size_t code_count = rc->code_count;
    (void)code_count;

    if (code == NULL || code_count == 0)
    {
        *out_value = VIGIL_NANBOX_NIL;
        vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "unsupported opcode");
        /* Try to recover source location from the stack chunk's span map. */
        if (rc->stack_chunk != NULL)
        {
            const vigil_chunk_t *sc2 = rc->stack_chunk;
            if (sc2->span_count > 0)
            {
                error->location.source_id = sc2->spans[0].source_id;
                error->location.offset = sc2->spans[0].start_offset;
            }
        }
        return VIGIL_STATUS_UNSUPPORTED;
    }

    const vigil_chunk_t *sc = rc->stack_chunk;
    vigil_value_t *R = vm->stack; /* register file = stack base */
    size_t ip = 0;
    vigil_status_t status = VIGIL_STATUS_OK;
    size_t initial_frame_count = vm->frame_count;
    int has_reg_objects = 0; /* set when an object is stored in a register */
    uint8_t ret_base_r = 0; /* first return-value register (set by RETURN) */
    uint8_t ret_count = 0;  /* number of return values */

    /* Ensure stack has room for registers. */
    /* Set up initial frame if needed. */
    vigil_vm_frame_t *frame = &vm->frames[vm->frame_count - 1];
    size_t base = frame->base_slot;

    if (vm->stack_capacity < base + (size_t)rc->max_registers + 16)
    {
        status = vigil_vm_grow_stack(vm, base + (size_t)rc->max_registers + 16, error);
        if (status != VIGIL_STATUS_OK)
            return status;
    }
    R = vm->stack + base;

    if (vm->stack_count < base + rc->max_registers)
    {
        if (vm->stack_capacity < base + rc->max_registers)
        {
            status = vigil_vm_grow_stack(vm, base + (size_t)rc->max_registers + 16, error);
            if (status != VIGIL_STATUS_OK)
                return status;
            R = vm->stack + base;
        }
        vm->stack_count = base + rc->max_registers;
    }

#define RRELEASE(reg)                                                                                                   \
    do                                                                                                                  \
    {                                                                                                                   \
        if (vigil_nanbox_has_object(R[(reg)]))                                                                         \
            vigil_value_release(&R[(reg)]);                                                                            \
        R[(reg)] = VIGIL_NANBOX_NIL;                                                                                   \
    } while (0)
#define RSTORE(reg, value_expr)                                                                                       \
    do                                                                                                               \
    {                                                                                                                \
        vigil_value_t _rstore_value = (value_expr);                                                                  \
        RRELEASE(reg);                                                                                               \
        R[(reg)] = _rstore_value;                                                                                    \
    } while (0)

#if REGVM_COMPUTED_GOTO
    _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wpedantic\"")

        /* Dispatch table. */
        static const void *dtable[256] = {
            [VREG_MOVE] = &&r_MOVE,
            [VREG_LOAD_K] = &&r_LOAD_K,
            [VREG_LOAD_NIL] = &&r_LOAD_NIL,
            [VREG_LOAD_TRUE] = &&r_LOAD_TRUE,
            [VREG_LOAD_FALSE] = &&r_LOAD_FALSE,
            [VREG_ADD] = &&r_ADD,
            [VREG_SUB] = &&r_SUB,
            [VREG_MUL] = &&r_MUL,
            [VREG_DIV] = &&r_DIV,
            [VREG_MOD] = &&r_MOD,
            [VREG_ADD_I32] = &&r_ADD_I32,
            [VREG_SUB_I32] = &&r_SUB_I32,
            [VREG_MUL_I32] = &&r_MUL_I32,
            [VREG_DIV_I32] = &&r_DIV_I32,
            [VREG_MOD_I32] = &&r_MOD_I32,
            [VREG_ADD_I64] = &&r_ADD_I64,
            [VREG_SUB_I64] = &&r_SUB_I64,
            [VREG_MUL_I64] = &&r_MUL_I64,
            [VREG_DIV_I64] = &&r_DIV_I64,
            [VREG_MOD_I64] = &&r_MOD_I64,
            [VREG_ADD_F64] = &&r_ADD_F64,
            [VREG_SUB_F64] = &&r_SUB_F64,
            [VREG_MUL_F64] = &&r_MUL_F64,
            [VREG_DIV_F64] = &&r_DIV_F64,
            [VREG_LT_I32] = &&r_LT_I32,
            [VREG_LE_I32] = &&r_LE_I32,
            [VREG_GT_I32] = &&r_GT_I32,
            [VREG_GE_I32] = &&r_GE_I32,
            [VREG_EQ_I32] = &&r_EQ_I32,
            [VREG_NE_I32] = &&r_NE_I32,
            [VREG_LT_I64] = &&r_LT_I64,
            [VREG_LE_I64] = &&r_LE_I64,
            [VREG_GT_I64] = &&r_GT_I64,
            [VREG_GE_I64] = &&r_GE_I64,
            [VREG_EQ_I64] = &&r_EQ_I64,
            [VREG_NE_I64] = &&r_NE_I64,
            [VREG_NEG] = &&r_NEG,
            [VREG_NOT] = &&r_NOT,
            [VREG_BNOT] = &&r_BNOT,
            [VREG_BAND] = &&r_BAND,
            [VREG_BOR] = &&r_BOR,
            [VREG_BXOR] = &&r_BXOR,
            [VREG_SHL] = &&r_SHL,
            [VREG_SHR] = &&r_SHR,
            [VREG_TO_I32] = &&r_TO_I32,
            [VREG_TO_I64] = &&r_TO_I64,
            [VREG_TO_F64] = &&r_TO_F64,
            [VREG_TO_STRING] = &&r_TO_STRING,
            [VREG_JMP] = &&r_JMP,
            [VREG_TEST] = &&r_TEST,
            [VREG_LT_I32_JMP] = &&r_LT_I32_JMP,
            [VREG_LE_I32_JMP] = &&r_LE_I32_JMP,
            [VREG_GT_I32_JMP] = &&r_GT_I32_JMP,
            [VREG_GE_I32_JMP] = &&r_GE_I32_JMP,
            [VREG_EQ_I32_JMP] = &&r_EQ_I32_JMP,
            [VREG_NE_I32_JMP] = &&r_NE_I32_JMP,
            [VREG_LT_I64_JMP] = &&r_LT_I64_JMP,
            [VREG_LE_I64_JMP] = &&r_LE_I64_JMP,
            [VREG_GT_I64_JMP] = &&r_GT_I64_JMP,
            [VREG_GE_I64_JMP] = &&r_GE_I64_JMP,
            [VREG_EQ_I64_JMP] = &&r_EQ_I64_JMP,
            [VREG_NE_I64_JMP] = &&r_NE_I64_JMP,
            [VREG_FORLOOP_I32] = &&r_FORLOOP_I32,
            [VREG_FORLOOP_I64] = &&r_FORLOOP_I64,
            [VREG_INC_I32] = &&r_INC_I32,
            [VREG_INC_I64] = &&r_INC_I64,
            [VREG_RETURN] = &&r_RETURN,
            [VREG_CALL_NATIVE] = &&r_CALL_NATIVE,
            [VREG_CALL] = &&r_CALL,
            [VREG_GET_GLOBAL] = &&r_GET_GLOBAL,
            [VREG_SET_GLOBAL] = &&r_SET_GLOBAL,
            [VREG_GET_CAPTURE] = &&r_GET_CAPTURE,
            [VREG_SET_CAPTURE] = &&r_SET_CAPTURE,
            [VREG_GET_FUNCTION] = &&r_GET_FUNCTION,
            [VREG_NEW_CLOSURE] = &&r_NEW_CLOSURE,
            [VREG_MATH_SIN] = &&r_MATH_SIN,
            [VREG_MATH_COS] = &&r_MATH_COS,
            [VREG_MATH_SQRT] = &&r_MATH_SQRT,
            [VREG_MATH_LOG] = &&r_MATH_LOG,
            [VREG_MATH_POW] = &&r_MATH_POW,
            [VREG_EQ] = &&r_EQ,
            [VREG_LT] = &&r_LT,
            [VREG_LE] = &&r_LE,
            [VREG_DUP] = &&r_DUP,
            [VREG_RELEASE] = &&r_RELEASE,
            [VREG_TO_U8] = &&r_TO_U8,
            [VREG_TO_U32] = &&r_TO_U32,
            [VREG_TO_U64] = &&r_TO_U64,
            [VREG_TESTSET] = &&r_TESTSET,
            [VREG_CALL_VALUE] = &&r_CALL_VALUE,
            [VREG_CALL_SELF] = &&r_CALL_SELF,
            [VREG_CALL_INTERFACE] = &&r_CALL_INTERFACE,
            [VREG_CALL_EXTERN] = &&r_CALL_EXTERN,
            [VREG_TAIL_CALL] = &&r_TAIL_CALL,
            [VREG_NEW_INSTANCE] = &&r_NEW_INSTANCE,
            [VREG_GET_FIELD] = &&r_GET_FIELD,
            [VREG_SET_FIELD] = &&r_SET_FIELD,
            [VREG_NEW_ARRAY] = &&r_NEW_ARRAY,
            [VREG_NEW_MAP] = &&r_NEW_MAP,
            [VREG_GET_INDEX] = &&r_GET_INDEX,
            [VREG_SET_INDEX] = &&r_SET_INDEX,
            [VREG_COLLECTION_SIZE] = &&r_COLLECTION_SIZE,
            [VREG_NEW_ERROR] = &&r_NEW_ERROR,
            [VREG_GET_ERROR_KIND] = &&r_GET_ERROR_KIND,
            [VREG_GET_ERROR_MSG] = &&r_GET_ERROR_MSG,
            [VREG_STRING_OP] = &&r_STRING_OP,
            [VREG_FORMAT_F64] = &&r_FORMAT_F64,
            [VREG_FORMAT_SPEC] = &&r_FORMAT_SPEC,
            [VREG_PARSE_I32] = &&r_PARSE_I32,
            [VREG_PARSE_F64] = &&r_PARSE_F64,
            [VREG_PARSE_BOOL] = &&r_PARSE_BOOL,
            [VREG_DEFER] = &&r_DEFER,
            [VREG_MAP_KEY_AT] = &&r_MAP_KEY_AT,
            [VREG_MAP_VALUE_AT] = &&r_MAP_VALUE_AT,
            [VREG_ARRAY_PUSH] = &&r_ARRAY_PUSH,
            [VREG_ARRAY_POP] = &&r_ARRAY_POP,
            [VREG_ARRAY_GET_SAFE] = &&r_ARRAY_GET_SAFE,
            [VREG_ARRAY_SET_SAFE] = &&r_ARRAY_SET_SAFE,
            [VREG_ARRAY_SLICE] = &&r_ARRAY_SLICE,
            [VREG_ARRAY_CONTAINS] = &&r_ARRAY_CONTAINS,
            [VREG_MAP_GET_SAFE] = &&r_MAP_GET_SAFE,
            [VREG_MAP_SET_SAFE] = &&r_MAP_SET_SAFE,
            [VREG_MAP_REMOVE_SAFE] = &&r_MAP_REMOVE_SAFE,
            [VREG_MAP_HAS] = &&r_MAP_HAS,
            [VREG_MAP_KEYS] = &&r_MAP_KEYS,
            [VREG_MAP_VALUES] = &&r_MAP_VALUES,
            [VREG_CHAR_FROM_INT] = &&r_CHAR_FROM_INT,
        };

    /* Patch NULLs. */
    static int patched = 0;
    if (VIGIL_UNLIKELY(!patched))
    {
        for (int i = 0; i < 256; i++)
            if (dtable[i] == NULL)
                ((const void **)dtable)[i] = &&r_UNKNOWN;
        patched = 1;
    }

#define REGVM_DEBUG_HOOK()                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (VIGIL_UNLIKELY(vm->debug_hook != NULL))                                                                    \
        {                                                                                                              \
            size_t _saved = frame->ip;                                                                                 \
            frame->ip = (ip < rc->span_map_count) ? rc->span_map[ip] : 0;                                             \
            if (vm->debug_hook(vm, vm->debug_hook_userdata) != 0)                                                      \
            { frame->ip = _saved; status = VIGIL_STATUS_OK; goto r_cleanup; }                                          \
            frame->ip = _saved;                                                                                        \
        }                                                                                                              \
    } while (0)

#define RDISPATCH() goto *dtable[VREG_GET_OP(code[ip])]
#define RNEXT()                                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        ip++;                                                                                                          \
        REGVM_DEBUG_HOOK();                                                                                            \
        RDISPATCH();                                                                                                   \
    } while (0)
#define RCASE(op) r_##op:
#else
#define REGVM_DEBUG_HOOK()                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (VIGIL_UNLIKELY(vm->debug_hook != NULL))                                                                    \
        {                                                                                                              \
            size_t _saved = frame->ip;                                                                                 \
            frame->ip = (ip < rc->span_map_count) ? rc->span_map[ip] : 0;                                             \
            if (vm->debug_hook(vm, vm->debug_hook_userdata) != 0)                                                      \
            { frame->ip = _saved; status = VIGIL_STATUS_OK; goto r_cleanup; }                                          \
            frame->ip = _saved;                                                                                        \
        }                                                                                                              \
    } while (0)
#define RDISPATCH() break
#define RNEXT() do { ip++; } while (0); break
#define RCASE(op) case VREG_##op:
#endif

#if REGVM_COMPUTED_GOTO
    REGVM_DEBUG_HOOK();
    RDISPATCH();
#else
    while (ip < code_count)
    {
        REGVM_DEBUG_HOOK();
        switch (VREG_GET_OP(code[ip]))
        {
#endif

    /* ── Data movement ─────────────────────────────────────────── */
    RCASE(LOAD_K)
    {
        vigil_reg_instr_t i = code[ip];
        const vigil_value_t *k = VIGIL_VM_CHUNK_CONSTANT(sc, (size_t)VREG_GET_Bx(i));
        if (k)
        {
            if (vigil_nanbox_has_object(*k))
            { RRELEASE(VREG_GET_A(i)); VIGIL_VM_VALUE_COPY(&R[VREG_GET_A(i)], k); has_reg_objects = 1; }
            else
            {
                RRELEASE(VREG_GET_A(i));
                R[VREG_GET_A(i)] = *k;
            }
        }
        RNEXT();
    }
    RCASE(MOVE)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t dst = VREG_GET_A(i), src = VREG_GET_B(i);
        if (dst != src)
        {
            RRELEASE(dst);
            if (vigil_nanbox_has_object(R[src]))
                VIGIL_VM_VALUE_COPY(&R[dst], &R[src]);
            else
                R[dst] = R[src];
        }
        RNEXT();
    }
    RCASE(LOAD_NIL)
    {
        RSTORE(VREG_GET_A(code[ip]), VIGIL_NANBOX_NIL);
        RNEXT();
    }
    RCASE(LOAD_TRUE)
    {
        RSTORE(VREG_GET_A(code[ip]), VIGIL_NANBOX_TRUE);
        RNEXT();
    }
    RCASE(LOAD_FALSE)
    {
        RSTORE(VREG_GET_A(code[ip]), VIGIL_NANBOX_FALSE);
        RNEXT();
    }
    RCASE(DUP)
    {
        vigil_reg_instr_t i = code[ip];
        RRELEASE(VREG_GET_A(i));
        VIGIL_VM_VALUE_COPY(&R[VREG_GET_A(i)], &R[VREG_GET_B(i)]);
        RNEXT();
    }
    RCASE(RELEASE)
    {
        RRELEASE(VREG_GET_A(code[ip]));
        RNEXT();
    }

    /* ── i32 arithmetic ────────────────────────────────────────── */
    RCASE(ADD_I32)
    {
        vigil_reg_instr_t i = code[ip];
        int32_t a = vigil_nanbox_decode_i32(R[VREG_GET_B(i)]);
        int32_t b = vigil_nanbox_decode_i32(R[VREG_GET_C(i)]);
        int32_t r;
        if (VIGIL_UNLIKELY(VIGIL_I32_ADD_OVERFLOW(a, b, &r)))
            goto r_overflow;
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_i32(r));
        RNEXT();
    }
    RCASE(SUB_I32)
    {
        vigil_reg_instr_t i = code[ip];
        int32_t a = vigil_nanbox_decode_i32(R[VREG_GET_B(i)]);
        int32_t b = vigil_nanbox_decode_i32(R[VREG_GET_C(i)]);
        int32_t r;
        if (VIGIL_UNLIKELY(VIGIL_I32_SUB_OVERFLOW(a, b, &r)))
            goto r_overflow;
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_i32(r));
        RNEXT();
    }
    RCASE(MUL_I32)
    {
        vigil_reg_instr_t i = code[ip];
        int32_t a = vigil_nanbox_decode_i32(R[VREG_GET_B(i)]);
        int32_t b = vigil_nanbox_decode_i32(R[VREG_GET_C(i)]);
        int32_t r;
        if (VIGIL_UNLIKELY(VIGIL_I32_MUL_OVERFLOW(a, b, &r)))
            goto r_overflow;
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_i32(r));
        RNEXT();
    }
    RCASE(DIV_I32)
    {
        vigil_reg_instr_t i = code[ip];
        int32_t a = vigil_nanbox_decode_i32(R[VREG_GET_B(i)]);
        int32_t b = vigil_nanbox_decode_i32(R[VREG_GET_C(i)]);
        if (VIGIL_UNLIKELY(b == 0))
            goto r_divzero;
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_i32(a / b));
        RNEXT();
    }
    RCASE(MOD_I32)
    {
        vigil_reg_instr_t i = code[ip];
        int32_t a = vigil_nanbox_decode_i32(R[VREG_GET_B(i)]);
        int32_t b = vigil_nanbox_decode_i32(R[VREG_GET_C(i)]);
        if (VIGIL_UNLIKELY(b == 0))
            goto r_divzero;
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_i32(a % b));
        RNEXT();
    }

    /* ── i32 comparisons ───────────────────────────────────────── */
    RCASE(LT_I32)
    {
        vigil_reg_instr_t i = code[ip];
        RSTORE(VREG_GET_A(i), vigil_nanbox_from_bool(vigil_nanbox_decode_i32(R[VREG_GET_B(i)]) <
                                                     vigil_nanbox_decode_i32(R[VREG_GET_C(i)])));
        RNEXT();
    }
    RCASE(LE_I32)
    {
        vigil_reg_instr_t i = code[ip];
        RSTORE(VREG_GET_A(i), vigil_nanbox_from_bool(vigil_nanbox_decode_i32(R[VREG_GET_B(i)]) <=
                                                     vigil_nanbox_decode_i32(R[VREG_GET_C(i)])));
        RNEXT();
    }
    RCASE(GT_I32)
    {
        vigil_reg_instr_t i = code[ip];
        RSTORE(VREG_GET_A(i), vigil_nanbox_from_bool(vigil_nanbox_decode_i32(R[VREG_GET_B(i)]) >
                                                     vigil_nanbox_decode_i32(R[VREG_GET_C(i)])));
        RNEXT();
    }
    RCASE(GE_I32)
    {
        vigil_reg_instr_t i = code[ip];
        RSTORE(VREG_GET_A(i), vigil_nanbox_from_bool(vigil_nanbox_decode_i32(R[VREG_GET_B(i)]) >=
                                                     vigil_nanbox_decode_i32(R[VREG_GET_C(i)])));
        RNEXT();
    }
    RCASE(EQ_I32)
    {
        vigil_reg_instr_t i = code[ip];
        RSTORE(VREG_GET_A(i), vigil_nanbox_from_bool(vigil_nanbox_decode_i32(R[VREG_GET_B(i)]) ==
                                                     vigil_nanbox_decode_i32(R[VREG_GET_C(i)])));
        RNEXT();
    }
    RCASE(NE_I32)
    {
        vigil_reg_instr_t i = code[ip];
        RSTORE(VREG_GET_A(i), vigil_nanbox_from_bool(vigil_nanbox_decode_i32(R[VREG_GET_B(i)]) !=
                                                     vigil_nanbox_decode_i32(R[VREG_GET_C(i)])));
        RNEXT();
    }

    /* ── i64 arithmetic ────────────────────────────────────────── */
    RCASE(ADD_I64)
    {
        vigil_reg_instr_t i = code[ip];
        int64_t a = regvm_decode_int(R[VREG_GET_B(i)]);
        int64_t b = regvm_decode_int(R[VREG_GET_C(i)]);
        int64_t r;
        if (VIGIL_UNLIKELY(vigil_vm_checked_add(a, b, &r) != VIGIL_STATUS_OK))
            goto r_overflow;
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_int(r));
        RNEXT();
    }
    RCASE(SUB_I64)
    {
        vigil_reg_instr_t i = code[ip];
        int64_t a = regvm_decode_int(R[VREG_GET_B(i)]);
        int64_t b = regvm_decode_int(R[VREG_GET_C(i)]);
        int64_t r;
        if (VIGIL_UNLIKELY(vigil_vm_checked_subtract(a, b, &r) != VIGIL_STATUS_OK))
            goto r_overflow;
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_int(r));
        RNEXT();
    }
    RCASE(MUL_I64)
    {
        vigil_reg_instr_t i = code[ip];
        int64_t a = regvm_decode_int(R[VREG_GET_B(i)]);
        int64_t b = regvm_decode_int(R[VREG_GET_C(i)]);
        int64_t r;
        if (VIGIL_UNLIKELY(vigil_vm_checked_multiply(a, b, &r) != VIGIL_STATUS_OK))
            goto r_overflow;
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_int(r));
        RNEXT();
    }
    RCASE(DIV_I64)
    {
        vigil_reg_instr_t i = code[ip];
        int64_t a = regvm_decode_int(R[VREG_GET_B(i)]);
        int64_t b = regvm_decode_int(R[VREG_GET_C(i)]);
        if (VIGIL_UNLIKELY(b == 0))
            goto r_divzero;
        int64_t r;
        if (VIGIL_UNLIKELY(vigil_vm_checked_divide(a, b, &r) != VIGIL_STATUS_OK))
            goto r_overflow;
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_int(r));
        RNEXT();
    }
    RCASE(MOD_I64)
    {
        vigil_reg_instr_t i = code[ip];
        int64_t a = regvm_decode_int(R[VREG_GET_B(i)]);
        int64_t b = regvm_decode_int(R[VREG_GET_C(i)]);
        if (VIGIL_UNLIKELY(b == 0))
            goto r_divzero;
        int64_t r;
        if (VIGIL_UNLIKELY(vigil_vm_checked_modulo(a, b, &r) != VIGIL_STATUS_OK))
            goto r_overflow;
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_int(r));
        RNEXT();
    }

    /* ── i64 comparisons ───────────────────────────────────────── */
    RCASE(LT_I64)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t rb = VREG_GET_B(i), rc2 = VREG_GET_C(i);
        RSTORE(VREG_GET_A(i), (vigil_nanbox_is_uint(R[rb]) || vigil_nanbox_is_uint(R[rc2]))
                                  ? vigil_nanbox_from_bool(regvm_decode_uint(R[rb]) < regvm_decode_uint(R[rc2]))
                                  : vigil_nanbox_from_bool(regvm_decode_int(R[rb]) < regvm_decode_int(R[rc2])));
        RNEXT();
    }
    RCASE(LE_I64)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t rb = VREG_GET_B(i), rc2 = VREG_GET_C(i);
        RSTORE(VREG_GET_A(i), (vigil_nanbox_is_uint(R[rb]) || vigil_nanbox_is_uint(R[rc2]))
                                  ? vigil_nanbox_from_bool(regvm_decode_uint(R[rb]) <= regvm_decode_uint(R[rc2]))
                                  : vigil_nanbox_from_bool(regvm_decode_int(R[rb]) <= regvm_decode_int(R[rc2])));
        RNEXT();
    }
    RCASE(GT_I64)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t rb = VREG_GET_B(i), rc2 = VREG_GET_C(i);
        RSTORE(VREG_GET_A(i), (vigil_nanbox_is_uint(R[rb]) || vigil_nanbox_is_uint(R[rc2]))
                                  ? vigil_nanbox_from_bool(regvm_decode_uint(R[rb]) > regvm_decode_uint(R[rc2]))
                                  : vigil_nanbox_from_bool(regvm_decode_int(R[rb]) > regvm_decode_int(R[rc2])));
        RNEXT();
    }
    RCASE(GE_I64)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t rb = VREG_GET_B(i), rc2 = VREG_GET_C(i);
        RSTORE(VREG_GET_A(i), (vigil_nanbox_is_uint(R[rb]) || vigil_nanbox_is_uint(R[rc2]))
                                  ? vigil_nanbox_from_bool(regvm_decode_uint(R[rb]) >= regvm_decode_uint(R[rc2]))
                                  : vigil_nanbox_from_bool(regvm_decode_int(R[rb]) >= regvm_decode_int(R[rc2])));
        RNEXT();
    }
    RCASE(EQ_I64)
    {
        vigil_reg_instr_t i = code[ip];
        RSTORE(VREG_GET_A(i), vigil_nanbox_from_bool(regvm_decode_int(R[VREG_GET_B(i)]) ==
                                                     regvm_decode_int(R[VREG_GET_C(i)])));
        RNEXT();
    }
    RCASE(NE_I64)
    {
        vigil_reg_instr_t i = code[ip];
        RSTORE(VREG_GET_A(i), vigil_nanbox_from_bool(regvm_decode_int(R[VREG_GET_B(i)]) !=
                                                     regvm_decode_int(R[VREG_GET_C(i)])));
        RNEXT();
    }

    /* ── f64 arithmetic ────────────────────────────────────────── */
    RCASE(ADD_F64)
    {
        vigil_reg_instr_t i = code[ip];
        double a = vigil_nanbox_decode_double(R[VREG_GET_B(i)]);
        double b = vigil_nanbox_decode_double(R[VREG_GET_C(i)]);
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_double(a + b));
        RNEXT();
    }
    RCASE(SUB_F64)
    {
        vigil_reg_instr_t i = code[ip];
        double a = vigil_nanbox_decode_double(R[VREG_GET_B(i)]);
        double b = vigil_nanbox_decode_double(R[VREG_GET_C(i)]);
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_double(a - b));
        RNEXT();
    }
    RCASE(MUL_F64)
    {
        vigil_reg_instr_t i = code[ip];
        double a = vigil_nanbox_decode_double(R[VREG_GET_B(i)]);
        double b = vigil_nanbox_decode_double(R[VREG_GET_C(i)]);
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_double(a * b));
        RNEXT();
    }
    RCASE(DIV_F64)
    {
        vigil_reg_instr_t i = code[ip];
        double a = vigil_nanbox_decode_double(R[VREG_GET_B(i)]);
        double b = vigil_nanbox_decode_double(R[VREG_GET_C(i)]);
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_double(a / b));
        RNEXT();
    }

    /* ── Math intrinsics ───────────────────────────────────────── */
    RCASE(MATH_SIN)
    {
        vigil_reg_instr_t i = code[ip];
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_double(sin(vigil_nanbox_decode_double(R[VREG_GET_B(i)]))));
        RNEXT();
    }
    RCASE(MATH_COS)
    {
        vigil_reg_instr_t i = code[ip];
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_double(cos(vigil_nanbox_decode_double(R[VREG_GET_B(i)]))));
        RNEXT();
    }
    RCASE(MATH_SQRT)
    {
        vigil_reg_instr_t i = code[ip];
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_double(sqrt(vigil_nanbox_decode_double(R[VREG_GET_B(i)]))));
        RNEXT();
    }
    RCASE(MATH_LOG)
    {
        vigil_reg_instr_t i = code[ip];
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_double(log(vigil_nanbox_decode_double(R[VREG_GET_B(i)]))));
        RNEXT();
    }
    RCASE(MATH_POW)
    {
        vigil_reg_instr_t i = code[ip];
        double a = vigil_nanbox_decode_double(R[VREG_GET_B(i)]);
        double b = vigil_nanbox_decode_double(R[VREG_GET_C(i)]);
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_double(pow(a, b)));
        RNEXT();
    }

    /* ── Increment ─────────────────────────────────────────────── */
    RCASE(INC_I32)
    {
        vigil_reg_instr_t i = code[ip];
        int32_t v = vigil_nanbox_decode_i32(R[VREG_GET_A(i)]);
        int8_t delta = (int8_t)VREG_GET_B(i);
        int32_t r;
        if (VIGIL_UNLIKELY(VIGIL_I32_ADD_OVERFLOW(v, (int32_t)delta, &r)))
            goto r_overflow;
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_i32(r));
        RNEXT();
    }
    RCASE(INC_I64)
    {
        vigil_reg_instr_t i = code[ip];
        int64_t v = regvm_decode_int(R[VREG_GET_A(i)]);
        int64_t delta = (int64_t)(int8_t)VREG_GET_B(i);
        int64_t r;
        if (VIGIL_UNLIKELY(vigil_vm_checked_add(v, delta, &r) != VIGIL_STATUS_OK))
            goto r_overflow;
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_int(r));
        RNEXT();
    }

    /* ── Control flow ──────────────────────────────────────────── */
    RCASE(JMP)
    {
        vigil_reg_instr_t i = code[ip];
        int16_t off = VREG_GET_sBx(i);
        ip = (size_t)((int32_t)ip + 1 + (int32_t)off);
        RDISPATCH();
    }
    RCASE(TEST)
    {
        vigil_reg_instr_t i = code[ip];
        int cond = (R[VREG_GET_A(i)] != VIGIL_NANBOX_FALSE && R[VREG_GET_A(i)] != VIGIL_NANBOX_NIL);
        if (!cond)
        {
            /* Condition is false — execute next instruction (the jump). */
            ip++;
            RDISPATCH();
        }
        else
        {
            /* Condition is true — skip the jump. */
            ip += 2;
            RDISPATCH();
        }
    }

    /* ── Fused compare+jump (i32) ──────────────────────────────── */
    RCASE(LT_I32_JMP)
    {
        vigil_reg_instr_t i = code[ip];
        if (vigil_nanbox_decode_i32(R[VREG_GET_A(i)]) < vigil_nanbox_decode_i32(R[VREG_GET_B(i)]))
        {
            ip += 2;
            RDISPATCH(); /* skip jump */
        }
        ip++;
        RDISPATCH(); /* execute jump */
    }
    RCASE(LE_I32_JMP)
    {
        vigil_reg_instr_t i = code[ip];
        if (vigil_nanbox_decode_i32(R[VREG_GET_A(i)]) <= vigil_nanbox_decode_i32(R[VREG_GET_B(i)]))
        {
            ip += 2;
            RDISPATCH();
        }
        ip++;
        RDISPATCH();
    }
    RCASE(GT_I32_JMP)
    {
        vigil_reg_instr_t i = code[ip];
        if (vigil_nanbox_decode_i32(R[VREG_GET_A(i)]) > vigil_nanbox_decode_i32(R[VREG_GET_B(i)]))
        {
            ip += 2;
            RDISPATCH();
        }
        ip++;
        RDISPATCH();
    }
    RCASE(GE_I32_JMP)
    {
        vigil_reg_instr_t i = code[ip];
        if (vigil_nanbox_decode_i32(R[VREG_GET_A(i)]) >= vigil_nanbox_decode_i32(R[VREG_GET_B(i)]))
        {
            ip += 2;
            RDISPATCH();
        }
        ip++;
        RDISPATCH();
    }
    RCASE(EQ_I32_JMP)
    {
        vigil_reg_instr_t i = code[ip];
        if (vigil_nanbox_decode_i32(R[VREG_GET_A(i)]) == vigil_nanbox_decode_i32(R[VREG_GET_B(i)]))
        {
            ip += 2;
            RDISPATCH();
        }
        ip++;
        RDISPATCH();
    }
    RCASE(NE_I32_JMP)
    {
        vigil_reg_instr_t i = code[ip];
        if (vigil_nanbox_decode_i32(R[VREG_GET_A(i)]) != vigil_nanbox_decode_i32(R[VREG_GET_B(i)]))
        {
            ip += 2;
            RDISPATCH();
        }
        ip++;
        RDISPATCH();
    }

    /* ── Fused compare+jump (i64) ──────────────────────────────── */
    RCASE(LT_I64_JMP)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ra = VREG_GET_A(i), rb = VREG_GET_B(i);
        int cond = (vigil_nanbox_is_uint(R[ra]) || vigil_nanbox_is_uint(R[rb]))
            ? (regvm_decode_uint(R[ra]) < regvm_decode_uint(R[rb]))
            : (regvm_decode_int(R[ra]) < regvm_decode_int(R[rb]));
        if (cond) { ip += 2; RDISPATCH(); }
        ip++; RDISPATCH();
    }
    RCASE(LE_I64_JMP)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ra = VREG_GET_A(i), rb = VREG_GET_B(i);
        int cond = (vigil_nanbox_is_uint(R[ra]) || vigil_nanbox_is_uint(R[rb]))
            ? (regvm_decode_uint(R[ra]) <= regvm_decode_uint(R[rb]))
            : (regvm_decode_int(R[ra]) <= regvm_decode_int(R[rb]));
        if (cond) { ip += 2; RDISPATCH(); }
        ip++; RDISPATCH();
    }
    RCASE(GT_I64_JMP)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ra = VREG_GET_A(i), rb = VREG_GET_B(i);
        int cond = (vigil_nanbox_is_uint(R[ra]) || vigil_nanbox_is_uint(R[rb]))
            ? (regvm_decode_uint(R[ra]) > regvm_decode_uint(R[rb]))
            : (regvm_decode_int(R[ra]) > regvm_decode_int(R[rb]));
        if (cond) { ip += 2; RDISPATCH(); }
        ip++; RDISPATCH();
    }
    RCASE(GE_I64_JMP)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ra = VREG_GET_A(i), rb = VREG_GET_B(i);
        int cond = (vigil_nanbox_is_uint(R[ra]) || vigil_nanbox_is_uint(R[rb]))
            ? (regvm_decode_uint(R[ra]) >= regvm_decode_uint(R[rb]))
            : (regvm_decode_int(R[ra]) >= regvm_decode_int(R[rb]));
        if (cond) { ip += 2; RDISPATCH(); }
        ip++; RDISPATCH();
    }
    RCASE(EQ_I64_JMP)
    {
        vigil_reg_instr_t i = code[ip];
        if (regvm_decode_int(R[VREG_GET_A(i)]) == regvm_decode_int(R[VREG_GET_B(i)]))
        {
            ip += 2;
            RDISPATCH();
        }
        ip++;
        RDISPATCH();
    }
    RCASE(NE_I64_JMP)
    {
        vigil_reg_instr_t i = code[ip];
        if (regvm_decode_int(R[VREG_GET_A(i)]) != regvm_decode_int(R[VREG_GET_B(i)]))
        {
            ip += 2;
            RDISPATCH();
        }
        ip++;
        RDISPATCH();
    }

    /* ── FORLOOP ───────────────────────────────────────────────── */
    RCASE(FORLOOP_I32)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t idx = VREG_GET_A(i);
        int8_t delta = (int8_t)VREG_GET_B(i);
        uint8_t cmp = VREG_GET_C(i);
        /* Next word: constant index for limit. */
        vigil_reg_instr_t i2 = code[ip + 1];
        uint16_t ci = VREG_GET_Bx(i2);
        const vigil_value_t *kv = VIGIL_VM_CHUNK_CONSTANT(sc, (size_t)ci);
        int32_t limit = vigil_nanbox_decode_i32(*kv);
        int32_t val = vigil_nanbox_decode_i32(R[idx]);
        int32_t r;
        if (VIGIL_UNLIKELY(VIGIL_I32_ADD_OVERFLOW(val, (int32_t)delta, &r)))
            goto r_overflow;
        RSTORE(idx, vigil_nanbox_encode_i32(r));
        int cont = 0;
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
        }
        if (cont)
        {
            /* Execute the back-jump (third word). */
            ip += 2;
            RDISPATCH();
        }
        /* Fall through — skip the back-jump. */
        ip += 3;
        RDISPATCH();
    }
    RCASE(FORLOOP_I64)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t idx = VREG_GET_A(i);
        int64_t delta = (int64_t)(int8_t)VREG_GET_B(i);
        uint8_t cmp = VREG_GET_C(i);
        vigil_reg_instr_t i2 = code[ip + 1];
        uint16_t ci = VREG_GET_Bx(i2);
        const vigil_value_t *kv = VIGIL_VM_CHUNK_CONSTANT(sc, (size_t)ci);
        int64_t limit = regvm_decode_int(*kv);
        int64_t val = regvm_decode_int(R[idx]);
        int64_t r;
        if (VIGIL_UNLIKELY(vigil_vm_checked_add(val, delta, &r) != VIGIL_STATUS_OK))
            goto r_overflow;
        RSTORE(idx, vigil_nanbox_encode_int(r));
        int cont = 0;
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
        }
        if (cont)
        {
            ip += 2;
            RDISPATCH();
        }
        ip += 3;
        RDISPATCH();
    }

    /* ── Generic ops — implemented via existing VM helpers ────── */
    RCASE(ADD)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ra = VREG_GET_B(i), rb = VREG_GET_C(i), rd = VREG_GET_A(i);
        if (vigil_nanbox_is_uint(R[ra]) || vigil_nanbox_is_uint(R[rb]))
        {
            uint64_t a = regvm_decode_uint(R[ra]), b = regvm_decode_uint(R[rb]), r;
            if (VIGIL_LIKELY(vigil_vm_checked_uadd(a, b, &r) == VIGIL_STATUS_OK))
            { RRELEASE(rd); R[rd] = regvm_encode_uint(r); has_reg_objects = 1; RNEXT(); }
            goto r_overflow;
        }
        if (vigil_nanbox_is_int(R[ra]) && vigil_nanbox_is_int(R[rb]))
        {
            int64_t a = regvm_decode_int(R[ra]), b = regvm_decode_int(R[rb]), r;
            if (VIGIL_LIKELY(vigil_vm_checked_add(a, b, &r) == VIGIL_STATUS_OK))
            { RRELEASE(rd); R[rd] = regvm_encode_int(r); has_reg_objects = 1; RNEXT(); }
            goto r_overflow;
        }
        /* Try f64 fast path. */
        if (vigil_nanbox_is_double(R[ra]) && vigil_nanbox_is_double(R[rb]))
        {
            RSTORE(rd, vigil_nanbox_encode_double(vigil_nanbox_decode_double(R[ra]) +
                                                 vigil_nanbox_decode_double(R[rb])));
            RNEXT();
        }
        /* String concatenation. */
        if (vigil_nanbox_is_object(R[ra]) && vigil_nanbox_is_object(R[rb]))
        {
            vigil_value_t result;
            status = vigil_vm_concat_strings(vm, &R[ra], &R[rb], &result, error);
            if (status == VIGIL_STATUS_OK)
            {
                RRELEASE(rd);
                R[rd] = result;
                has_reg_objects = 1;
                RNEXT();
            }
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }
    RCASE(SUB)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ra = VREG_GET_B(i), rb = VREG_GET_C(i), rd = VREG_GET_A(i);
        if (vigil_nanbox_is_uint(R[ra]) || vigil_nanbox_is_uint(R[rb]))
        {
            uint64_t a = regvm_decode_uint(R[ra]), b = regvm_decode_uint(R[rb]), r;
            if (VIGIL_LIKELY(vigil_vm_checked_usubtract(a, b, &r) == VIGIL_STATUS_OK))
            { RRELEASE(rd); R[rd] = regvm_encode_uint(r); has_reg_objects = 1; RNEXT(); }
            goto r_overflow;
        }
        if (vigil_nanbox_is_int(R[ra]) && vigil_nanbox_is_int(R[rb]))
        {
            int64_t a = regvm_decode_int(R[ra]), b = regvm_decode_int(R[rb]), r;
            if (VIGIL_LIKELY(vigil_vm_checked_subtract(a, b, &r) == VIGIL_STATUS_OK))
            { RRELEASE(rd); R[rd] = regvm_encode_int(r); has_reg_objects = 1; RNEXT(); }
            goto r_overflow;
        }
        if (vigil_nanbox_is_double(R[ra]) && vigil_nanbox_is_double(R[rb]))
        {
            RSTORE(rd, vigil_nanbox_encode_double(vigil_nanbox_decode_double(R[ra]) -
                                                 vigil_nanbox_decode_double(R[rb])));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }
    RCASE(MUL)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ra = VREG_GET_B(i), rb = VREG_GET_C(i), rd = VREG_GET_A(i);
        if (vigil_nanbox_is_uint(R[ra]) || vigil_nanbox_is_uint(R[rb]))
        {
            uint64_t a = regvm_decode_uint(R[ra]), b = regvm_decode_uint(R[rb]), r;
            if (VIGIL_LIKELY(vigil_vm_checked_umultiply(a, b, &r) == VIGIL_STATUS_OK))
            { RRELEASE(rd); R[rd] = regvm_encode_uint(r); has_reg_objects = 1; RNEXT(); }
            goto r_overflow;
        }
        if (vigil_nanbox_is_int(R[ra]) && vigil_nanbox_is_int(R[rb]))
        {
            int64_t a = regvm_decode_int(R[ra]), b = regvm_decode_int(R[rb]), r;
            if (VIGIL_LIKELY(vigil_vm_checked_multiply(a, b, &r) == VIGIL_STATUS_OK))
            { RRELEASE(rd); R[rd] = regvm_encode_int(r); has_reg_objects = 1; RNEXT(); }
            goto r_overflow;
        }
        if (vigil_nanbox_is_double(R[ra]) && vigil_nanbox_is_double(R[rb]))
        {
            RSTORE(rd, vigil_nanbox_encode_double(vigil_nanbox_decode_double(R[ra]) *
                                                 vigil_nanbox_decode_double(R[rb])));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }
    RCASE(DIV)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ra = VREG_GET_B(i), rb = VREG_GET_C(i), rd = VREG_GET_A(i);
        if (vigil_nanbox_is_uint(R[ra]) || vigil_nanbox_is_uint(R[rb]))
        {
            uint64_t a = regvm_decode_uint(R[ra]), b = regvm_decode_uint(R[rb]), r;
            if (VIGIL_UNLIKELY(b == 0)) goto r_divzero;
            if (VIGIL_LIKELY(vigil_vm_checked_udivide(a, b, &r) == VIGIL_STATUS_OK))
            { RRELEASE(rd); R[rd] = regvm_encode_uint(r); has_reg_objects = 1; RNEXT(); }
            goto r_overflow;
        }
        if (vigil_nanbox_is_int(R[ra]) && vigil_nanbox_is_int(R[rb]))
        {
            int64_t a = regvm_decode_int(R[ra]), b = regvm_decode_int(R[rb]), r;
            if (VIGIL_UNLIKELY(b == 0)) goto r_divzero;
            if (VIGIL_LIKELY(vigil_vm_checked_divide(a, b, &r) == VIGIL_STATUS_OK))
            { RRELEASE(rd); R[rd] = regvm_encode_int(r); has_reg_objects = 1; RNEXT(); }
            goto r_overflow;
        }
        if (vigil_nanbox_is_double(R[ra]) && vigil_nanbox_is_double(R[rb]))
        {
            RSTORE(rd, vigil_nanbox_encode_double(vigil_nanbox_decode_double(R[ra]) /
                                                 vigil_nanbox_decode_double(R[rb])));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }
    RCASE(MOD)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ra = VREG_GET_B(i), rb = VREG_GET_C(i), rd = VREG_GET_A(i);
        if (vigil_nanbox_is_uint(R[ra]) || vigil_nanbox_is_uint(R[rb]))
        {
            uint64_t a = regvm_decode_uint(R[ra]), b = regvm_decode_uint(R[rb]), r;
            if (VIGIL_UNLIKELY(b == 0)) goto r_divzero;
            if (VIGIL_LIKELY(vigil_vm_checked_umodulo(a, b, &r) == VIGIL_STATUS_OK))
            { RRELEASE(rd); R[rd] = regvm_encode_uint(r); has_reg_objects = 1; RNEXT(); }
            goto r_overflow;
        }
        if (vigil_nanbox_is_int(R[ra]) && vigil_nanbox_is_int(R[rb]))
        {
            int64_t a = regvm_decode_int(R[ra]), b = regvm_decode_int(R[rb]), r;
            if (VIGIL_UNLIKELY(b == 0)) goto r_divzero;
            if (VIGIL_LIKELY(vigil_vm_checked_modulo(a, b, &r) == VIGIL_STATUS_OK))
            { RRELEASE(rd); R[rd] = regvm_encode_int(r); has_reg_objects = 1; RNEXT(); }
            goto r_overflow;
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }
    RCASE(EQ)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t dst = VREG_GET_A(i);
        vigil_value_t res = vigil_nanbox_from_bool(vigil_vm_values_equal(&R[VREG_GET_B(i)], &R[VREG_GET_C(i)]));
        RRELEASE(dst);
        R[dst] = res;
        RNEXT();
    }
    RCASE(LT)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ra = VREG_GET_B(i), rb = VREG_GET_C(i);
        int compared;
        if (vigil_nanbox_is_uint(R[ra]) || vigil_nanbox_is_uint(R[rb]))
        {
            RSTORE(VREG_GET_A(i), vigil_nanbox_from_bool(regvm_decode_uint(R[ra]) < regvm_decode_uint(R[rb])));
            RNEXT();
        }
        if (vigil_nanbox_is_int(R[ra]) && vigil_nanbox_is_int(R[rb]))
        {
            RSTORE(VREG_GET_A(i), vigil_nanbox_from_bool(regvm_decode_int(R[ra]) < regvm_decode_int(R[rb])));
            RNEXT();
        }
        if (vigil_nanbox_is_double(R[ra]) && vigil_nanbox_is_double(R[rb]))
        {
            RSTORE(VREG_GET_A(i), vigil_nanbox_from_bool(vigil_nanbox_decode_double(R[ra]) <
                                                         vigil_nanbox_decode_double(R[rb])));
            RNEXT();
        }
        if (regvm_compare_strings(R[ra], R[rb], &compared))
        {
            RSTORE(VREG_GET_A(i), vigil_nanbox_from_bool(compared < 0));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }
    RCASE(LE)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ra = VREG_GET_B(i), rb = VREG_GET_C(i);
        int compared;
        if (vigil_nanbox_is_uint(R[ra]) || vigil_nanbox_is_uint(R[rb]))
        {
            RSTORE(VREG_GET_A(i), vigil_nanbox_from_bool(regvm_decode_uint(R[ra]) <= regvm_decode_uint(R[rb])));
            RNEXT();
        }
        if (vigil_nanbox_is_int(R[ra]) && vigil_nanbox_is_int(R[rb]))
        {
            RSTORE(VREG_GET_A(i), vigil_nanbox_from_bool(regvm_decode_int(R[ra]) <= regvm_decode_int(R[rb])));
            RNEXT();
        }
        if (vigil_nanbox_is_double(R[ra]) && vigil_nanbox_is_double(R[rb]))
        {
            RSTORE(VREG_GET_A(i), vigil_nanbox_from_bool(vigil_nanbox_decode_double(R[ra]) <=
                                                         vigil_nanbox_decode_double(R[rb])));
            RNEXT();
        }
        if (regvm_compare_strings(R[ra], R[rb], &compared))
        {
            RSTORE(VREG_GET_A(i), vigil_nanbox_from_bool(compared <= 0));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }

    /* ── Unary ops ─────────────────────────────────────────────── */
    RCASE(NEG)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t src = VREG_GET_B(i);
        if (vigil_nanbox_is_int(R[src]))
        {
            int64_t v = regvm_decode_int(R[src]);
            int64_t r;
            if (VIGIL_LIKELY(vigil_vm_checked_negate(v, &r) == VIGIL_STATUS_OK))
            {
                RSTORE(VREG_GET_A(i), regvm_encode_int(r));
                RNEXT();
            }
            goto r_overflow;
        }
        if (vigil_nanbox_is_double(R[src]))
        {
            RSTORE(VREG_GET_A(i), vigil_nanbox_encode_double(-vigil_nanbox_decode_double(R[src])));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }
    RCASE(NOT)
    {
        vigil_reg_instr_t i = code[ip];
        RSTORE(VREG_GET_A(i),
               vigil_nanbox_from_bool(R[VREG_GET_B(i)] == VIGIL_NANBOX_FALSE || R[VREG_GET_B(i)] == VIGIL_NANBOX_NIL));
        RNEXT();
    }
    RCASE(BNOT)
    {
        vigil_reg_instr_t i = code[ip];
        if (vigil_nanbox_is_int(R[VREG_GET_B(i)]))
        {
            RSTORE(VREG_GET_A(i), vigil_nanbox_encode_int(~regvm_decode_int(R[VREG_GET_B(i)])));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }

    /* ── Bitwise ops ───────────────────────────────────────────── */
    RCASE(BAND)
    {
        vigil_reg_instr_t i = code[ip];
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_int(regvm_decode_int(R[VREG_GET_B(i)]) &
                                                      regvm_decode_int(R[VREG_GET_C(i)])));
        RNEXT();
    }
    RCASE(BOR)
    {
        vigil_reg_instr_t i = code[ip];
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_int(regvm_decode_int(R[VREG_GET_B(i)]) |
                                                      regvm_decode_int(R[VREG_GET_C(i)])));
        RNEXT();
    }
    RCASE(BXOR)
    {
        vigil_reg_instr_t i = code[ip];
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_int(regvm_decode_int(R[VREG_GET_B(i)]) ^
                                                      regvm_decode_int(R[VREG_GET_C(i)])));
        RNEXT();
    }
    RCASE(SHL)
    {
        vigil_reg_instr_t i = code[ip];
        if (vigil_nanbox_is_uint(R[VREG_GET_B(i)]) || vigil_nanbox_is_uint(R[VREG_GET_C(i)]))
        {
            uint64_t a = regvm_decode_uint(R[VREG_GET_B(i)]);
            uint64_t b = regvm_decode_uint(R[VREG_GET_C(i)]);
            uint64_t r;
            if (VIGIL_UNLIKELY(vigil_vm_checked_ushift_left(a, b, &r) != VIGIL_STATUS_OK))
                goto r_overflow;
            RSTORE(VREG_GET_A(i), regvm_encode_uint(r));
            RNEXT();
        }
        int64_t a = regvm_decode_int(R[VREG_GET_B(i)]);
        int64_t b = regvm_decode_int(R[VREG_GET_C(i)]);
        int64_t r;
        if (VIGIL_UNLIKELY(vigil_vm_checked_shift_left(a, b, &r) != VIGIL_STATUS_OK))
            goto r_overflow;
        RSTORE(VREG_GET_A(i), regvm_encode_int(r));
        RNEXT();
    }
    RCASE(SHR)
    {
        vigil_reg_instr_t i = code[ip];
        if (vigil_nanbox_is_uint(R[VREG_GET_B(i)]) || vigil_nanbox_is_uint(R[VREG_GET_C(i)]))
        {
            uint64_t a = regvm_decode_uint(R[VREG_GET_B(i)]);
            uint64_t b = regvm_decode_uint(R[VREG_GET_C(i)]);
            uint64_t r;
            if (VIGIL_UNLIKELY(vigil_vm_checked_ushift_right(a, b, &r) != VIGIL_STATUS_OK))
                goto r_overflow;
            RSTORE(VREG_GET_A(i), vigil_nanbox_encode_uint(r));
            RNEXT();
        }
        int64_t a = regvm_decode_int(R[VREG_GET_B(i)]);
        int64_t b = regvm_decode_int(R[VREG_GET_C(i)]);
        int64_t r;
        if (VIGIL_UNLIKELY(vigil_vm_checked_shift_right(a, b, &r) != VIGIL_STATUS_OK))
            goto r_overflow;
        RSTORE(VREG_GET_A(i), vigil_nanbox_encode_int(r));
        RNEXT();
    }

    /* ── Type conversions ──────────────────────────────────────── */
    RCASE(TO_I32)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t src = VREG_GET_B(i);
        if (vigil_nanbox_is_int(R[src]))
        {
            int64_t v = regvm_decode_int(R[src]);
            RSTORE(VREG_GET_A(i), vigil_nanbox_encode_i32((int32_t)v));
            RNEXT();
        }
        if (vigil_nanbox_is_uint(R[src]))
        {
            uint64_t v = regvm_decode_uint(R[src]);
            RSTORE(VREG_GET_A(i), vigil_nanbox_encode_i32((int32_t)v));
            RNEXT();
        }
        if (vigil_nanbox_is_double(R[src]))
        {
            RSTORE(VREG_GET_A(i), vigil_nanbox_encode_i32((int32_t)vigil_nanbox_decode_double(R[src])));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }
    RCASE(TO_I64)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t src = VREG_GET_B(i);
        uint8_t dst = VREG_GET_A(i);
        if (vigil_nanbox_is_int(R[src]))
        {
            if (dst != src)
            {
                RRELEASE(dst);
                if (vigil_nanbox_has_object(R[src]))
                    VIGIL_VM_VALUE_COPY(&R[dst], &R[src]);
                else
                    R[dst] = R[src];
            }
            RNEXT();
        }
        if (vigil_nanbox_is_uint(R[src]))
        {
            RSTORE(dst, vigil_nanbox_encode_int((int64_t)regvm_decode_uint(R[src])));
            RNEXT();
        }
        if (vigil_nanbox_is_double(R[src]))
        {
            RSTORE(dst, vigil_nanbox_encode_int((int64_t)vigil_nanbox_decode_double(R[src])));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }
    RCASE(TO_F64)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t src = VREG_GET_B(i);
        if (vigil_nanbox_is_double(R[src]))
        {
            RSTORE(VREG_GET_A(i), R[src]);
            RNEXT();
        }
        if (vigil_nanbox_is_int(R[src]))
        {
            RSTORE(VREG_GET_A(i), vigil_nanbox_encode_double((double)regvm_decode_int(R[src])));
            RNEXT();
        }
        if (vigil_nanbox_is_uint(R[src]))
        {
            RSTORE(VREG_GET_A(i), vigil_nanbox_encode_double((double)regvm_decode_uint(R[src])));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }

    /* ── TO_STRING ─────────────────────────────────────────────── */
    RCASE(TO_STRING)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t src = VREG_GET_B(i);
        uint8_t dst = VREG_GET_A(i);
        vigil_value_t str_val;
        status = vigil_vm_stringify_value(vm, &R[src], &str_val, error);
        if (status != VIGIL_STATUS_OK)
        {
            status = VIGIL_STATUS_INVALID_ARGUMENT;
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            goto r_cleanup;
        }
        RRELEASE(dst);
        R[dst] = str_val;
        has_reg_objects = 1;
        RNEXT();
    }

    /* ── Globals ────────────────────────────────────────────────── */
    RCASE(GET_GLOBAL)
    {
        vigil_reg_instr_t i = code[ip];
        vigil_value_t gval;
        VIGIL_VM_VALUE_INIT_NIL(&gval);
        if (!vigil_function_object_get_global(frame->function, (size_t)VREG_GET_Bx(i), &gval))
        {
            status = VIGIL_STATUS_INTERNAL;
            goto r_cleanup;
        }
        RRELEASE(VREG_GET_A(i));
        R[VREG_GET_A(i)] = gval;
        if (vigil_nanbox_has_object(gval))
            has_reg_objects = 1;
        RNEXT();
    }
    RCASE(SET_GLOBAL)
    {
        vigil_reg_instr_t i = code[ip];
        status = vigil_function_object_set_global(frame->function, (size_t)VREG_GET_Bx(i), &R[VREG_GET_A(i)], error);
        if (status != VIGIL_STATUS_OK)
            goto r_cleanup;
        RNEXT();
    }

    /* ── Captures ──────────────────────────────────────────────── */
    RCASE(GET_CAPTURE)
    {
        vigil_reg_instr_t i = code[ip];
        vigil_value_t cval;
        VIGIL_VM_VALUE_INIT_NIL(&cval);
        if (frame->callable && vigil_object_type(frame->callable) == VIGIL_OBJECT_CLOSURE)
        {
            vigil_closure_object_get_capture((vigil_object_t *)frame->callable, (size_t)VREG_GET_Bx(i), &cval);
        }
        RRELEASE(VREG_GET_A(i));
        R[VREG_GET_A(i)] = cval;
        if (vigil_nanbox_has_object(cval))
            has_reg_objects = 1;
        RNEXT();
    }
    RCASE(SET_CAPTURE)
    {
        vigil_reg_instr_t i = code[ip];
        if (frame->callable && vigil_object_type(frame->callable) == VIGIL_OBJECT_CLOSURE)
        {
            status = vigil_closure_object_set_capture((vigil_object_t *)frame->callable, (size_t)VREG_GET_Bx(i),
                                                      &R[VREG_GET_A(i)], error);
            if (status != VIGIL_STATUS_OK)
                goto r_cleanup;
        }
        RNEXT();
    }

    /* ── Functions and closures ─────────────────────────────────── */
    RCASE(GET_FUNCTION)
    {
        vigil_reg_instr_t i = code[ip];
        const vigil_object_t *fn = vigil_vm_function_sibling(frame->function, (size_t)VREG_GET_Bx(i));
        if (!fn)
        {
            status = VIGIL_STATUS_INTERNAL;
            goto r_cleanup;
        }
        vigil_object_retain((vigil_object_t *)fn);
        RRELEASE(VREG_GET_A(i)); has_reg_objects = 1; vigil_value_init_object(&R[VREG_GET_A(i)], (vigil_object_t **)&fn);
        RNEXT();
    }
    RCASE(NEW_CLOSURE)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t func_idx = VREG_GET_B(i);
        uint8_t cap_count = VREG_GET_C(i);
        const vigil_object_t *fn = vigil_vm_function_sibling(frame->function, (size_t)func_idx);
        if (!fn)
        {
            status = VIGIL_STATUS_INTERNAL;
            goto r_cleanup;
        }
        /* Captures are on the stack at positions A .. A+cap_count-1.
           Sync stack_count to point past them. */
        vm->stack_count = base + (size_t)VREG_GET_A(i) + (size_t)cap_count;
        vigil_object_t *closure = NULL;
        status = vigil_closure_object_new(vm->runtime, (vigil_object_t *)fn, &vm->stack[base + (size_t)VREG_GET_A(i)],
                                          (size_t)cap_count, &closure, error);
        if (status != VIGIL_STATUS_OK)
            goto r_cleanup;
        RRELEASE(VREG_GET_A(i)); has_reg_objects = 1; vigil_value_init_object(&R[VREG_GET_A(i)], &closure);
        RNEXT();
    }

    /* ── Function call ──────────────────────────────────────────── */

#define REGVM_ISOLATE_CALL(arg_base_var, arg_count_val)                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        size_t _min = base + rc->max_registers;                                                                        \
        if ((arg_base_var) < _min)                                                                                     \
        {                                                                                                              \
            size_t _need = _min + (size_t)(arg_count_val) + 16;                                                        \
            if (vm->stack_capacity < _need)                                                                            \
            {                                                                                                          \
                status = vigil_vm_grow_stack(vm, _need, error);                                                        \
                if (status != VIGIL_STATUS_OK) goto r_cleanup;                                                         \
                R = vm->stack + base;                                                                                  \
            }                                                                                                          \
            memmove(&vm->stack[_min], &vm->stack[(arg_base_var)],                                                      \
                    (size_t)(arg_count_val) * sizeof(vigil_value_t));                                                   \
            /* Retain objects in the isolated copy so the callee's pop                                                 \
               does not free them while the caller still holds refs                                                    \
               in the original register slots. */                                                                      \
            for (size_t _ci = 0; _ci < (size_t)(arg_count_val); _ci++)                                                 \
            {                                                                                                          \
                if (vigil_nanbox_has_object(vm->stack[_min + _ci]))                                                    \
                    vigil_object_retain(                                                                                \
                        (vigil_object_t *)vigil_nanbox_decode_ptr(vm->stack[_min + _ci]));                              \
            }                                                                                                          \
            (arg_base_var) = _min;                                                                                     \
        }                                                                                                              \
    } while (0)

    RCASE(CALL)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t arg_base_r = VREG_GET_A(i);
        uint8_t func_idx = VREG_GET_B(i);
        uint8_t arg_count = VREG_GET_C(i);
        size_t arg_base = base + (size_t)arg_base_r;
        const vigil_object_t *callee = vigil_vm_function_sibling(frame->function, (size_t)func_idx);
        if (VIGIL_UNLIKELY(!callee))
        { status = VIGIL_STATUS_INTERNAL; goto r_cleanup; }
        size_t saved_ip = frame->ip;
        frame->ip = VIGIL_VM_CHUNK_CODE_SIZE(frame->chunk);
        vm->stack_count = arg_base + (size_t)arg_count;
        status = vigil_vm_execute_call(vm, callee, (size_t)arg_count, error);
        frame = &vm->frames[vm->frame_count - 1];
        frame->ip = saved_ip;
        R = vm->stack + base;
        if (status != VIGIL_STATUS_OK)
            goto r_cleanup;
        if (vm->stack_count < base + rc->max_registers)
            vm->stack_count = base + rc->max_registers;
        RNEXT();
    }

    /* ── Native call ───────────────────────────────────────────── */
    RCASE(CALL_NATIVE)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t arg_base_r = VREG_GET_A(i);
        uint8_t arg_count = VREG_GET_C(i);
        uint32_t ci;

        if (VIGIL_UNLIKELY(ip + 1U >= code_count))
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "regvm native call missing constant operand");
            status = VIGIL_STATUS_INTERNAL;
            goto r_cleanup;
        }
        ci = code[ip + 1U];
        size_t arg_base = base + (size_t)arg_base_r;
        vm->stack_count = arg_base + (size_t)arg_count;

        const vigil_value_t *native_val = VIGIL_VM_CHUNK_CONSTANT(sc, (size_t)ci);
        if (VIGIL_UNLIKELY(!native_val || !vigil_nanbox_has_object(*native_val)))
        { status = VIGIL_STATUS_UNSUPPORTED; goto r_cleanup; }
        vigil_native_fn_t native_fn = vigil_native_function_get(
            (vigil_object_t *)vigil_nanbox_decode_ptr(*native_val));
        if (VIGIL_UNLIKELY(!native_fn))
        { status = VIGIL_STATUS_UNSUPPORTED; goto r_cleanup; }

        status = native_fn(vm, (size_t)arg_count, error);
        if (VIGIL_UNLIKELY(status != VIGIL_STATUS_OK))
            goto r_cleanup;

        R = vm->stack + base;
        has_reg_objects = 1;
        /* Return values are at stack[arg_base .. stack_count-1].
           They're already in the caller's register window. */
        if (vm->stack_count < base + rc->max_registers)
            vm->stack_count = base + rc->max_registers;

        ip += 1U;
        RNEXT();
    }

    /* ── Return ────────────────────────────────────────────────── */
    /* ── Stack-sync macro for calling vm_ops helpers ──────────── */
    /* The vm_ops helpers pop from vm->stack and push results back.
       Since R = vm->stack + base, we sync stack_count before calling
       and refresh R afterwards (stack may have grown). */
#define REGVM_SYNC_PRE(top_reg) vm->stack_count = base + (size_t)(top_reg) + 1U
#define REGVM_SYNC_POST()                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        R = vm->stack + base;                                                                                          \
        has_reg_objects = 1;                                                                                           \
        if (vm->stack_count < base + rc->max_registers)                                                                \
            vm->stack_count = base + rc->max_registers;                                                                \
    } while (0)
#define REGVM_STACK_HELPER(top_reg, pop_count, dst_reg, ret_count, call_expr)                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        uint8_t _sync_top = (uint8_t)(top_reg);                                                                        \
        REGVM_SYNC_PRE(_sync_top);                                                                                     \
        frame->ip = 0;                                                                                                 \
        status = (call_expr);                                                                                          \
        if (status != VIGIL_STATUS_OK)                                                                                 \
            goto r_cleanup;                                                                                            \
        regvm_move_helper_results(vm, base, regvm_helper_result_base(_sync_top, (uint8_t)(pop_count)),               \
                                  (uint8_t)(dst_reg), (uint8_t)(ret_count));                                           \
        REGVM_SYNC_POST();                                                                                             \
        RNEXT();                                                                                                       \
    } while (0)

    /* ── Type conversions (unsigned) ───────────────────────────── */
    RCASE(TO_U8)
    {
        vigil_reg_instr_t i = code[ip];
        REGVM_STACK_HELPER(VREG_GET_B(i), 1, VREG_GET_A(i), 1, vigil_vm_op_to_u8(vm, frame, error));
    }
    RCASE(TO_U32)
    {
        vigil_reg_instr_t i = code[ip];
        REGVM_STACK_HELPER(VREG_GET_B(i), 1, VREG_GET_A(i), 1, vigil_vm_op_to_u32(vm, frame, error));
    }
    RCASE(TO_U64)
    {
        vigil_reg_instr_t i = code[ip];
        REGVM_STACK_HELPER(VREG_GET_B(i), 1, VREG_GET_A(i), 1, vigil_vm_op_to_u64(vm, frame, error));
    }

    /* ── TESTSET ───────────────────────────────────────────────── */
    RCASE(TESTSET)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t a = VREG_GET_A(i);
        uint8_t b = VREG_GET_B(i);
        uint8_t c = VREG_GET_C(i);
        int falsey = (R[b] == VIGIL_NANBOX_NIL || R[b] == VIGIL_NANBOX_FALSE);
        if (c == 0 ? !falsey : falsey)
        {
            ip += 2; /* skip next instruction */
        }
        else
        {
            RRELEASE(a);
            VIGIL_VM_VALUE_COPY(&R[a], &R[b]);
            ip++;
        }
        RDISPATCH();
    }

    /* ── Object/collection ops ─────────────────────────────────── */
    RCASE(GET_INDEX)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t top = VREG_GET_B(i) > VREG_GET_C(i) ? VREG_GET_B(i) : VREG_GET_C(i);
        REGVM_STACK_HELPER(top, 2, VREG_GET_A(i), 1, vigil_vm_op_get_index(vm, frame, error));
    }
    RCASE(SET_INDEX)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t top = VREG_GET_A(i);
        if (VREG_GET_B(i) > top) top = VREG_GET_B(i);
        if (VREG_GET_C(i) > top) top = VREG_GET_C(i);
        REGVM_SYNC_PRE(top);
        frame->ip = 0;
        status = vigil_vm_op_set_index(vm, frame, error);
        if (status != VIGIL_STATUS_OK) goto r_cleanup;
        REGVM_SYNC_POST();
        RNEXT();
    }
    RCASE(COLLECTION_SIZE)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t c = VREG_GET_C(i);
        if (c == 1)
            REGVM_STACK_HELPER(VREG_GET_B(i), 1, VREG_GET_A(i), 1, vigil_vm_op_get_string_size(vm, frame, error));
        else
            REGVM_STACK_HELPER(VREG_GET_B(i), 1, VREG_GET_A(i), 1,
                               vigil_vm_op_get_collection_size(vm, frame, error));
    }
    RCASE(GET_FIELD)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t obj = VREG_GET_B(i);
        uint8_t fi = VREG_GET_C(i);
        vigil_object_t *o;
        vigil_value_t fv = {0};
        if (!vigil_nanbox_is_object(R[obj]))
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "field access requires a class instance");
            status = VIGIL_STATUS_INVALID_ARGUMENT;
            goto r_cleanup;
        }
        has_reg_objects = 1;
        o = (vigil_object_t *)vigil_nanbox_decode_ptr(R[obj]);
        if (!vigil_instance_object_get_field(o, (size_t)fi, &fv))
        { status = VIGIL_STATUS_INVALID_ARGUMENT; goto r_cleanup; }
        RRELEASE(VREG_GET_A(i));
        R[VREG_GET_A(i)] = fv;
        RNEXT();
    }
    RCASE(SET_FIELD)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t obj_r = VREG_GET_A(i);
        uint8_t fi = VREG_GET_B(i);
        uint8_t val_r = VREG_GET_C(i);
        vigil_object_t *o;
        if (!vigil_nanbox_is_object(R[obj_r]))
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "field assignment requires a class instance");
            status = VIGIL_STATUS_INVALID_ARGUMENT;
            goto r_cleanup;
        }
        has_reg_objects = 1;
        o = (vigil_object_t *)vigil_nanbox_decode_ptr(R[obj_r]);
        status = vigil_instance_object_set_field(o, (size_t)fi, &R[val_r], error);
        if (status != VIGIL_STATUS_OK) goto r_cleanup;
        RNEXT();
    }
    RCASE(NEW_INSTANCE)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t dest = VREG_GET_A(i);
        uint16_t ci = VREG_GET_Bx(i);
        vigil_reg_instr_t i2 = code[ip + 1];
        uint8_t fields_base = VREG_GET_A(i2);
        uint8_t field_count = VREG_GET_B(i2);
        vigil_object_t *inst = NULL;
        vigil_value_t *fields = (field_count > 0) ? &R[fields_base] : NULL;
        status = vigil_instance_object_new(vm->runtime, (size_t)ci, fields, (size_t)field_count, &inst, error);
        if (status != VIGIL_STATUS_OK) goto r_cleanup;
        RRELEASE(dest); has_reg_objects = 1; vigil_value_init_object(&R[dest], &inst);
        REGVM_SYNC_POST();
        ip++;
        RNEXT();
    }
    RCASE(NEW_ARRAY)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t dest = VREG_GET_A(i);
        uint16_t count = VREG_GET_Bx(i);
        vigil_object_t *arr = NULL;
        vigil_value_t *items = (count > 0) ? &R[dest] : NULL;
        status = vigil_array_object_new(vm->runtime, items, (size_t)count, &arr, error);
        if (status != VIGIL_STATUS_OK) goto r_cleanup;
        RRELEASE(dest); has_reg_objects = 1; vigil_value_init_object(&R[dest], &arr);
        REGVM_SYNC_POST();
        RNEXT();
    }
    RCASE(NEW_MAP)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t dest = VREG_GET_A(i);
        uint16_t pair_count = VREG_GET_Bx(i);
        vigil_object_t *map = NULL;
        status = vigil_map_object_new(vm->runtime, &map, error);
        if (status != VIGIL_STATUS_OK) goto r_cleanup;
        for (uint16_t idx = 0; idx < pair_count; idx++)
        {
            vigil_value_t *key = &R[dest + idx * 2];
            vigil_value_t *val = &R[dest + idx * 2 + 1];
            status = vigil_map_object_set(map, key, val, error);
            if (status != VIGIL_STATUS_OK) { vigil_object_release(&map); goto r_cleanup; }
        }
        RRELEASE(dest); has_reg_objects = 1; vigil_value_init_object(&R[dest], &map);
        REGVM_SYNC_POST();
        RNEXT();
    }

    /* ── Error ops ─────────────────────────────────────────────── */
    RCASE(NEW_ERROR)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t top = VREG_GET_B(i) > VREG_GET_C(i) ? VREG_GET_B(i) : VREG_GET_C(i);
        REGVM_STACK_HELPER(top, 2, VREG_GET_A(i), 1, vigil_vm_op_new_error(vm, frame, error));
    }
    RCASE(GET_ERROR_KIND)
    {
        vigil_reg_instr_t i = code[ip];
        REGVM_STACK_HELPER(VREG_GET_B(i), 1, VREG_GET_A(i), 1, vigil_vm_op_get_error_kind(vm, frame, error));
    }
    RCASE(GET_ERROR_MSG)
    {
        vigil_reg_instr_t i = code[ip];
        REGVM_STACK_HELPER(VREG_GET_B(i), 1, VREG_GET_A(i), 1, vigil_vm_op_get_error_message(vm, frame, error));
    }

    /* ── String ops ───────────────────────────────────────────── */
    RCASE(STRING_OP)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t str_r = VREG_GET_B(i);
        uint8_t sub_op = VREG_GET_C(i);
        uint8_t pop_count = 0;
        uint8_t ret_count = 0;
        /* The string op may need 1-3 args on the stack depending on the
           sub-opcode.  The translator places them in consecutive registers
           ending at str_r.  Sync past the highest. */
        REGVM_SYNC_PRE(str_r);
        frame->ip = 0;
        /* Dispatch to the appropriate string helper based on the original
           stack opcode stored in C. */
        switch (sub_op)
        {
        case VIGIL_OPCODE_STRING_CONTAINS:
        case VIGIL_OPCODE_STRING_STARTS_WITH:
        case VIGIL_OPCODE_STRING_ENDS_WITH:
            pop_count = 2;
            ret_count = 1;
            status = vigil_vm_op_string_search(vm, frame, (const uint8_t *)&sub_op, error);
            break;
        case VIGIL_OPCODE_STRING_REPLACE:
            pop_count = 3;
            ret_count = 1;
            status = vigil_vm_op_string_replace(vm, frame, error);
            break;
        case VIGIL_OPCODE_STRING_SPLIT:
            pop_count = 2;
            ret_count = 1;
            status = vigil_vm_op_string_split(vm, frame, error);
            break;
        case VIGIL_OPCODE_STRING_INDEX_OF:
            pop_count = 2;
            ret_count = 2;
            status = vigil_vm_op_string_index_of(vm, frame, error);
            break;
        case VIGIL_OPCODE_STRING_SUBSTR:
            pop_count = 3;
            ret_count = 2;
            status = vigil_vm_op_string_substr(vm, frame, error);
            break;
        case VIGIL_OPCODE_STRING_BYTES:
            pop_count = 1;
            ret_count = 1;
            status = vigil_vm_op_string_bytes(vm, frame, error);
            break;
        case VIGIL_OPCODE_STRING_CHAR_AT:
            pop_count = 2;
            ret_count = 2;
            status = vigil_vm_op_string_char_at(vm, frame, error);
            break;
        case VIGIL_OPCODE_STRING_TRIM:
        case VIGIL_OPCODE_STRING_TO_UPPER:
        case VIGIL_OPCODE_STRING_TO_LOWER:
            pop_count = 1;
            ret_count = 1;
            status = vigil_vm_op_string_transform(vm, frame, (const uint8_t *)&sub_op, error);
            break;
        case VIGIL_OPCODE_STRING_TRIM_LEFT:
        case VIGIL_OPCODE_STRING_TRIM_RIGHT:
            pop_count = 1;
            ret_count = 1;
            status = vigil_vm_op_string_trim_dir(vm, frame, (const uint8_t *)&sub_op, error);
            break;
        case VIGIL_OPCODE_STRING_REVERSE:
            pop_count = 1;
            ret_count = 1;
            status = vigil_vm_op_string_reverse(vm, frame, error);
            break;
        case VIGIL_OPCODE_STRING_IS_EMPTY:
            pop_count = 1;
            ret_count = 1;
            status = vigil_vm_op_string_is_empty(vm, frame, error);
            break;
        case VIGIL_OPCODE_STRING_CHAR_COUNT:
            pop_count = 1;
            ret_count = 1;
            status = vigil_vm_op_string_char_count(vm, frame, error);
            break;
        case VIGIL_OPCODE_STRING_REPEAT:
            pop_count = 2;
            ret_count = 1;
            status = vigil_vm_op_string_repeat(vm, frame, error);
            break;
        case VIGIL_OPCODE_STRING_COUNT:
            pop_count = 2;
            ret_count = 1;
            status = vigil_vm_op_string_count(vm, frame, error);
            break;
        case VIGIL_OPCODE_STRING_LAST_INDEX_OF:
            pop_count = 2;
            ret_count = 2;
            status = vigil_vm_op_string_last_index_of(vm, frame, error);
            break;
        case VIGIL_OPCODE_STRING_TRIM_PREFIX:
        case VIGIL_OPCODE_STRING_TRIM_SUFFIX:
            pop_count = 2;
            ret_count = 1;
            status = vigil_vm_op_string_trim_affix(vm, frame, (const uint8_t *)&sub_op, error);
            break;
        case VIGIL_OPCODE_STRING_TO_C:
            pop_count = 1;
            ret_count = 1;
            status = vigil_vm_op_string_to_c(vm, frame, error);
            break;
        case VIGIL_OPCODE_STRING_FIELDS:
            pop_count = 1;
            ret_count = 1;
            status = vigil_vm_op_string_fields(vm, frame, error);
            break;
        case VIGIL_OPCODE_STRING_EQUAL_FOLD:
            pop_count = 2;
            ret_count = 1;
            status = vigil_vm_op_string_equal_fold(vm, frame, error);
            break;
        case VIGIL_OPCODE_STRING_CUT:
            pop_count = 2;
            ret_count = 3;
            status = vigil_vm_op_string_cut(vm, frame, error);
            break;
        case VIGIL_OPCODE_STRING_JOIN:
            pop_count = 2;
            ret_count = 1;
            status = vigil_vm_op_string_join(vm, frame, error);
            break;
        default:
            status = VIGIL_STATUS_UNSUPPORTED;
            break;
        }
        if (status != VIGIL_STATUS_OK) goto r_cleanup;
        regvm_move_helper_results(vm, base, regvm_helper_result_base(str_r, pop_count), VREG_GET_A(i), ret_count);
        REGVM_SYNC_POST();
        RNEXT();
    }

    /* ── Format ops ────────────────────────────────────────────── */
    RCASE(FORMAT_F64)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t val_r = VREG_GET_B(i);
        uint8_t prec = VREG_GET_C(i);
        vigil_value_t result;
        VIGIL_VM_VALUE_INIT_NIL(&result);
        status = vigil_vm_format_f64_value(vm, &R[val_r], (uint32_t)prec, &result, error);
        if (status != VIGIL_STATUS_OK) {
            status = VIGIL_STATUS_INVALID_ARGUMENT;
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            goto r_cleanup;
        }
        RRELEASE(VREG_GET_A(i)); VIGIL_VM_VALUE_COPY(&R[VREG_GET_A(i)], &result); has_reg_objects = 1;
        VIGIL_VM_VALUE_RELEASE(&result);
        RNEXT();
    }
    RCASE(FORMAT_SPEC)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t val_r = VREG_GET_B(i);
        uint32_t w1 = code[ip + 1];
        uint32_t w2 = code[ip + 2];
        vigil_value_t result;
        VIGIL_VM_VALUE_INIT_NIL(&result);
        status = vigil_vm_format_spec_value(vm, &R[val_r], w1, w2, &result, error);
        if (status != VIGIL_STATUS_OK) {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "format specifier error");
            status = VIGIL_STATUS_INVALID_ARGUMENT;
            goto r_cleanup;
        }
        RRELEASE(VREG_GET_A(i)); VIGIL_VM_VALUE_COPY(&R[VREG_GET_A(i)], &result); has_reg_objects = 1;
        VIGIL_VM_VALUE_RELEASE(&result);
        ip += 2;
        RNEXT();
    }

    /* ── Parse intrinsics ──────────────────────────────────────── */
    RCASE(PARSE_I32)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t src = VREG_GET_B(i);
        uint8_t dst = VREG_GET_A(i);
        has_reg_objects = 1;
        vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(R[src]);
        const char *s = vigil_string_object_c_str(obj);
        if (s != NULL && *s != '\0')
        {
            char *end; errno = 0;
            long val = strtol(s, &end, 10);
            if (errno == 0 && end != s && *end == '\0' && val >= INT32_MIN && val <= INT32_MAX)
            {
                RSTORE(dst, vigil_nanbox_encode_int((int64_t)val));
                RSTORE((uint8_t)(dst + 1U), vigil_runtime_ok_error_value(vm->runtime));
                RNEXT();
            }
        }
        /* Error path: fall back to stack-based implementation. */
        REGVM_SYNC_PRE(src);
        vigil_vm_parse_i32(vm);
        regvm_move_helper_results(vm, base, regvm_helper_result_base(src, 1), dst, 2);
        REGVM_SYNC_POST();
        RNEXT();
    }
    RCASE(PARSE_F64)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t src = VREG_GET_B(i);
        uint8_t dst = VREG_GET_A(i);
        has_reg_objects = 1;
        vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(R[src]);
        const char *s = vigil_string_object_c_str(obj);
        if (s != NULL && *s != '\0')
        {
            char *end; errno = 0;
            double val = strtod(s, &end);
            if (errno == 0 && end != s && *end == '\0')
            {
                RSTORE(dst, vigil_nanbox_encode_double(val));
                RSTORE((uint8_t)(dst + 1U), vigil_runtime_ok_error_value(vm->runtime));
                RNEXT();
            }
        }
        REGVM_SYNC_PRE(src);
        vigil_vm_parse_f64(vm);
        regvm_move_helper_results(vm, base, regvm_helper_result_base(src, 1), dst, 2);
        REGVM_SYNC_POST();
        RNEXT();
    }
    RCASE(PARSE_BOOL)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t src = VREG_GET_B(i);
        uint8_t dst = VREG_GET_A(i);
        has_reg_objects = 1;
        vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(R[src]);
        const char *s = vigil_string_object_c_str(obj);
        size_t len = s ? vigil_string_object_length(obj) : 0;
        if (len == 4 && memcmp(s, "true", 4) == 0)
        {
            RSTORE(dst, VIGIL_NANBOX_TRUE);
            RSTORE((uint8_t)(dst + 1U), vigil_runtime_ok_error_value(vm->runtime));
            RNEXT();
        }
        if (len == 5 && memcmp(s, "false", 5) == 0)
        {
            RSTORE(dst, VIGIL_NANBOX_FALSE);
            RSTORE((uint8_t)(dst + 1U), vigil_runtime_ok_error_value(vm->runtime));
            RNEXT();
        }
        REGVM_SYNC_PRE(src);
        vigil_vm_parse_bool(vm);
        regvm_move_helper_results(vm, base, regvm_helper_result_base(src, 1), dst, 2);
        REGVM_SYNC_POST();
        RNEXT();
    }

    /* ── Defer ─────────────────────────────────────────────────── */
    RCASE(DEFER)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t defer_op = VREG_GET_A(i);
        uint8_t top_r = VREG_GET_C(i);
        uint32_t a = code[ip + 1];
        uint32_t b = code[ip + 2];
        vigil_vm_defer_kind_t kind;
        switch (defer_op)
        {
        case VIGIL_OPCODE_DEFER_CALL:           kind = VIGIL_VM_DEFER_CALL; break;
        case VIGIL_OPCODE_DEFER_CALL_VALUE:     kind = VIGIL_VM_DEFER_CALL_VALUE; break;
        case VIGIL_OPCODE_DEFER_NEW_INSTANCE:   kind = VIGIL_VM_DEFER_NEW_INSTANCE; break;
        case VIGIL_OPCODE_DEFER_CALL_INTERFACE: kind = VIGIL_VM_DEFER_CALL_INTERFACE; break;
        case VIGIL_OPCODE_DEFER_CALL_NATIVE:    kind = VIGIL_VM_DEFER_CALL_NATIVE; break;
        default: status = VIGIL_STATUS_UNSUPPORTED; goto r_cleanup;
        }
        /* Sync registers to stack so we can capture the values. */
        size_t val_count = (size_t)b;
        if (val_count > 0)
            REGVM_SYNC_PRE(top_r);
        vigil_value_t *vals = NULL;
        if (val_count > 0)
        {
            vals = malloc(val_count * sizeof(vigil_value_t));
            if (!vals) { status = VIGIL_STATUS_OUT_OF_MEMORY; goto r_cleanup; }
            for (size_t di = 0; di < val_count; di++)
            {
                vm->stack_count -= 1;
                VIGIL_VM_VALUE_COPY(&vals[val_count - 1 - di], &vm->stack[vm->stack_count]);
            }
        }
        /* Grow defer array if needed. */
        if (frame->defer_count >= frame->defer_capacity)
        {
            size_t new_cap = frame->defer_capacity < 4 ? 4 : frame->defer_capacity * 2;
            vigil_vm_defer_action_t *nd = realloc(frame->defers, new_cap * sizeof(*nd));
            if (!nd) { free(vals); status = VIGIL_STATUS_OUT_OF_MEMORY; goto r_cleanup; }
            frame->defers = nd;
            frame->defer_capacity = new_cap;
        }
        vigil_vm_defer_action_t *da = &frame->defers[frame->defer_count++];
        da->kind = kind;
        da->operand_a = a;
        da->operand_b = b;
        da->arg_count = (uint32_t)val_count;
        da->values = vals;
        da->value_count = val_count;
        if (kind == VIGIL_VM_DEFER_CALL_INTERFACE)
        {
            /* Third extra word is method_index; store in operand_b,
               move arg_count to arg_count field. */
            da->operand_b = code[ip + 3];
            ip += 4;
        }
        else
        {
            ip += 3;
        }
        REGVM_SYNC_POST();
        RDISPATCH();
    }

    /* ── Call ops ───────────────────────────────────────────────── */
    RCASE(CALL_VALUE)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ret = VREG_GET_A(i);
        uint16_t arg_count = VREG_GET_Bx(i);
        uint8_t arg_base_r;
        size_t orig_base = base + (size_t)ret;
        size_t total = (size_t)arg_count + 1U;

        if (VIGIL_UNLIKELY(ip + 1U >= code_count))
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "regvm call_value missing argument base operand");
            status = VIGIL_STATUS_INTERNAL;
            goto r_cleanup;
        }
        arg_base_r = (uint8_t)(code[ip + 1U] & 0xFFU);
        size_t arg_base = base + (size_t)arg_base_r;
        REGVM_ISOLATE_CALL(arg_base, total);
        vm->stack_count = arg_base + total;

        size_t callee_slot = arg_base;
        vigil_value_t callee_val = vm->stack[callee_slot];
        if (!vigil_nanbox_is_object(callee_val))
        {
            regvm_discard_isolated_call_values(vm, orig_base, arg_base, total);
            status = VIGIL_STATUS_INVALID_ARGUMENT;
            goto r_cleanup;
        }
        vigil_object_t *callee_obj = (vigil_object_t *)vigil_nanbox_decode_ptr(callee_val);

        vigil_value_release(&vm->stack[callee_slot]);

        if (arg_count > 0)
            memmove(&vm->stack[callee_slot], &vm->stack[callee_slot + 1], (size_t)arg_count * sizeof(vigil_value_t));
        vm->stack[callee_slot + (size_t)arg_count] = VIGIL_NANBOX_NIL;
        vm->stack_count -= 1U;
        arg_base = callee_slot;

        size_t saved_ip = frame->ip;
        frame->ip = VIGIL_VM_CHUNK_CODE_SIZE(frame->chunk);
        status = vigil_vm_execute_call(vm, callee_obj, (size_t)arg_count, error);
        frame = &vm->frames[vm->frame_count - 1];
        frame->ip = saved_ip;
        R = vm->stack + base;
        if (status != VIGIL_STATUS_OK) goto r_cleanup;

        {
            size_t ret_n = vm->stack_count > arg_base ? vm->stack_count - arg_base : 0;
            regvm_move_call_results(vm, orig_base, arg_base, total, (size_t)arg_count, ret_n);
        }
        REGVM_SYNC_POST();
        ip += 1U;
        RNEXT();
    }
    RCASE(CALL_SELF)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ret = VREG_GET_A(i);
        uint8_t arg_count = VREG_GET_B(i);
        uint8_t arg_base_r = (uint8_t)(code[ip + 1] & 0xFF);
        size_t orig_base = base + (size_t)ret;
        size_t arg_base = base + (size_t)arg_base_r;
        size_t saved_ip = frame->ip;
        frame->ip = VIGIL_VM_CHUNK_CODE_SIZE(frame->chunk);
        vm->stack_count = arg_base + (size_t)arg_count;
        status = vigil_vm_execute_call(vm, frame->function, (size_t)arg_count, error);
        frame = &vm->frames[vm->frame_count - 1];
        frame->ip = saved_ip;
        R = vm->stack + base;
        if (status != VIGIL_STATUS_OK)
            goto r_cleanup;
        {
            size_t ret_n = vm->stack_count > arg_base ? vm->stack_count - arg_base : 0U;
            regvm_move_call_results(vm, orig_base, arg_base, (size_t)arg_count, (size_t)arg_count, ret_n);
        }
        REGVM_SYNC_POST();
        ip += 1;
        RNEXT();
    }
    RCASE(CALL_INTERFACE)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ret = VREG_GET_A(i);
        uint8_t iface_idx = VREG_GET_B(i);
        uint8_t arg_count = VREG_GET_C(i);
        uint32_t method_idx;
        uint8_t arg_base_r;
        size_t total = (size_t)arg_count + 1U;
        size_t orig_base = base + (size_t)ret;

        if (VIGIL_UNLIKELY(ip + 2U >= code_count))
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL,
                                    "regvm interface call missing operand words");
            status = VIGIL_STATUS_INTERNAL;
            goto r_cleanup;
        }
        method_idx = code[ip + 1U];
        arg_base_r = (uint8_t)(code[ip + 2U] & 0xFFU);
        size_t arg_base = base + (size_t)arg_base_r;
        REGVM_ISOLATE_CALL(arg_base, total);
        vm->stack_count = arg_base + total;

        const vigil_value_t *receiver = &vm->stack[arg_base];
        if (!vigil_nanbox_is_object(*receiver) ||
            vigil_object_type((vigil_object_t *)vigil_nanbox_decode_ptr(*receiver)) != VIGIL_OBJECT_INSTANCE)
        {
            regvm_discard_isolated_call_values(vm, orig_base, arg_base, total);
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                    "interface call requires a class instance receiver");
            status = VIGIL_STATUS_INVALID_ARGUMENT;
            goto r_cleanup;
        }
        size_t class_index = vigil_instance_object_class_index(
            (vigil_object_t *)vigil_nanbox_decode_ptr(*receiver));
        const vigil_object_t *callee = vigil_function_object_resolve_interface_method(
            frame->function, class_index, (size_t)iface_idx, (size_t)method_idx);
        if (!callee)
        {
            regvm_discard_isolated_call_values(vm, orig_base, arg_base, total);
            status = VIGIL_STATUS_INTERNAL;
            goto r_cleanup;
        }

        size_t saved_ip = frame->ip;
        frame->ip = VIGIL_VM_CHUNK_CODE_SIZE(frame->chunk);
        status = vigil_vm_execute_call(vm, callee, total, error);
        frame = &vm->frames[vm->frame_count - 1];
        frame->ip = saved_ip;
        R = vm->stack + base;
        if (status != VIGIL_STATUS_OK) goto r_cleanup;

        {
            size_t ret_n = vm->stack_count > arg_base ? vm->stack_count - arg_base : 0;
            regvm_move_call_results(vm, orig_base, arg_base, total, total, ret_n);
        }
        REGVM_SYNC_POST();
        ip += 2U;
        RNEXT();
    }
    RCASE(CALL_EXTERN)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ret = VREG_GET_A(i);
        uint8_t ci = VREG_GET_B(i);
        uint8_t arg_count = VREG_GET_C(i);
        size_t orig_base = base + (size_t)ret;
        uint8_t arg_base_r;

        if (VIGIL_UNLIKELY(ip + 1U >= code_count))
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "regvm extern call missing argument base operand");
            status = VIGIL_STATUS_INTERNAL;
            goto r_cleanup;
        }
        arg_base_r = (uint8_t)(code[ip + 1U] & 0xFFU);
        size_t arg_base = base + (size_t)arg_base_r;
        REGVM_ISOLATE_CALL(arg_base, arg_count);
        vm->stack_count = arg_base + (size_t)arg_count;
        const vigil_value_t *desc_val = VIGIL_VM_CHUNK_CONSTANT(sc, (size_t)ci);
        if (!desc_val || !vigil_nanbox_has_object(*desc_val))
        {
            regvm_discard_isolated_call_values(vm, orig_base, arg_base, (size_t)arg_count);
            status = VIGIL_STATUS_UNSUPPORTED;
            goto r_cleanup;
        }
        vigil_object_t *desc_obj = (vigil_object_t *)vigil_nanbox_decode_ptr(*desc_val);
        const char *desc = vigil_string_object_c_str(desc_obj);
        size_t desc_len = vigil_string_object_length(desc_obj);
        status = vigil_vm_call_extern_fn(vm, desc, desc_len, (size_t)arg_count, error);
        R = vm->stack + base;
        if (status != VIGIL_STATUS_OK) goto r_cleanup;
        {
            size_t ret_n = vm->stack_count > arg_base ? vm->stack_count - arg_base : 0;
            regvm_move_call_results(vm, orig_base, arg_base, (size_t)arg_count, (size_t)arg_count, ret_n);
        }
        REGVM_SYNC_POST();
        ip += 1U;
        RNEXT();
    }
    RCASE(TAIL_CALL)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ret = VREG_GET_A(i);
        uint8_t func_idx = VREG_GET_B(i);
        uint8_t arg_count = VREG_GET_C(i);
        size_t arg_base = base + (size_t)ret;
        REGVM_ISOLATE_CALL(arg_base, arg_count);
        vm->stack_count = arg_base + (size_t)arg_count;
        const vigil_object_t *callee = vigil_vm_function_sibling(frame->function, (size_t)func_idx);
        if (!callee)
        {
            regvm_discard_isolated_call_values(vm, base + (size_t)ret, arg_base, (size_t)arg_count);
            status = VIGIL_STATUS_INTERNAL;
            goto r_cleanup;
        }
        status = vigil_vm_execute_call(vm, callee, (size_t)arg_count, error);
        R = vm->stack + base;
        if (status == VIGIL_STATUS_OK)
        {
            size_t ret_n = vm->stack_count > arg_base ? vm->stack_count - arg_base : 0U;

            if (ret_n > 1U)
                regvm_release_value_range(&vm->stack[arg_base + 1U], ret_n - 1U);

            ret_base_r = 0U;
            ret_count = ret_n > 0U ? 1U : 0U;
            if (ret_n > 0U)
            {
                vigil_value_t rv = vm->stack[arg_base];

                if (arg_base != base)
                {
                    RRELEASE(0);
                    R[0] = rv;
                    vm->stack[arg_base] = VIGIL_NANBOX_NIL;
                }

                *out_value = R[0];
            }
            else
            {
                RRELEASE(0);
                R[0] = VIGIL_NANBOX_NIL;
                *out_value = VIGIL_NANBOX_NIL;
            }
            vm->stack_count = base + (size_t)ret_count;
        }
        /* If we were pushed by an inline CALL, restore the caller. */
        if (vm->frame_count > initial_frame_count)
        {
            vm->frame_count -= 1U;
            frame = &vm->frames[vm->frame_count - 1];
            vigil_chunk_t *cc = (vigil_chunk_t *)frame->chunk;
            rc = cc->reg_cache;
            code = rc->code; code_count = rc->code_count;
            sc = rc->stack_chunk;
            base = frame->base_slot;
            ip = frame->ip;
            R = vm->stack + base;
            if (ip >= 2 && VREG_GET_OP(code[ip - 2]) == VREG_CALL_SELF)
            {
                uint8_t cs_ret = VREG_GET_A(code[ip - 2]);
                R[cs_ret] = *out_value;
            }
            if (vm->stack_count < base + rc->max_registers)
                vm->stack_count = base + rc->max_registers;
            if (status != VIGIL_STATUS_OK) goto r_cleanup;
            RDISPATCH();
        }
        goto r_cleanup;
    }

    /* ── Array method ops ──────────────────────────────────────── */
    RCASE(ARRAY_PUSH)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t top = VREG_GET_B(i) > VREG_GET_C(i) ? VREG_GET_B(i) : VREG_GET_C(i);
        REGVM_STACK_HELPER(top, 2, VREG_GET_A(i), 0, vigil_vm_op_array_push(vm, frame, error));
    }
    RCASE(ARRAY_POP)
    {
        vigil_reg_instr_t i = code[ip];
        REGVM_STACK_HELPER(VREG_GET_C(i), 2, VREG_GET_A(i), 2, vigil_vm_op_array_pop(vm, frame, error));
    }
    RCASE(ARRAY_GET_SAFE)
    {
        vigil_reg_instr_t i = code[ip];
        REGVM_STACK_HELPER(VREG_GET_C(i), 3, VREG_GET_A(i), 2, vigil_vm_op_array_get_safe(vm, frame, error));
    }
    RCASE(ARRAY_SET_SAFE)
    {
        vigil_reg_instr_t i = code[ip];
        REGVM_STACK_HELPER(VREG_GET_C(i), 3, VREG_GET_A(i), 1, vigil_vm_op_array_set_safe(vm, frame, error));
    }
    RCASE(ARRAY_SLICE)
    {
        vigil_reg_instr_t i = code[ip];
        /* end register is at C (translator stored start in C, end is C+1 area) */
        uint8_t top = VREG_GET_C(i);
        if (VREG_GET_B(i) > top) top = VREG_GET_B(i);
        REGVM_STACK_HELPER(top, 3, VREG_GET_A(i), 1, vigil_vm_op_array_slice(vm, frame, error));
    }
    RCASE(ARRAY_CONTAINS)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t top = VREG_GET_B(i) > VREG_GET_C(i) ? VREG_GET_B(i) : VREG_GET_C(i);
        REGVM_STACK_HELPER(top, 2, VREG_GET_A(i), 1, vigil_vm_op_array_contains(vm, frame, error));
    }

    /* ── Map method ops ────────────────────────────────────────── */
    RCASE(MAP_GET_SAFE)
    {
        vigil_reg_instr_t i = code[ip];
        REGVM_STACK_HELPER(VREG_GET_C(i), 3, VREG_GET_A(i), 2, vigil_vm_op_map_get_safe(vm, frame, error));
    }
    RCASE(MAP_SET_SAFE)
    {
        vigil_reg_instr_t i = code[ip];
        REGVM_STACK_HELPER(VREG_GET_C(i), 3, VREG_GET_A(i), 1, vigil_vm_op_map_set_safe(vm, frame, error));
    }
    RCASE(MAP_REMOVE_SAFE)
    {
        vigil_reg_instr_t i = code[ip];
        REGVM_STACK_HELPER(VREG_GET_C(i), 3, VREG_GET_A(i), 2, vigil_vm_op_map_remove_safe(vm, frame, error));
    }
    RCASE(MAP_HAS)
    {
        vigil_reg_instr_t i = code[ip];
        REGVM_STACK_HELPER(VREG_GET_C(i), 2, VREG_GET_A(i), 1, vigil_vm_op_map_has(vm, frame, error));
    }
    RCASE(MAP_KEYS)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t keys_op = VIGIL_OPCODE_MAP_KEYS;
        REGVM_STACK_HELPER(VREG_GET_B(i), 1, VREG_GET_A(i), 1, vigil_vm_op_map_keys_values(vm, frame, &keys_op, error));
    }
    RCASE(MAP_VALUES)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t vals_op = VIGIL_OPCODE_MAP_VALUES;
        REGVM_STACK_HELPER(VREG_GET_B(i), 1, VREG_GET_A(i), 1, vigil_vm_op_map_keys_values(vm, frame, &vals_op, error));
    }
    RCASE(MAP_KEY_AT)
    {
        vigil_reg_instr_t i = code[ip];
        REGVM_STACK_HELPER(VREG_GET_C(i), 2, VREG_GET_A(i), 1, vigil_vm_op_get_map_key_at(vm, frame, error));
    }
    RCASE(MAP_VALUE_AT)
    {
        vigil_reg_instr_t i = code[ip];
        REGVM_STACK_HELPER(VREG_GET_C(i), 2, VREG_GET_A(i), 1, vigil_vm_op_get_map_value_at(vm, frame, error));
    }

    /* ── Char from int ─────────────────────────────────────────── */
    RCASE(CHAR_FROM_INT)
    {
        vigil_reg_instr_t i = code[ip];
        if (VREG_GET_C(i) == 1)
            REGVM_STACK_HELPER(VREG_GET_B(i), 1, VREG_GET_A(i), 1, vigil_vm_op_string_to_c(vm, frame, error));
        else
            REGVM_STACK_HELPER(VREG_GET_B(i), 1, VREG_GET_A(i), 1, vigil_vm_op_char_from_int(vm, frame, error));
    }

    /* ── Return ────────────────────────────────────────────────── */
    RCASE(RETURN)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t base_r = VREG_GET_A(i);
        uint8_t count = VREG_GET_B(i);
        if (count >= 1)
            *out_value = R[base_r];
        else
            *out_value = VIGIL_NANBOX_NIL;
        ret_base_r = base_r;
        ret_count = count;
        vm->stack_count = base + (size_t)base_r + (size_t)count;

        /* Fast path: inline return to caller (no defers, not top-level). */
        if (VIGIL_LIKELY(frame->defer_count == 0 && vm->frame_count > initial_frame_count))
        {
            /* Release callee-owned registers (skip args and return values). */
            if (has_reg_objects)
            {
                size_t cb = base;
                size_t rlo = (size_t)base_r;
                size_t rhi = rlo + (size_t)count;
                for (size_t ri = (size_t)rc->arity; ri < (size_t)rc->max_registers; ri++)
                {
                    if (ri >= rlo && ri < rhi) continue;
                    if (vigil_nanbox_has_object(vm->stack[cb + ri]))
                        vigil_value_release(&vm->stack[cb + ri]);
                }
            }

            /* Free the popped frame's defer array (allocated with raw realloc). */
            { vigil_vm_frame_t *pf = frame; if (pf->defers) { free(pf->defers); pf->defers = NULL; } }
            vm->frame_count -= 1U;
            frame = &vm->frames[vm->frame_count - 1];
            rc = ((vigil_chunk_t *)frame->chunk)->reg_cache;
            code = rc->code; code_count = rc->code_count;
            sc = rc->stack_chunk;
            base = frame->base_slot;
            ip = frame->ip;
            R = vm->stack + base; has_reg_objects = 1;
            /* For CALL_SELF (2-word instruction), move return value
               from R[arg_base_r] to R[ret]. */
            if (ip >= 2 && VREG_GET_OP(code[ip - 2]) == VREG_CALL_SELF)
            {
                uint8_t cs_ret = VREG_GET_A(code[ip - 2]);
                uint8_t cs_abr = (uint8_t)(code[ip - 1] & 0xFF);
                if (cs_ret != cs_abr && count >= 1)
                    R[cs_ret] = R[cs_abr];
            }
            if (vm->stack_count < base + rc->max_registers)
                vm->stack_count = base + rc->max_registers;
            RDISPATCH();
        }

        /* Slow path: drain defers, then return. */
        if (frame->defer_count > 0U)
        {
            size_t fi = (size_t)(frame - vm->frames);
            size_t pre_drain = vm->stack_count;
            status = regvm_drain_defers(vm, fi, error);
            /* Release any return values left on the stack by deferred calls. */
            while (vm->stack_count > pre_drain)
            {
                vm->stack_count--;
                if (vigil_nanbox_has_object(vm->stack[vm->stack_count]))
                    vigil_value_release(&vm->stack[vm->stack_count]);
            }
            if (status != VIGIL_STATUS_OK) goto r_cleanup;
        }
        if (vm->frame_count > initial_frame_count)
        {
            if (has_reg_objects)
            {
                size_t rlo2 = (size_t)base_r, rhi2 = rlo2 + (size_t)count;
                for (size_t ri2 = (size_t)rc->arity; ri2 < (size_t)rc->max_registers; ri2++)
                {
                    if (ri2 >= rlo2 && ri2 < rhi2) continue;
                    if (vigil_nanbox_has_object(vm->stack[base + ri2]))
                        vigil_value_release(&vm->stack[base + ri2]);
                }
            }

            { vigil_vm_frame_t *pf = &vm->frames[vm->frame_count - 1]; if (pf->defers) { free(pf->defers); pf->defers = NULL; } }
            vm->frame_count -= 1U;
            frame = &vm->frames[vm->frame_count - 1];
            rc = ((vigil_chunk_t *)frame->chunk)->reg_cache;
            code = rc->code; code_count = rc->code_count;
            sc = rc->stack_chunk;
            base = frame->base_slot;
            ip = frame->ip;
            R = vm->stack + base; has_reg_objects = 1;
            if (ip >= 2 && VREG_GET_OP(code[ip - 2]) == VREG_CALL_SELF)
            {
                uint8_t cs_ret = VREG_GET_A(code[ip - 2]);
                uint8_t cs_abr = (uint8_t)(code[ip - 1] & 0xFF);
                if (cs_ret != cs_abr && count >= 1)
                    R[cs_ret] = R[cs_abr];
            }
            if (vm->stack_count < base + rc->max_registers)
                vm->stack_count = base + rc->max_registers;
            RDISPATCH();
        }
        status = VIGIL_STATUS_OK;
        goto r_cleanup;
    }

#if REGVM_COMPUTED_GOTO
r_UNKNOWN:
    status = VIGIL_STATUS_UNSUPPORTED;
    goto r_cleanup;
    _Pragma("GCC diagnostic pop")
#else
        default:
            status = VIGIL_STATUS_UNSUPPORTED;
            goto r_cleanup;
        }
    }
#endif

        r_overflow:
    vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "integer arithmetic overflow or invalid operation");
    status = VIGIL_STATUS_INVALID_ARGUMENT;
    goto r_cleanup;

r_divzero:
    vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "division by zero in register VM");
    status = VIGIL_STATUS_INVALID_ARGUMENT;
    goto r_cleanup;

r_cleanup:
    if (has_reg_objects && R != NULL)
    {
        size_t nregs = (size_t)rc->max_registers;
        size_t rlo = (size_t)ret_base_r;
        size_t rhi = rlo + (size_t)ret_count;
        for (size_t ri = 0; ri < nregs; ri++)
        {
            if (ri >= rlo && ri < rhi) continue;
            if (vigil_nanbox_has_object(R[ri]))
                vigil_value_release(&R[ri]);
        }
    }
    return status;
}
