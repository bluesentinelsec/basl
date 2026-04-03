#include "internal/vigil_compiler_types.h"
#include "internal/vigil_internal.h"
#include "vigil/lexer.h"
#include <stdlib.h>
#include <string.h>

/* ── Escape-decoding helpers ───────────────────────────────────────── */

static int hex_digit_value(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

static vigil_status_t decode_hex_escape(const vigil_program_state_t *program, vigil_source_span_t span,
                                        const char *text, size_t end, size_t *index, char *out)
{
    int hi;
    int lo;
    size_t i = *index;

    if (i + 2U >= end)
    {
        return vigil_compile_report(program, span, "\\x escape requires two hex digits");
    }
    hi = hex_digit_value(text[i + 1U]);
    lo = hex_digit_value(text[i + 2U]);
    if (hi < 0 || lo < 0)
    {
        return vigil_compile_report(program, span, "\\x escape requires two hex digits");
    }
    *out = (char)(((unsigned int)hi << 4U) | (unsigned int)lo);
    *index = i + 2U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t decode_escape_char(const vigil_program_state_t *program, vigil_source_span_t span,
                                         const char *text, size_t end, size_t *index, char *out)
{
    size_t i = *index;

    switch (text[i])
    {
    case 'n':
        *out = '\n';
        return VIGIL_STATUS_OK;
    case 'r':
        *out = '\r';
        return VIGIL_STATUS_OK;
    case 't':
        *out = '\t';
        return VIGIL_STATUS_OK;
    case '\\':
        *out = '\\';
        return VIGIL_STATUS_OK;
    case '"':
        *out = '"';
        return VIGIL_STATUS_OK;
    case '\'':
        *out = '\'';
        return VIGIL_STATUS_OK;
    case '0':
        *out = '\0';
        return VIGIL_STATUS_OK;
    case 'x':
        return decode_hex_escape(program, span, text, end, index, out);
    default:
        return vigil_compile_report(program, span, "invalid escape sequence");
    }
}

vigil_status_t vigil_program_append_decoded_string_range(const vigil_program_state_t *program, vigil_source_span_t span,
                                                         const char *text, size_t start, size_t end,
                                                         vigil_string_t *out_text)
{
    size_t index;
    char decoded;
    vigil_status_t status;

    if (program == NULL || text == NULL || out_text == NULL || start > end)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    for (index = start; index < end; index += 1U)
    {
        if (index + 1U < end && text[index] == '{' && text[index + 1U] == '{')
        {
            status = vigil_string_append(out_text, "{", 1U, program->error);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }
            index += 1U;
            continue;
        }
        if (index + 1U < end && text[index] == '}' && text[index + 1U] == '}')
        {
            status = vigil_string_append(out_text, "}", 1U, program->error);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }
            index += 1U;
            continue;
        }
        if (text[index] != '\\')
        {
            status = vigil_string_append(out_text, text + index, 1U, program->error);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }
            continue;
        }

        index += 1U;
        if (index >= end)
        {
            return vigil_compile_report(program, span, "invalid escape sequence");
        }

        status = decode_escape_char(program, span, text, end, &index, &decoded);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        status = vigil_string_append(out_text, &decoded, 1U, program->error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }

    return VIGIL_STATUS_OK;
}

size_t vigil_program_skip_quoted_text(const char *text, size_t length, size_t index, char delimiter)
{
    index += 1U;
    while (index < length)
    {
        if (text[index] == '\\' && delimiter != '`')
        {
            index += 2U;
            continue;
        }
        if (text[index] == delimiter)
        {
            return index + 1U;
        }
        index += 1U;
    }
    return length;
}

