/*
 * vm_ops_string.c — Extracted string opcode handlers for the VM dispatch loop.
 *
 * Each function implements one (or a small group of related) string opcodes.
 * They pop operands, perform the operation, push results, and return status.
 * The dispatch loop in vm.c calls these and checks the return value.
 */

#include "vm_ops_string.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "vigil/string.h"

/* ── Types ─────────────────────────────────────────────────────── */

typedef struct
{
    vigil_value_t *items;
    size_t count;
    size_t capacity;
} value_array_t;

typedef struct
{
    const char *text;
    size_t length;
} string_span_t;

/* ── Static helpers ────────────────────────────────────────────── */

static size_t escape_string_to_c(const char *text, size_t text_length, char *out)
{
    size_t j = 0;
    out[j++] = '"';
    for (size_t i = 0; i < text_length; i++)
    {
        unsigned char c = (unsigned char)text[i];
        switch (c)
        {
        case '"':
            out[j++] = '\\';
            out[j++] = '"';
            break;
        case '\\':
            out[j++] = '\\';
            out[j++] = '\\';
            break;
        case '\n':
            out[j++] = '\\';
            out[j++] = 'n';
            break;
        case '\r':
            out[j++] = '\\';
            out[j++] = 'r';
            break;
        case '\t':
            out[j++] = '\\';
            out[j++] = 't';
            break;
        default:
            if (c >= 32 && c < 127)
            {
                out[j++] = (char)c;
            }
            else
            {
                out[j++] = '\\';
                out[j++] = 'x';
                out[j++] = "0123456789abcdef"[c >> 4];
                out[j++] = "0123456789abcdef"[c & 0xf];
            }
            break;
        }
    }
    out[j++] = '"';
    return j;
}

static vigil_status_t split_by_char(vigil_vm_t *vm, const char *text, size_t text_length, value_array_t *arr,
                                    vigil_error_t *error)
{
    vigil_status_t status = VIGIL_STATUS_OK;
    for (size_t i = 0; i < text_length && status == VIGIL_STATUS_OK; i++)
    {
        status = vigil_vm_grow_value_array(vm->runtime, &arr->items, &arr->capacity, arr->count + 1U, error);
        if (status == VIGIL_STATUS_OK)
        {
            VIGIL_VM_VALUE_INIT_NIL(&arr->items[arr->count]);
            status = vigil_vm_new_string_value(vm, text + i, 1U, &arr->items[arr->count], error);
            if (status == VIGIL_STATUS_OK)
                arr->count += 1U;
        }
    }
    return status;
}

static vigil_status_t split_by_separator(vigil_vm_t *vm, const char *text, size_t text_length, const string_span_t *sep,
                                         value_array_t *arr, vigil_error_t *error)
{
    vigil_status_t status = VIGIL_STATUS_OK;
    size_t index = 0U;
    size_t match_index;
    while (status == VIGIL_STATUS_OK)
    {
        status = vigil_vm_grow_value_array(vm->runtime, &arr->items, &arr->capacity, arr->count + 1U, error);
        if (status != VIGIL_STATUS_OK)
            break;
        VIGIL_VM_VALUE_INIT_NIL(&arr->items[arr->count]);
        if (vigil_vm_find_substring(text + index, text_length - index, sep->text, sep->length, &match_index))
        {
            status = vigil_vm_new_string_value(vm, text + index, match_index, &arr->items[arr->count], error);
            if (status == VIGIL_STATUS_OK)
                arr->count += 1U;
            index += match_index + sep->length;
        }
        else
        {
            status = vigil_vm_new_string_value(vm, text + index, text_length - index, &arr->items[arr->count], error);
            if (status == VIGIL_STATUS_OK)
                arr->count += 1U;
            break;
        }
    }
    return status;
}

static void free_value_array(vigil_vm_t *vm, vigil_value_t *items, size_t count)
{
    for (size_t i = 0; i < count; i++)
        vigil_value_release(&items[i]);
    vigil_runtime_free(vm->runtime, (void **)&items);
}

static vigil_status_t string_replace_loop(vigil_vm_t *vm, const string_span_t *src, const string_span_t *old_span,
                                          const string_span_t *new_span, vigil_value_t *out, vigil_error_t *error)
{
    vigil_status_t status = VIGIL_STATUS_OK;
    vigil_string_t built;
    size_t index = 0U;
    size_t match_index;

    vigil_string_init(&built, vm->runtime);
    while (index < src->length && status == VIGIL_STATUS_OK)
    {
        if (vigil_vm_find_substring(src->text + index, src->length - index, old_span->text, old_span->length,
                                    &match_index))
        {
            status = vigil_string_append(&built, src->text + index, match_index, error);
            if (status == VIGIL_STATUS_OK)
                status = vigil_string_append(&built, new_span->text, new_span->length, error);
            index += match_index + old_span->length;
        }
        else
        {
            status = vigil_string_append(&built, src->text + index, src->length - index, error);
            break;
        }
    }
    if (status == VIGIL_STATUS_OK)
        status = vigil_vm_new_string_value(vm, vigil_string_c_str(&built), vigil_string_length(&built), out, error);
    vigil_string_free(&built);
    return status;
}

