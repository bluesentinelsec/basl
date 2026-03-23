#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/json.h"

/* ── Internal types ──────────────────────────────────────────────── */

typedef struct vigil_json_member
{
    char *key;
    size_t key_length;
    vigil_json_value_t *value;
} vigil_json_member_t;

struct vigil_json_value
{
    vigil_json_type_t type;
    vigil_allocator_t allocator;
    union {
        int boolean;
        double number;
        struct
        {
            char *data;
            size_t length;
        } string;
        struct
        {
            vigil_json_value_t **items;
            size_t count;
            size_t capacity;
        } array;
        struct
        {
            vigil_json_member_t *members;
            size_t count;
            size_t capacity;
        } object;
    } as;
};

/* ── Allocator helpers ───────────────────────────────────────────── */

static void *json_alloc(const vigil_allocator_t *a, size_t size)
{
    return a->allocate(a->user_data, size);
}

static void *json_realloc(const vigil_allocator_t *a, void *p, size_t size)
{
    return a->reallocate(a->user_data, p, size);
}

static void json_dealloc(const vigil_allocator_t *a, void *p)
{
    a->deallocate(a->user_data, p);
}

static vigil_allocator_t resolve_allocator(const vigil_allocator_t *allocator)
{
    if (allocator != NULL && vigil_allocator_is_valid(allocator))
    {
        return *allocator;
    }
    return vigil_default_allocator();
}

static vigil_status_t alloc_value(const vigil_allocator_t *allocator, vigil_json_type_t type, vigil_json_value_t **out,
                                  vigil_error_t *error)
{
    vigil_allocator_t a = resolve_allocator(allocator);
    vigil_json_value_t *v = (vigil_json_value_t *)json_alloc(&a, sizeof(*v));
    if (v == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "json: allocation failed");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    memset(v, 0, sizeof(*v));
    v->type = type;
    v->allocator = a;
    *out = v;
    return VIGIL_STATUS_OK;
}

/* ── Constructors ────────────────────────────────────────────────── */

vigil_status_t vigil_json_null_new(const vigil_allocator_t *allocator, vigil_json_value_t **out, vigil_error_t *error)
{
    if (out == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "json: out is NULL");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    return alloc_value(allocator, VIGIL_JSON_NULL, out, error);
}

vigil_status_t vigil_json_bool_new(const vigil_allocator_t *allocator, int value, vigil_json_value_t **out,
                                   vigil_error_t *error)
{
    if (out == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "json: out is NULL");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    vigil_status_t s = alloc_value(allocator, VIGIL_JSON_BOOL, out, error);
    if (s == VIGIL_STATUS_OK)
        (*out)->as.boolean = (value != 0);
    return s;
}

vigil_status_t vigil_json_number_new(const vigil_allocator_t *allocator, double value, vigil_json_value_t **out,
                                     vigil_error_t *error)
{
    if (out == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "json: out is NULL");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    vigil_status_t s = alloc_value(allocator, VIGIL_JSON_NUMBER, out, error);
    if (s == VIGIL_STATUS_OK)
        (*out)->as.number = value;
    return s;
}

