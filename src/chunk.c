#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "internal/vigil_regvm.h"
#include "platform/platform.h"
#include "vigil/chunk.h"

#define VIGIL_REG_CACHE_STATE_EMPTY 0LL
#define VIGIL_REG_CACHE_STATE_BUILDING 1LL
#define VIGIL_REG_CACHE_STATE_READY 2LL

static const char *const kVigilOpcodeNames[VIGIL_OPCODE_MULTIPLY_F64_STORE + 1] = {
    [VIGIL_OPCODE_CONSTANT] = "CONSTANT",
    [VIGIL_OPCODE_NIL] = "NIL",
    [VIGIL_OPCODE_TRUE] = "TRUE",
    [VIGIL_OPCODE_FALSE] = "FALSE",
    [VIGIL_OPCODE_RETURN] = "RETURN",
    [VIGIL_OPCODE_POP] = "POP",
    [VIGIL_OPCODE_DUP] = "DUP",
    [VIGIL_OPCODE_DUP_TWO] = "DUP_TWO",
    [VIGIL_OPCODE_GET_LOCAL] = "GET_LOCAL",
    [VIGIL_OPCODE_SET_LOCAL] = "SET_LOCAL",
    [VIGIL_OPCODE_GET_GLOBAL] = "GET_GLOBAL",
    [VIGIL_OPCODE_SET_GLOBAL] = "SET_GLOBAL",
    [VIGIL_OPCODE_GET_FUNCTION] = "GET_FUNCTION",
    [VIGIL_OPCODE_NEW_CLOSURE] = "NEW_CLOSURE",
    [VIGIL_OPCODE_GET_CAPTURE] = "GET_CAPTURE",
    [VIGIL_OPCODE_SET_CAPTURE] = "SET_CAPTURE",
    [VIGIL_OPCODE_JUMP] = "JUMP",
    [VIGIL_OPCODE_JUMP_IF_FALSE] = "JUMP_IF_FALSE",
    [VIGIL_OPCODE_LOOP] = "LOOP",
    [VIGIL_OPCODE_ADD] = "ADD",
    [VIGIL_OPCODE_SUBTRACT] = "SUBTRACT",
    [VIGIL_OPCODE_MULTIPLY] = "MULTIPLY",
    [VIGIL_OPCODE_DIVIDE] = "DIVIDE",
    [VIGIL_OPCODE_MODULO] = "MODULO",
    [VIGIL_OPCODE_BITWISE_AND] = "BITWISE_AND",
    [VIGIL_OPCODE_BITWISE_OR] = "BITWISE_OR",
    [VIGIL_OPCODE_BITWISE_XOR] = "BITWISE_XOR",
    [VIGIL_OPCODE_SHIFT_LEFT] = "SHIFT_LEFT",
    [VIGIL_OPCODE_SHIFT_RIGHT] = "SHIFT_RIGHT",
    [VIGIL_OPCODE_NEGATE] = "NEGATE",
    [VIGIL_OPCODE_NOT] = "NOT",
    [VIGIL_OPCODE_BITWISE_NOT] = "BITWISE_NOT",
    [VIGIL_OPCODE_TO_I32] = "TO_I32",
    [VIGIL_OPCODE_TO_I64] = "TO_I64",
    [VIGIL_OPCODE_TO_U8] = "TO_U8",
    [VIGIL_OPCODE_TO_U32] = "TO_U32",
    [VIGIL_OPCODE_TO_U64] = "TO_U64",
    [VIGIL_OPCODE_TO_F64] = "TO_F64",
    [VIGIL_OPCODE_TO_STRING] = "TO_STRING",
    [VIGIL_OPCODE_FORMAT_F64] = "FORMAT_F64",
    [VIGIL_OPCODE_NEW_ERROR] = "NEW_ERROR",
    [VIGIL_OPCODE_GET_ERROR_KIND] = "GET_ERROR_KIND",
    [VIGIL_OPCODE_GET_ERROR_MESSAGE] = "GET_ERROR_MESSAGE",
    [VIGIL_OPCODE_EQUAL] = "EQUAL",
    [VIGIL_OPCODE_GREATER] = "GREATER",
    [VIGIL_OPCODE_LESS] = "LESS",
    [VIGIL_OPCODE_CALL] = "CALL",
    [VIGIL_OPCODE_CALL_VALUE] = "CALL_VALUE",
    [VIGIL_OPCODE_NEW_INSTANCE] = "NEW_INSTANCE",
    [VIGIL_OPCODE_GET_FIELD] = "GET_FIELD",
    [VIGIL_OPCODE_SET_FIELD] = "SET_FIELD",
    [VIGIL_OPCODE_NEW_ARRAY] = "NEW_ARRAY",
    [VIGIL_OPCODE_NEW_MAP] = "NEW_MAP",
    [VIGIL_OPCODE_GET_INDEX] = "GET_INDEX",
    [VIGIL_OPCODE_SET_INDEX] = "SET_INDEX",
    [VIGIL_OPCODE_GET_COLLECTION_SIZE] = "GET_COLLECTION_SIZE",
    [VIGIL_OPCODE_GET_MAP_KEY_AT] = "GET_MAP_KEY_AT",
    [VIGIL_OPCODE_GET_MAP_VALUE_AT] = "GET_MAP_VALUE_AT",
    [VIGIL_OPCODE_CALL_INTERFACE] = "CALL_INTERFACE",
    [VIGIL_OPCODE_DEFER_CALL] = "DEFER_CALL",
    [VIGIL_OPCODE_DEFER_CALL_VALUE] = "DEFER_CALL_VALUE",
    [VIGIL_OPCODE_DEFER_NEW_INSTANCE] = "DEFER_NEW_INSTANCE",
    [VIGIL_OPCODE_DEFER_CALL_INTERFACE] = "DEFER_CALL_INTERFACE",
    [VIGIL_OPCODE_GET_STRING_SIZE] = "GET_STRING_SIZE",
    [VIGIL_OPCODE_STRING_CONTAINS] = "STRING_CONTAINS",
    [VIGIL_OPCODE_STRING_STARTS_WITH] = "STRING_STARTS_WITH",
    [VIGIL_OPCODE_STRING_ENDS_WITH] = "STRING_ENDS_WITH",
    [VIGIL_OPCODE_STRING_TRIM] = "STRING_TRIM",
    [VIGIL_OPCODE_STRING_TO_UPPER] = "STRING_TO_UPPER",
    [VIGIL_OPCODE_STRING_TO_LOWER] = "STRING_TO_LOWER",
    [VIGIL_OPCODE_STRING_REPLACE] = "STRING_REPLACE",
    [VIGIL_OPCODE_STRING_SPLIT] = "STRING_SPLIT",
    [VIGIL_OPCODE_STRING_INDEX_OF] = "STRING_INDEX_OF",
    [VIGIL_OPCODE_STRING_SUBSTR] = "STRING_SUBSTR",
    [VIGIL_OPCODE_STRING_BYTES] = "STRING_BYTES",
    [VIGIL_OPCODE_STRING_CHAR_AT] = "STRING_CHAR_AT",
    [VIGIL_OPCODE_ARRAY_PUSH] = "ARRAY_PUSH",
    [VIGIL_OPCODE_ARRAY_POP] = "ARRAY_POP",
    [VIGIL_OPCODE_ARRAY_GET_SAFE] = "ARRAY_GET_SAFE",
    [VIGIL_OPCODE_ARRAY_SET_SAFE] = "ARRAY_SET_SAFE",
    [VIGIL_OPCODE_ARRAY_SLICE] = "ARRAY_SLICE",
    [VIGIL_OPCODE_ARRAY_CONTAINS] = "ARRAY_CONTAINS",
    [VIGIL_OPCODE_MAP_GET_SAFE] = "MAP_GET_SAFE",
    [VIGIL_OPCODE_MAP_SET_SAFE] = "MAP_SET_SAFE",
    [VIGIL_OPCODE_MAP_REMOVE_SAFE] = "MAP_REMOVE_SAFE",
    [VIGIL_OPCODE_MAP_HAS] = "MAP_HAS",
    [VIGIL_OPCODE_MAP_KEYS] = "MAP_KEYS",
    [VIGIL_OPCODE_MAP_VALUES] = "MAP_VALUES",
    [VIGIL_OPCODE_ADD_I64] = "ADD_I64",
    [VIGIL_OPCODE_SUBTRACT_I64] = "SUBTRACT_I64",
    [VIGIL_OPCODE_LESS_I64] = "LESS_I64",
    [VIGIL_OPCODE_LESS_EQUAL_I64] = "LESS_EQUAL_I64",
    [VIGIL_OPCODE_GREATER_I64] = "GREATER_I64",
    [VIGIL_OPCODE_GREATER_EQUAL_I64] = "GREATER_EQUAL_I64",
    [VIGIL_OPCODE_MULTIPLY_I64] = "MULTIPLY_I64",
    [VIGIL_OPCODE_DIVIDE_I64] = "DIVIDE_I64",
    [VIGIL_OPCODE_MODULO_I64] = "MODULO_I64",
    [VIGIL_OPCODE_EQUAL_I64] = "EQUAL_I64",
    [VIGIL_OPCODE_NOT_EQUAL_I64] = "NOT_EQUAL_I64",
    [VIGIL_OPCODE_LOCALS_ADD_I64] = "LOCALS_ADD_I64",
    [VIGIL_OPCODE_LOCALS_SUBTRACT_I64] = "LOCALS_SUBTRACT_I64",
    [VIGIL_OPCODE_LOCALS_MULTIPLY_I64] = "LOCALS_MULTIPLY_I64",
    [VIGIL_OPCODE_LOCALS_MODULO_I64] = "LOCALS_MODULO_I64",
    [VIGIL_OPCODE_LOCALS_LESS_I64] = "LOCALS_LESS_I64",
    [VIGIL_OPCODE_LOCALS_LESS_EQUAL_I64] = "LOCALS_LESS_EQUAL_I64",
    [VIGIL_OPCODE_LOCALS_GREATER_I64] = "LOCALS_GREATER_I64",
    [VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I64] = "LOCALS_GREATER_EQUAL_I64",
    [VIGIL_OPCODE_LOCALS_EQUAL_I64] = "LOCALS_EQUAL_I64",
    [VIGIL_OPCODE_LOCALS_NOT_EQUAL_I64] = "LOCALS_NOT_EQUAL_I64",
    [VIGIL_OPCODE_ADD_I32] = "ADD_I32",
    [VIGIL_OPCODE_SUBTRACT_I32] = "SUBTRACT_I32",
    [VIGIL_OPCODE_MULTIPLY_I32] = "MULTIPLY_I32",
    [VIGIL_OPCODE_LESS_I32] = "LESS_I32",
    [VIGIL_OPCODE_LESS_EQUAL_I32] = "LESS_EQUAL_I32",
    [VIGIL_OPCODE_GREATER_I32] = "GREATER_I32",
    [VIGIL_OPCODE_GREATER_EQUAL_I32] = "GREATER_EQUAL_I32",
    [VIGIL_OPCODE_EQUAL_I32] = "EQUAL_I32",
    [VIGIL_OPCODE_NOT_EQUAL_I32] = "NOT_EQUAL_I32",
    [VIGIL_OPCODE_MODULO_I32] = "MODULO_I32",
    [VIGIL_OPCODE_DIVIDE_I32] = "DIVIDE_I32",
    [VIGIL_OPCODE_LOCALS_ADD_I32_STORE] = "LOCALS_ADD_I32_STORE",
    [VIGIL_OPCODE_LOCALS_SUBTRACT_I32_STORE] = "LOCALS_SUBTRACT_I32_STORE",
    [VIGIL_OPCODE_LOCALS_MULTIPLY_I32_STORE] = "LOCALS_MULTIPLY_I32_STORE",
    [VIGIL_OPCODE_LOCALS_LESS_I32_STORE] = "LOCALS_LESS_I32_STORE",
    [VIGIL_OPCODE_LOCALS_LESS_EQUAL_I32_STORE] = "LOCALS_LESS_EQUAL_I32_STORE",
    [VIGIL_OPCODE_LOCALS_GREATER_I32_STORE] = "LOCALS_GREATER_I32_STORE",
    [VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I32_STORE] = "LOCALS_GREATER_EQUAL_I32_STORE",
    [VIGIL_OPCODE_LOCALS_EQUAL_I32_STORE] = "LOCALS_EQUAL_I32_STORE",
    [VIGIL_OPCODE_LOCALS_NOT_EQUAL_I32_STORE] = "LOCALS_NOT_EQUAL_I32_STORE",
    [VIGIL_OPCODE_LOCALS_MODULO_I32_STORE] = "LOCALS_MODULO_I32_STORE",
    [VIGIL_OPCODE_INCREMENT_LOCAL_I32] = "INCREMENT_LOCAL_I32",
    [VIGIL_OPCODE_TAIL_CALL] = "TAIL_CALL",
    [VIGIL_OPCODE_FORLOOP_I32] = "FORLOOP_I32",
    [VIGIL_OPCODE_CALL_NATIVE] = "CALL_NATIVE",
    [VIGIL_OPCODE_STRING_TRIM_LEFT] = "STRING_TRIM_LEFT",
    [VIGIL_OPCODE_STRING_TRIM_RIGHT] = "STRING_TRIM_RIGHT",
    [VIGIL_OPCODE_STRING_REPEAT] = "STRING_REPEAT",
    [VIGIL_OPCODE_STRING_REVERSE] = "STRING_REVERSE",
    [VIGIL_OPCODE_STRING_IS_EMPTY] = "STRING_IS_EMPTY",
    [VIGIL_OPCODE_STRING_COUNT] = "STRING_COUNT",
    [VIGIL_OPCODE_STRING_LAST_INDEX_OF] = "STRING_LAST_INDEX_OF",
    [VIGIL_OPCODE_STRING_TRIM_PREFIX] = "STRING_TRIM_PREFIX",
    [VIGIL_OPCODE_STRING_TRIM_SUFFIX] = "STRING_TRIM_SUFFIX",
    [VIGIL_OPCODE_CHAR_FROM_INT] = "CHAR_FROM_INT",
    [VIGIL_OPCODE_STRING_TO_C] = "STRING_TO_C",
    [VIGIL_OPCODE_STRING_JOIN] = "STRING_JOIN",
    [VIGIL_OPCODE_STRING_CUT] = "STRING_CUT",
    [VIGIL_OPCODE_STRING_FIELDS] = "STRING_FIELDS",
    [VIGIL_OPCODE_STRING_EQUAL_FOLD] = "STRING_EQUAL_FOLD",
    [VIGIL_OPCODE_STRING_CHAR_COUNT] = "STRING_CHAR_COUNT",
    [VIGIL_OPCODE_FORMAT_SPEC] = "FORMAT_SPEC",
    [VIGIL_OPCODE_DEFER_CALL_NATIVE] = "DEFER_CALL_NATIVE",
    [VIGIL_OPCODE_CALL_EXTERN] = "CALL_EXTERN",
    [VIGIL_OPCODE_MATH_SIN_F64] = "MATH_SIN_F64",
    [VIGIL_OPCODE_MATH_COS_F64] = "MATH_COS_F64",
    [VIGIL_OPCODE_MATH_SQRT_F64] = "MATH_SQRT_F64",
    [VIGIL_OPCODE_MATH_LOG_F64] = "MATH_LOG_F64",
    [VIGIL_OPCODE_MATH_POW_F64] = "MATH_POW_F64",
    [VIGIL_OPCODE_PARSE_I32] = "PARSE_I32",
    [VIGIL_OPCODE_PARSE_F64] = "PARSE_F64",
    [VIGIL_OPCODE_PARSE_BOOL] = "PARSE_BOOL",
    [VIGIL_OPCODE_CALL_SELF] = "CALL_SELF",
    [VIGIL_OPCODE_LESS_I32_JUMP_IF_FALSE] = "LESS_I32_JUMP_IF_FALSE",
    [VIGIL_OPCODE_LESS_EQUAL_I32_JUMP_IF_FALSE] = "LESS_EQUAL_I32_JUMP_IF_FALSE",
    [VIGIL_OPCODE_GREATER_I32_JUMP_IF_FALSE] = "GREATER_I32_JUMP_IF_FALSE",
    [VIGIL_OPCODE_GREATER_EQUAL_I32_JUMP_IF_FALSE] = "GREATER_EQUAL_I32_JUMP_IF_FALSE",
    [VIGIL_OPCODE_EQUAL_I32_JUMP_IF_FALSE] = "EQUAL_I32_JUMP_IF_FALSE",
    [VIGIL_OPCODE_NOT_EQUAL_I32_JUMP_IF_FALSE] = "NOT_EQUAL_I32_JUMP_IF_FALSE",
    [VIGIL_OPCODE_LESS_I64_JUMP_IF_FALSE] = "LESS_I64_JUMP_IF_FALSE",
    [VIGIL_OPCODE_LESS_EQUAL_I64_JUMP_IF_FALSE] = "LESS_EQUAL_I64_JUMP_IF_FALSE",
    [VIGIL_OPCODE_GREATER_I64_JUMP_IF_FALSE] = "GREATER_I64_JUMP_IF_FALSE",
    [VIGIL_OPCODE_GREATER_EQUAL_I64_JUMP_IF_FALSE] = "GREATER_EQUAL_I64_JUMP_IF_FALSE",
    [VIGIL_OPCODE_EQUAL_I64_JUMP_IF_FALSE] = "EQUAL_I64_JUMP_IF_FALSE",
    [VIGIL_OPCODE_NOT_EQUAL_I64_JUMP_IF_FALSE] = "NOT_EQUAL_I64_JUMP_IF_FALSE",
    [VIGIL_OPCODE_INCREMENT_LOCAL_I64] = "INCREMENT_LOCAL_I64",
    [VIGIL_OPCODE_FORLOOP_I64] = "FORLOOP_I64",
    [VIGIL_OPCODE_ADD_F64] = "ADD_F64",
    [VIGIL_OPCODE_SUBTRACT_F64] = "SUBTRACT_F64",
    [VIGIL_OPCODE_MULTIPLY_F64] = "MULTIPLY_F64",
    [VIGIL_OPCODE_DIVIDE_F64] = "DIVIDE_F64",
    [VIGIL_OPCODE_LOCALS_ADD_F64] = "LOCALS_ADD_F64",
    [VIGIL_OPCODE_LOCALS_SUBTRACT_F64] = "LOCALS_SUBTRACT_F64",
    [VIGIL_OPCODE_LOCALS_MULTIPLY_F64] = "LOCALS_MULTIPLY_F64",
    [VIGIL_OPCODE_LOCALS_ADD_F64_STORE] = "LOCALS_ADD_F64_STORE",
    [VIGIL_OPCODE_LOCALS_SUBTRACT_F64_STORE] = "LOCALS_SUBTRACT_F64_STORE",
    [VIGIL_OPCODE_LOCALS_MULTIPLY_F64_STORE] = "LOCALS_MULTIPLY_F64_STORE",
    [VIGIL_OPCODE_ADD_F64_STORE] = "ADD_F64_STORE",
    [VIGIL_OPCODE_SUBTRACT_F64_STORE] = "SUBTRACT_F64_STORE",
    [VIGIL_OPCODE_MULTIPLY_F64_STORE] = "MULTIPLY_F64_STORE",
};

