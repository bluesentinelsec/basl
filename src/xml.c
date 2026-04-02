/*
 * xml.c — Pure C11 XML 1.0 parser with DOM tree builder.
 *
 * Supports elements, attributes, text, CDATA, comments, PIs,
 * character/entity references, and the XML declaration.
 * Skips DOCTYPE without processing.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_utf8.h"
#include "internal/vigil_xml.h"

/* ── Parser state ────────────────────────────────────────────────── */

typedef struct
{
    const char *text;
    size_t length;
    size_t pos;
    size_t line;
    size_t column;
    vigil_xml_error_t *error;
} xml_parser_t;

/* ── Growable buffer ─────────────────────────────────────────────── */

typedef struct
{
    char *data;
    size_t len;
    size_t cap;
} xml_buf_t;

static void buf_init(xml_buf_t *b)
{
    memset(b, 0, sizeof(*b));
}

static int buf_push(xml_buf_t *b, char ch)
{
    if (b->len + 1U >= b->cap)
    {
        size_t nc = b->cap < 64U ? 64U : b->cap * 2U;
        char *nd = realloc(b->data, nc);
        if (nd == NULL)
            return 0;
        b->data = nd;
        b->cap = nc;
    }
    b->data[b->len++] = ch;
    return 1;
}

static int buf_push_str(xml_buf_t *b, const char *s, size_t n)
{
    size_t i;
    for (i = 0U; i < n; i++)
    {
        if (!buf_push(b, s[i]))
            return 0;
    }
    return 1;
}

static char *buf_finish(xml_buf_t *b, size_t *out_len)
{
    char *result;
    if (!buf_push(b, '\0'))
        return NULL;
    b->len--; /* don't count NUL in length */
    if (out_len != NULL)
        *out_len = b->len;
    result = b->data;
    b->data = NULL;
    b->len = 0U;
    b->cap = 0U;
    return result;
}

static void buf_free(xml_buf_t *b)
{
    free(b->data);
    memset(b, 0, sizeof(*b));
}

/* ── String helpers ──────────────────────────────────────────────── */

static char *xml_strdup(const char *s)
{
    size_t len;
    char *copy;
    if (s == NULL)
        return NULL;
    len = strlen(s);
    copy = malloc(len + 1U);
    if (copy != NULL)
        memcpy(copy, s, len + 1U);
    return copy;
}

/* ── Parser primitives ───────────────────────────────────────────── */

static void xml_set_error(xml_parser_t *p, const char *msg)
{
    if (p->error != NULL)
    {
        p->error->line = p->line;
        p->error->column = p->column;
        snprintf(p->error->message, sizeof(p->error->message), "line %zu, column %zu: %s", p->line, p->column, msg);
    }
}

static int xml_eof(const xml_parser_t *p)
{
    return p->pos >= p->length;
}

static char xml_peek(const xml_parser_t *p)
{
    return xml_eof(p) ? '\0' : p->text[p->pos];
}

static char xml_advance(xml_parser_t *p)
{
    char ch;
    if (xml_eof(p))
        return '\0';
    ch = p->text[p->pos++];
    if (ch == '\n')
    {
        p->line++;
        p->column = 1U;
    }
    else
    {
        p->column++;
    }
    return ch;
}

static int xml_match_char(xml_parser_t *p, char expected)
{
    if (xml_peek(p) == expected)
    {
        xml_advance(p);
        return 1;
    }
    return 0;
}

static int xml_match_str(xml_parser_t *p, const char *expected)
{
    size_t len = strlen(expected);
    if (p->pos + len > p->length)
        return 0;
    if (memcmp(p->text + p->pos, expected, len) != 0)
        return 0;
    while (len-- > 0U)
        xml_advance(p);
    return 1;
}

static void xml_skip_whitespace(xml_parser_t *p)
{
    while (!xml_eof(p))
    {
        char ch = xml_peek(p);
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
            xml_advance(p);
        else
            break;
    }
}

static int xml_is_name_start(char ch)
{
    return isalpha((unsigned char)ch) || ch == '_' || ch == ':';
}

static int xml_is_name_char(char ch)
{
    return isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '.' || ch == ':';
}

/* ── Name and reference parsing ──────────────────────────────────── */