/* ── GET_STRING_SIZE ───────────────────────────────────────────── */

vigil_status_t vigil_vm_op_get_string_size(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, value;
    const char *text;
    size_t length;

    frame->ip += 1U;
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text, &length))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "string len() requires a string receiver", error);
    }
    vigil_value_init_int(&value, (int64_t)length);
    VIGIL_VM_VALUE_RELEASE(&left);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_CONTAINS / STRING_STARTS_WITH / STRING_ENDS_WITH ─── */

vigil_status_t vigil_vm_op_string_search(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                         vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;
    vigil_opcode_t string_opcode = (vigil_opcode_t)code[frame->ip];
    const char *text;
    const char *needle;
    size_t text_length;
    size_t needle_length;
    int found;

    frame->ip += 1U;
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text, &text_length) ||
        !vigil_vm_get_string_parts(&right, &needle, &needle_length))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "string method arguments must be strings", error);
    }

    found = 0;
    if (string_opcode == VIGIL_OPCODE_STRING_CONTAINS)
    {
        found = vigil_vm_find_substring(text, text_length, needle, needle_length, NULL);
    }
    else if (string_opcode == VIGIL_OPCODE_STRING_STARTS_WITH)
    {
        found = needle_length <= text_length && memcmp(text, needle, needle_length) == 0;
    }
    else
    {
        found =
            needle_length <= text_length && memcmp(text + (text_length - needle_length), needle, needle_length) == 0;
    }
    (value) = vigil_nanbox_from_bool(found);
    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&right);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_TRIM / STRING_TO_UPPER / STRING_TO_LOWER ───────────── */

static vigil_status_t string_change_case(vigil_vm_t *vm, const char *text, size_t length, vigil_opcode_t opcode,
                                         vigil_value_t *out, vigil_error_t *error)
{
    void *memory = NULL;
    vigil_status_t status = vigil_runtime_alloc(vm->runtime, length + 1U, &memory, error);
    if (status == VIGIL_STATUS_OK)
    {
        char *buffer = (char *)memory;
        for (size_t i = 0U; i < length; i++)
            buffer[i] = (char)(opcode == VIGIL_OPCODE_STRING_TO_UPPER ? toupper((unsigned char)text[i])
                                                                      : tolower((unsigned char)text[i]));
        buffer[length] = '\0';
        status = vigil_vm_new_string_value(vm, buffer, length, out, error);
        vigil_runtime_free(vm->runtime, &memory);
    }
    return status;
}

vigil_status_t vigil_vm_op_string_transform(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                            vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, value;
    vigil_opcode_t string_opcode = (vigil_opcode_t)code[frame->ip];
    const char *text;
    size_t length;

    frame->ip += 1U;
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text, &length))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "string method requires a string receiver",
                                   error);
    }

    if (string_opcode == VIGIL_OPCODE_STRING_TRIM)
    {
        size_t start = 0U;
        size_t end = length;
        while (start < length && isspace((unsigned char)text[start]))
            start += 1U;
        while (end > start && isspace((unsigned char)text[end - 1U]))
            end -= 1U;
        status = vigil_vm_new_string_value(vm, text + start, end - start, &value, error);
    }
    else
    {
        status = string_change_case(vm, text, length, string_opcode, &value, error);
    }
    if (status != VIGIL_STATUS_OK)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return status;
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_REPLACE ────────────────────────────────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_string_replace(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;

    frame->ip += 1U;
    value = vigil_vm_pop_or_nil(vm);
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    const char *text, *old_text, *new_text;
    size_t text_length, old_length, new_length;

    if (!vigil_vm_get_string_parts(&left, &text, &text_length) ||
        !vigil_vm_get_string_parts(&right, &old_text, &old_length) ||
        !vigil_vm_get_string_parts(&value, &new_text, &new_length))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "string replace() arguments must be strings",
                                   error);
    }

    if (old_length == 0U)
    {
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_COPY(&right, &left);
    }
    else
    {
        vigil_value_t result;
        string_span_t src = {text, text_length};
        string_span_t old_span = {old_text, old_length};
        string_span_t new_span = {new_text, new_length};
        status = string_replace_loop(vm, &src, &old_span, &new_span, &result, error);
        if (status != VIGIL_STATUS_OK)
        {
            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&right);
            VIGIL_VM_VALUE_RELEASE(&value);
            return status;
        }
        VIGIL_VM_VALUE_RELEASE(&right);
        right = result;
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&value);
    status = vigil_vm_push(vm, &right, error);
    VIGIL_VM_VALUE_RELEASE(&right);
    return status;
}

