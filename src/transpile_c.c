/*
 * transpile_c.c — Vigil-to-C transpiler.
 *
 * Walks vigil_reg_chunk_t instruction sequences and emits portable C11
 * source.  Covers numeric ops (Phase 1) and string/error/defer ops
 * (Phase 2).
 */

#include "internal/vigil_transpile_c.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "internal/vigil_regvm.h"
#include "vigil/chunk.h"
#include "vigil/transpile.h"
#include "vigil/value.h"

/* ── String builder helper ───────────────────────────────────────── */

static vigil_status_t emit(vigil_transpile_ctx_t *ctx, const char *text)
{
    return vigil_string_append_cstr(ctx->output, text, ctx->error);
}

static vigil_status_t emitf(vigil_transpile_ctx_t *ctx, const char *format, ...)
{
    char buf[512];
    va_list args;
    int written;

    va_start(args, format);
    written = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= sizeof(buf))
    {
        vigil_error_set_literal(ctx->error, VIGIL_STATUS_INTERNAL, "transpile: format buffer overflow");
        return VIGIL_STATUS_INTERNAL;
    }
    return vigil_string_append(ctx->output, buf, (size_t)written, ctx->error);
}

#define EMIT(text)                                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        status = emit(ctx, text);                                                                                      \
        if (status != VIGIL_STATUS_OK)                                                                                 \
            return status;                                                                                             \
    } while (0)

#define EMITF(...)                                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        status = emitf(ctx, __VA_ARGS__);                                                                              \
        if (status != VIGIL_STATUS_OK)                                                                                 \
            return status;                                                                                             \
    } while (0)

/* ── Opcode emission helpers ─────────────────────────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static vigil_status_t emit_load_k(vigil_transpile_ctx_t *ctx, const vigil_reg_chunk_t *rc, uint8_t a, uint16_t bx)
{
    vigil_status_t status;
    const vigil_chunk_t *sc = rc->stack_chunk;
    if (sc == NULL || (size_t)bx >= vigil_chunk_constant_count(sc))
    {
        EMITF("    r[%u].i = 0; /* missing constant */\n", a);
        return VIGIL_STATUS_OK;
    }

    const vigil_value_t *k = vigil_chunk_constant(sc, (size_t)bx);
    vigil_value_kind_t kind = vigil_value_kind(k);
    if (kind == VIGIL_VALUE_INT)
        EMITF("    r[%u].i = (int64_t)%lldLL;\n", a, (long long)vigil_value_as_int(k));
    else if (kind == VIGIL_VALUE_BOOL)
        EMITF("    r[%u].v = %s;\n", a, vigil_value_as_bool(k) ? "VIGIL_NANBOX_TRUE" : "VIGIL_NANBOX_FALSE");
    else if (kind == VIGIL_VALUE_UINT)
        EMITF("    r[%u].v = vigil_nanbox_encode_uint(%lluULL);\n", a, (unsigned long long)vigil_value_as_uint(k));
    else if (kind == VIGIL_VALUE_FLOAT)
        EMITF("    r[%u].f = %.17g;\n", a, vigil_value_as_float(k));
    else if (kind == VIGIL_VALUE_OBJECT)
    {
        vigil_object_t *obj = vigil_value_as_object(k);
        if (obj != NULL && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
        {
            const char *s = vigil_string_object_c_str(obj);
            size_t len = vigil_string_object_length(obj);
            /* Emit as string object creation. Escape the C string. */
            EMITF("    { vigil_object_t *_s = NULL; vigil_string_object_new(tc->runtime, \"");
            for (size_t si = 0; si < len; si++)
            {
                unsigned char ch = (unsigned char)s[si];
                if (ch == '"' || ch == '\\')
                    EMITF("\\%c", ch);
                else if (ch == '\n')
                    EMIT("\\n");
                else if (ch == '\r')
                    EMIT("\\r");
                else if (ch == '\t')
                    EMIT("\\t");
                else if (ch < 0x20)
                    EMITF("\\x%02x", ch);
                else
                    EMITF("%c", ch);
            }
            EMITF("\", %zu, &_s, NULL); vigil_value_init_object(&r[%u].v, &_s); }\n", len, a);
        }
        else
        {
            /* Non-string object constant — copy from constant pool */
            EMITF("    r[%u].v = vigil_value_copy(&tc->constants[%u]);\n", a, (unsigned)bx);
        }
    }
    else
        EMITF("    r[%u].i = 0; /* unsupported constant kind */\n", a);

    return VIGIL_STATUS_OK;
}

static vigil_status_t emit_i32_arith(vigil_transpile_ctx_t *ctx, const char *op_str, uint8_t a, uint8_t b, uint8_t c)
{
    vigil_status_t status;
    EMITF("    r[%u].i = (int64_t)((int32_t)r[%u].i %s (int32_t)r[%u].i);\n", a, b, op_str, c);
    return VIGIL_STATUS_OK;
}

static vigil_status_t emit_i64_arith(vigil_transpile_ctx_t *ctx, const char *op_str, uint8_t a, uint8_t b, uint8_t c)
{
    vigil_status_t status;
    EMITF("    r[%u].i = r[%u].i %s r[%u].i;\n", a, b, op_str, c);
    return VIGIL_STATUS_OK;
}

static vigil_status_t emit_f64_arith(vigil_transpile_ctx_t *ctx, const char *op_str, uint8_t a, uint8_t b, uint8_t c)
{
    vigil_status_t status;
    EMITF("    r[%u].f = r[%u].f %s r[%u].f;\n", a, b, op_str, c);
    return VIGIL_STATUS_OK;
}

static vigil_status_t emit_i32_cmp(vigil_transpile_ctx_t *ctx, const char *op_str, uint8_t a, uint8_t b, uint8_t c)
{
    vigil_status_t status;
    EMITF("    r[%u].v = ((int32_t)r[%u].i %s (int32_t)r[%u].i) ? VIGIL_NANBOX_TRUE : VIGIL_NANBOX_FALSE;\n",
          a, b, op_str, c);
    return VIGIL_STATUS_OK;
}

static vigil_status_t emit_i64_cmp(vigil_transpile_ctx_t *ctx, const char *op_str, uint8_t a, uint8_t b, uint8_t c)
{
    vigil_status_t status;
    EMITF("    r[%u].v = (r[%u].i %s r[%u].i) ? VIGIL_NANBOX_TRUE : VIGIL_NANBOX_FALSE;\n", a, b, op_str, c);
    return VIGIL_STATUS_OK;
}

static vigil_status_t emit_cmp_jmp_i32(vigil_transpile_ctx_t *ctx, const char *op_str,
                                        uint8_t a, uint8_t b, size_t ip)
{
    vigil_status_t status;
    EMITF("    if ((int32_t)r[%u].i %s (int32_t)r[%u].i) goto L_%zu;\n", a, op_str, b, ip + 2);
    return VIGIL_STATUS_OK;
}

static vigil_status_t emit_cmp_jmp_i64(vigil_transpile_ctx_t *ctx, const char *op_str,
                                        uint8_t a, uint8_t b, size_t ip)
{
    vigil_status_t status;
    EMITF("    if (r[%u].i %s r[%u].i) goto L_%zu;\n", a, op_str, b, ip + 2);
    return VIGIL_STATUS_OK;
}