static char *xml_parse_name(xml_parser_t *p)
{
    xml_buf_t b;
    size_t start;

    if (xml_eof(p) || !xml_is_name_start(xml_peek(p)))
        return NULL;
    buf_init(&b);
    start = p->pos;
    while (!xml_eof(p) && xml_is_name_char(xml_peek(p)))
        buf_push(&b, xml_advance(p));
    if (b.len == 0U)
    {
        buf_free(&b);
        return NULL;
    }
    (void)start;
    return buf_finish(&b, NULL);
}

static int xml_decode_char_ref(xml_parser_t *p, xml_buf_t *b)
{
    uint32_t cp = 0U;
    int hex;
    char utf8[VIGIL_UTF8_MAX_BYTES];
    size_t utf8_len;

    hex = xml_match_char(p, 'x');
    if (hex)
    {
        while (!xml_eof(p) && xml_peek(p) != ';')
        {
            char ch = xml_advance(p);
            if (ch >= '0' && ch <= '9')
                cp = cp * 16U + (uint32_t)(ch - '0');
            else if (ch >= 'a' && ch <= 'f')
                cp = cp * 16U + 10U + (uint32_t)(ch - 'a');
            else if (ch >= 'A' && ch <= 'F')
                cp = cp * 16U + 10U + (uint32_t)(ch - 'A');
            else
            {
                xml_set_error(p, "invalid hex digit in character reference");
                return 0;
            }
        }
    }
    else
    {
        while (!xml_eof(p) && xml_peek(p) != ';')
        {
            char ch = xml_advance(p);
            if (ch >= '0' && ch <= '9')
                cp = cp * 10U + (uint32_t)(ch - '0');
            else
            {
                xml_set_error(p, "invalid digit in character reference");
                return 0;
            }
        }
    }
    if (!xml_match_char(p, ';'))
    {
        xml_set_error(p, "unterminated character reference");
        return 0;
    }
    utf8_len = vigil_utf8_encode(cp, utf8);
    if (utf8_len == 0U)
    {
        xml_set_error(p, "invalid codepoint in character reference");
        return 0;
    }
    return buf_push_str(b, utf8, utf8_len);
}

static int xml_decode_reference(xml_parser_t *p, xml_buf_t *b)
{
    /* '&' already consumed */
    if (xml_match_char(p, '#'))
        return xml_decode_char_ref(p, b);
    if (xml_match_str(p, "amp;"))
        return buf_push(b, '&');
    if (xml_match_str(p, "lt;"))
        return buf_push(b, '<');
    if (xml_match_str(p, "gt;"))
        return buf_push(b, '>');
    if (xml_match_str(p, "apos;"))
        return buf_push(b, '\'');
    if (xml_match_str(p, "quot;"))
        return buf_push(b, '"');
    xml_set_error(p, "unknown entity reference");
    return 0;
}

/* ── Element allocation ──────────────────────────────────────────── */

static vigil_xml_element_t *xml_element_new(const char *tag)
{
    vigil_xml_element_t *el = calloc(1U, sizeof(*el));
    if (el != NULL)
        el->tag = xml_strdup(tag);
    return el;
}

static void xml_element_free(vigil_xml_element_t *el)
{
    size_t i;
    if (el == NULL)
        return;
    free(el->tag);
    free(el->namespace_uri);
    free(el->text);
    for (i = 0U; i < el->attribute_count; i++)
    {
        free(el->attributes[i].name);
        free(el->attributes[i].namespace_uri);
        free(el->attributes[i].value);
    }
    free(el->attributes);
    for (i = 0U; i < el->child_count; i++)
        xml_element_free(el->children[i]);
    free(el->children);
    free(el);
}

static int xml_element_add_child(vigil_xml_element_t *parent, vigil_xml_element_t *child)
{
    if (parent->child_count >= parent->child_capacity)
    {
        size_t nc = parent->child_capacity < 4U ? 4U : parent->child_capacity * 2U;
        vigil_xml_element_t **np = realloc(parent->children, nc * sizeof(*np));
        if (np == NULL)
            return 0;
        parent->children = np;
        parent->child_capacity = nc;
    }
    child->parent = parent;
    parent->children[parent->child_count++] = child;
    return 1;
}