/* ── STRING_SPLIT ──────────────────────────────────────────────── */

vigil_status_t vigil_vm_op_string_split(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;
    const char *text, *separator;
    size_t text_length, separator_length;
    value_array_t arr = {NULL, 0U, 0U};
    vigil_object_t *array_object = NULL;

    frame->ip += 1U;
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text, &text_length) ||
        !vigil_vm_get_string_parts(&right, &separator, &separator_length))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "string split() arguments must be strings",
                                   error);
    }

    if (separator_length == 0U)
        status = split_by_char(vm, text, text_length, &arr, error);
    else
    {
        string_span_t sep = {separator, separator_length};
        status = split_by_separator(vm, text, text_length, &sep, &arr, error);
    }

    if (status == VIGIL_STATUS_OK)
        status = vigil_array_object_new(vm->runtime, arr.items, arr.count, &array_object, error);
    free_value_array(vm, arr.items, arr.count);
    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&right);
    if (status != VIGIL_STATUS_OK)
        return status;
    vigil_value_init_object(&value, &array_object);
    vigil_object_release(&array_object);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_INDEX_OF ───────────────────────────────────────────── */

vigil_status_t vigil_vm_op_string_index_of(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;
    const char *text, *needle;
    size_t text_length, needle_length, index;
    int found;

    frame->ip += 1U;
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text, &text_length) ||
        !vigil_vm_get_string_parts(&right, &needle, &needle_length))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "string method arguments must be strings", error);
    }

    found = vigil_vm_find_substring(text, text_length, needle, needle_length, &index);
    vigil_value_init_int(&value, found ? (int64_t)index : -1);
    VIGIL_VM_VALUE_RELEASE(&right);
    (right) = vigil_nanbox_from_bool(found);
    VIGIL_VM_VALUE_RELEASE(&left);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    if (status == VIGIL_STATUS_OK)
        status = vigil_vm_push(vm, &right, error);
    VIGIL_VM_VALUE_RELEASE(&right);
    return status;
}

/* ── STRING_SUBSTR ─────────────────────────────────────────────── */

typedef struct
{
    vigil_value_t str;
    vigil_value_t err;
} substr_result_t;

static vigil_status_t string_substr_slice(vigil_vm_t *vm, const string_span_t *src, int64_t start, int64_t slice_length,
                                          substr_result_t *out, vigil_error_t *error)
{
    vigil_status_t status;
    if (start < 0 || slice_length < 0 || (uint64_t)start > src->length ||
        (uint64_t)slice_length > src->length - (size_t)start)
    {
        status = vigil_vm_new_string_value(vm, "", 0U, &out->str, error);
        if (status == VIGIL_STATUS_OK)
            status = vigil_vm_make_error_value(vm, 7, "string slice is out of range",
                                               sizeof("string slice is out of range") - 1U, &out->err, error);
    }
    else
    {
        status = vigil_vm_new_string_value(vm, src->text + (size_t)start, (size_t)slice_length, &out->str, error);
        if (status == VIGIL_STATUS_OK)
            status = vigil_vm_make_ok_error_value(vm, &out->err, error);
    }
    return status;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_string_substr(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;
    const char *text;
    size_t text_length;

    frame->ip += 1U;
    value = vigil_vm_pop_or_nil(vm);
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text, &text_length) || !vigil_nanbox_is_int(right) ||
        !vigil_nanbox_is_int(value))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "string substr() requires i32 start and length",
                                   error);
    }
    {
        substr_result_t res;
        string_span_t src = {text, text_length};
        VIGIL_VM_VALUE_INIT_NIL(&res.str);
        VIGIL_VM_VALUE_INIT_NIL(&res.err);
        status = string_substr_slice(vm, &src, vigil_value_as_int(&right), vigil_value_as_int(&value), &res, error);
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_RELEASE(&value);
        right = res.str;
        value = res.err;
    }
    if (status != VIGIL_STATUS_OK)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_RELEASE(&value);
        return status;
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    status = vigil_vm_push(vm, &right, error);
    VIGIL_VM_VALUE_RELEASE(&right);
    if (status == VIGIL_STATUS_OK)
        status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_BYTES ──────────────────────────────────────────────── */