/* ── Peephole: eliminate redundant VREG_MOVE ─────────────────────── */

/* Return the number of extra words consumed by an opcode (multi-word instructions). */
static size_t opcode_extra_words(uint8_t op)
{
    switch ((vigil_reg_op_t)op)
    {
    case VREG_CALL_NATIVE:
    case VREG_CALL_SELF:
    case VREG_CALL_EXTERN:
    case VREG_CALL_VALUE:
    case VREG_NEW_INSTANCE:
        return 1;
    case VREG_CALL_INTERFACE:
    case VREG_FORMAT_SPEC:
    case VREG_DEFER:
        return 2;
    default:
        return 0;
    }
}

/*
 * For a given opcode, return which of the A/B/C fields are *source* (read)
 * operands.  Bit 0 = A is source, bit 1 = B is source, bit 2 = C is source.
 * Returns 0 for opcodes we don't want to optimize (complex/multi-word/fallback).
 */
static unsigned opcode_src_mask(uint8_t op)
{
    switch ((vigil_reg_op_t)op)
    {
    /* A B C  — A = dest, B and C are sources */
    case VREG_ADD: case VREG_SUB: case VREG_MUL: case VREG_DIV: case VREG_MOD:
    case VREG_ADD_I32: case VREG_SUB_I32: case VREG_MUL_I32: case VREG_DIV_I32: case VREG_MOD_I32:
    case VREG_ADD_I64: case VREG_SUB_I64: case VREG_MUL_I64: case VREG_DIV_I64: case VREG_MOD_I64:
    case VREG_ADD_F64: case VREG_SUB_F64: case VREG_MUL_F64: case VREG_DIV_F64:
    case VREG_LT_I32: case VREG_LE_I32: case VREG_GT_I32: case VREG_GE_I32:
    case VREG_EQ_I32: case VREG_NE_I32:
    case VREG_LT_I64: case VREG_LE_I64: case VREG_GT_I64: case VREG_GE_I64:
    case VREG_EQ_I64: case VREG_NE_I64:
    case VREG_EQ: case VREG_LT: case VREG_LE:
    case VREG_BAND: case VREG_BOR: case VREG_BXOR: case VREG_SHL: case VREG_SHR:
    case VREG_MATH_POW:
        return 0x6; /* B, C */

    /* A B — A = dest, B is source */
    case VREG_NEG: case VREG_NOT: case VREG_BNOT:
    case VREG_TO_I32: case VREG_TO_I64: case VREG_TO_U8: case VREG_TO_U32: case VREG_TO_U64:
    case VREG_TO_F64:
    case VREG_MATH_SIN: case VREG_MATH_COS: case VREG_MATH_SQRT: case VREG_MATH_LOG:
        return 0x2; /* B */

    /* Fused compare-jump: A and B are both sources (no dest) */
    case VREG_LT_I32_JMP: case VREG_LE_I32_JMP: case VREG_GT_I32_JMP: case VREG_GE_I32_JMP:
    case VREG_EQ_I32_JMP: case VREG_NE_I32_JMP:
    case VREG_LT_I64_JMP: case VREG_LE_I64_JMP: case VREG_GT_I64_JMP: case VREG_GE_I64_JMP:
    case VREG_EQ_I64_JMP: case VREG_NE_I64_JMP:
        return 0x3; /* A, B */

    /* TEST: A is source */
    case VREG_TEST:
        return 0x1; /* A */

    /* RETURN: A is source when B != 0 */
    case VREG_RETURN:
        return 0x1; /* A (caller checks B) */

    /* Immediate arith: A = dest, B = source */
    case VREG_ADDI: case VREG_SUBI: case VREG_ADDI_I64: case VREG_SUBI_I64:
        return 0x2; /* B */

    /* LT_I32_IMM_JMP: A is source */
    case VREG_LT_I32_IMM_JMP:
        return 0x1; /* A */

    default:
        return 0; /* don't optimize */
    }
}

/*
 * Rewrite the instruction at code[ip] by substituting register `old_reg`
 * with `new_reg` in source operand positions.  Returns 1 if a substitution
 * was made.
 */
static int substitute_src_reg(vigil_reg_instr_t *code, size_t ip, uint8_t old_reg, uint8_t new_reg)
{
    vigil_reg_instr_t instr = code[ip];
    uint8_t op = VREG_GET_OP(instr);
    uint8_t a = VREG_GET_A(instr);
    uint8_t b = VREG_GET_B(instr);
    uint8_t c = VREG_GET_C(instr);
    unsigned mask = opcode_src_mask(op);
    int changed = 0;

    if (!mask)
        return 0;

    if ((mask & 0x1) && a == old_reg) { a = new_reg; changed = 1; }
    if ((mask & 0x2) && b == old_reg) { b = new_reg; changed = 1; }
    if ((mask & 0x4) && c == old_reg) { c = new_reg; changed = 1; }

    if (changed)
        code[ip] = vigil_reg_abc(op, a, b, c);
    return changed;
}

/*
 * Build a bitset of instruction indices that are jump targets.
 * Caller must free the returned array.
 */
static uint8_t *build_jump_targets(const vigil_reg_instr_t *code, size_t count)
{
    size_t bytes = (count + 7) / 8;
    vigil_allocator_t alloc = vigil_default_allocator();
    uint8_t *targets = (uint8_t *)alloc.allocate(alloc.user_data, bytes);
    if (!targets)
        return NULL;
    memset(targets, 0, bytes);

    for (size_t ip = 0; ip < count; ip++)
    {
        vigil_reg_instr_t instr = code[ip];
        uint8_t op = VREG_GET_OP(instr);
        int16_t sbx = VREG_GET_sBx(instr);
        size_t extra = opcode_extra_words(op);

        switch ((vigil_reg_op_t)op)
        {
        case VREG_JMP:
        {
            int64_t target = (int64_t)ip + 1 + (int64_t)sbx;
            if (target >= 0 && (size_t)target < count)
                targets[(size_t)target / 8] |= (uint8_t)(1U << ((size_t)target % 8));
            break;
        }
        case VREG_TEST:
        case VREG_TESTSET:
        case VREG_LT_I32_IMM_JMP:
            /* These skip the next instruction on condition */
            if (ip + 2 < count)
                targets[(ip + 2) / 8] |= (uint8_t)(1U << ((ip + 2) % 8));
            break;
        case VREG_LT_I32_JMP: case VREG_LE_I32_JMP: case VREG_GT_I32_JMP: case VREG_GE_I32_JMP:
        case VREG_EQ_I32_JMP: case VREG_NE_I32_JMP:
        case VREG_LT_I64_JMP: case VREG_LE_I64_JMP: case VREG_GT_I64_JMP: case VREG_GE_I64_JMP:
        case VREG_EQ_I64_JMP: case VREG_NE_I64_JMP:
            /* Fused compare-jump: skip next on true */
            if (ip + 2 < count)
                targets[(ip + 2) / 8] |= (uint8_t)(1U << ((ip + 2) % 8));
            break;
        case VREG_FORLOOP_I32:
        case VREG_FORLOOP_I64:
        {
            /* 3-word instruction: word3 is the JMP */
            if (ip + 2 < count)
            {
                vigil_reg_instr_t jmp_word = code[ip + 2];
                int16_t jmp_sbx = VREG_GET_sBx(jmp_word);
                int64_t target = (int64_t)(ip + 2) + 1 + (int64_t)jmp_sbx;
                if (target >= 0 && (size_t)target < count)
                    targets[(size_t)target / 8] |= (uint8_t)(1U << ((size_t)target % 8));
            }
            ip += 2; /* skip word2 and word3 */
            break;
        }
        default:
            break;
        }

        ip += extra;
    }

    return targets;
}

