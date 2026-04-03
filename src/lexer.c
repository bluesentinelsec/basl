#include <ctype.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/lexer.h"

vigil_status_t vigil_token_list_append(vigil_token_list_t *list, vigil_token_kind_t kind, vigil_source_span_t span,
                                       vigil_error_t *error);

typedef struct vigil_lexer_state
{
    const char *text;
    size_t length;
    size_t offset;
    vigil_source_id_t source_id;
    vigil_token_list_t *tokens;
    vigil_diagnostic_list_t *diagnostics;
    vigil_error_t *error;
    int had_error;
    vigil_error_t last_error;
    vigil_token_kind_t last_token_kind;
} vigil_lexer_state_t;

static vigil_source_span_t vigil_lexer_span(const vigil_lexer_state_t *state, size_t start, size_t end)
{
    vigil_source_span_t span;

    span.source_id = state->source_id;
    span.start_offset = start;
    span.end_offset = end;
    return span;
}

static int vigil_lexer_is_at_end(const vigil_lexer_state_t *state)
{
    return state->offset >= state->length;
}

static char vigil_lexer_peek(const vigil_lexer_state_t *state)
{
    if (vigil_lexer_is_at_end(state))
    {
        return '\0';
    }

    return state->text[state->offset];
}

static char vigil_lexer_peek_next(const vigil_lexer_state_t *state)
{
    if (state->offset + 1U >= state->length)
    {
        return '\0';
    }

    return state->text[state->offset + 1U];
}

static char vigil_lexer_advance(vigil_lexer_state_t *state)
{
    char current;

    current = vigil_lexer_peek(state);
    if (!vigil_lexer_is_at_end(state))
    {
        state->offset += 1U;
    }

    return current;
}

static int vigil_lexer_match(vigil_lexer_state_t *state, char expected)
{
    if (vigil_lexer_peek(state) != expected)
    {
        return 0;
    }

    state->offset += 1U;
    return 1;
}

static vigil_status_t vigil_lexer_emit(vigil_lexer_state_t *state, vigil_token_kind_t kind, size_t start, size_t end)
{
    state->last_token_kind = kind;
    return vigil_token_list_append(state->tokens, kind, vigil_lexer_span(state, start, end), state->error);
}