vigil_status_t vigil_vm_op_string_bytes(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, value;
    const char *text;
    size_t text_length;
    vigil_value_t *items = NULL;
    size_t item_count = 0U;
    size_t item_capacity = 0U;
    size_t index;
    vigil_object_t *array_object = NULL;

    frame->ip += 1U;
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text, &text_length))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "string bytes() requires a string receiver",
                                   error);
    }
    status = vigil_vm_grow_value_array(vm->runtime, &items, &item_capacity, text_length, error);
    for (index = 0U; status == VIGIL_STATUS_OK && index < text_length; index += 1U)
        vigil_value_init_uint(&items[index], (uint64_t)(unsigned char)text[index]);
    item_count = status == VIGIL_STATUS_OK ? text_length : 0U;
    if (status == VIGIL_STATUS_OK)
        status = vigil_array_object_new(vm->runtime, items, item_count, &array_object, error);
    for (index = 0U; index < item_count; index += 1U)
        vigil_value_release(&items[index]);
    vigil_runtime_free(vm->runtime, (void **)&items);
    if (status != VIGIL_STATUS_OK)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return status;
    }
    vigil_value_init_object(&value, &array_object);
    vigil_object_release(&array_object);
    VIGIL_VM_VALUE_RELEASE(&left);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_CHAR_AT ────────────────────────────────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_string_char_at(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;
    const char *text;
    size_t text_length;
    int64_t index;

    frame->ip += 1U;
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);
    VIGIL_VM_VALUE_INIT_NIL(&value);

    if (!vigil_vm_get_string_parts(&left, &text, &text_length) || !vigil_nanbox_is_int(right))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "string char_at() requires an i32 index", error);
    }
    index = vigil_value_as_int(&right);
    if (index < 0 || (uint64_t)index >= text_length)
    {
        status = vigil_vm_new_string_value(vm, "", 0U, &right, error);
        if (status == VIGIL_STATUS_OK)
            status = vigil_vm_make_error_value(vm, 7, "string index is out of range",
                                               sizeof("string index is out of range") - 1U, &value, error);
    }
    else
    {
        status = vigil_vm_new_string_value(vm, text + (size_t)index, 1U, &right, error);
        if (status == VIGIL_STATUS_OK)
            status = vigil_vm_make_ok_error_value(vm, &value, error);
    }
    if (status != VIGIL_STATUS_OK)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return status;
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    status = vigil_vm_push(vm, &right, error);
    VIGIL_VM_VALUE_RELEASE(&right);
    if (status == VIGIL_STATUS_OK)
        status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_TRIM_LEFT / STRING_TRIM_RIGHT ──────────────────────── */

vigil_status_t vigil_vm_op_string_trim_dir(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                           vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, value;
    vigil_opcode_t string_opcode = (vigil_opcode_t)code[frame->ip];
    const char *text;
    size_t length;

    frame->ip += 1U;
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text, &length))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "string method requires a string receiver",
                                   error);
    }

    if (string_opcode == VIGIL_OPCODE_STRING_TRIM_LEFT)
    {
        size_t start = 0U;
        while (start < length && isspace((unsigned char)text[start]))
            start += 1U;
        status = vigil_vm_new_string_value(vm, text + start, length - start, &value, error);
    }
    else
    {
        size_t end = length;
        while (end > 0U && isspace((unsigned char)text[end - 1U]))
            end -= 1U;
        status = vigil_vm_new_string_value(vm, text, end, &value, error);
    }
    if (status != VIGIL_STATUS_OK)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return status;
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_REVERSE ────────────────────────────────────────────── */

vigil_status_t vigil_vm_op_string_reverse(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, value;
    const char *text;
    size_t length;

    frame->ip += 1U;
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text, &length))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "string method requires a string receiver",
                                   error);
    }

    {
        void *memory = NULL;
        char *buffer;
        size_t i;

        status = vigil_runtime_alloc(vm->runtime, length + 1U, &memory, error);
        if (status == VIGIL_STATUS_OK)
        {
            buffer = (char *)memory;
            for (i = 0U; i < length; i += 1U)
                buffer[i] = text[length - 1U - i];
            buffer[length] = '\0';
            status = vigil_vm_new_string_value(vm, buffer, length, &value, error);
            vigil_runtime_free(vm->runtime, &memory);
        }
    }
    if (status != VIGIL_STATUS_OK)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return status;
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_IS_EMPTY ───────────────────────────────────────────── */

