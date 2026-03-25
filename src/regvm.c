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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_nanbox.h"
#include "internal/vigil_vm_internal.h"
#include "vigil/chunk.h"

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

/* ── Virtual stack ─────────────────────────────────────────────── */

typedef struct
{
    uint8_t regs[256];
    int top;
    uint8_t max_reg;
    uint8_t local_count;
} vstack_t;

static void vs_init(vstack_t *vs, uint8_t lc)
{
    vs->top = 0;
    vs->max_reg = lc > 0 ? lc : 1;
    vs->local_count = lc;
    memset(vs->regs, 0, sizeof(vs->regs));
}

static uint8_t vs_push(vstack_t *vs)
{
    uint8_t r = (uint8_t)vs->top;
    vs->regs[vs->top] = r;
    vs->top++;
    if (r >= vs->max_reg)
        vs->max_reg = r + 1;
    return r;
}

static uint8_t vs_pop(vstack_t *vs)
{
    vs->top--;
    return vs->regs[vs->top];
}

static uint8_t vs_peek(const vstack_t *vs, int depth)
{
    return vs->regs[vs->top - 1 - depth];
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
    case VIGIL_OPCODE_CALL_VALUE:
    case VIGIL_OPCODE_CALL_SELF:
    case VIGIL_OPCODE_NEW_ARRAY:
    case VIGIL_OPCODE_NEW_MAP:
    case VIGIL_OPCODE_GET_FIELD:
    case VIGIL_OPCODE_SET_FIELD:
    case VIGIL_OPCODE_NEW_INSTANCE:
    case VIGIL_OPCODE_FORMAT_F64:
    case VIGIL_OPCODE_ADD_F64_STORE:
    case VIGIL_OPCODE_SUBTRACT_F64_STORE:
    case VIGIL_OPCODE_MULTIPLY_F64_STORE:
    case VIGIL_OPCODE_PARSE_I32:
    case VIGIL_OPCODE_PARSE_F64:
    case VIGIL_OPCODE_PARSE_BOOL:
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
    case VIGIL_OPCODE_CALL:
    case VIGIL_OPCODE_TAIL_CALL:
    case VIGIL_OPCODE_CALL_NATIVE:
    case VIGIL_OPCODE_CALL_INTERFACE:
    case VIGIL_OPCODE_CALL_EXTERN:
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
    case VIGIL_OPCODE_DEFER_CALL:
    case VIGIL_OPCODE_DEFER_CALL_VALUE:
    case VIGIL_OPCODE_DEFER_NEW_INSTANCE:
    case VIGIL_OPCODE_DEFER_CALL_INTERFACE:
    case VIGIL_OPCODE_DEFER_CALL_NATIVE:
        return 9;

    /* 13-byte: opcode + u32 + u32 + u32 */
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

static uint8_t map_cmp_jmp(uint8_t stack_op)
{
    switch (stack_op)
    {
    case VIGIL_OPCODE_LESS_I32_JUMP_IF_FALSE:
        return VREG_LT_I32_JMP;
    case VIGIL_OPCODE_LESS_EQUAL_I32_JUMP_IF_FALSE:
        return VREG_LE_I32_JMP;
    case VIGIL_OPCODE_GREATER_I32_JUMP_IF_FALSE:
        return VREG_GT_I32_JMP;
    case VIGIL_OPCODE_GREATER_EQUAL_I32_JUMP_IF_FALSE:
        return VREG_GE_I32_JMP;
    case VIGIL_OPCODE_EQUAL_I32_JUMP_IF_FALSE:
        return VREG_EQ_I32_JMP;
    case VIGIL_OPCODE_NOT_EQUAL_I32_JUMP_IF_FALSE:
        return VREG_NE_I32_JMP;
    case VIGIL_OPCODE_LESS_I64_JUMP_IF_FALSE:
        return VREG_LT_I64_JMP;
    case VIGIL_OPCODE_LESS_EQUAL_I64_JUMP_IF_FALSE:
        return VREG_LE_I64_JMP;
    case VIGIL_OPCODE_GREATER_I64_JUMP_IF_FALSE:
        return VREG_GT_I64_JMP;
    case VIGIL_OPCODE_GREATER_EQUAL_I64_JUMP_IF_FALSE:
        return VREG_GE_I64_JMP;
    case VIGIL_OPCODE_EQUAL_I64_JUMP_IF_FALSE:
        return VREG_EQ_I64_JMP;
    case VIGIL_OPCODE_NOT_EQUAL_I64_JUMP_IF_FALSE:
        return VREG_NE_I64_JMP;
    default:
        return VREG_LT_I32_JMP;
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
        vigil_status_t _s = emit(rc, (instr), start_ip);                                                               \
        if (_s != VIGIL_STATUS_OK)                                                                                     \
            goto tr_fail;                                                                                              \
    } while (0)

/* Count locals by scanning for the highest GET_LOCAL/SET_LOCAL operand. */
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

vigil_status_t vigil_reg_translate(const vigil_chunk_t *stack_chunk, vigil_reg_chunk_t *rc, vigil_runtime_t *runtime,
                                   vigil_error_t *error)
{
    (void)runtime;
    (void)error;

    const uint8_t *code = stack_chunk->code.data;
    size_t code_size = stack_chunk->code.length;

    vigil_reg_chunk_init(rc);
    rc->stack_chunk = stack_chunk;

    uint8_t lc = count_locals(code, code_size);
    vstack_t vs;
    vs_init(&vs, lc);

    /* Collect jump targets for stack-state reset at join points. */
    size_t jt_count = 0;
    size_t *jt = collect_jump_targets(code, code_size, &jt_count);

    offset_map_t omap;
    omap_init(&omap);
    jump_patch_list_t patches;
    jpatch_init(&patches);

    size_t ip = 0;
    while (ip < code_size)
    {
        size_t start_ip = ip;
        uint8_t op = code[ip];

        /* Record offset mapping. */
        omap_add(&omap, start_ip, rc->code_count);

        /* At jump targets, restore the virtual stack depth from the
           jump source. Use the maximum depth from all incoming jumps
           to handle join points where branches have different depths. */
        {
            int restored = 0;
            for (size_t pi = 0; pi < patches.count; pi++)
            {
                if (patches.items[pi].target_stack_off == start_ip)
                {
                    if (!restored || patches.items[pi].stack_depth > vs.top)
                        vs.top = patches.items[pi].stack_depth;
                    restored = 1;
                }
            }
        }

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
            vs_pop(&vs);
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
            uint8_t dst = vs_push(&vs);
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
            /* Emit placeholder — will be patched. */
            jpatch_add(&patches, rc->code_count, target, 0, vs.top);
            TR_EMIT(vigil_reg_asbx(VREG_JMP, 0, 0));
            break;
        }
        case VIGIL_OPCODE_LOOP: {
            uint32_t off = rd_u32(code, &ip);
            size_t target = ip - (size_t)off;
            jpatch_add(&patches, rc->code_count, target, 1, vs.top);
            TR_EMIT(vigil_reg_asbx(VREG_JMP, 0, 0));
            break;
        }
        case VIGIL_OPCODE_JUMP_IF_FALSE: {
            uint32_t off = rd_u32(code, &ip);
            size_t target = ip + (size_t)off;
            /* Peek at the condition register (don't pop — the following
               POP in the bytecode handles that on the true path, and
               the jump skips it on the false path). */
            uint8_t cond = vs_peek(&vs, 0);
            TR_EMIT(vigil_reg_abc(VREG_TEST, cond, 0, 0));
            jpatch_add(&patches, rc->code_count, target, 0, vs.top);
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
            /* These pop two values and jump if comparison is false. */
            uint8_t rb = vs_pop(&vs);
            uint8_t ra = vs_pop(&vs);
            size_t target = ip + (size_t)off;
            /* Encode: op=cmp_jmp, A=ra, B=rb, jump offset in next word */
            TR_EMIT(vigil_reg_abc(map_cmp_jmp(op), ra, rb, 0));
            /* Emit a second word with the jump offset (patched later). */
            jpatch_add(&patches, rc->code_count, target, 0, vs.top);
            TR_EMIT(vigil_reg_asbx(VREG_JMP, 0, 0));
            /* The stack VM skips a trailing POP after fused compare+jump.
               We must do the same to keep the virtual stack in sync. */
            if (ip < code_size && code[ip] == VIGIL_OPCODE_POP)
                ip += 1;
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
            /* Encode: A=local, B=delta, C=cmp. Next word: constant index.
               Third word: jump offset (patched). */
            TR_EMIT(vigil_reg_abc(rop, (uint8_t)idx, (uint8_t)delta, cmp));
            TR_EMIT(vigil_reg_abx(VREG_LOAD_K, 0, (uint16_t)ci)); /* constant index */
            jpatch_add(&patches, rc->code_count, target, 1, vs.top);
            TR_EMIT(vigil_reg_asbx(VREG_JMP, 0, 0)); /* back jump */
            /* FORLOOP pushes FALSE on exit. */
            uint8_t fr = vs_push(&vs);
            (void)fr; /* The FALSE is consumed by the next JUMP_IF_FALSE */
            break;
        }

        /* ── Calls ─────────────────────────────────────────────── */
        case VIGIL_OPCODE_CALL: {
            uint32_t func_idx = rd_u32(code, &ip);
            uint32_t arg_count = rd_raw_u32(code, &ip);
            /* Args are on the virtual stack. Pop them. */
            uint8_t base = 0;
            (void)base;
            if (arg_count > 0)
            {
                /* The first arg is deepest on the stack. */
                base = vs.regs[vs.top - (int)arg_count];
                for (uint32_t i = 0; i < arg_count; i++)
                    vs_pop(&vs);
            }
            /* Push return value register. */
            uint8_t ret = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_CALL, ret, (uint8_t)func_idx, (uint8_t)arg_count));
            break;
        }
        case VIGIL_OPCODE_CALL_SELF: {
            uint32_t arg_count = rd_u32(code, &ip);
            uint8_t base = 0;
            (void)base;
            if (arg_count > 0)
            {
                base = vs.regs[vs.top - (int)arg_count];
                for (uint32_t i = 0; i < arg_count; i++)
                    vs_pop(&vs);
            }
            uint8_t ret = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_CALL_SELF, ret, (uint8_t)arg_count, 0));
            break;
        }
        case VIGIL_OPCODE_TAIL_CALL: {
            uint32_t func_idx = rd_u32(code, &ip);
            uint32_t arg_count = rd_raw_u32(code, &ip);
            for (uint32_t i = 0; i < arg_count; i++)
                vs_pop(&vs);
            uint8_t ret = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_TAIL_CALL, ret, (uint8_t)func_idx, (uint8_t)arg_count));
            break;
        }
        case VIGIL_OPCODE_CALL_NATIVE: {
            uint32_t ci = rd_u32(code, &ip);
            uint32_t arg_count = rd_raw_u32(code, &ip);
            /* Infer return count from bytecode after the call.
               - Multiple SET_LOCAL+POP pairs: multi-return.
               - Otherwise: single return (native always pushes at least nil). */
            uint32_t ret_count = 1;
            {
                size_t scan = ip;
                uint32_t pairs = 0;
                while (scan + 5 < code_size && code[scan] == VIGIL_OPCODE_SET_LOCAL)
                {
                    if (scan + 6 <= code_size && code[scan + 5] == VIGIL_OPCODE_POP)
                    {
                        pairs++;
                        scan += 6;
                    }
                    else
                        break;
                }
                if (pairs >= 2)
                    ret_count = pairs;
            }
            for (uint32_t i = 0; i < arg_count; i++)
                vs_pop(&vs);
            uint8_t ret = (uint8_t)vs.top; /* position where results start */
            for (uint32_t i = 0; i < ret_count; i++)
                vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_CALL_NATIVE, ret, (uint8_t)ci, (uint8_t)arg_count));
            break;
        }
        case VIGIL_OPCODE_CALL_VALUE: {
            uint32_t arg_count = rd_u32(code, &ip);
            /* The callable is on the stack below the args. */
            for (uint32_t i = 0; i < arg_count + 1; i++)
                vs_pop(&vs);
            uint8_t ret = vs_push(&vs);
            TR_EMIT(vigil_reg_abx(VREG_CALL_VALUE, ret, (uint16_t)arg_count));
            break;
        }
        case VIGIL_OPCODE_CALL_INTERFACE: {
            uint32_t iface_idx = rd_u32(code, &ip);
            uint32_t arg_count = rd_raw_u32(code, &ip);
            for (uint32_t i = 0; i < arg_count + 1; i++)
                vs_pop(&vs);
            uint8_t ret = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_CALL_INTERFACE, ret, (uint8_t)iface_idx, (uint8_t)arg_count));
            break;
        }
        case VIGIL_OPCODE_CALL_EXTERN: {
            uint32_t ci = rd_u32(code, &ip);
            uint32_t arg_count = rd_raw_u32(code, &ip);
            for (uint32_t i = 0; i < arg_count; i++)
                vs_pop(&vs);
            uint8_t ret = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_CALL_EXTERN, ret, (uint8_t)ci, (uint8_t)arg_count));
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
            uint8_t base_r = 0;
            if (ret_count > 0 && vs.top > 0)
                base_r = vs.regs[vs.top - (int)ret_count];
            for (uint32_t i = 0; i < ret_count && vs.top > 0; i++)
                vs_pop(&vs);
            TR_EMIT(vigil_reg_abc(VREG_RETURN, base_r, (uint8_t)ret_count, 0));
            /* Don't reset vs.top — the jump target restoration at the
               next reachable instruction will set the correct depth. */
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
            /* Captures are on the stack. Pop them. */
            for (uint32_t i = 0; i < cap_count; i++)
                vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_NEW_CLOSURE, r, (uint8_t)func_idx, (uint8_t)cap_count));
            break;
        }

        /* ── Objects ───────────────────────────────────────────── */
        case VIGIL_OPCODE_NEW_INSTANCE: {
            uint32_t ci = rd_u32(code, &ip);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abx(VREG_NEW_INSTANCE, r, (uint16_t)ci));
            break;
        }
        case VIGIL_OPCODE_GET_FIELD: {
            uint32_t fi = rd_u32(code, &ip);
            uint8_t obj = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_GET_FIELD, r, obj, (uint8_t)fi));
            break;
        }
        case VIGIL_OPCODE_SET_FIELD: {
            uint32_t fi = rd_u32(code, &ip);
            uint8_t val = vs_pop(&vs);
            uint8_t obj = vs_pop(&vs);
            TR_EMIT(vigil_reg_abc(VREG_SET_FIELD, obj, (uint8_t)fi, val));
            break;
        }
        case VIGIL_OPCODE_NEW_ARRAY: {
            uint32_t count = rd_u32(code, &ip);
            for (uint32_t i = 0; i < count; i++)
                vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abx(VREG_NEW_ARRAY, r, (uint16_t)count));
            break;
        }
        case VIGIL_OPCODE_NEW_MAP: {
            uint32_t count = rd_u32(code, &ip);
            for (uint32_t i = 0; i < count * 2; i++)
                vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abx(VREG_NEW_MAP, r, (uint16_t)count));
            break;
        }
        case VIGIL_OPCODE_GET_INDEX: {
            uint8_t idx = vs_pop(&vs);
            uint8_t obj = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_GET_INDEX, r, obj, idx));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_SET_INDEX: {
            uint8_t val = vs_pop(&vs);
            uint8_t idx = vs_pop(&vs);
            uint8_t obj = vs_pop(&vs);
            TR_EMIT(vigil_reg_abc(VREG_SET_INDEX, obj, idx, val));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_GET_COLLECTION_SIZE: {
            uint8_t obj = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_COLLECTION_SIZE, r, obj, 0));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_GET_STRING_SIZE: {
            uint8_t obj = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_COLLECTION_SIZE, r, obj, 1));
            ip += 1;
            break;
        }

        /* ── Error handling ────────────────────────────────────── */
        case VIGIL_OPCODE_NEW_ERROR: {
            uint8_t msg = vs_pop(&vs);
            uint8_t kind = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_NEW_ERROR, r, kind, msg));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_GET_ERROR_KIND: {
            uint8_t err = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_GET_ERROR_KIND, r, err, 0));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_GET_ERROR_MESSAGE: {
            uint8_t err = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_GET_ERROR_MSG, r, err, 0));
            ip += 1;
            break;
        }

        /* ── Format ────────────────────────────────────────────── */
        case VIGIL_OPCODE_FORMAT_F64: {
            uint32_t prec = rd_u32(code, &ip);
            uint8_t val = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_FORMAT_F64, r, val, (uint8_t)prec));
            break;
        }
        case VIGIL_OPCODE_FORMAT_SPEC: {
            uint32_t w1 = rd_u32(code, &ip);
            uint32_t w2 = rd_raw_u32(code, &ip);
            uint8_t val = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            /* Encode as two instructions. */
            TR_EMIT(vigil_reg_abc(VREG_FORMAT_SPEC, r, val, 0));
            TR_EMIT((uint32_t)w1);
            TR_EMIT((uint32_t)w2);
            break;
        }

        /* ── Parse intrinsics ──────────────────────────────────── */
        case VIGIL_OPCODE_PARSE_I32: {
            rd_u32(code, &ip); /* skip operand */
            uint8_t str = vs_pop(&vs);
            uint8_t r1 = vs_push(&vs); /* value */
            uint8_t r2 = vs_push(&vs); /* error */
            TR_EMIT(vigil_reg_abc(VREG_PARSE_I32, r1, str, r2));
            break;
        }
        case VIGIL_OPCODE_PARSE_F64: {
            rd_u32(code, &ip);
            uint8_t str = vs_pop(&vs);
            uint8_t r1 = vs_push(&vs);
            uint8_t r2 = vs_push(&vs);
            (void)r2;
            TR_EMIT(vigil_reg_abc(VREG_PARSE_F64, r1, str, r2));
            break;
        }
        case VIGIL_OPCODE_PARSE_BOOL: {
            rd_u32(code, &ip);
            uint8_t str = vs_pop(&vs);
            uint8_t r1 = vs_push(&vs);
            uint8_t r2 = vs_push(&vs);
            (void)r2;
            TR_EMIT(vigil_reg_abc(VREG_PARSE_BOOL, r1, str, r2));
            break;
        }

        case VIGIL_OPCODE_CHAR_FROM_INT: {
            uint8_t val = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_CHAR_FROM_INT, r, val, 0));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_STRING_TO_C: {
            /* Pops string, pushes i32 codepoint. */
            uint8_t val = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_CHAR_FROM_INT, r, val, 1)); /* reuse with flag */
            ip += 1;
            break;
        }

        /* ── String/collection ops — encode as VREG_STRING_OP ──── */
        case VIGIL_OPCODE_STRING_CONTAINS:
        case VIGIL_OPCODE_STRING_STARTS_WITH:
        case VIGIL_OPCODE_STRING_ENDS_WITH:
        case VIGIL_OPCODE_STRING_INDEX_OF:
        case VIGIL_OPCODE_STRING_LAST_INDEX_OF:
        case VIGIL_OPCODE_STRING_REPLACE:
        case VIGIL_OPCODE_STRING_EQUAL_FOLD: {
            /* Two-arg string ops: pop 2, push 1. */
            uint8_t arg = vs_pop(&vs);
            (void)arg;
            uint8_t str = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_STRING_OP, r, str, op));
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
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_STRING_OP, r, str, op));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_STRING_SPLIT:
        case VIGIL_OPCODE_STRING_SUBSTR:
        case VIGIL_OPCODE_STRING_CUT:
        case VIGIL_OPCODE_STRING_TRIM_PREFIX:
        case VIGIL_OPCODE_STRING_TRIM_SUFFIX: {
            uint8_t arg = vs_pop(&vs);
            (void)arg;
            uint8_t str = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_STRING_OP, r, str, op));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_STRING_REPEAT:
        case VIGIL_OPCODE_STRING_COUNT:
        case VIGIL_OPCODE_STRING_CHAR_AT:
        case VIGIL_OPCODE_STRING_JOIN: {
            uint8_t arg = vs_pop(&vs);
            (void)arg;
            uint8_t str = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_STRING_OP, r, str, op));
            ip += 1;
            break;
        }

        /* ── Collection method ops ─────────────────────────────── */
        case VIGIL_OPCODE_ARRAY_PUSH: {
            uint8_t val = vs_pop(&vs);
            uint8_t arr = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_ARRAY_PUSH, r, arr, val));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_ARRAY_POP: {
            uint8_t arr = vs_pop(&vs);
            uint8_t r1 = vs_push(&vs);
            uint8_t r2 = vs_push(&vs);
            (void)r2;
            TR_EMIT(vigil_reg_abc(VREG_ARRAY_POP, r1, arr, r2));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_ARRAY_GET_SAFE:
        case VIGIL_OPCODE_ARRAY_SET_SAFE: {
            uint8_t idx = vs_pop(&vs);
            uint8_t arr = vs_pop(&vs);
            uint8_t r1 = vs_push(&vs);
            uint8_t r2 = vs_push(&vs);
            (void)r2;
            uint8_t rop = (op == VIGIL_OPCODE_ARRAY_GET_SAFE) ? VREG_ARRAY_GET_SAFE : VREG_ARRAY_SET_SAFE;
            TR_EMIT(vigil_reg_abc(rop, r1, arr, idx));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_ARRAY_SLICE: {
            uint8_t end = vs_pop(&vs);
            (void)end;
            uint8_t start = vs_pop(&vs);
            uint8_t arr = vs_pop(&vs);
            uint8_t r1 = vs_push(&vs);
            uint8_t r2 = vs_push(&vs);
            (void)r2;
            TR_EMIT(vigil_reg_abc(VREG_ARRAY_SLICE, r1, arr, start));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_ARRAY_CONTAINS: {
            uint8_t val = vs_pop(&vs);
            uint8_t arr = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_ARRAY_CONTAINS, r, arr, val));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_MAP_GET_SAFE:
        case VIGIL_OPCODE_MAP_SET_SAFE:
        case VIGIL_OPCODE_MAP_REMOVE_SAFE: {
            uint8_t key = vs_pop(&vs);
            uint8_t map = vs_pop(&vs);
            uint8_t r1 = vs_push(&vs);
            uint8_t r2 = vs_push(&vs);
            (void)r2;
            uint8_t rop = VREG_MAP_GET_SAFE;
            if (op == VIGIL_OPCODE_MAP_SET_SAFE)
                rop = VREG_MAP_SET_SAFE;
            if (op == VIGIL_OPCODE_MAP_REMOVE_SAFE)
                rop = VREG_MAP_REMOVE_SAFE;
            TR_EMIT(vigil_reg_abc(rop, r1, map, key));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_MAP_HAS: {
            uint8_t key = vs_pop(&vs);
            uint8_t map = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            TR_EMIT(vigil_reg_abc(VREG_MAP_HAS, r, map, key));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_MAP_KEYS:
        case VIGIL_OPCODE_MAP_VALUES: {
            uint8_t map = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            uint8_t rop = (op == VIGIL_OPCODE_MAP_KEYS) ? VREG_MAP_KEYS : VREG_MAP_VALUES;
            TR_EMIT(vigil_reg_abc(rop, r, map, 0));
            ip += 1;
            break;
        }
        case VIGIL_OPCODE_GET_MAP_KEY_AT:
        case VIGIL_OPCODE_GET_MAP_VALUE_AT: {
            uint8_t idx = vs_pop(&vs);
            uint8_t map = vs_pop(&vs);
            uint8_t r = vs_push(&vs);
            uint8_t rop = (op == VIGIL_OPCODE_GET_MAP_KEY_AT) ? VREG_MAP_KEY_AT : VREG_MAP_VALUE_AT;
            TR_EMIT(vigil_reg_abc(rop, r, map, idx));
            ip += 1;
            break;
        }

        /* ── Defer ops — encode generically ────────────────────── */
        case VIGIL_OPCODE_DEFER_CALL:
        case VIGIL_OPCODE_DEFER_CALL_VALUE:
        case VIGIL_OPCODE_DEFER_NEW_INSTANCE:
        case VIGIL_OPCODE_DEFER_CALL_INTERFACE:
        case VIGIL_OPCODE_DEFER_CALL_NATIVE: {
            uint32_t a = rd_u32(code, &ip);
            uint32_t b = rd_raw_u32(code, &ip);
            TR_EMIT(vigil_reg_abc(VREG_DEFER, op, (uint8_t)a, (uint8_t)b));
            TR_EMIT((uint32_t)a);
            TR_EMIT((uint32_t)b);
            break;
        }

        default:
            /* Unknown opcode — skip it. This shouldn't happen for valid bytecode. */
            ip += stack_op_size(code, ip, code_size);
            break;
        }
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

    rc->max_registers = vs.max_reg;

    free(jt);
    omap_free(&omap);
    jpatch_free(&patches);
    return VIGIL_STATUS_OK;

tr_fail:
    free(jt);
    omap_free(&omap);
    jpatch_free(&patches);
    vigil_reg_chunk_free(rc, runtime);
    return VIGIL_STATUS_OUT_OF_MEMORY;
}

/* ── Debug dump ────────────────────────────────────────────────── */

static const char *reg_op_name(uint8_t op)
{
    static const char *names[] = {
        [VREG_MOVE] = "MOVE",
        [VREG_LOAD_K] = "LOAD_K",
        [VREG_LOAD_NIL] = "LOAD_NIL",
        [VREG_LOAD_TRUE] = "LOAD_T",
        [VREG_LOAD_FALSE] = "LOAD_F",
        [VREG_ADD] = "ADD",
        [VREG_SUB] = "SUB",
        [VREG_MUL] = "MUL",
        [VREG_DIV] = "DIV",
        [VREG_MOD] = "MOD",
        [VREG_ADD_I32] = "ADD_I32",
        [VREG_SUB_I32] = "SUB_I32",
        [VREG_MUL_I32] = "MUL_I32",
        [VREG_DIV_I32] = "DIV_I32",
        [VREG_MOD_I32] = "MOD_I32",
        [VREG_ADD_I64] = "ADD_I64",
        [VREG_SUB_I64] = "SUB_I64",
        [VREG_MUL_I64] = "MUL_I64",
        [VREG_DIV_I64] = "DIV_I64",
        [VREG_MOD_I64] = "MOD_I64",
        [VREG_ADD_F64] = "ADD_F64",
        [VREG_SUB_F64] = "SUB_F64",
        [VREG_MUL_F64] = "MUL_F64",
        [VREG_DIV_F64] = "DIV_F64",
        [VREG_LT_I32] = "LT_I32",
        [VREG_LE_I32] = "LE_I32",
        [VREG_GT_I32] = "GT_I32",
        [VREG_GE_I32] = "GE_I32",
        [VREG_EQ_I32] = "EQ_I32",
        [VREG_NE_I32] = "NE_I32",
        [VREG_LT_I64] = "LT_I64",
        [VREG_LE_I64] = "LE_I64",
        [VREG_GT_I64] = "GT_I64",
        [VREG_GE_I64] = "GE_I64",
        [VREG_EQ_I64] = "EQ_I64",
        [VREG_NE_I64] = "NE_I64",
        [VREG_NEG] = "NEG",
        [VREG_NOT] = "NOT",
        [VREG_BNOT] = "BNOT",
        [VREG_EQ] = "EQ",
        [VREG_LT] = "LT",
        [VREG_LE] = "LE",
        [VREG_JMP] = "JMP",
        [VREG_TEST] = "TEST",
        [VREG_LT_I32_JMP] = "LT_I32_JMP",
        [VREG_LE_I32_JMP] = "LE_I32_JMP",
        [VREG_GT_I32_JMP] = "GT_I32_JMP",
        [VREG_GE_I32_JMP] = "GE_I32_JMP",
        [VREG_EQ_I32_JMP] = "EQ_I32_JMP",
        [VREG_NE_I32_JMP] = "NE_I32_JMP",
        [VREG_FORLOOP_I32] = "FORLOOP32",
        [VREG_FORLOOP_I64] = "FORLOOP64",
        [VREG_INC_I32] = "INC_I32",
        [VREG_INC_I64] = "INC_I64",
        [VREG_RETURN] = "RETURN",
        [VREG_CALL] = "CALL",
        [VREG_MATH_SIN] = "SIN",
        [VREG_MATH_COS] = "COS",
        [VREG_MATH_SQRT] = "SQRT",
        [VREG_MATH_LOG] = "LOG",
        [VREG_MATH_POW] = "POW",
        [VREG_DUP] = "DUP",
        [VREG_TO_I32] = "TO_I32",
        [VREG_TO_I64] = "TO_I64",
        [VREG_TO_F64] = "TO_F64",
    };
    if (op < sizeof(names) / sizeof(names[0]) && names[op])
        return names[op];
    return "???";
}

void vigil_reg_dump(const vigil_reg_chunk_t *rc)
{
    fprintf(stderr, "=== Register instructions: %zu, max_regs: %u ===\n", rc->code_count, rc->max_registers);
    for (size_t i = 0; i < rc->code_count; i++)
    {
        vigil_reg_instr_t instr = rc->code[i];
        uint8_t op = VREG_GET_OP(instr);
        uint8_t a = VREG_GET_A(instr);
        uint8_t b = VREG_GET_B(instr);
        uint8_t c = VREG_GET_C(instr);
        int16_t sbx = VREG_GET_sBx(instr);
        uint16_t bx = VREG_GET_Bx(instr);

        if (op == VREG_JMP)
            fprintf(stderr, "  [%2zu] %-12s sBx=%d (→%zu)\n", i, reg_op_name(op), sbx, (size_t)((int)i + 1 + sbx));
        else if (op == VREG_LOAD_K)
            fprintf(stderr, "  [%2zu] %-12s R%u = K[%u]\n", i, reg_op_name(op), a, bx);
        else if (op == VREG_TEST)
            fprintf(stderr, "  [%2zu] %-12s R%u\n", i, reg_op_name(op), a);
        else if (op == VREG_RETURN)
            fprintf(stderr, "  [%2zu] %-12s R%u count=%u\n", i, reg_op_name(op), a, b);
        else
            fprintf(stderr, "  [%2zu] %-12s R%u R%u R%u (op=%u)\n", i, reg_op_name(op), a, b, c, op);
    }
    fprintf(stderr, "===\n");
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

vigil_status_t vigil_regvm_execute(vigil_vm_t *vm, const vigil_reg_chunk_t *rc, vigil_value_t *out_value,
                                   vigil_error_t *error)
{
    if (!vm || !rc || !out_value)
        return VIGIL_STATUS_INVALID_ARGUMENT;

    const vigil_reg_instr_t *code = rc->code;
    const size_t code_count = rc->code_count;
    (void)code_count;
    const vigil_chunk_t *sc = rc->stack_chunk;
    vigil_value_t *R = vm->stack; /* register file = stack base */
    size_t ip = 0;
    vigil_status_t status = VIGIL_STATUS_OK;

    /* Ensure stack has room for registers. */
    if (vm->stack_capacity < (size_t)rc->max_registers + 16)
    {
        status = vigil_vm_grow_stack(vm, (size_t)rc->max_registers + 16, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        R = vm->stack;
    }

    /* Set up initial frame if needed. */
    vigil_vm_frame_t *frame = &vm->frames[vm->frame_count - 1];
    size_t base = frame->base_slot;
    R = vm->stack + base;

    /* Ensure stack_count covers all registers. */
    if (vm->stack_count < base + rc->max_registers)
        vm->stack_count = base + rc->max_registers;

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
            [VREG_MATH_SIN] = &&r_MATH_SIN,
            [VREG_MATH_COS] = &&r_MATH_COS,
            [VREG_MATH_SQRT] = &&r_MATH_SQRT,
            [VREG_MATH_LOG] = &&r_MATH_LOG,
            [VREG_MATH_POW] = &&r_MATH_POW,
            [VREG_EQ] = &&r_EQ,
            [VREG_LT] = &&r_LT,
            [VREG_LE] = &&r_LE,
            [VREG_DUP] = &&r_DUP,
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

#define RDISPATCH() goto *dtable[VREG_GET_OP(code[ip])]
#define RNEXT()                                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        ip++;                                                                                                          \
        RDISPATCH();                                                                                                   \
    } while (0)
#define RCASE(op) r_##op:
#else
#define RDISPATCH() break
#define RNEXT() break
#define RCASE(op) case VREG_##op:
#endif

#if REGVM_COMPUTED_GOTO
    RDISPATCH();
#else
    while (ip < code_count)
    {
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
                VIGIL_VM_VALUE_COPY(&R[VREG_GET_A(i)], k);
            else
                R[VREG_GET_A(i)] = *k;
        }
        RNEXT();
    }
    RCASE(MOVE)
    {
        vigil_reg_instr_t i = code[ip];
        if (vigil_nanbox_has_object(R[VREG_GET_B(i)]))
            VIGIL_VM_VALUE_COPY(&R[VREG_GET_A(i)], &R[VREG_GET_B(i)]);
        else
            R[VREG_GET_A(i)] = R[VREG_GET_B(i)];
        RNEXT();
    }
    RCASE(LOAD_NIL)
    {
        R[VREG_GET_A(code[ip])] = VIGIL_NANBOX_NIL;
        RNEXT();
    }
    RCASE(LOAD_TRUE)
    {
        R[VREG_GET_A(code[ip])] = VIGIL_NANBOX_TRUE;
        RNEXT();
    }
    RCASE(LOAD_FALSE)
    {
        R[VREG_GET_A(code[ip])] = VIGIL_NANBOX_FALSE;
        RNEXT();
    }
    RCASE(DUP)
    {
        vigil_reg_instr_t i = code[ip];
        VIGIL_VM_VALUE_COPY(&R[VREG_GET_A(i)], &R[VREG_GET_B(i)]);
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
        R[VREG_GET_A(i)] = vigil_nanbox_encode_i32(r);
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
        R[VREG_GET_A(i)] = vigil_nanbox_encode_i32(r);
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
        R[VREG_GET_A(i)] = vigil_nanbox_encode_i32(r);
        RNEXT();
    }
    RCASE(DIV_I32)
    {
        vigil_reg_instr_t i = code[ip];
        int32_t a = vigil_nanbox_decode_i32(R[VREG_GET_B(i)]);
        int32_t b = vigil_nanbox_decode_i32(R[VREG_GET_C(i)]);
        if (VIGIL_UNLIKELY(b == 0))
            goto r_divzero;
        R[VREG_GET_A(i)] = vigil_nanbox_encode_i32(a / b);
        RNEXT();
    }
    RCASE(MOD_I32)
    {
        vigil_reg_instr_t i = code[ip];
        int32_t a = vigil_nanbox_decode_i32(R[VREG_GET_B(i)]);
        int32_t b = vigil_nanbox_decode_i32(R[VREG_GET_C(i)]);
        if (VIGIL_UNLIKELY(b == 0))
            goto r_divzero;
        R[VREG_GET_A(i)] = vigil_nanbox_encode_i32(a % b);
        RNEXT();
    }

    /* ── i32 comparisons ───────────────────────────────────────── */
    RCASE(LT_I32)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_from_bool(vigil_nanbox_decode_i32(R[VREG_GET_B(i)]) <
                                                  vigil_nanbox_decode_i32(R[VREG_GET_C(i)]));
        RNEXT();
    }
    RCASE(LE_I32)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_from_bool(vigil_nanbox_decode_i32(R[VREG_GET_B(i)]) <=
                                                  vigil_nanbox_decode_i32(R[VREG_GET_C(i)]));
        RNEXT();
    }
    RCASE(GT_I32)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_from_bool(vigil_nanbox_decode_i32(R[VREG_GET_B(i)]) >
                                                  vigil_nanbox_decode_i32(R[VREG_GET_C(i)]));
        RNEXT();
    }
    RCASE(GE_I32)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_from_bool(vigil_nanbox_decode_i32(R[VREG_GET_B(i)]) >=
                                                  vigil_nanbox_decode_i32(R[VREG_GET_C(i)]));
        RNEXT();
    }
    RCASE(EQ_I32)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_from_bool(vigil_nanbox_decode_i32(R[VREG_GET_B(i)]) ==
                                                  vigil_nanbox_decode_i32(R[VREG_GET_C(i)]));
        RNEXT();
    }
    RCASE(NE_I32)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_from_bool(vigil_nanbox_decode_i32(R[VREG_GET_B(i)]) !=
                                                  vigil_nanbox_decode_i32(R[VREG_GET_C(i)]));
        RNEXT();
    }

    /* ── i64 arithmetic ────────────────────────────────────────── */
    RCASE(ADD_I64)
    {
        vigil_reg_instr_t i = code[ip];
        int64_t a = vigil_nanbox_decode_int(R[VREG_GET_B(i)]);
        int64_t b = vigil_nanbox_decode_int(R[VREG_GET_C(i)]);
        int64_t r;
        if (VIGIL_UNLIKELY(vigil_vm_checked_add(a, b, &r) != VIGIL_STATUS_OK))
            goto r_overflow;
        R[VREG_GET_A(i)] = vigil_nanbox_encode_int(r);
        RNEXT();
    }
    RCASE(SUB_I64)
    {
        vigil_reg_instr_t i = code[ip];
        int64_t a = vigil_nanbox_decode_int(R[VREG_GET_B(i)]);
        int64_t b = vigil_nanbox_decode_int(R[VREG_GET_C(i)]);
        int64_t r;
        if (VIGIL_UNLIKELY(vigil_vm_checked_subtract(a, b, &r) != VIGIL_STATUS_OK))
            goto r_overflow;
        R[VREG_GET_A(i)] = vigil_nanbox_encode_int(r);
        RNEXT();
    }
    RCASE(MUL_I64)
    {
        vigil_reg_instr_t i = code[ip];
        int64_t a = vigil_nanbox_decode_int(R[VREG_GET_B(i)]);
        int64_t b = vigil_nanbox_decode_int(R[VREG_GET_C(i)]);
        int64_t r;
        if (VIGIL_UNLIKELY(vigil_vm_checked_multiply(a, b, &r) != VIGIL_STATUS_OK))
            goto r_overflow;
        R[VREG_GET_A(i)] = vigil_nanbox_encode_int(r);
        RNEXT();
    }
    RCASE(DIV_I64)
    {
        vigil_reg_instr_t i = code[ip];
        int64_t a = vigil_nanbox_decode_int(R[VREG_GET_B(i)]);
        int64_t b = vigil_nanbox_decode_int(R[VREG_GET_C(i)]);
        if (VIGIL_UNLIKELY(b == 0))
            goto r_divzero;
        int64_t r;
        if (VIGIL_UNLIKELY(vigil_vm_checked_divide(a, b, &r) != VIGIL_STATUS_OK))
            goto r_overflow;
        R[VREG_GET_A(i)] = vigil_nanbox_encode_int(r);
        RNEXT();
    }
    RCASE(MOD_I64)
    {
        vigil_reg_instr_t i = code[ip];
        int64_t a = vigil_nanbox_decode_int(R[VREG_GET_B(i)]);
        int64_t b = vigil_nanbox_decode_int(R[VREG_GET_C(i)]);
        if (VIGIL_UNLIKELY(b == 0))
            goto r_divzero;
        int64_t r;
        if (VIGIL_UNLIKELY(vigil_vm_checked_modulo(a, b, &r) != VIGIL_STATUS_OK))
            goto r_overflow;
        R[VREG_GET_A(i)] = vigil_nanbox_encode_int(r);
        RNEXT();
    }

    /* ── i64 comparisons ───────────────────────────────────────── */
    RCASE(LT_I64)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_from_bool(vigil_nanbox_decode_int(R[VREG_GET_B(i)]) <
                                                  vigil_nanbox_decode_int(R[VREG_GET_C(i)]));
        RNEXT();
    }
    RCASE(LE_I64)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_from_bool(vigil_nanbox_decode_int(R[VREG_GET_B(i)]) <=
                                                  vigil_nanbox_decode_int(R[VREG_GET_C(i)]));
        RNEXT();
    }
    RCASE(GT_I64)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_from_bool(vigil_nanbox_decode_int(R[VREG_GET_B(i)]) >
                                                  vigil_nanbox_decode_int(R[VREG_GET_C(i)]));
        RNEXT();
    }
    RCASE(GE_I64)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_from_bool(vigil_nanbox_decode_int(R[VREG_GET_B(i)]) >=
                                                  vigil_nanbox_decode_int(R[VREG_GET_C(i)]));
        RNEXT();
    }
    RCASE(EQ_I64)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_from_bool(vigil_nanbox_decode_int(R[VREG_GET_B(i)]) ==
                                                  vigil_nanbox_decode_int(R[VREG_GET_C(i)]));
        RNEXT();
    }
    RCASE(NE_I64)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_from_bool(vigil_nanbox_decode_int(R[VREG_GET_B(i)]) !=
                                                  vigil_nanbox_decode_int(R[VREG_GET_C(i)]));
        RNEXT();
    }

    /* ── f64 arithmetic ────────────────────────────────────────── */
    RCASE(ADD_F64)
    {
        vigil_reg_instr_t i = code[ip];
        double a = vigil_nanbox_decode_double(R[VREG_GET_B(i)]);
        double b = vigil_nanbox_decode_double(R[VREG_GET_C(i)]);
        R[VREG_GET_A(i)] = vigil_nanbox_encode_double(a + b);
        RNEXT();
    }
    RCASE(SUB_F64)
    {
        vigil_reg_instr_t i = code[ip];
        double a = vigil_nanbox_decode_double(R[VREG_GET_B(i)]);
        double b = vigil_nanbox_decode_double(R[VREG_GET_C(i)]);
        R[VREG_GET_A(i)] = vigil_nanbox_encode_double(a - b);
        RNEXT();
    }
    RCASE(MUL_F64)
    {
        vigil_reg_instr_t i = code[ip];
        double a = vigil_nanbox_decode_double(R[VREG_GET_B(i)]);
        double b = vigil_nanbox_decode_double(R[VREG_GET_C(i)]);
        R[VREG_GET_A(i)] = vigil_nanbox_encode_double(a * b);
        RNEXT();
    }
    RCASE(DIV_F64)
    {
        vigil_reg_instr_t i = code[ip];
        double a = vigil_nanbox_decode_double(R[VREG_GET_B(i)]);
        double b = vigil_nanbox_decode_double(R[VREG_GET_C(i)]);
        R[VREG_GET_A(i)] = vigil_nanbox_encode_double(a / b);
        RNEXT();
    }

    /* ── Math intrinsics ───────────────────────────────────────── */
    RCASE(MATH_SIN)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_encode_double(sin(vigil_nanbox_decode_double(R[VREG_GET_B(i)])));
        RNEXT();
    }
    RCASE(MATH_COS)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_encode_double(cos(vigil_nanbox_decode_double(R[VREG_GET_B(i)])));
        RNEXT();
    }
    RCASE(MATH_SQRT)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_encode_double(sqrt(vigil_nanbox_decode_double(R[VREG_GET_B(i)])));
        RNEXT();
    }
    RCASE(MATH_LOG)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_encode_double(log(vigil_nanbox_decode_double(R[VREG_GET_B(i)])));
        RNEXT();
    }
    RCASE(MATH_POW)
    {
        vigil_reg_instr_t i = code[ip];
        double a = vigil_nanbox_decode_double(R[VREG_GET_B(i)]);
        double b = vigil_nanbox_decode_double(R[VREG_GET_C(i)]);
        R[VREG_GET_A(i)] = vigil_nanbox_encode_double(pow(a, b));
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
        R[VREG_GET_A(i)] = vigil_nanbox_encode_i32(r);
        RNEXT();
    }
    RCASE(INC_I64)
    {
        vigil_reg_instr_t i = code[ip];
        int64_t v = vigil_nanbox_decode_int(R[VREG_GET_A(i)]);
        int64_t delta = (int64_t)(int8_t)VREG_GET_B(i);
        int64_t r;
        if (VIGIL_UNLIKELY(vigil_vm_checked_add(v, delta, &r) != VIGIL_STATUS_OK))
            goto r_overflow;
        R[VREG_GET_A(i)] = vigil_nanbox_encode_int(r);
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
        if (vigil_nanbox_decode_int(R[VREG_GET_A(i)]) < vigil_nanbox_decode_int(R[VREG_GET_B(i)]))
        {
            ip += 2;
            RDISPATCH();
        }
        ip++;
        RDISPATCH();
    }
    RCASE(LE_I64_JMP)
    {
        vigil_reg_instr_t i = code[ip];
        if (vigil_nanbox_decode_int(R[VREG_GET_A(i)]) <= vigil_nanbox_decode_int(R[VREG_GET_B(i)]))
        {
            ip += 2;
            RDISPATCH();
        }
        ip++;
        RDISPATCH();
    }
    RCASE(GT_I64_JMP)
    {
        vigil_reg_instr_t i = code[ip];
        if (vigil_nanbox_decode_int(R[VREG_GET_A(i)]) > vigil_nanbox_decode_int(R[VREG_GET_B(i)]))
        {
            ip += 2;
            RDISPATCH();
        }
        ip++;
        RDISPATCH();
    }
    RCASE(GE_I64_JMP)
    {
        vigil_reg_instr_t i = code[ip];
        if (vigil_nanbox_decode_int(R[VREG_GET_A(i)]) >= vigil_nanbox_decode_int(R[VREG_GET_B(i)]))
        {
            ip += 2;
            RDISPATCH();
        }
        ip++;
        RDISPATCH();
    }
    RCASE(EQ_I64_JMP)
    {
        vigil_reg_instr_t i = code[ip];
        if (vigil_nanbox_decode_int(R[VREG_GET_A(i)]) == vigil_nanbox_decode_int(R[VREG_GET_B(i)]))
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
        if (vigil_nanbox_decode_int(R[VREG_GET_A(i)]) != vigil_nanbox_decode_int(R[VREG_GET_B(i)]))
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
        R[idx] = vigil_nanbox_encode_i32(r);
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
        int64_t limit = vigil_nanbox_decode_int(*kv);
        int64_t val = vigil_nanbox_decode_int(R[idx]);
        int64_t r;
        if (VIGIL_UNLIKELY(vigil_vm_checked_add(val, delta, &r) != VIGIL_STATUS_OK))
            goto r_overflow;
        R[idx] = vigil_nanbox_encode_int(r);
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
        /* Try i64 fast path. */
        if (vigil_nanbox_is_int_inline(R[ra]) && vigil_nanbox_is_int_inline(R[rb]))
        {
            int64_t a = vigil_nanbox_decode_int(R[ra]);
            int64_t b = vigil_nanbox_decode_int(R[rb]);
            int64_t r;
            if (VIGIL_LIKELY(vigil_vm_checked_add(a, b, &r) == VIGIL_STATUS_OK))
            {
                R[rd] = vigil_nanbox_encode_int(r);
                RNEXT();
            }
        }
        /* Try f64 fast path. */
        if (vigil_nanbox_is_double(R[ra]) && vigil_nanbox_is_double(R[rb]))
        {
            R[rd] = vigil_nanbox_encode_double(vigil_nanbox_decode_double(R[ra]) + vigil_nanbox_decode_double(R[rb]));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }
    RCASE(SUB)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ra = VREG_GET_B(i), rb = VREG_GET_C(i), rd = VREG_GET_A(i);
        if (vigil_nanbox_is_int_inline(R[ra]) && vigil_nanbox_is_int_inline(R[rb]))
        {
            int64_t a = vigil_nanbox_decode_int(R[ra]);
            int64_t b = vigil_nanbox_decode_int(R[rb]);
            int64_t r;
            if (VIGIL_LIKELY(vigil_vm_checked_subtract(a, b, &r) == VIGIL_STATUS_OK))
            {
                R[rd] = vigil_nanbox_encode_int(r);
                RNEXT();
            }
        }
        if (vigil_nanbox_is_double(R[ra]) && vigil_nanbox_is_double(R[rb]))
        {
            R[rd] = vigil_nanbox_encode_double(vigil_nanbox_decode_double(R[ra]) - vigil_nanbox_decode_double(R[rb]));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }
    RCASE(MUL)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ra = VREG_GET_B(i), rb = VREG_GET_C(i), rd = VREG_GET_A(i);
        if (vigil_nanbox_is_int_inline(R[ra]) && vigil_nanbox_is_int_inline(R[rb]))
        {
            int64_t a = vigil_nanbox_decode_int(R[ra]);
            int64_t b = vigil_nanbox_decode_int(R[rb]);
            int64_t r;
            if (VIGIL_LIKELY(vigil_vm_checked_multiply(a, b, &r) == VIGIL_STATUS_OK))
            {
                R[rd] = vigil_nanbox_encode_int(r);
                RNEXT();
            }
        }
        if (vigil_nanbox_is_double(R[ra]) && vigil_nanbox_is_double(R[rb]))
        {
            R[rd] = vigil_nanbox_encode_double(vigil_nanbox_decode_double(R[ra]) * vigil_nanbox_decode_double(R[rb]));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }
    RCASE(DIV)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ra = VREG_GET_B(i), rb = VREG_GET_C(i), rd = VREG_GET_A(i);
        if (vigil_nanbox_is_int_inline(R[ra]) && vigil_nanbox_is_int_inline(R[rb]))
        {
            int64_t a = vigil_nanbox_decode_int(R[ra]);
            int64_t b = vigil_nanbox_decode_int(R[rb]);
            if (VIGIL_UNLIKELY(b == 0))
                goto r_divzero;
            int64_t r;
            if (VIGIL_LIKELY(vigil_vm_checked_divide(a, b, &r) == VIGIL_STATUS_OK))
            {
                R[rd] = vigil_nanbox_encode_int(r);
                RNEXT();
            }
        }
        if (vigil_nanbox_is_double(R[ra]) && vigil_nanbox_is_double(R[rb]))
        {
            R[rd] = vigil_nanbox_encode_double(vigil_nanbox_decode_double(R[ra]) / vigil_nanbox_decode_double(R[rb]));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }
    RCASE(MOD)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ra = VREG_GET_B(i), rb = VREG_GET_C(i), rd = VREG_GET_A(i);
        if (vigil_nanbox_is_int_inline(R[ra]) && vigil_nanbox_is_int_inline(R[rb]))
        {
            int64_t a = vigil_nanbox_decode_int(R[ra]);
            int64_t b = vigil_nanbox_decode_int(R[rb]);
            if (VIGIL_UNLIKELY(b == 0))
                goto r_divzero;
            int64_t r;
            if (VIGIL_LIKELY(vigil_vm_checked_modulo(a, b, &r) == VIGIL_STATUS_OK))
            {
                R[rd] = vigil_nanbox_encode_int(r);
                RNEXT();
            }
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }
    RCASE(EQ)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_from_bool(vigil_vm_values_equal(&R[VREG_GET_B(i)], &R[VREG_GET_C(i)]));
        RNEXT();
    }
    RCASE(LT)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ra = VREG_GET_B(i), rb = VREG_GET_C(i);
        if (vigil_nanbox_is_int_inline(R[ra]) && vigil_nanbox_is_int_inline(R[rb]))
        {
            R[VREG_GET_A(i)] = vigil_nanbox_from_bool(vigil_nanbox_decode_int(R[ra]) < vigil_nanbox_decode_int(R[rb]));
            RNEXT();
        }
        if (vigil_nanbox_is_double(R[ra]) && vigil_nanbox_is_double(R[rb]))
        {
            R[VREG_GET_A(i)] =
                vigil_nanbox_from_bool(vigil_nanbox_decode_double(R[ra]) < vigil_nanbox_decode_double(R[rb]));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }
    RCASE(LE)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ra = VREG_GET_B(i), rb = VREG_GET_C(i);
        if (vigil_nanbox_is_int_inline(R[ra]) && vigil_nanbox_is_int_inline(R[rb]))
        {
            R[VREG_GET_A(i)] = vigil_nanbox_from_bool(vigil_nanbox_decode_int(R[ra]) <= vigil_nanbox_decode_int(R[rb]));
            RNEXT();
        }
        if (vigil_nanbox_is_double(R[ra]) && vigil_nanbox_is_double(R[rb]))
        {
            R[VREG_GET_A(i)] =
                vigil_nanbox_from_bool(vigil_nanbox_decode_double(R[ra]) <= vigil_nanbox_decode_double(R[rb]));
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
        if (vigil_nanbox_is_int_inline(R[src]))
        {
            int64_t v = vigil_nanbox_decode_int(R[src]);
            int64_t r;
            if (VIGIL_LIKELY(vigil_vm_checked_negate(v, &r) == VIGIL_STATUS_OK))
            {
                R[VREG_GET_A(i)] = vigil_nanbox_encode_int(r);
                RNEXT();
            }
        }
        if (vigil_nanbox_is_double(R[src]))
        {
            R[VREG_GET_A(i)] = vigil_nanbox_encode_double(-vigil_nanbox_decode_double(R[src]));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }
    RCASE(NOT)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] =
            vigil_nanbox_from_bool(R[VREG_GET_B(i)] == VIGIL_NANBOX_FALSE || R[VREG_GET_B(i)] == VIGIL_NANBOX_NIL);
        RNEXT();
    }
    RCASE(BNOT)
    {
        vigil_reg_instr_t i = code[ip];
        if (vigil_nanbox_is_int_inline(R[VREG_GET_B(i)]))
        {
            R[VREG_GET_A(i)] = vigil_nanbox_encode_int(~vigil_nanbox_decode_int(R[VREG_GET_B(i)]));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }

    /* ── Bitwise ops ───────────────────────────────────────────── */
    RCASE(BAND)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_encode_int(vigil_nanbox_decode_int(R[VREG_GET_B(i)]) &
                                                   vigil_nanbox_decode_int(R[VREG_GET_C(i)]));
        RNEXT();
    }
    RCASE(BOR)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_encode_int(vigil_nanbox_decode_int(R[VREG_GET_B(i)]) |
                                                   vigil_nanbox_decode_int(R[VREG_GET_C(i)]));
        RNEXT();
    }
    RCASE(BXOR)
    {
        vigil_reg_instr_t i = code[ip];
        R[VREG_GET_A(i)] = vigil_nanbox_encode_int(vigil_nanbox_decode_int(R[VREG_GET_B(i)]) ^
                                                   vigil_nanbox_decode_int(R[VREG_GET_C(i)]));
        RNEXT();
    }
    RCASE(SHL)
    {
        vigil_reg_instr_t i = code[ip];
        int64_t a = vigil_nanbox_decode_int(R[VREG_GET_B(i)]);
        int64_t b = vigil_nanbox_decode_int(R[VREG_GET_C(i)]);
        int64_t r;
        if (VIGIL_UNLIKELY(vigil_vm_checked_shift_left(a, b, &r) != VIGIL_STATUS_OK))
            goto r_overflow;
        R[VREG_GET_A(i)] = vigil_nanbox_encode_int(r);
        RNEXT();
    }
    RCASE(SHR)
    {
        vigil_reg_instr_t i = code[ip];
        int64_t a = vigil_nanbox_decode_int(R[VREG_GET_B(i)]);
        int64_t b = vigil_nanbox_decode_int(R[VREG_GET_C(i)]);
        int64_t r;
        if (VIGIL_UNLIKELY(vigil_vm_checked_shift_right(a, b, &r) != VIGIL_STATUS_OK))
            goto r_overflow;
        R[VREG_GET_A(i)] = vigil_nanbox_encode_int(r);
        RNEXT();
    }

    /* ── Type conversions ──────────────────────────────────────── */
    RCASE(TO_I32)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t src = VREG_GET_B(i);
        if (vigil_nanbox_is_int_inline(R[src]))
        {
            int64_t v = vigil_nanbox_decode_int(R[src]);
            R[VREG_GET_A(i)] = vigil_nanbox_encode_i32((int32_t)v);
            RNEXT();
        }
        if (vigil_nanbox_is_double(R[src]))
        {
            R[VREG_GET_A(i)] = vigil_nanbox_encode_i32((int32_t)vigil_nanbox_decode_double(R[src]));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }
    RCASE(TO_I64)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t src = VREG_GET_B(i);
        if (vigil_nanbox_is_int_inline(R[src]))
        {
            /* Already an inline int — no-op. */
            R[VREG_GET_A(i)] = R[src];
            RNEXT();
        }
        if (vigil_nanbox_is_double(R[src]))
        {
            R[VREG_GET_A(i)] = vigil_nanbox_encode_int((int64_t)vigil_nanbox_decode_double(R[src]));
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
            R[VREG_GET_A(i)] = R[src];
            RNEXT();
        }
        if (vigil_nanbox_is_int_inline(R[src]))
        {
            R[VREG_GET_A(i)] = vigil_nanbox_encode_double((double)vigil_nanbox_decode_int(R[src]));
            RNEXT();
        }
        status = VIGIL_STATUS_UNSUPPORTED;
        goto r_cleanup;
    }

    /* ── Native call ───────────────────────────────────────────── */
    RCASE(CALL_NATIVE)
    {
        vigil_reg_instr_t i = code[ip];
        uint8_t ret_reg = VREG_GET_A(i);
        uint8_t ci = VREG_GET_B(i);
        uint8_t arg_count = VREG_GET_C(i);

        /* Sync stack: args are at registers ret_reg .. ret_reg+arg_count-1. */
        vm->stack_count = base + (size_t)ret_reg + (size_t)arg_count;

        const vigil_value_t *native_val = VIGIL_VM_CHUNK_CONSTANT(sc, (size_t)ci);
        if (!native_val || !vigil_nanbox_has_object(*native_val))
        {
            status = VIGIL_STATUS_UNSUPPORTED;
            goto r_cleanup;
        }
        vigil_object_t *native_obj = (vigil_object_t *)vigil_nanbox_decode_ptr(*native_val);
        vigil_native_fn_t native_fn = vigil_native_function_get(native_obj);
        if (!native_fn)
        {
            status = VIGIL_STATUS_UNSUPPORTED;
            goto r_cleanup;
        }
        status = native_fn(vm, (size_t)arg_count, error);
        if (status != VIGIL_STATUS_OK)
            goto r_cleanup;

        /* Native function modified the stack. The results are at
           stack positions ret_reg .. ret_reg + return_count - 1.
           Since R[] = vm->stack + base, the results are already
           in the right registers. Just ensure stack_count covers
           the register window. */
        if (vm->stack_count < base + rc->max_registers)
            vm->stack_count = base + rc->max_registers;

        RNEXT();
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
        vm->stack_count = base + (size_t)base_r + 1U;
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

        r_overflow: vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "integer overflow in register VM");
    return VIGIL_STATUS_INVALID_ARGUMENT;

r_divzero:
    vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "division by zero in register VM");
    return VIGIL_STATUS_INVALID_ARGUMENT;

r_cleanup:
    return status;
}