#define IS_JUMP_TARGET(targets, ip) ((targets)[(ip) / 8] & (1U << ((ip) % 8)))

/*
 * Peephole pass: eliminate VREG_MOVE A B where the next instruction
 * reads A as a source and ip+1 is not a jump target.  We NOP the MOVE
 * by rewriting it to VREG_MOVE A A (identity, skipped during emission).
 */
static void peephole_eliminate_moves(vigil_reg_instr_t *code, size_t count, const uint8_t *targets)
{
    for (size_t ip = 0; ip + 1 < count; ip++)
    {
        uint8_t op = VREG_GET_OP(code[ip]);
        size_t extra = opcode_extra_words(op);

        if (op != VREG_MOVE)
        {
            ip += extra;
            continue;
        }

        uint8_t move_dst = VREG_GET_A(code[ip]);
        uint8_t move_src = VREG_GET_B(code[ip]);
        size_t next = ip + 1;

        if (move_dst == move_src)
            continue; /* already identity */

        /* Don't optimize if next instruction is a jump target */
        if (IS_JUMP_TARGET(targets, next))
            continue;

        /* Don't optimize multi-word or complex next instructions */
        uint8_t next_op = VREG_GET_OP(code[next]);
        if (opcode_extra_words(next_op) > 0)
            continue;

        /* Don't eliminate MOVEs before TEST/TESTSET — the destination
           register may be needed on the conditional jump path where the
           next instruction is skipped (e.g. || short-circuit in f-strings). */
        if (next_op == VREG_TEST || next_op == VREG_TESTSET)
            continue;

        /* Don't optimize if next instruction writes to move_src
           (would change semantics if we substitute) */
        unsigned next_mask = opcode_src_mask(next_op);
        if (!next_mask)
            continue;
        uint8_t next_a = VREG_GET_A(code[next]);
        /* For instructions where A is a dest (mask doesn't include bit 0),
           check if dest == move_src */
        if (!(next_mask & 0x1) && next_a == move_src)
            continue;

        if (substitute_src_reg(code, next, move_dst, move_src))
        {
            /* NOP the move by making it identity */
            code[ip] = vigil_reg_abc(VREG_MOVE, move_dst, move_dst, 0);
        }
    }
}