vigil_status_t vigil_json_string_new(const vigil_allocator_t *allocator, const char *value, size_t length,
                                     vigil_json_value_t **out, vigil_error_t *error)
{
    if (out == NULL || value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "json: out or value is NULL");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    vigil_status_t s = alloc_value(allocator, VIGIL_JSON_STRING, out, error);
    if (s != VIGIL_STATUS_OK)
        return s;

    char *buf = (char *)json_alloc(&(*out)->allocator, length + 1);
    if (buf == NULL)
    {
        json_dealloc(&(*out)->allocator, *out);
        *out = NULL;
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "json: allocation failed");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    memcpy(buf, value, length);
    buf[length] = '\0';
    (*out)->as.string.data = buf;
    (*out)->as.string.length = length;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_json_array_new(const vigil_allocator_t *allocator, vigil_json_value_t **out, vigil_error_t *error)
{
    if (out == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "json: out is NULL");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    return alloc_value(allocator, VIGIL_JSON_ARRAY, out, error);
}

vigil_status_t vigil_json_object_new(const vigil_allocator_t *allocator, vigil_json_value_t **out, vigil_error_t *error)
{
    if (out == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "json: out is NULL");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    return alloc_value(allocator, VIGIL_JSON_OBJECT, out, error);
}

/* ── Destructor ──────────────────────────────────────────────────── */

void vigil_json_free(vigil_json_value_t **value)
{
    if (value == NULL || *value == NULL)
        return;
    vigil_json_value_t *v = *value;
    vigil_allocator_t a = v->allocator;

    switch (v->type)
    {
    case VIGIL_JSON_STRING:
        json_dealloc(&a, v->as.string.data);
        break;
    case VIGIL_JSON_ARRAY:
        for (size_t i = 0; i < v->as.array.count; i++)
        {
            vigil_json_free(&v->as.array.items[i]);
        }
        json_dealloc(&a, v->as.array.items);
        break;
    case VIGIL_JSON_OBJECT:
        for (size_t i = 0; i < v->as.object.count; i++)
        {
            json_dealloc(&a, v->as.object.members[i].key);
            vigil_json_free(&v->as.object.members[i].value);
        }
        json_dealloc(&a, v->as.object.members);
        break;
    default:
        break;
    }
    json_dealloc(&a, v);
    *value = NULL;
}

/* ── Type inspection ─────────────────────────────────────────────── */

vigil_json_type_t vigil_json_type(const vigil_json_value_t *value)
{
    return value != NULL ? value->type : VIGIL_JSON_NULL;
}

/* ── Scalar accessors ────────────────────────────────────────────── */

int vigil_json_bool_value(const vigil_json_value_t *value)
{
    return (value != NULL && value->type == VIGIL_JSON_BOOL) ? value->as.boolean : 0;
}

double vigil_json_number_value(const vigil_json_value_t *value)
{
    return (value != NULL && value->type == VIGIL_JSON_NUMBER) ? value->as.number : 0.0;
}

const char *vigil_json_string_value(const vigil_json_value_t *value)
{
    return (value != NULL && value->type == VIGIL_JSON_STRING) ? value->as.string.data : "";
}

size_t vigil_json_string_length(const vigil_json_value_t *value)
{
    return (value != NULL && value->type == VIGIL_JSON_STRING) ? value->as.string.length : 0;
}

/* ── Array operations ────────────────────────────────────────────── */

size_t vigil_json_array_count(const vigil_json_value_t *array)
{
    return (array != NULL && array->type == VIGIL_JSON_ARRAY) ? array->as.array.count : 0;
}

const vigil_json_value_t *vigil_json_array_get(const vigil_json_value_t *array, size_t index)
{
    if (array == NULL || array->type != VIGIL_JSON_ARRAY)
        return NULL;
    if (index >= array->as.array.count)
        return NULL;
    return array->as.array.items[index];
}

vigil_status_t vigil_json_array_push(vigil_json_value_t *array, vigil_json_value_t *element, vigil_error_t *error)
{
    if (array == NULL || array->type != VIGIL_JSON_ARRAY || element == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "json: invalid array push");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (array->as.array.count >= array->as.array.capacity)
    {
        size_t new_cap = array->as.array.capacity < 8 ? 8 : array->as.array.capacity * 2;
        vigil_json_value_t **new_items = (vigil_json_value_t **)json_realloc(&array->allocator, array->as.array.items,
                                                                             new_cap * sizeof(vigil_json_value_t *));
        if (new_items == NULL)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "json: allocation failed");
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        array->as.array.items = new_items;
        array->as.array.capacity = new_cap;
    }
    array->as.array.items[array->as.array.count++] = element;
    return VIGIL_STATUS_OK;
}

/* ── Object operations ───────────────────────────────────────────── */

size_t vigil_json_object_count(const vigil_json_value_t *object)
{
    return (object != NULL && object->type == VIGIL_JSON_OBJECT) ? object->as.object.count : 0;
}