vigil_status_t vigil_vm_op_string_is_empty(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_value_t left, value;
    const char *text;
    size_t length;

    frame->ip += 1U;
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text, &length))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "string method requires a string receiver",
                                   error);
    }

    VIGIL_VM_VALUE_RELEASE(&left);
    vigil_value_init_bool(&value, length == 0U);
    vigil_status_t status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_CHAR_COUNT ─────────────────────────────────────────── */

vigil_status_t vigil_vm_op_string_char_count(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_value_t left, value;
    const char *text;
    size_t text_length;
    size_t ci;
    int32_t ccount;

    frame->ip += 1U;
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text, &text_length))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "char_count() requires a string receiver", error);
    }

    ccount = 0;
    for (ci = 0U; ci < text_length;)
    {
        unsigned char uc = (unsigned char)text[ci];
        if (uc < 0x80U)
            ci += 1U;
        else if ((uc & 0xE0U) == 0xC0U)
            ci += 2U;
        else if ((uc & 0xF0U) == 0xE0U)
            ci += 3U;
        else
            ci += 4U;
        ccount += 1;
    }

    VIGIL_VM_VALUE_RELEASE(&left);
    vigil_value_init_int(&value, (int64_t)ccount);
    vigil_status_t status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_REPEAT ─────────────────────────────────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_string_repeat(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;
    const char *text;
    size_t length;
    int64_t count;

    frame->ip += 1U;
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text, &length) || !vigil_nanbox_is_int(right))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "string repeat() requires an i32 count", error);
    }

    count = vigil_value_as_int(&right);
    VIGIL_VM_VALUE_RELEASE(&right);

    if (count <= 0)
    {
        status = vigil_vm_new_string_value(vm, "", 0U, &value, error);
    }
    else
    {
        size_t total = length * (size_t)count;
        void *memory = NULL;
        char *buffer;
        int64_t i;

        status = vigil_runtime_alloc(vm->runtime, total + 1U, &memory, error);
        if (status == VIGIL_STATUS_OK)
        {
            buffer = (char *)memory;
            for (i = 0; i < count; i += 1)
                memcpy(buffer + (size_t)i * length, text, length);
            buffer[total] = '\0';
            status = vigil_vm_new_string_value(vm, buffer, total, &value, error);
            vigil_runtime_free(vm->runtime, &memory);
        }
    }
    if (status != VIGIL_STATUS_OK)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return status;
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_COUNT ──────────────────────────────────────────────── */

vigil_status_t vigil_vm_op_string_count(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_value_t left, right, value;
    const char *text, *needle;
    size_t text_length, needle_length;

    frame->ip += 1U;
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text, &text_length) ||
        !vigil_vm_get_string_parts(&right, &needle, &needle_length))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "string count() requires a string argument",
                                   error);
    }

    int64_t n = 0;
    if (needle_length > 0U && needle_length <= text_length)
    {
        const char *p = text;
        const char *end = text + text_length;
        while (p <= end - needle_length)
        {
            if (memcmp(p, needle, needle_length) == 0)
            {
                n += 1;
                p += needle_length;
            }
            else
            {
                p += 1;
            }
        }
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&right);
    vigil_value_init_int(&value, n);
    vigil_status_t status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_LAST_INDEX_OF ──────────────────────────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_string_last_index_of(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;
    const char *text, *needle;
    size_t text_length, needle_length;

    frame->ip += 1U;
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text, &text_length) ||
        !vigil_vm_get_string_parts(&right, &needle, &needle_length))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                   "string last_index_of() requires a string argument", error);
    }

    int64_t found_index = -1;
    if (needle_length > 0U && needle_length <= text_length)
    {
        size_t i = text_length - needle_length;
        for (;;)
        {
            if (memcmp(text + i, needle, needle_length) == 0)
            {
                found_index = (int64_t)i;
                break;
            }
            if (i == 0U)
                break;
            i -= 1U;
        }
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&right);
    vigil_value_init_int(&value, found_index >= 0 ? found_index : 0);
    status = vigil_vm_push(vm, &value, error);
    if (status == VIGIL_STATUS_OK)
    {
        vigil_value_t found_val;
        vigil_value_init_bool(&found_val, found_index >= 0);
        status = vigil_vm_push(vm, &found_val, error);
        VIGIL_VM_VALUE_RELEASE(&found_val);
    }
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_TRIM_PREFIX / STRING_TRIM_SUFFIX ───────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_string_trim_affix(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                             vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;
    vigil_opcode_t string_opcode = (vigil_opcode_t)code[frame->ip];
    const char *text, *prefix;
    size_t text_length, prefix_length;

    frame->ip += 1U;
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text, &text_length) ||
        !vigil_vm_get_string_parts(&right, &prefix, &prefix_length))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "string method requires a string argument",
                                   error);
    }

    if (string_opcode == VIGIL_OPCODE_STRING_TRIM_PREFIX)
    {
        if (prefix_length <= text_length && memcmp(text, prefix, prefix_length) == 0)
            status = vigil_vm_new_string_value(vm, text + prefix_length, text_length - prefix_length, &value, error);
        else
            status = vigil_vm_new_string_value(vm, text, text_length, &value, error);
    }
    else
    {
        if (prefix_length <= text_length && memcmp(text + text_length - prefix_length, prefix, prefix_length) == 0)
            status = vigil_vm_new_string_value(vm, text, text_length - prefix_length, &value, error);
        else
            status = vigil_vm_new_string_value(vm, text, text_length, &value, error);
    }
    if (status != VIGIL_STATUS_OK)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return status;
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&right);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── CHAR_FROM_INT ─────────────────────────────────────────────── */