static int xml_element_append_text(vigil_xml_element_t *el, const char *s, size_t n)
{
    if (n == 0U)
        return 1;
    if (el->text_len + n + 1U > el->text_cap)
    {
        size_t nc = el->text_cap < 64U ? 64U : el->text_cap;
        while (nc < el->text_len + n + 1U)
            nc *= 2U;
        char *nt = realloc(el->text, nc);
        if (nt == NULL)
            return 0;
        el->text = nt;
        el->text_cap = nc;
    }
    memcpy(el->text + el->text_len, s, n);
    el->text_len += n;
    el->text[el->text_len] = '\0';
    return 1;
}

static int xml_element_add_attribute(vigil_xml_element_t *el, char *name, char *value)
{
    if (el->attribute_count >= el->attribute_capacity)
    {
        size_t nc = el->attribute_capacity < 4U ? 4U : el->attribute_capacity * 2U;
        vigil_xml_attribute_t *na = realloc(el->attributes, nc * sizeof(*na));
        if (na == NULL)
            return 0;
        el->attributes = na;
        el->attribute_capacity = nc;
    }
    el->attributes[el->attribute_count].name = name;
    el->attributes[el->attribute_count].namespace_uri = NULL;
    el->attributes[el->attribute_count].value = value;
    el->attribute_count++;
    return 1;
}

/* ── Skip helpers ────────────────────────────────────────────────── */

static int xml_skip_comment(xml_parser_t *p)
{
    /* "<!--" already matched */
    while (!xml_eof(p))
    {
        if (xml_match_str(p, "-->"))
            return 1;
        xml_advance(p);
    }
    xml_set_error(p, "unterminated comment");
    return 0;
}

static int xml_skip_pi(xml_parser_t *p)
{
    /* "<?" already matched */
    while (!xml_eof(p))
    {
        if (xml_match_str(p, "?>"))
            return 1;
        xml_advance(p);
    }
    xml_set_error(p, "unterminated processing instruction");
    return 0;
}

static int xml_skip_doctype(xml_parser_t *p)
{
    /* "<!DOCTYPE" already matched */
    int depth = 1;
    while (!xml_eof(p) && depth > 0)
    {
        char ch = xml_advance(p);
        if (ch == '<')
            depth++;
        else if (ch == '>')
            depth--;
    }
    return depth == 0;
}

static int xml_parse_cdata(xml_parser_t *p, vigil_xml_element_t *el)
{
    /* "<![CDATA[" already matched */
    while (!xml_eof(p))
    {
        if (xml_match_str(p, "]]>"))
            return 1;
        if (!xml_element_append_text(el, &p->text[p->pos], 1U))
            return 0;
        xml_advance(p);
    }
    xml_set_error(p, "unterminated CDATA section");
    return 0;
}

/* ── Attribute value parsing ─────────────────────────────────────── */

static char *xml_parse_attr_value(xml_parser_t *p)
{
    char quote;
    xml_buf_t b;

    quote = xml_peek(p);
    if (quote != '"' && quote != '\'')
    {
        xml_set_error(p, "expected quoted attribute value");
        return NULL;
    }
    xml_advance(p);
    buf_init(&b);
    while (!xml_eof(p) && xml_peek(p) != quote)
    {
        if (xml_peek(p) == '&')
        {
            xml_advance(p);
            if (!xml_decode_reference(p, &b))
            {
                buf_free(&b);
                return NULL;
            }
        }
        else
        {
            buf_push(&b, xml_advance(p));
        }
    }
    if (!xml_match_char(p, quote))
    {
        xml_set_error(p, "unterminated attribute value");
        buf_free(&b);
        return NULL;
    }
    return buf_finish(&b, NULL);
}

/* ── Element parsing ─────────────────────────────────────────────── */

static vigil_xml_element_t *xml_parse_element(xml_parser_t *p);

