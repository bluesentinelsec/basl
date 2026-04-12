#ifndef VIGIL_TYPE_H
#define VIGIL_TYPE_H

#include <stddef.h>

#include "vigil/export.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum vigil_type_kind
    {
        VIGIL_TYPE_INVALID = 0,
        VIGIL_TYPE_I32 = 1,
        VIGIL_TYPE_I64 = 2,
        VIGIL_TYPE_U8 = 3,
        VIGIL_TYPE_U32 = 4,
        VIGIL_TYPE_U64 = 5,
        VIGIL_TYPE_F64 = 6,
        VIGIL_TYPE_BOOL = 7,
        VIGIL_TYPE_STRING = 8,
        VIGIL_TYPE_ERR = 9,
        VIGIL_TYPE_VOID = 10,
        VIGIL_TYPE_NIL = 11,
        VIGIL_TYPE_OBJECT = 12
    } vigil_type_kind_t;

    typedef enum vigil_unary_operator_kind
    {
        VIGIL_UNARY_OPERATOR_NEGATE = 0,
        VIGIL_UNARY_OPERATOR_LOGICAL_NOT = 1,
        VIGIL_UNARY_OPERATOR_BITWISE_NOT = 2
    } vigil_unary_operator_kind_t;

    typedef enum vigil_binary_operator_kind
    {
        VIGIL_BINARY_OPERATOR_ADD = 0,
        VIGIL_BINARY_OPERATOR_SUBTRACT = 1,
        VIGIL_BINARY_OPERATOR_MULTIPLY = 2,
        VIGIL_BINARY_OPERATOR_DIVIDE = 3,
        VIGIL_BINARY_OPERATOR_MODULO = 4,
        VIGIL_BINARY_OPERATOR_BITWISE_AND = 5,
        VIGIL_BINARY_OPERATOR_BITWISE_OR = 6,
        VIGIL_BINARY_OPERATOR_BITWISE_XOR = 7,
        VIGIL_BINARY_OPERATOR_SHIFT_LEFT = 8,
        VIGIL_BINARY_OPERATOR_SHIFT_RIGHT = 9,
        VIGIL_BINARY_OPERATOR_GREATER = 10,
        VIGIL_BINARY_OPERATOR_GREATER_EQUAL = 11,
        VIGIL_BINARY_OPERATOR_LESS = 12,
        VIGIL_BINARY_OPERATOR_LESS_EQUAL = 13,
        VIGIL_BINARY_OPERATOR_EQUAL = 14,
        VIGIL_BINARY_OPERATOR_NOT_EQUAL = 15,
        VIGIL_BINARY_OPERATOR_LOGICAL_AND = 16,
        VIGIL_BINARY_OPERATOR_LOGICAL_OR = 17
    } vigil_binary_operator_kind_t;

    typedef struct vigil_function_signature
    {
        vigil_type_kind_t return_type;
        const vigil_type_kind_t *parameter_types;
        size_t parameter_count;
    } vigil_function_signature_t;

    VIGIL_API const char *vigil_type_kind_name(vigil_type_kind_t kind);
    VIGIL_API int vigil_type_kind_is_valid(vigil_type_kind_t kind);
    VIGIL_API vigil_type_kind_t vigil_type_kind_from_name(const char *text, size_t length);
    VIGIL_API int vigil_type_is_assignable(vigil_type_kind_t target_type, vigil_type_kind_t source_type);
    VIGIL_API int vigil_type_supports_unary_operator(vigil_unary_operator_kind_t operator_kind,
                                                     vigil_type_kind_t operand_type);
    VIGIL_API int vigil_type_supports_binary_operator(vigil_binary_operator_kind_t operator_kind,
                                                      vigil_type_kind_t left_type, vigil_type_kind_t right_type);
    VIGIL_API int vigil_function_signature_is_valid(const vigil_function_signature_t *signature);
    VIGIL_API int vigil_function_signature_accepts_arguments(const vigil_function_signature_t *signature,
                                                             const vigil_type_kind_t *argument_types,
                                                             size_t argument_count);

#ifdef __cplusplus
}
#endif

#endif
