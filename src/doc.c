#include "vigil/doc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Allocation helpers ──────────────────────────────────────────── */

static void *doc_alloc(const vigil_allocator_t *a, size_t size)
{
    if (a != NULL && a->allocate != NULL)
        return a->allocate(a->user_data, size);
    return malloc(size);
}

static void *doc_realloc(const vigil_allocator_t *a, void *p, size_t size)
{
    if (a != NULL && a->reallocate != NULL)
        return a->reallocate(a->user_data, p, size);
    return realloc(p, size);
}

static void doc_free_ptr(const vigil_allocator_t *a, void *p)
{
    if (p == NULL)
        return;
    if (a != NULL && a->deallocate != NULL)
    {
        a->deallocate(a->user_data, p);
        return;
    }
    free(p);
}

static char *doc_strdup(const vigil_allocator_t *a, const char *s, size_t len)
{
    char *out;
    if (s == NULL || len == 0)
        return NULL;
    out = (char *)doc_alloc(a, len + 1);
    if (out == NULL)
        return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

/* ── String builder ──────────────────────────────────────────────── */

typedef struct doc_buf
{
    const vigil_allocator_t *allocator;
    char *data;
    size_t length;
    size_t capacity;
} doc_buf_t;

static void buf_init(doc_buf_t *b, const vigil_allocator_t *a)
{
    b->allocator = a;
    b->data = NULL;
    b->length = 0;
    b->capacity = 0;
}

static int buf_grow(doc_buf_t *b, size_t needed)
{
    size_t cap;
    char *p;
    if (b->length + needed + 1 <= b->capacity)
        return 1;
    cap = b->capacity == 0 ? 256 : b->capacity;
    while (cap < b->length + needed + 1)
        cap *= 2;
    p = (char *)doc_realloc(b->allocator, b->data, cap);
    if (p == NULL)
        return 0;
    b->data = p;
    b->capacity = cap;
    return 1;
}

static void buf_append(doc_buf_t *b, const char *s, size_t len)
{
    if (len == 0 || s == NULL)
        return;
    if (!buf_grow(b, len))
        return;
    if (b->data == NULL)
        return;
    memcpy(b->data + b->length, s, len);
    b->length += len;
    b->data[b->length] = '\0';
}

static void buf_append_cstr(doc_buf_t *b, const char *s)
{
    if (s != NULL)
        buf_append(b, s, strlen(s));
}

static void buf_append_char(doc_buf_t *b, char c)
{
    buf_append(b, &c, 1);
}

static void buf_append_indent(doc_buf_t *b, int spaces)
{
    int i;
    for (i = 0; i < spaces; i++)
        buf_append_char(b, ' ');
}

static void buf_free(doc_buf_t *b)
{
    doc_free_ptr(b->allocator, b->data);
    b->data = NULL;
    b->length = 0;
    b->capacity = 0;
}

/* ── Token helpers ───────────────────────────────────────────────── */

static const vigil_token_t *tok_at(const vigil_token_list_t *tokens, size_t i)
{
    return vigil_token_list_get(tokens, i);
}

static const char *tok_text(const char *src, const vigil_token_t *t, size_t *out_len)
{
    size_t len;
    if (t == NULL)
    {
        if (out_len)
            *out_len = 0;
        return NULL;
    }
    len = t->span.end_offset - t->span.start_offset;
    if (out_len)
        *out_len = len;
    return src + t->span.start_offset;
}

static int tok_is_ident_text(const char *src, const vigil_token_t *t, const char *text, size_t text_len)
{
    size_t len;
    const char *s;
    if (t == NULL || t->kind != VIGIL_TOKEN_IDENTIFIER)
        return 0;
    s = tok_text(src, t, &len);
    return len == text_len && memcmp(s, text, len) == 0;
}

static int tok_is_type_start(const vigil_token_t *t)
{
    if (t == NULL)
        return 0;
    return t->kind == VIGIL_TOKEN_IDENTIFIER || t->kind == VIGIL_TOKEN_FN;
}

/* ── Comment extraction ──────────────────────────────────────────── */

static size_t doc_find_line_start(const char *src, size_t pos)
{
    while (pos > 0 && src[pos - 1] != '\n')
        pos--;
    return pos;
}

static size_t doc_trim_line_end(const char *src, size_t line_start, size_t line_end)
{
    if (line_end > line_start && src[line_end - 1] == '\r')
        line_end--;
    return line_end;
}

static int doc_extract_comment_text(const char *src, size_t line_start, size_t line_end, const char **out_text,
                                    size_t *out_len)
{
    const char *text = src + line_start;
    size_t len = doc_trim_line_end(src, line_start, line_end) - line_start;

    while (len > 0 && (*text == ' ' || *text == '\t'))
    {
        text++;
        len--;
    }

    if (len < 2 || text[0] != '/' || text[1] != '/')
        return 0;

    text += 2;
    len -= 2;
    if (len > 0 && *text == ' ')
    {
        text++;
        len--;
    }

    *out_text = text;
    *out_len = len;
    return 1;
}

static void doc_prepend_comment_line(doc_buf_t *buf, const vigil_allocator_t *a, const char *text, size_t len,
                                     int *first)
{
    if (*first)
    {
        buf_append(buf, text, len);
        *first = 0;
        return;
    }

    {
        char *old = buf->data;
        size_t old_len = buf->length;
        doc_buf_t new_buf;

        buf_init(&new_buf, a);
        buf_append(&new_buf, text, len);
        buf_append_char(&new_buf, '\n');
        buf_append(&new_buf, old, old_len);
        buf_free(buf);
        *buf = new_buf;
    }
}

static void doc_append_comment_line(doc_buf_t *buf, const char *text, size_t len, int *found)
{
    if (*found)
        buf_append_char(buf, '\n');
    buf_append(buf, text, len);
    *found = 1;
}

static size_t doc_skip_leading_blank_lines(const char *src, size_t limit)
{
    size_t pos = 0;

    while (pos < limit && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\r' || src[pos] == '\n'))
        pos++;

    return pos;
}

static void extract_comment_before(const vigil_allocator_t *a, const char *src, size_t src_len, size_t decl_offset,
                                   vigil_doc_comment_t *out)
{
    size_t end;
    doc_buf_t buf;
    int first;

    (void)src_len;
    out->text = NULL;
    out->length = 0;
    if (decl_offset == 0 || src == NULL)
        return;

    /* Move to the end of the line immediately before the declaration line. */
    end = doc_find_line_start(src, decl_offset);
    /* Now end is at the start of the decl line (or 0). Skip the \n. */
    if (end > 0)
        end--; /* skip \n */
    end = doc_trim_line_end(src, 0, end);

    /* Walk backward collecting // comment lines. */
    buf_init(&buf, a);
    first = 1;

    while (end > 0)
    {
        size_t line_end = end;
        size_t line_start = doc_find_line_start(src, end);
        const char *text;
        size_t text_len;

        if (!doc_extract_comment_text(src, line_start, line_end, &text, &text_len))
            break;

        doc_prepend_comment_line(&buf, a, text, text_len, &first);

        /* Move to previous line. */
        if (line_start == 0)
            break;
        end = line_start - 1; /* skip \n before this line */
        end = doc_trim_line_end(src, 0, end);
    }

    if (buf.length > 0)
    {
        out->text = buf.data;
        out->length = buf.length;
    }
    else
    {
        buf_free(&buf);
    }
}

static int module_summary_starts_at(const char *src, size_t src_len, size_t first_decl_offset, size_t *out_pos)
{
    size_t pos;
    if (src == NULL || first_decl_offset == 0)
        return 0;
    pos = doc_skip_leading_blank_lines(src, first_decl_offset);
    if (pos >= first_decl_offset)
        return 0;
    if (src[pos] != '/')
        return 0;
    if (pos + 1 >= src_len || src[pos + 1] != '/')
        return 0;
    *out_pos = pos;
    return 1;
}

static void extract_module_summary(const vigil_allocator_t *a, const char *src, size_t src_len,
                                   size_t first_decl_offset, vigil_doc_comment_t *out)
{
    size_t pos;
    doc_buf_t buf;
    int found = 0;

    out->text = NULL;
    out->length = 0;
    if (!module_summary_starts_at(src, src_len, first_decl_offset, &pos))
        return;

    buf_init(&buf, a);

    while (pos < first_decl_offset)
    {
        size_t line_start = pos;
        size_t line_end = pos;
        const char *text;
        size_t text_len;

        while (line_end < src_len && src[line_end] != '\n')
            line_end++;

        if (!doc_extract_comment_text(src, line_start, line_end, &text, &text_len))
            break;

        doc_append_comment_line(&buf, text, text_len, &found);

        pos = line_end;
        if (pos < src_len && src[pos] == '\n')
            pos++;
    }

    if (buf.length > 0)
    {
        out->text = buf.data;
        out->length = buf.length;
    }
    else
    {
        buf_free(&buf);
    }
}

/* ── Type text extraction from tokens ────────────────────────────── */

static void extract_generic_args(const char *src, const vigil_token_list_t *tokens, size_t *cursor, doc_buf_t *buf)
{
    const vigil_token_t *t;
    size_t len;
    const char *text;
    int angle_depth;

    buf_append_char(buf, '<');
    (*cursor)++;
    angle_depth = 1;
    while (angle_depth > 0)
    {
        t = tok_at(tokens, *cursor);
        if (t == NULL || t->kind == VIGIL_TOKEN_EOF)
            break;
        if (t->kind == VIGIL_TOKEN_LESS)
        {
            angle_depth++;
            buf_append_char(buf, '<');
        }
        else if (t->kind == VIGIL_TOKEN_GREATER)
        {
            angle_depth--;
            buf_append_char(buf, '>');
        }
        else if (t->kind == VIGIL_TOKEN_SHIFT_RIGHT)
        {
            angle_depth -= 2;
            buf_append_cstr(buf, ">>");
        }
        else if (t->kind == VIGIL_TOKEN_COMMA)
        {
            buf_append_cstr(buf, ", ");
        }
        else
        {
            text = tok_text(src, t, &len);
            buf_append(buf, text, len);
        }
        (*cursor)++;
    }
}

static void extract_type_text(const char *src, const vigil_token_list_t *tokens, size_t *cursor, doc_buf_t *buf)
{
    /* Extracts a type expression: identifier, module.Type, array<T>, map<K,V>, fn(...) -> R */
    const vigil_token_t *t = tok_at(tokens, *cursor);
    size_t len;
    const char *text;

    if (t == NULL)
        return;

    text = tok_text(src, t, &len);
    buf_append(buf, text, len);
    (*cursor)++;

    /* Handle dot-qualified types: math.Vec3, module.Type */
    t = tok_at(tokens, *cursor);
    if (t != NULL && t->kind == VIGIL_TOKEN_DOT)
    {
        buf_append_char(buf, '.');
        (*cursor)++;
        t = tok_at(tokens, *cursor);
        if (t != NULL)
        {
            text = tok_text(src, t, &len);
            buf_append(buf, text, len);
            (*cursor)++;
        }
        t = tok_at(tokens, *cursor);
    }

    /* Handle generic types: array<T>, map<K,V> */
    if (t != NULL && t->kind == VIGIL_TOKEN_LESS)
    {
        extract_generic_args(src, tokens, cursor, buf);
    }
}

/* ── Parameter list extraction ───────────────────────────────────── */

static int tok_is_end_of_list(const vigil_token_list_t *tokens, size_t cursor, vigil_token_kind_t close_kind)
{
    const vigil_token_t *t = tok_at(tokens, cursor);
    return t == NULL || t->kind == VIGIL_TOKEN_EOF || t->kind == close_kind;
}

static vigil_status_t extract_params(const vigil_allocator_t *a, const char *src, const vigil_token_list_t *tokens,
                                     size_t *cursor, vigil_doc_param_t **out_params, size_t *out_count)
{
    const vigil_token_t *t;
    size_t cap = 4, count = 0;
    vigil_doc_param_t *params;

    *out_params = NULL;
    *out_count = 0;

    t = tok_at(tokens, *cursor);
    if (t == NULL || t->kind != VIGIL_TOKEN_LPAREN)
        return VIGIL_STATUS_OK;
    (*cursor)++;

    params = (vigil_doc_param_t *)doc_alloc(a, cap * sizeof(vigil_doc_param_t));
    if (params == NULL)
        return VIGIL_STATUS_OUT_OF_MEMORY;

    while (!tok_is_end_of_list(tokens, *cursor, VIGIL_TOKEN_RPAREN))
    {
        doc_buf_t type_buf;
        size_t name_len;
        const char *name_text;

        buf_init(&type_buf, a);
        extract_type_text(src, tokens, cursor, &type_buf);

        t = tok_at(tokens, *cursor);
        name_text = tok_text(src, t, &name_len);
        (*cursor)++;

        if (count >= cap)
        {
            cap *= 2;
            params = (vigil_doc_param_t *)doc_realloc(a, params, cap * sizeof(vigil_doc_param_t));
            if (params == NULL)
            {
                buf_free(&type_buf);
                return VIGIL_STATUS_OUT_OF_MEMORY;
            }
        }

        params[count].type_text = type_buf.data;
        params[count].type_length = type_buf.length;
        params[count].name = doc_strdup(a, name_text, name_len);
        params[count].name_length = name_len;
        count++;

        t = tok_at(tokens, *cursor);
        if (t != NULL && t->kind == VIGIL_TOKEN_COMMA)
            (*cursor)++;
    }

    /* Skip ) */
    t = tok_at(tokens, *cursor);
    if (t != NULL && t->kind == VIGIL_TOKEN_RPAREN)
        (*cursor)++;

    *out_params = params;
    *out_count = count;
    return VIGIL_STATUS_OK;
}

/* ── Return type extraction ──────────────────────────────────────── */

static void extract_tuple_return(const char *src, const vigil_token_list_t *tokens, size_t *cursor, doc_buf_t *buf)
{
    const vigil_token_t *t;
    int first = 1;
    buf_append_char(buf, '(');
    (*cursor)++;
    while (1)
    {
        t = tok_at(tokens, *cursor);
        if (tok_is_end_of_list(tokens, *cursor, VIGIL_TOKEN_RPAREN))
            break;
        if (!first)
            buf_append_cstr(buf, ", ");
        first = 0;
        extract_type_text(src, tokens, cursor, buf);
        t = tok_at(tokens, *cursor);
        if (t != NULL && t->kind == VIGIL_TOKEN_COMMA)
            (*cursor)++;
    }
    buf_append_char(buf, ')');
    t = tok_at(tokens, *cursor);
    if (t != NULL && t->kind == VIGIL_TOKEN_RPAREN)
        (*cursor)++;
}

static void extract_return_type(const vigil_allocator_t *a, const char *src, const vigil_token_list_t *tokens,
                                size_t *cursor, char **out_text, size_t *out_len)
{
    const vigil_token_t *t;
    doc_buf_t buf;

    *out_text = NULL;
    *out_len = 0;

    t = tok_at(tokens, *cursor);
    if (t == NULL || t->kind != VIGIL_TOKEN_ARROW)
        return;
    (*cursor)++;

    buf_init(&buf, a);

    t = tok_at(tokens, *cursor);
    if (t != NULL && t->kind == VIGIL_TOKEN_LPAREN)
    {
        extract_tuple_return(src, tokens, cursor, &buf);
    }
    else
    {
        extract_type_text(src, tokens, cursor, &buf);
    }

    *out_text = buf.data;
    *out_len = buf.length;
}

/* ── Skip brace-delimited body ───────────────────────────────────── */

static void skip_brace_body(const vigil_token_list_t *tokens, size_t *cursor)
{
    const vigil_token_t *t;
    int depth = 0;

    t = tok_at(tokens, *cursor);
    if (t == NULL || t->kind != VIGIL_TOKEN_LBRACE)
        return;
    depth = 1;
    (*cursor)++;

    while (depth > 0)
    {
        t = tok_at(tokens, *cursor);
        if (t == NULL || t->kind == VIGIL_TOKEN_EOF)
            break;
        if (t->kind == VIGIL_TOKEN_LBRACE)
            depth++;
        else if (t->kind == VIGIL_TOKEN_RBRACE)
            depth--;
        (*cursor)++;
    }
}

/* ── Module name from filename ───────────────────────────────────── */

static char *module_name_from_path(const vigil_allocator_t *a, const char *path, size_t path_len, size_t *out_len)
{
    const char *base, *dot;
    size_t base_len;

    /* Find last / or \ */
    base = path + path_len;
    while (base > path && *(base - 1) != '/' && *(base - 1) != '\\')
        base--;
    base_len = (size_t)((path + path_len) - base);

    /* Strip .vigil extension. */
    dot = base + base_len;
    while (dot > base && *(dot - 1) != '.')
        dot--;
    if (dot > base)
        base_len = (size_t)(dot - 1 - base);

    *out_len = base_len;
    return doc_strdup(a, base, base_len);
}

/* ── Add symbol to module ────────────────────────────────────────── */

static vigil_doc_symbol_t *module_add_symbol(vigil_doc_module_t *m)
{
    vigil_doc_symbol_t *s;
    if (m->symbol_count >= m->symbol_capacity)
    {
        size_t new_cap = m->symbol_capacity == 0 ? 8 : m->symbol_capacity * 2;
        vigil_doc_symbol_t *p =
            (vigil_doc_symbol_t *)doc_realloc(m->allocator, m->symbols, new_cap * sizeof(vigil_doc_symbol_t));
        if (p == NULL)
            return NULL;
        m->symbols = p;
        m->symbol_capacity = new_cap;
    }
    s = &m->symbols[m->symbol_count++];
    memset(s, 0, sizeof(*s));
    return s;
}

static void free_symbol(const vigil_allocator_t *a, vigil_doc_symbol_t *sym);

static void free_symbol_array(const vigil_allocator_t *a, vigil_doc_symbol_t *arr, size_t count)
{
    size_t i;
    if (arr == NULL)
        return;
    for (i = 0; i < count; i++)
        free_symbol(a, &arr[i]);
    doc_free_ptr(a, arr);
}

/* ── Parse function declaration ──────────────────────────────────── */

static vigil_status_t parse_function(const vigil_allocator_t *a, const char *src, size_t src_len,
                                     const vigil_token_list_t *tokens, size_t *cursor, vigil_doc_symbol_t *sym)
{
    const vigil_token_t *t;
    size_t name_len;
    const char *name_text;
    vigil_status_t status;

    (void)src_len;
    /* cursor is on 'fn' */
    (*cursor)++;

    /* Name */
    t = tok_at(tokens, *cursor);
    name_text = tok_text(src, t, &name_len);
    sym->kind = VIGIL_DOC_FUNCTION;
    sym->name = doc_strdup(a, name_text, name_len);
    sym->name_length = name_len;
    (*cursor)++;

    /* Parameters */
    status = extract_params(a, src, tokens, cursor, &sym->params, &sym->param_count);
    if (status != VIGIL_STATUS_OK)
        return status;

    /* Return type */
    extract_return_type(a, src, tokens, cursor, &sym->return_text, &sym->return_length);

    /* Skip body */
    skip_brace_body(tokens, cursor);

    return VIGIL_STATUS_OK;
}

/* ── Parse class declaration ─────────────────────────────────────── */

typedef struct
{
    const vigil_allocator_t *a;
    const char *src;
    size_t src_len;
    const vigil_token_list_t *tokens;
    size_t *cursor;
    vigil_doc_symbol_t *sym;
    size_t *field_cap;
    size_t *method_cap;
} class_parse_ctx_t;

static int class_has_implements_keyword(const char *src, const vigil_token_t *t)
{
    return tok_is_ident_text(src, t, "implements", 10);
}

static int parse_class_implement_name(const char *src, const vigil_token_list_t *tokens, size_t *cursor,
                                      doc_buf_t *impl_buf, int *first)
{
    const vigil_token_t *t = tok_at(tokens, *cursor);

    if (t == NULL || t->kind == VIGIL_TOKEN_LBRACE || t->kind == VIGIL_TOKEN_EOF)
        return 0;

    if (!*first)
        buf_append_cstr(impl_buf, ", ");
    *first = 0;

    const char *name_text;
    size_t name_len;
    name_text = tok_text(src, t, &name_len);
    buf_append(impl_buf, name_text, name_len);
    (*cursor)++;

    t = tok_at(tokens, *cursor);
    if (t != NULL && t->kind == VIGIL_TOKEN_COMMA)
        (*cursor)++;

    return 1;
}

static void parse_class_implements_clause(const vigil_allocator_t *a, const char *src, const vigil_token_list_t *tokens,
                                          size_t *cursor, vigil_doc_symbol_t *sym)
{
    const vigil_token_t *t = tok_at(tokens, *cursor);
    doc_buf_t impl_buf;
    int first = 1;

    if (!class_has_implements_keyword(src, t))
        return;

    buf_init(&impl_buf, a);
    (*cursor)++;
    while (parse_class_implement_name(src, tokens, cursor, &impl_buf, &first))
        ;

    sym->implements_text = impl_buf.data;
    sym->implements_length = impl_buf.length;
}

static vigil_doc_symbol_t *grow_symbol_array(const vigil_allocator_t *a, vigil_doc_symbol_t *symbols, size_t *capacity)
{
    *capacity *= 2;
    return (vigil_doc_symbol_t *)doc_realloc(a, symbols, *capacity * sizeof(vigil_doc_symbol_t));
}

static void discard_class_member(const vigil_allocator_t *a, vigil_doc_symbol_t *member)
{
    free_symbol(a, member);
}

static void skip_class_field_tail(const vigil_token_list_t *tokens, size_t *cursor)
{
    const vigil_token_t *t;

    while (1)
    {
        t = tok_at(tokens, *cursor);
        if (tok_is_end_of_list(tokens, *cursor, VIGIL_TOKEN_SEMICOLON))
            break;
        (*cursor)++;
    }
    if (t != NULL && t->kind == VIGIL_TOKEN_SEMICOLON)
        (*cursor)++;
}

static void parse_class_method_member(const class_parse_ctx_t *ctx, int is_pub)
{
    vigil_doc_symbol_t member;
    const vigil_token_t *t = tok_at(ctx->tokens, *ctx->cursor);

    memset(&member, 0, sizeof(member));
    extract_comment_before(ctx->a, ctx->src, ctx->src_len, t->span.start_offset, &member.comment);
    parse_function(ctx->a, ctx->src, ctx->src_len, ctx->tokens, ctx->cursor, &member);

    if (!is_pub)
    {
        discard_class_member(ctx->a, &member);
        return;
    }

    size_t *method_cap = ctx->method_cap;
    if (ctx->sym->method_count >= *method_cap)
        ctx->sym->methods = grow_symbol_array(ctx->a, ctx->sym->methods, method_cap);
    ctx->sym->methods[ctx->sym->method_count++] = member;
}

static void parse_field_type(const class_parse_ctx_t *ctx, doc_buf_t *type_buf)
{
    buf_init(type_buf, ctx->a);
    extract_type_text(ctx->src, ctx->tokens, ctx->cursor, type_buf);
}

static void parse_field_name(const class_parse_ctx_t *ctx, const char **fname_text, size_t *fname_len)
{
    const vigil_token_t *t = tok_at(ctx->tokens, *ctx->cursor);

    *fname_text = tok_text(ctx->src, t, fname_len);
    (*ctx->cursor)++;
}

static void store_class_field_symbol(const class_parse_ctx_t *ctx, size_t field_start, const char *fname_text,
                                     size_t fname_len, doc_buf_t *type_buf)
{
    vigil_doc_symbol_t member;

    memset(&member, 0, sizeof(member));
    extract_comment_before(ctx->a, ctx->src, ctx->src_len, field_start, &member.comment);
    member.kind = VIGIL_DOC_VARIABLE;
    member.name = doc_strdup(ctx->a, fname_text, fname_len);
    member.name_length = fname_len;
    member.type_text = type_buf->data;
    member.type_length = type_buf->length;
    size_t *field_cap = ctx->field_cap;
    if (ctx->sym->field_count >= *field_cap)
        ctx->sym->fields = grow_symbol_array(ctx->a, ctx->sym->fields, field_cap);
    ctx->sym->fields[ctx->sym->field_count++] = member;
}
static void parse_class_field_member(const class_parse_ctx_t *ctx, int is_pub)
{
    doc_buf_t type_buf;
    const vigil_token_t *t = tok_at(ctx->tokens, *ctx->cursor);
    size_t field_start = t->span.start_offset;
    size_t fname_len;
    const char *fname_text;

    parse_field_type(ctx, &type_buf);
    parse_field_name(ctx, &fname_text, &fname_len);
    skip_class_field_tail(ctx->tokens, ctx->cursor);

    if (!is_pub)
    {
        buf_free(&type_buf);
        return;
    }

    store_class_field_symbol(ctx, field_start, fname_text, fname_len, &type_buf);
}

static const vigil_token_t *consume_public_modifier(class_parse_ctx_t *ctx, int *is_pub)
{
    const vigil_token_t *t = tok_at(ctx->tokens, *ctx->cursor);

    if (t == NULL)
        return t;

    if (t->kind == VIGIL_TOKEN_PUB)
    {
        *is_pub = 1;
        (*ctx->cursor)++;
        t = tok_at(ctx->tokens, *ctx->cursor);
    }

    return t;
}

static int handle_class_member(class_parse_ctx_t *ctx, const vigil_token_t *t, int is_pub)
{
    if (t->kind == VIGIL_TOKEN_FN)
    {
        parse_class_method_member(ctx, is_pub);
        return 1;
    }

    if (tok_is_type_start(t))
    {
        parse_class_field_member(ctx, is_pub);
        return 1;
    }

    (*ctx->cursor)++;
    return 1;
}

static int parse_class_body_member(class_parse_ctx_t *ctx)
{
    const vigil_token_t *t = tok_at(ctx->tokens, *ctx->cursor);
    int is_pub = 0;

    if (tok_is_end_of_list(ctx->tokens, *ctx->cursor, VIGIL_TOKEN_RBRACE))
        return 0;

    t = consume_public_modifier(ctx, &is_pub);
    if (tok_is_end_of_list(ctx->tokens, *ctx->cursor, VIGIL_TOKEN_RBRACE))
        return 0;

    return handle_class_member(ctx, t, is_pub);
}

static void parse_class_body(class_parse_ctx_t *ctx)
{
    while (parse_class_body_member(ctx))
        ;
}

static void skip_class_close_brace(const vigil_token_list_t *tokens, size_t *cursor)
{
    const vigil_token_t *t = tok_at(tokens, *cursor);

    if (t != NULL && t->kind == VIGIL_TOKEN_RBRACE)
        (*cursor)++;
}

static int prepare_class_body(class_parse_ctx_t *ctx)
{
    const vigil_token_t *t;
    size_t name_len;
    const char *name_text;

    /* cursor is on 'class' */
    (*ctx->cursor)++;

    /* Name */
    t = tok_at(ctx->tokens, *ctx->cursor);
    name_text = tok_text(ctx->src, t, &name_len);
    ctx->sym->kind = VIGIL_DOC_CLASS;
    ctx->sym->name = doc_strdup(ctx->a, name_text, name_len);
    ctx->sym->name_length = name_len;
    (*ctx->cursor)++;

    parse_class_implements_clause(ctx->a, ctx->src, ctx->tokens, ctx->cursor, ctx->sym);

    /* Allocate fields and methods arrays. */
    ctx->sym->fields = (vigil_doc_symbol_t *)doc_alloc(ctx->a, (*ctx->field_cap) * sizeof(vigil_doc_symbol_t));
    ctx->sym->methods = (vigil_doc_symbol_t *)doc_alloc(ctx->a, (*ctx->method_cap) * sizeof(vigil_doc_symbol_t));
    ctx->sym->field_count = 0;
    ctx->sym->method_count = 0;

    t = tok_at(ctx->tokens, *ctx->cursor);
    if (t == NULL || t->kind != VIGIL_TOKEN_LBRACE)
        return 0;
    (*ctx->cursor)++;
    return 1;
}

static vigil_status_t parse_class(const vigil_allocator_t *a, const char *src, size_t src_len,
                                  const vigil_token_list_t *tokens, size_t *cursor, vigil_doc_symbol_t *sym)
{
    class_parse_ctx_t ctx;
    size_t field_cap = 4, method_cap = 4;

    ctx.a = a;
    ctx.src = src;
    ctx.src_len = src_len;
    ctx.tokens = tokens;
    ctx.cursor = cursor;
    ctx.sym = sym;
    ctx.field_cap = &field_cap;
    ctx.method_cap = &method_cap;

    if (!prepare_class_body(&ctx))
        return VIGIL_STATUS_OK;

    parse_class_body(&ctx);
    skip_class_close_brace(tokens, cursor);

    return VIGIL_STATUS_OK;
}

/* ── Parse interface declaration ─────────────────────────────────── */

static void parse_interface_method(const vigil_allocator_t *a, const char *src, size_t src_len,
                                   const vigil_token_list_t *tokens, size_t *cursor, vigil_doc_symbol_t *method)
{
    const vigil_token_t *t;
    size_t name_len;
    const char *name_text;

    t = tok_at(tokens, *cursor);
    memset(method, 0, sizeof(*method));
    extract_comment_before(a, src, src_len, t->span.start_offset, &method->comment);
    (*cursor)++; /* skip fn */

    t = tok_at(tokens, *cursor);
    name_text = tok_text(src, t, &name_len);
    method->kind = VIGIL_DOC_FUNCTION;
    method->name = doc_strdup(a, name_text, name_len);
    method->name_length = name_len;
    (*cursor)++;

    extract_params(a, src, tokens, cursor, &method->params, &method->param_count);
    extract_return_type(a, src, tokens, cursor, &method->return_text, &method->return_length);

    t = tok_at(tokens, *cursor);
    if (t != NULL && t->kind == VIGIL_TOKEN_SEMICOLON)
        (*cursor)++;
}

static vigil_status_t parse_interface(const vigil_allocator_t *a, const char *src, size_t src_len,
                                      const vigil_token_list_t *tokens, size_t *cursor, vigil_doc_symbol_t *sym)
{
    const vigil_token_t *t;
    size_t name_len;
    const char *name_text;
    size_t method_cap = 4;

    (void)src_len;
    /* cursor is on 'interface' */
    (*cursor)++;

    /* Name */
    t = tok_at(tokens, *cursor);
    name_text = tok_text(src, t, &name_len);
    sym->kind = VIGIL_DOC_INTERFACE;
    sym->name = doc_strdup(a, name_text, name_len);
    sym->name_length = name_len;
    (*cursor)++;

    sym->iface_methods = (vigil_doc_symbol_t *)doc_alloc(a, method_cap * sizeof(vigil_doc_symbol_t));
    sym->iface_method_count = 0;

    /* Open brace */
    t = tok_at(tokens, *cursor);
    if (t == NULL || t->kind != VIGIL_TOKEN_LBRACE)
        return VIGIL_STATUS_OK;
    (*cursor)++;

    while (1)
    {
        t = tok_at(tokens, *cursor);
        if (tok_is_end_of_list(tokens, *cursor, VIGIL_TOKEN_RBRACE))
            break;

        if (t->kind == VIGIL_TOKEN_FN)
        {
            vigil_doc_symbol_t method;
            parse_interface_method(a, src, src_len, tokens, cursor, &method);

            if (sym->iface_method_count >= method_cap)
            {
                method_cap *= 2;
                sym->iface_methods =
                    (vigil_doc_symbol_t *)doc_realloc(a, sym->iface_methods, method_cap * sizeof(vigil_doc_symbol_t));
            }
            sym->iface_methods[sym->iface_method_count++] = method;
        }
        else
        {
            (*cursor)++;
        }
    }

    t = tok_at(tokens, *cursor);
    if (t != NULL && t->kind == VIGIL_TOKEN_RBRACE)
        (*cursor)++;

    return VIGIL_STATUS_OK;
}

/* ── Parse enum declaration ──────────────────────────────────────── */

static vigil_status_t parse_enum(const vigil_allocator_t *a, const char *src, size_t src_len,
                                 const vigil_token_list_t *tokens, size_t *cursor, vigil_doc_symbol_t *sym)
{
    const vigil_token_t *t;
    size_t name_len;
    const char *name_text;
    size_t var_cap = 8;

    (void)src_len;
    /* cursor is on 'enum' */
    (*cursor)++;

    t = tok_at(tokens, *cursor);
    name_text = tok_text(src, t, &name_len);
    sym->kind = VIGIL_DOC_ENUM;
    sym->name = doc_strdup(a, name_text, name_len);
    sym->name_length = name_len;
    (*cursor)++;

    sym->variant_names = (char **)doc_alloc(a, var_cap * sizeof(char *));
    sym->variant_name_lengths = (size_t *)doc_alloc(a, var_cap * sizeof(size_t));
    sym->variant_count = 0;

    t = tok_at(tokens, *cursor);
    if (t == NULL || t->kind != VIGIL_TOKEN_LBRACE)
        return VIGIL_STATUS_OK;
    (*cursor)++;

    while (1)
    {
        t = tok_at(tokens, *cursor);
        if (tok_is_end_of_list(tokens, *cursor, VIGIL_TOKEN_RBRACE))
            break;

        if (t->kind == VIGIL_TOKEN_IDENTIFIER)
        {
            name_text = tok_text(src, t, &name_len);
            if (sym->variant_count >= var_cap)
            {
                var_cap *= 2;
                sym->variant_names = (char **)doc_realloc(a, sym->variant_names, var_cap * sizeof(char *));
                sym->variant_name_lengths =
                    (size_t *)doc_realloc(a, sym->variant_name_lengths, var_cap * sizeof(size_t));
            }
            sym->variant_names[sym->variant_count] = doc_strdup(a, name_text, name_len);
            sym->variant_name_lengths[sym->variant_count] = name_len;
            sym->variant_count++;
        }
        (*cursor)++;
    }

    t = tok_at(tokens, *cursor);
    if (t != NULL && t->kind == VIGIL_TOKEN_RBRACE)
        (*cursor)++;

    return VIGIL_STATUS_OK;
}

/* ── Parse constant declaration ──────────────────────────────────── */

static void parse_const_or_var(const vigil_allocator_t *a, const char *src, const vigil_token_list_t *tokens,
                               size_t *cursor, vigil_doc_symbol_t *sym, int is_const)
{
    const vigil_token_t *t;
    doc_buf_t type_buf;
    size_t name_len;
    const char *name_text;

    if (is_const)
        (*cursor)++; /* skip 'const' */

    /* Type */
    buf_init(&type_buf, a);
    extract_type_text(src, tokens, cursor, &type_buf);

    /* Name */
    t = tok_at(tokens, *cursor);
    name_text = tok_text(src, t, &name_len);
    (*cursor)++;

    sym->kind = is_const ? VIGIL_DOC_CONSTANT : VIGIL_DOC_VARIABLE;
    sym->name = doc_strdup(a, name_text, name_len);
    sym->name_length = name_len;
    sym->type_text = type_buf.data;
    sym->type_length = type_buf.length;

    /* Skip to semicolon. */
    while (1)
    {
        t = tok_at(tokens, *cursor);
        if (tok_is_end_of_list(tokens, *cursor, VIGIL_TOKEN_SEMICOLON))
            break;
        (*cursor)++;
    }
    if (t != NULL && t->kind == VIGIL_TOKEN_SEMICOLON)
        (*cursor)++;
}

/* ── vigil_doc_extract ────────────────────────────────────────────── */

typedef enum
{
    DOC_DECL_OTHER = 0,
    DOC_DECL_IMPORT,
    DOC_DECL_FUNCTION,
    DOC_DECL_CLASS,
    DOC_DECL_INTERFACE,
    DOC_DECL_ENUM,
    DOC_DECL_CONST,
    DOC_DECL_VAR
} doc_decl_kind_t;

typedef struct
{
    const vigil_allocator_t *allocator;
    const char *source_text;
    size_t source_length;
    const vigil_token_list_t *tokens;
    vigil_doc_module_t *module;
} doc_extract_ctx_t;

static int is_module_decl_start(const vigil_token_t *t)
{
    return t != NULL && (t->kind == VIGIL_TOKEN_PUB || t->kind == VIGIL_TOKEN_FN || t->kind == VIGIL_TOKEN_CLASS ||
                         t->kind == VIGIL_TOKEN_INTERFACE || t->kind == VIGIL_TOKEN_ENUM ||
                         t->kind == VIGIL_TOKEN_CONST || t->kind == VIGIL_TOKEN_IMPORT || tok_is_type_start(t));
}

static size_t find_module_first_decl_offset(const vigil_token_list_t *tokens)
{
    size_t scan = 0;
    int depth = 0;
    const vigil_token_t *t;

    while (1)
    {
        t = tok_at(tokens, scan);
        if (t == NULL || t->kind == VIGIL_TOKEN_EOF)
            break;
        if (t->kind == VIGIL_TOKEN_LBRACE)
        {
            depth++;
            scan++;
            continue;
        }
        if (t->kind == VIGIL_TOKEN_RBRACE)
        {
            if (depth > 0)
                depth--;
            scan++;
            continue;
        }
        if (depth == 0 && is_module_decl_start(t))
            return t->span.start_offset;
        scan++;
    }

    return (size_t)-1;
}

static int module_contains_main(const char *source_text, const vigil_token_list_t *tokens)
{
    size_t scan = 0;

    while (1)
    {
        const vigil_token_t *t = tok_at(tokens, scan);
        if (t == NULL || t->kind == VIGIL_TOKEN_EOF)
            return 0;
        if (t->kind == VIGIL_TOKEN_FN)
        {
            const vigil_token_t *name = tok_at(tokens, scan + 1);
            if (name != NULL && name->kind == VIGIL_TOKEN_IDENTIFIER &&
                name->span.end_offset - name->span.start_offset == 4 &&
                memcmp(source_text + name->span.start_offset, "main", 4) == 0)
            {
                return 1;
            }
        }
        scan++;
    }
}

static void skip_to_semicolon(const vigil_token_list_t *tokens, size_t *cursor)
{
    const vigil_token_t *t;

    while (1)
    {
        t = tok_at(tokens, *cursor);
        if (tok_is_end_of_list(tokens, *cursor, VIGIL_TOKEN_SEMICOLON))
            break;
        (*cursor)++;
    }
    if (t != NULL && t->kind == VIGIL_TOKEN_SEMICOLON)
        (*cursor)++;
}

static void skip_named_block_decl(const vigil_token_list_t *tokens, size_t *cursor)
{
    (*cursor)++;
    (*cursor)++;
    while (1)
    {
        if (tok_is_end_of_list(tokens, *cursor, VIGIL_TOKEN_LBRACE))
            break;
        (*cursor)++;
    }
    skip_brace_body(tokens, cursor);
}

static void skip_function_params(const vigil_token_list_t *tokens, size_t *cursor)
{
    const vigil_token_t *t;

    t = tok_at(tokens, *cursor);
    if (t != NULL && t->kind == VIGIL_TOKEN_LPAREN)
    {
        int depth = 1;
        (*cursor)++;
        while (depth > 0)
        {
            t = tok_at(tokens, *cursor);
            if (t == NULL || t->kind == VIGIL_TOKEN_EOF)
                break;
            if (t->kind == VIGIL_TOKEN_LPAREN)
                depth++;
            else if (t->kind == VIGIL_TOKEN_RPAREN)
                depth--;
            (*cursor)++;
        }
    }
}

static void skip_function_return_type(const vigil_token_list_t *tokens, size_t *cursor)
{
    const vigil_token_t *t = tok_at(tokens, *cursor);

    if (t == NULL || t->kind != VIGIL_TOKEN_ARROW)
        return;
    (*cursor)++;
    while (1)
    {
        t = tok_at(tokens, *cursor);
        if (tok_is_end_of_list(tokens, *cursor, VIGIL_TOKEN_LBRACE))
            break;
        (*cursor)++;
    }
}

static void skip_private_function_decl(const vigil_token_list_t *tokens, size_t *cursor)
{
    (*cursor)++;
    (*cursor)++;
    skip_function_params(tokens, cursor);
    skip_function_return_type(tokens, cursor);
    skip_brace_body(tokens, cursor);
}

static doc_decl_kind_t classify_module_decl(const vigil_token_t *t)
{
    if (t == NULL)
        return DOC_DECL_OTHER;
    if (t->kind == VIGIL_TOKEN_IMPORT)
        return DOC_DECL_IMPORT;
    if (t->kind == VIGIL_TOKEN_FN)
        return DOC_DECL_FUNCTION;
    if (t->kind == VIGIL_TOKEN_CLASS)
        return DOC_DECL_CLASS;
    if (t->kind == VIGIL_TOKEN_INTERFACE)
        return DOC_DECL_INTERFACE;
    if (t->kind == VIGIL_TOKEN_ENUM)
        return DOC_DECL_ENUM;
    if (t->kind == VIGIL_TOKEN_CONST)
        return DOC_DECL_CONST;
    if (tok_is_type_start(t))
        return DOC_DECL_VAR;
    return DOC_DECL_OTHER;
}

static void skip_hidden_decl(doc_decl_kind_t kind, const vigil_token_list_t *tokens, size_t *cursor)
{
    switch (kind)
    {
    case DOC_DECL_IMPORT:
    case DOC_DECL_CONST:
    case DOC_DECL_VAR:
        skip_to_semicolon(tokens, cursor);
        break;
    case DOC_DECL_FUNCTION:
        skip_private_function_decl(tokens, cursor);
        break;
    case DOC_DECL_CLASS:
    case DOC_DECL_INTERFACE:
    case DOC_DECL_ENUM:
        skip_named_block_decl(tokens, cursor);
        break;
    case DOC_DECL_OTHER:
    default:
        (*cursor)++;
        break;
    }
}

static vigil_doc_symbol_t *begin_visible_decl(const doc_extract_ctx_t *ctx, size_t decl_start)
{
    vigil_doc_symbol_t *sym = module_add_symbol(ctx->module);
    if (sym == NULL)
        return NULL;
    extract_comment_before(ctx->allocator, ctx->source_text, ctx->source_length, decl_start, &sym->comment);
    return sym;
}

static vigil_status_t parse_visible_decl(const doc_extract_ctx_t *ctx, doc_decl_kind_t kind, size_t decl_start,
                                         size_t *cursor)
{
    vigil_doc_symbol_t *sym = begin_visible_decl(ctx, decl_start);
    if (sym == NULL)
        return VIGIL_STATUS_OUT_OF_MEMORY;

    switch (kind)
    {
    case DOC_DECL_FUNCTION:
        return parse_function(ctx->allocator, ctx->source_text, ctx->source_length, ctx->tokens, cursor, sym);
    case DOC_DECL_CLASS:
        return parse_class(ctx->allocator, ctx->source_text, ctx->source_length, ctx->tokens, cursor, sym);
    case DOC_DECL_INTERFACE:
        return parse_interface(ctx->allocator, ctx->source_text, ctx->source_length, ctx->tokens, cursor, sym);
    case DOC_DECL_ENUM:
        return parse_enum(ctx->allocator, ctx->source_text, ctx->source_length, ctx->tokens, cursor, sym);
    case DOC_DECL_CONST:
        parse_const_or_var(ctx->allocator, ctx->source_text, ctx->tokens, cursor, sym, 1);
        return VIGIL_STATUS_OK;
    case DOC_DECL_VAR:
        parse_const_or_var(ctx->allocator, ctx->source_text, ctx->tokens, cursor, sym, 0);
        return VIGIL_STATUS_OK;
    case DOC_DECL_IMPORT:
    case DOC_DECL_OTHER:
    default:
        return VIGIL_STATUS_OK;
    }
}

static vigil_status_t handle_module_decl(const doc_extract_ctx_t *ctx, size_t *cursor, int is_script)
{
    const vigil_token_t *t = tok_at(ctx->tokens, *cursor);
    size_t decl_start;
    doc_decl_kind_t kind;
    int is_pub = 0;

    if (t == NULL || t->kind == VIGIL_TOKEN_EOF)
        return VIGIL_STATUS_OK;

    decl_start = t->span.start_offset;
    if (t->kind == VIGIL_TOKEN_PUB)
    {
        is_pub = 1;
        (*cursor)++;
        t = tok_at(ctx->tokens, *cursor);
    }

    kind = classify_module_decl(t);
    if (kind == DOC_DECL_IMPORT)
    {
        skip_to_semicolon(ctx->tokens, cursor);
        return VIGIL_STATUS_OK;
    }
    if (kind == DOC_DECL_OTHER)
    {
        (*cursor)++;
        return VIGIL_STATUS_OK;
    }
    if (is_pub || is_script)
        return parse_visible_decl(ctx, kind, decl_start, cursor);

    skip_hidden_decl(kind, ctx->tokens, cursor);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_doc_extract(const vigil_allocator_t *allocator, const char *filename, size_t filename_length,
                                 const char *source_text, size_t source_length, const vigil_token_list_t *tokens,
                                 vigil_doc_module_t *out_module, vigil_error_t *error)
{
    doc_extract_ctx_t ctx;
    size_t cursor = 0;
    size_t first_decl_offset;
    const vigil_token_t *t;
    int is_script;
    vigil_status_t status;

    (void)error;
    if (out_module == NULL)
        return VIGIL_STATUS_INVALID_ARGUMENT;
    memset(out_module, 0, sizeof(*out_module));
    out_module->allocator = allocator;

    out_module->name = module_name_from_path(allocator, filename, filename_length, &out_module->name_length);
    ctx.allocator = allocator;
    ctx.source_text = source_text;
    ctx.source_length = source_length;
    ctx.tokens = tokens;
    ctx.module = out_module;

    first_decl_offset = find_module_first_decl_offset(tokens);

    if (first_decl_offset != (size_t)-1)
    {
        extract_module_summary(allocator, source_text, source_length, first_decl_offset, &out_module->summary);
    }

    is_script = module_contains_main(source_text, tokens);
    cursor = 0;
    while (1)
    {
        t = tok_at(tokens, cursor);
        if (t == NULL || t->kind == VIGIL_TOKEN_EOF)
            break;
        status = handle_module_decl(&ctx, &cursor, is_script);
        if (status != VIGIL_STATUS_OK)
            return status;
    }

    return VIGIL_STATUS_OK;
}

/* ── Render helpers ──────────────────────────────────────────────── */

static void render_func_sig(doc_buf_t *b, const vigil_doc_symbol_t *sym)
{
    size_t i;
    buf_append(b, sym->name, sym->name_length);
    buf_append_char(b, '(');
    for (i = 0; i < sym->param_count; i++)
    {
        if (i > 0)
            buf_append_cstr(b, ", ");
        buf_append(b, sym->params[i].type_text, sym->params[i].type_length);
        buf_append_char(b, ' ');
        buf_append(b, sym->params[i].name, sym->params[i].name_length);
    }
    buf_append_char(b, ')');
    if (sym->return_text != NULL && sym->return_length > 0)
    {
        buf_append_cstr(b, " -> ");
        buf_append(b, sym->return_text, sym->return_length);
    }
}

static void render_indent_comment(doc_buf_t *b, const vigil_doc_comment_t *c, int indent)
{
    size_t pos = 0;
    if (c->text == NULL || c->length == 0)
        return;
    while (pos < c->length)
    {
        size_t line_start = pos;
        while (pos < c->length && c->text[pos] != '\n')
            pos++;
        buf_append_indent(b, indent);
        buf_append(b, c->text + line_start, pos - line_start);
        buf_append_char(b, '\n');
        if (pos < c->length)
            pos++; /* skip \n */
    }
}

static void render_symbol_comment_block(doc_buf_t *b, const vigil_doc_comment_t *comment, int indent)
{
    if (comment->text == NULL)
        return;
    buf_append_char(b, '\n');
    render_indent_comment(b, comment, indent);
}

static void render_named_typed_symbol(doc_buf_t *b, const vigil_doc_symbol_t *sym)
{
    buf_append(b, sym->name, sym->name_length);
    buf_append_char(b, ' ');
    buf_append(b, sym->type_text, sym->type_length);
    buf_append_char(b, '\n');
}

static void render_enum_variants(doc_buf_t *b, const vigil_doc_symbol_t *sym)
{
    size_t j;

    if (sym->variant_count == 0)
        return;

    buf_append_cstr(b, "\n  Variants\n");
    for (j = 0; j < sym->variant_count; j++)
    {
        buf_append_cstr(b, "    ");
        buf_append(b, sym->variant_names[j], sym->variant_name_lengths[j]);
        buf_append_char(b, '\n');
    }
}

static void render_interface_methods(doc_buf_t *b, const vigil_doc_symbol_t *sym)
{
    size_t j;

    if (sym->iface_method_count == 0)
        return;

    buf_append_cstr(b, "\n  Methods\n");
    for (j = 0; j < sym->iface_method_count; j++)
    {
        buf_append_cstr(b, "    ");
        render_func_sig(b, &sym->iface_methods[j]);
        buf_append_char(b, '\n');
        if (sym->iface_methods[j].comment.text != NULL)
            render_indent_comment(b, &sym->iface_methods[j].comment, 6);
    }
}

static void render_class_fields(doc_buf_t *b, const vigil_doc_symbol_t *sym)
{
    size_t j;

    if (sym->field_count == 0)
        return;

    buf_append_cstr(b, "\n  Fields\n");
    for (j = 0; j < sym->field_count; j++)
    {
        buf_append_cstr(b, "    ");
        render_named_typed_symbol(b, &sym->fields[j]);
        if (sym->fields[j].comment.text != NULL)
            render_indent_comment(b, &sym->fields[j].comment, 6);
    }
}

static void render_class_methods(doc_buf_t *b, const vigil_doc_symbol_t *sym)
{
    size_t j;

    if (sym->method_count == 0)
        return;

    buf_append_cstr(b, "\n  Methods\n");
    for (j = 0; j < sym->method_count; j++)
    {
        buf_append_cstr(b, "    ");
        render_func_sig(b, &sym->methods[j]);
        buf_append_char(b, '\n');
        if (sym->methods[j].comment.text != NULL)
            render_indent_comment(b, &sym->methods[j].comment, 6);
    }
}

static void render_enum_detail(doc_buf_t *b, const vigil_doc_symbol_t *sym)
{
    buf_append(b, sym->name, sym->name_length);
    buf_append_char(b, '\n');
    render_symbol_comment_block(b, &sym->comment, 2);
    render_enum_variants(b, sym);
}

static void render_interface_detail(doc_buf_t *b, const vigil_doc_symbol_t *sym)
{
    buf_append(b, sym->name, sym->name_length);
    buf_append_char(b, '\n');
    render_symbol_comment_block(b, &sym->comment, 2);
    render_interface_methods(b, sym);
}

static void render_class_detail(doc_buf_t *b, const vigil_doc_symbol_t *sym)
{
    buf_append(b, sym->name, sym->name_length);
    if (sym->implements_text != NULL)
    {
        buf_append_cstr(b, " implements ");
        buf_append(b, sym->implements_text, sym->implements_length);
    }
    buf_append_char(b, '\n');
    render_symbol_comment_block(b, &sym->comment, 2);
    render_class_fields(b, sym);
    render_class_methods(b, sym);
}

static void render_class_name_line(doc_buf_t *b, const vigil_doc_symbol_t *sym)
{
    buf_append(b, sym->name, sym->name_length);
    if (sym->implements_text != NULL)
    {
        buf_append_cstr(b, " implements ");
        buf_append(b, sym->implements_text, sym->implements_length);
    }
    buf_append_char(b, '\n');
}

/* ── Render module view ──────────────────────────────────────────── */

static void render_module_header(doc_buf_t *b, const vigil_doc_module_t *m)
{
    buf_append_cstr(b, "MODULE\n  ");
    buf_append(b, m->name, m->name_length);
    buf_append_char(b, '\n');
}

static void render_module_summary_block(doc_buf_t *b, const vigil_doc_module_t *m)
{
    if (m->summary.text == NULL || m->summary.length == 0)
        return;

    buf_append_cstr(b, "\nSUMMARY\n");
    render_indent_comment(b, &m->summary, 2);
}

static void render_module_typed_section(doc_buf_t *b, const vigil_doc_module_t *m, vigil_doc_kind_t kind,
                                        const char *title)
{
    size_t i;
    int has_section = 0;

    for (i = 0; i < m->symbol_count; i++)
    {
        const vigil_doc_symbol_t *sym = &m->symbols[i];

        if (sym->kind != kind)
            continue;
        if (!has_section)
        {
            buf_append_cstr(b, "\n");
            buf_append_cstr(b, title);
            buf_append_char(b, '\n');
            has_section = 1;
        }
        buf_append_cstr(b, "  ");
        render_named_typed_symbol(b, sym);
        if (sym->comment.text != NULL)
            render_indent_comment(b, &sym->comment, 4);
    }
}

static void render_module_enum_section(doc_buf_t *b, const vigil_doc_module_t *m)
{
    size_t i, j;
    int has_section = 0;

    for (i = 0; i < m->symbol_count; i++)
    {
        const vigil_doc_symbol_t *sym = &m->symbols[i];

        if (sym->kind != VIGIL_DOC_ENUM)
            continue;
        if (!has_section)
        {
            buf_append_cstr(b, "\nENUMS\n");
            has_section = 1;
        }
        buf_append_cstr(b, "  ");
        buf_append(b, sym->name, sym->name_length);
        buf_append_char(b, '\n');
        render_symbol_comment_block(b, &sym->comment, 4);
        if (sym->variant_count > 0)
        {
            buf_append_cstr(b, "\n    Variants\n");
            for (j = 0; j < sym->variant_count; j++)
            {
                buf_append_cstr(b, "      ");
                buf_append(b, sym->variant_names[j], sym->variant_name_lengths[j]);
                buf_append_char(b, '\n');
            }
        }
    }
}

static void render_module_interface_section(doc_buf_t *b, const vigil_doc_module_t *m)
{
    size_t i, j;
    int has_section = 0;

    for (i = 0; i < m->symbol_count; i++)
    {
        const vigil_doc_symbol_t *sym = &m->symbols[i];

        if (sym->kind != VIGIL_DOC_INTERFACE)
            continue;
        if (!has_section)
        {
            buf_append_cstr(b, "\nINTERFACES\n");
            has_section = 1;
        }
        buf_append_cstr(b, "  ");
        buf_append(b, sym->name, sym->name_length);
        buf_append_char(b, '\n');
        render_symbol_comment_block(b, &sym->comment, 4);
        if (sym->iface_method_count > 0)
        {
            buf_append_cstr(b, "\n    Methods\n");
            for (j = 0; j < sym->iface_method_count; j++)
            {
                buf_append_cstr(b, "      ");
                render_func_sig(b, &sym->iface_methods[j]);
                buf_append_char(b, '\n');
                if (sym->iface_methods[j].comment.text != NULL)
                    render_indent_comment(b, &sym->iface_methods[j].comment, 8);
            }
        }
    }
}

static void render_module_class_section(doc_buf_t *b, const vigil_doc_module_t *m)
{
    size_t i, j;
    int has_section = 0;

    for (i = 0; i < m->symbol_count; i++)
    {
        const vigil_doc_symbol_t *sym = &m->symbols[i];

        if (sym->kind != VIGIL_DOC_CLASS)
            continue;
        if (!has_section)
        {
            buf_append_cstr(b, "\nCLASSES\n");
            has_section = 1;
        }
        buf_append_cstr(b, "  ");
        render_class_name_line(b, sym);
        render_symbol_comment_block(b, &sym->comment, 4);
        if (sym->field_count > 0)
        {
            buf_append_cstr(b, "\n    Fields\n");
            for (j = 0; j < sym->field_count; j++)
            {
                buf_append_cstr(b, "      ");
                render_named_typed_symbol(b, &sym->fields[j]);
                if (sym->fields[j].comment.text != NULL)
                    render_indent_comment(b, &sym->fields[j].comment, 8);
            }
        }
        if (sym->method_count > 0)
        {
            buf_append_cstr(b, "\n    Methods\n");
            for (j = 0; j < sym->method_count; j++)
            {
                buf_append_cstr(b, "      ");
                render_func_sig(b, &sym->methods[j]);
                buf_append_char(b, '\n');
                if (sym->methods[j].comment.text != NULL)
                    render_indent_comment(b, &sym->methods[j].comment, 8);
            }
        }
    }
}

static void render_module_function_section(doc_buf_t *b, const vigil_doc_module_t *m)
{
    size_t i;
    int has_section = 0;

    for (i = 0; i < m->symbol_count; i++)
    {
        const vigil_doc_symbol_t *sym = &m->symbols[i];

        if (sym->kind != VIGIL_DOC_FUNCTION)
            continue;
        if (!has_section)
        {
            buf_append_cstr(b, "\nFUNCTIONS\n");
            has_section = 1;
        }
        buf_append_cstr(b, "  ");
        render_func_sig(b, sym);
        buf_append_char(b, '\n');
        if (sym->comment.text != NULL)
            render_indent_comment(b, &sym->comment, 4);
    }
}

static void render_module_view(doc_buf_t *b, const vigil_doc_module_t *m)
{
    render_module_header(b, m);
    render_module_summary_block(b, m);
    render_module_typed_section(b, m, VIGIL_DOC_CONSTANT, "CONSTANTS");
    render_module_typed_section(b, m, VIGIL_DOC_VARIABLE, "VARIABLES");
    render_module_enum_section(b, m);
    render_module_interface_section(b, m);
    render_module_class_section(b, m);
    render_module_function_section(b, m);
}

/* ── Render symbol view ──────────────────────────────────────────── */

static void render_symbol_detail(doc_buf_t *b, const vigil_doc_symbol_t *sym)
{
    switch (sym->kind)
    {
    case VIGIL_DOC_FUNCTION:
        render_func_sig(b, sym);
        buf_append_char(b, '\n');
        render_symbol_comment_block(b, &sym->comment, 0);
        break;

    case VIGIL_DOC_CONSTANT:
    case VIGIL_DOC_VARIABLE:
        render_named_typed_symbol(b, sym);
        render_symbol_comment_block(b, &sym->comment, 0);
        break;

    case VIGIL_DOC_ENUM:
        render_enum_detail(b, sym);
        break;

    case VIGIL_DOC_INTERFACE:
        render_interface_detail(b, sym);
        break;

    case VIGIL_DOC_CLASS:
        render_class_detail(b, sym);
        break;
    }
}

static int find_symbol_text(const vigil_doc_module_t *m, const char *name, size_t name_len,
                            const vigil_doc_symbol_t **out)
{
    size_t i;
    for (i = 0; i < m->symbol_count; i++)
    {
        if (m->symbols[i].name_length == name_len && memcmp(m->symbols[i].name, name, name_len) == 0)
        {
            *out = &m->symbols[i];
            return 1;
        }
    }
    return 0;
}

static int find_symbol(const vigil_doc_module_t *m, const char *name, const vigil_doc_symbol_t **out)
{
    return find_symbol_text(m, name, strlen(name), out);
}

static int find_class_member(const vigil_doc_symbol_t *cls, const char *member, size_t member_len,
                             const vigil_doc_symbol_t **out, int *is_method)
{
    size_t i;
    for (i = 0; i < cls->field_count; i++)
    {
        if (cls->fields[i].name_length == member_len && memcmp(cls->fields[i].name, member, member_len) == 0)
        {
            *out = &cls->fields[i];
            *is_method = 0;
            return 1;
        }
    }
    for (i = 0; i < cls->method_count; i++)
    {
        if (cls->methods[i].name_length == member_len && memcmp(cls->methods[i].name, member, member_len) == 0)
        {
            *out = &cls->methods[i];
            *is_method = 1;
            return 1;
        }
    }
    return 0;
}

static int find_iface_method(const vigil_doc_symbol_t *iface, const char *method, size_t method_len,
                             const vigil_doc_symbol_t **out)
{
    size_t i;
    for (i = 0; i < iface->iface_method_count; i++)
    {
        if (iface->iface_methods[i].name_length == method_len &&
            memcmp(iface->iface_methods[i].name, method, method_len) == 0)
        {
            *out = &iface->iface_methods[i];
            return 1;
        }
    }
    return 0;
}

/* ── vigil_doc_render ─────────────────────────────────────────────── */

static void set_symbol_not_found_error(vigil_error_t *error)
{
    if (error == NULL)
        return;

    vigil_error_clear(error);
    error->type = VIGIL_STATUS_INVALID_ARGUMENT;
    error->value = "public symbol not found";
    error->length = 23;
}

static vigil_status_t render_symbol_not_found(doc_buf_t *buf, vigil_error_t *error)
{
    buf_free(buf);
    set_symbol_not_found_error(error);
    return VIGIL_STATUS_INVALID_ARGUMENT;
}

static void render_qualified_symbol_prefix(doc_buf_t *b, const char *parent_name, size_t parent_len)
{
    buf_append(b, parent_name, parent_len);
    buf_append_char(b, '.');
}

static int render_qualified_class_symbol(doc_buf_t *b, const vigil_doc_symbol_t *parent_sym, const char *parent_name,
                                         size_t parent_len, const char *member_name, size_t member_len)
{
    const vigil_doc_symbol_t *member_sym;
    int is_method = 0;

    if (!find_class_member(parent_sym, member_name, member_len, &member_sym, &is_method))
        return 0;

    render_qualified_symbol_prefix(b, parent_name, parent_len);
    if (is_method)
    {
        render_func_sig(b, member_sym);
        buf_append_char(b, '\n');
        render_symbol_comment_block(b, &member_sym->comment, 0);
        return 1;
    }

    render_named_typed_symbol(b, member_sym);
    render_symbol_comment_block(b, &member_sym->comment, 0);
    return 1;
}

static int render_qualified_interface_symbol(doc_buf_t *b, const vigil_doc_symbol_t *parent_sym,
                                             const char *parent_name, size_t parent_len, const char *member_name,
                                             size_t member_len)
{
    const vigil_doc_symbol_t *member_sym;

    if (!find_iface_method(parent_sym, member_name, member_len, &member_sym))
        return 0;

    render_qualified_symbol_prefix(b, parent_name, parent_len);
    render_func_sig(b, member_sym);
    buf_append_char(b, '\n');
    render_symbol_comment_block(b, &member_sym->comment, 0);
    return 1;
}

static vigil_status_t render_qualified_symbol(doc_buf_t *b, const vigil_doc_module_t *module, const char *symbol,
                                              vigil_error_t *error)
{
    const char *dot = strchr(symbol, '.');
    const char *parent_name = symbol;
    size_t parent_len = (size_t)(dot - symbol);
    const char *member_name = dot + 1;
    size_t member_len = strlen(member_name);
    const vigil_doc_symbol_t *parent_sym = NULL;

    if (!find_symbol_text(module, parent_name, parent_len, &parent_sym))
        return render_symbol_not_found(b, error);

    if (parent_sym->kind == VIGIL_DOC_CLASS &&
        render_qualified_class_symbol(b, parent_sym, parent_name, parent_len, member_name, member_len))
        return VIGIL_STATUS_OK;

    if (parent_sym->kind == VIGIL_DOC_INTERFACE &&
        render_qualified_interface_symbol(b, parent_sym, parent_name, parent_len, member_name, member_len))
        return VIGIL_STATUS_OK;

    return render_symbol_not_found(b, error);
}

static vigil_status_t render_requested_symbol(doc_buf_t *b, const vigil_doc_module_t *module, const char *symbol,
                                              vigil_error_t *error)
{
    const vigil_doc_symbol_t *sym;

    if (strchr(symbol, '.') != NULL)
        return render_qualified_symbol(b, module, symbol, error);

    if (!find_symbol(module, symbol, &sym))
        return render_symbol_not_found(b, error);

    render_symbol_detail(b, sym);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_doc_render(const vigil_doc_module_t *module, const char *symbol, char **out_text,
                                size_t *out_length, vigil_error_t *error)
{
    doc_buf_t buf;

    if (module == NULL || out_text == NULL)
        return VIGIL_STATUS_INVALID_ARGUMENT;
    *out_text = NULL;
    if (out_length != NULL)
        *out_length = 0;

    buf_init(&buf, module->allocator);

    if (symbol == NULL || symbol[0] == '\0')
    {
        render_module_view(&buf, module);
    }
    else if (render_requested_symbol(&buf, module, symbol, error) != VIGIL_STATUS_OK)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_text = buf.data;
    if (out_length != NULL)
        *out_length = buf.length;
    return VIGIL_STATUS_OK;
}

/* ── Free helpers ────────────────────────────────────────────────── */

static void free_symbol(const vigil_allocator_t *a, vigil_doc_symbol_t *sym)
{
    size_t i;
    doc_free_ptr(a, sym->name);
    doc_free_ptr(a, sym->comment.text);
    doc_free_ptr(a, sym->return_text);
    doc_free_ptr(a, sym->type_text);
    doc_free_ptr(a, sym->implements_text);

    if (sym->params != NULL)
    {
        for (i = 0; i < sym->param_count; i++)
        {
            doc_free_ptr(a, sym->params[i].type_text);
            doc_free_ptr(a, sym->params[i].name);
        }
        doc_free_ptr(a, sym->params);
    }

    free_symbol_array(a, sym->fields, sym->field_count);
    free_symbol_array(a, sym->methods, sym->method_count);
    free_symbol_array(a, sym->iface_methods, sym->iface_method_count);

    if (sym->variant_names != NULL)
    {
        for (i = 0; i < sym->variant_count; i++)
            doc_free_ptr(a, sym->variant_names[i]);
        doc_free_ptr(a, sym->variant_names);
    }
    doc_free_ptr(a, sym->variant_name_lengths);
}

void vigil_doc_module_free(vigil_doc_module_t *module)
{
    size_t i;
    const vigil_allocator_t *a;
    if (module == NULL)
        return;
    a = module->allocator;
    doc_free_ptr(a, module->name);
    doc_free_ptr(a, module->summary.text);
    if (module->symbols != NULL)
    {
        for (i = 0; i < module->symbol_count; i++)
            free_symbol(a, &module->symbols[i]);
        doc_free_ptr(a, module->symbols);
    }
    memset(module, 0, sizeof(*module));
}
