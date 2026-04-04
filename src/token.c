#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/token.h"

static const char *const kVigilTokenNames[VIGIL_TOKEN_STRUCT + 1] = {
    [VIGIL_TOKEN_EOF] = "eof",
    [VIGIL_TOKEN_IDENTIFIER] = "identifier",
    [VIGIL_TOKEN_INT_LITERAL] = "int_literal",
    [VIGIL_TOKEN_FLOAT_LITERAL] = "float_literal",
    [VIGIL_TOKEN_STRING_LITERAL] = "string_literal",
    [VIGIL_TOKEN_RAW_STRING_LITERAL] = "raw_string_literal",
    [VIGIL_TOKEN_FSTRING_LITERAL] = "fstring_literal",
    [VIGIL_TOKEN_CHAR_LITERAL] = "char_literal",
    [VIGIL_TOKEN_IMPORT] = "import",
    [VIGIL_TOKEN_AS] = "as",
    [VIGIL_TOKEN_PUB] = "pub",
    [VIGIL_TOKEN_FN] = "fn",
    [VIGIL_TOKEN_CLASS] = "class",
    [VIGIL_TOKEN_INTERFACE] = "interface",
    [VIGIL_TOKEN_ENUM] = "enum",
    [VIGIL_TOKEN_CONST] = "const",
    [VIGIL_TOKEN_RETURN] = "return",
    [VIGIL_TOKEN_DEFER] = "defer",
    [VIGIL_TOKEN_IF] = "if",
    [VIGIL_TOKEN_ELSE] = "else",
    [VIGIL_TOKEN_FOR] = "for",
    [VIGIL_TOKEN_WHILE] = "while",
    [VIGIL_TOKEN_SWITCH] = "switch",
    [VIGIL_TOKEN_GUARD] = "guard",
    [VIGIL_TOKEN_CASE] = "case",
    [VIGIL_TOKEN_DEFAULT] = "default",
    [VIGIL_TOKEN_BREAK] = "break",
    [VIGIL_TOKEN_CONTINUE] = "continue",
    [VIGIL_TOKEN_IN] = "in",
    [VIGIL_TOKEN_NIL] = "nil",
    [VIGIL_TOKEN_TRUE] = "true",
    [VIGIL_TOKEN_FALSE] = "false",
    [VIGIL_TOKEN_LPAREN] = "left_paren",
    [VIGIL_TOKEN_RPAREN] = "right_paren",
    [VIGIL_TOKEN_LBRACE] = "left_brace",
    [VIGIL_TOKEN_RBRACE] = "right_brace",
    [VIGIL_TOKEN_LBRACKET] = "left_bracket",
    [VIGIL_TOKEN_RBRACKET] = "right_bracket",
    [VIGIL_TOKEN_COMMA] = "comma",
    [VIGIL_TOKEN_DOT] = "dot",
    [VIGIL_TOKEN_SEMICOLON] = "semicolon",
    [VIGIL_TOKEN_COLON] = "colon",
    [VIGIL_TOKEN_QUESTION] = "question",
    [VIGIL_TOKEN_ARROW] = "arrow",
    [VIGIL_TOKEN_ASSIGN] = "assign",
    [VIGIL_TOKEN_PLUS] = "plus",
    [VIGIL_TOKEN_MINUS] = "minus",
    [VIGIL_TOKEN_STAR] = "star",
    [VIGIL_TOKEN_SLASH] = "slash",
    [VIGIL_TOKEN_PERCENT] = "percent",
    [VIGIL_TOKEN_PLUS_PLUS] = "plus_plus",
    [VIGIL_TOKEN_MINUS_MINUS] = "minus_minus",
    [VIGIL_TOKEN_PLUS_ASSIGN] = "plus_assign",
    [VIGIL_TOKEN_MINUS_ASSIGN] = "minus_assign",
    [VIGIL_TOKEN_STAR_ASSIGN] = "star_assign",
    [VIGIL_TOKEN_SLASH_ASSIGN] = "slash_assign",
    [VIGIL_TOKEN_PERCENT_ASSIGN] = "percent_assign",
    [VIGIL_TOKEN_EQUAL_EQUAL] = "equal_equal",
    [VIGIL_TOKEN_BANG] = "bang",
    [VIGIL_TOKEN_BANG_EQUAL] = "bang_equal",
    [VIGIL_TOKEN_LESS] = "less",
    [VIGIL_TOKEN_LESS_EQUAL] = "less_equal",
    [VIGIL_TOKEN_GREATER] = "greater",
    [VIGIL_TOKEN_GREATER_EQUAL] = "greater_equal",
    [VIGIL_TOKEN_AMPERSAND] = "ampersand",
    [VIGIL_TOKEN_AMPERSAND_AMPERSAND] = "ampersand_ampersand",
    [VIGIL_TOKEN_PIPE] = "pipe",
    [VIGIL_TOKEN_PIPE_PIPE] = "pipe_pipe",
    [VIGIL_TOKEN_CARET] = "caret",
    [VIGIL_TOKEN_TILDE] = "tilde",
    [VIGIL_TOKEN_SHIFT_LEFT] = "shift_left",
    [VIGIL_TOKEN_SHIFT_RIGHT] = "shift_right",
    [VIGIL_TOKEN_EXTERN] = "extern",
    [VIGIL_TOKEN_WALRUS] = "walrus",
    [VIGIL_TOKEN_STRUCT] = "struct",
};

