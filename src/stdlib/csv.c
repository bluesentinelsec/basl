/* VIGIL standard library: csv module.
 *
 * RFC 4180 compliant CSV parsing and generation.
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/runtime.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"

/* ── Allocator helpers ───────────────────────────────────────────── */

static vigil_allocator_t get_alloc(vigil_vm_t *vm)
{
    const vigil_allocator_t *a = vigil_runtime_allocator(vigil_vm_runtime(vm));
    if (a != NULL)
        return *a;
    return vigil_default_allocator();
}

/* ── Helpers ─────────────────────────────────────────────────────── */

static bool get_string_arg(vigil_vm_t *vm, size_t base, size_t idx, const char **out, size_t *out_len)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    if (!vigil_nanbox_is_object(v))
        return false;
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(v);
    if (!obj || vigil_object_type(obj) != VIGIL_OBJECT_STRING)
        return false;
    *out = vigil_string_object_c_str(obj);
    *out_len = vigil_string_object_length(obj);
    return true;
}

static vigil_status_t push_string(vigil_vm_t *vm, const char *str, size_t len, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_status_t s = vigil_string_object_new(vigil_vm_runtime(vm), str, len, &obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t v;
    vigil_value_init_object(&v, &obj);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

/* ── CSV Parser ──────────────────────────────────────────────────── */

typedef struct
{
    const char *data;
    size_t len;
    size_t pos;
} csv_reader_t;

static int csv_peek(csv_reader_t *r)
{
    return r->pos < r->len ? (unsigned char)r->data[r->pos] : -1;
}

static int csv_next(csv_reader_t *r)
{
    return r->pos < r->len ? (unsigned char)r->data[r->pos++] : -1;
}

static void csv_skip_crlf(csv_reader_t *r)
{
    if (csv_peek(r) == '\r')
        csv_next(r);
    if (csv_peek(r) == '\n')
        csv_next(r);
}

/* Parse a single field, handling quotes. Returns allocated string. */
static int csv_is_field_end(int c)
{
    return c == ',' || c == '\r' || c == '\n';
}
static bool csv_field_spill(const vigil_allocator_t *alloc, char **buf, char **heap_buf, size_t len, size_t *cap)
{
    size_t new_cap = (*cap + 1U) * 2U;
    char *h = (char *)alloc->allocate(alloc->user_data, new_cap + 1U);
    if (!h)
        return false;
    memcpy(h, *buf, len);
    alloc->deallocate(alloc->user_data, *heap_buf);
    *heap_buf = h;
    *buf = h;
    *cap = new_cap;
    return true;
}

/* Parse one CSV field into *out_data / *out_len.
 * Uses stack_buf (stack_cap bytes) when the field fits; otherwise heap-allocates
 * into *heap_buf (caller must free if *heap_buf != NULL on return).
 * Returns false on allocation failure. */
static bool csv_parse_field_buf(const vigil_allocator_t *alloc, csv_reader_t *r, char *stack_buf, size_t stack_cap,
                                char **heap_buf, const char **out_data, size_t *out_len)
{
    char *buf = stack_buf;
    size_t cap = stack_cap - 1U;
    size_t len = 0;
    int quoted = (csv_peek(r) == '"');
    int c;

    *heap_buf = NULL;
    if (quoted)
        csv_next(r);

    while ((c = csv_peek(r)) != -1)
    {
        if (quoted)
        {
            if (c != '"')
            {
                csv_next(r);
            }
            else
            {
                csv_next(r);
                if (csv_peek(r) != '"')
                    break;
                csv_next(r);
            }
        }
        else
        {
            if (csv_is_field_end(c))
                break;
            csv_next(r);
        }

        if (len >= cap && !csv_field_spill(alloc, &buf, heap_buf, len, &cap))
            return false;
        buf[len++] = (char)c;
    }

    buf[len] = '\0';
    *out_data = buf;
    *out_len = len;
    return true;
}

/* ── csv.parse(data: string) -> array<array<string>> ─────────────── */

static vigil_status_t csv_parse(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t data_len;
    vigil_status_t s;
    vigil_object_t *rows_arr = NULL;
    vigil_value_t rows_val;

    if (!get_string_arg(vm, base, 0, &data, &data_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &rows_arr, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_value_init_object(&rows_val, &rows_arr);
        s = vigil_vm_stack_push(vm, &rows_val, error);
        vigil_value_release(&rows_val);
        return s;
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_allocator_t alloc = get_alloc(vm);

    csv_reader_t reader = {data, data_len, 0};

    /* Create outer array */
    s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &rows_arr, error);
    if (s != VIGIL_STATUS_OK)
        return s;

    while (csv_peek(&reader) != -1)
    {
        /* Create row array */
        vigil_object_t *row_arr = NULL;
        s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &row_arr, error);
        if (s != VIGIL_STATUS_OK)
            return s;

        /* Parse fields in row */
        int first = 1;
        while (csv_peek(&reader) != -1 && csv_peek(&reader) != '\r' && csv_peek(&reader) != '\n')
        {
            if (!first)
            {
                if (csv_peek(&reader) == ',')
                    csv_next(&reader);
            }
            first = 0;

            char field_stack[256];
            char *field_heap;
            const char *field;
            size_t field_len;
            if (!csv_parse_field_buf(&alloc, &reader, field_stack, sizeof(field_stack), &field_heap, &field,
                                     &field_len))
            {
                return VIGIL_STATUS_INTERNAL;
            }

            vigil_object_t *str_obj = NULL;
            s = vigil_string_object_new(vigil_vm_runtime(vm), field, field_len, &str_obj, error);
            alloc.deallocate(alloc.user_data, field_heap);
            if (s != VIGIL_STATUS_OK)
                return s;

            vigil_value_t str_val;
            vigil_value_init_object(&str_val, &str_obj);
            s = vigil_array_object_append(row_arr, &str_val, error);
            vigil_value_release(&str_val);
            if (s != VIGIL_STATUS_OK)
                return s;
        }

        /* Add row to rows */
        vigil_value_t row_val;
        vigil_value_init_object(&row_val, &row_arr);
        s = vigil_array_object_append(rows_arr, &row_val, error);
        vigil_value_release(&row_val);
        if (s != VIGIL_STATUS_OK)
            return s;

        csv_skip_crlf(&reader);
    }

    vigil_value_init_object(&rows_val, &rows_arr);
    s = vigil_vm_stack_push(vm, &rows_val, error);
    vigil_value_release(&rows_val);
    return s;
}

/* ── csv.parse_row(data: string) -> array<string> ────────────────── */

static vigil_status_t csv_parse_row(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t data_len;
    vigil_status_t s;
    vigil_object_t *row_arr = NULL;
    vigil_value_t row_val;

    if (!get_string_arg(vm, base, 0, &data, &data_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &row_arr, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_value_init_object(&row_val, &row_arr);
        s = vigil_vm_stack_push(vm, &row_val, error);
        vigil_value_release(&row_val);
        return s;
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_allocator_t alloc = get_alloc(vm);

    csv_reader_t reader = {data, data_len, 0};

    s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &row_arr, error);
    if (s != VIGIL_STATUS_OK)
        return s;

    int first = 1;
    while (csv_peek(&reader) != -1 && csv_peek(&reader) != '\r' && csv_peek(&reader) != '\n')
    {
        if (!first)
        {
            if (csv_peek(&reader) == ',')
                csv_next(&reader);
        }
        first = 0;

        char field_stack[256];
        char *field_heap;
        const char *field;
        size_t field_len;
        if (!csv_parse_field_buf(&alloc, &reader, field_stack, sizeof(field_stack), &field_heap, &field, &field_len))
        {
            return VIGIL_STATUS_INTERNAL;
        }

        vigil_object_t *str_obj = NULL;
        s = vigil_string_object_new(vigil_vm_runtime(vm), field, field_len, &str_obj, error);
        alloc.deallocate(alloc.user_data, field_heap);
        if (s != VIGIL_STATUS_OK)
            return s;

        vigil_value_t str_val;
        vigil_value_init_object(&str_val, &str_obj);
        s = vigil_array_object_append(row_arr, &str_val, error);
        vigil_value_release(&str_val);
        if (s != VIGIL_STATUS_OK)
            return s;
    }

    vigil_value_init_object(&row_val, &row_arr);
    s = vigil_vm_stack_push(vm, &row_val, error);
    vigil_value_release(&row_val);
    return s;
}

/* ── CSV Writer ──────────────────────────────────────────────────── */

/* Check if field needs quoting */
static int csv_needs_quote(const char *s, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        if (s[i] == ',' || s[i] == '"' || s[i] == '\r' || s[i] == '\n')
            return 1;
    }
    return 0;
}

/* Write a field to buffer, quoting if needed */
static void csv_write_field(const vigil_allocator_t *alloc, char **buf, size_t *cap, size_t *len, const char *field,
                            size_t field_len)
{
    int need_quote = csv_needs_quote(field, field_len);
    size_t needed = field_len + (need_quote ? 2 : 0);

    /* Count extra quotes needed */
    if (need_quote)
    {
        for (size_t i = 0; i < field_len; i++)
        {
            if (field[i] == '"')
                needed++;
        }
    }

    /* Grow buffer if needed */
    while (*len + needed + 1 > *cap)
    {
        *cap = *cap ? *cap * 2 : 256;
        *buf = (char *)alloc->reallocate(alloc->user_data, *buf, *cap);
    }

    if (need_quote)
        (*buf)[(*len)++] = '"';
    for (size_t i = 0; i < field_len; i++)
    {
        if (field[i] == '"')
            (*buf)[(*len)++] = '"';
        (*buf)[(*len)++] = field[i];
    }
    if (need_quote)
        (*buf)[(*len)++] = '"';
}

/* ── csv.stringify(rows: array<array<string>>) -> string ─────────── */

static vigil_status_t csv_stringify(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t rows_val = vigil_vm_stack_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (!vigil_nanbox_is_object(rows_val))
    {
        return push_string(vm, "", 0, error);
    }

    vigil_object_t *rows_arr = (vigil_object_t *)vigil_nanbox_decode_ptr(rows_val);
    if (!rows_arr || vigil_object_type(rows_arr) != VIGIL_OBJECT_ARRAY)
    {
        return push_string(vm, "", 0, error);
    }

    vigil_allocator_t alloc = get_alloc(vm);
    char *buf = NULL;
    size_t cap = 0, len = 0;
    size_t row_count = vigil_array_object_length(rows_arr);

    for (size_t r = 0; r < row_count; r++)
    {
        vigil_value_t row_val;
        vigil_value_init_nil(&row_val);
        if (!vigil_array_object_get(rows_arr, r, &row_val))
            continue;
        if (!vigil_nanbox_is_object(row_val))
        {
            vigil_value_release(&row_val);
            continue;
        }

        vigil_object_t *row_arr = (vigil_object_t *)vigil_nanbox_decode_ptr(row_val);
        if (!row_arr || vigil_object_type(row_arr) != VIGIL_OBJECT_ARRAY)
        {
            vigil_value_release(&row_val);
            continue;
        }

        size_t col_count = vigil_array_object_length(row_arr);
        for (size_t c = 0; c < col_count; c++)
        {
            if (c > 0)
            {
                if (len + 1 >= cap)
                {
                    cap = cap ? cap * 2 : 256;
                    buf = (char *)alloc.reallocate(alloc.user_data, buf, cap);
                }
                buf[len++] = ',';
            }

            vigil_value_t cell_val;
            vigil_value_init_nil(&cell_val);
            if (!vigil_array_object_get(row_arr, c, &cell_val))
                continue;
            if (vigil_nanbox_is_object(cell_val))
            {
                vigil_object_t *str_obj = (vigil_object_t *)vigil_nanbox_decode_ptr(cell_val);
                if (str_obj && vigil_object_type(str_obj) == VIGIL_OBJECT_STRING)
                {
                    const char *s = vigil_string_object_c_str(str_obj);
                    size_t slen = vigil_string_object_length(str_obj);
                    csv_write_field(&alloc, &buf, &cap, &len, s, slen);
                }
            }
            vigil_value_release(&cell_val);
        }
        vigil_value_release(&row_val);

        /* Add newline */
        if (len + 2 >= cap)
        {
            cap = cap ? cap * 2 : 256;
            buf = (char *)alloc.reallocate(alloc.user_data, buf, cap);
        }
        buf[len++] = '\r';
        buf[len++] = '\n';
    }

    vigil_status_t s = push_string(vm, buf ? buf : "", len, error);
    alloc.deallocate(alloc.user_data, buf);
    return s;
}

/* ── csv.stringify_row(row: array<string>) -> string ─────────────── */

static vigil_status_t csv_stringify_row(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t row_val = vigil_vm_stack_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (!vigil_nanbox_is_object(row_val))
    {
        return push_string(vm, "", 0, error);
    }

    vigil_object_t *row_arr = (vigil_object_t *)vigil_nanbox_decode_ptr(row_val);
    if (!row_arr || vigil_object_type(row_arr) != VIGIL_OBJECT_ARRAY)
    {
        return push_string(vm, "", 0, error);
    }

    vigil_allocator_t alloc = get_alloc(vm);
    char *buf = NULL;
    size_t cap = 0, len = 0;
    size_t col_count = vigil_array_object_length(row_arr);

    for (size_t c = 0; c < col_count; c++)
    {
        if (c > 0)
        {
            if (len + 1 >= cap)
            {
                cap = cap ? cap * 2 : 256;
                buf = (char *)alloc.reallocate(alloc.user_data, buf, cap);
            }
            buf[len++] = ',';
        }

        vigil_value_t cell_val;
        vigil_value_init_nil(&cell_val);
        if (!vigil_array_object_get(row_arr, c, &cell_val))
            continue;
        if (vigil_nanbox_is_object(cell_val))
        {
            vigil_object_t *str_obj = (vigil_object_t *)vigil_nanbox_decode_ptr(cell_val);
            if (str_obj && vigil_object_type(str_obj) == VIGIL_OBJECT_STRING)
            {
                const char *s = vigil_string_object_c_str(str_obj);
                size_t slen = vigil_string_object_length(str_obj);
                csv_write_field(&alloc, &buf, &cap, &len, s, slen);
            }
        }
        vigil_value_release(&cell_val);
    }

    vigil_status_t s = push_string(vm, buf ? buf : "", len, error);
    alloc.deallocate(alloc.user_data, buf);
    return s;
}

/* ── Module definition ───────────────────────────────────────────── */

static const int str_param[] = {VIGIL_TYPE_STRING};
static const int arr_param[] = {VIGIL_TYPE_OBJECT};

static const vigil_native_type_t array_array_string_ret = VIGIL_NATIVE_TYPE_ARRAY(VIGIL_TYPE_OBJECT);
static const vigil_native_type_t array_string_ret = VIGIL_NATIVE_TYPE_ARRAY(VIGIL_TYPE_STRING);
static const vigil_native_type_t array_array_string_param = VIGIL_NATIVE_TYPE_ARRAY(VIGIL_TYPE_OBJECT);
static const vigil_native_type_t array_string_param = VIGIL_NATIVE_TYPE_ARRAY(VIGIL_TYPE_STRING);

static const vigil_native_module_function_t csv_functions[] = {
    {"parse", 5U, csv_parse, 1U, str_param, VIGIL_TYPE_OBJECT, 1U, NULL, 0, NULL, &array_array_string_ret},
    {"parse_row", 9U, csv_parse_row, 1U, str_param, VIGIL_TYPE_OBJECT, 1U, NULL, 0, NULL, &array_string_ret},
    {"stringify", 9U, csv_stringify, 1U, arr_param, VIGIL_TYPE_STRING, 1U, NULL, 0, &array_array_string_param, NULL},
    {"stringify_row", 13U, csv_stringify_row, 1U, arr_param, VIGIL_TYPE_STRING, 1U, NULL, 0, &array_string_param, NULL},
};

#define CSV_FUNCTION_COUNT (sizeof(csv_functions) / sizeof(csv_functions[0]))

VIGIL_API const vigil_native_module_t vigil_stdlib_csv = {"csv", 3U, csv_functions, CSV_FUNCTION_COUNT, NULL, 0U};
