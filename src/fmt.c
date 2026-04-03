/*
 * vigil_fmt — token-stream source formatter.
 *
 * Walks the token list and the raw source text to produce canonically
 * formatted output.  Comments are preserved by scanning the gaps between
 * consecutive token spans in the original source.
 *
 * Indentation: 4 spaces per brace-depth level.
 * All control-flow bodies are multi-line (no single-line ifs).
 * Imports are sorted alphabetically and grouped before other decls.
 */

#include "vigil/fmt.h"
#include "vigil/allocator.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/token.h"

/* ── dynamic buffer ──────────────────────────────────────────────── */

typedef struct
{
    char *data;
    size_t len;
    size_t cap;
    vigil_allocator_t alloc;
} buf_t;

static void buf_init(buf_t *b, const vigil_allocator_t *a)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    if (a != NULL && a->allocate != NULL)
        b->alloc = *a;
    else
        b->alloc = vigil_default_allocator();
}

static void buf_grow(buf_t *b, size_t need)
{
    if (b->len + need <= b->cap)
        return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + need)
        cap *= 2;
    b->data = (char *)b->alloc.reallocate(b->alloc.user_data, b->data, cap);
    b->cap = cap;
}

static void buf_push(buf_t *b, char c)
{
    buf_grow(b, 1);
    b->data[b->len++] = c;
}

