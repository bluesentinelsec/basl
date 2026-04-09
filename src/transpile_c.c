/*
 * transpile_c.c — Vigil-to-C transpiler (Phase 1: numeric subset).
 *
 * Walks vigil_reg_chunk_t instruction sequences and emits portable C11
 * source.  Phase 1 covers all numeric register ops: arithmetic,
 * comparisons, bitwise, conversions, control flow, calls, loops, and
 * math intrinsics.
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

static vigil_status_t emit_load_k(vigil_transpile_ctx_t *ctx, const vigil_reg_chunk_t *rc, uint8_t a, uint16_t bx)
{
    vigil_status_t status;
    const vigil_chunk_t *sc = rc->stack_chunk;
    if (sc != NULL && (size_t)bx < vigil_chunk_constant_count(sc))
    {
        const vigil_value_t *k = vigil_chunk_constant(sc, (size_t)bx);
        vigil_value_kind_t kind = vigil_value_kind(k);
        if (kind == VIGIL_VALUE_INT)
            EMITF("    r[%u].i = (int64_t)%lldLL;\n", a, (long long)vigil_value_as_int(k));
        else if (kind == VIGIL_VALUE_FLOAT)
            EMITF("    r[%u].f = %.17g;\n", a, vigil_value_as_float(k));
        else
            EMITF("    r[%u].i = 0; /* unsupported constant kind */\n", a);
    }
    else
    {
        EMITF("    r[%u].i = 0; /* missing constant */\n", a);
    }
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
    EMITF("    r[%u].i = ((int32_t)r[%u].i %s (int32_t)r[%u].i);\n", a, b, op_str, c);
    return VIGIL_STATUS_OK;
}

static vigil_status_t emit_i64_cmp(vigil_transpile_ctx_t *ctx, const char *op_str, uint8_t a, uint8_t b, uint8_t c)
{
    vigil_status_t status;
    EMITF("    r[%u].i = (r[%u].i %s r[%u].i);\n", a, b, op_str, c);
    return VIGIL_STATUS_OK;
}

static vigil_status_t emit_cmp_jmp_i32(vigil_transpile_ctx_t *ctx, const char *op_str,
                                        uint8_t a, uint8_t b, size_t ip)
{
    vigil_status_t status;
    EMITF("    if ((int32_t)r[%u].i %s (int32_t)r[%u].i) goto L_%zu;\n", a, b, op_str, ip + 2);
    return VIGIL_STATUS_OK;
}

static vigil_status_t emit_cmp_jmp_i64(vigil_transpile_ctx_t *ctx, const char *op_str,
                                        uint8_t a, uint8_t b, size_t ip)
{
    vigil_status_t status;
    EMITF("    if (r[%u].i %s r[%u].i) goto L_%zu;\n", a, b, op_str, ip + 2);
    return VIGIL_STATUS_OK;
}

static vigil_status_t emit_call_args(vigil_transpile_ctx_t *ctx, uint8_t base, uint8_t count)
{
    vigil_status_t status;
    for (uint8_t i = 0; i < count; i++)
    {
        if (i > 0)
            EMIT(", ");
        EMITF("r[%u]", (unsigned)(base + i));
    }
    return VIGIL_STATUS_OK;
}