vigil_status_t vigil_vm_op_char_from_int(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, value;

    frame->ip += 1U;
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_nanbox_is_int(left))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "char() requires an integer argument", error);
    }

    int32_t code_point = vigil_nanbox_decode_i32(left);
    VIGIL_VM_VALUE_RELEASE(&left);

    if (code_point < 0 || code_point > 255)
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "char() argument must be 0-255", error);

    char ch = (char)code_point;
    status = vigil_vm_new_string_value(vm, &ch, 1, &value, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_TO_C ───────────────────────────────────────────────── */

vigil_status_t vigil_vm_op_string_to_c(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, value;
    const char *text;
    size_t text_length;

    frame->ip += 1U;
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text, &text_length))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "to_c() requires a string", error);
    }

    size_t out_cap = text_length * 4 + 3;
    char *out_buf = NULL;
    status = vigil_runtime_alloc(vm->runtime, out_cap, (void **)&out_buf, error);
    if (status != VIGIL_STATUS_OK)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return status;
    }

    size_t j = escape_string_to_c(text, text_length, out_buf);

    VIGIL_VM_VALUE_RELEASE(&left);
    status = vigil_vm_new_string_value(vm, out_buf, j, &value, error);
    vigil_runtime_free(vm->runtime, (void **)&out_buf);
    if (status != VIGIL_STATUS_OK)
        return status;
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_FIELDS ─────────────────────────────────────────────── */

static vigil_status_t split_whitespace_fields(vigil_vm_t *vm, const char *text, size_t text_length, value_array_t *arr,
                                              vigil_error_t *error)
{
    vigil_status_t status = VIGIL_STATUS_OK;
    size_t i = 0;
    while (i < text_length && status == VIGIL_STATUS_OK)
    {
        while (i < text_length && isspace((unsigned char)text[i]))
            i++;
        if (i >= text_length)
            break;
        size_t start = i;
        while (i < text_length && !isspace((unsigned char)text[i]))
            i++;
        status = vigil_vm_grow_value_array(vm->runtime, &arr->items, &arr->capacity, arr->count + 1U, error);
        if (status == VIGIL_STATUS_OK)
        {
            VIGIL_VM_VALUE_INIT_NIL(&arr->items[arr->count]);
            status = vigil_vm_new_string_value(vm, text + start, i - start, &arr->items[arr->count], error);
            if (status == VIGIL_STATUS_OK)
                arr->count++;
        }
    }
    return status;
}

vigil_status_t vigil_vm_op_string_fields(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, value;
    const char *text;
    size_t text_length;
    value_array_t arr = {NULL, 0U, 0U};
    vigil_object_t *array_object = NULL;

    frame->ip += 1U;
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text, &text_length))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "fields() requires a string", error);
    }

    status = split_whitespace_fields(vm, text, text_length, &arr, error);
    if (status == VIGIL_STATUS_OK)
        status = vigil_array_object_new(vm->runtime, arr.items, arr.count, &array_object, error);
    free_value_array(vm, arr.items, arr.count);
    VIGIL_VM_VALUE_RELEASE(&left);
    if (status != VIGIL_STATUS_OK)
        return status;
    vigil_value_init_object(&value, &array_object);
    vigil_object_release(&array_object);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_EQUAL_FOLD ─────────────────────────────────────────── */