/* ── Main instruction dispatch ───────────────────────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static vigil_status_t emit_instruction(vigil_transpile_ctx_t *ctx, const vigil_reg_chunk_t *rc, size_t *ip,
                                       size_t func_index, size_t func_count)
{
    vigil_status_t status;
    vigil_reg_instr_t instr = rc->code[*ip];
    uint8_t op = VREG_GET_OP(instr);
    uint8_t a = VREG_GET_A(instr);
    uint8_t b = VREG_GET_B(instr);
    uint8_t c = VREG_GET_C(instr);
    uint16_t bx = VREG_GET_Bx(instr);
    int16_t sbx = VREG_GET_sBx(instr);

    (void)func_count;

    switch ((vigil_reg_op_t)op)
    {
    /* ── Data movement ─────────────────────────────────────────── */
    case VREG_MOVE:      EMITF("    vigil_tc_move_reg(&r[%u].v, r[%u].v);\n", a, b); break;
    case VREG_LOAD_K:    return emit_load_k(ctx, rc, a, bx);
    case VREG_LOAD_NIL:  EMITF("    r[%u].v = VIGIL_NANBOX_NIL;\n", a); break;
    case VREG_LOAD_TRUE: EMITF("    r[%u].v = VIGIL_NANBOX_TRUE;\n", a); break;
    case VREG_LOAD_FALSE:EMITF("    r[%u].v = VIGIL_NANBOX_FALSE;\n", a); break;

    /* ── Typed i32 arithmetic ──────────────────────────────────── */
    case VREG_ADD_I32: return emit_i32_arith(ctx, "+", a, b, c);
    case VREG_SUB_I32: return emit_i32_arith(ctx, "-", a, b, c);
    case VREG_MUL_I32: return emit_i32_arith(ctx, "*", a, b, c);
    case VREG_DIV_I32: return emit_i32_arith(ctx, "/", a, b, c);
    case VREG_MOD_I32: return emit_i32_arith(ctx, "%", a, b, c);

    /* ── Typed i64 arithmetic ──────────────────────────────────── */
    case VREG_ADD_I64: return emit_i64_arith(ctx, "+", a, b, c);
    case VREG_SUB_I64: return emit_i64_arith(ctx, "-", a, b, c);
    case VREG_MUL_I64: return emit_i64_arith(ctx, "*", a, b, c);
    case VREG_DIV_I64: return emit_i64_arith(ctx, "/", a, b, c);
    case VREG_MOD_I64: return emit_i64_arith(ctx, "%", a, b, c);

    /* ── Typed f64 arithmetic ──────────────────────────────────── */
    case VREG_ADD_F64: return emit_f64_arith(ctx, "+", a, b, c);
    case VREG_SUB_F64: return emit_f64_arith(ctx, "-", a, b, c);
    case VREG_MUL_F64: return emit_f64_arith(ctx, "*", a, b, c);
    case VREG_DIV_F64: return emit_f64_arith(ctx, "/", a, b, c);

    /* ── Generic arithmetic ────────────────────────────────────── */
    case VREG_ADD:
        EMITF("    vigil_tc_generic_add(tc, &r[%u].v, &r[%u].v, &r[%u].v, NULL);\n", a, b, c);
        break;
    case VREG_SUB: return emit_i64_arith(ctx, "-", a, b, c);
    case VREG_MUL: return emit_i64_arith(ctx, "*", a, b, c);
    case VREG_DIV: return emit_i64_arith(ctx, "/", a, b, c);
    case VREG_MOD: return emit_i64_arith(ctx, "%", a, b, c);

    /* ── Typed i32 comparisons ─────────────────────────────────── */
    case VREG_LT_I32: return emit_i32_cmp(ctx, "<",  a, b, c);
    case VREG_LE_I32: return emit_i32_cmp(ctx, "<=", a, b, c);
    case VREG_GT_I32: return emit_i32_cmp(ctx, ">",  a, b, c);
    case VREG_GE_I32: return emit_i32_cmp(ctx, ">=", a, b, c);
    case VREG_EQ_I32: return emit_i32_cmp(ctx, "==", a, b, c);
    case VREG_NE_I32: return emit_i32_cmp(ctx, "!=", a, b, c);

    /* ── Typed i64 comparisons ─────────────────────────────────── */
    case VREG_LT_I64: return emit_i64_cmp(ctx, "<",  a, b, c);
    case VREG_LE_I64: return emit_i64_cmp(ctx, "<=", a, b, c);
    case VREG_GT_I64: return emit_i64_cmp(ctx, ">",  a, b, c);
    case VREG_GE_I64: return emit_i64_cmp(ctx, ">=", a, b, c);
    case VREG_EQ_I64: return emit_i64_cmp(ctx, "==", a, b, c);
    case VREG_NE_I64: return emit_i64_cmp(ctx, "!=", a, b, c);

    /* ── Generic comparisons ───────────────────────────────────── */
    case VREG_EQ:
        EMITF("    r[%u].v = vigil_tc_values_equal((uint64_t *)r, %u, %u) ? VIGIL_NANBOX_TRUE : VIGIL_NANBOX_FALSE;\n",
              a, b, c);
        break;
    case VREG_LT:
        EMITF("    r[%u].v = vigil_tc_values_lt((uint64_t *)r, %u, %u) ? VIGIL_NANBOX_TRUE : VIGIL_NANBOX_FALSE;\n",
              a, b, c);
        break;
    case VREG_LE:
        EMITF("    r[%u].v = vigil_tc_values_le((uint64_t *)r, %u, %u) ? VIGIL_NANBOX_TRUE : VIGIL_NANBOX_FALSE;\n",
              a, b, c);
        break;

    /* ── Unary ─────────────────────────────────────────────────── */
    case VREG_NEG:
        if (*ip > 0)
        {
            vigil_reg_instr_t prev = rc->code[*ip - 1];
            if (VREG_GET_OP(prev) == VREG_LOAD_K && VREG_GET_A(prev) == b && rc->stack_chunk != NULL)
            {
                uint16_t prev_bx = (uint16_t)VREG_GET_Bx(prev);
                if ((size_t)prev_bx < vigil_chunk_constant_count(rc->stack_chunk))
                {
                    const vigil_value_t *prev_k = vigil_chunk_constant(rc->stack_chunk, (size_t)prev_bx);
                    if (vigil_value_kind(prev_k) == VIGIL_VALUE_FLOAT)
                    {
                        EMITF("    r[%u].f = -r[%u].f;\n", a, b);
                        break;
                    }
                }
            }
        }
        EMITF("    r[%u].v = vigil_tc_negate(r[%u].v);\n", a, b);
        break;
    case VREG_NOT:  EMITF("    r[%u].v = !vigil_tc_is_truthy(r[%u].v) ? VIGIL_NANBOX_TRUE : VIGIL_NANBOX_FALSE;\n", a, b); break;
    case VREG_BNOT: EMITF("    r[%u].i = ~r[%u].i;\n", a, b); break;

    /* ── Bitwise ───────────────────────────────────────────────── */
    case VREG_BAND: return emit_i64_arith(ctx, "&",  a, b, c);
    case VREG_BOR:  return emit_i64_arith(ctx, "|",  a, b, c);
    case VREG_BXOR: return emit_i64_arith(ctx, "^",  a, b, c);
    case VREG_SHL:  return emit_i64_arith(ctx, "<<", a, b, c);
    case VREG_SHR:  return emit_i64_arith(ctx, ">>", a, b, c);

    /* ── Type conversions ──────────────────────────────────────── */
    case VREG_TO_I32: EMITF("    r[%u].i = vigil_tc_to_i32_value(r[%u].v);\n", a, b); break;
    case VREG_TO_I64: EMITF("    r[%u].i = r[%u].i;\n", a, b); break;
    case VREG_TO_U8:  EMITF("    r[%u].i = (int64_t)(uint8_t)r[%u].i;\n", a, b); break;
    case VREG_TO_U32: EMITF("    r[%u].i = (int64_t)(uint32_t)r[%u].i;\n", a, b); break;
    case VREG_TO_U64: EMITF("    r[%u].i = (int64_t)(uint64_t)r[%u].i;\n", a, b); break;
    case VREG_TO_F64: EMITF("    r[%u].f = (double)r[%u].i;\n", a, b); break;

    /* ── Control flow ──────────────────────────────────────────── */
    case VREG_JMP:
        EMITF("    goto L_%zu;\n", (size_t)((int64_t)(*ip) + 1 + (int64_t)sbx));
        break;
    case VREG_TEST:
        if (c) EMITF("    if (!vigil_tc_is_truthy(r[%u].v)) goto L_%zu;\n", a, *ip + 2);
        else   EMITF("    if (vigil_tc_is_truthy(r[%u].v)) goto L_%zu;\n", a, *ip + 2);
        break;
    case VREG_TESTSET:
        /* Short-circuit logical operators: copy the tested value to the
           result register on BOTH branches.  The original code only set
           the result on the fall-through path, leaving it uninitialized
           when the jump (short-circuit) path was taken. */
        if (c) EMITF("    if (vigil_tc_is_truthy(r[%u].v)) { r[%u] = r[%u]; } else { r[%u] = r[%u]; goto L_%zu; }\n", b, a, b, a, b, *ip + 2);
        else   EMITF("    if (!vigil_tc_is_truthy(r[%u].v)) { r[%u] = r[%u]; } else { r[%u] = r[%u]; goto L_%zu; }\n", b, a, b, a, b, *ip + 2);
        break;

    /* ── Fused compare-jump ────────────────────────────────────── */
    case VREG_LT_I32_JMP: return emit_cmp_jmp_i32(ctx, "<",  a, b, *ip);
    case VREG_LE_I32_JMP: return emit_cmp_jmp_i32(ctx, "<=", a, b, *ip);
    case VREG_GT_I32_JMP: return emit_cmp_jmp_i32(ctx, ">",  a, b, *ip);
    case VREG_GE_I32_JMP: return emit_cmp_jmp_i32(ctx, ">=", a, b, *ip);
    case VREG_EQ_I32_JMP: return emit_cmp_jmp_i32(ctx, "==", a, b, *ip);
    case VREG_NE_I32_JMP: return emit_cmp_jmp_i32(ctx, "!=", a, b, *ip);
    case VREG_LT_I64_JMP: return emit_cmp_jmp_i64(ctx, "<",  a, b, *ip);
    case VREG_LE_I64_JMP: return emit_cmp_jmp_i64(ctx, "<=", a, b, *ip);
    case VREG_GT_I64_JMP: return emit_cmp_jmp_i64(ctx, ">",  a, b, *ip);
    case VREG_GE_I64_JMP: return emit_cmp_jmp_i64(ctx, ">=", a, b, *ip);
    case VREG_EQ_I64_JMP: return emit_cmp_jmp_i64(ctx, "==", a, b, *ip);
    case VREG_NE_I64_JMP: return emit_cmp_jmp_i64(ctx, "!=", a, b, *ip);

    /* ── Calls ─────────────────────────────────────────────────── */
    case VREG_CALL:
    {
        /* Use vigil_tc_call_self for correct defer/frame handling.
           Direct C calls skip VM frame setup, which breaks defers
           registered inside the callee. */
        if (*ip + 1 >= rc->code_count) { vigil_error_set_literal(ctx->error, VIGIL_STATUS_INTERNAL, "transpile: truncated CALL"); return VIGIL_STATUS_INTERNAL; }
        uint8_t arg_base_r = a; /* CALL uses A as both arg_base and return base */
        EMITF("    vigil_tc_call_self(tc, (uint64_t *)r, %u, %u, %u, %u, NULL);\n",
              (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)arg_base_r);
        break;
    }
    case VREG_CALL_SELF:
    {
        if (*ip + 1 >= rc->code_count) { vigil_error_set_literal(ctx->error, VIGIL_STATUS_INTERNAL, "transpile: truncated CALL_SELF"); return VIGIL_STATUS_INTERNAL; }
        uint8_t arg_base_r = (uint8_t)(rc->code[*ip + 1] & 0xFF);
        *ip += 1;
        /* Use call_extern for correct multi-return handling. */
        EMITF("    vigil_tc_call_self(tc, (uint64_t *)r, %u, %zu, %u, %u, NULL);\n",
              (unsigned)a, func_index, (unsigned)b, (unsigned)arg_base_r);
        break;
    }
    case VREG_TAIL_CALL:
    {
        /* Tail calls also need multi-return support. */
        EMITF("    vigil_tc_call_self(tc, (uint64_t *)r, %u, %u, %u, %u, NULL);\n",
              (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)a);
        EMIT("    vigil_tc_drain_defers(tc, NULL);\n");
        EMITF("    return r[%u];\n", (unsigned)a);
        break;
    }
        break;
    case VREG_RETURN:
        if (b == 0) { EMIT("    vigil_tc_drain_defers(tc, NULL);\n    tc->ret_count = 0;\n    return (vigil_reg_t){0};\n"); }
        else if (b == 1) { EMIT("    vigil_tc_drain_defers(tc, NULL);\n"); EMITF("    tc->ret_count = 1;\n    return r[%u];\n", a); }
        else {
            EMIT("    vigil_tc_drain_defers(tc, NULL);\n");
            EMITF("    tc->ret_count = %u;\n", (unsigned)b);
            for (uint8_t ri = 1; ri < b; ri++)
                EMITF("    tc->ret_buf[%u] = r[%u].v;\n", (unsigned)ri, (unsigned)(a + ri));
            EMITF("    return r[%u];\n", a);
        }
        break;

    /* ── Loop superinstructions ────────────────────────────────── */
    case VREG_FORLOOP_I32:
    {
        /* 3-word instruction: word1=op+A+B+C, word2=limit const idx, word3=JMP */
        int8_t delta = (int8_t)b;
        uint8_t cmp = c;
        vigil_reg_instr_t w2 = rc->code[*ip + 1];
        uint16_t ci = VREG_GET_Bx(w2);
        vigil_reg_instr_t jmp_word = rc->code[*ip + 2];
        int16_t jmp_sbx = VREG_GET_sBx(jmp_word);
        size_t jmp_ip = *ip + 2;
        size_t target = (size_t)((int64_t)jmp_ip + 1 + (int64_t)jmp_sbx);
        const char *cmp_op = cmp == 0 ? "<" : cmp == 1 ? "<=" : cmp == 2 ? ">" : ">=";
        EMITF("    r[%u].i = (int64_t)((int32_t)r[%u].i + (int32_t)%d);\n", a, a, (int)delta);
        EMITF("    if ((int32_t)r[%u].i %s (int32_t)vigil_nanbox_decode_i32(tc->constants[%u])) goto L_%zu;\n",
              a, cmp_op, (unsigned)ci, target);
        *ip += 2; /* skip word2 and word3 */
        break;
    }
    case VREG_FORLOOP_I64:
    {
        int8_t delta = (int8_t)b;
        uint8_t cmp = c;
        vigil_reg_instr_t w2 = rc->code[*ip + 1];
        uint16_t ci = VREG_GET_Bx(w2);
        vigil_reg_instr_t jmp_word = rc->code[*ip + 2];
        int16_t jmp_sbx = VREG_GET_sBx(jmp_word);
        size_t jmp_ip = *ip + 2;
        size_t target = (size_t)((int64_t)jmp_ip + 1 + (int64_t)jmp_sbx);
        const char *cmp_op = cmp == 0 ? "<" : cmp == 1 ? "<=" : cmp == 2 ? ">" : ">=";
        EMITF("    r[%u].i = r[%u].i + (int64_t)%d;\n", a, a, (int)delta);
        EMITF("    if (r[%u].i %s vigil_nanbox_decode_int(tc->constants[%u])) goto L_%zu;\n",
              a, cmp_op, (unsigned)ci, target);
        *ip += 2;
        break;
    }
    case VREG_INC_I32:  EMITF("    r[%u].i = (int64_t)((int32_t)r[%u].i + (int32_t)%d);\n", a, a, (int)(int8_t)b); break;
    case VREG_INC_I64:  EMITF("    r[%u].i = r[%u].i + (int64_t)%d;\n", a, a, (int)(int8_t)b); break;

    /* ── Immediate arithmetic ──────────────────────────────────── */
    case VREG_ADDI:     EMITF("    r[%u].i = (int64_t)((int32_t)r[%u].i + (int32_t)%d);\n", a, b, (int)(int8_t)c); break;
    case VREG_SUBI:     EMITF("    r[%u].i = (int64_t)((int32_t)r[%u].i - (int32_t)%d);\n", a, b, (int)(int8_t)c); break;
    case VREG_ADDI_I64: EMITF("    r[%u].i = r[%u].i + (int64_t)%d;\n", a, b, (int)(int8_t)c); break;
    case VREG_SUBI_I64: EMITF("    r[%u].i = r[%u].i - (int64_t)%d;\n", a, b, (int)(int8_t)c); break;
    case VREG_LT_I32_IMM_JMP:
        EMITF("    if ((int32_t)r[%u].i < (int32_t)%d) goto L_%zu;\n", a, (int)(int8_t)c, *ip + 2);
        break;

    /* ── Math intrinsics ───────────────────────────────────────── */
    case VREG_MATH_SIN:  EMITF("    r[%u].f = sin(r[%u].f);\n", a, b); break;
    case VREG_MATH_COS:  EMITF("    r[%u].f = cos(r[%u].f);\n", a, b); break;
    case VREG_MATH_SQRT: EMITF("    r[%u].f = sqrt(r[%u].f);\n", a, b); break;
    case VREG_MATH_LOG:  EMITF("    r[%u].f = log(r[%u].f);\n", a, b); break;
    case VREG_MATH_POW:  EMITF("    r[%u].f = pow(r[%u].f, r[%u].f);\n", a, b, c); break;

    /* ── Phase 2: String/error/parse ops ───────────────────────── */
    case VREG_TO_STRING:
        EMITF("    vigil_tc_to_string(tc, &r[%u].v, &r[%u].v, NULL);\n", a, b);
        break;
    case VREG_STRING_OP:
        /* String ops: A=dest, B=top_reg (string), C=sub_opcode.
           Delegate to runtime helper that dispatches the string operation. */
        EMITF("    vigil_tc_string_op(tc, (uint64_t *)r, %u, %u, %u, NULL);\n",
              (unsigned)a, (unsigned)b, (unsigned)c);
        break;
    case VREG_FORMAT_F64:
        EMITF("    vigil_tc_format_f64(tc, &r[%u].v, &r[%u].v, %u, NULL);\n", a, b, (unsigned)c);
        break;
    case VREG_FORMAT_SPEC:
    {
        if (*ip + 2 >= rc->code_count) { vigil_error_set_literal(ctx->error, VIGIL_STATUS_INTERNAL, "transpile: truncated FORMAT_SPEC"); return VIGIL_STATUS_INTERNAL; }
        uint32_t w1 = rc->code[*ip + 1];
        uint32_t w2 = rc->code[*ip + 2];
        *ip += 2;
        EMITF("    vigil_tc_format_spec(tc, &r[%u].v, &r[%u].v, %uU, %uU, NULL);\n",
              a, b, (unsigned)w1, (unsigned)w2);
        break;
    }
    case VREG_NEW_ERROR:
        EMITF("    vigil_tc_new_error(tc, &r[%u].v, &r[%u].v, &r[%u].v, NULL);\n", a, b, c);
        break;
    case VREG_GET_ERROR_KIND:
        EMITF("    vigil_tc_get_error_kind(tc, &r[%u].v, &r[%u].v, NULL);\n", a, b);
        break;
    case VREG_GET_ERROR_MSG:
        EMITF("    vigil_tc_get_error_msg(tc, &r[%u].v, &r[%u].v, NULL);\n", a, b);
        break;
    case VREG_PARSE_I32:
        EMITF("    vigil_tc_parse_i32(tc, &r[%u].v, &r[%u].v, NULL);\n", a, b);
        break;
    case VREG_PARSE_F64:
        EMITF("    vigil_tc_parse_f64(tc, &r[%u].v, &r[%u].v, NULL);\n", a, b);
        break;
    case VREG_PARSE_BOOL:
        EMITF("    vigil_tc_parse_bool(tc, &r[%u].v, &r[%u].v, NULL);\n", a, b);
        break;
    case VREG_DEFER:
    {
        uint32_t operand_a;
        uint32_t operand_b;
        uint32_t operand_c = 0;
        if (*ip + 2 >= rc->code_count)
        {
            vigil_error_set_literal(ctx->error, VIGIL_STATUS_INTERNAL, "transpile: truncated DEFER");
            return VIGIL_STATUS_INTERNAL;
        }
        operand_a = rc->code[*ip + 1];
        operand_b = rc->code[*ip + 2];
        if (a == VIGIL_OPCODE_DEFER_CALL_INTERFACE)
        {
            if (*ip + 3 >= rc->code_count)
            {
                vigil_error_set_literal(ctx->error, VIGIL_STATUS_INTERNAL, "transpile: truncated DEFER_CALL_INTERFACE");
                return VIGIL_STATUS_INTERNAL;
            }
            operand_c = rc->code[*ip + 3];
            *ip += 3;
        }
        else
            *ip += 2;
        EMITF("    vigil_tc_defer(tc, (uint64_t *)r, %u, %u, %uU, %uU, %uU, NULL);\n",
              (unsigned)a, (unsigned)c, (unsigned)operand_a, (unsigned)operand_b, (unsigned)operand_c);
        break;
    }
    case VREG_CALL_NATIVE:
    {
        uint32_t ci;
        if (*ip + 1 >= rc->code_count) { vigil_error_set_literal(ctx->error, VIGIL_STATUS_INTERNAL, "transpile: truncated CALL_NATIVE"); return VIGIL_STATUS_INTERNAL; }
        ci = rc->code[*ip + 1];
        *ip += 1;
        EMITF("    vigil_tc_call_native(tc, (uint64_t *)r, %u, %u, %u, NULL);\n",
              (unsigned)a, (unsigned)c, (unsigned)ci);
        break;
    }

    /* ── Phase 3: Collections, arrays, maps ────────────────────── */
    case VREG_NEW_ARRAY:
        EMITF("    vigil_tc_vm_op(tc, (uint64_t *)r, %u, %u, %u, %u, NULL);\n",
              (unsigned)op, (unsigned)a, (unsigned)(bx >> 8), (unsigned)(bx & 0xFF));
        break;
    case VREG_NEW_MAP:
        EMITF("    vigil_tc_vm_op(tc, (uint64_t *)r, %u, %u, %u, %u, NULL);\n",
              (unsigned)op, (unsigned)a, (unsigned)(bx >> 8), (unsigned)(bx & 0xFF));
        break;
    case VREG_GET_INDEX:
    case VREG_SET_INDEX:
    case VREG_COLLECTION_SIZE:
    case VREG_ARRAY_PUSH:
    case VREG_ARRAY_POP:
    case VREG_ARRAY_GET_SAFE:
    case VREG_ARRAY_SET_SAFE:
    case VREG_ARRAY_SLICE:
    case VREG_ARRAY_CONTAINS:
    case VREG_ARRAY_SORT:
    case VREG_ARRAY_SORT_DESC:
    case VREG_ARRAY_REVERSE:
    case VREG_ARRAY_INDEX_OF:
    case VREG_ARRAY_REMOVE_AT:
    case VREG_ARRAY_INSERT_AT:
    case VREG_ARRAY_CLEAR:
    case VREG_MAP_KEY_AT:
    case VREG_MAP_VALUE_AT:
    case VREG_MAP_GET_SAFE:
    case VREG_MAP_SET_SAFE:
    case VREG_MAP_REMOVE_SAFE:
    case VREG_MAP_HAS:
    case VREG_MAP_KEYS:
    case VREG_MAP_VALUES:
    case VREG_MAP_CLEAR:
    case VREG_DUP:
    case VREG_RELEASE:
        EMITF("    vigil_tc_vm_op(tc, (uint64_t *)r, %u, %u, %u, %u, NULL);\n",
              (unsigned)op, (unsigned)a, (unsigned)b, (unsigned)c);
        break;

    /* ── Phase 4: Classes, interfaces, enums ───────────────────── */
    case VREG_GET_FIELD:
    case VREG_SET_FIELD:
    case VREG_CHAR_FROM_INT:
        EMITF("    vigil_tc_vm_op(tc, (uint64_t *)r, %u, %u, %u, %u, NULL);\n",
              (unsigned)op, (unsigned)a, (unsigned)b, (unsigned)c);
        break;
    case VREG_NEW_INSTANCE:
    {
        if (*ip + 1 >= rc->code_count) { vigil_error_set_literal(ctx->error, VIGIL_STATUS_INTERNAL, "transpile: truncated NEW_INSTANCE"); return VIGIL_STATUS_INTERNAL; }
        uint32_t w2 = rc->code[*ip + 1];
        uint8_t fields_base = VREG_GET_A(w2);
        uint8_t field_count = VREG_GET_B(w2);
        *ip += 1;
        /* Pass class_idx in b (high byte of bx) and c (low byte), fields_base and field_count via a second call arg pattern. */
        EMITF("    { uint16_t _ci = %u; uint8_t _fb = %u, _fc = %u;\n", (unsigned)bx, (unsigned)fields_base, (unsigned)field_count);
        EMITF("      vigil_tc_new_instance(tc, (uint64_t *)r, %u, _ci, _fb, _fc, NULL); }\n", (unsigned)a);
        break;
    }
    case VREG_CALL_INTERFACE:
    {
        if (*ip + 2 >= rc->code_count) { vigil_error_set_literal(ctx->error, VIGIL_STATUS_INTERNAL, "transpile: truncated CALL_INTERFACE"); return VIGIL_STATUS_INTERNAL; }
        uint32_t method_idx = rc->code[*ip + 1];
        uint8_t arg_base_r = (uint8_t)(rc->code[*ip + 2] & 0xFF);
        *ip += 2;
        EMITF("    vigil_tc_call_interface(tc, (uint64_t *)r, %u, %u, %u, %u, %u, NULL);\n",
              (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)method_idx, (unsigned)arg_base_r);
        break;
    }
    case VREG_CALL_EXTERN:
    {
        /* A=ret, B=const_idx, C=arg_count. Next word = arg_base. */
        if (*ip + 1 >= rc->code_count) { vigil_error_set_literal(ctx->error, VIGIL_STATUS_INTERNAL, "transpile: truncated CALL_EXTERN"); return VIGIL_STATUS_INTERNAL; }
        uint8_t arg_base_r = (uint8_t)(rc->code[*ip + 1] & 0xFF);
        *ip += 1;
        EMITF("    vigil_tc_call_extern(tc, (uint64_t *)r, %u, %u, %u, %u, NULL);\n",
              (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)arg_base_r);
        break;
    }
    case VREG_CALL_VALUE:
    {
        /* A=ret, Bx=arg_count. Next word = arg_base. */
        if (*ip + 1 >= rc->code_count) { vigil_error_set_literal(ctx->error, VIGIL_STATUS_INTERNAL, "transpile: truncated CALL_VALUE"); return VIGIL_STATUS_INTERNAL; }
        uint8_t arg_base_r = (uint8_t)(rc->code[*ip + 1] & 0xFF);
        *ip += 1;
        EMITF("    vigil_tc_call_value(tc, (uint64_t *)r, %u, %u, %u, NULL);\n",
              (unsigned)a, (unsigned)bx, (unsigned)arg_base_r);
        break;
    }

    /* -- Phase 5: Globals, captures, closures -- */
    case VREG_GET_GLOBAL:
    case VREG_SET_GLOBAL:
    case VREG_GET_CAPTURE:
    case VREG_SET_CAPTURE:
    case VREG_GET_FUNCTION:
        EMITF("    vigil_tc_vm_op(tc, (uint64_t *)r, %u, %u, %u, %u, NULL);\n",
              (unsigned)op, (unsigned)a, (unsigned)(bx >> 8), (unsigned)(bx & 0xFF));
        break;
    case VREG_NEW_CLOSURE:
        EMITF("    vigil_tc_vm_op(tc, (uint64_t *)r, %u, %u, %u, %u, NULL);\n",
              (unsigned)op, (unsigned)a, (unsigned)b, (unsigned)c);
        break;

    default:
        EMITF("    /* unsupported opcode %u */\n", (unsigned)op);
        break;
    }

    return VIGIL_STATUS_OK;
}

