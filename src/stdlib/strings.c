/* strings.c — strings module: Builder class for efficient string concatenation. */

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

/* ── Builder handle registry ─────────────────────────────────────── */

typedef struct
{
    char *data;
    size_t len;
    size_t cap;
    vigil_allocator_t alloc;
} builder_t;

#define MAX_BUILDERS 256
static builder_t *builders[MAX_BUILDERS];

static int64_t builder_alloc(vigil_allocator_t alloc)
{
    for (int64_t i = 0; i < MAX_BUILDERS; i++)
    {
        if (builders[i] == NULL)
        {
            builders[i] = (builder_t *)alloc.allocate(alloc.user_data, sizeof(builder_t));
            if (!builders[i])
                return -1;
            memset(builders[i], 0, sizeof(builder_t));
            builders[i]->alloc = alloc;
            return i;
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
        vigil_allocator_t alloc = builders[h]->alloc;
        alloc.deallocate(alloc.user_data, builders[h]->data);
        alloc.deallocate(alloc.user_data, builders[h]);
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
        b->data = (char *)b->alloc.reallocate(b->alloc.user_data, b->data, new_cap);
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
static vigil_status_t builder_new_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_allocator_t alloc = get_alloc(vm);
    int64_t h = builder_alloc(alloc);
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
    (void)error;
    builder_t *b = builder_get(h);
    if (b && s)
        builder_append(b, s, len);
    vigil_vm_stack_pop_n(vm, arg_count);
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

/* ── Documentation ───────────────────────────────────────────────── */

static const vigil_native_symbol_doc_t vigil_strings_module_doc = {
    "String utilities and Builder for efficient concatenation.",
    "The strings module provides the Builder class for constructing strings "
    "incrementally without repeated allocation.",
    NULL,
};

static const vigil_native_symbol_doc_t doc_builder_class = {
    "Mutable string builder for efficient concatenation.",
    "Builder accumulates string fragments in a dynamic buffer and produces "
    "the final string with to_string(). Call destroy() to release resources.",
    "var b = strings.Builder.new()\nb.write(\"hello \")\nb.write(\"world\")\nstring s = b.to_string()\nb.destroy()",
};

static const vigil_native_symbol_doc_t doc_builder_new = {
    "Create a new Builder.",
    "Returns a fresh Builder instance with an empty buffer.",
    "var b = strings.Builder.new()",
};

static const vigil_native_symbol_doc_t doc_builder_write = {
    "Append a string to the builder.",
    "Writes the given string to the internal buffer, growing it as needed.",
    "b.write(\"hello\")",
};

static const vigil_native_symbol_doc_t doc_builder_to_string = {
    "Return the accumulated string.",
    "Returns the contents of the buffer as a string without consuming the builder.",
    "string s = b.to_string()",
};

static const vigil_native_symbol_doc_t doc_builder_len = {
    "Return the current length in bytes.",
    "Returns the number of bytes written to the builder so far.",
    "i32 n = b.len()",
};

static const vigil_native_symbol_doc_t doc_builder_clear = {
    "Reset the builder to empty.",
    "Clears the buffer length to zero without deallocating memory.",
    "b.clear()",
};

static const vigil_native_symbol_doc_t doc_builder_destroy = {
    "Free the builder resources.",
    "Releases the internal buffer and the builder handle. The builder must not be used after this call.",
    "b.destroy()",
};

/* ── Class descriptor ────────────────────────────────────────────── */

static const vigil_native_class_field_t builder_fields[] = {
    {"handle", 6U, VIGIL_TYPE_I64, 0, NULL, 0U, 0, NULL, NULL},
};

static const int p_str[] = {VIGIL_TYPE_STRING};

/* clang-format off */
static const vigil_native_class_method_t builder_methods[] = {
    {"new", 3U, builder_new_fn, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL, 1, "Builder", 7U, 0, NULL, NULL, NULL, &doc_builder_new},
    {"write", 5U, builder_write, 1U, p_str, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL, &doc_builder_write},
    {"to_string", 9U, builder_to_string, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL, &doc_builder_to_string},
    {"len", 3U, builder_len, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL, &doc_builder_len},
    {"clear", 5U, builder_clear, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL, &doc_builder_clear},
    {"destroy", 7U, builder_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL, &doc_builder_destroy},
};
/* clang-format on */

static const vigil_native_class_t strings_classes[] = {
    {"Builder", 7U, builder_fields, BUILDER_FIELD_COUNT, builder_methods,
     sizeof(builder_methods) / sizeof(builder_methods[0]), NULL, &doc_builder_class},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_strings = {"strings",
                                                              7U,
                                                              NULL,
                                                              0U,
                                                              strings_classes,
                                                              sizeof(strings_classes) / sizeof(strings_classes[0]),
                                                              &vigil_strings_module_doc,
                                                              NULL,
                                                              0U};