vigil_status_t vigil_vm_op_string_equal_fold(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_value_t left, right, value;
    const char *text1, *text2;
    size_t len1, len2;

    frame->ip += 1U;
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text1, &len1) || !vigil_vm_get_string_parts(&right, &text2, &len2))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "equal_fold() requires string arguments", error);
    }

    int equal = 0;
    if (len1 == len2)
    {
        equal = 1;
        for (size_t i = 0; i < len1; i++)
        {
            if (tolower((unsigned char)text1[i]) != tolower((unsigned char)text2[i]))
            {
                equal = 0;
                break;
            }
        }
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&right);
    (value) = vigil_nanbox_from_bool(equal);
    vigil_status_t status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_CUT ────────────────────────────────────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_string_cut(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right;
    const char *text, *sep;
    size_t text_len, sep_len;
    size_t match_idx;

    frame->ip += 1U;
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &text, &text_len) || !vigil_vm_get_string_parts(&right, &sep, &sep_len))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "cut() requires string arguments", error);
    }

    vigil_value_t before, after, found_val;
    VIGIL_VM_VALUE_INIT_NIL(&before);
    VIGIL_VM_VALUE_INIT_NIL(&after);

    int found = vigil_vm_find_substring(text, text_len, sep, sep_len, &match_idx);
    if (found)
    {
        status = vigil_vm_new_string_value(vm, text, match_idx, &before, error);
        if (status == VIGIL_STATUS_OK)
            status = vigil_vm_new_string_value(vm, text + match_idx + sep_len, text_len - match_idx - sep_len, &after,
                                               error);
    }
    else
    {
        status = vigil_vm_new_string_value(vm, text, text_len, &before, error);
        if (status == VIGIL_STATUS_OK)
            status = vigil_vm_new_string_value(vm, "", 0, &after, error);
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&right);
    if (status != VIGIL_STATUS_OK)
    {
        VIGIL_VM_VALUE_RELEASE(&before);
        VIGIL_VM_VALUE_RELEASE(&after);
        return status;
    }
    (found_val) = vigil_nanbox_from_bool(found);
    status = vigil_vm_push(vm, &before, error);
    VIGIL_VM_VALUE_RELEASE(&before);
    if (status == VIGIL_STATUS_OK)
        status = vigil_vm_push(vm, &after, error);
    VIGIL_VM_VALUE_RELEASE(&after);
    if (status == VIGIL_STATUS_OK)
        status = vigil_vm_push(vm, &found_val, error);
    VIGIL_VM_VALUE_RELEASE(&found_val);
    return status;
}

/* ── STRING_JOIN ───────────────────────────────────────────────── */

static vigil_status_t string_join_elements(vigil_object_t *arr, const char *sep, size_t sep_len, vigil_string_t *built,
                                           vigil_error_t *error)
{
    vigil_status_t status = VIGIL_STATUS_OK;
    size_t arr_len = vigil_array_object_length(arr);
    for (size_t i = 0; i < arr_len && status == VIGIL_STATUS_OK; i++)
    {
        vigil_value_t elem;
        const char *elem_text;
        size_t elem_len;
        if (!vigil_array_object_get(arr, i, &elem) || !vigil_vm_get_string_parts(&elem, &elem_text, &elem_len))
        {
            vigil_value_release(&elem);
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        if (i > 0)
            status = vigil_string_append(built, sep, sep_len, error);
        if (status == VIGIL_STATUS_OK)
            status = vigil_string_append(built, elem_text, elem_len, error);
        vigil_value_release(&elem);
    }
    return status;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_string_join(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;
    const char *sep;
    size_t sep_len;
    vigil_object_t *arr;

    frame->ip += 1U;
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&left, &sep, &sep_len))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "join() requires a string separator", error);
    }
    if (!vigil_nanbox_is_object(right) || (arr = (vigil_object_t *)vigil_nanbox_decode_ptr(right)) == NULL ||
        vigil_object_type(arr) != VIGIL_OBJECT_ARRAY)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "join() requires an array<string> argument",
                                   error);
    }

    vigil_string_t built;
    vigil_string_init(&built, vm->runtime);
    status = string_join_elements(arr, sep, sep_len, &built, error);

    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&right);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_string_free(&built);
        return status;
    }
    status = vigil_vm_new_string_value(vm, vigil_string_c_str(&built), vigil_string_length(&built), &value, error);
    vigil_string_free(&built);
    if (status != VIGIL_STATUS_OK)
        return status;
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── STRING_NEXT_CHAR ──────────────────────────────────────────── */