static vigil_status_t vigil_chunk_append_text(vigil_string_t *output, const char *text, vigil_error_t *error);
static vigil_status_t vigil_chunk_append_value(vigil_string_t *output, const vigil_value_t *value,
                                               vigil_error_t *error);

static uint32_t vigil_chunk_decode_u32(const uint8_t *bytes, size_t offset)
{
    uint32_t value;

    value = (uint32_t)bytes[offset];
    value |= (uint32_t)bytes[offset + 1U] << 8U;
    value |= (uint32_t)bytes[offset + 2U] << 16U;
    value |= (uint32_t)bytes[offset + 3U] << 24U;
    return value;
}

static vigil_status_t vigil_chunk_append_formatted(vigil_string_t *output, vigil_error_t *error,
                                                   const char *failure_message, const char *format, ...)
{
    char line[64];
    int written;
    va_list args;

    va_start(args, format);
    written = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (written < 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, failure_message);
        return VIGIL_STATUS_INTERNAL;
    }

    return vigil_string_append(output, line, (size_t)written, error);
}

vigil_status_t vigil_chunk_ensure_reg_cache(vigil_chunk_t *chunk, uint8_t arity,
                                            const vigil_reg_chunk_t **out_reg_cache, vigil_error_t *error)
{
    vigil_reg_chunk_t *reg_cache;
    vigil_status_t status;

    if (out_reg_cache == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_reg_cache must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_reg_cache = NULL;

    if (chunk == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "chunk must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    for (;;)
    {
        int64_t state = vigil_atomic_load(&chunk->reg_cache_state);

        if (state == VIGIL_REG_CACHE_STATE_READY)
        {
            reg_cache = chunk->reg_cache;
            if (reg_cache == NULL)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "chunk reg_cache ready state was null");
                return VIGIL_STATUS_INTERNAL;
            }
            *out_reg_cache = reg_cache;
            return VIGIL_STATUS_OK;
        }

        if (state == VIGIL_REG_CACHE_STATE_EMPTY &&
            vigil_atomic_cas(&chunk->reg_cache_state, VIGIL_REG_CACHE_STATE_EMPTY, VIGIL_REG_CACHE_STATE_BUILDING))
        {
            reg_cache = malloc(sizeof(*reg_cache));
            if (reg_cache == NULL)
            {
                vigil_atomic_store(&chunk->reg_cache_state, VIGIL_REG_CACHE_STATE_EMPTY);
                return VIGIL_STATUS_OUT_OF_MEMORY;
            }

            reg_cache->arity = arity;
            status = vigil_reg_translate(chunk, reg_cache, chunk->runtime, error);
            if (status != VIGIL_STATUS_OK)
            {
                free(reg_cache);
                vigil_atomic_store(&chunk->reg_cache_state, VIGIL_REG_CACHE_STATE_EMPTY);
                return status;
            }

            chunk->reg_cache = reg_cache;
            vigil_atomic_store(&chunk->reg_cache_state, VIGIL_REG_CACHE_STATE_READY);
            *out_reg_cache = reg_cache;
            return VIGIL_STATUS_OK;
        }

        while (vigil_atomic_load(&chunk->reg_cache_state) == VIGIL_REG_CACHE_STATE_BUILDING)
        {
        }
    }
}

