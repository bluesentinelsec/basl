/*
 * vigil_regvm.h — Register-based VM instruction set and translator.
 *
 * The compiler emits stack-based bytecode as before.  A translation pass
 * converts it to fixed-width 32-bit register instructions before execution.
 * The register VM dispatch loop then executes these instructions, addressing
 * values by register index instead of push/pop.
 *
 * Instruction encoding (32-bit words):
 *
 *   ABC format:  [op:8][A:8][B:8][C:8]
 *     A = destination register or first operand
 *     B = source register 1
 *     C = source register 2
 *
 *   ABx format:  [op:8][A:8][Bx:16]
 *     A = register
 *     Bx = unsigned 16-bit immediate (constant index, arg count, etc.)
 *
 *   AsBx format: [op:8][A:8][sBx:16]  (sBx = Bx - 32767, signed)
 *     A = register
 *     sBx = signed 16-bit offset (jumps)
 *
 *   Ax format:   [op:8][Ax:24]
 *     Ax = 24-bit unsigned immediate (large constant index)
 *
 * Register window: each function frame has registers R0..R(N-1) where
 * N = max_registers.  Locals occupy R0..R(local_count-1).  Temporaries
 * use R(local_count)..R(max_registers-1).
 */

#ifndef VIGIL_REGVM_H
#define VIGIL_REGVM_H

#include <stddef.h>
#include <stdint.h>

#include "vigil/chunk.h"
#include "vigil/status.h"
#include "vigil/value.h"
#include "vigil_vm_internal.h"

/* ── Register instruction opcodes ──────────────────────────────── */