vigil_status_t vigil_vm_op_string_next_char(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t str_val, offset_val, char_val, next_val;
    const char *text;
    size_t text_length;
    int64_t offset;
    size_t char_len;

    frame->ip += 1U;
    offset_val = vigil_vm_pop_or_nil(vm);
    str_val = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&str_val, &text, &text_length) || !vigil_nanbox_is_int(offset_val))
    {
        VIGIL_VM_VALUE_RELEASE(&str_val);
        VIGIL_VM_VALUE_RELEASE(&offset_val);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                   "string next_char requires a string and i32 offset", error);
    }

    offset = vigil_value_as_int(&offset_val);
    if (offset < 0 || (size_t)offset >= text_length)
    {
        VIGIL_VM_VALUE_RELEASE(&str_val);
        VIGIL_VM_VALUE_RELEASE(&offset_val);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "string next_char offset out of range", error);
    }

    {
        unsigned char uc = (unsigned char)text[(size_t)offset];
        if (uc < 0x80U)
            char_len = 1U;
        else if ((uc & 0xE0U) == 0xC0U)
            char_len = 2U;
        else if ((uc & 0xF0U) == 0xE0U)
            char_len = 3U;
        else
            char_len = 4U;
        if ((size_t)offset + char_len > text_length)
            char_len = text_length - (size_t)offset;
    }

    status = vigil_vm_new_string_value(vm, text + (size_t)offset, char_len, &char_val, error);
    VIGIL_VM_VALUE_RELEASE(&str_val);
    VIGIL_VM_VALUE_RELEASE(&offset_val);
    if (status != VIGIL_STATUS_OK)
        return status;

    vigil_value_init_int(&next_val, offset + (int64_t)char_len);
    status = vigil_vm_push(vm, &char_val, error);
    VIGIL_VM_VALUE_RELEASE(&char_val);
    if (status == VIGIL_STATUS_OK)
        status = vigil_vm_push(vm, &next_val, error);
    VIGIL_VM_VALUE_RELEASE(&next_val);
    return status;
}

/* ── STRING_PAD_LEFT / STRING_PAD_RIGHT ────────────────────────── */

static vigil_status_t vigil_vm_op_string_pad(vigil_vm_t *vm, vigil_vm_frame_t *frame, int left, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t str_val, width_val, fill_val, result;
    const char *text;
    size_t text_len;
    const char *fill_text;
    size_t fill_len;

    frame->ip += 1U;
    fill_val = vigil_vm_pop_or_nil(vm);
    width_val = vigil_vm_pop_or_nil(vm);
    str_val = vigil_vm_pop_or_nil(vm);

    if (!vigil_vm_get_string_parts(&str_val, &text, &text_len))
    {
        VIGIL_VM_VALUE_RELEASE(&str_val);
        VIGIL_VM_VALUE_RELEASE(&width_val);
        VIGIL_VM_VALUE_RELEASE(&fill_val);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "pad requires a string receiver", error);
    }

    int64_t width = vigil_value_as_int(&width_val);
    VIGIL_VM_VALUE_RELEASE(&width_val);

    if (!vigil_vm_get_string_parts(&fill_val, &fill_text, &fill_len) || fill_len == 0)
    {
        VIGIL_VM_VALUE_RELEASE(&str_val);
        VIGIL_VM_VALUE_RELEASE(&fill_val);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "pad fill must be a non-empty string", error);
    }

    if (width <= 0 || (size_t)width <= text_len)
    {
        /* No padding needed — return original string. */
        VIGIL_VM_VALUE_RELEASE(&fill_val);
        status = vigil_vm_push(vm, &str_val, error);
        VIGIL_VM_VALUE_RELEASE(&str_val);
        return status;
    }

    size_t target = (size_t)width;
    size_t pad_count = target - text_len;
    size_t total = target;
    char *buf = (char *)malloc(total + 1U);
    if (buf == NULL)
    {
        VIGIL_VM_VALUE_RELEASE(&str_val);
        VIGIL_VM_VALUE_RELEASE(&fill_val);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "out of memory", error);
    }

    if (left)
    {
        for (size_t i = 0; i < pad_count; i++)
            buf[i] = fill_text[i % fill_len];
        memcpy(buf + pad_count, text, text_len);
    }
    else
    {
        memcpy(buf, text, text_len);
        for (size_t i = 0; i < pad_count; i++)
            buf[text_len + i] = fill_text[i % fill_len];
    }
    buf[total] = '\0';

    VIGIL_VM_VALUE_RELEASE(&str_val);
    VIGIL_VM_VALUE_RELEASE(&fill_val);

    status = vigil_vm_new_string_value(vm, buf, total, &result, error);
    free(buf);
    if (status != VIGIL_STATUS_OK)
        return status;
    status = vigil_vm_push(vm, &result, error);
    VIGIL_VM_VALUE_RELEASE(&result);
    return status;
}

vigil_status_t vigil_vm_op_string_pad_left(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    return vigil_vm_op_string_pad(vm, frame, 1, error);
}

vigil_status_t vigil_vm_op_string_pad_right(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    return vigil_vm_op_string_pad(vm, frame, 0, error);
}