static vigil_status_t vigil_lexer_report(vigil_lexer_state_t *state, size_t start, size_t end, const char *message)
{
    vigil_status_t status;

    state->had_error = 1;
    status = vigil_diagnostic_list_append_cstr(state->diagnostics, VIGIL_DIAGNOSTIC_ERROR,
                                               vigil_lexer_span(state, start, end), message, state->error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vigil_error_set_literal(state->error, VIGIL_STATUS_SYNTAX_ERROR, message);
    state->error->location.source_id = state->source_id;
    state->last_error = *state->error;
    return VIGIL_STATUS_SYNTAX_ERROR;
}

static int vigil_lexer_is_prefixed_digit(char ch, char prefix)
{
    switch (prefix)
    {
    case 'x':
    case 'X':
        return isxdigit((unsigned char)ch);
    case 'b':
    case 'B':
        return ch == '0' || ch == '1';
    case 'o':
    case 'O':
        return ch >= '0' && ch <= '7';
    default:
        return 0;
    }
}

static int vigil_lexer_is_identifier_start(char ch)
{
    return ch == '_' || isalpha((unsigned char)ch);
}

static int vigil_lexer_is_identifier_part(char ch)
{
    return ch == '_' || isalnum((unsigned char)ch);
}

static vigil_token_kind_t vigil_lexer_keyword_kind(const char *text, size_t length)
{
    static const struct
    {
        const char *word;
        size_t len;
        vigil_token_kind_t kind;
    } kKeywords[] = {
        {"as", 2U, VIGIL_TOKEN_AS},
        {"fn", 2U, VIGIL_TOKEN_FN},
        {"if", 2U, VIGIL_TOKEN_IF},
        {"in", 2U, VIGIL_TOKEN_IN},
        {"for", 3U, VIGIL_TOKEN_FOR},
        {"nil", 3U, VIGIL_TOKEN_NIL},
        {"pub", 3U, VIGIL_TOKEN_PUB},
        {"case", 4U, VIGIL_TOKEN_CASE},
        {"else", 4U, VIGIL_TOKEN_ELSE},
        {"enum", 4U, VIGIL_TOKEN_ENUM},
        {"true", 4U, VIGIL_TOKEN_TRUE},
        {"break", 5U, VIGIL_TOKEN_BREAK},
        {"class", 5U, VIGIL_TOKEN_CLASS},
        {"const", 5U, VIGIL_TOKEN_CONST},
        {"defer", 5U, VIGIL_TOKEN_DEFER},
        {"false", 5U, VIGIL_TOKEN_FALSE},
        {"guard", 5U, VIGIL_TOKEN_GUARD},
        {"while", 5U, VIGIL_TOKEN_WHILE},
        {"extern", 6U, VIGIL_TOKEN_EXTERN},
        {"import", 6U, VIGIL_TOKEN_IMPORT},
        {"return", 6U, VIGIL_TOKEN_RETURN},
        {"switch", 6U, VIGIL_TOKEN_SWITCH},
        {"default", 7U, VIGIL_TOKEN_DEFAULT},
        {"continue", 8U, VIGIL_TOKEN_CONTINUE},
        {"interface", 9U, VIGIL_TOKEN_INTERFACE},
    };
    size_t i;

    for (i = 0U; i < sizeof(kKeywords) / sizeof(kKeywords[0]); i++)
    {
        if (length == kKeywords[i].len && memcmp(text, kKeywords[i].word, length) == 0)
            return kKeywords[i].kind;
    }
    return VIGIL_TOKEN_IDENTIFIER;
}

static vigil_status_t vigil_lexer_scan_identifier(vigil_lexer_state_t *state, size_t start)
{
    while (vigil_lexer_is_identifier_part(vigil_lexer_peek(state)))
    {
        vigil_lexer_advance(state);
    }

    return vigil_lexer_emit(state, vigil_lexer_keyword_kind(state->text + start, state->offset - start), start,
                            state->offset);
}

static vigil_status_t vigil_lexer_scan_prefixed_number(vigil_lexer_state_t *state, size_t start, char prefix)
{
    size_t digits_start;

    vigil_lexer_advance(state);
    digits_start = state->offset;
    while (vigil_lexer_is_prefixed_digit(vigil_lexer_peek(state), prefix))
    {
        vigil_lexer_advance(state);
    }

    if (state->offset == digits_start)
    {
        return vigil_lexer_report(state, start, state->offset, "expected digits after numeric base prefix");
    }

    if (isalnum((unsigned char)vigil_lexer_peek(state)))
    {
        while (isalnum((unsigned char)vigil_lexer_peek(state)))
        {
            vigil_lexer_advance(state);
        }
        return vigil_lexer_report(state, start, state->offset, "invalid digits for numeric base prefix");
    }

    return vigil_lexer_emit(state, VIGIL_TOKEN_INT_LITERAL, start, state->offset);
}

static vigil_status_t vigil_lexer_scan_number(vigil_lexer_state_t *state, size_t start)
{
    vigil_token_kind_t kind;

    kind = VIGIL_TOKEN_INT_LITERAL;
    if (state->text[start] == '0')
    {
        char prefix = vigil_lexer_peek(state);

        if (prefix == 'x' || prefix == 'X' || prefix == 'b' || prefix == 'B' || prefix == 'o' || prefix == 'O')
        {
            return vigil_lexer_scan_prefixed_number(state, start, prefix);
        }
    }

    while (isdigit((unsigned char)vigil_lexer_peek(state)))
    {
        vigil_lexer_advance(state);
    }

    if (vigil_lexer_peek(state) == '.' && isdigit((unsigned char)vigil_lexer_peek_next(state)))
    {
        kind = VIGIL_TOKEN_FLOAT_LITERAL;
        vigil_lexer_advance(state);
        while (isdigit((unsigned char)vigil_lexer_peek(state)))
        {
            vigil_lexer_advance(state);
        }
    }

    if (vigil_lexer_peek(state) == 'e' || vigil_lexer_peek(state) == 'E')
    {
        kind = VIGIL_TOKEN_FLOAT_LITERAL;
        vigil_lexer_advance(state);
        if (vigil_lexer_peek(state) == '+' || vigil_lexer_peek(state) == '-')
        {
            vigil_lexer_advance(state);
        }
        while (isdigit((unsigned char)vigil_lexer_peek(state)))
        {
            vigil_lexer_advance(state);
        }
    }

    return vigil_lexer_emit(state, kind, start, state->offset);
}

static vigil_status_t vigil_lexer_scan_quoted(vigil_lexer_state_t *state, size_t start, char quote,
                                              vigil_token_kind_t kind)
{
    while (!vigil_lexer_is_at_end(state) && vigil_lexer_peek(state) != quote)
    {
        if (quote != '`' && vigil_lexer_peek(state) == '\\')
        {
            vigil_lexer_advance(state);
            if (!vigil_lexer_is_at_end(state))
            {
                vigil_lexer_advance(state);
            }
            continue;
        }

        vigil_lexer_advance(state);
    }

    if (vigil_lexer_is_at_end(state))
    {
        return vigil_lexer_report(
            state, start, state->offset,
            quote == '\'' ? "unterminated character literal"
                          : (quote == '`' ? "unterminated raw string literal" : "unterminated string literal"));
    }

    vigil_lexer_advance(state);
    return vigil_lexer_emit(state, kind, start, state->offset);
}

static vigil_status_t vigil_lexer_skip_block_comment(vigil_lexer_state_t *state, size_t start)
{
    while (!vigil_lexer_is_at_end(state))
    {
        if (vigil_lexer_peek(state) == '*' && vigil_lexer_peek_next(state) == '/')
        {
            vigil_lexer_advance(state);
            vigil_lexer_advance(state);
            return VIGIL_STATUS_OK;
        }

        vigil_lexer_advance(state);
    }

    return vigil_lexer_report(state, start, state->offset, "unterminated block comment");
}

/* Scan an f-string literal.  Unlike plain strings, f-strings must track
   brace depth so that `"` inside `{...}` interpolations does not
   terminate the token.  This allows e.g. f"{pad("hi", 10)}". */
static void vigil_lexer_skip_nested_literal(vigil_lexer_state_t *state, char quote)
{
    vigil_lexer_advance(state);
    while (!vigil_lexer_is_at_end(state) && vigil_lexer_peek(state) != quote)
    {
        if (vigil_lexer_peek(state) == '\\')
        {
            vigil_lexer_advance(state);
            if (!vigil_lexer_is_at_end(state))
                vigil_lexer_advance(state);
            continue;
        }
        vigil_lexer_advance(state);
    }
    if (!vigil_lexer_is_at_end(state))
        vigil_lexer_advance(state);
}

static vigil_status_t vigil_lexer_scan_fstring(vigil_lexer_state_t *state, size_t start)
{
    size_t brace_depth = 0U;

    while (!vigil_lexer_is_at_end(state))
    {
        char c = vigil_lexer_peek(state);
        if (c == '\\')
        {
            vigil_lexer_advance(state);
            if (!vigil_lexer_is_at_end(state))
            {
                vigil_lexer_advance(state);
            }
            continue;
        }
        if (c == '{')
        {
            brace_depth += 1U;
            vigil_lexer_advance(state);
            continue;
        }
        if (c == '}' && brace_depth > 0U)
        {
            brace_depth -= 1U;
            vigil_lexer_advance(state);
            continue;
        }
        if (c == '"' && brace_depth > 0U)
        {
            vigil_lexer_skip_nested_literal(state, '"');
            continue;
        }
        if (c == '\'' && brace_depth > 0U)
        {
            vigil_lexer_skip_nested_literal(state, '\'');
            continue;
        }
        if (c == '"' && brace_depth == 0U)
        {
            vigil_lexer_advance(state);
            return vigil_lexer_emit(state, VIGIL_TOKEN_FSTRING_LITERAL, start, state->offset);
        }
        vigil_lexer_advance(state);
    }

    return vigil_lexer_report(state, start, state->offset, "unterminated f-string literal");
}

static vigil_status_t vigil_lexer_scan_two_char(vigil_lexer_state_t *state, size_t start, char second,
                                                vigil_token_kind_t two_kind, vigil_token_kind_t one_kind)
{
    if (vigil_lexer_match(state, second))
    {
        return vigil_lexer_emit(state, two_kind, start, state->offset);
    }
    return vigil_lexer_emit(state, one_kind, start, state->offset);
}

static vigil_status_t vigil_lexer_scan_slash(vigil_lexer_state_t *state, size_t start)
{
    if (vigil_lexer_match(state, '/'))
    {
        while (!vigil_lexer_is_at_end(state) && vigil_lexer_peek(state) != '\n')
        {
            vigil_lexer_advance(state);
        }
        return VIGIL_STATUS_OK;
    }
    if (vigil_lexer_match(state, '*'))
    {
        return vigil_lexer_skip_block_comment(state, start);
    }
    return vigil_lexer_scan_two_char(state, start, '=', VIGIL_TOKEN_SLASH_ASSIGN, VIGIL_TOKEN_SLASH);
}

static vigil_status_t vigil_lexer_scan_operator(vigil_lexer_state_t *state, size_t start, char ch)
{
    switch (ch)
    {
    case '+':
        if (vigil_lexer_match(state, '+'))
            return vigil_lexer_emit(state, VIGIL_TOKEN_PLUS_PLUS, start, state->offset);
        return vigil_lexer_scan_two_char(state, start, '=', VIGIL_TOKEN_PLUS_ASSIGN, VIGIL_TOKEN_PLUS);
    case '-':
        if (vigil_lexer_match(state, '>'))
            return vigil_lexer_emit(state, VIGIL_TOKEN_ARROW, start, state->offset);
        if (vigil_lexer_match(state, '-'))
            return vigil_lexer_emit(state, VIGIL_TOKEN_MINUS_MINUS, start, state->offset);
        return vigil_lexer_scan_two_char(state, start, '=', VIGIL_TOKEN_MINUS_ASSIGN, VIGIL_TOKEN_MINUS);
    case '<':
        if (vigil_lexer_match(state, '<'))
            return vigil_lexer_emit(state, VIGIL_TOKEN_SHIFT_LEFT, start, state->offset);
        return vigil_lexer_scan_two_char(state, start, '=', VIGIL_TOKEN_LESS_EQUAL, VIGIL_TOKEN_LESS);
    case '>':
        if (vigil_lexer_match(state, '>'))
            return vigil_lexer_emit(state, VIGIL_TOKEN_SHIFT_RIGHT, start, state->offset);
        return vigil_lexer_scan_two_char(state, start, '=', VIGIL_TOKEN_GREATER_EQUAL, VIGIL_TOKEN_GREATER);
    default:
        return VIGIL_STATUS_INTERNAL;
    }
}

static int vigil_lexer_asi_trigger(vigil_token_kind_t kind)
{
    switch (kind)
    {
    case VIGIL_TOKEN_IDENTIFIER:
    case VIGIL_TOKEN_INT_LITERAL:
    case VIGIL_TOKEN_FLOAT_LITERAL:
    case VIGIL_TOKEN_STRING_LITERAL:
    case VIGIL_TOKEN_RAW_STRING_LITERAL:
    case VIGIL_TOKEN_FSTRING_LITERAL:
    case VIGIL_TOKEN_CHAR_LITERAL:
    case VIGIL_TOKEN_NIL:
    case VIGIL_TOKEN_TRUE:
    case VIGIL_TOKEN_FALSE:
    case VIGIL_TOKEN_BREAK:
    case VIGIL_TOKEN_CONTINUE:
    case VIGIL_TOKEN_RETURN:
    case VIGIL_TOKEN_PLUS_PLUS:
    case VIGIL_TOKEN_MINUS_MINUS:
    case VIGIL_TOKEN_RPAREN:
    case VIGIL_TOKEN_RBRACE:
    case VIGIL_TOKEN_RBRACKET:
        return 1;
    default:
        return 0;
    }
}

static vigil_status_t vigil_lexer_scan_token(vigil_lexer_state_t *state)
{
    vigil_status_t status;
    size_t start;
    char ch;

    start = state->offset;
    ch = vigil_lexer_advance(state);
    switch (ch)
    {
    case ' ':
    case '\r':
    case '\t':
        return VIGIL_STATUS_OK;
    case '\n':
        if (vigil_lexer_asi_trigger(state->last_token_kind))
        {
            /* Peek past whitespace and comments on subsequent lines.
               Suppress ASI when the first non-blank, non-comment
               character would start a continuation:
               - '{' : opening brace for function/block body
               - binary operators that continue an expression
               - '.' : method chain continuation */
            size_t peek = state->offset;
            for (;;)
            {
                while (peek < state->length &&
                       (state->text[peek] == ' ' || state->text[peek] == '\t' ||
                        state->text[peek] == '\r' || state->text[peek] == '\n'))
                    peek++;
                if (peek + 1U < state->length && state->text[peek] == '/' && state->text[peek + 1U] == '/')
                {
                    while (peek < state->length && state->text[peek] != '\n')
                        peek++;
                    continue;
                }
                if (peek + 1U < state->length && state->text[peek] == '/' && state->text[peek + 1U] == '*')
                {
                    peek += 2U;
                    while (peek + 1U < state->length &&
                           !(state->text[peek] == '*' && state->text[peek + 1U] == '/'))
                        peek++;
                    if (peek + 1U < state->length)
                        peek += 2U;
                    continue;
                }
                break;
            }
            if (peek < state->length)
            {
                char nc = state->text[peek];
                if (nc == '{')
                    return VIGIL_STATUS_OK;
                char nc2 = (peek + 1U < state->length) ? state->text[peek + 1U] : '\0';
                if ((nc == '&' && nc2 == '&') || (nc == '|' && nc2 == '|'))
                    return VIGIL_STATUS_OK;
                if (nc == '.')
                    return VIGIL_STATUS_OK;
                /* Closing delimiters continue an expression */
                if (nc == ')' || nc == ']')
                    return VIGIL_STATUS_OK;
            }
            return vigil_lexer_emit(state, VIGIL_TOKEN_SEMICOLON, start, start);
        }
        return VIGIL_STATUS_OK;
    case '(':
        return vigil_lexer_emit(state, VIGIL_TOKEN_LPAREN, start, state->offset);
    case ')':
        return vigil_lexer_emit(state, VIGIL_TOKEN_RPAREN, start, state->offset);
    case '{':
        return vigil_lexer_emit(state, VIGIL_TOKEN_LBRACE, start, state->offset);
    case '}':
        return vigil_lexer_emit(state, VIGIL_TOKEN_RBRACE, start, state->offset);
    case '[':
        return vigil_lexer_emit(state, VIGIL_TOKEN_LBRACKET, start, state->offset);
    case ']':
        return vigil_lexer_emit(state, VIGIL_TOKEN_RBRACKET, start, state->offset);
    case ',':
        return vigil_lexer_emit(state, VIGIL_TOKEN_COMMA, start, state->offset);
    case '.':
        return vigil_lexer_emit(state, VIGIL_TOKEN_DOT, start, state->offset);
    case ';':
        return vigil_lexer_emit(state, VIGIL_TOKEN_SEMICOLON, start, state->offset);
    case ':':
        return vigil_lexer_emit(state, VIGIL_TOKEN_COLON, start, state->offset);
    case '?':
        return vigil_lexer_emit(state, VIGIL_TOKEN_QUESTION, start, state->offset);
    case '~':
        return vigil_lexer_emit(state, VIGIL_TOKEN_TILDE, start, state->offset);
    case '+':
    case '-':
    case '<':
    case '>':
        return vigil_lexer_scan_operator(state, start, ch);
    case '*':
        return vigil_lexer_scan_two_char(state, start, '=', VIGIL_TOKEN_STAR_ASSIGN, VIGIL_TOKEN_STAR);
    case '/':
        return vigil_lexer_scan_slash(state, start);
    case '%':
        return vigil_lexer_scan_two_char(state, start, '=', VIGIL_TOKEN_PERCENT_ASSIGN, VIGIL_TOKEN_PERCENT);
    case '=':
        return vigil_lexer_scan_two_char(state, start, '=', VIGIL_TOKEN_EQUAL_EQUAL, VIGIL_TOKEN_ASSIGN);
    case '!':
        return vigil_lexer_scan_two_char(state, start, '=', VIGIL_TOKEN_BANG_EQUAL, VIGIL_TOKEN_BANG);
    case '&':
        return vigil_lexer_scan_two_char(state, start, '&', VIGIL_TOKEN_AMPERSAND_AMPERSAND, VIGIL_TOKEN_AMPERSAND);
    case '|':
        return vigil_lexer_scan_two_char(state, start, '|', VIGIL_TOKEN_PIPE_PIPE, VIGIL_TOKEN_PIPE);
    case '^':
        return vigil_lexer_emit(state, VIGIL_TOKEN_CARET, start, state->offset);
    case '"':
        return vigil_lexer_scan_quoted(state, start, '"', VIGIL_TOKEN_STRING_LITERAL);
    case '\'':
        return vigil_lexer_scan_quoted(state, start, '\'', VIGIL_TOKEN_CHAR_LITERAL);
    case '`':
        return vigil_lexer_scan_quoted(state, start, '`', VIGIL_TOKEN_RAW_STRING_LITERAL);
    case 'f':
        if (vigil_lexer_peek(state) == '"')
        {
            vigil_lexer_advance(state);
            return vigil_lexer_scan_fstring(state, start);
        }
        return vigil_lexer_scan_identifier(state, start);
    default:
        if (isdigit((unsigned char)ch))
        {
            return vigil_lexer_scan_number(state, start);
        }
        if (vigil_lexer_is_identifier_start(ch))
        {
            return vigil_lexer_scan_identifier(state, start);
        }
        status = vigil_lexer_report(state, start, state->offset, "unexpected character");
        if (status != VIGIL_STATUS_SYNTAX_ERROR)
        {
            return status;
        }
        return VIGIL_STATUS_OK;
    }
}

vigil_status_t vigil_lex_source(const vigil_source_registry_t *registry, vigil_source_id_t source_id,
                                vigil_token_list_t *tokens, vigil_diagnostic_list_t *diagnostics, vigil_error_t *error)
{
    vigil_lexer_state_t state;
    const vigil_source_file_t *source;
    vigil_status_t status;

    vigil_error_clear(error);
    if (registry == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "source registry must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (tokens == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "token list must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (diagnostics == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "diagnostic list must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    source = vigil_source_registry_get(registry, source_id);
    if (source == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "source_id must reference a registered source file");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_token_list_clear(tokens);
    memset(&state, 0, sizeof(state));
    state.text = vigil_string_c_str(&source->text);
    state.length = vigil_string_length(&source->text);
    state.source_id = source_id;
    state.tokens = tokens;
    state.diagnostics = diagnostics;
    state.error = error;
    state.last_token_kind = VIGIL_TOKEN_EOF;

    while (!vigil_lexer_is_at_end(&state))
    {
        status = vigil_lexer_scan_token(&state);
        if (status != VIGIL_STATUS_OK && status != VIGIL_STATUS_SYNTAX_ERROR)
        {
            return status;
        }
    }

    if (state.had_error)
    {
        status = vigil_lexer_emit(&state, VIGIL_TOKEN_EOF, state.offset, state.offset);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        *error = state.last_error;
        return VIGIL_STATUS_SYNTAX_ERROR;
    }

    if (vigil_lexer_asi_trigger(state.last_token_kind))
    {
        status = vigil_lexer_emit(&state, VIGIL_TOKEN_SEMICOLON, state.offset, state.offset);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }

    status = vigil_lexer_emit(&state, VIGIL_TOKEN_EOF, state.offset, state.offset);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vigil_error_clear(error);
    return VIGIL_STATUS_OK;
}