static int xml_parse_text_content(xml_parser_t *p, vigil_xml_element_t *el)
{
    xml_buf_t b;
    buf_init(&b);
    while (!xml_eof(p) && xml_peek(p) != '<')
    {
        if (xml_peek(p) == '&')
        {
            xml_advance(p);
            if (!xml_decode_reference(p, &b))
            {
                buf_free(&b);
                return 0;
            }
        }
        else
        {
            buf_push(&b, xml_advance(p));
        }
    }
    if (b.len > 0U)
    {
        int ok = xml_element_append_text(el, b.data, b.len);
        buf_free(&b);
        return ok;
    }
    buf_free(&b);
    return 1;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static int xml_parse_children(xml_parser_t *p, vigil_xml_element_t *el, const char *close_tag)
{
    while (!xml_eof(p))
    {
        if (xml_peek(p) != '<')
        {
            if (!xml_parse_text_content(p, el))
                return 0;
            continue;
        }
        /* Check for closing tag */
        if (p->pos + 1U < p->length && p->text[p->pos + 1U] == '/')
        {
            char *end_name;
            xml_advance(p); /* < */
            xml_advance(p); /* / */
            end_name = xml_parse_name(p);
            xml_skip_whitespace(p);
            if (!xml_match_char(p, '>'))
            {
                xml_set_error(p, "expected '>' in closing tag");
                free(end_name);
                return 0;
            }
            if (end_name == NULL || strcmp(end_name, close_tag) != 0)
            {
                char msg[128];
                snprintf(msg, sizeof(msg), "mismatched closing tag: expected </%s>, got </%s>", close_tag,
                         end_name ? end_name : "?");
                xml_set_error(p, msg);
                free(end_name);
                return 0;
            }
            free(end_name);
            return 1;
        }
        /* Check for special sequences */
        if (xml_match_str(p, "<!--"))
        {
            if (!xml_skip_comment(p))
                return 0;
            continue;
        }
        if (xml_match_str(p, "<![CDATA["))
        {
            if (!xml_parse_cdata(p, el))
                return 0;
            continue;
        }
        if (xml_match_str(p, "<?"))
        {
            if (!xml_skip_pi(p))
                return 0;
            continue;
        }
        /* Child element */
        {
            vigil_xml_element_t *child = xml_parse_element(p);
            if (child == NULL)
                return 0;
            if (!xml_element_add_child(el, child))
            {
                xml_element_free(child);
                return 0;
            }
        }
    }
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "unclosed element <%s>", close_tag);
        xml_set_error(p, msg);
    }
    return 0;
}

static vigil_xml_element_t *xml_parse_element(xml_parser_t *p)
{
    char *tag;
    vigil_xml_element_t *el;

    if (!xml_match_char(p, '<'))
    {
        xml_set_error(p, "expected '<'");
        return NULL;
    }
    tag = xml_parse_name(p);
    if (tag == NULL)
    {
        xml_set_error(p, "expected element name");
        return NULL;
    }
    el = xml_element_new(tag);
    free(tag);
    if (el == NULL)
        return NULL;

    /* Parse attributes */
    while (!xml_eof(p))
    {
        char *attr_name;
        char *attr_value;

        xml_skip_whitespace(p);
        if (xml_peek(p) == '/' || xml_peek(p) == '>')
            break;
        attr_name = xml_parse_name(p);
        if (attr_name == NULL)
        {
            xml_set_error(p, "expected attribute name");
            xml_element_free(el);
            return NULL;
        }
        xml_skip_whitespace(p);
        if (!xml_match_char(p, '='))
        {
            xml_set_error(p, "expected '=' after attribute name");
            free(attr_name);
            xml_element_free(el);
            return NULL;
        }
        xml_skip_whitespace(p);
        attr_value = xml_parse_attr_value(p);
        if (attr_value == NULL)
        {
            free(attr_name);
            xml_element_free(el);
            return NULL;
        }
        if (!xml_element_add_attribute(el, attr_name, attr_value))
        {
            free(attr_name);
            free(attr_value);
            xml_element_free(el);
            return NULL;
        }
    }

    /* Self-closing or open tag */
    if (xml_match_str(p, "/>"))
        return el;
    if (!xml_match_char(p, '>'))
    {
        xml_set_error(p, "expected '>' or '/>'");
        xml_element_free(el);
        return NULL;
    }

    /* Parse children until closing tag */
    if (!xml_parse_children(p, el, el->tag))
    {
        xml_element_free(el);
        return NULL;
    }
    return el;
}

/* ── XML declaration ─────────────────────────────────────────────── */