/* ── Function emission ───────────────────────────────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_transpile_emit_function(vigil_transpile_ctx_t *ctx, const vigil_reg_chunk_t *rc,
                                             const char *func_name, uint8_t arity, uint8_t max_regs,
                                             size_t func_index, size_t func_count)
{
    vigil_status_t status;
    vigil_reg_instr_t *code = NULL;
    uint8_t *targets = NULL;

    if (rc == NULL || rc->code == NULL || rc->code_count == 0)
    {
        vigil_error_set_literal(ctx->error, VIGIL_STATUS_INVALID_ARGUMENT, "transpile: empty register chunk");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    /* Work on a copy so the peephole pass doesn't mutate the original. */
    {
        vigil_allocator_t alloc = vigil_default_allocator();
        code = (vigil_reg_instr_t *)alloc.allocate(alloc.user_data, rc->code_count * sizeof(vigil_reg_instr_t));
    }
    if (!code)
    {
        vigil_error_set_literal(ctx->error, VIGIL_STATUS_OUT_OF_MEMORY, "transpile: alloc failed");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    memcpy(code, rc->code, rc->code_count * sizeof(vigil_reg_instr_t));

    targets = build_jump_targets(code, rc->code_count);
    if (!targets)
    {
        vigil_allocator_t a = vigil_default_allocator();
        a.deallocate(a.user_data, code);
        vigil_error_set_literal(ctx->error, VIGIL_STATUS_OUT_OF_MEMORY, "transpile: alloc failed");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    peephole_eliminate_moves(code, rc->code_count, targets);

    /* Build a modified reg_chunk that points to our optimized code. */
    vigil_reg_chunk_t opt_rc = *rc;
    opt_rc.code = code;

    /* Function signature */
    EMITF("vigil_reg_t %s(vigil_tc_t *tc", func_name);
    for (uint8_t i = 0; i < arity; i++)
        EMITF(", vigil_reg_t arg_%u", (unsigned)i);
    EMIT(")\n{\n");

    /* Register file */
    uint8_t regs = max_regs > 0 ? max_regs : 1;
    EMITF("    vigil_reg_t r[%u];\n", (unsigned)regs);
    EMITF("    memset(r, 0, sizeof(r));\n");

    /* Copy arguments into registers */
    for (uint8_t i = 0; i < arity; i++)
        EMITF("    r[%u] = arg_%u;\n", (unsigned)i, (unsigned)i);

    /* Set per-function constant pool */
    EMITF("    tc->constants = vigil_fn_constants[%zu];\n", func_index);
    EMITF("    tc->constant_count = vigil_fn_constant_counts[%zu];\n", func_index);

    EMIT("\n");

    /* Emit instructions with labels */
    for (size_t ip = 0; ip < opt_rc.code_count; ip++)
    {
        EMITF("L_%zu:;\n", ip);

        /* Skip identity moves (NOPped by peephole) */
        uint8_t op = VREG_GET_OP(opt_rc.code[ip]);
        if (op == VREG_MOVE && VREG_GET_A(opt_rc.code[ip]) == VREG_GET_B(opt_rc.code[ip]))
            continue;

        status = emit_instruction(ctx, &opt_rc, &ip, func_index, func_count);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_allocator_t a = vigil_default_allocator();
            a.deallocate(a.user_data, targets);
            a.deallocate(a.user_data, code);
            return status;
        }
    }

    /* Fallthrough return */
    EMIT("    vigil_tc_drain_defers(tc, NULL);\n");
    EMIT("    return (vigil_reg_t){0};\n");
    EMIT("}\n\n");

    {
        vigil_allocator_t a = vigil_default_allocator();
        a.deallocate(a.user_data, targets);
        a.deallocate(a.user_data, code);
    }
    return VIGIL_STATUS_OK;
}