const vigil_json_value_t *vigil_json_object_get(const vigil_json_value_t *object, const char *key)
{
    if (object == NULL || object->type != VIGIL_JSON_OBJECT || key == NULL)
        return NULL;
    size_t key_len = strlen(key);
    for (size_t i = 0; i < object->as.object.count; i++)
    {
        if (object->as.object.members[i].key_length == key_len &&
            memcmp(object->as.object.members[i].key, key, key_len) == 0)
        {
            return object->as.object.members[i].value;
        }
    }
    return NULL;
}

vigil_status_t vigil_json_object_set(vigil_json_value_t *object, const char *key, size_t key_length,
                                     vigil_json_value_t *value, vigil_error_t *error)
{
    if (object == NULL || object->type != VIGIL_JSON_OBJECT || key == NULL || value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "json: invalid object set");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    /* Replace existing key. */
    for (size_t i = 0; i < object->as.object.count; i++)
    {
        if (object->as.object.members[i].key_length == key_length &&
            memcmp(object->as.object.members[i].key, key, key_length) == 0)
        {
            vigil_json_free(&object->as.object.members[i].value);
            object->as.object.members[i].value = value;
            return VIGIL_STATUS_OK;
        }
    }

    /* Grow if needed. */
    if (object->as.object.count >= object->as.object.capacity)
    {
        size_t new_cap = object->as.object.capacity < 8 ? 8 : object->as.object.capacity * 2;
        vigil_json_member_t *new_members = (vigil_json_member_t *)json_realloc(
            &object->allocator, object->as.object.members, new_cap * sizeof(vigil_json_member_t));
        if (new_members == NULL)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "json: allocation failed");
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        object->as.object.members = new_members;
        object->as.object.capacity = new_cap;
    }

    /* Copy key. */
    char *key_copy = (char *)json_alloc(&object->allocator, key_length + 1);
    if (key_copy == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "json: allocation failed");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    memcpy(key_copy, key, key_length);
    key_copy[key_length] = '\0';

    vigil_json_member_t *m = &object->as.object.members[object->as.object.count++];
    m->key = key_copy;
    m->key_length = key_length;
    m->value = value;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_json_object_entry(const vigil_json_value_t *object, size_t index, const char **out_key,
                                       size_t *out_key_length, const vigil_json_value_t **out_value)
{
    if (object == NULL || object->type != VIGIL_JSON_OBJECT || index >= object->as.object.count)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    const vigil_json_member_t *m = &object->as.object.members[index];
    if (out_key != NULL)
        *out_key = m->key;
    if (out_key_length != NULL)
        *out_key_length = m->key_length;
    if (out_value != NULL)
        *out_value = m->value;
    return VIGIL_STATUS_OK;
}

/* ── Parser ──────────────────────────────────────────────────────── */

typedef struct json_parser
{
    const char *input;
    size_t length;
    size_t pos;
    const vigil_allocator_t *allocator;
    vigil_error_t *error;
} json_parser_t;

static void parser_skip_whitespace(json_parser_t *p)
{
    while (p->pos < p->length)
    {
        char c = p->input[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            p->pos++;
        }
        else
        {
            break;
        }
    }
}

static vigil_status_t parser_error(json_parser_t *p, const char *msg)
{
    vigil_error_set_literal(p->error, VIGIL_STATUS_SYNTAX_ERROR, msg);
    return VIGIL_STATUS_SYNTAX_ERROR;
}

static int parser_peek(json_parser_t *p)
{
    return p->pos < p->length ? (unsigned char)p->input[p->pos] : -1;
}

static int parser_advance(json_parser_t *p)
{
    if (p->pos >= p->length)
        return -1;
    return (unsigned char)p->input[p->pos++];
}

static int parser_expect(json_parser_t *p, char c)
{
    if (p->pos < p->length && p->input[p->pos] == c)
    {
        p->pos++;
        return 1;
    }
    return 0;
}