static int xml_parse_declaration(xml_parser_t *p, vigil_xml_document_t *doc)
{
    if (!xml_match_str(p, "<?xml"))
        return 1; /* no declaration is fine */

    xml_skip_whitespace(p);
    while (!xml_eof(p) && !xml_match_str(p, "?>"))
    {
        char *name = xml_parse_name(p);
        char *value;
        if (name == NULL)
        {
            xml_advance(p);
            continue;
        }
        xml_skip_whitespace(p);
        if (!xml_match_char(p, '='))
        {
            free(name);
            continue;
        }
        xml_skip_whitespace(p);
        value = xml_parse_attr_value(p);
        if (value == NULL)
        {
            free(name);
            return 0;
        }
        if (strcmp(name, "version") == 0)
        {
            free(doc->version);
            doc->version = value;
        }
        else if (strcmp(name, "encoding") == 0)
        {
            free(doc->encoding);
            doc->encoding = value;
        }
        else
        {
            free(value);
        }
        free(name);
        xml_skip_whitespace(p);
    }
    return 1;
}

/* ── Public API ──────────────────────────────────────────────────── */

vigil_status_t vigil_xml_parse(const char *text, size_t length, vigil_xml_document_t **out_document,
                               vigil_xml_error_t *out_error)
{
    xml_parser_t p;
    vigil_xml_document_t *doc;

    if (out_document != NULL)
        *out_document = NULL;
    if (text == NULL || out_document == NULL)
        return VIGIL_STATUS_INVALID_ARGUMENT;

    doc = calloc(1U, sizeof(*doc));
    if (doc == NULL)
        return VIGIL_STATUS_OUT_OF_MEMORY;

    memset(&p, 0, sizeof(p));
    p.text = text;
    p.length = length;
    p.line = 1U;
    p.column = 1U;
    p.error = out_error;
    if (out_error != NULL)
        memset(out_error, 0, sizeof(*out_error));

    /* Skip UTF-8 BOM */
    if (length >= 3U && (unsigned char)text[0] == 0xEFU && (unsigned char)text[1] == 0xBBU &&
        (unsigned char)text[2] == 0xBFU)
    {
        p.pos = 3U;
    }

    /* XML declaration */
    xml_skip_whitespace(&p);
    if (!xml_parse_declaration(&p, doc))
    {
        vigil_xml_document_free(doc);
        return VIGIL_STATUS_SYNTAX_ERROR;
    }

    /* Skip leading comments, PIs, whitespace */
    for (;;)
    {
        xml_skip_whitespace(&p);
        if (xml_match_str(&p, "<!--"))
        {
            if (!xml_skip_comment(&p))
            {
                vigil_xml_document_free(doc);
                return VIGIL_STATUS_SYNTAX_ERROR;
            }
            continue;
        }
        if (xml_match_str(&p, "<?"))
        {
            if (!xml_skip_pi(&p))
            {
                vigil_xml_document_free(doc);
                return VIGIL_STATUS_SYNTAX_ERROR;
            }
            continue;
        }
        if (xml_match_str(&p, "<!DOCTYPE"))
        {
            if (!xml_skip_doctype(&p))
            {
                xml_set_error(&p, "unterminated DOCTYPE");
                vigil_xml_document_free(doc);
                return VIGIL_STATUS_SYNTAX_ERROR;
            }
            continue;
        }
        break;
    }

    /* Root element */
    if (xml_eof(&p) || xml_peek(&p) != '<')
    {
        xml_set_error(&p, "expected root element");
        vigil_xml_document_free(doc);
        return VIGIL_STATUS_SYNTAX_ERROR;
    }
    doc->root = xml_parse_element(&p);
    if (doc->root == NULL)
    {
        vigil_xml_document_free(doc);
        return VIGIL_STATUS_SYNTAX_ERROR;
    }

    /* Skip trailing whitespace/comments */
    for (;;)
    {
        xml_skip_whitespace(&p);
        if (xml_match_str(&p, "<!--"))
        {
            if (!xml_skip_comment(&p))
            {
                vigil_xml_document_free(doc);
                return VIGIL_STATUS_SYNTAX_ERROR;
            }
            continue;
        }
        break;
    }

    *out_document = doc;
    return VIGIL_STATUS_OK;
}

void vigil_xml_document_free(vigil_xml_document_t *doc)
{
    if (doc == NULL)
        return;
    xml_element_free(doc->root);
    free(doc->version);
    free(doc->encoding);
    free(doc);
}

/* ── Serializer ──────────────────────────────────────────────────── */

static void xml_escape_text(xml_buf_t *b, const char *s, size_t n)
{
    size_t i;
    for (i = 0U; i < n; i++)
    {
        switch (s[i])
        {
        case '&':
            buf_push_str(b, "&amp;", 5U);
            break;
        case '<':
            buf_push_str(b, "&lt;", 4U);
            break;
        case '>':
            buf_push_str(b, "&gt;", 4U);
            break;
        default:
            buf_push(b, s[i]);
            break;
        }
    }
}