vigil_status_t vigil_parser_parse_embedded_expression(vigil_parser_state_t *state, const char *text, size_t length,
                                                      size_t absolute_offset, vigil_source_span_t error_span,
                                                      vigil_expression_result_t *out_result)
{
    vigil_status_t status;
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_source_id_t source_id;
    const vigil_source_file_t *source;
    vigil_token_list_t tokens;
    size_t token_index;
    vigil_program_state_t *program;
    const vigil_token_list_t *previous_tokens;
    vigil_parser_state_t nested;
    vigil_expression_result_t nested_result;

    if (state == NULL || text == NULL || out_result == NULL)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_source_registry_init(&registry, state->program->registry->runtime);
    vigil_diagnostic_list_init(&diagnostics, state->program->registry->runtime);
    vigil_token_list_init(&tokens, state->program->registry->runtime);
    source_id = 0U;
    status =
        vigil_source_registry_register(&registry, "<fstring>", 9U, text, length, &source_id, state->program->error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_token_list_free(&tokens);
        vigil_diagnostic_list_free(&diagnostics);
        vigil_source_registry_free(&registry);
        return status;
    }

    status = vigil_lex_source(&registry, source_id, &tokens, &diagnostics, state->program->error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_token_list_free(&tokens);
        vigil_diagnostic_list_free(&diagnostics);
        vigil_source_registry_free(&registry);
        return vigil_parser_report(state, error_span, "invalid f-string interpolation expression");
    }

    source = vigil_source_registry_get(&registry, source_id);
    if (source == NULL)
    {
        vigil_token_list_free(&tokens);
        vigil_diagnostic_list_free(&diagnostics);
        vigil_source_registry_free(&registry);
        return VIGIL_STATUS_INTERNAL;
    }

    for (token_index = 0U; token_index < tokens.count; token_index += 1U)
    {
        if (absolute_offset != 0U)
        {
            tokens.items[token_index].span.source_id = state->program->source->id;
            tokens.items[token_index].span.start_offset += absolute_offset;
            tokens.items[token_index].span.end_offset += absolute_offset;
        }
    }

    program = (vigil_program_state_t *)state->program;
    previous_tokens = program->tokens;
    program->tokens = &tokens;
    {
        const vigil_source_file_t *previous_source = NULL;
        if (absolute_offset == 0U)
        {
            previous_source = program->source;
            program->source = source;
        }
        nested = *state;
        nested.current = 0U;
        nested.body_end = vigil_token_list_count(&tokens);
        vigil_expression_result_clear(&nested_result);
        status = vigil_parser_parse_expression(&nested, &nested_result);
        if (status == VIGIL_STATUS_OK)
        {
            /* Skip trailing semicolons from automatic semicolon insertion. */
            while (vigil_parser_match(&nested, VIGIL_TOKEN_SEMICOLON))
            {
            }
        }
        if (status == VIGIL_STATUS_OK && !vigil_parser_check(&nested, VIGIL_TOKEN_EOF))
        {
            status = vigil_parser_report(
                &nested, vigil_parser_peek(&nested) == NULL ? error_span : vigil_parser_peek(&nested)->span,
                "expected end of f-string interpolation expression");
        }
        if (status == VIGIL_STATUS_OK)
        {
            state->chunk = nested.chunk;
            *out_result = nested_result;
        }
        if (absolute_offset == 0U)
        {
            program->source = previous_source;
        }
    }
    program->tokens = previous_tokens;
    vigil_token_list_free(&tokens);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    return status;
}

vigil_status_t vigil_parser_emit_fstring_part_string(vigil_parser_state_t *state, vigil_source_span_t span,
                                                     int *part_count, const char *text, size_t length)
{
    vigil_status_t status;

    if (state == NULL || part_count == NULL || text == NULL)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_parser_emit_string_constant_text(state, span, text, length);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    if (*part_count > 0)
    {
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ADD, span);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }
    *part_count += 1;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_parser_emit_fstring_part_value(vigil_parser_state_t *state, vigil_source_span_t span,
                                                    int *part_count)
{
    vigil_status_t status;

    if (state == NULL || part_count == NULL)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (*part_count > 0)
    {
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ADD, span);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }
    *part_count += 1;
    return VIGIL_STATUS_OK;
}

/* ── F-string interpolation scanning ───────────────────────────────── */

typedef struct
{
    size_t paren_depth;
    size_t bracket_depth;
    size_t brace_depth;
    size_t format_start;
} fstring_scan_state_t;

