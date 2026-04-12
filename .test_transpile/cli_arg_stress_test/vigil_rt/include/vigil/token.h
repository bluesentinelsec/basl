#ifndef VIGIL_TOKEN_H
#define VIGIL_TOKEN_H

#include <stddef.h>

#include "vigil/export.h"
#include "vigil/runtime.h"
#include "vigil/source.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum vigil_token_kind
    {
        VIGIL_TOKEN_EOF = 0,
        VIGIL_TOKEN_IDENTIFIER = 1,
        VIGIL_TOKEN_INT_LITERAL = 2,
        VIGIL_TOKEN_FLOAT_LITERAL = 3,
        VIGIL_TOKEN_STRING_LITERAL = 4,
        VIGIL_TOKEN_RAW_STRING_LITERAL = 5,
        VIGIL_TOKEN_FSTRING_LITERAL = 6,
        VIGIL_TOKEN_CHAR_LITERAL = 7,
        VIGIL_TOKEN_IMPORT = 8,
        VIGIL_TOKEN_AS = 9,
        VIGIL_TOKEN_PUB = 10,
        VIGIL_TOKEN_FN = 11,
        VIGIL_TOKEN_CLASS = 12,
        VIGIL_TOKEN_INTERFACE = 13,
        VIGIL_TOKEN_ENUM = 14,
        VIGIL_TOKEN_CONST = 15,
        VIGIL_TOKEN_RETURN = 16,
        VIGIL_TOKEN_DEFER = 17,
        VIGIL_TOKEN_IF = 18,
        VIGIL_TOKEN_ELSE = 19,
        VIGIL_TOKEN_FOR = 20,
        VIGIL_TOKEN_WHILE = 21,
        VIGIL_TOKEN_SWITCH = 22,
        VIGIL_TOKEN_GUARD = 23,
        VIGIL_TOKEN_CASE = 24,
        VIGIL_TOKEN_DEFAULT = 25,
        VIGIL_TOKEN_BREAK = 26,
        VIGIL_TOKEN_CONTINUE = 27,
        VIGIL_TOKEN_IN = 28,
        VIGIL_TOKEN_NIL = 29,
        VIGIL_TOKEN_TRUE = 30,
        VIGIL_TOKEN_FALSE = 31,
        VIGIL_TOKEN_LPAREN = 32,
        VIGIL_TOKEN_RPAREN = 33,
        VIGIL_TOKEN_LBRACE = 34,
        VIGIL_TOKEN_RBRACE = 35,
        VIGIL_TOKEN_LBRACKET = 36,
        VIGIL_TOKEN_RBRACKET = 37,
        VIGIL_TOKEN_COMMA = 38,
        VIGIL_TOKEN_DOT = 39,
        VIGIL_TOKEN_SEMICOLON = 40,
        VIGIL_TOKEN_COLON = 41,
        VIGIL_TOKEN_QUESTION = 42,
        VIGIL_TOKEN_ARROW = 43,
        VIGIL_TOKEN_ASSIGN = 44,
        VIGIL_TOKEN_PLUS = 45,
        VIGIL_TOKEN_MINUS = 46,
        VIGIL_TOKEN_STAR = 47,
        VIGIL_TOKEN_SLASH = 48,
        VIGIL_TOKEN_PERCENT = 49,
        VIGIL_TOKEN_PLUS_PLUS = 50,
        VIGIL_TOKEN_MINUS_MINUS = 51,
        VIGIL_TOKEN_PLUS_ASSIGN = 52,
        VIGIL_TOKEN_MINUS_ASSIGN = 53,
        VIGIL_TOKEN_STAR_ASSIGN = 54,
        VIGIL_TOKEN_SLASH_ASSIGN = 55,
        VIGIL_TOKEN_PERCENT_ASSIGN = 56,
        VIGIL_TOKEN_EQUAL_EQUAL = 57,
        VIGIL_TOKEN_BANG = 58,
        VIGIL_TOKEN_BANG_EQUAL = 59,
        VIGIL_TOKEN_LESS = 60,
        VIGIL_TOKEN_LESS_EQUAL = 61,
        VIGIL_TOKEN_GREATER = 62,
        VIGIL_TOKEN_GREATER_EQUAL = 63,
        VIGIL_TOKEN_AMPERSAND = 64,
        VIGIL_TOKEN_AMPERSAND_AMPERSAND = 65,
        VIGIL_TOKEN_PIPE = 66,
        VIGIL_TOKEN_PIPE_PIPE = 67,
        VIGIL_TOKEN_CARET = 68,
        VIGIL_TOKEN_TILDE = 69,
        VIGIL_TOKEN_SHIFT_LEFT = 70,
        VIGIL_TOKEN_SHIFT_RIGHT = 71,
        VIGIL_TOKEN_EXTERN = 72,
        VIGIL_TOKEN_WALRUS = 73,
        VIGIL_TOKEN_STRUCT = 74
    } vigil_token_kind_t;

    typedef struct vigil_token
    {
        vigil_token_kind_t kind;
        vigil_source_span_t span;
    } vigil_token_t;

    typedef struct vigil_token_list
    {
        vigil_runtime_t *runtime;
        vigil_token_t *items;
        size_t count;
        size_t capacity;
    } vigil_token_list_t;

    VIGIL_API void vigil_token_list_init(vigil_token_list_t *list, vigil_runtime_t *runtime);
    VIGIL_API void vigil_token_list_clear(vigil_token_list_t *list);
    VIGIL_API void vigil_token_list_free(vigil_token_list_t *list);
    VIGIL_API size_t vigil_token_list_count(const vigil_token_list_t *list);
    /*
     * The returned pointer is invalidated by any later token-list mutation that
     * may reallocate the backing storage.
     */
    VIGIL_API const vigil_token_t *vigil_token_list_get(const vigil_token_list_t *list, size_t index);
    VIGIL_API const char *vigil_token_kind_name(vigil_token_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif
