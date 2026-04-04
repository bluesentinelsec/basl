/* strings.c — strings module: Builder class for efficient string concatenation. */

#include <stdlib.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_nanbox.h"

/* ── Builder handle registry ─────────────────────────────────────── */

typedef struct
{
    char *data;
    size_t len;
    size_t cap;
} builder_t;

#define MAX_BUILDERS 256
static builder_t *builders[MAX_BUILDERS];

static int64_t builder_alloc(void)
{
    for (int64_t i = 0; i < MAX_BUILDERS; i++)
    {
        if (builders[i] == NULL)
        {
            builders[i] = (builder_t *)calloc(1, sizeof(builder_t));
            return builders[i] ? i : -1;
        }
    }
    return -1;
}

static builder_t *builder_get(int64_t h)
{
    if (h < 0 || h >= MAX_BUILDERS)
        return NULL;
    return builders[h];
}

static void builder_free(int64_t h)
{
    if (h >= 0 && h < MAX_BUILDERS && builders[h])
    {
        free(builders[h]->data);
        free(builders[h]);
        builders[h] = NULL;
    }
}

static void builder_append(builder_t *b, const char *s, size_t len)
{
    if (b->len + len > b->cap)
    {
        size_t new_cap = b->cap == 0 ? 64 : b->cap;
        while (new_cap < b->len + len)
            new_cap *= 2;
        b->data = (char *)realloc(b->data, new_cap);
        b->cap = new_cap;
    }
    memcpy(b->data + b->len, s, len);
    b->len += len;
}

/* ── Helpers ─────────────────────────────────────────────────────── */

static bool get_str(vigil_vm_t *vm, size_t base, size_t idx, const char **s, size_t *len)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    if (!vigil_nanbox_is_object(v))
        return false;
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(v);
    if (!obj || vigil_object_type(obj) != VIGIL_OBJECT_STRING)
        return false;
    *s = vigil_string_object_c_str(obj);
    *len = vigil_string_object_length(obj);
    return true;
}

static vigil_status_t push_str(vigil_vm_t *vm, const char *text, size_t len, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_value_t val;
    vigil_status_t st = vigil_string_object_new(vigil_vm_runtime(vm), text, len, &obj, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_init_object(&val, &obj);
    st = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return st;
}

/* ── Builder methods ─────────────────────────────────────────────── */

enum
{
    BUILDER_HANDLE = 0,
    BUILDER_FIELD_COUNT
};

static int64_t get_builder_handle(vigil_vm_t *vm, size_t base)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    return vigil_value_as_int(&v);
}

/* Builder() -> Builder */
static vigil_status_t builder_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    int64_t h = builder_alloc();
    vigil_value_t val = vigil_nanbox_encode_int(h);
    return vigil_vm_stack_push(vm, &val, error);
}

/* builder.write(s: string) -> void */
static vigil_status_t builder_write(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = get_builder_handle(vm, base);
    const char *s = NULL;
    size_t len = 0;
    get_str(vm, base, 1, &s, &len);
    vigil_vm_stack_pop_n(vm, arg_count);
    (void)error;
    builder_t *b = builder_get(h);
    if (b && s)
        builder_append(b, s, len);
    return VIGIL_STATUS_OK;
}

/* builder.to_string() -> string */
static vigil_status_t builder_to_string(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = get_builder_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    builder_t *b = builder_get(h);
    if (!b)
        return push_str(vm, "", 0, error);
    return push_str(vm, b->data ? b->data : "", b->len, error);
}

/* builder.len() -> i32 */
static vigil_status_t builder_len(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = get_builder_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    builder_t *b = builder_get(h);
    vigil_value_t val = vigil_nanbox_encode_i32(b ? (int32_t)b->len : 0);
    return vigil_vm_stack_push(vm, &val, error);
}

/* builder.clear() -> void */
static vigil_status_t builder_clear(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = get_builder_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    (void)error;
    builder_t *b = builder_get(h);
    if (b)
        b->len = 0;
    return VIGIL_STATUS_OK;
}

/* builder.destroy() -> void */
static vigil_status_t builder_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = get_builder_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    (void)error;
    builder_free(h);
    return VIGIL_STATUS_OK;
}

/* ── Class descriptor ────────────────────────────────────────────── */

static const vigil_native_class_field_t builder_fields[] = {
    {"handle", 6U, VIGIL_TYPE_I64, 0, NULL, 0U, 0, NULL, NULL},
};

static const int p_str[] = {VIGIL_TYPE_STRING};

/* clang-format off */
static const vigil_native_class_method_t builder_methods[] = {
    {"new", 3U, builder_new, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL, 1, "Builder", 7U, 0, NULL, NULL, NULL, NULL},
    {"write", 5U, builder_write, 1U, p_str, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL, NULL},
    {"to_string", 9U, builder_to_string, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     NULL},
    {"len", 3U, builder_len, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL, NULL},
    {"clear", 5U, builder_clear, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL, NULL},
    {"destroy", 7U, builder_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL, NULL},
};
/* clang-format on */

static const vigil_native_class_t strings_classes[] = {
    {"Builder", 7U, builder_fields, BUILDER_FIELD_COUNT, builder_methods,
     sizeof(builder_methods) / sizeof(builder_methods[0]), NULL, NULL},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_strings = {
    "strings", 7U, NULL, 0U, strings_classes, sizeof(strings_classes) / sizeof(strings_classes[0]), NULL, NULL, 0U};