static int vigil_token_list_validate_mutable(const vigil_token_list_t *list, vigil_error_t *error)
{
    vigil_error_clear(error);

    if (list == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "token list must not be null");
        return 0;
    }

    if (list->runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "token list runtime must not be null");
        return 0;
    }

    return 1;
}

static vigil_status_t vigil_token_list_grow(vigil_token_list_t *list, size_t minimum_capacity, vigil_error_t *error)
{
    vigil_status_t status;
    size_t new_capacity;
    void *memory;

    if (list->capacity >= minimum_capacity)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    new_capacity = list->capacity == 0U ? 16U : list->capacity;
    while (new_capacity < minimum_capacity)
    {
        if (new_capacity > SIZE_MAX / 2U)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "token list capacity would overflow");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }

        new_capacity *= 2U;
    }

    if (new_capacity > SIZE_MAX / sizeof(*list->items))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "token list capacity would overflow");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    memory = list->items;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(list->runtime, new_capacity * sizeof(*list->items), &memory, error);
    }
    else
    {
        status = vigil_runtime_realloc(list->runtime, &memory, new_capacity * sizeof(*list->items), error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_token_t *)memory + list->capacity, 0, (new_capacity - list->capacity) * sizeof(*list->items));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    list->items = (vigil_token_t *)memory;
    list->capacity = new_capacity;
    return VIGIL_STATUS_OK;
}

void vigil_token_list_init(vigil_token_list_t *list, vigil_runtime_t *runtime)
{
    if (list == NULL)
    {
        return;
    }

    memset(list, 0, sizeof(*list));
    list->runtime = runtime;
}

void vigil_token_list_clear(vigil_token_list_t *list)
{
    if (list == NULL)
    {
        return;
    }

    list->count = 0U;
}

void vigil_token_list_free(vigil_token_list_t *list)
{
    void *memory;

    if (list == NULL)
    {
        return;
    }

    memory = list->items;
    if (list->runtime != NULL)
    {
        vigil_runtime_free(list->runtime, &memory);
    }
    memset(list, 0, sizeof(*list));
}

size_t vigil_token_list_count(const vigil_token_list_t *list)
{
    if (list == NULL)
    {
        return 0U;
    }

    return list->count;
}

const vigil_token_t *vigil_token_list_get(const vigil_token_list_t *list, size_t index)
{
    if (list == NULL || index >= list->count)
    {
        return NULL;
    }

    return &list->items[index];
}

vigil_status_t vigil_token_list_append(vigil_token_list_t *list, vigil_token_kind_t kind, vigil_source_span_t span,
                                       vigil_error_t *error)
{
    vigil_status_t status;

    if (!vigil_token_list_validate_mutable(list, error))
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (list->count == SIZE_MAX)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "token list is full");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_token_list_grow(list, list->count + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    list->items[list->count].kind = kind;
    list->items[list->count].span = span;
    list->count += 1U;
    vigil_error_clear(error);
    return VIGIL_STATUS_OK;
}

const char *vigil_token_kind_name(vigil_token_kind_t kind)
{
    if (kind > VIGIL_TOKEN_STRUCT)
    {
        return "unknown";
    }

    if (kVigilTokenNames[kind] == NULL)
    {
        return "unknown";
    }

    return kVigilTokenNames[kind];
}