static size_t fstring_skip_escaped_string(const char *text, size_t length, size_t cursor, char delim)
{
    cursor += 2U; /* skip \<quote> */
    while (cursor + 1U < length)
    {
        if (text[cursor] == '\\' && text[cursor + 1U] == delim)
        {
            return cursor + 2U;
        }
        if (text[cursor] == '\\' && cursor + 1U < length)
        {
            cursor += 2U;
            continue;
        }
        cursor += 1U;
    }
    return cursor;
}

static int fstring_is_quote(char c)
{
    return c == '"' || c == '\'' || c == '`';
}

static size_t fstring_skip_string_or_escape(const char *text, size_t length, size_t cursor)
{
    if (text[cursor] == '\\' && cursor + 1U < length)
    {
        if (fstring_is_quote(text[cursor + 1U]))
        {
            return fstring_skip_escaped_string(text, length, cursor, text[cursor + 1U]);
        }
        return cursor + 2U;
    }
    if (fstring_is_quote(text[cursor]))
    {
        return vigil_program_skip_quoted_text(text, length, cursor, text[cursor]);
    }
    return 0U; /* not a string/escape */
}

static void fstring_update_depth(char c, fstring_scan_state_t *ss)
{
    if (c == '(')
    {
        ss->paren_depth += 1U;
    }
    else if (c == ')' && ss->paren_depth > 0U)
    {
        ss->paren_depth -= 1U;
    }
    else if (c == '[')
    {
        ss->bracket_depth += 1U;
    }
    else if (c == ']' && ss->bracket_depth > 0U)
    {
        ss->bracket_depth -= 1U;
    }
    else if (c == '{')
    {
        ss->brace_depth += 1U;
    }
}

static int fstring_at_top_level(const fstring_scan_state_t *ss)
{
    return ss->paren_depth == 0U && ss->bracket_depth == 0U && ss->brace_depth == 0U;
}

static size_t fstring_advance_cursor(const char *text, size_t length, size_t cursor, fstring_scan_state_t *ss,
                                     size_t *out_end)
{
    size_t skip = fstring_skip_string_or_escape(text, length, cursor);
    if (skip != 0U)
    {
        return skip;
    }

    if (text[cursor] == '}')
    {
        if (fstring_at_top_level(ss))
        {
            *out_end = cursor;
            return cursor;
        }
        ss->brace_depth -= 1U;
    }
    else if (text[cursor] == ':' && fstring_at_top_level(ss) && ss->format_start == SIZE_MAX)
    {
        ss->format_start = cursor + 1U;
    }
    else
    {
        fstring_update_depth(text[cursor], ss);
    }

    return cursor + 1U;
}

static size_t fstring_scan_interpolation_end(const char *text, size_t length, size_t expression_start,
                                             size_t *out_format_start)
{
    fstring_scan_state_t ss;
    size_t cursor = expression_start;
    size_t end_pos = SIZE_MAX;

    memset(&ss, 0, sizeof(ss));
    ss.format_start = SIZE_MAX;

    while (cursor < length)
    {
        size_t next = fstring_advance_cursor(text, length, cursor, &ss, &end_pos);
        if (end_pos != SIZE_MAX)
        {
            *out_format_start = ss.format_start;
            return end_pos;
        }
        cursor = next;
    }

    *out_format_start = ss.format_start;
    return SIZE_MAX;
}

/* ── F-string expression decode + compile ──────────────────────────── */

static void fstring_decode_escapes(const char *expr_text, size_t expr_len, vigil_string_t *decoded,
                                   const vigil_program_state_t *program)
{
    size_t ei;

    for (ei = 0; ei < expr_len; ei++)
    {
        if (expr_text[ei] == '\\' && ei + 1U < expr_len)
        {
            char next = expr_text[ei + 1U];
            if (next == '"' || next == '\'' || next == '\\')
            {
                vigil_string_append(decoded, &next, 1U, program->error);
                ei++;
                continue;
            }
        }
        vigil_string_append(decoded, expr_text + ei, 1U, program->error);
    }
}

