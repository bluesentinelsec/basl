#include <string.h>

#include "vigil/type.h"

typedef struct
{
    const char *name;
    size_t length;
    vigil_type_kind_t kind;
} vigil_type_name_entry_t;

typedef enum
{
    VIGIL_TYPE_BINARY_RULE_INVALID = 0,
    VIGIL_TYPE_BINARY_RULE_ADD,
    VIGIL_TYPE_BINARY_RULE_ARITHMETIC,
    VIGIL_TYPE_BINARY_RULE_INTEGER_ONLY,
    VIGIL_TYPE_BINARY_RULE_ORDERED,
    VIGIL_TYPE_BINARY_RULE_EQUALITY,
    VIGIL_TYPE_BINARY_RULE_LOGICAL
} vigil_type_binary_rule_t;

typedef struct
{
    vigil_binary_operator_kind_t operator_kind;
    vigil_type_binary_rule_t rule;
} vigil_type_binary_rule_entry_t;

static int vigil_type_kind_is_integer(vigil_type_kind_t kind)
{
    return kind == VIGIL_TYPE_I32 || kind == VIGIL_TYPE_I64 || kind == VIGIL_TYPE_U8 || kind == VIGIL_TYPE_U32 ||
           kind == VIGIL_TYPE_U64;
}

static int vigil_type_kind_is_signed_integer(vigil_type_kind_t kind)
{
    return kind == VIGIL_TYPE_I32 || kind == VIGIL_TYPE_I64;
}

static int vigil_type_kinds_match(vigil_type_kind_t left_type, vigil_type_kind_t right_type)
{
    return vigil_type_kind_is_valid(left_type) && vigil_type_kind_is_valid(right_type) && left_type == right_type;
}

static int vigil_type_kinds_are_matching_integers(vigil_type_kind_t left_type, vigil_type_kind_t right_type)
{
    return vigil_type_kind_is_integer(left_type) && vigil_type_kinds_match(left_type, right_type);
}

static int vigil_type_kinds_are(vigil_type_kind_t left_type, vigil_type_kind_t right_type, vigil_type_kind_t kind)
{
    return left_type == kind && right_type == kind;
}

static vigil_type_binary_rule_t vigil_type_binary_rule(vigil_binary_operator_kind_t operator_kind)
{
    static const vigil_type_binary_rule_entry_t entries[] = {
        {VIGIL_BINARY_OPERATOR_ADD, VIGIL_TYPE_BINARY_RULE_ADD},
        {VIGIL_BINARY_OPERATOR_SUBTRACT, VIGIL_TYPE_BINARY_RULE_ARITHMETIC},
        {VIGIL_BINARY_OPERATOR_MULTIPLY, VIGIL_TYPE_BINARY_RULE_ARITHMETIC},
        {VIGIL_BINARY_OPERATOR_DIVIDE, VIGIL_TYPE_BINARY_RULE_ARITHMETIC},
        {VIGIL_BINARY_OPERATOR_MODULO, VIGIL_TYPE_BINARY_RULE_INTEGER_ONLY},
        {VIGIL_BINARY_OPERATOR_BITWISE_AND, VIGIL_TYPE_BINARY_RULE_INTEGER_ONLY},
        {VIGIL_BINARY_OPERATOR_BITWISE_OR, VIGIL_TYPE_BINARY_RULE_INTEGER_ONLY},
        {VIGIL_BINARY_OPERATOR_BITWISE_XOR, VIGIL_TYPE_BINARY_RULE_INTEGER_ONLY},
        {VIGIL_BINARY_OPERATOR_SHIFT_LEFT, VIGIL_TYPE_BINARY_RULE_INTEGER_ONLY},
        {VIGIL_BINARY_OPERATOR_SHIFT_RIGHT, VIGIL_TYPE_BINARY_RULE_INTEGER_ONLY},
        {VIGIL_BINARY_OPERATOR_GREATER, VIGIL_TYPE_BINARY_RULE_ORDERED},
        {VIGIL_BINARY_OPERATOR_GREATER_EQUAL, VIGIL_TYPE_BINARY_RULE_ORDERED},
        {VIGIL_BINARY_OPERATOR_LESS, VIGIL_TYPE_BINARY_RULE_ORDERED},
        {VIGIL_BINARY_OPERATOR_LESS_EQUAL, VIGIL_TYPE_BINARY_RULE_ORDERED},
        {VIGIL_BINARY_OPERATOR_EQUAL, VIGIL_TYPE_BINARY_RULE_EQUALITY},
        {VIGIL_BINARY_OPERATOR_NOT_EQUAL, VIGIL_TYPE_BINARY_RULE_EQUALITY},
        {VIGIL_BINARY_OPERATOR_LOGICAL_AND, VIGIL_TYPE_BINARY_RULE_LOGICAL},
        {VIGIL_BINARY_OPERATOR_LOGICAL_OR, VIGIL_TYPE_BINARY_RULE_LOGICAL},
    };
    size_t index;

    for (index = 0U; index < sizeof(entries) / sizeof(entries[0]); index += 1U)
    {
        if (entries[index].operator_kind == operator_kind)
            return entries[index].rule;
    }

    return VIGIL_TYPE_BINARY_RULE_INVALID;
}

const char *vigil_type_kind_name(vigil_type_kind_t kind)
{
    switch (kind)
    {
    case VIGIL_TYPE_I32:
        return "i32";
    case VIGIL_TYPE_I64:
        return "i64";
    case VIGIL_TYPE_U8:
        return "u8";
    case VIGIL_TYPE_U32:
        return "u32";
    case VIGIL_TYPE_U64:
        return "u64";
    case VIGIL_TYPE_F64:
        return "f64";
    case VIGIL_TYPE_BOOL:
        return "bool";
    case VIGIL_TYPE_STRING:
        return "string";
    case VIGIL_TYPE_ERR:
        return "err";
    case VIGIL_TYPE_VOID:
        return "void";
    case VIGIL_TYPE_NIL:
        return "nil";
    case VIGIL_TYPE_OBJECT:
        return "object";
    default:
        return "invalid";
    }
}

