#ifndef VIGIL_CHUNK_H
#define VIGIL_CHUNK_H

#include <stddef.h>
#include <stdint.h>

#include "vigil/array.h"
#include "vigil/debug_info.h"
#include "vigil/export.h"
#include "vigil/source.h"
#include "vigil/status.h"
#include "vigil/string.h"
#include "vigil/value.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum vigil_opcode
    {
        VIGIL_OPCODE_CONSTANT = 0,
        VIGIL_OPCODE_NIL = 1,
        VIGIL_OPCODE_TRUE = 2,
        VIGIL_OPCODE_FALSE = 3,
        VIGIL_OPCODE_RETURN = 4,
        VIGIL_OPCODE_POP = 5,
        VIGIL_OPCODE_DUP = 6,
        VIGIL_OPCODE_DUP_TWO = 7,
        VIGIL_OPCODE_GET_LOCAL = 8,
        VIGIL_OPCODE_SET_LOCAL = 9,
        VIGIL_OPCODE_GET_GLOBAL = 10,
        VIGIL_OPCODE_SET_GLOBAL = 11,
        VIGIL_OPCODE_GET_FUNCTION = 12,
        VIGIL_OPCODE_NEW_CLOSURE = 13,
        VIGIL_OPCODE_GET_CAPTURE = 14,
        VIGIL_OPCODE_SET_CAPTURE = 15,
        VIGIL_OPCODE_JUMP = 16,
        VIGIL_OPCODE_JUMP_IF_FALSE = 17,
        VIGIL_OPCODE_LOOP = 18,
        VIGIL_OPCODE_ADD = 19,
        VIGIL_OPCODE_SUBTRACT = 20,
        VIGIL_OPCODE_MULTIPLY = 21,
        VIGIL_OPCODE_DIVIDE = 22,
        VIGIL_OPCODE_MODULO = 23,
        VIGIL_OPCODE_BITWISE_AND = 24,
        VIGIL_OPCODE_BITWISE_OR = 25,
        VIGIL_OPCODE_BITWISE_XOR = 26,
        VIGIL_OPCODE_SHIFT_LEFT = 27,
        VIGIL_OPCODE_SHIFT_RIGHT = 28,
        VIGIL_OPCODE_NEGATE = 29,
        VIGIL_OPCODE_NOT = 30,
        VIGIL_OPCODE_BITWISE_NOT = 31,
        VIGIL_OPCODE_TO_I32 = 32,
        VIGIL_OPCODE_TO_I64 = 33,
        VIGIL_OPCODE_TO_U8 = 34,
        VIGIL_OPCODE_TO_U32 = 35,
        VIGIL_OPCODE_TO_U64 = 36,
        VIGIL_OPCODE_TO_F64 = 37,
        VIGIL_OPCODE_TO_STRING = 38,
        VIGIL_OPCODE_FORMAT_F64 = 39,
        VIGIL_OPCODE_NEW_ERROR = 40,
        VIGIL_OPCODE_GET_ERROR_KIND = 41,
        VIGIL_OPCODE_GET_ERROR_MESSAGE = 42,
        VIGIL_OPCODE_EQUAL = 43,
        VIGIL_OPCODE_GREATER = 44,
        VIGIL_OPCODE_LESS = 45,
        VIGIL_OPCODE_CALL = 46,
        VIGIL_OPCODE_CALL_VALUE = 47,
        VIGIL_OPCODE_NEW_INSTANCE = 48,
        VIGIL_OPCODE_GET_FIELD = 49,
        VIGIL_OPCODE_SET_FIELD = 50,
        VIGIL_OPCODE_NEW_ARRAY = 51,
        VIGIL_OPCODE_NEW_MAP = 52,
        VIGIL_OPCODE_GET_INDEX = 53,
        VIGIL_OPCODE_SET_INDEX = 54,
        VIGIL_OPCODE_GET_COLLECTION_SIZE = 55,
        VIGIL_OPCODE_GET_MAP_KEY_AT = 56,
        VIGIL_OPCODE_GET_MAP_VALUE_AT = 57,
        VIGIL_OPCODE_CALL_INTERFACE = 58,
        VIGIL_OPCODE_DEFER_CALL = 59,
        VIGIL_OPCODE_DEFER_CALL_VALUE = 60,
        VIGIL_OPCODE_DEFER_NEW_INSTANCE = 61,
        VIGIL_OPCODE_DEFER_CALL_INTERFACE = 62,
        VIGIL_OPCODE_GET_STRING_SIZE = 63,
        VIGIL_OPCODE_STRING_CONTAINS = 64,
        VIGIL_OPCODE_STRING_STARTS_WITH = 65,
        VIGIL_OPCODE_STRING_ENDS_WITH = 66,
        VIGIL_OPCODE_STRING_TRIM = 67,
        VIGIL_OPCODE_STRING_TO_UPPER = 68,
        VIGIL_OPCODE_STRING_TO_LOWER = 69,
        VIGIL_OPCODE_STRING_REPLACE = 70,
        VIGIL_OPCODE_STRING_SPLIT = 71,
        VIGIL_OPCODE_STRING_INDEX_OF = 72,
        VIGIL_OPCODE_STRING_SUBSTR = 73,
        VIGIL_OPCODE_STRING_BYTES = 74,
        VIGIL_OPCODE_STRING_CHAR_AT = 75,
        VIGIL_OPCODE_ARRAY_PUSH = 76,
        VIGIL_OPCODE_ARRAY_POP = 77,
        VIGIL_OPCODE_ARRAY_GET_SAFE = 78,
        VIGIL_OPCODE_ARRAY_SET_SAFE = 79,
        VIGIL_OPCODE_ARRAY_SLICE = 80,
        VIGIL_OPCODE_ARRAY_CONTAINS = 81,
        VIGIL_OPCODE_MAP_GET_SAFE = 82,
        VIGIL_OPCODE_MAP_SET_SAFE = 83,
        VIGIL_OPCODE_MAP_REMOVE_SAFE = 84,
        VIGIL_OPCODE_MAP_HAS = 85,
        VIGIL_OPCODE_MAP_KEYS = 86,
        VIGIL_OPCODE_MAP_VALUES = 87,

        /* Specialized integer opcodes — emitted by the compiler when both
           operands are statically known to be signed integers (i32/i64).
           These skip the type-dispatch switch in the generic arithmetic
           handler.  The VM reads two i64 values from the stack and
           produces an i64 (arithmetic) or bool (comparison) result. */
        VIGIL_OPCODE_ADD_I64 = 88,
        VIGIL_OPCODE_SUBTRACT_I64 = 89,
        VIGIL_OPCODE_LESS_I64 = 90,
        VIGIL_OPCODE_LESS_EQUAL_I64 = 91,
        VIGIL_OPCODE_GREATER_I64 = 92,
        VIGIL_OPCODE_GREATER_EQUAL_I64 = 93,
        VIGIL_OPCODE_MULTIPLY_I64 = 94,
        VIGIL_OPCODE_DIVIDE_I64 = 95,
        VIGIL_OPCODE_MODULO_I64 = 96,
        VIGIL_OPCODE_EQUAL_I64 = 97,
        VIGIL_OPCODE_NOT_EQUAL_I64 = 98,

        /* ── Superinstructions ─────────────────────────────────────────
           Fused GET_LOCAL + GET_LOCAL + <i64 op>.  Each reads two local
           slots directly (two u32 operands) and pushes the result,
           eliminating two dispatches and all intermediate stack traffic.
           The compiler emits these when it detects the pattern. */
        VIGIL_OPCODE_LOCALS_ADD_I64 = 99,
        VIGIL_OPCODE_LOCALS_SUBTRACT_I64 = 100,
        VIGIL_OPCODE_LOCALS_MULTIPLY_I64 = 101,
        VIGIL_OPCODE_LOCALS_MODULO_I64 = 102,
        VIGIL_OPCODE_LOCALS_LESS_I64 = 103,
        VIGIL_OPCODE_LOCALS_LESS_EQUAL_I64 = 104,
        VIGIL_OPCODE_LOCALS_GREATER_I64 = 105,
        VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I64 = 106,
        VIGIL_OPCODE_LOCALS_EQUAL_I64 = 107,
        VIGIL_OPCODE_LOCALS_NOT_EQUAL_I64 = 108,

        /* ── i32-specific opcodes ─────────────────────────────────────
           These operate on values known at compile time to be i32.
           They use 32-bit overflow checks (cheaper than i64) and avoid
           sign-extension overhead in the NaN-box decode path. */
        VIGIL_OPCODE_ADD_I32 = 109,
        VIGIL_OPCODE_SUBTRACT_I32 = 110,
        VIGIL_OPCODE_MULTIPLY_I32 = 111,
        VIGIL_OPCODE_LESS_I32 = 112,
        VIGIL_OPCODE_LESS_EQUAL_I32 = 113,
        VIGIL_OPCODE_GREATER_I32 = 114,
        VIGIL_OPCODE_GREATER_EQUAL_I32 = 115,
        VIGIL_OPCODE_EQUAL_I32 = 116,
        VIGIL_OPCODE_NOT_EQUAL_I32 = 117,
        VIGIL_OPCODE_MODULO_I32 = 118,
        VIGIL_OPCODE_DIVIDE_I32 = 119,

        /* Three-address i32 superinstructions: read two locals, operate,
           store result to a third local.  Zero stack traffic. */
        VIGIL_OPCODE_LOCALS_ADD_I32_STORE = 120,
        VIGIL_OPCODE_LOCALS_SUBTRACT_I32_STORE = 121,
        VIGIL_OPCODE_LOCALS_MULTIPLY_I32_STORE = 122,
        VIGIL_OPCODE_LOCALS_LESS_I32_STORE = 123,
        VIGIL_OPCODE_LOCALS_LESS_EQUAL_I32_STORE = 124,
        VIGIL_OPCODE_LOCALS_GREATER_I32_STORE = 125,
        VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I32_STORE = 126,
        VIGIL_OPCODE_LOCALS_EQUAL_I32_STORE = 127,
        VIGIL_OPCODE_LOCALS_NOT_EQUAL_I32_STORE = 128,
        VIGIL_OPCODE_LOCALS_MODULO_I32_STORE = 129,

        /* Single-opcode increment: local[idx] += imm8 (signed).
           Covers i = i + 1, i = i - 1, and small constant steps. */
        VIGIL_OPCODE_INCREMENT_LOCAL_I32 = 130,

        /* Tail-call optimization: reuses the current frame instead of
           pushing a new one.  Same operand format as CALL. */
        VIGIL_OPCODE_TAIL_CALL = 131,

        /* FORLOOP_I32: fused increment + compare + branch for counting loops.
           Format: [opcode][u32 local][i8 delta][u32 const_limit][u8 cmp][u32 back_offset]
           Increments local by delta, compares against constant limit using
           cmp (0=LT,1=LE,2=GT,3=GE,4=NE), jumps back if true. */
        VIGIL_OPCODE_FORLOOP_I32 = 132,
        VIGIL_OPCODE_CALL_NATIVE = 133,

        VIGIL_OPCODE_STRING_TRIM_LEFT = 134,
        VIGIL_OPCODE_STRING_TRIM_RIGHT = 135,
        VIGIL_OPCODE_STRING_REPEAT = 136,
        VIGIL_OPCODE_STRING_REVERSE = 137,
        VIGIL_OPCODE_STRING_IS_EMPTY = 138,
        VIGIL_OPCODE_STRING_COUNT = 139,
        VIGIL_OPCODE_STRING_LAST_INDEX_OF = 140,
        VIGIL_OPCODE_STRING_TRIM_PREFIX = 141,
        VIGIL_OPCODE_STRING_TRIM_SUFFIX = 142,
        VIGIL_OPCODE_CHAR_FROM_INT = 143,
        VIGIL_OPCODE_STRING_TO_C = 144,
        VIGIL_OPCODE_STRING_JOIN = 145,
        VIGIL_OPCODE_STRING_CUT = 146,
        VIGIL_OPCODE_STRING_FIELDS = 147,
        VIGIL_OPCODE_STRING_EQUAL_FOLD = 148,

        /* string.char_count() — returns the number of Unicode code points
           (not bytes) in a string. */
        VIGIL_OPCODE_STRING_CHAR_COUNT = 149,

        /* FORMAT_SPEC — general-purpose value formatting.
           Two u32 operands:
             word1: [fill_char:8][align:2][format_type:4][grouping:1][unused:17]
             word2: [width:16][precision:16]
           align: 0=default, 1=left(<), 2=right(>), 3=center(^)
           format_type: 0=string, 1=decimal, 2=hex_lower, 3=hex_upper,
                        4=binary, 5=octal, 6=float_f
           grouping: 1 = insert thousands separators */
        VIGIL_OPCODE_FORMAT_SPEC = 150,
        VIGIL_OPCODE_DEFER_CALL_NATIVE = 151,
        VIGIL_OPCODE_CALL_EXTERN = 152,

        /* Dedicated math intrinsics — bypass CALL_NATIVE overhead. */
        VIGIL_OPCODE_MATH_SIN_F64 = 153,
        VIGIL_OPCODE_MATH_COS_F64 = 154,
        VIGIL_OPCODE_MATH_SQRT_F64 = 155,
        VIGIL_OPCODE_MATH_LOG_F64 = 156,
        VIGIL_OPCODE_MATH_POW_F64 = 157,

        /* Dedicated parse intrinsics — bypass CALL_NATIVE overhead.
           Each pops a string, pushes (value, ok_error) on success or
           (default, error_object) on failure. */
        VIGIL_OPCODE_PARSE_I32 = 158,
        VIGIL_OPCODE_PARSE_F64 = 159,
        VIGIL_OPCODE_PARSE_BOOL = 160,

        /* Self-recursion fast path: the compiler emits this when a
           function calls itself.  Takes one u32 operand (arg_count).
           The VM reuses the current frame's function and chunk pointers
           directly, skipping the sibling constant lookup. */
        VIGIL_OPCODE_CALL_SELF = 161,

        /* Fused i32 compare + conditional jump superinstructions.
           Format: [opcode(1)][u32 jump_offset]  (6 bytes)
           Pops two i32 values, compares, jumps if false.
           Eliminates the intermediate bool push/pop and one dispatch. */
        VIGIL_OPCODE_LESS_I32_JUMP_IF_FALSE = 162,
        VIGIL_OPCODE_LESS_EQUAL_I32_JUMP_IF_FALSE = 163,
        VIGIL_OPCODE_GREATER_I32_JUMP_IF_FALSE = 164,
        VIGIL_OPCODE_GREATER_EQUAL_I32_JUMP_IF_FALSE = 165,
        VIGIL_OPCODE_EQUAL_I32_JUMP_IF_FALSE = 166,
        VIGIL_OPCODE_NOT_EQUAL_I32_JUMP_IF_FALSE = 167,

        /* Fused i64 compare + conditional jump superinstructions.
           Format: [opcode(1)][u32 jump_offset]  (6 bytes)
           Pops two i64 values, compares, jumps if the condition is false.
           Eliminates the intermediate bool push/pop and one dispatch cycle. */
        VIGIL_OPCODE_LESS_I64_JUMP_IF_FALSE = 168,
        VIGIL_OPCODE_LESS_EQUAL_I64_JUMP_IF_FALSE = 169,
        VIGIL_OPCODE_GREATER_I64_JUMP_IF_FALSE = 170,
        VIGIL_OPCODE_GREATER_EQUAL_I64_JUMP_IF_FALSE = 171,
        VIGIL_OPCODE_EQUAL_I64_JUMP_IF_FALSE = 172,
        VIGIL_OPCODE_NOT_EQUAL_I64_JUMP_IF_FALSE = 173,

        /* i64 local increment — analogous to INCREMENT_LOCAL_I32.
           Format: [opcode(1)][u32 local_idx][i8 delta]  (6 bytes) */
        VIGIL_OPCODE_INCREMENT_LOCAL_I64 = 174,

        /* Fused i64 loop: increment + compare + conditional backward jump.
           Format: [opcode(1)][u32 local_idx][i8 delta][u32 limit_const_idx]
                   [u8 cmp][u32 back_offset]  (15 bytes)
           cmp encoding: 0=<  1=<=  2=>  3=>=  4=!= */
        VIGIL_OPCODE_FORLOOP_I64 = 175,

        /* ── f64-specific opcodes ─────────────────────────────────────
           These operate on values known at compile time to be f64.
           They skip the runtime nanbox type check entirely. */
        VIGIL_OPCODE_ADD_F64 = 176,
        VIGIL_OPCODE_SUBTRACT_F64 = 177,
        VIGIL_OPCODE_MULTIPLY_F64 = 178,
        VIGIL_OPCODE_DIVIDE_F64 = 179,

        /* Two-address f64 superinstructions: read two locals, operate,
           push result.  Eliminates two GET_LOCAL dispatches. */
        VIGIL_OPCODE_LOCALS_ADD_F64 = 180,
        VIGIL_OPCODE_LOCALS_SUBTRACT_F64 = 181,
        VIGIL_OPCODE_LOCALS_MULTIPLY_F64 = 182,

        /* Three-address f64 superinstructions: read two locals, operate,
           store result to a third local.  Zero stack traffic. */
        VIGIL_OPCODE_LOCALS_ADD_F64_STORE = 183,
        VIGIL_OPCODE_LOCALS_SUBTRACT_F64_STORE = 184,
        VIGIL_OPCODE_LOCALS_MULTIPLY_F64_STORE = 185,

        /* Fused f64 arithmetic + store: pop two f64 values, operate,
           store result to local.  Eliminates SET_LOCAL + POP dispatches.
           Format: [opcode(1)][u32 dst_local]  (5 bytes) */
        VIGIL_OPCODE_ADD_F64_STORE = 186,
        VIGIL_OPCODE_SUBTRACT_F64_STORE = 187,
        VIGIL_OPCODE_MULTIPLY_F64_STORE = 188,
        VIGIL_OPCODE_STRING_NEXT_CHAR = 189,
        VIGIL_OPCODE_ARRAY_SORT = 190,
        VIGIL_OPCODE_ARRAY_SORT_DESC = 191,
        VIGIL_OPCODE_ARRAY_REVERSE = 192,
        VIGIL_OPCODE_ARRAY_INDEX_OF = 193,
        VIGIL_OPCODE_ARRAY_REMOVE_AT = 194,
        VIGIL_OPCODE_ARRAY_INSERT_AT = 195,
        VIGIL_OPCODE_ARRAY_CLEAR = 196,
        VIGIL_OPCODE_MAP_CLEAR = 197,
        VIGIL_OPCODE_STRING_PAD_LEFT = 198,
        VIGIL_OPCODE_STRING_PAD_RIGHT = 199,
        VIGIL_OPCODE_STRING_IS_DIGIT = 200,
        VIGIL_OPCODE_STRING_IS_ALPHA = 201,
        VIGIL_OPCODE_STRING_IS_ALNUM = 202,
        VIGIL_OPCODE_STRING_IS_SPACE = 203,
        VIGIL_OPCODE_STRING_IS_UPPER = 204,
        VIGIL_OPCODE_STRING_IS_LOWER = 205
    } vigil_opcode_t;

    /* Forward-declare the register chunk for the translation cache. */
    typedef struct vigil_reg_chunk vigil_reg_chunk_t;

    typedef struct vigil_chunk
    {
        vigil_runtime_t *runtime;
        vigil_byte_buffer_t code;
        vigil_source_span_t *spans;
        size_t span_count;
        size_t span_capacity;
        vigil_value_t *constants;
        size_t constant_count;
        size_t constant_capacity;
        vigil_debug_local_table_t debug_locals;
        /* Cached register-VM translation (lazily populated, owned). */
        vigil_reg_chunk_t *reg_cache;
        /* Translation cache state: 0 = empty, 1 = translating, 2 = ready. */
        volatile int64_t reg_cache_state;
    } vigil_chunk_t;

    VIGIL_API void vigil_chunk_init(vigil_chunk_t *chunk, vigil_runtime_t *runtime);
    VIGIL_API void vigil_chunk_clear(vigil_chunk_t *chunk);
    VIGIL_API void vigil_chunk_free(vigil_chunk_t *chunk);
    VIGIL_API size_t vigil_chunk_code_size(const vigil_chunk_t *chunk);
    VIGIL_API const uint8_t *vigil_chunk_code(const vigil_chunk_t *chunk);
    VIGIL_API size_t vigil_chunk_constant_count(const vigil_chunk_t *chunk);
    /*
     * The returned pointer is invalidated by any subsequent chunk mutation or
     * lifetime operation that can reallocate or clear the constant pool.
     */
    VIGIL_API const vigil_value_t *vigil_chunk_constant(const vigil_chunk_t *chunk, size_t index);
    VIGIL_API vigil_source_span_t vigil_chunk_span_at(const vigil_chunk_t *chunk, size_t offset);
    VIGIL_API const char *vigil_opcode_name(vigil_opcode_t opcode);
    VIGIL_API uint8_t vigil_opcode_size(vigil_opcode_t opcode);
    VIGIL_API vigil_status_t vigil_chunk_write_byte(vigil_chunk_t *chunk, uint8_t value, vigil_source_span_t span,
                                                    vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_chunk_write_opcode(vigil_chunk_t *chunk, vigil_opcode_t opcode,
                                                      vigil_source_span_t span, vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_chunk_write_u32(vigil_chunk_t *chunk, uint32_t value, vigil_source_span_t span,
                                                   vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_chunk_add_constant(vigil_chunk_t *chunk, const vigil_value_t *value,
                                                      size_t *out_index, vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_chunk_write_constant(vigil_chunk_t *chunk, const vigil_value_t *value,
                                                        vigil_source_span_t span, size_t *out_index,
                                                        vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_chunk_disassemble(const vigil_chunk_t *chunk, vigil_string_t *output,
                                                     vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