static vigil_status_t fstring_compile_expression(vigil_parser_state_t *state, const vigil_token_t *token,
                                                 const char *text, size_t trim_start, size_t trim_end,
                                                 vigil_expression_result_t *out_result)
{
    const char *expr_text = text + trim_start;
    size_t expr_len = trim_end - trim_start;
    size_t absolute_offset = token->span.start_offset + trim_start;
    int has_escapes = 0;
    size_t ei;

    for (ei = 0; ei < expr_len; ei++)
    {
        if (expr_text[ei] == '\\')
        {
            has_escapes = 1;
            break;
        }
    }

    if (has_escapes)
    {
        vigil_status_t status;
        vigil_string_t decoded_expr;
        vigil_string_init(&decoded_expr, state->program->registry->runtime);
        fstring_decode_escapes(expr_text, expr_len, &decoded_expr, state->program);
        status = vigil_parser_parse_embedded_expression(
            state, vigil_string_c_str(&decoded_expr), vigil_string_length(&decoded_expr), 0U, token->span, out_result);
        vigil_string_free(&decoded_expr);
        return status;
    }

    return vigil_parser_parse_embedded_expression(state, expr_text, expr_len, absolute_offset, token->span, out_result);
}

/* ── Format specifier types ────────────────────────────────────────── */

typedef struct
{
    char fill_char;
    unsigned int align_val;
    unsigned int width_val;
    unsigned int prec_val;
    unsigned int fmt_type;
    unsigned int grouping_val;
} fstring_format_spec_t;

/* ── Format specifier parsing ──────────────────────────────────────── */

static unsigned int fstring_align_value(char c)
{
    if (c == '<')
    {
        return 1U;
    }
    if (c == '>')
    {
        return 2U;
    }
    if (c == '^')
    {
        return 3U;
    }
    return 0U;
}

static void fstring_parse_alignment(const char *text, size_t *fs, size_t fe, fstring_format_spec_t *spec)
{
    if (fe - *fs >= 2U && fstring_align_value(text[*fs + 1U]) != 0U)
    {
        spec->fill_char = text[*fs];
        spec->align_val = fstring_align_value(text[*fs + 1U]);
        *fs += 2U;
    }
    else if (fe - *fs >= 1U && fstring_align_value(text[*fs]) != 0U)
    {
        spec->align_val = fstring_align_value(text[*fs]);
        *fs += 1U;
    }
}

static void fstring_parse_width(const char *text, size_t *fs, size_t fe, fstring_format_spec_t *spec)
{
    while (*fs < fe && text[*fs] >= '0' && text[*fs] <= '9')
    {
        spec->width_val = spec->width_val * 10U + (unsigned int)(text[*fs] - '0');
        *fs += 1U;
    }
}

static void fstring_parse_grouping(const char *text, size_t *fs, size_t fe, fstring_format_spec_t *spec)
{
    if (*fs < fe && text[*fs] == ',')
    {
        spec->grouping_val = 1U;
        *fs += 1U;
    }
}

static void fstring_parse_precision(const char *text, size_t *fs, size_t fe, fstring_format_spec_t *spec)
{
    if (*fs < fe && text[*fs] == '.')
    {
        *fs += 1U;
        while (*fs < fe && text[*fs] >= '0' && text[*fs] <= '9')
        {
            spec->prec_val = spec->prec_val * 10U + (unsigned int)(text[*fs] - '0');
            *fs += 1U;
        }
    }
}