/* ── Public API ──────────────────────────────────────────────────── */

static size_t count_siblings(const vigil_object_t *function)
{
    size_t n = 0;
    while (vigil_function_object_sibling(function, n) != NULL)
        n++;
    return n > 0 ? n : 1;
}

static size_t find_entry_index(const vigil_object_t *function, size_t func_count)
{
    size_t i;
    for (i = 0; i < func_count; i++)
    {
        if (vigil_function_object_sibling(function, i) == function)
            return i;
    }
    return 0;
}

static vigil_status_t emit_forward_decl(vigil_transpile_ctx_t *ctx, const vigil_object_t *function,
                                         size_t idx, int is_static)
{
    vigil_status_t status;
    uint8_t fn_arity = (uint8_t)vigil_function_object_arity(function);
    if (is_static)
        EMIT("static ");
    EMITF("vigil_reg_t vigil_fn_%zu(vigil_tc_t *tc", idx);
    for (uint8_t p = 0; p < fn_arity; p++)
        EMITF(", vigil_reg_t arg_%u", (unsigned)p);
    EMIT(");\n");
    return VIGIL_STATUS_OK;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_transpile_to_c(vigil_runtime_t *runtime, const vigil_object_t *function,
                                    vigil_string_t *out_source, vigil_error_t *error)
{
    vigil_transpile_ctx_t ctx_storage;
    vigil_transpile_ctx_t *ctx = &ctx_storage;
    vigil_status_t status;
    const vigil_reg_chunk_t *rc;
    size_t func_count, entry_idx, i;

    if (runtime == NULL || function == NULL || out_source == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "transpile: null argument");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (vigil_function_object_chunk(function) == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "transpile: function has no chunk");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    ctx_storage.runtime = runtime;
    ctx_storage.output = out_source;
    ctx_storage.error = error;
    ctx_storage.root_function = function;

    EMIT("/* Generated by vigil transpile — do not edit. */\n");
    EMIT("#include \"vigil_generated.h\"\n");
    EMIT("#include <math.h>\n");
    EMIT("#include <string.h>\n");
    EMIT("#include \"vigil/transpile_rt.h\"\n");
    EMIT("#include \"vigil/value.h\"\n");
    EMIT("#include \"internal/vigil_nanbox.h\"\n\n");
    EMIT("vigil_status_t vigil_tc_defer(vigil_tc_t *tc, vigil_value_t *regs, uint8_t defer_op,\n");
    EMIT("                                  uint8_t top_reg, uint32_t operand_a, uint32_t operand_b,\n");
    EMIT("                                  uint32_t operand_c, vigil_error_t *error);\n");
    EMIT("vigil_status_t vigil_tc_drain_defers(vigil_tc_t *tc, vigil_error_t *error);\n\n");

    func_count = count_siblings(function);
    entry_idx = find_entry_index(function, func_count);

    /* Emit constant pool sizes for all functions */
    {
        EMITF("vigil_value_t *vigil_fn_constants[%zu];\n", func_count > 0 ? func_count : 1);
        EMITF("size_t vigil_fn_constant_counts[%zu];\n\n", func_count > 0 ? func_count : 1);
    }

    /* Forward declarations */
    for (i = 0; i < func_count; i++)
    {
        const vigil_object_t *fn = vigil_function_object_sibling(function, i);
        if (fn == NULL)
            fn = function;
        status = emit_forward_decl(ctx, fn, i, i != entry_idx);
        if (status != VIGIL_STATUS_OK)
            return status;
    }
    EMIT("\n");

    /* Emit each function */
    for (i = 0; i < func_count; i++)
    {
        const vigil_object_t *fn = vigil_function_object_sibling(function, i);
        if (fn == NULL)
            fn = function;
        const vigil_chunk_t *fn_chunk = vigil_function_object_chunk(fn);
        if (fn_chunk == NULL)
            continue;

        status = vigil_chunk_ensure_reg_cache((vigil_chunk_t *)fn_chunk, (uint8_t)vigil_function_object_arity(fn),
                                              &rc, error);
        if (status != VIGIL_STATUS_OK)
            return status;

        char name_buf[64];
        snprintf(name_buf, sizeof(name_buf), "vigil_fn_%zu", i);
        status = vigil_transpile_emit_function(ctx, rc, name_buf, rc->arity, rc->max_registers, i, func_count);
        if (status != VIGIL_STATUS_OK)
            return status;
    }

    return VIGIL_STATUS_OK;
}

size_t vigil_transpile_entry_index(const vigil_object_t *function)
{
    if (function == NULL)
        return 0;
    return find_entry_index(function, count_siblings(function));
}

size_t vigil_transpile_func_count(const vigil_object_t *function)
{
    if (function == NULL)
        return 1;
    return count_siblings(function);
}

/* ── Embedded runtime extraction ─────────────────────────────────── */

/* Use forward declaration to avoid miniz include path issues. */
extern int mz_uncompress(unsigned char *pDest, unsigned long *pDest_len,
                         const unsigned char *pSource, unsigned long source_len);
#define MZ_OK 0

#include <stdlib.h>
#include "platform/platform.h"

/* Include the generated embedded sources table. */
#ifdef VIGIL_HAS_EMBEDDED_SOURCES
#include "embedded_sources.c"
#endif

vigil_status_t vigil_transpile_write_runtime(const char *output_dir, vigil_error_t *error)
{
#ifndef VIGIL_HAS_EMBEDDED_SOURCES
    (void)output_dir;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "transpile: no embedded sources available");
    return VIGIL_STATUS_UNSUPPORTED;
#else
    char path_buf[4096];
    size_t i;

    for (i = 0; i < vigil_embedded_file_count; i++)
    {
        const vigil_embedded_file_t *ef = &vigil_embedded_files[i];
        unsigned char *decompressed;
        unsigned long dest_len;
        int mz_status;

        snprintf(path_buf, sizeof(path_buf), "%s/vigil_rt/%s", output_dir, ef->path);

        /* Create parent directories. */
        {
            char dir_buf[4096];
            size_t len;
            snprintf(dir_buf, sizeof(dir_buf), "%s", path_buf);
            len = strlen(dir_buf);
            while (len > 0 && dir_buf[len - 1] != '/' && dir_buf[len - 1] != '\\')
                len--;
            if (len > 0)
            {
                dir_buf[len - 1] = '\0';
                vigil_platform_mkdir_p(dir_buf, error);
            }
        }

        dest_len = (unsigned long)ef->original_size;
        {
            vigil_allocator_t alloc = vigil_default_allocator();
            decompressed = (unsigned char *)alloc.allocate(alloc.user_data, ef->original_size + 1);
            if (!decompressed)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "transpile: out of memory decompressing");
                return VIGIL_STATUS_OUT_OF_MEMORY;
            }

            mz_status = mz_uncompress(decompressed, &dest_len, ef->data, (unsigned long)ef->compressed_size);
            if (mz_status != MZ_OK)
            {
                alloc.deallocate(alloc.user_data, decompressed);
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "transpile: decompression failed");
                return VIGIL_STATUS_INTERNAL;
            }

            {
                vigil_status_t st = vigil_platform_write_file(path_buf, decompressed, (size_t)dest_len, error);
                alloc.deallocate(alloc.user_data, decompressed);
                if (st != VIGIL_STATUS_OK)
                    return st;
            }
        }
    }

    return VIGIL_STATUS_OK;
#endif /* VIGIL_HAS_EMBEDDED_SOURCES */
}