static vigil_status_t parser_match_literal(json_parser_t *p, const char *lit, size_t len)
{
    if (p->pos + len > p->length || memcmp(p->input + p->pos, lit, len) != 0)
    {
        return parser_error(p, "json: unexpected token");
    }
    p->pos += len;
    return VIGIL_STATUS_OK;
}

/* Forward declaration. */
static vigil_status_t parse_value(json_parser_t *p, vigil_json_value_t **out);

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* Read a 4-hex-digit unicode value at p->pos, advancing past it. */
static vigil_status_t json_read_hex4(json_parser_t *p, uint32_t *out)
{
    uint32_t cp = 0;
    if (p->pos + 4 > p->length)
        return parser_error(p, "json: incomplete unicode escape");
    for (int i = 0; i < 4; i++)
    {
        int d = hex_digit(p->input[p->pos++]);
        if (d < 0)
            return parser_error(p, "json: invalid hex digit");
        cp = (cp << 4) | (uint32_t)d;
    }
    *out = cp;
    return VIGIL_STATUS_OK;
}

/* Read the low surrogate of a surrogate pair and combine with *hi. */
static vigil_status_t json_read_surrogate_pair(json_parser_t *p, uint32_t *hi)
{
    if (p->pos + 6 > p->length || p->input[p->pos] != '\\' || p->input[p->pos + 1] != 'u')
        return parser_error(p, "json: missing low surrogate");
    p->pos += 2;
    uint32_t lo = 0;
    vigil_status_t s = json_read_hex4(p, &lo);
    if (s != VIGIL_STATUS_OK)
        return s;
    if (lo < 0xDC00U || lo > 0xDFFFU)
        return parser_error(p, "json: invalid low surrogate");
    *hi = 0x10000U + ((*hi - 0xD800U) << 10) + (lo - 0xDC00U);
    return VIGIL_STATUS_OK;
}