static vigil_status_t fstring_parse_type_char(vigil_parser_state_t *state, vigil_source_span_t span, const char *text,
                                              size_t *fs, size_t fe, fstring_format_spec_t *spec)
{
    if (*fs < fe)
    {
        char tc = text[*fs];
        if (tc == 'd')
        {
            spec->fmt_type = 1U;
        }
        else if (tc == 'x')
        {
            spec->fmt_type = 2U;
        }
        else if (tc == 'X')
        {
            spec->fmt_type = 3U;
        }
        else if (tc == 'b')
        {
            spec->fmt_type = 4U;
        }
        else if (tc == 'o')
        {
            spec->fmt_type = 5U;
        }
        else if (tc == 'f')
        {
            spec->fmt_type = 6U;
        }
        else
        {
            return vigil_parser_report(state, span, "invalid format type character (expected d, x, X, b, o, or f)");
        }
        *fs += 1U;
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t fstring_parse_format_spec(vigil_parser_state_t *state, vigil_source_span_t span, const char *text,
                                                size_t format_start, size_t cursor, fstring_format_spec_t *spec)
{
    size_t fs;
    size_t fe;
    vigil_status_t status;

    vigil_program_trim_text_range(text, format_start, cursor, &fs, &fe);
    if (fs >= fe)
    {
        return vigil_parser_report(state, span, "empty format specifier");
    }

    memset(spec, 0, sizeof(*spec));
    fstring_parse_alignment(text, &fs, fe, spec);
    fstring_parse_width(text, &fs, fe, spec);
    fstring_parse_grouping(text, &fs, fe, spec);
    fstring_parse_precision(text, &fs, fe, spec);
    status = fstring_parse_type_char(state, span, text, &fs, fe, spec);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (fs != fe)
    {
        return vigil_parser_report(state, span, "invalid f-string format specifier");
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t fstring_validate_format_type(vigil_parser_state_t *state, vigil_source_span_t span,
                                                   vigil_expression_result_t *expr, fstring_format_spec_t *spec)
{
    if (spec->fmt_type == 6U)
    {
        if (!vigil_parser_type_is_f64(expr->type))
        {
            return vigil_parser_report(state, span, "float format specifier 'f' requires an f64 value");
        }
    }
    else if (spec->fmt_type >= 1U && spec->fmt_type <= 5U)
    {
        if (!vigil_parser_type_is_integer(expr->type))
        {
            return vigil_parser_report(state, span, "integer format specifier requires an integer value");
        }
    }
    else if (spec->grouping_val)
    {
        if (vigil_parser_type_is_integer(expr->type))
        {
            spec->fmt_type = 1U;
        }
        else
        {
            return vigil_parser_report(state, span, "grouping ',' requires an integer value");
        }
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t fstring_emit_format_spec(vigil_parser_state_t *state, vigil_source_span_t span, const char *text,
                                               size_t format_start, size_t cursor, vigil_expression_result_t *expr)
{
    vigil_status_t status;
    fstring_format_spec_t spec;
    uint32_t word1;
    uint32_t word2;

    status = fstring_parse_format_spec(state, span, text, format_start, cursor, &spec);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = fstring_validate_format_type(state, span, expr, &spec);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (spec.fmt_type == 0U && !spec.grouping_val && !vigil_parser_type_is_string(expr->type))
    {
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_TO_STRING, span);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }

    word1 = ((uint32_t)(unsigned char)spec.fill_char) | (spec.align_val << 8U) | (spec.fmt_type << 10U) |
            (spec.grouping_val << 14U);
    word2 = (spec.width_val & 0xFFFFU) | ((spec.prec_val & 0xFFFFU) << 16U);

    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_FORMAT_SPEC, span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_u32(state, word1, span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    return vigil_parser_emit_u32(state, word2, span);
}

/* ── Refactored f-string literal parser ────────────────────────────── */

static vigil_status_t fstring_coerce_to_string(vigil_parser_state_t *state, vigil_source_span_t span,
                                               vigil_expression_result_t *expr)
{
    if (vigil_parser_type_is_string(expr->type))
    {
        return VIGIL_STATUS_OK;
    }
    if (!vigil_parser_type_is_integer(expr->type) && !vigil_parser_type_is_f64(expr->type) &&
        !vigil_parser_type_is_bool(expr->type))
    {
        return vigil_parser_report(state, span,
                                   "f-string interpolation requires a string, integer, f64, or bool value");
    }
    return vigil_parser_emit_opcode(state, VIGIL_OPCODE_TO_STRING, span);
}

static vigil_status_t fstring_compile_interpolation(vigil_parser_state_t *state, const vigil_token_t *token,
                                                    const char *text, size_t inner_length, size_t *index,
                                                    int *part_count)
{
    vigil_status_t status;
    size_t expression_start = *index + 1U;
    size_t expression_end;
    size_t format_start = SIZE_MAX;
    size_t trim_start;
    size_t trim_end;
    size_t cursor;
    vigil_expression_result_t expression_result;

    cursor = fstring_scan_interpolation_end(text, inner_length, expression_start, &format_start);
    expression_end = (format_start != SIZE_MAX) ? format_start - 1U : cursor;

    if (cursor == SIZE_MAX || cursor >= inner_length)
    {
        return vigil_parser_report(state, token->span, "unterminated f-string interpolation");
    }

    vigil_program_trim_text_range(text, expression_start, expression_end, &trim_start, &trim_end);
    if (trim_start == trim_end)
    {
        return vigil_parser_report(state, token->span, "f-string interpolation expression must not be empty");
    }

    vigil_expression_result_clear(&expression_result);
    status = fstring_compile_expression(state, token, text, trim_start, trim_end, &expression_result);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_scalar_expression(state, token->span, &expression_result,
                                                    "f-string interpolation expressions must be single values");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (format_start == SIZE_MAX)
    {
        status = fstring_coerce_to_string(state, token->span, &expression_result);
    }
    else
    {
        status = fstring_emit_format_spec(state, token->span, text, format_start, cursor, &expression_result);
    }
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_parser_emit_fstring_part_value(state, token->span, part_count);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    *index = cursor + 1U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t fstring_process_interpolation(vigil_parser_state_t *state, const vigil_token_t *token,
                                                    size_t segment_start, size_t *index, vigil_string_t *segment,
                                                    int *part_count)
{
    vigil_status_t status;
    const char *text;
    size_t length;

    text = vigil_parser_token_text(state, token, &length);

    vigil_string_clear(segment);
    status =
        vigil_program_append_decoded_string_range(state->program, token->span, text, segment_start, *index, segment);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    if (vigil_string_length(segment) > 0U)
    {
        status = vigil_parser_emit_fstring_part_string(state, token->span, part_count, vigil_string_c_str(segment),
                                                       vigil_string_length(segment));
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }

    return fstring_compile_interpolation(state, token, text, length - 1U, index, part_count);
}

vigil_status_t vigil_parser_parse_fstring_literal(vigil_parser_state_t *state, const vigil_token_t *token,
                                                  vigil_expression_result_t *out_result)
{
    const char *text;
    size_t length;
    size_t index;
    size_t segment_start;
    int part_count;
    vigil_string_t segment;
    vigil_status_t status;

    if (state == NULL || token == NULL || out_result == NULL)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    text = vigil_parser_token_text(state, token, &length);
    if (text == NULL || length < 3U)
    {
        return vigil_parser_report(state, token->span, "invalid f-string literal");
    }

    vigil_string_init(&segment, state->program->registry->runtime);
    segment_start = 2U;
    part_count = 0;
    index = 2U;
    while (index < length - 1U)
    {
        if (text[index] == '\\')
        {
            index += 2U;
            continue;
        }
        if (text[index] == '{' && index + 1U < length - 1U && text[index + 1U] == '{')
        {
            index += 2U;
            continue;
        }
        if (text[index] == '}' && index + 1U < length - 1U && text[index + 1U] == '}')
        {
            index += 2U;
            continue;
        }
        if (text[index] != '{')
        {
            if (text[index] == '}')
            {
                vigil_string_free(&segment);
                return vigil_parser_report(state, token->span, "unmatched '}' in f-string");
            }
            index += 1U;
            continue;
        }

        status = fstring_process_interpolation(state, token, segment_start, &index, &segment, &part_count);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_string_free(&segment);
            return status;
        }
        segment_start = index;
    }

    vigil_string_clear(&segment);
    status = vigil_program_append_decoded_string_range(state->program, token->span, text, segment_start, length - 1U,
                                                       &segment);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_string_free(&segment);
        return status;
    }
    if (vigil_string_length(&segment) > 0U || part_count == 0)
    {
        status = vigil_parser_emit_fstring_part_string(state, token->span, &part_count, vigil_string_c_str(&segment),
                                                       vigil_string_length(&segment));
    }
    vigil_string_free(&segment);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_STRING));
    return VIGIL_STATUS_OK;
}