typedef enum vigil_reg_op
{
    /* Data movement */
    VREG_MOVE = 0,       /* A B     R[A] = R[B] */
    VREG_LOAD_K = 1,     /* A Bx    R[A] = K[Bx] */
    VREG_LOAD_NIL = 2,   /* A       R[A] = nil */
    VREG_LOAD_TRUE = 3,  /* A       R[A] = true */
    VREG_LOAD_FALSE = 4, /* A       R[A] = false */

    /* Generic arithmetic (nanbox type-checked) */
    VREG_ADD = 5, /* A B C   R[A] = R[B] + R[C] */
    VREG_SUB = 6, /* A B C   R[A] = R[B] - R[C] */
    VREG_MUL = 7, /* A B C   R[A] = R[B] * R[C] */
    VREG_DIV = 8, /* A B C   R[A] = R[B] / R[C] */
    VREG_MOD = 9, /* A B C   R[A] = R[B] % R[C] */

    /* Typed i32 arithmetic */
    VREG_ADD_I32 = 10, /* A B C   R[A] = R[B] +i32 R[C] */
    VREG_SUB_I32 = 11,
    VREG_MUL_I32 = 12,
    VREG_DIV_I32 = 13,
    VREG_MOD_I32 = 14,

    /* Typed i64 arithmetic */
    VREG_ADD_I64 = 15, /* A B C   R[A] = R[B] +i64 R[C] */
    VREG_SUB_I64 = 16,
    VREG_MUL_I64 = 17,
    VREG_DIV_I64 = 18,
    VREG_MOD_I64 = 19,

    /* Typed f64 arithmetic */
    VREG_ADD_F64 = 20, /* A B C   R[A] = R[B] +f64 R[C] */
    VREG_SUB_F64 = 21,
    VREG_MUL_F64 = 22,
    VREG_DIV_F64 = 23,

    /* Comparisons (generic) */
    VREG_EQ = 24, /* A B C   R[A] = R[B] == R[C] */
    VREG_LT = 25, /* A B C   R[A] = R[B] < R[C] */
    VREG_LE = 26, /* A B C   R[A] = R[B] <= R[C] */

    /* Typed i32 comparisons */
    VREG_LT_I32 = 27,
    VREG_LE_I32 = 28,
    VREG_GT_I32 = 29,
    VREG_GE_I32 = 30,
    VREG_EQ_I32 = 31,
    VREG_NE_I32 = 32,

    /* Typed i64 comparisons */
    VREG_LT_I64 = 33,
    VREG_LE_I64 = 34,
    VREG_GT_I64 = 35,
    VREG_GE_I64 = 36,
    VREG_EQ_I64 = 37,
    VREG_NE_I64 = 38,

    /* Unary */
    VREG_NEG = 39,  /* A B     R[A] = -R[B] */
    VREG_NOT = 40,  /* A B     R[A] = !R[B] */
    VREG_BNOT = 41, /* A B     R[A] = ~R[B] */

    /* Bitwise */
    VREG_BAND = 42, /* A B C   R[A] = R[B] & R[C] */
    VREG_BOR = 43,
    VREG_BXOR = 44,
    VREG_SHL = 45,
    VREG_SHR = 46,

    /* Type conversions */
    VREG_TO_I32 = 47, /* A B     R[A] = i32(R[B]) */
    VREG_TO_I64 = 48,
    VREG_TO_U8 = 49,
    VREG_TO_U32 = 50,
    VREG_TO_U64 = 51,
    VREG_TO_F64 = 52,
    VREG_TO_STRING = 53,

    /* Control flow */
    VREG_JMP = 54,     /* sBx      ip += sBx */
    VREG_TEST = 55,    /* A C      if bool(R[A]) != C then skip next */
    VREG_TESTSET = 56, /* A B C    if bool(R[B]) != C then skip; else R[A]=R[B] */

    /* Typed compare + jump (fused) */
    VREG_LT_I32_JMP = 57, /* A B sBx  if R[A] <i32 R[B] then ip += sBx */
    VREG_LE_I32_JMP = 58,
    VREG_GT_I32_JMP = 59,
    VREG_GE_I32_JMP = 60,
    VREG_EQ_I32_JMP = 61,
    VREG_NE_I32_JMP = 62,
    VREG_LT_I64_JMP = 63,
    VREG_LE_I64_JMP = 64,
    VREG_GT_I64_JMP = 65,
    VREG_GE_I64_JMP = 66,
    VREG_EQ_I64_JMP = 67,
    VREG_NE_I64_JMP = 68,

    /* Calls */
    VREG_CALL = 69,           /* A B C    call K[A] with B args at R[A+1], C returns */
    VREG_CALL_NATIVE = 70,    /* A C + u32 call native K[ip+1] with C args at R[A] */
    VREG_CALL_VALUE = 71,     /* A Bx + u32  call value with Bx args from packed base at ip+1 */
    VREG_CALL_SELF = 72,      /* A B      self-recurse with B args at R[A] */
    VREG_CALL_INTERFACE = 73, /* A B C + u32 + u32 */
    VREG_TAIL_CALL = 74,      /* A B C    tail call K[A] with B args */
    VREG_RETURN = 75,         /* A B      return B values starting at R[A] */

    /* Globals and captures */
    VREG_GET_GLOBAL = 76,   /* A Bx     R[A] = globals[Bx] */
    VREG_SET_GLOBAL = 77,   /* A Bx     globals[Bx] = R[A] */
    VREG_GET_CAPTURE = 78,  /* A Bx     R[A] = captures[Bx] */
    VREG_SET_CAPTURE = 79,  /* A Bx     captures[Bx] = R[A] */
    VREG_GET_FUNCTION = 80, /* A Bx     R[A] = functions[Bx] */
    VREG_NEW_CLOSURE = 81,  /* A B C    R[A] = closure(K[B], C captures) */

    /* Objects and collections */
    VREG_NEW_INSTANCE = 82,
    VREG_GET_FIELD = 83, /* A B C    R[A] = R[B].field[C] */
    VREG_SET_FIELD = 84, /* A B C    R[A].field[B] = R[C] */
    VREG_NEW_ARRAY = 85, /* A B      R[A] = new array with B elements from R[A+1].. */
    VREG_NEW_MAP = 86,
    VREG_GET_INDEX = 87, /* A B C    R[A] = R[B][R[C]] */
    VREG_SET_INDEX = 88, /* A B C    R[A][R[B]] = R[C] */
    VREG_COLLECTION_SIZE = 89,

    /* Math intrinsics */
    VREG_MATH_SIN = 90, /* A B      R[A] = sin(R[B]) */
    VREG_MATH_COS = 91,
    VREG_MATH_SQRT = 92,
    VREG_MATH_LOG = 93,
    VREG_MATH_POW = 94, /* A B C    R[A] = pow(R[B], R[C]) */

    /* Loop superinstructions */
    VREG_FORLOOP_I32 = 95, /* A sBx    R[A]+=R[A+2]; if R[A]<?R[A+1] ip+=sBx */
    VREG_FORLOOP_I64 = 96,
    VREG_INC_I32 = 97, /* A B      R[A] += imm8(B) */
    VREG_INC_I64 = 98,

    /* Error handling */
    VREG_NEW_ERROR = 99,
    VREG_GET_ERROR_KIND = 100,
    VREG_GET_ERROR_MSG = 101,

    /* String ops — these fall back to the existing VM helpers */
    VREG_STRING_OP = 102, /* A B C    string operation B on R[A] with args */

    /* Format */
    VREG_FORMAT_F64 = 103, /* A B C    R[A] = format_f64(R[B], C) */
    VREG_FORMAT_SPEC = 104,

    /* Parse intrinsics */
    VREG_PARSE_I32 = 105,
    VREG_PARSE_F64 = 106,
    VREG_PARSE_BOOL = 107,

    /* Defer */
    VREG_DEFER = 108, /* A B C    defer call type A, operands B, C */

    /* Map/array methods */
    VREG_MAP_KEY_AT = 109,
    VREG_MAP_VALUE_AT = 110,
    VREG_ARRAY_PUSH = 111,
    VREG_ARRAY_POP = 112,
    VREG_ARRAY_GET_SAFE = 113,
    VREG_ARRAY_SET_SAFE = 114,
    VREG_ARRAY_SLICE = 115,
    VREG_ARRAY_CONTAINS = 116,
    VREG_MAP_GET_SAFE = 117,
    VREG_MAP_SET_SAFE = 118,
    VREG_MAP_REMOVE_SAFE = 119,
    VREG_MAP_HAS = 120,
    VREG_MAP_KEYS = 121,
    VREG_MAP_VALUES = 122,

    /* Char/string */
    VREG_CHAR_FROM_INT = 123,
    VREG_CALL_EXTERN = 124, /* A B C + u32 */

    /* DUP — needed for some patterns during translation */
    VREG_DUP = 125,     /* A B      R[A] = R[B] (with retain) */
    VREG_RELEASE = 126, /* A        release object in R[A] if present */

    VREG_ARRAY_SORT = 127,      /* A        sort R[A] ascending */
    VREG_ARRAY_SORT_DESC = 128, /* A        sort R[A] descending */
    VREG_ARRAY_REVERSE = 129,   /* A        reverse R[A] in place */
    VREG_ARRAY_INDEX_OF = 130,  /* A B C    R[A] = index_of(R[B], R[C]) */
    VREG_ARRAY_REMOVE_AT = 131, /* A B C    R[A],R[A+1] = remove(R[B], R[C]) */
    VREG_ARRAY_INSERT_AT = 132, /* A B C    R[A] = insert(R[B], R[C], ...) */
    VREG_ARRAY_CLEAR = 133,     /* A        clear R[A] */
    VREG_MAP_CLEAR = 134,       /* A        clear R[A] */

    /* Immediate i32 arithmetic. */
    VREG_ADDI = 135, /* A B C   R[A] = R[B] +i32 imm8(C) */
    VREG_SUBI = 136, /* A B C   R[A] = R[B] -i32 imm8(C) */

    VREG_OP_COUNT
} vigil_reg_op_t;