/* ── Main instruction dispatch ───────────────────────────────────── */

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
    case VREG_MOVE:      EMITF("    r[%u] = r[%u];\n", a, b); break;
    case VREG_LOAD_K:    return emit_load_k(ctx, rc, a, bx);
    case VREG_LOAD_NIL:  EMITF("    r[%u].i = 0;\n", a); break;
    case VREG_LOAD_TRUE: EMITF("    r[%u].i = 1;\n", a); break;
    case VREG_LOAD_FALSE:EMITF("    r[%u].i = 0;\n", a); break;

    /* ── Typed i32 arithmetic ──────────────────────────────────── */
    case VREG_ADD_I32: return emit_i32_arith(ctx, "+", a, b, c);
    case VREG_SUB_I32: return emit_i32_arith(ctx, "-", a, b, c);
    case VREG_MUL_I32: return emit_i32_arith(ctx, "*", a, b, c);
    case VREG_DIV_I32: return emit_i32_arith(ctx, "/", a, b, c);
    case VREG_MOD_I32: return emit_i32_arith(ctx, "%%", a, b, c);

    /* ── Typed i64 arithmetic ──────────────────────────────────── */
    case VREG_ADD_I64: return emit_i64_arith(ctx, "+", a, b, c);
    case VREG_SUB_I64: return emit_i64_arith(ctx, "-", a, b, c);
    case VREG_MUL_I64: return emit_i64_arith(ctx, "*", a, b, c);
    case VREG_DIV_I64: return emit_i64_arith(ctx, "/", a, b, c);
    case VREG_MOD_I64: return emit_i64_arith(ctx, "%%", a, b, c);

    /* ── Typed f64 arithmetic ──────────────────────────────────── */
    case VREG_ADD_F64: return emit_f64_arith(ctx, "+", a, b, c);
    case VREG_SUB_F64: return emit_f64_arith(ctx, "-", a, b, c);
    case VREG_MUL_F64: return emit_f64_arith(ctx, "*", a, b, c);
    case VREG_DIV_F64: return emit_f64_arith(ctx, "/", a, b, c);

    /* ── Generic arithmetic ────────────────────────────────────── */
    case VREG_ADD: return emit_i64_arith(ctx, "+", a, b, c);
    case VREG_SUB: return emit_i64_arith(ctx, "-", a, b, c);
    case VREG_MUL: return emit_i64_arith(ctx, "*", a, b, c);
    case VREG_DIV: return emit_i64_arith(ctx, "/", a, b, c);
    case VREG_MOD: return emit_i64_arith(ctx, "%%", a, b, c);

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
    case VREG_EQ: return emit_i64_cmp(ctx, "==", a, b, c);
    case VREG_LT: return emit_i64_cmp(ctx, "<",  a, b, c);
    case VREG_LE: return emit_i64_cmp(ctx, "<=", a, b, c);

    /* ── Unary ─────────────────────────────────────────────────── */
    case VREG_NEG:  EMITF("    r[%u].i = -r[%u].i;\n", a, b); break;
    case VREG_NOT:  EMITF("    r[%u].i = !r[%u].i;\n", a, b); break;
    case VREG_BNOT: EMITF("    r[%u].i = ~r[%u].i;\n", a, b); break;

    /* ── Bitwise ───────────────────────────────────────────────── */
    case VREG_BAND: return emit_i64_arith(ctx, "&",  a, b, c);
    case VREG_BOR:  return emit_i64_arith(ctx, "|",  a, b, c);
    case VREG_BXOR: return emit_i64_arith(ctx, "^",  a, b, c);
    case VREG_SHL:  return emit_i64_arith(ctx, "<<", a, b, c);
    case VREG_SHR:  return emit_i64_arith(ctx, ">>", a, b, c);

    /* ── Type conversions ──────────────────────────────────────── */
    case VREG_TO_I32: EMITF("    r[%u].i = (int64_t)(int32_t)r[%u].i;\n", a, b); break;
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
        if (c) EMITF("    if (!r[%u].i) goto L_%zu;\n", a, *ip + 2);
        else   EMITF("    if (r[%u].i) goto L_%zu;\n", a, *ip + 2);
        break;
    case VREG_TESTSET:
        if (c) EMITF("    if (r[%u].i) { r[%u] = r[%u]; } else { goto L_%zu; }\n", b, a, b, *ip + 2);
        else   EMITF("    if (!r[%u].i) { r[%u] = r[%u]; } else { goto L_%zu; }\n", b, a, b, *ip + 2);
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
        EMITF("    r[%u] = vigil_fn_%u(", a, (unsigned)b);
        status = emit_call_args(ctx, a, c);
        if (status != VIGIL_STATUS_OK) return status;
        EMIT(");\n");
        break;
    case VREG_CALL_SELF:
    {
        uint32_t arg_base;
        if (*ip + 1 >= rc->code_count) { vigil_error_set_literal(ctx->error, VIGIL_STATUS_INTERNAL, "transpile: truncated CALL_SELF"); return VIGIL_STATUS_INTERNAL; }
        arg_base = rc->code[*ip + 1];
        *ip += 1;
        EMITF("    r[%u] = vigil_fn_%zu(", a, func_index);
        status = emit_call_args(ctx, (uint8_t)arg_base, b);
        if (status != VIGIL_STATUS_OK) return status;
        EMIT(");\n");
        break;
    }
    case VREG_TAIL_CALL:
        EMITF("    r[%u] = vigil_fn_%u(", a, (unsigned)b);
        status = emit_call_args(ctx, a, c);
        if (status != VIGIL_STATUS_OK) return status;
        EMIT(");\n");
        break;
    case VREG_RETURN:
        if (b == 0) EMIT("    return (vigil_reg_t){0};\n");
        else        EMITF("    return r[%u];\n", a);
        break;

    /* ── Loop superinstructions ────────────────────────────────── */
    case VREG_FORLOOP_I32:
    {
        size_t target = (size_t)((int64_t)(*ip) + 1 + (int64_t)sbx);
        EMITF("    r[%u].i = (int64_t)((int32_t)r[%u].i + (int32_t)r[%u].i);\n", a, a, a + 2);
        EMITF("    if ((int32_t)r[%u].i < (int32_t)r[%u].i) goto L_%zu;\n", a, a + 1, target);
        break;
    }
    case VREG_FORLOOP_I64:
    {
        size_t target = (size_t)((int64_t)(*ip) + 1 + (int64_t)sbx);
        EMITF("    r[%u].i = r[%u].i + r[%u].i;\n", a, a, a + 2);
        EMITF("    if (r[%u].i < r[%u].i) goto L_%zu;\n", a, a + 1, target);
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

    default:
        EMITF("    /* unsupported opcode %u */\n", (unsigned)op);
        break;
    }

    return VIGIL_STATUS_OK;
}