int vigil_type_kind_is_valid(vigil_type_kind_t kind)
{
    return kind >= VIGIL_TYPE_I32 && kind <= VIGIL_TYPE_OBJECT;
}

vigil_type_kind_t vigil_type_kind_from_name(const char *text, size_t length)
{
    static const vigil_type_name_entry_t entries[] = {
        {"i32", 3U, VIGIL_TYPE_I32},   {"i64", 3U, VIGIL_TYPE_I64},       {"u8", 2U, VIGIL_TYPE_U8},
        {"u32", 3U, VIGIL_TYPE_U32},   {"u64", 3U, VIGIL_TYPE_U64},       {"f64", 3U, VIGIL_TYPE_F64},
        {"bool", 4U, VIGIL_TYPE_BOOL}, {"string", 6U, VIGIL_TYPE_STRING}, {"err", 3U, VIGIL_TYPE_ERR},
        {"void", 4U, VIGIL_TYPE_VOID}, {"nil", 3U, VIGIL_TYPE_NIL},
    };
    size_t index;

    if (text == NULL)
    {
        return VIGIL_TYPE_INVALID;
    }

    for (index = 0U; index < sizeof(entries) / sizeof(entries[0]); index += 1U)
    {
        if (length == entries[index].length && memcmp(text, entries[index].name, length) == 0)
            return entries[index].kind;
    }

    return VIGIL_TYPE_INVALID;
}

int vigil_type_is_assignable(vigil_type_kind_t target_type, vigil_type_kind_t source_type)
{
    return vigil_type_kinds_match(target_type, source_type);
}

int vigil_type_supports_unary_operator(vigil_unary_operator_kind_t operator_kind, vigil_type_kind_t operand_type)
{
    if (!vigil_type_kind_is_valid(operand_type))
    {
        return 0;
    }

    switch (operator_kind)
    {
    case VIGIL_UNARY_OPERATOR_NEGATE:
        return vigil_type_kind_is_signed_integer(operand_type) || operand_type == VIGIL_TYPE_F64;
    case VIGIL_UNARY_OPERATOR_LOGICAL_NOT:
        return operand_type == VIGIL_TYPE_BOOL;
    case VIGIL_UNARY_OPERATOR_BITWISE_NOT:
        return vigil_type_kind_is_signed_integer(operand_type);
    default:
        return 0;
    }
}

int vigil_type_supports_binary_operator(vigil_binary_operator_kind_t operator_kind, vigil_type_kind_t left_type,
                                        vigil_type_kind_t right_type)
{
    vigil_type_binary_rule_t rule;

    if (!vigil_type_kind_is_valid(left_type) || !vigil_type_kind_is_valid(right_type))
    {
        return 0;
    }

    rule = vigil_type_binary_rule(operator_kind);
    switch (rule)
    {
    case VIGIL_TYPE_BINARY_RULE_ADD:
        return vigil_type_kinds_are_matching_integers(left_type, right_type) ||
               vigil_type_kinds_are(left_type, right_type, VIGIL_TYPE_F64) ||
               vigil_type_kinds_are(left_type, right_type, VIGIL_TYPE_STRING);
    case VIGIL_TYPE_BINARY_RULE_ARITHMETIC:
        return vigil_type_kinds_are_matching_integers(left_type, right_type) ||
               vigil_type_kinds_are(left_type, right_type, VIGIL_TYPE_F64);
    case VIGIL_TYPE_BINARY_RULE_INTEGER_ONLY:
        return vigil_type_kinds_are_matching_integers(left_type, right_type);
    case VIGIL_TYPE_BINARY_RULE_ORDERED:
        return vigil_type_kinds_are_matching_integers(left_type, right_type) ||
               vigil_type_kinds_are(left_type, right_type, VIGIL_TYPE_F64) ||
               vigil_type_kinds_are(left_type, right_type, VIGIL_TYPE_STRING);
    case VIGIL_TYPE_BINARY_RULE_EQUALITY:
        return vigil_type_kinds_match(left_type, right_type);
    case VIGIL_TYPE_BINARY_RULE_LOGICAL:
        return vigil_type_kinds_are(left_type, right_type, VIGIL_TYPE_BOOL);
    case VIGIL_TYPE_BINARY_RULE_INVALID:
    default:
        return 0;
    }
}

int vigil_function_signature_is_valid(const vigil_function_signature_t *signature)
{
    size_t index;

    if (signature == NULL || !vigil_type_kind_is_valid(signature->return_type))
    {
        return 0;
    }

    if (signature->parameter_count != 0U && signature->parameter_types == NULL)
    {
        return 0;
    }

    for (index = 0U; index < signature->parameter_count; index += 1U)
    {
        if (!vigil_type_kind_is_valid(signature->parameter_types[index]))
        {
            return 0;
        }
    }

    return 1;
}

int vigil_function_signature_accepts_arguments(const vigil_function_signature_t *signature,
                                               const vigil_type_kind_t *argument_types, size_t argument_count)
{
    size_t index;

    if (!vigil_function_signature_is_valid(signature))
    {
        return 0;
    }

    if (signature->parameter_count != argument_count)
    {
        return 0;
    }

    if (argument_count != 0U && argument_types == NULL)
    {
        return 0;
    }

    for (index = 0U; index < argument_count; index += 1U)
    {
        if (!vigil_type_is_assignable(signature->parameter_types[index], argument_types[index]))
        {
            return 0;
        }
    }

    return 1;
}