/* ── Instruction encoding/decoding ─────────────────────────────── */

typedef uint32_t vigil_reg_instr_t;

/* Encode */
static inline vigil_reg_instr_t vigil_reg_abc(uint8_t op, uint8_t a, uint8_t b, uint8_t c)
{
    return (uint32_t)op | ((uint32_t)a << 8) | ((uint32_t)b << 16) | ((uint32_t)c << 24);
}

static inline vigil_reg_instr_t vigil_reg_abx(uint8_t op, uint8_t a, uint16_t bx)
{
    return (uint32_t)op | ((uint32_t)a << 8) | ((uint32_t)bx << 16);
}

static inline vigil_reg_instr_t vigil_reg_asbx(uint8_t op, uint8_t a, int16_t sbx)
{
    uint16_t ubx = (uint16_t)(sbx + 32767);
    return (uint32_t)op | ((uint32_t)a << 8) | ((uint32_t)ubx << 16);
}

static inline vigil_reg_instr_t vigil_reg_ax(uint8_t op, uint32_t ax)
{
    return (uint32_t)op | ((ax & 0xFFFFFFU) << 8);
}

/* Decode */
#define VREG_GET_OP(i) ((uint8_t)((i) & 0xFFU))
#define VREG_GET_A(i) ((uint8_t)(((i) >> 8) & 0xFFU))
#define VREG_GET_B(i) ((uint8_t)(((i) >> 16) & 0xFFU))
#define VREG_GET_C(i) ((uint8_t)(((i) >> 24) & 0xFFU))
#define VREG_GET_Bx(i) ((uint16_t)(((i) >> 16) & 0xFFFFU))
#define VREG_GET_sBx(i) ((int16_t)((int32_t)(((i) >> 16) & 0xFFFFU) - 32767))
#define VREG_GET_Ax(i) (((i) >> 8) & 0xFFFFFFU)