/* Encode a Unicode codepoint as UTF-8 into buf[*pos].  Returns bytes written (1-4). */
static size_t json_encode_utf8(char *buf, size_t pos, uint32_t cp)
{
    if (cp < 0x80U)
    {
        buf[pos] = (char)cp;
        return 1;
    }
    if (cp < 0x800U)
    {
        buf[pos] = (char)(0xC0 | (cp >> 6));
        buf[pos + 1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000U)
    {
        buf[pos] = (char)(0xE0 | (cp >> 12));
        buf[pos + 1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[pos + 2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    buf[pos] = (char)(0xF0 | (cp >> 18));
    buf[pos + 1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[pos + 2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[pos + 3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* ── Growable string buffer for parse_string_content ──────────────── */

typedef struct
{
    char *data;
    size_t len;
    size_t cap;
    vigil_allocator_t alloc;
} json_strbuf_t;

static vigil_status_t json_strbuf_init(json_strbuf_t *sb, vigil_allocator_t a, size_t cap)
{
    sb->data = (char *)json_alloc(&a, cap);
    if (!sb->data)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    sb->len = 0;
    sb->cap = cap;
    sb->alloc = a;
    return VIGIL_STATUS_OK;
}

static vigil_status_t json_strbuf_push(json_strbuf_t *sb, char ch)
{
    if (sb->len >= sb->cap)
    {
        size_t nc = sb->cap * 2;
        char *nb = (char *)json_realloc(&sb->alloc, sb->data, nc);
        if (!nb)
            return VIGIL_STATUS_OUT_OF_MEMORY;
        sb->data = nb;
        sb->cap = nc;
    }
    sb->data[sb->len++] = ch;
    return VIGIL_STATUS_OK;
}

static vigil_status_t json_strbuf_ensure(json_strbuf_t *sb, size_t extra)
{
    if (sb->len + extra >= sb->cap)
    {
        size_t nc = sb->cap * 2;
        while (nc <= sb->len + extra)
            nc *= 2;
        char *nb = (char *)json_realloc(&sb->alloc, sb->data, nc);
        if (!nb)
            return VIGIL_STATUS_OUT_OF_MEMORY;
        sb->data = nb;
        sb->cap = nc;
    }
    return VIGIL_STATUS_OK;
}

/* ── Escape-sequence handler (extracted from parse_string_content) ── */

static vigil_status_t parse_unicode_escape(json_parser_t *p, json_strbuf_t *sb)
{
    uint32_t cp = 0;
    vigil_status_t s = json_read_hex4(p, &cp);
    if (s != VIGIL_STATUS_OK)
        return s;
    if (cp >= 0xD800U && cp <= 0xDBFFU)
    {
        s = json_read_surrogate_pair(p, &cp);
        if (s != VIGIL_STATUS_OK)
            return s;
    }
    s = json_strbuf_ensure(sb, 4);
    if (s != VIGIL_STATUS_OK)
        return s;
    sb->len += json_encode_utf8(sb->data, sb->len, cp);
    return VIGIL_STATUS_OK;
}

/* Table mapping escape characters to their replacement byte.
   0 = not a simple escape (needs special handling). */
static const char kEscapeTable[256] = {
    ['\"'] = '\"', ['\\'] = '\\', ['/'] = '/', ['b'] = '\b', ['f'] = '\f', ['n'] = '\n', ['r'] = '\r', ['t'] = '\t',
};

static vigil_status_t parse_escape(json_parser_t *p, json_strbuf_t *sb)
{
    if (p->pos >= p->length)
        return parser_error(p, "json: unterminated escape");
    unsigned char esc = (unsigned char)p->input[p->pos++];
    if (esc == 'u')
        return parse_unicode_escape(p, sb);
    char replacement = kEscapeTable[esc];
    if (replacement)
        return json_strbuf_push(sb, replacement);
    return parser_error(p, "json: invalid escape character");
}

/* ── String content parser ───────────────────────────────────────── */

static vigil_status_t parse_string_content(json_parser_t *p, char **out, size_t *out_len)
{
    /* Opening quote already consumed by caller. */
    json_strbuf_t sb;
    vigil_status_t s = json_strbuf_init(&sb, resolve_allocator(p->allocator), 32);
    if (s != VIGIL_STATUS_OK)
    {
        vigil_error_set_literal(p->error, VIGIL_STATUS_OUT_OF_MEMORY, "json: allocation failed");
        return s;
    }

    for (;;)
    {
        if (p->pos >= p->length)
        {
            s = parser_error(p, "json: unterminated string");
            goto fail;
        }
        char c = p->input[p->pos++];
        if (c == '"')
            break;
        if (c == '\\')
            s = parse_escape(p, &sb);
        else
            s = json_strbuf_push(&sb, c);
        if (s != VIGIL_STATUS_OK)
            goto fail;
    }

    /* Ensure room for NUL terminator. */
    s = json_strbuf_ensure(&sb, 1);
    if (s != VIGIL_STATUS_OK)
        goto fail;
    sb.data[sb.len] = '\0';
    *out = sb.data;
    *out_len = sb.len;
    return VIGIL_STATUS_OK;

fail:
    json_dealloc(&sb.alloc, sb.data);
    return s;
}

static vigil_status_t parse_string(json_parser_t *p, vigil_json_value_t **out)
{
    char *str = NULL;
    size_t len = 0;
    vigil_status_t s = parse_string_content(p, &str, &len);
    if (s != VIGIL_STATUS_OK)
        return s;

    s = vigil_json_string_new(p->allocator, str, len, out, p->error);
    vigil_allocator_t a = resolve_allocator(p->allocator);
    json_dealloc(&a, str);
    return s;
}

static int json_is_digit(json_parser_t *p)
{
    return p->pos < p->length && p->input[p->pos] >= '0' && p->input[p->pos] <= '9';
}

static void json_skip_digits(json_parser_t *p)
{
    while (json_is_digit(p))
        p->pos++;
}

static vigil_status_t parse_number(json_parser_t *p, vigil_json_value_t **out)
{
    size_t start = p->pos;

    /* Optional minus. */
    if (p->pos < p->length && p->input[p->pos] == '-')
        p->pos++;

    /* Integer part. */
    if (!json_is_digit(p))
        return parser_error(p, "json: invalid number");
    if (p->input[p->pos] == '0')
        p->pos++;
    else
        json_skip_digits(p);

    /* Fractional part. */
    if (p->pos < p->length && p->input[p->pos] == '.')
    {
        p->pos++;
        if (!json_is_digit(p))
            return parser_error(p, "json: invalid number fraction");
        json_skip_digits(p);
    }

    /* Exponent. */
    if (p->pos < p->length && (p->input[p->pos] == 'e' || p->input[p->pos] == 'E'))
    {
        p->pos++;
        if (p->pos < p->length && (p->input[p->pos] == '+' || p->input[p->pos] == '-'))
            p->pos++;
        if (!json_is_digit(p))
            return parser_error(p, "json: invalid number exponent");
        json_skip_digits(p);
    }

    /* Parse the number text.  We need a NUL-terminated copy for strtod. */
    size_t num_len = p->pos - start;
    char tmp[64];
    if (num_len >= sizeof(tmp))
    {
        return parser_error(p, "json: number too long");
    }
    memcpy(tmp, p->input + start, num_len);
    tmp[num_len] = '\0';

    char *end = NULL;
    double value = strtod(tmp, &end);
    if (end != tmp + num_len)
    {
        return parser_error(p, "json: invalid number");
    }

    return vigil_json_number_new(p->allocator, value, out, p->error);
}

static vigil_status_t parse_array(json_parser_t *p, vigil_json_value_t **out)
{
    vigil_status_t s = vigil_json_array_new(p->allocator, out, p->error);
    if (s != VIGIL_STATUS_OK)
        return s;

    parser_skip_whitespace(p);
    if (parser_peek(p) == ']')
    {
        parser_advance(p);
        return VIGIL_STATUS_OK;
    }

    for (;;)
    {
        vigil_json_value_t *elem = NULL;
        s = parse_value(p, &elem);
        if (s != VIGIL_STATUS_OK)
        {
            vigil_json_free(out);
            return s;
        }
        s = vigil_json_array_push(*out, elem, p->error);
        if (s != VIGIL_STATUS_OK)
        {
            vigil_json_free(&elem);
            vigil_json_free(out);
            return s;
        }
        parser_skip_whitespace(p);
        if (parser_expect(p, ','))
        {
            parser_skip_whitespace(p);
            continue;
        }
        if (parser_expect(p, ']'))
            break;
        vigil_json_free(out);
        return parser_error(p, "json: expected ',' or ']'");
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t parse_object(json_parser_t *p, vigil_json_value_t **out)
{
    vigil_status_t s = vigil_json_object_new(p->allocator, out, p->error);
    if (s != VIGIL_STATUS_OK)
        return s;

    parser_skip_whitespace(p);
    if (parser_peek(p) == '}')
    {
        parser_advance(p);
        return VIGIL_STATUS_OK;
    }

    for (;;)
    {
        parser_skip_whitespace(p);
        if (!parser_expect(p, '"'))
        {
            vigil_json_free(out);
            return parser_error(p, "json: expected string key");
        }
        char *key = NULL;
        size_t key_len = 0;
        s = parse_string_content(p, &key, &key_len);
        if (s != VIGIL_STATUS_OK)
        {
            vigil_json_free(out);
            return s;
        }

        parser_skip_whitespace(p);
        if (!parser_expect(p, ':'))
        {
            vigil_allocator_t a = resolve_allocator(p->allocator);
            json_dealloc(&a, key);
            vigil_json_free(out);
            return parser_error(p, "json: expected ':'");
        }

        vigil_json_value_t *val = NULL;
        s = parse_value(p, &val);
        if (s != VIGIL_STATUS_OK)
        {
            vigil_allocator_t a = resolve_allocator(p->allocator);
            json_dealloc(&a, key);
            vigil_json_free(out);
            return s;
        }

        s = vigil_json_object_set(*out, key, key_len, val, p->error);
        vigil_allocator_t a = resolve_allocator(p->allocator);
        json_dealloc(&a, key);
        if (s != VIGIL_STATUS_OK)
        {
            vigil_json_free(&val);
            vigil_json_free(out);
            return s;
        }

        parser_skip_whitespace(p);
        if (parser_expect(p, ','))
            continue;
        if (parser_expect(p, '}'))
            break;
        vigil_json_free(out);
        return parser_error(p, "json: expected ',' or '}'");
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t parse_value(json_parser_t *p, vigil_json_value_t **out)
{
    parser_skip_whitespace(p);
    int c = parser_peek(p);
    switch (c)
    {
    case '"':
        parser_advance(p);
        return parse_string(p, out);
    case '{':
        parser_advance(p);
        return parse_object(p, out);
    case '[':
        parser_advance(p);
        return parse_array(p, out);
    case 't': {
        vigil_status_t s = parser_match_literal(p, "true", 4);
        if (s != VIGIL_STATUS_OK)
            return s;
        return vigil_json_bool_new(p->allocator, 1, out, p->error);
    }
    case 'f': {
        vigil_status_t s = parser_match_literal(p, "false", 5);
        if (s != VIGIL_STATUS_OK)
            return s;
        return vigil_json_bool_new(p->allocator, 0, out, p->error);
    }
    case 'n': {
        vigil_status_t s = parser_match_literal(p, "null", 4);
        if (s != VIGIL_STATUS_OK)
            return s;
        return vigil_json_null_new(p->allocator, out, p->error);
    }
    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        return parse_number(p, out);
    default:
        return parser_error(p, "json: unexpected character");
    }
}

vigil_status_t vigil_json_parse(const vigil_allocator_t *allocator, const char *input, size_t length,
                                vigil_json_value_t **out, vigil_error_t *error)
{
    if (input == NULL || out == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "json: input or out is NULL");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    vigil_error_clear(error);

    json_parser_t p;
    p.input = input;
    p.length = length;
    p.pos = 0;
    p.allocator = allocator;
    p.error = error;

    vigil_status_t s = parse_value(&p, out);
    if (s != VIGIL_STATUS_OK)
        return s;

    parser_skip_whitespace(&p);
    if (p.pos != p.length)
    {
        vigil_json_free(out);
        return parser_error(&p, "json: trailing content after value");
    }
    return VIGIL_STATUS_OK;
}

/* ── Emitter ─────────────────────────────────────────────────────── */

typedef struct json_emitter
{
    char *buf;
    size_t len;
    size_t cap;
    vigil_allocator_t allocator;
    vigil_status_t status;
    vigil_error_t *error;
} json_emitter_t;

static void emit_grow(json_emitter_t *e, size_t need)
{
    if (e->status != VIGIL_STATUS_OK)
        return;
    if (e->len + need <= e->cap)
        return;
    size_t new_cap = e->cap < 64 ? 64 : e->cap;
    while (new_cap < e->len + need)
        new_cap *= 2;
    char *nb = (char *)json_realloc(&e->allocator, e->buf, new_cap);
    if (nb == NULL)
    {
        e->status = VIGIL_STATUS_OUT_OF_MEMORY;
        vigil_error_set_literal(e->error, VIGIL_STATUS_OUT_OF_MEMORY, "json: allocation failed");
        return;
    }
    e->buf = nb;
    e->cap = new_cap;
}

static void emit_raw(json_emitter_t *e, const char *s, size_t n)
{
    if (n == 0)
        return;
    emit_grow(e, n);
    if (e->status != VIGIL_STATUS_OK || e->buf == NULL)
        return;
    memcpy(e->buf + e->len, s, n);
    e->len += n;
}

static void emit_char(json_emitter_t *e, char c)
{
    emit_raw(e, &c, 1);
}

static void emit_string(json_emitter_t *e, const char *s, size_t len)
{
    emit_char(e, '"');
    for (size_t i = 0; i < len && e->status == VIGIL_STATUS_OK; i++)
    {
        unsigned char c = (unsigned char)s[i];
        switch (c)
        {
        case '"':
            emit_raw(e, "\\\"", 2);
            break;
        case '\\':
            emit_raw(e, "\\\\", 2);
            break;
        case '\b':
            emit_raw(e, "\\b", 2);
            break;
        case '\f':
            emit_raw(e, "\\f", 2);
            break;
        case '\n':
            emit_raw(e, "\\n", 2);
            break;
        case '\r':
            emit_raw(e, "\\r", 2);
            break;
        case '\t':
            emit_raw(e, "\\t", 2);
            break;
        default:
            if (c < 0x20)
            {
                char hex[7];
                snprintf(hex, sizeof(hex), "\\u%04x", c);
                emit_raw(e, hex, 6);
            }
            else
            {
                emit_char(e, (char)c);
            }
            break;
        }
    }
    emit_char(e, '"');
}

static void emit_value(json_emitter_t *e, const vigil_json_value_t *v);

static void emit_value(json_emitter_t *e, const vigil_json_value_t *v)
{
    if (e->status != VIGIL_STATUS_OK)
        return;
    if (v == NULL)
    {
        emit_raw(e, "null", 4);
        return;
    }

    switch (v->type)
    {
    case VIGIL_JSON_NULL:
        emit_raw(e, "null", 4);
        break;
    case VIGIL_JSON_BOOL:
        if (v->as.boolean)
            emit_raw(e, "true", 4);
        else
            emit_raw(e, "false", 5);
        break;
    case VIGIL_JSON_NUMBER: {
        char tmp[64];
        int n;
        double val = v->as.number;
        /* Emit integers without decimal point. */
        if (val == (double)(int64_t)val && val >= -1e15 && val <= 1e15)
        {
            n = snprintf(tmp, sizeof(tmp), "%" PRId64, (int64_t)val);
        }
        else
        {
            n = snprintf(tmp, sizeof(tmp), "%.17g", val);
        }
        if (n > 0)
            emit_raw(e, tmp, (size_t)n);
        break;
    }
    case VIGIL_JSON_STRING:
        emit_string(e, v->as.string.data, v->as.string.length);
        break;
    case VIGIL_JSON_ARRAY:
        emit_char(e, '[');
        for (size_t i = 0; i < v->as.array.count; i++)
        {
            if (i > 0)
                emit_char(e, ',');
            emit_value(e, v->as.array.items[i]);
        }
        emit_char(e, ']');
        break;
    case VIGIL_JSON_OBJECT:
        emit_char(e, '{');
        for (size_t i = 0; i < v->as.object.count; i++)
        {
            if (i > 0)
                emit_char(e, ',');
            emit_string(e, v->as.object.members[i].key, v->as.object.members[i].key_length);
            emit_char(e, ':');
            emit_value(e, v->as.object.members[i].value);
        }
        emit_char(e, '}');
        break;
    }
}

vigil_status_t vigil_json_emit(const vigil_json_value_t *value, char **out_string, size_t *out_length,
                               vigil_error_t *error)
{
    if (value == NULL || out_string == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "json: value or out_string is NULL");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    vigil_error_clear(error);

    json_emitter_t e;
    memset(&e, 0, sizeof(e));
    e.allocator = value->allocator;
    e.error = error;
    e.status = VIGIL_STATUS_OK;

    emit_value(&e, value);

    if (e.status != VIGIL_STATUS_OK)
    {
        if (e.buf != NULL)
            json_dealloc(&e.allocator, e.buf);
        return e.status;
    }

    /* NUL-terminate. */
    emit_char(&e, '\0');
    if (e.status != VIGIL_STATUS_OK)
    {
        if (e.buf != NULL)
            json_dealloc(&e.allocator, e.buf);
        return e.status;
    }
    e.len--; /* Don't count NUL in length. */

    *out_string = e.buf;
    if (out_length != NULL)
        *out_length = e.len;
    return VIGIL_STATUS_OK;
}