static void xml_escape_attr(xml_buf_t *b, const char *s)
{
    size_t i;
    size_t n = strlen(s);
    for (i = 0U; i < n; i++)
    {
        switch (s[i])
        {
        case '&':
            buf_push_str(b, "&amp;", 5U);
            break;
        case '<':
            buf_push_str(b, "&lt;", 4U);
            break;
        case '"':
            buf_push_str(b, "&quot;", 6U);
            break;
        default:
            buf_push(b, s[i]);
            break;
        }
    }
}

static void xml_serialize_element(xml_buf_t *b, const vigil_xml_element_t *el)
{
    size_t i;
    buf_push(b, '<');
    buf_push_str(b, el->tag, strlen(el->tag));
    for (i = 0U; i < el->attribute_count; i++)
    {
        buf_push(b, ' ');
        buf_push_str(b, el->attributes[i].name, strlen(el->attributes[i].name));
        buf_push_str(b, "=\"", 2U);
        xml_escape_attr(b, el->attributes[i].value);
        buf_push(b, '"');
    }
    if (el->child_count == 0U && (el->text == NULL || el->text_len == 0U))
    {
        buf_push_str(b, "/>", 2U);
        return;
    }
    buf_push(b, '>');
    if (el->text != NULL && el->text_len > 0U)
        xml_escape_text(b, el->text, el->text_len);
    for (i = 0U; i < el->child_count; i++)
        xml_serialize_element(b, el->children[i]);
    buf_push_str(b, "</", 2U);
    buf_push_str(b, el->tag, strlen(el->tag));
    buf_push(b, '>');
}

char *vigil_xml_document_to_string(const vigil_xml_document_t *doc, size_t *out_length)
{
    xml_buf_t b;
    if (doc == NULL || doc->root == NULL)
        return NULL;
    buf_init(&b);
    buf_push_str(&b, "<?xml version=\"", 15U);
    buf_push_str(&b, doc->version ? doc->version : "1.0", doc->version ? strlen(doc->version) : 3U);
    buf_push_str(&b, "\"?>", 3U);
    xml_serialize_element(&b, doc->root);
    return buf_finish(&b, out_length);
}

/* ── Query helpers ───────────────────────────────────────────────── */

const char *vigil_xml_element_attr(const vigil_xml_element_t *el, const char *name)
{
    size_t i;
    if (el == NULL || name == NULL)
        return NULL;
    for (i = 0U; i < el->attribute_count; i++)
    {
        if (strcmp(el->attributes[i].name, name) == 0)
            return el->attributes[i].value;
    }
    return NULL;
}

const vigil_xml_element_t *vigil_xml_element_child(const vigil_xml_element_t *el, const char *tag)
{
    size_t i;
    if (el == NULL || tag == NULL)
        return NULL;
    for (i = 0U; i < el->child_count; i++)
    {
        if (strcmp(el->children[i]->tag, tag) == 0)
            return el->children[i];
    }
    return NULL;
}

size_t vigil_xml_element_children_by_tag(const vigil_xml_element_t *el, const char *tag,
                                         const vigil_xml_element_t **out, size_t max)
{
    size_t i;
    size_t count = 0U;
    if (el == NULL || tag == NULL)
        return 0U;
    for (i = 0U; i < el->child_count && count < max; i++)
    {
        if (strcmp(el->children[i]->tag, tag) == 0)
            out[count++] = el->children[i];
    }
    return count;
}

char *vigil_xml_element_all_text(const vigil_xml_element_t *el, size_t *out_length)
{
    xml_buf_t b;
    size_t i;
    if (el == NULL)
        return NULL;
    buf_init(&b);
    if (el->text != NULL && el->text_len > 0U)
        buf_push_str(&b, el->text, el->text_len);
    for (i = 0U; i < el->child_count; i++)
    {
        size_t child_len = 0U;
        char *child_text = vigil_xml_element_all_text(el->children[i], &child_len);
        if (child_text != NULL)
        {
            buf_push_str(&b, child_text, child_len);
            free(child_text);
        }
    }
    return buf_finish(&b, out_length);
}