static void buf_write(buf_t *b, const char *s, size_t n)
{
    if (n == 0 || !s)
        return;
    buf_grow(b, n);
    if (!b->data)
        return;
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

static void buf_puts(buf_t *b, const char *s)
{
    buf_write(b, s, strlen(s));
}

/* ── formatter state ─────────────────────────────────────────────── */

typedef struct
{
    const char *src;
    size_t src_len;
    const vigil_token_list_t *tokens;
    size_t count;
    buf_t out;
    int indent;
    bool at_line_start;
    size_t prev_emitted_index;
    size_t current_index;
} fmt_state_t;

/* Per-token loop context. */
typedef struct
{
    int for_paren_depth;
    bool in_for_header;
    bool in_case_body;
    bool after_case_kw;
    bool brace_is_literal[64];
    int brace_depth;
    bool prev_was_literal_rbrace;
    int generic_depth;
    int ternary_depth;
    bool in_enum_body;
    size_t skipped_control_rparen;
    /* Multi-line list tracking for trailing comma enforcement. */
    int list_depth;
    bool list_multiline[32];
} fmt_ctx_t;

static void emit_indent(fmt_state_t *f)
{
    for (int i = 0; i < f->indent; i++)
        buf_puts(&f->out, "    ");
    f->at_line_start = false;
}

static void emit_newline(fmt_state_t *f)
{
    buf_push(&f->out, '\n');
    f->at_line_start = true;
}

static void emit_space(fmt_state_t *f)
{
    buf_push(&f->out, ' ');
}

static void emit_str(fmt_state_t *f, const char *s, size_t n)
{
    if (f->at_line_start && n > 0)
        emit_indent(f);
    buf_write(&f->out, s, n);
    f->at_line_start = false;
}

static void emit_cstr(fmt_state_t *f, const char *s)
{
    emit_str(f, s, strlen(s));
}

static const char *tok_text(const fmt_state_t *f, const vigil_token_t *t)
{
    return f->src + t->span.start_offset;
}

static size_t tok_len(const vigil_token_t *t)
{
    return t->span.end_offset - t->span.start_offset;
}

/* ── comment extraction ──────────────────────────────────────── */

/* Check whether the source text between two offsets contains a newline. */
static bool source_has_newline(const fmt_state_t *f, size_t from, size_t to)
{
    for (size_t i = from; i < to && i < f->src_len; i++)
    {
        if (f->src[i] == '\n')
            return true;
    }
    return false;
}

/* Generic forward scan for a matching close delimiter. */
static size_t fmt_find_matching_close(const fmt_state_t *f, size_t i, vigil_token_kind_t open_kind,
                                      vigil_token_kind_t close_kind)
{
    size_t depth = 0U;

    for (; i < f->count; i++)
    {
        const vigil_token_t *token = vigil_token_list_get(f->tokens, i);
        if (token->kind == open_kind)
            depth += 1U;
        else if (token->kind == close_kind)
        {
            if (depth == 0U)
                return SIZE_MAX;
            depth -= 1U;
            if (depth == 0U)
                return i;
        }
    }

    return SIZE_MAX;
}

/* Check if the current list nesting level is multi-line. */
static bool fmt_in_multiline_list(const fmt_ctx_t *ctx)
{
    int ld = ctx->list_depth - 1;
    return ld >= 0 && ld < 32 && ctx->list_multiline[ld];
}

static void emit_comment_text(fmt_state_t *f, size_t cstart, size_t cend)
{
    if (!f->at_line_start)
        emit_newline(f);
    emit_indent(f);
    buf_write(&f->out, f->src + cstart, cend - cstart);
    emit_newline(f);
}

static size_t skip_block_comment(const char *src, size_t i, size_t end)
{
    i += 2;
    while (i + 1 < end && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
    if (i + 1 < end)
        i += 2;
    return i;
}

static bool emit_comments_between(fmt_state_t *f, size_t start, size_t end)
{
    bool emitted = false;
    size_t i = start;
    while (i < end)
    {
        if (i + 1 < end && f->src[i] == '/' && f->src[i + 1] == '/')
        {
            size_t cstart = i;
            while (i < end && f->src[i] != '\n')
                i++;
            emit_comment_text(f, cstart, i);
            emitted = true;
        }
        else if (i + 1 < end && f->src[i] == '/' && f->src[i + 1] == '*')
        {
            size_t cstart = i;
            i = skip_block_comment(f->src, i, end);
            emit_comment_text(f, cstart, i);
            emitted = true;
        }
        else
        {
            i++;
        }
    }
    return emitted;
}

/* ── token classification helpers (bitmap tables) ────────────────── */

// clang-format off
#define TOK_BIT(k) (UINT64_C(1) << ((k) & 63))
#define TOK_IDX(k) ((unsigned)(k) >> 6)
#define TOK_TEST(tbl, k) (((tbl)[TOK_IDX(k)] & TOK_BIT(k)) != 0)

static const uint64_t kBinaryOpBits[2] = {
    TOK_BIT(VIGIL_TOKEN_PLUS) | TOK_BIT(VIGIL_TOKEN_MINUS) | TOK_BIT(VIGIL_TOKEN_STAR) |
    TOK_BIT(VIGIL_TOKEN_SLASH) | TOK_BIT(VIGIL_TOKEN_PERCENT) | TOK_BIT(VIGIL_TOKEN_EQUAL_EQUAL) |
    TOK_BIT(VIGIL_TOKEN_BANG_EQUAL) | TOK_BIT(VIGIL_TOKEN_LESS) | TOK_BIT(VIGIL_TOKEN_LESS_EQUAL) |
    TOK_BIT(VIGIL_TOKEN_GREATER) | TOK_BIT(VIGIL_TOKEN_GREATER_EQUAL) | TOK_BIT(VIGIL_TOKEN_AMPERSAND_AMPERSAND) |
    TOK_BIT(VIGIL_TOKEN_AMPERSAND) ,
    TOK_BIT(VIGIL_TOKEN_PIPE_PIPE) | TOK_BIT(VIGIL_TOKEN_PIPE) | TOK_BIT(VIGIL_TOKEN_CARET) |
    TOK_BIT(VIGIL_TOKEN_SHIFT_LEFT) | TOK_BIT(VIGIL_TOKEN_SHIFT_RIGHT)
};

static const uint64_t kAssignOpBits[2] = {
    TOK_BIT(VIGIL_TOKEN_ASSIGN) | TOK_BIT(VIGIL_TOKEN_PLUS_ASSIGN) | TOK_BIT(VIGIL_TOKEN_MINUS_ASSIGN) |
    TOK_BIT(VIGIL_TOKEN_STAR_ASSIGN) | TOK_BIT(VIGIL_TOKEN_SLASH_ASSIGN) | TOK_BIT(VIGIL_TOKEN_PERCENT_ASSIGN) ,
    0
};

static const uint64_t kKeywordBits[2] = {
    TOK_BIT(VIGIL_TOKEN_IMPORT) | TOK_BIT(VIGIL_TOKEN_AS) | TOK_BIT(VIGIL_TOKEN_PUB) |
    TOK_BIT(VIGIL_TOKEN_FN) | TOK_BIT(VIGIL_TOKEN_CLASS) | TOK_BIT(VIGIL_TOKEN_INTERFACE) |
    TOK_BIT(VIGIL_TOKEN_ENUM) | TOK_BIT(VIGIL_TOKEN_CONST) | TOK_BIT(VIGIL_TOKEN_RETURN) |
    TOK_BIT(VIGIL_TOKEN_DEFER) | TOK_BIT(VIGIL_TOKEN_IF) | TOK_BIT(VIGIL_TOKEN_ELSE) |
    TOK_BIT(VIGIL_TOKEN_FOR) | TOK_BIT(VIGIL_TOKEN_WHILE) | TOK_BIT(VIGIL_TOKEN_SWITCH) |
    TOK_BIT(VIGIL_TOKEN_GUARD) | TOK_BIT(VIGIL_TOKEN_CASE) | TOK_BIT(VIGIL_TOKEN_DEFAULT) |
    TOK_BIT(VIGIL_TOKEN_BREAK) | TOK_BIT(VIGIL_TOKEN_CONTINUE) | TOK_BIT(VIGIL_TOKEN_IN) |
    TOK_BIT(VIGIL_TOKEN_NIL) | TOK_BIT(VIGIL_TOKEN_TRUE) | TOK_BIT(VIGIL_TOKEN_FALSE) ,
    0
};

static const uint64_t kNoSpaceBeforeBits[2] = {
    TOK_BIT(VIGIL_TOKEN_RPAREN) | TOK_BIT(VIGIL_TOKEN_RBRACKET) | TOK_BIT(VIGIL_TOKEN_RBRACE) |
    TOK_BIT(VIGIL_TOKEN_COMMA) | TOK_BIT(VIGIL_TOKEN_SEMICOLON) | TOK_BIT(VIGIL_TOKEN_DOT) |
    TOK_BIT(VIGIL_TOKEN_COLON) | TOK_BIT(VIGIL_TOKEN_PLUS_PLUS) | TOK_BIT(VIGIL_TOKEN_MINUS_MINUS),
    0
};

static const uint64_t kNoSpaceAfterBits[2] = {
    TOK_BIT(VIGIL_TOKEN_LPAREN) | TOK_BIT(VIGIL_TOKEN_LBRACKET) | TOK_BIT(VIGIL_TOKEN_LBRACE) |
    TOK_BIT(VIGIL_TOKEN_DOT),
    0
};

static const uint64_t kTopLevelDeclBits[2] = {
    TOK_BIT(VIGIL_TOKEN_FN) | TOK_BIT(VIGIL_TOKEN_CLASS) | TOK_BIT(VIGIL_TOKEN_INTERFACE) |
    TOK_BIT(VIGIL_TOKEN_ENUM) | TOK_BIT(VIGIL_TOKEN_CONST) | TOK_BIT(VIGIL_TOKEN_PUB),
    0
};

static const uint64_t kLiteralBraceOpenerBits[2] = {
    TOK_BIT(VIGIL_TOKEN_ASSIGN) | TOK_BIT(VIGIL_TOKEN_PLUS_ASSIGN) | TOK_BIT(VIGIL_TOKEN_MINUS_ASSIGN) |
    TOK_BIT(VIGIL_TOKEN_STAR_ASSIGN) | TOK_BIT(VIGIL_TOKEN_SLASH_ASSIGN) | TOK_BIT(VIGIL_TOKEN_PERCENT_ASSIGN) |
    TOK_BIT(VIGIL_TOKEN_LPAREN) | TOK_BIT(VIGIL_TOKEN_LBRACKET) | TOK_BIT(VIGIL_TOKEN_COMMA) |
    TOK_BIT(VIGIL_TOKEN_RETURN) | TOK_BIT(VIGIL_TOKEN_COLON),
    0
};

static bool is_binary_op(vigil_token_kind_t k) { return TOK_TEST(kBinaryOpBits, k); }
static bool is_assign_op(vigil_token_kind_t k) { return TOK_TEST(kAssignOpBits, k); }
static bool is_keyword(vigil_token_kind_t k) { return TOK_TEST(kKeywordBits, k); }
// clang-format on

/* ── import sorting ──────────────────────────────────────────────── */

typedef struct
{
    size_t start_idx;
    size_t end_idx;
    const char *path;
    size_t path_len;
} import_info_t;

static int import_cmp(const void *a, const void *b)
{
    const import_info_t *ia = a;
    const import_info_t *ib = b;
    size_t min = ia->path_len < ib->path_len ? ia->path_len : ib->path_len;
    int c = memcmp(ia->path, ib->path, min);
    if (c != 0)
        return c;
    return (ia->path_len > ib->path_len) - (ia->path_len < ib->path_len);
}

/* ── spacing rules ───────────────────────────────────────────────── */

static bool is_call_prefix(vigil_token_kind_t pk)
{
    return pk == VIGIL_TOKEN_IDENTIFIER || pk == VIGIL_TOKEN_RPAREN || pk == VIGIL_TOKEN_RBRACKET;
}

static bool is_control_condition_keyword(vigil_token_kind_t kind)
{
    return kind == VIGIL_TOKEN_IF || kind == VIGIL_TOKEN_WHILE || kind == VIGIL_TOKEN_SWITCH;
}

static bool fmt_is_lambda_open_pipe(const fmt_state_t *f, size_t index)
{
    size_t cursor;

    if (f == NULL || index >= f->count)
        return false;
    if (vigil_token_list_get(f->tokens, index)->kind != VIGIL_TOKEN_PIPE)
        return false;

    cursor = index + 1U;
    if (cursor < f->count && vigil_token_list_get(f->tokens, cursor)->kind == VIGIL_TOKEN_PIPE)
        return true;

    while (cursor < f->count)
    {
        vigil_token_kind_t kind = vigil_token_list_get(f->tokens, cursor)->kind;

        if (kind == VIGIL_TOKEN_IDENTIFIER)
        {
            cursor += 1U;
            continue;
        }
        if (kind == VIGIL_TOKEN_COMMA)
        {
            cursor += 1U;
            continue;
        }
        return kind == VIGIL_TOKEN_PIPE;
    }

    return false;
}

static bool fmt_is_lambda_close_pipe(const fmt_state_t *f, size_t index)
{
    size_t cursor;

    if (f == NULL || index >= f->count)
        return false;
    if (vigil_token_list_get(f->tokens, index)->kind != VIGIL_TOKEN_PIPE)
        return false;
    if (index == 0U)
        return false;

    cursor = index;
    if (cursor > 0U && vigil_token_list_get(f->tokens, cursor - 1U)->kind == VIGIL_TOKEN_PIPE)
        return true;

    while (cursor > 0U)
    {
        vigil_token_kind_t kind = vigil_token_list_get(f->tokens, cursor - 1U)->kind;

        if (kind == VIGIL_TOKEN_IDENTIFIER || kind == VIGIL_TOKEN_COMMA)
        {
            cursor -= 1U;
            continue;
        }
        return kind == VIGIL_TOKEN_PIPE && fmt_is_lambda_open_pipe(f, cursor - 1U);
    }

    return false;
}

static bool need_space_before(const vigil_token_t *prev, const vigil_token_t *cur, const fmt_state_t *f)
{
    vigil_token_kind_t pk = prev->kind;
    vigil_token_kind_t ck = cur->kind;

    if (pk == VIGIL_TOKEN_PIPE && fmt_is_lambda_open_pipe(f, f->prev_emitted_index))
    {
        return false;
    }
    if (ck == VIGIL_TOKEN_PIPE && fmt_is_lambda_close_pipe(f, f->current_index))
    {
        return false;
    }

    if (TOK_TEST(kNoSpaceAfterBits, pk))
        return false;
    if (TOK_TEST(kNoSpaceBeforeBits, ck))
        return false;
    if ((ck == VIGIL_TOKEN_LPAREN || ck == VIGIL_TOKEN_LBRACKET) && is_call_prefix(pk))
        return false;

    if (is_binary_op(ck) || is_assign_op(ck) || is_binary_op(pk) || is_assign_op(pk))
        return true;
    if (pk == VIGIL_TOKEN_COMMA || pk == VIGIL_TOKEN_COLON || pk == VIGIL_TOKEN_QUESTION)
        return true;
    if (ck == VIGIL_TOKEN_QUESTION || ck == VIGIL_TOKEN_ARROW || pk == VIGIL_TOKEN_ARROW)
        return true;
    if (is_keyword(pk))
        return true;
    if (ck == VIGIL_TOKEN_LBRACE)
        return true;
    if (pk == VIGIL_TOKEN_IDENTIFIER && ck == VIGIL_TOKEN_IDENTIFIER)
        return true;
    if (pk == VIGIL_TOKEN_GREATER && ck == VIGIL_TOKEN_IDENTIFIER)
        return true;

    if (ck == VIGIL_TOKEN_BANG || ck == VIGIL_TOKEN_TILDE)
        return pk == VIGIL_TOKEN_IDENTIFIER || pk == VIGIL_TOKEN_INT_LITERAL || pk == VIGIL_TOKEN_RPAREN;
    if (ck == VIGIL_TOKEN_MINUS && (pk == VIGIL_TOKEN_LPAREN || pk == VIGIL_TOKEN_COMMA || is_assign_op(pk) ||
                                    is_binary_op(pk) || pk == VIGIL_TOKEN_RETURN))
        return false;

    return true;
}

static int need_newline_before(const vigil_token_t *prev, const vigil_token_t *cur, const fmt_state_t *f)
{
    vigil_token_kind_t pk = prev->kind;
    vigil_token_kind_t ck = cur->kind;
    (void)f;

    if (pk == VIGIL_TOKEN_LBRACE)
        return 1;
    if (ck == VIGIL_TOKEN_RBRACE)
        return 1;
    if (pk == VIGIL_TOKEN_SEMICOLON)
        return 1;
    if (pk == VIGIL_TOKEN_RBRACE)
    {
        if (ck == VIGIL_TOKEN_ELSE)
            return 0;
        if (ck == VIGIL_TOKEN_RBRACE)
            return 1;
        return 2;
    }
    if (ck == VIGIL_TOKEN_CASE || ck == VIGIL_TOKEN_DEFAULT)
        return 1;
    return 0;
}

/* ── import collection (Pass 1) ──────────────────────────────────── */

static size_t fmt_scan_single_import(const fmt_state_t *f, size_t i, import_info_t *imp)
{
    imp->start_idx = i;
    i++; /* skip 'import' */

    if (i < f->count)
    {
        const vigil_token_t *path_tok = vigil_token_list_get(f->tokens, i);
        if (path_tok->kind == VIGIL_TOKEN_STRING_LITERAL)
        {
            imp->path = tok_text(f, path_tok) + 1;
            imp->path_len = tok_len(path_tok) - 2;
        }
        i++;
    }

    if (i < f->count && vigil_token_list_get(f->tokens, i)->kind == VIGIL_TOKEN_AS)
    {
        i++;
        if (i < f->count)
            i++;
    }

    if (i < f->count && vigil_token_list_get(f->tokens, i)->kind == VIGIL_TOKEN_SEMICOLON)
        i++;

    imp->end_idx = i;
    return i;
}

static size_t fmt_collect_imports(fmt_state_t *f, import_info_t **out_imports, size_t *out_count)
{
    import_info_t *imports = NULL;
    size_t count = 0;
    size_t cap = 0;
    size_t i = 0;

    while (i < f->count)
    {
        if (vigil_token_list_get(f->tokens, i)->kind != VIGIL_TOKEN_IMPORT)
            break;
        import_info_t imp = {0};
        i = fmt_scan_single_import(f, i, &imp);
        if (count == cap)
        {
            cap = cap ? cap * 2 : 8;
            imports =
                (import_info_t *)f->out.alloc.reallocate(f->out.alloc.user_data, imports, cap * sizeof(import_info_t));
        }
        imports[count++] = imp;
    }

    if (count > 1)
        qsort(imports, count, sizeof(import_info_t), import_cmp);

    *out_imports = imports;
    *out_count = count;
    return i;
}

/* ── import emission (Pass 2) ────────────────────────────────────── */

static void fmt_emit_single_import(fmt_state_t *f, const import_info_t *imp)
{
    emit_cstr(f, "import \"");
    buf_write(&f->out, imp->path, imp->path_len);
    buf_push(&f->out, '"');

    size_t j = imp->start_idx + 2;
    if (j < imp->end_idx && vigil_token_list_get(f->tokens, j)->kind == VIGIL_TOKEN_AS)
    {
        buf_puts(&f->out, " as ");
        j++;
        if (j < imp->end_idx)
        {
            const vigil_token_t *alias = vigil_token_list_get(f->tokens, j);
            buf_write(&f->out, tok_text(f, alias), tok_len(alias));
        }
    }
    emit_newline(f);
}

static size_t fmt_emit_imports(fmt_state_t *f, const import_info_t *imports, size_t import_count,
                               size_t first_non_import)
{
    size_t initial_comment_end = 0;

    if (import_count > 0)
    {
        const vigil_token_t *first_imp_tok = vigil_token_list_get(f->tokens, imports[0].start_idx);
        if (emit_comments_between(f, 0, first_imp_tok->span.start_offset))
            emit_newline(f);
    }

    if (first_non_import < f->count)
    {
        const vigil_token_t *first_ni = vigil_token_list_get(f->tokens, first_non_import);
        size_t scan_start = 0;
        if (import_count > 0)
        {
            const vigil_token_t *last = vigil_token_list_get(f->tokens, imports[import_count - 1].end_idx - 1);
            scan_start = last->span.end_offset;
        }
        emit_comments_between(f, scan_start, first_ni->span.start_offset);
        initial_comment_end = first_ni->span.start_offset;
    }

    for (size_t ii = 0; ii < import_count; ii++)
    {
        const vigil_token_t *imp_tok = vigil_token_list_get(f->tokens, imports[ii].start_idx);
        if (ii > 0)
        {
            const vigil_token_t *prev_end = vigil_token_list_get(f->tokens, imports[ii - 1].end_idx - 1);
            emit_comments_between(f, prev_end->span.end_offset, imp_tok->span.start_offset);
        }
        fmt_emit_single_import(f, &imports[ii]);
    }

    if (import_count > 0 && first_non_import < f->count &&
        vigil_token_list_get(f->tokens, first_non_import)->kind != VIGIL_TOKEN_EOF)
        emit_newline(f);

    return initial_comment_end;
}

/* ── Pass 3 helpers ──────────────────────────────────────────────── */

static bool brace_lit_at(const fmt_ctx_t *ctx, int depth)
{
    return depth >= 0 && depth < 64 && ctx->brace_is_literal[depth];
}

static void fmt_adjust_indent_before(fmt_state_t *f, fmt_ctx_t *ctx, vigil_token_kind_t ck)
{
    if (ck == VIGIL_TOKEN_RBRACE)
    {
        ctx->brace_depth--;
        if (!brace_lit_at(ctx, ctx->brace_depth))
        {
            if (ctx->in_case_body)
            {
                f->indent--;
                ctx->in_case_body = false;
            }
            f->indent--;
        }
    }

    if ((ck == VIGIL_TOKEN_CASE || ck == VIGIL_TOKEN_DEFAULT) && ctx->in_case_body)
    {
        f->indent--;
        ctx->in_case_body = false;
    }

    if (ck == VIGIL_TOKEN_CASE || ck == VIGIL_TOKEN_DEFAULT)
        ctx->after_case_kw = true;
}

/* Determine whether newlines should be suppressed (literal brace context). */
static bool fmt_in_literal_context(const fmt_ctx_t *ctx, vigil_token_kind_t pk, vigil_token_kind_t ck)
{
    if (brace_lit_at(ctx, ctx->brace_depth - 1))
        return true;
    if (ck == VIGIL_TOKEN_RBRACE && brace_lit_at(ctx, ctx->brace_depth))
        return true;
    if (pk == VIGIL_TOKEN_LBRACE && brace_lit_at(ctx, ctx->brace_depth - 1))
        return true;
    return false;
}

static bool fmt_should_suppress_newline(const fmt_ctx_t *ctx, vigil_token_kind_t pk, vigil_token_kind_t ck)
{
    if (ctx->in_for_header && pk == VIGIL_TOKEN_SEMICOLON)
        return true;
    if (ctx->prev_was_literal_rbrace && pk == VIGIL_TOKEN_RBRACE)
        return true;
    /* Inside multi-line lists, suppress newline between } and , (lambda end). */
    if (fmt_in_multiline_list(ctx))
    {
        if (pk == VIGIL_TOKEN_RBRACE && ck == VIGIL_TOKEN_COMMA)
            return true;
        return false;
    }
    if (!fmt_in_literal_context(ctx, pk, ck))
        return false;
    return pk == VIGIL_TOKEN_LBRACE || ck == VIGIL_TOKEN_RBRACE || pk == VIGIL_TOKEN_SEMICOLON ||
           pk == VIGIL_TOKEN_RBRACE;
}

/* Apply generic/ternary overrides to the base space decision. */
static bool fmt_is_generic_open(const fmt_state_t *f, const vigil_token_t *prev)
{
    if (prev->kind != VIGIL_TOKEN_IDENTIFIER)
        return false;
    const char *pt = tok_text(f, prev);
    size_t pl = tok_len(prev);
    return (pl == 5 && memcmp(pt, "array", 5) == 0) || (pl == 3 && memcmp(pt, "map", 3) == 0);
}

static bool fmt_adjust_space(const fmt_state_t *f, const fmt_ctx_t *ctx, const vigil_token_t *prev,
                             const vigil_token_t *cur, bool space)
{
    if (ctx->generic_depth > 0 &&
        (cur->kind == VIGIL_TOKEN_GREATER || prev->kind == VIGIL_TOKEN_LESS || prev->kind == VIGIL_TOKEN_GREATER))
        space = false;
    if (cur->kind == VIGIL_TOKEN_LESS && fmt_is_generic_open(f, prev))
        space = false;
    if (cur->kind == VIGIL_TOKEN_COLON && ctx->ternary_depth > 0 && !ctx->after_case_kw)
        space = true;
    return space;
}

static void fmt_emit_newlines(fmt_state_t *f, int nl, bool force_nl)
{
    if (!f->at_line_start)
        emit_newline(f);
    if (nl == 2 && !force_nl)
        emit_newline(f);
}

static int fmt_newline_level(const fmt_state_t *f, const vigil_token_t *prev, const vigil_token_t *cur)
{
    int nl = need_newline_before(prev, cur, f);
    if (nl == 1 && f->indent == 0 && prev->kind == VIGIL_TOKEN_SEMICOLON && TOK_TEST(kTopLevelDeclBits, cur->kind))
        nl = 2;
    return nl;
}

static void fmt_emit_spacing(fmt_state_t *f, const fmt_ctx_t *ctx, const vigil_token_t *prev, const vigil_token_t *cur)
{
    bool suppress = fmt_should_suppress_newline(ctx, prev->kind, cur->kind);
    bool force_nl = ctx->in_enum_body && prev->kind == VIGIL_TOKEN_COMMA;
    if (!force_nl && prev->kind == VIGIL_TOKEN_COMMA && fmt_in_multiline_list(ctx))
        force_nl = true;
    int nl = fmt_newline_level(f, prev, cur);
    /* Inside multi-line lists, cap at single newline (no blank lines). */
    if (nl > 1 && fmt_in_multiline_list(ctx))
        nl = 1;

    if (suppress)
    {
        if (need_space_before(prev, cur, f))
            emit_space(f);
    }
    else if (force_nl || nl >= 1)
    {
        fmt_emit_newlines(f, nl, force_nl);
    }
    else
    {
        if (fmt_adjust_space(f, ctx, prev, cur, need_space_before(prev, cur, f)))
            emit_space(f);
    }
}

/* ── post-token state tracking ───────────────────────────────────── */

static bool fmt_preceded_by_enum(const fmt_state_t *f, size_t i)
{
    for (size_t k = i; k > 0 && i - k < 10; k--)
    {
        vigil_token_kind_t tk = vigil_token_list_get(f->tokens, k - 1)->kind;
        if (tk == VIGIL_TOKEN_ENUM)
            return true;
        if (tk == VIGIL_TOKEN_LBRACE || tk == VIGIL_TOKEN_RBRACE || tk == VIGIL_TOKEN_SEMICOLON)
            return false;
    }
    return false;
}

static size_t fmt_find_matching_rparen(const fmt_state_t *f, size_t i)
{
    size_t depth = 0U;

    for (; i < f->count; i++)
    {
        const vigil_token_t *token = vigil_token_list_get(f->tokens, i);
        if (token->kind == VIGIL_TOKEN_LPAREN)
            depth += 1U;
        else if (token->kind == VIGIL_TOKEN_RPAREN)
        {
            if (depth == 0U)
                return SIZE_MAX;
            depth -= 1U;
            if (depth == 0U)
                return i;
        }
    }

    return SIZE_MAX;
}

static bool fmt_is_control_condition_paren(const fmt_state_t *f, size_t i)
{
    const vigil_token_t *prev;
    size_t closing;

    if (i == 0 || i >= f->count)
        return false;
    if (vigil_token_list_get(f->tokens, i)->kind != VIGIL_TOKEN_LPAREN)
        return false;

    prev = vigil_token_list_get(f->tokens, i - 1);
    if (!is_control_condition_keyword(prev->kind))
        return false;

    closing = fmt_find_matching_rparen(f, i);
    if (closing == SIZE_MAX || closing + 1U >= f->count)
        return false;

    return vigil_token_list_get(f->tokens, closing + 1U)->kind == VIGIL_TOKEN_LBRACE;
}

static void fmt_track_lbrace(fmt_state_t *f, fmt_ctx_t *ctx, size_t i)
{
    bool is_lit = (i > 0) && TOK_TEST(kLiteralBraceOpenerBits, vigil_token_list_get(f->tokens, i - 1)->kind);
    if (ctx->brace_depth < 64)
        ctx->brace_is_literal[ctx->brace_depth] = is_lit;
    ctx->brace_depth++;
    if (!is_lit)
        f->indent++;
    if (i > 0 && fmt_preceded_by_enum(f, i))
        ctx->in_enum_body = true;
}

static void fmt_track_generics(fmt_state_t *f, fmt_ctx_t *ctx, const vigil_token_t *cur, size_t i)
{
    if (cur->kind == VIGIL_TOKEN_LESS && i > 0)
    {
        const vigil_token_t *prev = vigil_token_list_get(f->tokens, i - 1);
        if (prev->kind == VIGIL_TOKEN_IDENTIFIER)
        {
            const char *pt = tok_text(f, prev);
            size_t pl = tok_len(prev);
            if ((pl == 5 && memcmp(pt, "array", 5) == 0) || (pl == 3 && memcmp(pt, "map", 3) == 0))
                ctx->generic_depth++;
        }
    }
    if (cur->kind == VIGIL_TOKEN_GREATER && ctx->generic_depth > 0)
        ctx->generic_depth--;
}

static void fmt_track_for_header(fmt_ctx_t *ctx, vigil_token_kind_t ck)
{
    if (ck == VIGIL_TOKEN_FOR)
    {
        ctx->in_for_header = true;
        ctx->for_paren_depth = 0;
    }
    if (ctx->in_for_header)
    {
        if (ck == VIGIL_TOKEN_LPAREN)
            ctx->for_paren_depth++;
        if (ck == VIGIL_TOKEN_RPAREN && --ctx->for_paren_depth <= 0)
            ctx->in_for_header = false;
    }
}

static void fmt_update_state_after(fmt_state_t *f, fmt_ctx_t *ctx, const vigil_token_t *cur, size_t i)
{
    if (cur->kind == VIGIL_TOKEN_LBRACE)
        fmt_track_lbrace(f, ctx, i);

    if (cur->kind == VIGIL_TOKEN_COLON && ctx->after_case_kw)
    {
        ctx->after_case_kw = false;
        ctx->in_case_body = true;
        f->indent++;
    }

    fmt_track_for_header(ctx, cur->kind);
    fmt_track_generics(f, ctx, cur, i);

    if (cur->kind == VIGIL_TOKEN_QUESTION)
        ctx->ternary_depth++;
    if (cur->kind == VIGIL_TOKEN_COLON && ctx->ternary_depth > 0 && !ctx->after_case_kw)
        ctx->ternary_depth--;

    if (cur->kind == VIGIL_TOKEN_RBRACE)
    {
        ctx->prev_was_literal_rbrace = brace_lit_at(ctx, ctx->brace_depth);
        if (ctx->in_enum_body)
            ctx->in_enum_body = false;
    }
    else
    {
        ctx->prev_was_literal_rbrace = false;
    }
}

/* ── output finalization ─────────────────────────────────────────── */

static void fmt_finalize_output(fmt_state_t *f)
{
    if (f->count > 0)
    {
        const vigil_token_t *last = vigil_token_list_get(f->tokens, f->count - 1);
        emit_comments_between(f, last->span.end_offset, f->src_len);
    }

    if (f->out.len > 0 && f->out.data[f->out.len - 1] != '\n')
        buf_push(&f->out, '\n');

    while (f->out.len > 1 && f->out.data[f->out.len - 1] == '\n' && f->out.data[f->out.len - 2] == '\n')
        f->out.len--;
}

/* ── public entry point ──────────────────────────────────────────── */

vigil_status_t vigil_fmt(const vigil_allocator_t *allocator, const char *source_text, size_t source_length,
                         const vigil_token_list_t *tokens, char **out_text, size_t *out_length, vigil_error_t *error)
{
    (void)error;
    fmt_state_t f;
    f.src = source_text;
    f.src_len = source_length;
    f.tokens = tokens;
    f.count = vigil_token_list_count(tokens);
    buf_init(&f.out, allocator);
    f.indent = 0;
    f.at_line_start = true;
    f.prev_emitted_index = SIZE_MAX;
    f.current_index = SIZE_MAX;

    if (f.count == 0 || (f.count == 1 && vigil_token_list_get(tokens, 0)->kind == VIGIL_TOKEN_EOF))
    {
        *out_text = (char *)f.out.alloc.allocate(f.out.alloc.user_data, 1);
        if (*out_text)
            (*out_text)[0] = '\0';
        *out_length = 0;
        return VIGIL_STATUS_OK;
    }

    import_info_t *imports = NULL;
    size_t import_count = 0;
    size_t first_non_import = fmt_collect_imports(&f, &imports, &import_count);
    size_t initial_comment_end = fmt_emit_imports(&f, imports, import_count, first_non_import);

    fmt_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.skipped_control_rparen = SIZE_MAX;
    size_t prev_emitted_idx = SIZE_MAX;

    for (size_t i = first_non_import; i < f.count; i++)
    {
        const vigil_token_t *cur = vigil_token_list_get(tokens, i);
        if (cur->kind == VIGIL_TOKEN_EOF)
            break;
        if (i == ctx.skipped_control_rparen)
        {
            ctx.skipped_control_rparen = SIZE_MAX;
            continue;
        }
        if (fmt_is_control_condition_paren(&f, i))
        {
            ctx.skipped_control_rparen = fmt_find_matching_rparen(&f, i);
            continue;
        }

        size_t gap_start = prev_emitted_idx != SIZE_MAX
                               ? vigil_token_list_get(tokens, prev_emitted_idx)->span.end_offset
                               : initial_comment_end;
        bool had_comment = emit_comments_between(&f, gap_start, cur->span.start_offset);

        /* For closing delimiters of multi-line lists, we handle
           indent ourselves; skip the normal indent adjustment only
           for ) and ] (braces still need brace_depth tracking). */
        bool handled_multiline_close = false;
        if ((cur->kind == VIGIL_TOKEN_RPAREN || cur->kind == VIGIL_TOKEN_RBRACKET) && fmt_in_multiline_list(&ctx))
        {
            handled_multiline_close = true;
        }

        if (!handled_multiline_close)
            fmt_adjust_indent_before(&f, &ctx, cur->kind);

        if (prev_emitted_idx != SIZE_MAX && !had_comment)
        {
            f.prev_emitted_index = prev_emitted_idx;
            f.current_index = i;
            /* Before spacing for a closing delimiter in a multi-line list,
               insert trailing comma on the current line (before the newline). */
            if (fmt_in_multiline_list(&ctx) &&
                (cur->kind == VIGIL_TOKEN_RPAREN || cur->kind == VIGIL_TOKEN_RBRACKET ||
                 (cur->kind == VIGIL_TOKEN_RBRACE && brace_lit_at(&ctx, ctx.brace_depth))))
            {
                vigil_token_kind_t pk = vigil_token_list_get(tokens, prev_emitted_idx)->kind;
                if (pk != VIGIL_TOKEN_COMMA && pk != VIGIL_TOKEN_LBRACE && pk != VIGIL_TOKEN_LBRACKET &&
                    pk != VIGIL_TOKEN_LPAREN)
                {
                    buf_push(&f.out, ',');
                }
            }
            fmt_emit_spacing(&f, &ctx, vigil_token_list_get(tokens, prev_emitted_idx), cur);
        }

        /* Suppress semicolons except inside C-style for headers. */
        if (cur->kind == VIGIL_TOKEN_SEMICOLON && !ctx.in_for_header)
        {
            fmt_update_state_after(&f, &ctx, cur, i);
            prev_emitted_idx = i;
            continue;
        }

        /* ── Multi-line list tracking & trailing comma enforcement ── */

        /* Enter list on ( or [ */
        if (cur->kind == VIGIL_TOKEN_LPAREN || cur->kind == VIGIL_TOKEN_LBRACKET)
        {
            bool ml = false;
            vigil_token_kind_t close_kind = cur->kind == VIGIL_TOKEN_LPAREN ? VIGIL_TOKEN_RPAREN : VIGIL_TOKEN_RBRACKET;
            size_t ci = fmt_find_matching_close(&f, i, cur->kind, close_kind);
            if (ci != SIZE_MAX)
            {
                const vigil_token_t *ct = vigil_token_list_get(tokens, ci);
                ml = source_has_newline(&f, cur->span.end_offset, ct->span.start_offset);
            }
            if (ctx.list_depth < 32)
                ctx.list_multiline[ctx.list_depth] = ml;
            ctx.list_depth++;
            if (ml)
            {
                f.indent++;
                /* Emit the opening delimiter, then newline+indent for first element. */
                emit_str(&f, tok_text(&f, cur), tok_len(cur));
                fmt_update_state_after(&f, &ctx, cur, i);
                emit_newline(&f);
                prev_emitted_idx = i;
                continue;
            }
        }

        /* Strip trailing comma in single-line lists. */

        /* Enter list on literal { (detect via preceding non-semicolon token). */
        if (cur->kind == VIGIL_TOKEN_LBRACE)
        {
            bool is_lit = false;
            for (size_t k = i; k > 0; k--)
            {
                vigil_token_kind_t pk = vigil_token_list_get(tokens, k - 1)->kind;
                if (pk != VIGIL_TOKEN_SEMICOLON)
                {
                    is_lit = TOK_TEST(kLiteralBraceOpenerBits, pk);
                    break;
                }
            }
            if (is_lit)
            {
                bool ml = false;
                size_t ci = fmt_find_matching_close(&f, i, VIGIL_TOKEN_LBRACE, VIGIL_TOKEN_RBRACE);
                if (ci != SIZE_MAX)
                {
                    const vigil_token_t *ct = vigil_token_list_get(tokens, ci);
                    ml = source_has_newline(&f, cur->span.end_offset, ct->span.start_offset);
                }
                if (ctx.list_depth < 32)
                    ctx.list_multiline[ctx.list_depth] = ml;
                ctx.list_depth++;
                if (ml)
                {
                    f.indent++;
                    emit_str(&f, tok_text(&f, cur), tok_len(cur));
                    fmt_update_state_after(&f, &ctx, cur, i);
                    emit_newline(&f);
                    prev_emitted_idx = i;
                    continue;
                }
            }
        }
        /* Strip trailing comma in single-line lists. */
        if (cur->kind == VIGIL_TOKEN_COMMA)
        {
            size_t nxt = i + 1U;
            while (nxt < f.count && vigil_token_list_get(tokens, nxt)->kind == VIGIL_TOKEN_SEMICOLON)
                nxt++;
            if (nxt < f.count)
            {
                vigil_token_kind_t nk = vigil_token_list_get(tokens, nxt)->kind;
                if (nk == VIGIL_TOKEN_RPAREN || nk == VIGIL_TOKEN_RBRACKET || nk == VIGIL_TOKEN_RBRACE)
                {
                    if (!fmt_in_multiline_list(&ctx))
                    {
                        /* Single-line: suppress trailing comma. */
                        fmt_update_state_after(&f, &ctx, cur, i);
                        prev_emitted_idx = i;
                        continue;
                    }
                    /* Multi-line: keep the trailing comma (emit it). */
                }
            }
        }

        /* Before closing delimiter of a multi-line list:
           dedent and ensure we're on a fresh line. Skip normal spacing
           to avoid extra blank lines after lambda bodies. */
        if (cur->kind == VIGIL_TOKEN_RPAREN || cur->kind == VIGIL_TOKEN_RBRACKET ||
            (cur->kind == VIGIL_TOKEN_RBRACE && brace_lit_at(&ctx, ctx.brace_depth)))
        {
            if (fmt_in_multiline_list(&ctx))
            {
                f.indent--;
                if (!f.at_line_start)
                {
                    emit_newline(&f);
                }
                emit_str(&f, tok_text(&f, cur), tok_len(cur));
                fmt_update_state_after(&f, &ctx, cur, i);
                if (ctx.list_depth > 0)
                    ctx.list_depth--;
                prev_emitted_idx = i;
                continue;
            }
            if (ctx.list_depth > 0)
                ctx.list_depth--;
        }

        emit_str(&f, tok_text(&f, cur), tok_len(cur));
        fmt_update_state_after(&f, &ctx, cur, i);

        prev_emitted_idx = i;
    }

    fmt_finalize_output(&f);
    f.out.alloc.deallocate(f.out.alloc.user_data, imports);

    buf_push(&f.out, '\0');
    *out_text = f.out.data;
    *out_length = f.out.len - 1;
    return VIGIL_STATUS_OK;
}