/* ── Function emission ───────────────────────────────────────────── */

vigil_status_t vigil_transpile_emit_function(vigil_transpile_ctx_t *ctx, const vigil_reg_chunk_t *rc,
                                             const char *func_name, uint8_t arity, uint8_t max_regs,
                                             size_t func_index, size_t func_count)
{
    vigil_status_t status;

    if (rc == NULL || rc->code == NULL || rc->code_count == 0)
    {
        vigil_error_set_literal(ctx->error, VIGIL_STATUS_INVALID_ARGUMENT, "transpile: empty register chunk");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    /* Function signature */
    EMITF("vigil_reg_t %s(", func_name);
    for (uint8_t i = 0; i < arity; i++)
    {
        if (i > 0)
            EMIT(", ");
        EMITF("vigil_reg_t arg_%u", (unsigned)i);
    }
    if (arity == 0)
        EMIT("void");
    EMIT(")\n{\n");

    /* Register file */
    uint8_t regs = max_regs > 0 ? max_regs : 1;
    EMITF("    vigil_reg_t r[%u];\n", (unsigned)regs);
    EMITF("    memset(r, 0, sizeof(r));\n");

    /* Copy arguments into registers */
    for (uint8_t i = 0; i < arity; i++)
        EMITF("    r[%u] = arg_%u;\n", (unsigned)i, (unsigned)i);

    EMIT("\n");

    /* Emit instructions with labels */
    for (size_t ip = 0; ip < rc->code_count; ip++)
    {
        EMITF("L_%zu:;\n", ip);
        status = emit_instruction(ctx, rc, &ip, func_index, func_count);
        if (status != VIGIL_STATUS_OK)
            return status;
    }

    /* Fallthrough return */
    EMIT("    return (vigil_reg_t){0};\n");
    EMIT("}\n\n");

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
    EMITF("vigil_reg_t vigil_fn_%zu(", idx);
    for (uint8_t p = 0; p < fn_arity; p++)
    {
        if (p > 0)
            EMIT(", ");
        EMITF("vigil_reg_t arg_%u", (unsigned)p);
    }
    if (fn_arity == 0)
        EMIT("void");
    EMIT(");\n");
    return VIGIL_STATUS_OK;
}

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
    EMIT("#include <string.h>\n\n");

    func_count = count_siblings(function);
    entry_idx = find_entry_index(function, func_count);

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