static vigil_status_t vigil_chunk_require_operand_bytes(const vigil_chunk_t *chunk, size_t offset, size_t bytes,
                                                        const char *failure_message, vigil_error_t *error)
{
    if (offset + bytes >= chunk->code.length)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, failure_message);
        return VIGIL_STATUS_INTERNAL;
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_disassemble_call(const vigil_chunk_t *chunk, size_t *offset, vigil_string_t *output,
                                                   vigil_error_t *error)
{
    uint32_t function_index;
    uint32_t arg_count;
    vigil_status_t status;

    status = vigil_chunk_require_operand_bytes(chunk, *offset, 12U, "truncated call instruction", error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    function_index = vigil_chunk_decode_u32(chunk->code.data, *offset + 1U);
    arg_count = vigil_chunk_decode_u32(chunk->code.data, *offset + 5U);
    status = vigil_chunk_append_formatted(output, error, "failed to format chunk call operand", " %u %u",
                                          function_index, arg_count);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    *offset += 13U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_disassemble_call_value(const vigil_chunk_t *chunk, size_t *offset,
                                                         vigil_string_t *output, vigil_error_t *error)
{
    vigil_opcode_t opcode;
    uint32_t arg_count;
    uint32_t ret_count;
    vigil_status_t status;

    opcode = (vigil_opcode_t)chunk->code.data[*offset];
    if (opcode == VIGIL_OPCODE_CALL_VALUE)
    {
        status = vigil_chunk_require_operand_bytes(chunk, *offset, 8U, "truncated indirect call instruction", error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        arg_count = vigil_chunk_decode_u32(chunk->code.data, *offset + 1U);
        ret_count = vigil_chunk_decode_u32(chunk->code.data, *offset + 5U);
        status = vigil_chunk_append_formatted(output, error, "failed to format chunk indirect call operand", " %u %u",
                                              arg_count, ret_count);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        *offset += 9U;
        return VIGIL_STATUS_OK;
    }

    status = vigil_chunk_require_operand_bytes(chunk, *offset, 4U, "truncated indirect call instruction", error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    arg_count = vigil_chunk_decode_u32(chunk->code.data, *offset + 1U);
    status =
        vigil_chunk_append_formatted(output, error, "failed to format chunk indirect call operand", " %u", arg_count);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    *offset += 5U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_disassemble_closure(const vigil_chunk_t *chunk, size_t *offset,
                                                      vigil_string_t *output, vigil_error_t *error)
{
    uint32_t function_index;
    uint32_t capture_count;
    vigil_status_t status;

    status = vigil_chunk_require_operand_bytes(chunk, *offset, 8U, "truncated closure instruction", error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    function_index = vigil_chunk_decode_u32(chunk->code.data, *offset + 1U);
    capture_count = vigil_chunk_decode_u32(chunk->code.data, *offset + 5U);
    status = vigil_chunk_append_formatted(output, error, "failed to format chunk closure operand", " %u %u",
                                          function_index, capture_count);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    *offset += 9U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_disassemble_interface_call(const vigil_chunk_t *chunk, size_t *offset,
                                                             vigil_string_t *output, vigil_error_t *error)
{
    vigil_opcode_t opcode;
    uint32_t interface_index;
    uint32_t method_index;
    uint32_t arg_count;
    uint32_t ret_count;
    vigil_status_t status;

    opcode = (vigil_opcode_t)chunk->code.data[*offset];
    status = vigil_chunk_require_operand_bytes(chunk, *offset, opcode == VIGIL_OPCODE_CALL_INTERFACE ? 16U : 12U,
                                               "truncated interface call instruction", error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    interface_index = vigil_chunk_decode_u32(chunk->code.data, *offset + 1U);
    method_index = vigil_chunk_decode_u32(chunk->code.data, *offset + 5U);
    arg_count = vigil_chunk_decode_u32(chunk->code.data, *offset + 9U);
    if (opcode == VIGIL_OPCODE_CALL_INTERFACE)
    {
        ret_count = vigil_chunk_decode_u32(chunk->code.data, *offset + 13U);
        status = vigil_chunk_append_formatted(output, error, "failed to format chunk interface call operand",
                                              " %u %u %u %u", interface_index, method_index, arg_count, ret_count);
    }
    else
    {
        status = vigil_chunk_append_formatted(output, error, "failed to format chunk interface call operand",
                                              " %u %u %u", interface_index, method_index, arg_count);
    }
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    *offset += opcode == VIGIL_OPCODE_CALL_INTERFACE ? 17U : 13U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_disassemble_call_extern(const vigil_chunk_t *chunk, size_t *offset,
                                                          vigil_string_t *output, vigil_error_t *error)
{
    uint32_t descriptor_index;
    uint32_t arg_count;
    uint32_t ret_count;
    vigil_status_t status;

    status = vigil_chunk_require_operand_bytes(chunk, *offset, 12U, "truncated extern call instruction", error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    descriptor_index = vigil_chunk_decode_u32(chunk->code.data, *offset + 1U);
    arg_count = vigil_chunk_decode_u32(chunk->code.data, *offset + 5U);
    ret_count = vigil_chunk_decode_u32(chunk->code.data, *offset + 9U);
    status = vigil_chunk_append_formatted(output, error, "failed to format chunk extern call operand", " %u %u %u",
                                          descriptor_index, arg_count, ret_count);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    *offset += 13U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_disassemble_increment_local(const vigil_chunk_t *chunk, size_t *offset,
                                                              vigil_string_t *output, vigil_error_t *error)
{
    uint32_t local_index;
    int8_t delta;
    vigil_status_t status;

    status = vigil_chunk_require_operand_bytes(chunk, *offset, 5U, "truncated increment-local instruction", error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    local_index = vigil_chunk_decode_u32(chunk->code.data, *offset + 1U);
    delta = (int8_t)chunk->code.data[*offset + 5U];
    status = vigil_chunk_append_formatted(output, error, "failed to format increment-local operands", " %u %d",
                                          local_index, (int)delta);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    *offset += 6U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_disassemble_forloop(const vigil_chunk_t *chunk, size_t *offset,
                                                      vigil_string_t *output, vigil_error_t *error)
{
    uint32_t local_index;
    int8_t delta;
    uint32_t limit_constant;
    uint8_t comparison_kind;
    uint32_t back_offset;
    vigil_status_t status;

    status = vigil_chunk_require_operand_bytes(chunk, *offset, 14U, "truncated for-loop instruction", error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    local_index = vigil_chunk_decode_u32(chunk->code.data, *offset + 1U);
    delta = (int8_t)chunk->code.data[*offset + 5U];
    limit_constant = vigil_chunk_decode_u32(chunk->code.data, *offset + 6U);
    comparison_kind = chunk->code.data[*offset + 10U];
    back_offset = vigil_chunk_decode_u32(chunk->code.data, *offset + 11U);
    status =
        vigil_chunk_append_formatted(output, error, "failed to format for-loop operands", " %u %d %u %u %u",
                                     local_index, (int)delta, limit_constant, (unsigned)comparison_kind, back_offset);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    *offset += 15U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_disassemble_two_u32_operands(const vigil_chunk_t *chunk, size_t *offset,
                                                               vigil_string_t *output, vigil_error_t *error,
                                                               const char *truncated_message,
                                                               const char *format_failure_message)
{
    uint32_t first_operand;
    uint32_t second_operand;
    vigil_status_t status;

    status = vigil_chunk_require_operand_bytes(chunk, *offset, 8U, truncated_message, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    first_operand = vigil_chunk_decode_u32(chunk->code.data, *offset + 1U);
    second_operand = vigil_chunk_decode_u32(chunk->code.data, *offset + 5U);
    status =
        vigil_chunk_append_formatted(output, error, format_failure_message, " %u %u", first_operand, second_operand);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    *offset += 9U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_disassemble_return(const vigil_chunk_t *chunk, size_t *offset, vigil_string_t *output,
                                                     vigil_error_t *error)
{
    uint32_t operand;
    vigil_status_t status;

    if (*offset + 4U >= chunk->code.length)
    {
        *offset += 1U;
        return VIGIL_STATUS_OK;
    }

    operand = vigil_chunk_decode_u32(chunk->code.data, *offset + 1U);
    status = vigil_chunk_append_formatted(output, error, "failed to format chunk return count", " %u", operand);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    *offset += 5U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_disassemble_u32_operand(const vigil_chunk_t *chunk, size_t *offset,
                                                          vigil_string_t *output, vigil_error_t *error)
{
    uint32_t operand;
    vigil_status_t status;

    status = vigil_chunk_require_operand_bytes(chunk, *offset, 4U, "truncated constant instruction", error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    operand = vigil_chunk_decode_u32(chunk->code.data, *offset + 1U);
    status = vigil_chunk_append_formatted(output, error, "failed to format chunk constant index", " %u", operand);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if ((vigil_opcode_t)chunk->code.data[*offset] == VIGIL_OPCODE_CONSTANT)
    {
        status = vigil_chunk_append_text(output, " ", error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        status = vigil_chunk_append_value(output, vigil_chunk_constant(chunk, (size_t)operand), error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }

    *offset += 5U;
    return VIGIL_STATUS_OK;
}

static int vigil_chunk_is_call_opcode(vigil_opcode_t opcode)
{
    return opcode == VIGIL_OPCODE_CALL || opcode == VIGIL_OPCODE_DEFER_CALL;
}

static int vigil_chunk_is_call_value_opcode(vigil_opcode_t opcode)
{
    return opcode == VIGIL_OPCODE_CALL_VALUE || opcode == VIGIL_OPCODE_DEFER_CALL_VALUE;
}

static int vigil_chunk_is_interface_call_opcode(vigil_opcode_t opcode)
{
    return opcode == VIGIL_OPCODE_CALL_INTERFACE || opcode == VIGIL_OPCODE_DEFER_CALL_INTERFACE;
}

static int vigil_chunk_is_two_u32_operand_opcode(vigil_opcode_t opcode)
{
    return opcode == VIGIL_OPCODE_NEW_INSTANCE || opcode == VIGIL_OPCODE_NEW_ARRAY || opcode == VIGIL_OPCODE_NEW_MAP ||
           opcode == VIGIL_OPCODE_DEFER_NEW_INSTANCE || opcode == VIGIL_OPCODE_FORMAT_SPEC ||
           opcode == VIGIL_OPCODE_CALL_SELF;
}

static int vigil_chunk_is_increment_local_opcode(vigil_opcode_t opcode)
{
    return opcode == VIGIL_OPCODE_INCREMENT_LOCAL_I32 || opcode == VIGIL_OPCODE_INCREMENT_LOCAL_I64;
}

static int vigil_chunk_is_forloop_opcode(vigil_opcode_t opcode)
{
    return opcode == VIGIL_OPCODE_FORLOOP_I32 || opcode == VIGIL_OPCODE_FORLOOP_I64;
}

static int vigil_chunk_is_u32_operand_opcode(vigil_opcode_t opcode)
{
    /* Lookup table avoids a long OR-chain that inflates cyclomatic complexity. */
    static const uint8_t table[VIGIL_OPCODE_MULTIPLY_F64_STORE + 1] = {
        [VIGIL_OPCODE_CONSTANT] = 1,
        [VIGIL_OPCODE_GET_LOCAL] = 1,
        [VIGIL_OPCODE_SET_LOCAL] = 1,
        [VIGIL_OPCODE_GET_GLOBAL] = 1,
        [VIGIL_OPCODE_SET_GLOBAL] = 1,
        [VIGIL_OPCODE_GET_FUNCTION] = 1,
        [VIGIL_OPCODE_GET_CAPTURE] = 1,
        [VIGIL_OPCODE_SET_CAPTURE] = 1,
        [VIGIL_OPCODE_JUMP] = 1,
        [VIGIL_OPCODE_JUMP_IF_FALSE] = 1,
        [VIGIL_OPCODE_LOOP] = 1,
        [VIGIL_OPCODE_FORMAT_F64] = 1,
        [VIGIL_OPCODE_GET_FIELD] = 1,
        [VIGIL_OPCODE_SET_FIELD] = 1,
        [VIGIL_OPCODE_LESS_I32_JUMP_IF_FALSE] = 1,
        [VIGIL_OPCODE_LESS_EQUAL_I32_JUMP_IF_FALSE] = 1,
        [VIGIL_OPCODE_GREATER_I32_JUMP_IF_FALSE] = 1,
        [VIGIL_OPCODE_GREATER_EQUAL_I32_JUMP_IF_FALSE] = 1,
        [VIGIL_OPCODE_EQUAL_I32_JUMP_IF_FALSE] = 1,
        [VIGIL_OPCODE_NOT_EQUAL_I32_JUMP_IF_FALSE] = 1,
        [VIGIL_OPCODE_LESS_I64_JUMP_IF_FALSE] = 1,
        [VIGIL_OPCODE_LESS_EQUAL_I64_JUMP_IF_FALSE] = 1,
        [VIGIL_OPCODE_GREATER_I64_JUMP_IF_FALSE] = 1,
        [VIGIL_OPCODE_GREATER_EQUAL_I64_JUMP_IF_FALSE] = 1,
        [VIGIL_OPCODE_EQUAL_I64_JUMP_IF_FALSE] = 1,
        [VIGIL_OPCODE_NOT_EQUAL_I64_JUMP_IF_FALSE] = 1,
    };
    return (size_t)opcode < sizeof(table) && table[(size_t)opcode];
}

static vigil_status_t vigil_chunk_disassemble_instruction(const vigil_chunk_t *chunk, size_t *offset,
                                                          vigil_string_t *output, vigil_error_t *error)
{
    vigil_opcode_t opcode = (vigil_opcode_t)chunk->code.data[*offset];

    if (vigil_chunk_is_call_opcode(opcode))
    {
        return vigil_chunk_disassemble_call(chunk, offset, output, error);
    }
    if (vigil_chunk_is_call_value_opcode(opcode))
    {
        return vigil_chunk_disassemble_call_value(chunk, offset, output, error);
    }
    if (opcode == VIGIL_OPCODE_NEW_CLOSURE)
    {
        return vigil_chunk_disassemble_closure(chunk, offset, output, error);
    }
    if (vigil_chunk_is_interface_call_opcode(opcode))
    {
        return vigil_chunk_disassemble_interface_call(chunk, offset, output, error);
    }
    if (opcode == VIGIL_OPCODE_CALL_EXTERN)
    {
        return vigil_chunk_disassemble_call_extern(chunk, offset, output, error);
    }
    if (vigil_chunk_is_two_u32_operand_opcode(opcode))
    {
        return vigil_chunk_disassemble_two_u32_operands(
            chunk, offset, output, error,
            opcode == VIGIL_OPCODE_NEW_INSTANCE || opcode == VIGIL_OPCODE_DEFER_NEW_INSTANCE
                ? "truncated constructor instruction"
                : "truncated collection instruction",
            opcode == VIGIL_OPCODE_NEW_INSTANCE || opcode == VIGIL_OPCODE_DEFER_NEW_INSTANCE
                ? "failed to format chunk constructor operand"
                : "failed to format chunk collection operand");
    }
    if (vigil_chunk_is_increment_local_opcode(opcode))
    {
        return vigil_chunk_disassemble_increment_local(chunk, offset, output, error);
    }
    if (vigil_chunk_is_forloop_opcode(opcode))
    {
        return vigil_chunk_disassemble_forloop(chunk, offset, output, error);
    }
    if (opcode == VIGIL_OPCODE_RETURN)
    {
        return vigil_chunk_disassemble_return(chunk, offset, output, error);
    }
    if (vigil_chunk_is_u32_operand_opcode(opcode))
    {
        return vigil_chunk_disassemble_u32_operand(chunk, offset, output, error);
    }

    *offset += 1U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_validate_mutable(const vigil_chunk_t *chunk, vigil_error_t *error)
{
    vigil_error_clear(error);

    if (chunk == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "chunk must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (chunk->runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "chunk runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_validate_output(const vigil_chunk_t *chunk, const vigil_string_t *output,
                                                  vigil_error_t *error)
{
    vigil_status_t status;

    status = vigil_chunk_validate_mutable(chunk, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (output == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "output string must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (output->bytes.runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "output string runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_grow_spans(vigil_chunk_t *chunk, size_t minimum_capacity, vigil_error_t *error)
{
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    vigil_status_t status;

    if (minimum_capacity <= chunk->span_capacity)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    old_capacity = chunk->span_capacity;
    next_capacity = old_capacity == 0U ? 8U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > (SIZE_MAX / 2U))
        {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > (SIZE_MAX / sizeof(*chunk->spans)))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "chunk span allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    if (chunk->spans == NULL)
    {
        memory = NULL;
        status = vigil_runtime_alloc(chunk->runtime, next_capacity * sizeof(*chunk->spans), &memory, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }
    else
    {
        memory = chunk->spans;
        status = vigil_runtime_realloc(chunk->runtime, &memory, next_capacity * sizeof(*chunk->spans), error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        memset((vigil_source_span_t *)memory + old_capacity, 0, (next_capacity - old_capacity) * sizeof(*chunk->spans));
    }

    chunk->spans = (vigil_source_span_t *)memory;
    chunk->span_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_grow_constants(vigil_chunk_t *chunk, size_t minimum_capacity, vigil_error_t *error)
{
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    vigil_status_t status;

    if (minimum_capacity <= chunk->constant_capacity)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    old_capacity = chunk->constant_capacity;
    next_capacity = old_capacity == 0U ? 8U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > (SIZE_MAX / 2U))
        {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > (SIZE_MAX / sizeof(*chunk->constants)))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "chunk constant allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    if (chunk->constants == NULL)
    {
        memory = NULL;
        status = vigil_runtime_alloc(chunk->runtime, next_capacity * sizeof(*chunk->constants), &memory, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }
    else
    {
        memory = chunk->constants;
        status = vigil_runtime_realloc(chunk->runtime, &memory, next_capacity * sizeof(*chunk->constants), error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        memset((vigil_value_t *)memory + old_capacity, 0, (next_capacity - old_capacity) * sizeof(*chunk->constants));
    }

    chunk->constants = (vigil_value_t *)memory;
    chunk->constant_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_append_bytes(vigil_chunk_t *chunk, const uint8_t *bytes, size_t byte_count,
                                               vigil_source_span_t span, vigil_error_t *error)
{
    vigil_status_t status;
    size_t old_code_length;
    size_t old_span_count;
    size_t i;

    if (byte_count == 0U)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    old_code_length = chunk->code.length;
    old_span_count = chunk->span_count;
    if (byte_count > SIZE_MAX - old_code_length)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "chunk code size overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    if (byte_count > SIZE_MAX - old_span_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "chunk span table overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    status = vigil_byte_buffer_reserve(&chunk->code, old_code_length + byte_count, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_chunk_grow_spans(chunk, old_span_count + byte_count, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    memcpy(chunk->code.data + old_code_length, bytes, byte_count);
    chunk->code.length = old_code_length + byte_count;
    for (i = 0U; i < byte_count; ++i)
    {
        chunk->spans[old_span_count + i] = span;
    }

    chunk->span_count = old_span_count + byte_count;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_append_text(vigil_string_t *output, const char *text, vigil_error_t *error)
{
    return vigil_string_append_cstr(output, text, error);
}

static vigil_status_t vigil_chunk_append_value(vigil_string_t *output, const vigil_value_t *value, vigil_error_t *error)
{
    char buffer[64];
    int written;
    vigil_object_t *object;
    vigil_status_t status;

    if (value == NULL)
    {
        return vigil_chunk_append_text(output, "<null>", error);
    }

    switch (vigil_value_kind(value))
    {
    case VIGIL_VALUE_NIL:
        return vigil_chunk_append_text(output, "nil", error);
    case VIGIL_VALUE_BOOL:
        return vigil_chunk_append_text(output, vigil_value_as_bool(value) ? "true" : "false", error);
    case VIGIL_VALUE_INT:
        written = snprintf(buffer, sizeof(buffer), "%lld", (long long)vigil_value_as_int(value));
        break;
    case VIGIL_VALUE_UINT:
        written = snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)vigil_value_as_uint(value));
        break;
    case VIGIL_VALUE_FLOAT:
        written = snprintf(buffer, sizeof(buffer), "%.17g", vigil_value_as_float(value));
        break;
    case VIGIL_VALUE_OBJECT:
        object = vigil_value_as_object(value);
        if (vigil_object_type(object) == VIGIL_OBJECT_STRING)
        {
            status = vigil_chunk_append_text(output, "\"", error);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }

            status = vigil_string_append_cstr(output, vigil_string_object_c_str(object), error);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }

            return vigil_chunk_append_text(output, "\"", error);
        }

        return vigil_chunk_append_text(output, "<object>", error);
    default:
        return vigil_chunk_append_text(output, "<unknown>", error);
    }

    if (written < 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format chunk value");
        return VIGIL_STATUS_INTERNAL;
    }

    return vigil_string_append(output, buffer, (size_t)written, error);
}

void vigil_chunk_init(vigil_chunk_t *chunk, vigil_runtime_t *runtime)
{
    if (chunk == NULL)
    {
        return;
    }

    memset(chunk, 0, sizeof(*chunk));
    chunk->runtime = runtime;
    vigil_byte_buffer_init(&chunk->code, runtime);
    vigil_debug_local_table_init(&chunk->debug_locals, runtime);
}

void vigil_chunk_clear(vigil_chunk_t *chunk)
{
    size_t i;

    if (chunk == NULL)
    {
        return;
    }

    /* Free cached register translation so it is re-translated after clear. */
    if (chunk->reg_cache != NULL)
    {
        vigil_reg_chunk_free(chunk->reg_cache, chunk->runtime);
        free(chunk->reg_cache);
        chunk->reg_cache = NULL;
    }
    vigil_atomic_store(&chunk->reg_cache_state, VIGIL_REG_CACHE_STATE_EMPTY);

    vigil_byte_buffer_clear(&chunk->code);
    chunk->span_count = 0U;
    for (i = 0U; i < chunk->constant_count; ++i)
    {
        vigil_value_release(&chunk->constants[i]);
    }

    chunk->constant_count = 0U;
}

void vigil_chunk_free(vigil_chunk_t *chunk)
{
    void *memory;

    if (chunk == NULL)
    {
        return;
    }

    /* Free cached register translation. */
    if (chunk->reg_cache != NULL)
    {
        vigil_reg_chunk_free(chunk->reg_cache, chunk->runtime);
        free(chunk->reg_cache);
        chunk->reg_cache = NULL;
    }
    vigil_atomic_store(&chunk->reg_cache_state, VIGIL_REG_CACHE_STATE_EMPTY);

    vigil_chunk_clear(chunk);
    vigil_byte_buffer_free(&chunk->code);

    memory = chunk->spans;
    if (chunk->runtime != NULL)
    {
        vigil_runtime_free(chunk->runtime, &memory);
    }

    memory = chunk->constants;
    if (chunk->runtime != NULL)
    {
        vigil_runtime_free(chunk->runtime, &memory);
    }

    vigil_debug_local_table_free(&chunk->debug_locals);

    memset(chunk, 0, sizeof(*chunk));
}

size_t vigil_chunk_code_size(const vigil_chunk_t *chunk)
{
    if (chunk == NULL)
    {
        return 0U;
    }

    return chunk->code.length;
}

const uint8_t *vigil_chunk_code(const vigil_chunk_t *chunk)
{
    if (chunk == NULL)
    {
        return NULL;
    }

    return chunk->code.data;
}

size_t vigil_chunk_constant_count(const vigil_chunk_t *chunk)
{
    if (chunk == NULL)
    {
        return 0U;
    }

    return chunk->constant_count;
}

const vigil_value_t *vigil_chunk_constant(const vigil_chunk_t *chunk, size_t index)
{
    if (chunk == NULL || index >= chunk->constant_count)
    {
        return NULL;
    }

    return &chunk->constants[index];
}

vigil_source_span_t vigil_chunk_span_at(const vigil_chunk_t *chunk, size_t offset)
{
    vigil_source_span_t span;

    vigil_source_span_clear(&span);
    if (chunk == NULL || offset >= chunk->span_count)
    {
        return span;
    }

    return chunk->spans[offset];
}

const char *vigil_opcode_name(vigil_opcode_t opcode)
{
    if (opcode > VIGIL_OPCODE_MULTIPLY_F64_STORE)
    {
        return "UNKNOWN";
    }

    if (kVigilOpcodeNames[opcode] == NULL)
    {
        return "UNKNOWN";
    }

    return kVigilOpcodeNames[opcode];
}

vigil_status_t vigil_chunk_write_byte(vigil_chunk_t *chunk, uint8_t value, vigil_source_span_t span,
                                      vigil_error_t *error)
{
    vigil_status_t status;

    status = vigil_chunk_validate_mutable(chunk, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    return vigil_chunk_append_bytes(chunk, &value, 1U, span, error);
}

vigil_status_t vigil_chunk_write_opcode(vigil_chunk_t *chunk, vigil_opcode_t opcode, vigil_source_span_t span,
                                        vigil_error_t *error)
{
    return vigil_chunk_write_byte(chunk, (uint8_t)opcode, span, error);
}

vigil_status_t vigil_chunk_write_u32(vigil_chunk_t *chunk, uint32_t value, vigil_source_span_t span,
                                     vigil_error_t *error)
{
    uint8_t encoded[4];
    vigil_status_t status;

    status = vigil_chunk_validate_mutable(chunk, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    encoded[0] = (uint8_t)(value & 0xffU);
    encoded[1] = (uint8_t)((value >> 8U) & 0xffU);
    encoded[2] = (uint8_t)((value >> 16U) & 0xffU);
    encoded[3] = (uint8_t)((value >> 24U) & 0xffU);
    return vigil_chunk_append_bytes(chunk, encoded, sizeof(encoded), span, error);
}

vigil_status_t vigil_chunk_add_constant(vigil_chunk_t *chunk, const vigil_value_t *value, size_t *out_index,
                                        vigil_error_t *error)
{
    vigil_status_t status;

    status = vigil_chunk_validate_mutable(chunk, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (value == NULL || out_index == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "chunk constant requires value and out_index");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (chunk->constant_count == SIZE_MAX)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "chunk constant table overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    status = vigil_chunk_grow_constants(chunk, chunk->constant_count + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    chunk->constants[chunk->constant_count] = vigil_value_copy(value);
    *out_index = chunk->constant_count;
    chunk->constant_count += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_chunk_write_constant(vigil_chunk_t *chunk, const vigil_value_t *value, vigil_source_span_t span,
                                          size_t *out_index, vigil_error_t *error)
{
    vigil_status_t status;
    size_t index;
    uint8_t encoded[5];

    status = vigil_chunk_add_constant(chunk, value, &index, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (index > UINT32_MAX)
    {
        vigil_value_release(&chunk->constants[chunk->constant_count - 1U]);
        chunk->constant_count -= 1U;
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "chunk constant index overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    encoded[0] = (uint8_t)VIGIL_OPCODE_CONSTANT;
    encoded[1] = (uint8_t)((uint32_t)index & 0xffU);
    encoded[2] = (uint8_t)(((uint32_t)index >> 8U) & 0xffU);
    encoded[3] = (uint8_t)(((uint32_t)index >> 16U) & 0xffU);
    encoded[4] = (uint8_t)(((uint32_t)index >> 24U) & 0xffU);
    status = vigil_chunk_append_bytes(chunk, encoded, sizeof(encoded), span, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_value_release(&chunk->constants[chunk->constant_count - 1U]);
        chunk->constant_count -= 1U;
        return status;
    }

    if (out_index != NULL)
    {
        *out_index = index;
    }

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_chunk_disassemble(const vigil_chunk_t *chunk, vigil_string_t *output, vigil_error_t *error)
{
    size_t offset;
    vigil_status_t status;
    vigil_opcode_t opcode;

    status = vigil_chunk_validate_output(chunk, output, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vigil_string_clear(output);
    for (offset = 0U; offset < chunk->code.length;)
    {
        opcode = (vigil_opcode_t)chunk->code.data[offset];
        status = vigil_chunk_append_formatted(output, error, "failed to format chunk disassembly", "%04zu %s", offset,
                                              vigil_opcode_name(opcode));
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        status = vigil_chunk_disassemble_instruction(chunk, &offset, output, error);

        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        status = vigil_chunk_append_text(output, "\n", error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }

    return VIGIL_STATUS_OK;
}