/* ── Register chunk ────────────────────────────────────────────── */

typedef struct vigil_reg_chunk
{
    vigil_reg_instr_t *code;
    size_t code_count;
    size_t code_capacity;
    /* Map from register instruction index → source span index in the
       original stack chunk.  Used for error reporting. */
    size_t *span_map;
    size_t span_map_count;
    /* Pointer to the original stack chunk (for constants and spans). */
    const vigil_chunk_t *stack_chunk;
    uint8_t max_registers;
    uint8_t arity; /* function parameter count, set before translation */
} vigil_reg_chunk_t;

/* ── Translation API ───────────────────────────────────────────── */

/*
 * Translate a stack-based chunk to register instructions.
 * Returns VIGIL_STATUS_OK on success.  On failure, the reg_chunk is
 * left in a valid but empty state.
 */
vigil_status_t vigil_reg_translate(const vigil_chunk_t *stack_chunk, vigil_reg_chunk_t *reg_chunk,
                                   vigil_runtime_t *runtime, vigil_error_t *error);

int vigil_reg_chunk_is_translatable(const vigil_chunk_t *stack_chunk);

void vigil_reg_chunk_init(vigil_reg_chunk_t *rc);
void vigil_reg_chunk_free(vigil_reg_chunk_t *rc, vigil_runtime_t *runtime);
vigil_status_t vigil_chunk_ensure_reg_cache(vigil_chunk_t *chunk, uint8_t arity,
                                            const vigil_reg_chunk_t **out_reg_cache, vigil_error_t *error);

/* ── Register VM execution ─────────────────────────────────────── */

vigil_status_t vigil_regvm_execute(vigil_vm_t *vm, const vigil_reg_chunk_t *rc, vigil_value_t *out_value,
                                   vigil_error_t *error);

#endif /* VIGIL_REGVM_H */
