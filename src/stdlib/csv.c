/* VIGIL standard library: csv module.
 *
 * RFC 4180 compliant CSV parsing and generation with custom delimiters,
 * header-based parsing, file I/O, and inspection helpers.
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
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

/* ── Stack helpers ───────────────────────────────────────────────── */

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

static vigil_status_t push_i32(vigil_vm_t *vm, int32_t val, vigil_error_t *error)
{
    vigil_value_t v;
    vigil_value_init_int(&v, (int64_t)val);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t push_bool(vigil_vm_t *vm, bool b, vigil_error_t *error)
{
    vigil_value_t v;
    vigil_value_init_bool(&v, b);
    return vigil_vm_stack_push(vm, &v, error);
}

/* ── Multi-return helpers ────────────────────────────────────────── */

static vigil_status_t push_obj_and_ok(vigil_vm_t *vm, vigil_object_t **obj, vigil_error_t *error)
{
    vigil_value_t v;
    vigil_value_init_object(&v, obj);
    vigil_status_t s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t push_obj_and_err(vigil_vm_t *vm, vigil_object_t **empty, const char *msg, vigil_error_t *error)
{
    vigil_value_t v;
    vigil_value_init_object(&v, empty);
    vigil_status_t s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_object_t *err_obj = NULL;
    s = vigil_error_object_new_cstr(vigil_vm_runtime(vm), msg, 1, &err_obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_init_object(&v, &err_obj);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

static vigil_status_t push_str_and_ok(vigil_vm_t *vm, const char *str, size_t len, vigil_error_t *error)
{
    vigil_status_t s = push_string(vm, str, len, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t push_str_and_err(vigil_vm_t *vm, const char *msg, vigil_error_t *error)
{
    vigil_status_t s = push_string(vm, "", 0, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_object_t *err_obj = NULL;
    s = vigil_error_object_new_cstr(vigil_vm_runtime(vm), msg, 1, &err_obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t v;
    vigil_value_init_object(&v, &err_obj);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

static vigil_status_t push_err_only(vigil_vm_t *vm, const char *msg, vigil_error_t *error)
{
    vigil_object_t *err_obj = NULL;
    vigil_status_t s = vigil_error_object_new_cstr(vigil_vm_runtime(vm), msg, 1, &err_obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t v;
    vigil_value_init_object(&v, &err_obj);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

static vigil_status_t push_ok_only(vigil_vm_t *vm, vigil_error_t *error)
{
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t push_i32_and_ok(vigil_vm_t *vm, int32_t val, vigil_error_t *error)
{
    vigil_status_t s = push_i32(vm, val, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t push_i32_and_err(vigil_vm_t *vm, const char *msg, vigil_error_t *error)
{
    vigil_status_t s = push_i32(vm, 0, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_object_t *err_obj = NULL;
    s = vigil_error_object_new_cstr(vigil_vm_runtime(vm), msg, 1, &err_obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t v;
    vigil_value_init_object(&v, &err_obj);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

/* ── Delimiter validation ────────────────────────────────────────── */

static bool csv_valid_delimiter(const char *delim, size_t len)
{
    if (len != 1)
        return false;
    char c = delim[0];
    return c != '"' && c != '\r' && c != '\n' && c != '\0';
}

/* ── CSV Reader ──────────────────────────────────────────────────── */

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

/* ── Field parsing ───────────────────────────────────────────────── */

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

static bool csv_parse_field_buf(const vigil_allocator_t *alloc, csv_reader_t *r, char delim, char *stack_buf,
                                size_t stack_cap, char **heap_buf, const char **out_data, size_t *out_len)
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
            if (c == (unsigned char)delim || c == '\r' || c == '\n')
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

/* ── CSV Writer helpers ──────────────────────────────────────────── */

static int csv_needs_quote(const char *s, size_t len, char delim)
{
    for (size_t i = 0; i < len; i++)
    {
        if (s[i] == delim || s[i] == '"' || s[i] == '\r' || s[i] == '\n')
            return 1;
    }
    return 0;
}

static void csv_write_field(const vigil_allocator_t *alloc, char **buf, size_t *cap, size_t *len, const char *field,
                            size_t field_len, char delim)
{
    int need_quote = csv_needs_quote(field, field_len, delim);
    size_t needed = field_len + (need_quote ? 2 : 0);

    if (need_quote)
    {
        for (size_t i = 0; i < field_len; i++)
        {
            if (field[i] == '"')
                needed++;
        }
    }

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

/* ── File I/O helpers ────────────────────────────────────────────── */

static vigil_status_t csv_read_file_data(const vigil_allocator_t *alloc, const char *path, char **out_data,
                                         size_t *out_len, const char **out_msg, vigil_error_t *error)
{
    FILE *file = NULL;
    *out_data = NULL;
    *out_len = 0;
    *out_msg = NULL;

#ifdef _WIN32
    {
        errno_t open_status = fopen_s(&file, path, "rb");
        if (open_status != 0)
            file = NULL;
    }
#else
    file = fopen(path, "rb");
#endif
    if (!file)
    {
        *out_msg = "read_file: could not open file";
        return VIGIL_STATUS_OK;
    }
    if (fseek(file, 0L, SEEK_END) != 0)
    {
        *out_msg = "read_file: could not seek file";
        fclose(file);
        return VIGIL_STATUS_OK;
    }
    long sz = ftell(file);
    if (sz < 0)
    {
        *out_msg = "read_file: could not size file";
        fclose(file);
        return VIGIL_STATUS_OK;
    }
    if (fseek(file, 0L, SEEK_SET) != 0)
    {
        *out_msg = "read_file: could not rewind file";
        fclose(file);
        return VIGIL_STATUS_OK;
    }
    size_t size = (size_t)sz;
    char *data = (char *)alloc->allocate(alloc->user_data, size + 1U);
    if (!data)
    {
        fclose(file);
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "csv read_file: allocation failed");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    size_t nread = fread(data, 1U, size, file);
    int ferr = ferror(file);
    fclose(file);
    if (nread != size && ferr)
    {
        alloc->deallocate(alloc->user_data, data);
        *out_msg = "read_file: could not read file";
        return VIGIL_STATUS_OK;
    }
    data[nread] = '\0';
    *out_data = data;
    *out_len = nread;
    return VIGIL_STATUS_OK;
}

static vigil_status_t csv_write_file_data(const char *path, const char *text, size_t length, const char **out_msg)
{
    FILE *file = NULL;
    *out_msg = NULL;

#ifdef _WIN32
    {
        errno_t open_status = fopen_s(&file, path, "wb");
        if (open_status != 0)
            file = NULL;
    }
#else
    file = fopen(path, "wb");
#endif
    if (!file)
    {
        *out_msg = "write_file: could not open file";
        return VIGIL_STATUS_OK;
    }
    size_t nw = fwrite(text, 1U, length, file);
    if (nw != length)
    {
        fclose(file);
        *out_msg = "write_file: could not write file";
        return VIGIL_STATUS_OK;
    }
    if (fclose(file) != 0)
    {
        *out_msg = "write_file: could not close file";
        return VIGIL_STATUS_OK;
    }
    return VIGIL_STATUS_OK;
}

/* ── Internal: parse CSV data into rows array ────────────────────── */

static vigil_status_t csv_parse_rows(vigil_vm_t *vm, const char *data, size_t data_len, char delim,
                                     vigil_object_t **out_rows, vigil_error_t *error)
{
    vigil_allocator_t alloc = get_alloc(vm);
    csv_reader_t reader = {data, data_len, 0};
    vigil_status_t s;

    s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, out_rows, error);
    if (s != VIGIL_STATUS_OK)
        return s;

    while (csv_peek(&reader) != -1)
    {
        vigil_object_t *row_arr = NULL;
        s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &row_arr, error);
        if (s != VIGIL_STATUS_OK)
            return s;

        int first = 1;
        while (csv_peek(&reader) != -1 && csv_peek(&reader) != '\r' && csv_peek(&reader) != '\n')
        {
            if (!first)
            {
                if (csv_peek(&reader) == (unsigned char)delim)
                    csv_next(&reader);
            }
            first = 0;

            char field_stack[256];
            char *field_heap;
            const char *field;
            size_t field_len;
            if (!csv_parse_field_buf(&alloc, &reader, delim, field_stack, sizeof(field_stack), &field_heap, &field,
                                     &field_len))
                return VIGIL_STATUS_INTERNAL;

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

        vigil_value_t row_val;
        vigil_value_init_object(&row_val, &row_arr);
        s = vigil_array_object_append(*out_rows, &row_val, error);
        vigil_value_release(&row_val);
        if (s != VIGIL_STATUS_OK)
            return s;

        csv_skip_crlf(&reader);
    }

    return VIGIL_STATUS_OK;
}

/* ── Internal: parse single row ──────────────────────────────────── */

static vigil_status_t csv_parse_single_row(vigil_vm_t *vm, const char *data, size_t data_len, char delim,
                                           vigil_object_t **out_row, vigil_error_t *error)
{
    vigil_allocator_t alloc = get_alloc(vm);
    csv_reader_t reader = {data, data_len, 0};
    vigil_status_t s;

    s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, out_row, error);
    if (s != VIGIL_STATUS_OK)
        return s;

    int first = 1;
    while (csv_peek(&reader) != -1 && csv_peek(&reader) != '\r' && csv_peek(&reader) != '\n')
    {
        if (!first)
        {
            if (csv_peek(&reader) == (unsigned char)delim)
                csv_next(&reader);
        }
        first = 0;

        char field_stack[256];
        char *field_heap;
        const char *field;
        size_t field_len;
        if (!csv_parse_field_buf(&alloc, &reader, delim, field_stack, sizeof(field_stack), &field_heap, &field,
                                 &field_len))
            return VIGIL_STATUS_INTERNAL;

        vigil_object_t *str_obj = NULL;
        s = vigil_string_object_new(vigil_vm_runtime(vm), field, field_len, &str_obj, error);
        alloc.deallocate(alloc.user_data, field_heap);
        if (s != VIGIL_STATUS_OK)
            return s;

        vigil_value_t str_val;
        vigil_value_init_object(&str_val, &str_obj);
        s = vigil_array_object_append(*out_row, &str_val, error);
        vigil_value_release(&str_val);
        if (s != VIGIL_STATUS_OK)
            return s;
    }

    return VIGIL_STATUS_OK;
}

/* ── Internal: stringify rows to buffer ──────────────────────────── */

static vigil_status_t csv_stringify_rows_buf(vigil_vm_t *vm, vigil_object_t *rows_arr, char delim, char **out_buf,
                                             size_t *out_len, vigil_error_t *error)
{
    (void)error;
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
                buf[len++] = delim;
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
                    csv_write_field(&alloc, &buf, &cap, &len, s, slen, delim);
                }
            }
            vigil_value_release(&cell_val);
        }
        vigil_value_release(&row_val);

        /* CRLF line ending per RFC 4180 */
        if (len + 2 >= cap)
        {
            cap = cap ? cap * 2 : 256;
            buf = (char *)alloc.reallocate(alloc.user_data, buf, cap);
        }
        buf[len++] = '\r';
        buf[len++] = '\n';
    }

    *out_buf = buf;
    *out_len = len;
    return VIGIL_STATUS_OK;
}

/* ── Internal: stringify single row ──────────────────────────────── */

static vigil_status_t csv_stringify_single_row_buf(vigil_vm_t *vm, vigil_object_t *row_arr, char delim, char **out_buf,
                                                   size_t *out_len, vigil_error_t *error)
{
    (void)error;
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
            buf[len++] = delim;
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
                csv_write_field(&alloc, &buf, &cap, &len, s, slen, delim);
            }
        }
        vigil_value_release(&cell_val);
    }

    *out_buf = buf;
    *out_len = len;
    return VIGIL_STATUS_OK;
}

/* ══════════════════════════════════════════════════════════════════
 *  Public API functions
 * ══════════════════════════════════════════════════════════════════ */

/* ── csv.parse(data, delimiter) -> (array<array<string>>, err) ──── */

static vigil_status_t csv_parse(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data, *delim_str;
    size_t data_len, delim_len;

    if (!get_string_arg(vm, base, 0, &data, &data_len) ||
        !get_string_arg(vm, base, 1, &delim_str, &delim_len) ||
        !csv_valid_delimiter(delim_str, delim_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_object_t *empty = NULL;
        vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &empty, error);
        if (s != VIGIL_STATUS_OK) return s;
        return push_obj_and_err(vm, &empty, "parse: invalid arguments or delimiter", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_object_t *rows = NULL;
    vigil_status_t s = csv_parse_rows(vm, data, data_len, delim_str[0], &rows, error);
    if (s != VIGIL_STATUS_OK) return s;
    return push_obj_and_ok(vm, &rows, error);
}

/* ── csv.parse_row(line, delimiter) -> (array<string>, err) ────── */

static vigil_status_t csv_parse_row(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data, *delim_str;
    size_t data_len, delim_len;

    if (!get_string_arg(vm, base, 0, &data, &data_len) ||
        !get_string_arg(vm, base, 1, &delim_str, &delim_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_object_t *empty = NULL;
        vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &empty, error);
        if (s != VIGIL_STATUS_OK) return s;
        return push_obj_and_err(vm, &empty, "parse_row: invalid arguments", error);
    }
    if (!csv_valid_delimiter(delim_str, delim_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_object_t *empty = NULL;
        vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &empty, error);
        if (s != VIGIL_STATUS_OK) return s;
        return push_obj_and_err(vm, &empty, "parse_row: delimiter must be a single character", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_object_t *row = NULL;
    vigil_status_t s = csv_parse_single_row(vm, data, data_len, delim_str[0], &row, error);
    if (s != VIGIL_STATUS_OK) return s;
    return push_obj_and_ok(vm, &row, error);
}

/* ── csv.stringify(rows, delimiter) -> (string, err) ──────────────── */

static vigil_status_t csv_stringify(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t rows_val = vigil_vm_stack_get(vm, base);
    const char *delim_str;
    size_t delim_len;

    if (!get_string_arg(vm, base, 1, &delim_str, &delim_len) ||
        !csv_valid_delimiter(delim_str, delim_len) ||
        !vigil_nanbox_is_object(rows_val))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "stringify: invalid arguments", error);
    }

    vigil_object_t *rows_arr = (vigil_object_t *)vigil_nanbox_decode_ptr(rows_val);
    if (!rows_arr || vigil_object_type(rows_arr) != VIGIL_OBJECT_ARRAY)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "stringify: expected array", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    char *buf = NULL;
    size_t len = 0;
    vigil_status_t s = csv_stringify_rows_buf(vm, rows_arr, delim_str[0], &buf, &len, error);
    if (s != VIGIL_STATUS_OK) return s;

    s = push_str_and_ok(vm, buf ? buf : "", len, error);
    vigil_allocator_t alloc = get_alloc(vm);
    alloc.deallocate(alloc.user_data, buf);
    return s;
}

/* ── csv.stringify_row(row, delimiter) -> (string, err) ──────────── */

static vigil_status_t csv_stringify_row(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t row_val = vigil_vm_stack_get(vm, base);
    const char *delim_str;
    size_t delim_len;

    if (!get_string_arg(vm, base, 1, &delim_str, &delim_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "stringify_row: invalid arguments", error);
    }
    if (!csv_valid_delimiter(delim_str, delim_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "stringify_row: delimiter must be a single character", error);
    }
    if (!vigil_nanbox_is_object(row_val))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "stringify_row: expected array", error);
    }

    vigil_object_t *row_arr = (vigil_object_t *)vigil_nanbox_decode_ptr(row_val);
    if (!row_arr || vigil_object_type(row_arr) != VIGIL_OBJECT_ARRAY)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "stringify_row: expected array", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    char *buf = NULL;
    size_t len = 0;
    vigil_status_t s = csv_stringify_single_row_buf(vm, row_arr, delim_str[0], &buf, &len, error);
    if (s != VIGIL_STATUS_OK) return s;

    s = push_str_and_ok(vm, buf ? buf : "", len, error);
    vigil_allocator_t alloc = get_alloc(vm);
    alloc.deallocate(alloc.user_data, buf);
    return s;
}

/* ── csv.read_file(path, delimiter) -> (array<array<string>>, err) ── */

static vigil_status_t csv_read_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path, *delim_str;
    size_t path_len, delim_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len) ||
        !get_string_arg(vm, base, 1, &delim_str, &delim_len) ||
        !csv_valid_delimiter(delim_str, delim_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_object_t *empty = NULL;
        vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &empty, error);
        if (s != VIGIL_STATUS_OK) return s;
        return push_obj_and_err(vm, &empty, "read_file: invalid arguments", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_allocator_t alloc = get_alloc(vm);
    char *data = NULL;
    size_t data_len = 0;
    const char *msg = NULL;
    vigil_status_t s = csv_read_file_data(&alloc, path, &data, &data_len, &msg, error);
    if (s != VIGIL_STATUS_OK) return s;
    if (msg)
    {
        vigil_object_t *empty = NULL;
        s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &empty, error);
        if (s != VIGIL_STATUS_OK) return s;
        return push_obj_and_err(vm, &empty, msg, error);
    }

    vigil_object_t *rows = NULL;
    s = csv_parse_rows(vm, data, data_len, delim_str[0], &rows, error);
    alloc.deallocate(alloc.user_data, data);
    if (s != VIGIL_STATUS_OK) return s;
    return push_obj_and_ok(vm, &rows, error);
}

/* ── csv.write_file(path, rows, delimiter) -> err ────────────────── */

static vigil_status_t csv_write_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path, *delim_str;
    size_t path_len, delim_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len) ||
        !get_string_arg(vm, base, 2, &delim_str, &delim_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_err_only(vm, "write_file: invalid arguments", error);
    }
    if (!csv_valid_delimiter(delim_str, delim_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_err_only(vm, "write_file: delimiter must be a single character", error);
    }

    vigil_value_t rows_val = vigil_vm_stack_get(vm, base + 1);
    if (!vigil_nanbox_is_object(rows_val))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_err_only(vm, "write_file: expected array", error);
    }
    vigil_object_t *rows_arr = (vigil_object_t *)vigil_nanbox_decode_ptr(rows_val);
    if (!rows_arr || vigil_object_type(rows_arr) != VIGIL_OBJECT_ARRAY)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_err_only(vm, "write_file: expected array", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    char *buf = NULL;
    size_t len = 0;
    vigil_status_t s = csv_stringify_rows_buf(vm, rows_arr, delim_str[0], &buf, &len, error);
    if (s != VIGIL_STATUS_OK) return s;

    const char *msg = NULL;
    s = csv_write_file_data(path, buf ? buf : "", len, &msg);
    vigil_allocator_t alloc = get_alloc(vm);
    alloc.deallocate(alloc.user_data, buf);
    if (s != VIGIL_STATUS_OK) return s;
    if (msg) return push_err_only(vm, msg, error);
    return push_ok_only(vm, error);
}

/* ── csv.parse_with_header(data, delim) -> array<map<string,string>> */


/* ── csv.parse_with_header(data, delim) -> (array<map<string,string>>, err) */

static vigil_status_t csv_parse_with_header(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data, *delim_str;
    size_t data_len, delim_len;

    if (!get_string_arg(vm, base, 0, &data, &data_len) ||
        !get_string_arg(vm, base, 1, &delim_str, &delim_len) ||
        !csv_valid_delimiter(delim_str, delim_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_object_t *empty = NULL;
        vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &empty, error);
        if (s != VIGIL_STATUS_OK) return s;
        return push_obj_and_err(vm, &empty, "parse_with_header: invalid arguments", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_object_t *rows = NULL;
    vigil_status_t s = csv_parse_rows(vm, data, data_len, delim_str[0], &rows, error);
    if (s != VIGIL_STATUS_OK) return s;

    size_t row_count = vigil_array_object_length(rows);
    if (row_count < 1)
    {
        vigil_object_t *result = NULL;
        s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &result, error);
        if (s != VIGIL_STATUS_OK) return s;
        return push_obj_and_ok(vm, &result, error);
    }

    vigil_value_t header_val;
    vigil_value_init_nil(&header_val);
    vigil_array_object_get(rows, 0, &header_val);
    vigil_object_t *header_arr = (vigil_object_t *)vigil_nanbox_decode_ptr(header_val);
    size_t col_count = vigil_array_object_length(header_arr);

    vigil_object_t *result = NULL;
    s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &result, error);
    if (s != VIGIL_STATUS_OK) { vigil_value_release(&header_val); return s; }

    for (size_t r = 1; r < row_count; r++)
    {
        vigil_value_t row_val;
        vigil_value_init_nil(&row_val);
        vigil_array_object_get(rows, r, &row_val);
        vigil_object_t *row_arr = (vigil_object_t *)vigil_nanbox_decode_ptr(row_val);
        size_t this_cols = vigil_array_object_length(row_arr);

        vigil_object_t *map = NULL;
        s = vigil_map_object_new(vigil_vm_runtime(vm), &map, error);
        if (s != VIGIL_STATUS_OK) { vigil_value_release(&row_val); vigil_value_release(&header_val); return s; }

        for (size_t c = 0; c < col_count; c++)
        {
            vigil_value_t key_val;
            vigil_value_init_nil(&key_val);
            vigil_array_object_get(header_arr, c, &key_val);

            vigil_value_t cell_val;
            vigil_value_init_nil(&cell_val);
            if (c < this_cols)
                vigil_array_object_get(row_arr, c, &cell_val);
            else
            {
                vigil_object_t *empty_str = NULL;
                s = vigil_string_object_new(vigil_vm_runtime(vm), "", 0, &empty_str, error);
                if (s != VIGIL_STATUS_OK) { vigil_value_release(&key_val); vigil_value_release(&row_val); vigil_value_release(&header_val); return s; }
                vigil_value_init_object(&cell_val, &empty_str);
            }

            s = vigil_map_object_set(map, &key_val, &cell_val, error);
            vigil_value_release(&key_val);
            vigil_value_release(&cell_val);
            if (s != VIGIL_STATUS_OK) { vigil_value_release(&row_val); vigil_value_release(&header_val); return s; }
        }

        vigil_value_t map_val;
        vigil_value_init_object(&map_val, &map);
        s = vigil_array_object_append(result, &map_val, error);
        vigil_value_release(&map_val);
        vigil_value_release(&row_val);
        if (s != VIGIL_STATUS_OK) { vigil_value_release(&header_val); return s; }
    }

    vigil_value_release(&header_val);
    return push_obj_and_ok(vm, &result, error);
}

/* ── csv.read_file_with_header(path, delim) -> (array<map<string,string>>, err) */

static vigil_status_t csv_read_file_with_header(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path, *delim_str;
    size_t path_len, delim_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len) ||
        !get_string_arg(vm, base, 1, &delim_str, &delim_len) ||
        !csv_valid_delimiter(delim_str, delim_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_object_t *empty = NULL;
        vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &empty, error);
        if (s != VIGIL_STATUS_OK) return s;
        return push_obj_and_err(vm, &empty, "read_file_with_header: invalid arguments", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_allocator_t alloc = get_alloc(vm);
    char *data = NULL;
    size_t data_len = 0;
    const char *msg = NULL;
    vigil_status_t s = csv_read_file_data(&alloc, path, &data, &data_len, &msg, error);
    if (s != VIGIL_STATUS_OK) return s;
    if (msg)
    {
        vigil_object_t *empty = NULL;
        s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &empty, error);
        if (s != VIGIL_STATUS_OK) return s;
        return push_obj_and_err(vm, &empty, msg, error);
    }

    vigil_object_t *rows = NULL;
    s = csv_parse_rows(vm, data, data_len, delim_str[0], &rows, error);
    alloc.deallocate(alloc.user_data, data);
    if (s != VIGIL_STATUS_OK) return s;

    size_t row_count = vigil_array_object_length(rows);
    if (row_count < 1)
    {
        vigil_object_t *result = NULL;
        s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &result, error);
        if (s != VIGIL_STATUS_OK) return s;
        return push_obj_and_ok(vm, &result, error);
    }

    vigil_value_t header_val;
    vigil_value_init_nil(&header_val);
    vigil_array_object_get(rows, 0, &header_val);
    vigil_object_t *header_arr = (vigil_object_t *)vigil_nanbox_decode_ptr(header_val);
    size_t col_count = vigil_array_object_length(header_arr);

    vigil_object_t *result = NULL;
    s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &result, error);
    if (s != VIGIL_STATUS_OK) { vigil_value_release(&header_val); return s; }

    for (size_t r = 1; r < row_count; r++)
    {
        vigil_value_t row_val;
        vigil_value_init_nil(&row_val);
        vigil_array_object_get(rows, r, &row_val);
        vigil_object_t *row_arr = (vigil_object_t *)vigil_nanbox_decode_ptr(row_val);
        size_t this_cols = vigil_array_object_length(row_arr);

        vigil_object_t *map = NULL;
        s = vigil_map_object_new(vigil_vm_runtime(vm), &map, error);
        if (s != VIGIL_STATUS_OK) { vigil_value_release(&row_val); vigil_value_release(&header_val); return s; }

        for (size_t c = 0; c < col_count; c++)
        {
            vigil_value_t key_val;
            vigil_value_init_nil(&key_val);
            vigil_array_object_get(header_arr, c, &key_val);

            vigil_value_t cell_val;
            vigil_value_init_nil(&cell_val);
            if (c < this_cols)
                vigil_array_object_get(row_arr, c, &cell_val);
            else
            {
                vigil_object_t *empty_str = NULL;
                s = vigil_string_object_new(vigil_vm_runtime(vm), "", 0, &empty_str, error);
                if (s != VIGIL_STATUS_OK) { vigil_value_release(&key_val); vigil_value_release(&row_val); vigil_value_release(&header_val); return s; }
                vigil_value_init_object(&cell_val, &empty_str);
            }

            s = vigil_map_object_set(map, &key_val, &cell_val, error);
            vigil_value_release(&key_val);
            vigil_value_release(&cell_val);
            if (s != VIGIL_STATUS_OK) { vigil_value_release(&row_val); vigil_value_release(&header_val); return s; }
        }

        vigil_value_t map_val;
        vigil_value_init_object(&map_val, &map);
        s = vigil_array_object_append(result, &map_val, error);
        vigil_value_release(&map_val);
        vigil_value_release(&row_val);
        if (s != VIGIL_STATUS_OK) { vigil_value_release(&header_val); return s; }
    }

    vigil_value_release(&header_val);
    return push_obj_and_ok(vm, &result, error);
}
static vigil_status_t csv_field_count(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data, *delim_str;
    size_t data_len, delim_len;

    if (!get_string_arg(vm, base, 0, &data, &data_len) ||
        !get_string_arg(vm, base, 1, &delim_str, &delim_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i32_and_err(vm, "field_count: invalid arguments", error);
    }
    if (!csv_valid_delimiter(delim_str, delim_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i32_and_err(vm, "field_count: delimiter must be a single character", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (data_len == 0)
        return push_i32_and_ok(vm, 0, error);

    /* Parse just the first row to count fields */
    vigil_object_t *row = NULL;
    vigil_status_t s = csv_parse_single_row(vm, data, data_len, delim_str[0], &row, error);
    if (s != VIGIL_STATUS_OK) return s;

    int32_t count = (int32_t)vigil_array_object_length(row);
    vigil_object_release(&row);
    return push_i32_and_ok(vm, count, error);
}

/* ── csv.row_count(data) -> i32 ──────────────────────────────────── */

static vigil_status_t csv_row_count(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t data_len;

    if (!get_string_arg(vm, base, 0, &data, &data_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i32(vm, 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    /* Count rows by scanning for line endings, respecting quoted fields */
    csv_reader_t reader = {data, data_len, 0};
    int32_t count = 0;

    while (csv_peek(&reader) != -1)
    {
        /* Skip all fields in this row */
        int first = 1;
        while (csv_peek(&reader) != -1 && csv_peek(&reader) != '\r' && csv_peek(&reader) != '\n')
        {
            if (!first && csv_peek(&reader) == ',')
                csv_next(&reader);
            first = 0;

            /* Skip one field */
            if (csv_peek(&reader) == '"')
            {
                csv_next(&reader);
                while (csv_peek(&reader) != -1)
                {
                    if (csv_peek(&reader) == '"')
                    {
                        csv_next(&reader);
                        if (csv_peek(&reader) != '"')
                            break;
                    }
                    csv_next(&reader);
                }
            }
            else
            {
                while (csv_peek(&reader) != -1 && csv_peek(&reader) != ',' && csv_peek(&reader) != '\r' &&
                       csv_peek(&reader) != '\n')
                    csv_next(&reader);
            }
        }
        count++;
        csv_skip_crlf(&reader);
    }

    return push_i32(vm, count, error);
}

/* ── csv.has_header(data, delimiter) -> bool ──────────────────────── */

static vigil_status_t csv_has_header(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data, *delim_str;
    size_t data_len, delim_len;

    if (!get_string_arg(vm, base, 0, &data, &data_len) ||
        !get_string_arg(vm, base, 1, &delim_str, &delim_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, false, error);
    }
    if (!csv_valid_delimiter(delim_str, delim_len) || data_len == 0)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, false, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    /* Heuristic: first row is a header if all fields are non-empty,
     * none are purely numeric, and there are at least 2 rows */
    vigil_object_t *rows = NULL;
    vigil_status_t s = csv_parse_rows(vm, data, data_len, delim_str[0], &rows, error);
    if (s != VIGIL_STATUS_OK) return s;

    size_t row_count = vigil_array_object_length(rows);
    if (row_count < 2)
    {
        vigil_object_release(&rows);
        return push_bool(vm, false, error);
    }

    vigil_value_t header_val;
    vigil_value_init_nil(&header_val);
    vigil_array_object_get(rows, 0, &header_val);
    vigil_object_t *header_arr = (vigil_object_t *)vigil_nanbox_decode_ptr(header_val);
    size_t col_count = vigil_array_object_length(header_arr);

    if (col_count == 0)
    {
        vigil_value_release(&header_val);
        vigil_object_release(&rows);
        return push_bool(vm, false, error);
    }

    bool looks_like_header = true;
    for (size_t c = 0; c < col_count && looks_like_header; c++)
    {
        vigil_value_t cell;
        vigil_value_init_nil(&cell);
        vigil_array_object_get(header_arr, c, &cell);
        vigil_object_t *str_obj = (vigil_object_t *)vigil_nanbox_decode_ptr(cell);
        const char *field = vigil_string_object_c_str(str_obj);
        size_t flen = vigil_string_object_length(str_obj);

        /* Empty field — not a header */
        if (flen == 0)
            looks_like_header = false;

        /* Purely numeric — not a header */
        if (looks_like_header)
        {
            bool all_digit = true;
            bool has_dot = false;
            for (size_t i = 0; i < flen; i++)
            {
                if (field[i] == '.' && !has_dot)
                    has_dot = true;
                else if (!isdigit((unsigned char)field[i]) && !(i == 0 && field[i] == '-'))
                {
                    all_digit = false;
                    break;
                }
            }
            if (all_digit && flen > 0)
                looks_like_header = false;
        }

        vigil_value_release(&cell);
    }

    vigil_value_release(&header_val);
    vigil_object_release(&rows);
    return push_bool(vm, looks_like_header, error);
}

/* ══════════════════════════════════════════════════════════════════
 *  Module registration
 * ══════════════════════════════════════════════════════════════════ */

/* ── Parameter type arrays ───────────────────────────────────────── */

static const int str_str_param[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int obj_str_param[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_STRING};
static const int str_obj_str_param[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_OBJECT, VIGIL_TYPE_STRING};
static const int str_param[] = {VIGIL_TYPE_STRING};

/* ── Return type arrays ──────────────────────────────────────────── */

static const int obj_err_returns[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_ERR};
static const int str_err_returns[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_ERR};
static const int i32_err_returns[] = {VIGIL_TYPE_I32, VIGIL_TYPE_ERR};

/* ── Extended type info ──────────────────────────────────────────── */

static const vigil_native_type_t array_string_type = VIGIL_NATIVE_TYPE_ARRAY(VIGIL_TYPE_STRING);
static const vigil_native_type_t array_array_string_ret = {
    VIGIL_TYPE_OBJECT, 4, VIGIL_TYPE_OBJECT, 0, 0, &array_string_type
};
static const vigil_native_type_t array_string_ret = VIGIL_NATIVE_TYPE_ARRAY(VIGIL_TYPE_STRING);
static const vigil_native_type_t map_ss_type = VIGIL_NATIVE_TYPE_MAP(VIGIL_TYPE_STRING, VIGIL_TYPE_STRING);
static const vigil_native_type_t array_map_ss_ret = {
    VIGIL_TYPE_OBJECT, 4, VIGIL_TYPE_OBJECT, 0, 0, &map_ss_type
};

/* Extended param types for stringify_row(row, delimiter) */
static const vigil_native_type_t stringify_row_param_ext[] = {
    VIGIL_NATIVE_TYPE_ARRAY(VIGIL_TYPE_STRING), /* array<string> */
    VIGIL_NATIVE_TYPE_PRIMITIVE(VIGIL_TYPE_STRING),
};

/* ── Parameter name arrays ───────────────────────────────────────── */

static const char *const pn_data_delim[] = {"data", "delimiter"};
static const char *const pn_line_delim[] = {"line", "delimiter"};
static const char *const pn_rows_delim[] = {"rows", "delimiter"};
static const char *const pn_row_delim[] = {"row", "delimiter"};
static const char *const pn_path_delim[] = {"path", "delimiter"};
static const char *const pn_path_rows_delim[] = {"path", "rows", "delimiter"};
static const char *const pn_data[] = {"data"};

/* ── Parameter type name arrays ──────────────────────────────────── */

static const char *const tn_rows_delim[] = {"array<array<string>>", "string"};
static const char *const tn_row_delim[] = {"array<string>", "string"};
static const char *const tn_path_rows_delim[] = {"string", "array<array<string>>", "string"};

/* ── Documentation ───────────────────────────────────────────────── */

static const vigil_native_symbol_doc_t vigil_csv_module_doc = {
    "CSV parsing and generation.",
    "The csv module provides RFC 4180 compliant CSV parsing and generation with custom delimiters, "
    "header-based parsing, file I/O, and inspection helpers.",
    NULL,
};

static const vigil_native_symbol_doc_t doc_parse = {
    "Parse CSV to 2D array.",
    "Parses CSV data into an array of rows, each row an array of fields.",
    "array<array<string>> rows, err e = csv.parse(data, \",\")",
};

static const vigil_native_symbol_doc_t doc_parse_row = {
    "Parse single CSV row.",
    "Parses one line of CSV into an array of fields.",
    "array<string> fields, err e = csv.parse_row(line, \",\")",
};

static const vigil_native_symbol_doc_t doc_stringify = {
    "Convert 2D array to CSV.",
    "Generates RFC 4180 CSV with CRLF line endings.",
    "string csv_text, err e = csv.stringify(rows, \",\")",
};

static const vigil_native_symbol_doc_t doc_stringify_row = {
    "Convert row to CSV line.",
    "Generates a single CSV line without a trailing newline.",
    "string line, err e = csv.stringify_row(row, \",\")",
};

static const vigil_native_symbol_doc_t doc_read_file = {
    "Read and parse CSV file.",
    "Reads a file and parses its contents as CSV.",
    "array<array<string>> rows, err e = csv.read_file(\"data.csv\", \",\")",
};

static const vigil_native_symbol_doc_t doc_write_file = {
    "Write CSV to file.",
    "Stringifies rows and writes to a file.",
    "err e = csv.write_file(\"out.csv\", rows, \",\")",
};

static const vigil_native_symbol_doc_t doc_parse_with_header = {
    "Parse CSV with header row.",
    "Parses CSV using the first row as column keys, returning an array of maps.",
    "array<map<string, string>> records, err e = csv.parse_with_header(data, \",\")",
};

static const vigil_native_symbol_doc_t doc_read_file_with_header = {
    "Read CSV file with header row.",
    "Reads a file and parses with the first row as column keys.",
    "array<map<string, string>> records, err e = csv.read_file_with_header(\"data.csv\", \",\")",
};



static const vigil_native_symbol_doc_t doc_field_count = {
    "Count fields in first row.",
    "Returns the number of fields in the first row of CSV data.",
    "i32 count, err e = csv.field_count(data, \",\")",
};

static const vigil_native_symbol_doc_t doc_row_count = {
    "Count rows in CSV data.",
    "Returns the number of rows in CSV data.",
    "i32 count = csv.row_count(data)",
};

static const vigil_native_symbol_doc_t doc_has_header = {
    "Check if CSV has a header row.",
    "Heuristic: returns true if the first row contains non-empty, non-numeric fields and there are at least 2 rows.",
    "bool has = csv.has_header(data, \",\")",
};

/* ── Function table ──────────────────────────────────────────────── */

static const vigil_native_module_function_t csv_functions[] = {
    /* parse(data, delimiter) -> (array<array<string>>, err) */
    {"parse", 5U, csv_parse, 2U, str_str_param, VIGIL_TYPE_OBJECT, 2U, obj_err_returns, 0, NULL,
     &array_array_string_ret, 0U, pn_data_delim, NULL, "array<array<string>>", &doc_parse},

    /* parse_row(line, delimiter) -> (array<string>, err) */
    {"parse_row", 9U, csv_parse_row, 2U, str_str_param, VIGIL_TYPE_OBJECT, 2U, obj_err_returns, VIGIL_TYPE_STRING,
     NULL, &array_string_ret, 0U, pn_line_delim, NULL, "array<string>", &doc_parse_row},

    /* stringify(rows, delimiter) -> (string, err) */
    {"stringify", 9U, csv_stringify, 2U, obj_str_param, VIGIL_TYPE_STRING, 2U, str_err_returns, 0,
     NULL, NULL, 0U, pn_rows_delim, tn_rows_delim, NULL, &doc_stringify},

    /* stringify_row(row, delimiter) -> (string, err) */
    {"stringify_row", 13U, csv_stringify_row, 2U, obj_str_param, VIGIL_TYPE_STRING, 2U, str_err_returns, 0,
     stringify_row_param_ext, NULL, 0U, pn_row_delim, tn_row_delim, NULL, &doc_stringify_row},

    /* read_file(path, delimiter) -> (array<array<string>>, err) */
    {"read_file", 9U, csv_read_file, 2U, str_str_param, VIGIL_TYPE_OBJECT, 2U, obj_err_returns, 0, NULL,
     &array_array_string_ret, 0U, pn_path_delim, NULL, "array<array<string>>", &doc_read_file},

    /* write_file(path, rows, delimiter) -> err */
    {"write_file", 10U, csv_write_file, 3U, str_obj_str_param, VIGIL_TYPE_ERR, 1U, NULL, 0, NULL,
     NULL, 0U, pn_path_rows_delim, tn_path_rows_delim, NULL, &doc_write_file},

    /* parse_with_header(data, delimiter) -> (array<map<string,string>>, err) */
    {"parse_with_header", 17U, csv_parse_with_header, 2U, str_str_param, VIGIL_TYPE_OBJECT, 2U, obj_err_returns, 0,
     NULL, &array_map_ss_ret, 0U, pn_data_delim, NULL, "array<map<string, string>>", &doc_parse_with_header},

    /* read_file_with_header(path, delimiter) -> (array<map<string,string>>, err) */
    {"read_file_with_header", 21U, csv_read_file_with_header, 2U, str_str_param, VIGIL_TYPE_OBJECT, 2U,
     obj_err_returns, 0, NULL, &array_map_ss_ret, 0U, pn_path_delim, NULL, "array<map<string, string>>",
     &doc_read_file_with_header},

    /* field_count(data, delimiter) -> (i32, err) */
    {"field_count", 11U, csv_field_count, 2U, str_str_param, VIGIL_TYPE_I32, 2U, i32_err_returns, 0, NULL, NULL, 0U,
     pn_data_delim, NULL, NULL, &doc_field_count},

    /* row_count(data) -> i32 */
    {"row_count", 9U, csv_row_count, 1U, str_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, pn_data, NULL, NULL,
     &doc_row_count},

    /* has_header(data, delimiter) -> bool */
    {"has_header", 10U, csv_has_header, 2U, str_str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U,
     pn_data_delim, NULL, NULL, &doc_has_header},
};

#define CSV_FUNCTION_COUNT (sizeof(csv_functions) / sizeof(csv_functions[0]))

VIGIL_API const vigil_native_module_t vigil_stdlib_csv = {
    "csv", 3U, csv_functions, CSV_FUNCTION_COUNT, NULL, 0U, &vigil_csv_module_doc, NULL, 0U};
