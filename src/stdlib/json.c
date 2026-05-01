/* VIGIL standard library: json module.
 *
 * Exposes the in-tree JSON parser/emitter through a native Value class.
 * Uses a cached tree with handle registry to avoid re-parsing on every access.
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "vigil/json.h"
#include "vigil/native_module.h"
#include "vigil/runtime.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_internal.h"

/* ── Field layout ────────────────────────────────────────────────── */

enum
{
    F_RAW = 0,
    F_HANDLE = 1,
    JSON_VALUE_FIELD_COUNT = 2
};

#define JSON_ERR_IO 5
#define JSON_ERR_PARSE 8
#define JSON_ERR_TYPE 9

/* ── Handle registry ─────────────────────────────────────────────── */

#define MAX_JSON_HANDLES 512
typedef struct
{
    vigil_json_value_t *tree;
    int owned; /* 1 if this handle owns the tree and should free it */
} json_handle_t;
static json_handle_t json_handles[MAX_JSON_HANDLES];

static int64_t json_handle_alloc(vigil_json_value_t *tree, int owned)
{
    for (int64_t i = 0; i < MAX_JSON_HANDLES; i++)
    {
        if (json_handles[i].tree == NULL)
        {
            json_handles[i].tree = tree;
            json_handles[i].owned = owned;
            return i;
        }
    }
    return -1;
}
static vigil_json_value_t *json_handle_get(int64_t h)
{
    if (h < 0 || h >= MAX_JSON_HANDLES)
        return NULL;
    return json_handles[h].tree;
}
static void json_handle_free(int64_t h)
{
    if (h >= 0 && h < MAX_JSON_HANDLES && json_handles[h].tree)
    {
        if (json_handles[h].owned)
            vigil_json_free(&json_handles[h].tree);
        json_handles[h].tree = NULL;
        json_handles[h].owned = 0;
    }
}

/* ── Helpers ─────────────────────────────────────────────────────── */

static const char *json_type_name(vigil_json_type_t type)
{
    switch (type)
    {
    case VIGIL_JSON_NULL:
        return "null";
    case VIGIL_JSON_BOOL:
        return "bool";
    case VIGIL_JSON_NUMBER:
        return "number";
    case VIGIL_JSON_STRING:
        return "string";
    case VIGIL_JSON_ARRAY:
        return "array";
    case VIGIL_JSON_OBJECT:
        return "object";
    default:
        return "invalid";
    }
}

static const vigil_allocator_t *json_runtime_allocator(vigil_vm_t *vm)
{
    return vigil_runtime_allocator(vigil_vm_runtime(vm));
}

static vigil_object_t *json_get_self(vigil_vm_t *vm, size_t base)
{
    vigil_value_t value = vigil_vm_stack_get(vm, base);
    return vigil_value_as_object(&value);
}

static bool json_get_string_arg(vigil_vm_t *vm, size_t slot, const char **out_text, size_t *out_length)
{
    vigil_value_t value = vigil_vm_stack_get(vm, slot);
    vigil_object_t *object = vigil_value_as_object(&value);
    if (object == NULL || vigil_object_type(object) != VIGIL_OBJECT_STRING)
        return false;
    *out_text = vigil_string_object_c_str(object);
    *out_length = vigil_string_object_length(object);
    return true;
}

static bool json_get_raw_text(vigil_object_t *self, const char **out_text, size_t *out_length)
{
    vigil_value_t value;
    vigil_object_t *raw = NULL;

    if (self == NULL || !vigil_instance_object_get_field(self, F_RAW, &value))
        return false;

    raw = vigil_value_as_object(&value);
    if (raw == NULL || vigil_object_type(raw) != VIGIL_OBJECT_STRING)
    {
        vigil_value_release(&value);
        return false;
    }

    *out_text = vigil_string_object_c_str(raw);
    *out_length = vigil_string_object_length(raw);
    vigil_value_release(&value);
    return true;
}

static int64_t json_get_handle_field(vigil_object_t *self)
{
    vigil_value_t value;
    if (self == NULL || !vigil_instance_object_get_field(self, F_HANDLE, &value))
        return -1;
    return vigil_value_as_int(&value);
}

static vigil_json_value_t *json_get_tree(vigil_object_t *self)
{
    return json_handle_get(json_get_handle_field(self));
}

/* ── Stack push helpers ──────────────────────────────────────────── */

static vigil_status_t json_push_err_kind(vigil_vm_t *vm, const char *message, int64_t kind, vigil_error_t *error)
{
    vigil_object_t *err_object = NULL;
    vigil_value_t value;
    vigil_status_t status =
        vigil_error_object_new_cstr(vigil_vm_runtime(vm), message != NULL ? message : "", kind, &err_object, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    vigil_value_init_object(&value, &err_object);
    status = vigil_vm_stack_push(vm, &value, error);
    vigil_value_release(&value);
    return status;
}

static vigil_status_t json_push_string(vigil_vm_t *vm, const char *text, size_t length, vigil_error_t *error)
{
    vigil_object_t *string = NULL;
    vigil_value_t value;
    vigil_status_t status = vigil_string_object_new(vigil_vm_runtime(vm), text, length, &string, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    vigil_value_init_object(&value, &string);
    status = vigil_vm_stack_push(vm, &value, error);
    vigil_value_release(&value);
    return status;
}

static vigil_status_t json_push_string_and_ok(vigil_vm_t *vm, const char *text, size_t length, vigil_error_t *error)
{
    vigil_status_t status = json_push_string(vm, text, length, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t json_push_bool_and_ok(vigil_vm_t *vm, bool boolean, vigil_error_t *error)
{
    vigil_value_t value;
    vigil_status_t status;
    vigil_value_init_bool(&value, boolean);
    status = vigil_vm_stack_push(vm, &value, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t json_push_i32_and_ok(vigil_vm_t *vm, int64_t integer, vigil_error_t *error)
{
    vigil_value_t value;
    vigil_status_t status;
    vigil_value_init_int(&value, integer);
    status = vigil_vm_stack_push(vm, &value, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t json_push_f64_and_ok(vigil_vm_t *vm, double number, vigil_error_t *error)
{
    vigil_value_t value;
    vigil_status_t status;
    vigil_value_init_float(&value, number);
    status = vigil_vm_stack_push(vm, &value, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t json_push_nil_and_err(vigil_vm_t *vm, const char *message, int64_t kind, vigil_error_t *error)
{
    vigil_value_t value;
    vigil_status_t status;
    vigil_value_init_nil(&value);
    status = vigil_vm_stack_push(vm, &value, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    return json_push_err_kind(vm, message, kind, error);
}

static vigil_status_t json_push_false_and_err(vigil_vm_t *vm, const char *message, int64_t kind, vigil_error_t *error)
{
    vigil_value_t value;
    vigil_status_t status;
    vigil_value_init_bool(&value, false);
    status = vigil_vm_stack_push(vm, &value, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    return json_push_err_kind(vm, message, kind, error);
}

static vigil_status_t json_push_zero_i32_and_err(vigil_vm_t *vm, const char *message, int64_t kind,
                                                 vigil_error_t *error)
{
    vigil_value_t value;
    vigil_status_t status;
    vigil_value_init_int(&value, 0);
    status = vigil_vm_stack_push(vm, &value, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    return json_push_err_kind(vm, message, kind, error);
}

static vigil_status_t json_push_zero_f64_and_err(vigil_vm_t *vm, const char *message, int64_t kind,
                                                 vigil_error_t *error)
{
    vigil_value_t value;
    vigil_status_t status;
    vigil_value_init_float(&value, 0.0);
    status = vigil_vm_stack_push(vm, &value, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    return json_push_err_kind(vm, message, kind, error);
}

static vigil_status_t json_push_empty_string_and_err(vigil_vm_t *vm, const char *message, int64_t kind,
                                                     vigil_error_t *error)
{
    vigil_status_t status = json_push_string(vm, "", 0U, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    return json_push_err_kind(vm, message, kind, error);
}

static vigil_status_t json_push_object(vigil_vm_t *vm, vigil_object_t **object, vigil_error_t *error)
{
    vigil_value_t value;
    vigil_status_t status;
    vigil_value_init_object(&value, object);
    status = vigil_vm_stack_push(vm, &value, error);
    vigil_value_release(&value);
    return status;
}

static vigil_status_t json_push_object_and_ok(vigil_vm_t *vm, vigil_object_t **object, vigil_error_t *error)
{
    vigil_value_t value;
    vigil_status_t status;
    vigil_value_init_object(&value, object);
    status = vigil_vm_stack_push(vm, &value, error);
    vigil_value_release(&value);
    if (status != VIGIL_STATUS_OK)
        return status;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

/* ── Wrap helpers ────────────────────────────────────────────────── */

static vigil_status_t json_wrap_tree(vigil_runtime_t *runtime, size_t class_index, vigil_json_value_t *tree, int owned,
                                     vigil_object_t **out_instance, vigil_error_t *error)
{
    const vigil_allocator_t *allocator = vigil_runtime_allocator(runtime);
    vigil_status_t status;
    vigil_object_t *raw_string = NULL;
    vigil_object_t *instance = NULL;
    vigil_value_t fields[JSON_VALUE_FIELD_COUNT];
    char *emitted = NULL;
    size_t emitted_length = 0U;
    int64_t h;

    *out_instance = NULL;
    vigil_value_init_nil(&fields[F_RAW]);
    vigil_value_init_int(&fields[F_HANDLE], -1);

    h = json_handle_alloc(tree, owned);
    if (h < 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "json: handle registry full");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    vigil_value_init_int(&fields[F_HANDLE], h);

    /* Generate raw text for backward compat */
    status = vigil_json_emit(tree, &emitted, &emitted_length, error);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    status = vigil_string_object_new(runtime, emitted, emitted_length, &raw_string, error);
    if (allocator != NULL && emitted != NULL)
        allocator->deallocate(allocator->user_data, emitted);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;
    vigil_value_init_object(&fields[F_RAW], &raw_string);

    status = vigil_instance_object_new(runtime, class_index, fields, JSON_VALUE_FIELD_COUNT, &instance, error);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    *out_instance = instance;
    instance = NULL;
    status = VIGIL_STATUS_OK;

cleanup:
    if (status != VIGIL_STATUS_OK)
    {
        json_handle_free(h);
    }
    vigil_object_release(&instance);
    vigil_object_release(&raw_string);
    vigil_value_release(&fields[F_RAW]);
    return status;
}

/* Regenerate the raw field from the cached tree after mutation */
static vigil_status_t json_sync_raw(vigil_vm_t *vm, vigil_object_t *self, vigil_json_value_t *tree,
                                    vigil_error_t *error)
{
    const vigil_allocator_t *allocator = json_runtime_allocator(vm);
    vigil_runtime_t *runtime = vigil_vm_runtime(vm);
    char *emitted = NULL;
    size_t emitted_length = 0U;
    vigil_object_t *raw_string = NULL;
    vigil_value_t raw_value;
    vigil_status_t status;

    status = vigil_json_emit(tree, &emitted, &emitted_length, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    status = vigil_string_object_new(runtime, emitted, emitted_length, &raw_string, error);
    if (allocator != NULL && emitted != NULL)
        allocator->deallocate(allocator->user_data, emitted);
    if (status != VIGIL_STATUS_OK)
        return status;

    vigil_value_init_object(&raw_value, &raw_string);
    status = vigil_instance_object_set_field(self, F_RAW, &raw_value, error);
    vigil_value_release(&raw_value);
    return status;
}

static const vigil_object_t *json_current_function(vigil_vm_t *vm)
{
    size_t frame_depth = vigil_vm_frame_depth(vm);
    if (frame_depth == 0U)
        return NULL;
    return vigil_vm_frame_function(vm, frame_depth - 1U);
}

static vigil_status_t json_make_number(vigil_vm_t *vm, double number, vigil_json_value_t **out_json,
                                       vigil_error_t *error)
{
    return vigil_json_number_new(json_runtime_allocator(vm), number, out_json, error);
}

static const vigil_json_value_t *json_object_get_len(const vigil_json_value_t *object, const char *key,
                                                     size_t key_length)
{
    size_t index;
    if (object == NULL || vigil_json_type(object) != VIGIL_JSON_OBJECT)
        return NULL;
    for (index = 0U; index < vigil_json_object_count(object); ++index)
    {
        const char *entry_key = NULL;
        size_t entry_key_length = 0U;
        const vigil_json_value_t *entry_value = NULL;
        if (vigil_json_object_entry(object, index, &entry_key, &entry_key_length, &entry_value) != VIGIL_STATUS_OK)
            return NULL;
        if (entry_key_length == key_length && memcmp(entry_key, key, key_length) == 0)
            return entry_value;
    }
    return NULL;
}

static int json_number_to_i64(double number, int64_t min_value, int64_t max_value, int64_t *out_value)
{
    if (!isfinite(number) || floor(number) != number || number < (double)min_value || number > (double)max_value)
        return 0;
    *out_value = (int64_t)number;
    return 1;
}

static vigil_status_t json_parse_text(vigil_vm_t *vm, const char *text, size_t length, vigil_json_value_t **out_json,
                                      const char **out_message)
{
    vigil_error_t local_error = {0};
    vigil_status_t status =
        vigil_json_parse(vigil_runtime_allocator(vigil_vm_runtime(vm)), text, length, out_json, &local_error);
    if (status == VIGIL_STATUS_OK)
        return VIGIL_STATUS_OK;
    if (out_message != NULL)
        *out_message = vigil_error_message(&local_error);
    if (status == VIGIL_STATUS_SYNTAX_ERROR)
    {
        *out_json = NULL;
        return VIGIL_STATUS_OK;
    }
    return status;
}

/* ── Encode / Decode ─────────────────────────────────────────────── */

static vigil_status_t json_encode_value(vigil_vm_t *vm, const vigil_object_t *function, const vigil_value_t *value,
                                        vigil_json_value_t **out_json, vigil_error_t *error);
static vigil_status_t json_decode_primitive_type(vigil_vm_t *vm, const vigil_json_value_t *json, vigil_type_kind_t kind,
                                                 vigil_value_t *out_value, vigil_error_t *error);
static vigil_status_t json_decode_to_type(vigil_vm_t *vm, const vigil_object_t *function,
                                          const vigil_json_value_t *json, vigil_runtime_resolved_type_t type,
                                          vigil_value_t *out_value, vigil_error_t *error);

static vigil_status_t json_encode_instance(vigil_vm_t *vm, const vigil_object_t *function, const vigil_object_t *object,
                                           vigil_json_value_t **out_json, vigil_error_t *error)
{
    size_t class_index = vigil_instance_object_class_index(object);
    size_t field_count = vigil_instance_object_field_count(object);
    vigil_json_value_t *json = NULL;
    vigil_status_t status;
    size_t field_index;

    status = vigil_json_object_new(json_runtime_allocator(vm), &json, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    for (field_index = 0U; field_index < field_count; ++field_index)
    {
        const char *field_name = NULL;
        size_t field_name_length = 0U;
        vigil_runtime_resolved_type_t field_type;
        int is_public = 0;
        vigil_value_t field_value;
        vigil_json_value_t *field_json = NULL;

        if (!vigil_function_object_get_class_field(function, class_index, field_index, &field_name, &field_name_length,
                                                   &field_type, &is_public))
        {
            vigil_json_free(&json);
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "encode: missing class metadata");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        (void)field_type;
        if (!is_public)
            continue;
        if (!vigil_instance_object_get_field(object, field_index, &field_value))
        {
            vigil_json_free(&json);
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "encode: missing instance field");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }

        status = json_encode_value(vm, function, &field_value, &field_json, error);
        vigil_value_release(&field_value);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_json_free(&json);
            return status;
        }

        status = vigil_json_object_set(json, field_name, field_name_length, field_json, error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_json_free(&field_json);
            vigil_json_free(&json);
            return status;
        }
    }

    *out_json = json;
    return VIGIL_STATUS_OK;
}

static vigil_status_t json_encode_array(vigil_vm_t *vm, const vigil_object_t *function, const vigil_object_t *object,
                                        vigil_json_value_t **out_json, vigil_error_t *error)
{
    vigil_json_value_t *json = NULL;
    vigil_status_t status;
    size_t index;
    size_t length = vigil_array_object_length(object);

    (void)function;
    status = vigil_json_array_new(json_runtime_allocator(vm), &json, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    for (index = 0U; index < length; ++index)
    {
        vigil_value_t item;
        vigil_json_value_t *item_json = NULL;

        if (!vigil_array_object_get(object, index, &item))
        {
            vigil_json_free(&json);
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "encode: array access failed");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }

        status = json_encode_value(vm, function, &item, &item_json, error);
        vigil_value_release(&item);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_json_free(&json);
            return status;
        }

        status = vigil_json_array_push(json, item_json, error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_json_free(&item_json);
            vigil_json_free(&json);
            return status;
        }
    }

    *out_json = json;
    return VIGIL_STATUS_OK;
}

static vigil_status_t json_encode_map(vigil_vm_t *vm, const vigil_object_t *function, const vigil_object_t *object,
                                      vigil_json_value_t **out_json, vigil_error_t *error)
{
    vigil_json_value_t *json = NULL;
    vigil_status_t status;
    size_t index;
    size_t count = vigil_map_object_count(object);

    status = vigil_json_object_new(json_runtime_allocator(vm), &json, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    for (index = 0U; index < count; ++index)
    {
        vigil_value_t key;
        vigil_value_t value;
        vigil_object_t *key_object;
        vigil_json_value_t *value_json = NULL;
        const char *key_text;
        size_t key_length;

        if (!vigil_map_object_key_at(object, index, &key) || !vigil_map_object_value_at(object, index, &value))
        {
            vigil_json_free(&json);
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "encode: map access failed");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }

        key_object = vigil_value_as_object(&key);
        if (vigil_value_kind(&key) != VIGIL_VALUE_OBJECT || key_object == NULL ||
            vigil_object_type(key_object) != VIGIL_OBJECT_STRING)
        {
            vigil_value_release(&key);
            vigil_value_release(&value);
            vigil_json_free(&json);
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "encode: map keys must be strings");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        key_text = vigil_string_object_c_str(key_object);
        key_length = vigil_string_object_length(key_object);

        status = json_encode_value(vm, function, &value, &value_json, error);
        vigil_value_release(&key);
        vigil_value_release(&value);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_json_free(&json);
            return status;
        }

        status = vigil_json_object_set(json, key_text, key_length, value_json, error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_json_free(&value_json);
            vigil_json_free(&json);
            return status;
        }
    }

    *out_json = json;
    return VIGIL_STATUS_OK;
}

static vigil_status_t json_encode_value(vigil_vm_t *vm, const vigil_object_t *function, const vigil_value_t *value,
                                        vigil_json_value_t **out_json, vigil_error_t *error)
{
    vigil_object_t *object = NULL;

    *out_json = NULL;
    switch (vigil_value_kind(value))
    {
    case VIGIL_VALUE_BOOL:
        return vigil_json_bool_new(json_runtime_allocator(vm), vigil_value_as_bool(value) ? 1 : 0, out_json, error);
    case VIGIL_VALUE_INT:
        return json_make_number(vm, (double)vigil_value_as_int(value), out_json, error);
    case VIGIL_VALUE_UINT:
        return json_make_number(vm, (double)vigil_value_as_uint(value), out_json, error);
    case VIGIL_VALUE_FLOAT:
        return json_make_number(vm, vigil_value_as_float(value), out_json, error);
    case VIGIL_VALUE_OBJECT:
        object = vigil_value_as_object(value);
        if (object == NULL)
            break;
        switch (vigil_object_type(object))
        {
        case VIGIL_OBJECT_STRING:
            return vigil_json_string_new(json_runtime_allocator(vm), vigil_string_object_c_str(object),
                                         vigil_string_object_length(object), out_json, error);
        case VIGIL_OBJECT_ARRAY:
            return json_encode_array(vm, function, object, out_json, error);
        case VIGIL_OBJECT_MAP:
            return json_encode_map(vm, function, object, out_json, error);
        case VIGIL_OBJECT_INSTANCE:
            return json_encode_instance(vm, function, object, out_json, error);
        default:
            break;
        }
        break;
    case VIGIL_VALUE_NIL:
    default:
        break;
    }

    vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "encode: unsupported value type");
    return VIGIL_STATUS_INVALID_ARGUMENT;
}

/* ── Decode (lenient: missing fields get defaults, extra fields ignored) ── */

static vigil_status_t json_decode_class(vigil_vm_t *vm, const vigil_object_t *function, const vigil_json_value_t *json,
                                        size_t class_index, vigil_value_t *out_value, vigil_error_t *error)
{
    size_t field_count;
    vigil_value_t *fields = NULL;
    vigil_object_t *instance = NULL;
    vigil_status_t status = VIGIL_STATUS_OK;
    void *memory = NULL;
    size_t field_index;

    if (vigil_json_type(json) != VIGIL_JSON_OBJECT)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "decode: expected object");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    field_count = vigil_function_object_class_field_count(function, class_index);
    if (field_count == 0U && vigil_json_object_count(json) != 0U)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "decode: missing class metadata");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (field_count != 0U)
    {
        status = vigil_runtime_alloc(vigil_vm_runtime(vm), field_count * sizeof(*fields), &memory, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        fields = (vigil_value_t *)memory;
        for (field_index = 0U; field_index < field_count; ++field_index)
            vigil_value_init_nil(&fields[field_index]);
    }

    for (field_index = 0U; field_index < field_count; ++field_index)
    {
        const char *field_name = NULL;
        size_t field_name_length = 0U;
        vigil_runtime_resolved_type_t field_type;
        int is_public = 0;
        const vigil_json_value_t *child = NULL;

        if (!vigil_function_object_get_class_field(function, class_index, field_index, &field_name, &field_name_length,
                                                   &field_type, &is_public))
        {
            status = VIGIL_STATUS_INVALID_ARGUMENT;
            vigil_error_set_literal(error, status, "decode: missing class field metadata");
            goto cleanup;
        }
        if (!is_public)
        {
            status = VIGIL_STATUS_INVALID_ARGUMENT;
            vigil_error_set_literal(error, status, "decode: non-public fields are not supported");
            goto cleanup;
        }

        child = json_object_get_len(json, field_name, field_name_length);
        if (child == NULL)
        {
            /* Lenient: use zero/empty/false default for missing fields */
            switch (field_type.kind)
            {
            case VIGIL_TYPE_BOOL:
                vigil_value_init_bool(&fields[field_index], false);
                break;
            case VIGIL_TYPE_I32:
            case VIGIL_TYPE_I64:
                vigil_value_init_int(&fields[field_index], 0);
                break;
            case VIGIL_TYPE_U8:
            case VIGIL_TYPE_U32:
            case VIGIL_TYPE_U64:
                vigil_value_init_uint(&fields[field_index], 0);
                break;
            case VIGIL_TYPE_F64:
                vigil_value_init_float(&fields[field_index], 0.0);
                break;
            case VIGIL_TYPE_STRING: {
                vigil_object_t *empty = NULL;
                status = vigil_string_object_new(vigil_vm_runtime(vm), "", 0U, &empty, error);
                if (status != VIGIL_STATUS_OK)
                    goto cleanup;
                vigil_value_init_object(&fields[field_index], &empty);
                break;
            }
            default:
                vigil_value_init_nil(&fields[field_index]);
                break;
            }
            continue;
        }

        status = json_decode_to_type(vm, function, child, field_type, &fields[field_index], error);
        if (status != VIGIL_STATUS_OK)
            goto cleanup;
    }

    /* Extra fields in JSON are silently ignored (lenient) */

    status = vigil_instance_object_new(vigil_vm_runtime(vm), class_index, fields, field_count, &instance, error);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    vigil_value_init_object(out_value, &instance);

cleanup:
    if (fields != NULL)
    {
        for (field_index = 0U; field_index < field_count; ++field_index)
            vigil_value_release(&fields[field_index]);
        memory = fields;
        vigil_runtime_free(vigil_vm_runtime(vm), &memory);
    }
    return status;
}

static vigil_status_t json_decode_array(vigil_vm_t *vm, const vigil_object_t *function, const vigil_json_value_t *json,
                                        vigil_runtime_resolved_type_t type, vigil_value_t *out_value,
                                        vigil_error_t *error)
{
    vigil_runtime_resolved_type_t element_type;
    vigil_object_t *array = NULL;
    vigil_status_t status;
    size_t index;

    if (vigil_json_type(json) != VIGIL_JSON_ARRAY)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "decode: expected array");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (!vigil_function_object_get_array_type(function, type.object_index, &element_type))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "decode: missing array type metadata");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0U, &array, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    for (index = 0U; index < vigil_json_array_count(json); ++index)
    {
        vigil_value_t item_value;
        vigil_value_init_nil(&item_value);
        status = json_decode_to_type(vm, function, vigil_json_array_get(json, index), element_type, &item_value, error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_value_release(&item_value);
            vigil_object_release(&array);
            return status;
        }
        status = vigil_array_object_append(array, &item_value, error);
        vigil_value_release(&item_value);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_object_release(&array);
            return status;
        }
    }

    vigil_value_init_object(out_value, &array);
    return VIGIL_STATUS_OK;
}

static vigil_status_t json_decode_map(vigil_vm_t *vm, const vigil_object_t *function, const vigil_json_value_t *json,
                                      vigil_runtime_resolved_type_t type, vigil_value_t *out_value,
                                      vigil_error_t *error)
{
    vigil_runtime_resolved_type_t key_type;
    vigil_runtime_resolved_type_t value_type;
    vigil_object_t *map = NULL;
    vigil_status_t status;
    size_t index;

    if (vigil_json_type(json) != VIGIL_JSON_OBJECT)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "decode: expected object for map");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (!vigil_function_object_get_map_type(function, type.object_index, &key_type, &value_type))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "decode: missing map type metadata");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (key_type.kind != VIGIL_TYPE_STRING || key_type.object_kind != VIGIL_RUNTIME_OBJECT_NONE)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "decode: only map<string, T> is supported");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_map_object_new(vigil_vm_runtime(vm), &map, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    for (index = 0U; index < vigil_json_object_count(json); ++index)
    {
        const char *key_text = NULL;
        size_t key_length = 0U;
        const vigil_json_value_t *child = NULL;
        vigil_object_t *key_object = NULL;
        vigil_value_t key_value;
        vigil_value_t value;

        vigil_value_init_nil(&key_value);
        vigil_value_init_nil(&value);
        status = vigil_json_object_entry(json, index, &key_text, &key_length, &child);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_object_release(&map);
            return status;
        }

        status = vigil_string_object_new(vigil_vm_runtime(vm), key_text, key_length, &key_object, error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_object_release(&map);
            return status;
        }
        vigil_value_init_object(&key_value, &key_object);

        status = json_decode_to_type(vm, function, child, value_type, &value, error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_value_release(&key_value);
            vigil_value_release(&value);
            vigil_object_release(&map);
            return status;
        }

        status = vigil_map_object_set(map, &key_value, &value, error);
        vigil_value_release(&key_value);
        vigil_value_release(&value);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_object_release(&map);
            return status;
        }
    }

    vigil_value_init_object(out_value, &map);
    return VIGIL_STATUS_OK;
}

static vigil_status_t json_decode_to_type(vigil_vm_t *vm, const vigil_object_t *function,
                                          const vigil_json_value_t *json, vigil_runtime_resolved_type_t type,
                                          vigil_value_t *out_value, vigil_error_t *error)
{
    vigil_value_init_nil(out_value);
    if (type.object_kind == VIGIL_RUNTIME_OBJECT_CLASS)
        return json_decode_class(vm, function, json, type.object_index, out_value, error);
    if (type.object_kind == VIGIL_RUNTIME_OBJECT_ARRAY)
        return json_decode_array(vm, function, json, type, out_value, error);
    if (type.object_kind == VIGIL_RUNTIME_OBJECT_MAP)
        return json_decode_map(vm, function, json, type, out_value, error);
    if (type.object_kind != VIGIL_RUNTIME_OBJECT_NONE)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "decode: unsupported target type");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    return json_decode_primitive_type(vm, json, type.kind, out_value, error);
}

static vigil_status_t json_decode_unsigned_type(const vigil_json_value_t *json, vigil_type_kind_t kind,
                                                vigil_value_t *out_value)
{
    double number;
    int64_t integer;

    if (vigil_json_type(json) != VIGIL_JSON_NUMBER)
        return VIGIL_STATUS_INVALID_ARGUMENT;

    switch (kind)
    {
    case VIGIL_TYPE_U8:
        if (!json_number_to_i64(vigil_json_number_value(json), 0, UINT8_MAX, &integer))
            return VIGIL_STATUS_INVALID_ARGUMENT;
        vigil_value_init_uint(out_value, (uint64_t)integer);
        return VIGIL_STATUS_OK;
    case VIGIL_TYPE_U32:
        if (!json_number_to_i64(vigil_json_number_value(json), 0, INT64_C(4294967295), &integer))
            return VIGIL_STATUS_INVALID_ARGUMENT;
        vigil_value_init_uint(out_value, (uint64_t)integer);
        return VIGIL_STATUS_OK;
    case VIGIL_TYPE_U64:
        number = vigil_json_number_value(json);
        if (!isfinite(number) || floor(number) != number || number < 0.0 || number > 18446744073709551615.0)
            return VIGIL_STATUS_INVALID_ARGUMENT;
        vigil_value_init_uint(out_value, (uint64_t)number);
        return VIGIL_STATUS_OK;
    default:
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
}

static vigil_status_t json_decode_string_type(vigil_vm_t *vm, const vigil_json_value_t *json, vigil_value_t *out_value,
                                              vigil_error_t *error)
{
    vigil_object_t *string = NULL;
    vigil_status_t status;

    if (vigil_json_type(json) != VIGIL_JSON_STRING)
        return VIGIL_STATUS_INVALID_ARGUMENT;

    status = vigil_string_object_new(vigil_vm_runtime(vm), vigil_json_string_value(json),
                                     vigil_json_string_length(json), &string, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    vigil_value_init_object(out_value, &string);
    return VIGIL_STATUS_OK;
}

static vigil_status_t json_decode_primitive_type(vigil_vm_t *vm, const vigil_json_value_t *json, vigil_type_kind_t kind,
                                                 vigil_value_t *out_value, vigil_error_t *error)
{
    int64_t integer;
    vigil_status_t status;

    switch (kind)
    {
    case VIGIL_TYPE_BOOL:
        if (vigil_json_type(json) != VIGIL_JSON_BOOL)
            break;
        vigil_value_init_bool(out_value, vigil_json_bool_value(json) != 0);
        return VIGIL_STATUS_OK;
    case VIGIL_TYPE_F64:
        if (vigil_json_type(json) != VIGIL_JSON_NUMBER)
            break;
        vigil_value_init_float(out_value, vigil_json_number_value(json));
        return VIGIL_STATUS_OK;
    case VIGIL_TYPE_I32:
        if (vigil_json_type(json) != VIGIL_JSON_NUMBER ||
            !json_number_to_i64(vigil_json_number_value(json), INT32_MIN, INT32_MAX, &integer))
            break;
        vigil_value_init_int(out_value, integer);
        return VIGIL_STATUS_OK;
    case VIGIL_TYPE_I64:
        if (vigil_json_type(json) != VIGIL_JSON_NUMBER ||
            !json_number_to_i64(vigil_json_number_value(json), INT64_MIN, INT64_MAX, &integer))
            break;
        vigil_value_init_int(out_value, integer);
        return VIGIL_STATUS_OK;
    case VIGIL_TYPE_U8:
    case VIGIL_TYPE_U32:
    case VIGIL_TYPE_U64:
        status = json_decode_unsigned_type(json, kind, out_value);
        if (status == VIGIL_STATUS_OK)
            return status;
        break;
    case VIGIL_TYPE_STRING:
        return json_decode_string_type(vm, json, out_value, error);
    default:
        break;
    }

    vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "decode: json value does not match target type");
    return VIGIL_STATUS_INVALID_ARGUMENT;
}

/* ── File I/O ────────────────────────────────────────────────────── */

static vigil_status_t json_read_file(vigil_vm_t *vm, const char *path, char **out_data, size_t *out_length,
                                     const char **out_message, vigil_error_t *error)
{
    const vigil_allocator_t *allocator = json_runtime_allocator(vm);
    FILE *file = NULL;
    char *data = NULL;
    long size_long;
    size_t size;
    size_t read_count;
    int read_error = 0;

    *out_data = NULL;
    *out_length = 0U;
    if (out_message != NULL)
        *out_message = NULL;

#ifdef _WIN32
    {
        errno_t open_status = fopen_s(&file, path, "rb");
        if (open_status != 0)
            file = NULL;
    }
#else
    file = fopen(path, "rb");
#endif

    if (file == NULL)
    {
        if (out_message)
            *out_message = "read: could not open file";
        return VIGIL_STATUS_OK;
    }
    if (fseek(file, 0L, SEEK_END) != 0)
    {
        if (out_message)
            *out_message = "read: could not seek file";
        fclose(file);
        return VIGIL_STATUS_OK;
    }
    size_long = ftell(file);
    if (size_long < 0)
    {
        if (out_message)
            *out_message = "read: could not size file";
        fclose(file);
        return VIGIL_STATUS_OK;
    }
    if (fseek(file, 0L, SEEK_SET) != 0)
    {
        if (out_message)
            *out_message = "read: could not rewind file";
        fclose(file);
        return VIGIL_STATUS_OK;
    }

    size = (size_t)size_long;
    data = (char *)allocator->allocate(allocator->user_data, size + 1U);
    if (data == NULL)
    {
        fclose(file);
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "json read: allocation failed");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    read_count = fread(data, 1U, size, file);
    read_error = ferror(file);
    fclose(file);
    if (read_count != size && read_error)
    {
        allocator->deallocate(allocator->user_data, data);
        if (out_message != NULL)
            *out_message = "read: could not read file";
        return VIGIL_STATUS_OK;
    }

    data[read_count] = '\0';
    *out_data = data;
    *out_length = read_count;
    return VIGIL_STATUS_OK;
}

static vigil_status_t json_write_file(vigil_vm_t *vm, const char *path, const char *text, size_t length,
                                      const char **out_message)
{
    FILE *file = NULL;
    size_t write_count;
    (void)vm;
    if (out_message != NULL)
        *out_message = NULL;

#ifdef _WIN32
    {
        errno_t open_status = fopen_s(&file, path, "wb");
        if (open_status != 0)
            file = NULL;
    }
#else
    file = fopen(path, "wb");
#endif

    if (file == NULL)
    {
        if (out_message)
            *out_message = "write: could not open file";
        return VIGIL_STATUS_OK;
    }
    write_count = fwrite(text, 1U, length, file);
    if (write_count != length)
    {
        fclose(file);
        if (out_message)
            *out_message = "write: could not write file";
        return VIGIL_STATUS_OK;
    }
    if (fclose(file) != 0)
    {
        if (out_message)
            *out_message = "write: could not close file";
        return VIGIL_STATUS_OK;
    }
    return VIGIL_STATUS_OK;
}

/* ── Value methods: parse, read ──────────────────────────────────── */

static vigil_status_t json_value_parse(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_runtime_t *runtime = vigil_vm_runtime(vm);
    const char *text = NULL;
    size_t length = 0U;
    size_t class_index;
    const char *message = "parse: expected string";
    vigil_json_value_t *json = NULL;
    vigil_object_t *instance = NULL;
    vigil_status_t status;

    class_index = (size_t)vigil_value_as_int(&(vigil_value_t){vigil_vm_stack_get(vm, base)});
    if (!json_get_string_arg(vm, base + 1U, &text, &length))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_nil_and_err(vm, message, JSON_ERR_PARSE, error);
    }

    status = json_parse_text(vm, text, length, &json, &message);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }
    if (json == NULL)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_nil_and_err(vm, message, JSON_ERR_PARSE, error);
    }

    status = json_wrap_tree(runtime, class_index, json, 1, &instance, error);
    /* json_wrap_tree takes ownership on success; on failure json is leaked — free it */
    if (status != VIGIL_STATUS_OK)
        vigil_json_free(&json);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (status != VIGIL_STATUS_OK)
        return status;
    return json_push_object_and_ok(vm, &instance, error);
}

static vigil_status_t json_value_read(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    const vigil_allocator_t *allocator = json_runtime_allocator(vm);
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_runtime_t *runtime = vigil_vm_runtime(vm);
    const char *path = NULL;
    size_t path_length = 0U;
    size_t class_index;
    const char *message = "read: expected path string";
    char *text = NULL;
    size_t length = 0U;
    vigil_json_value_t *json = NULL;
    vigil_object_t *instance = NULL;
    vigil_status_t status;

    (void)path_length;
    class_index = (size_t)vigil_value_as_int(&(vigil_value_t){vigil_vm_stack_get(vm, base)});
    if (!json_get_string_arg(vm, base + 1U, &path, &path_length))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_nil_and_err(vm, message, JSON_ERR_IO, error);
    }

    status = json_read_file(vm, path, &text, &length, &message, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }
    if (text == NULL)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_nil_and_err(vm, message, JSON_ERR_IO, error);
    }

    status = json_parse_text(vm, text, length, &json, &message);
    if (allocator != NULL)
        allocator->deallocate(allocator->user_data, text);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }
    if (json == NULL)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_nil_and_err(vm, message, JSON_ERR_PARSE, error);
    }

    status = json_wrap_tree(runtime, class_index, json, 1, &instance, error);
    if (status != VIGIL_STATUS_OK)
        vigil_json_free(&json);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (status != VIGIL_STATUS_OK)
        return status;
    return json_push_object_and_ok(vm, &instance, error);
}

/* ── Accessor methods (use cached tree) ──────────────────────────── */

static vigil_status_t json_value_kind(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_json_value_t *tree = json_get_tree(json_get_self(vm, base));
    const char *kind_name = tree != NULL ? json_type_name(vigil_json_type(tree)) : "invalid";
    vigil_vm_stack_pop_n(vm, arg_count);
    return json_push_string(vm, kind_name, strlen(kind_name), error);
}

static vigil_status_t json_value_is_kind(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error, vigil_json_type_t want)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_json_value_t *tree = json_get_tree(json_get_self(vm, base));
    bool matches = tree != NULL && vigil_json_type(tree) == want;
    vigil_vm_stack_pop_n(vm, arg_count);
    {
        vigil_value_t value;
        vigil_value_init_bool(&value, matches);
        return vigil_vm_stack_push(vm, &value, error);
    }
}

static vigil_status_t json_value_is_null(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return json_value_is_kind(vm, arg_count, error, VIGIL_JSON_NULL);
}
static vigil_status_t json_value_is_bool(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return json_value_is_kind(vm, arg_count, error, VIGIL_JSON_BOOL);
}
static vigil_status_t json_value_is_number(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return json_value_is_kind(vm, arg_count, error, VIGIL_JSON_NUMBER);
}
static vigil_status_t json_value_is_string(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return json_value_is_kind(vm, arg_count, error, VIGIL_JSON_STRING);
}
static vigil_status_t json_value_is_array(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return json_value_is_kind(vm, arg_count, error, VIGIL_JSON_ARRAY);
}
static vigil_status_t json_value_is_object(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return json_value_is_kind(vm, arg_count, error, VIGIL_JSON_OBJECT);
}

static vigil_status_t json_value_len(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_json_value_t *tree = json_get_tree(json_get_self(vm, base));
    size_t length = 0U;

    if (tree == NULL)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_zero_i32_and_err(vm, "len: invalid json.Value", JSON_ERR_PARSE, error);
    }
    if (vigil_json_type(tree) == VIGIL_JSON_ARRAY)
        length = vigil_json_array_count(tree);
    else if (vigil_json_type(tree) == VIGIL_JSON_OBJECT)
        length = vigil_json_object_count(tree);
    else
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_zero_i32_and_err(vm, "len: value is not an array or object", JSON_ERR_PARSE, error);
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    if (length > (size_t)INT32_MAX)
        return json_push_zero_i32_and_err(vm, "len: value is too large", JSON_ERR_PARSE, error);
    return json_push_i32_and_ok(vm, (int64_t)length, error);
}

static vigil_status_t json_value_as_bool(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_json_value_t *tree = json_get_tree(json_get_self(vm, base));

    if (tree == NULL || vigil_json_type(tree) != VIGIL_JSON_BOOL)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_false_and_err(vm, "as_bool: value is not a bool", JSON_ERR_PARSE, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    return json_push_bool_and_ok(vm, vigil_json_bool_value(tree) != 0, error);
}

static vigil_status_t json_value_as_number(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_json_value_t *tree = json_get_tree(json_get_self(vm, base));

    if (tree == NULL || vigil_json_type(tree) != VIGIL_JSON_NUMBER)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_zero_f64_and_err(vm, "as_number: value is not a number", JSON_ERR_PARSE, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    return json_push_f64_and_ok(vm, vigil_json_number_value(tree), error);
}

static vigil_status_t json_value_as_string(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_json_value_t *tree = json_get_tree(json_get_self(vm, base));

    if (tree == NULL || vigil_json_type(tree) != VIGIL_JSON_STRING)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_empty_string_and_err(vm, "as_string: value is not a string", JSON_ERR_PARSE, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    return json_push_string_and_ok(vm, vigil_json_string_value(tree), vigil_json_string_length(tree), error);
}

static vigil_status_t json_value_at(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = json_get_self(vm, base);
    size_t class_index = vigil_instance_object_class_index(self);
    vigil_json_value_t *tree = json_get_tree(self);
    vigil_value_t index_value = (vigil_value_t){vigil_vm_stack_get(vm, base + 1U)};
    int64_t index = vigil_value_as_int(&index_value);
    const vigil_json_value_t *child = NULL;
    vigil_object_t *instance = NULL;
    vigil_status_t status;

    if (tree == NULL)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_nil_and_err(vm, "at: invalid json.Value", JSON_ERR_PARSE, error);
    }
    if (vigil_json_type(tree) != VIGIL_JSON_ARRAY)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_nil_and_err(vm, "at: value is not an array", JSON_ERR_PARSE, error);
    }
    if (index < 0 || (size_t)index >= vigil_json_array_count(tree))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_nil_and_err(vm, "at: index out of range", JSON_ERR_PARSE, error);
    }

    child = vigil_json_array_get(tree, (size_t)index);
    /* Child is not owned — it belongs to the parent tree */
    status = json_wrap_tree(vigil_vm_runtime(vm), class_index, (vigil_json_value_t *)child, 0, &instance, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (status != VIGIL_STATUS_OK)
        return status;
    return json_push_object_and_ok(vm, &instance, error);
}

static vigil_status_t json_value_get(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = json_get_self(vm, base);
    size_t class_index = vigil_instance_object_class_index(self);
    vigil_json_value_t *tree = json_get_tree(self);
    const char *key = NULL;
    size_t key_length = 0U;
    const vigil_json_value_t *child = NULL;
    vigil_object_t *instance = NULL;
    vigil_status_t status;

    if (!json_get_string_arg(vm, base + 1U, &key, &key_length))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_nil_and_err(vm, "get: expected string key", JSON_ERR_PARSE, error);
    }
    if (tree == NULL)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_nil_and_err(vm, "get: invalid json.Value", JSON_ERR_PARSE, error);
    }
    if (vigil_json_type(tree) != VIGIL_JSON_OBJECT)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_nil_and_err(vm, "get: value is not an object", JSON_ERR_PARSE, error);
    }

    child = vigil_json_object_get(tree, key);
    if (child == NULL)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_nil_and_err(vm, "get: key not found", JSON_ERR_PARSE, error);
    }

    status = json_wrap_tree(vigil_vm_runtime(vm), class_index, (vigil_json_value_t *)child, 0, &instance, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (status != VIGIL_STATUS_OK)
        return status;
    return json_push_object_and_ok(vm, &instance, error);
}

static vigil_status_t json_value_has(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_json_value_t *tree = json_get_tree(json_get_self(vm, base));
    const char *key = NULL;
    size_t key_length = 0U;
    bool has = false;

    (void)key_length;
    if (json_get_string_arg(vm, base + 1U, &key, &key_length) && tree != NULL &&
        vigil_json_type(tree) == VIGIL_JSON_OBJECT && vigil_json_object_get(tree, key) != NULL)
        has = true;

    vigil_vm_stack_pop_n(vm, arg_count);
    {
        vigil_value_t value;
        vigil_value_init_bool(&value, has);
        return vigil_vm_stack_push(vm, &value, error);
    }
}

static vigil_status_t json_value_keys(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_runtime_t *runtime = vigil_vm_runtime(vm);
    vigil_json_value_t *tree = json_get_tree(json_get_self(vm, base));
    vigil_object_t *array = NULL;
    vigil_status_t status;
    size_t index;

    if (tree == NULL || vigil_json_type(tree) != VIGIL_JSON_OBJECT)
    {
        status = vigil_array_object_new(runtime, NULL, 0U, &array, error);
        vigil_vm_stack_pop_n(vm, arg_count);
        if (status != VIGIL_STATUS_OK)
            return status;
        return json_push_object(vm, &array, error);
    }

    status = vigil_array_object_new(runtime, NULL, 0U, &array, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    for (index = 0U; index < vigil_json_object_count(tree); ++index)
    {
        const char *key = NULL;
        size_t key_length = 0U;
        const vigil_json_value_t *value = NULL;
        vigil_object_t *string = NULL;
        vigil_value_t key_value;

        status = vigil_json_object_entry(tree, index, &key, &key_length, &value);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_object_release(&array);
            return status;
        }
        status = vigil_string_object_new(runtime, key, key_length, &string, error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_object_release(&array);
            return status;
        }
        vigil_value_init_object(&key_value, &string);
        status = vigil_array_object_append(array, &key_value, error);
        vigil_value_release(&key_value);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_object_release(&array);
            return status;
        }
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    return json_push_object(vm, &array, error);
}

static vigil_status_t json_value_stringify(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *text = NULL;
    size_t length = 0U;

    if (!json_get_raw_text(json_get_self(vm, base), &text, &length))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_string(vm, "", 0U, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    return json_push_string(vm, text, length, error);
}

static vigil_status_t json_value_write(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path = NULL;
    size_t path_length = 0U;
    const char *text = NULL;
    size_t text_length = 0U;
    const char *message = "write: expected string path";
    vigil_status_t status;

    (void)path_length;
    if (!json_get_string_arg(vm, base + 1U, &path, &path_length))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_err_kind(vm, message, JSON_ERR_IO, error);
    }
    if (!json_get_raw_text(json_get_self(vm, base), &text, &text_length))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_err_kind(vm, "write: invalid json.Value", JSON_ERR_PARSE, error);
    }

    /* Validate the raw text is valid JSON via the cached tree */
    if (json_get_tree(json_get_self(vm, base)) == NULL)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_err_kind(vm, "write: invalid JSON", JSON_ERR_PARSE, error);
    }

    status = json_write_file(vm, path, text, text_length, &message);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (status != VIGIL_STATUS_OK)
        return status;
    if (message != NULL)
        return json_push_err_kind(vm, message, JSON_ERR_IO, error);
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

/* ── Static constructors ─────────────────────────────────────────── */

static vigil_status_t json_value_static_object(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t class_index = (size_t)vigil_value_as_int(&(vigil_value_t){vigil_vm_stack_get(vm, base)});
    vigil_json_value_t *tree = NULL;
    vigil_object_t *instance = NULL;
    vigil_status_t status;

    status = vigil_json_object_new(json_runtime_allocator(vm), &tree, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }
    status = json_wrap_tree(vigil_vm_runtime(vm), class_index, tree, 1, &instance, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_json_free(&tree);
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    return json_push_object(vm, &instance, error);
}

static vigil_status_t json_value_static_array(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t class_index = (size_t)vigil_value_as_int(&(vigil_value_t){vigil_vm_stack_get(vm, base)});
    vigil_json_value_t *tree = NULL;
    vigil_object_t *instance = NULL;
    vigil_status_t status;

    status = vigil_json_array_new(json_runtime_allocator(vm), &tree, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }
    status = json_wrap_tree(vigil_vm_runtime(vm), class_index, tree, 1, &instance, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_json_free(&tree);
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    return json_push_object(vm, &instance, error);
}

static vigil_status_t json_value_static_from_string(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t class_index = (size_t)vigil_value_as_int(&(vigil_value_t){vigil_vm_stack_get(vm, base)});
    const char *text = NULL;
    size_t length = 0U;
    vigil_json_value_t *tree = NULL;
    vigil_object_t *instance = NULL;
    vigil_status_t status;

    if (!json_get_string_arg(vm, base + 1U, &text, &length))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_nil_and_err(vm, "from_string: expected string", JSON_ERR_TYPE, error);
    }
    status = vigil_json_string_new(json_runtime_allocator(vm), text, length, &tree, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }
    status = json_wrap_tree(vigil_vm_runtime(vm), class_index, tree, 1, &instance, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_json_free(&tree);
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    return json_push_object(vm, &instance, error);
}

static vigil_status_t json_value_static_from_number(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t class_index = (size_t)vigil_value_as_int(&(vigil_value_t){vigil_vm_stack_get(vm, base)});
    vigil_value_t arg = vigil_vm_stack_get(vm, base + 1U);
    double number = vigil_value_as_float(&arg);
    vigil_json_value_t *tree = NULL;
    vigil_object_t *instance = NULL;
    vigil_status_t status;

    status = vigil_json_number_new(json_runtime_allocator(vm), number, &tree, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }
    status = json_wrap_tree(vigil_vm_runtime(vm), class_index, tree, 1, &instance, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_json_free(&tree);
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    return json_push_object(vm, &instance, error);
}

static vigil_status_t json_value_static_from_int(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t class_index = (size_t)vigil_value_as_int(&(vigil_value_t){vigil_vm_stack_get(vm, base)});
    vigil_value_t arg = vigil_vm_stack_get(vm, base + 1U);
    int64_t integer = vigil_value_as_int(&arg);
    vigil_json_value_t *tree = NULL;
    vigil_object_t *instance = NULL;
    vigil_status_t status;

    status = vigil_json_number_new(json_runtime_allocator(vm), (double)integer, &tree, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }
    status = json_wrap_tree(vigil_vm_runtime(vm), class_index, tree, 1, &instance, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_json_free(&tree);
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    return json_push_object(vm, &instance, error);
}

static vigil_status_t json_value_static_from_bool(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t class_index = (size_t)vigil_value_as_int(&(vigil_value_t){vigil_vm_stack_get(vm, base)});
    vigil_value_t arg = vigil_vm_stack_get(vm, base + 1U);
    bool b = vigil_value_as_bool(&arg);
    vigil_json_value_t *tree = NULL;
    vigil_object_t *instance = NULL;
    vigil_status_t status;

    status = vigil_json_bool_new(json_runtime_allocator(vm), b ? 1 : 0, &tree, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }
    status = json_wrap_tree(vigil_vm_runtime(vm), class_index, tree, 1, &instance, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_json_free(&tree);
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    return json_push_object(vm, &instance, error);
}

static vigil_status_t json_value_static_null(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t class_index = (size_t)vigil_value_as_int(&(vigil_value_t){vigil_vm_stack_get(vm, base)});
    vigil_json_value_t *tree = NULL;
    vigil_object_t *instance = NULL;
    vigil_status_t status;

    status = vigil_json_null_new(json_runtime_allocator(vm), &tree, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }
    status = json_wrap_tree(vigil_vm_runtime(vm), class_index, tree, 1, &instance, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_json_free(&tree);
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    return json_push_object(vm, &instance, error);
}

/* ── Mutation methods ────────────────────────────────────────────── */

static vigil_status_t json_value_set(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = json_get_self(vm, base);
    vigil_json_value_t *tree = json_get_tree(self);
    const char *key = NULL;
    size_t key_length = 0U;
    vigil_json_value_t *val_tree = NULL;
    vigil_json_value_t *copy = NULL;
    vigil_status_t status;

    if (!json_get_string_arg(vm, base + 1U, &key, &key_length))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_err_kind(vm, "set: expected string key", JSON_ERR_TYPE, error);
    }
    if (tree == NULL || vigil_json_type(tree) != VIGIL_JSON_OBJECT)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_err_kind(vm, "set: value is not an object", JSON_ERR_TYPE, error);
    }

    /* Get the json.Value argument's tree */
    {
        vigil_value_t arg_val = vigil_vm_stack_get(vm, base + 2U);
        vigil_object_t *arg_obj = vigil_value_as_object(&arg_val);
        val_tree = json_get_tree(arg_obj);
    }
    if (val_tree == NULL)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_err_kind(vm, "set: invalid value argument", JSON_ERR_TYPE, error);
    }

    /* We need a copy since vigil_json_object_set takes ownership.
     * Emit then re-parse to create an independent copy. */
    {
        const vigil_allocator_t *allocator = json_runtime_allocator(vm);
        char *emitted = NULL;
        size_t emitted_length = 0U;
        status = vigil_json_emit(val_tree, &emitted, &emitted_length, error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_vm_stack_pop_n(vm, arg_count);
            return status;
        }
        {
            vigil_error_t parse_err = {0};
            status = vigil_json_parse(allocator, emitted, emitted_length, &copy, &parse_err);
        }
        if (allocator != NULL && emitted != NULL)
            allocator->deallocate(allocator->user_data, emitted);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_vm_stack_pop_n(vm, arg_count);
            return status;
        }
    }

    status = vigil_json_object_set(tree, key, key_length, copy, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_json_free(&copy);
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }

    status = json_sync_raw(vm, self, tree, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (status != VIGIL_STATUS_OK)
        return status;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t json_value_remove(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = json_get_self(vm, base);
    vigil_json_value_t *tree = json_get_tree(self);
    const char *key = NULL;
    size_t key_length = 0U;
    int64_t handle;
    vigil_json_value_t *new_obj = NULL;
    vigil_status_t status;
    size_t i, count;

    if (!json_get_string_arg(vm, base + 1U, &key, &key_length))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_err_kind(vm, "remove: expected string key", JSON_ERR_TYPE, error);
    }
    if (tree == NULL || vigil_json_type(tree) != VIGIL_JSON_OBJECT)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_err_kind(vm, "remove: value is not an object", JSON_ERR_TYPE, error);
    }

    /* Rebuild the object without the target key (no public remove API) */
    status = vigil_json_object_new(json_runtime_allocator(vm), &new_obj, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }

    count = vigil_json_object_count(tree);
    for (i = 0U; i < count; ++i)
    {
        const char *entry_key = NULL;
        size_t entry_key_length = 0U;
        const vigil_json_value_t *entry_value = NULL;
        vigil_json_value_t *copy = NULL;
        char *emitted = NULL;
        size_t emitted_length = 0U;

        status = vigil_json_object_entry(tree, i, &entry_key, &entry_key_length, &entry_value);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_json_free(&new_obj);
            vigil_vm_stack_pop_n(vm, arg_count);
            return status;
        }

        if (entry_key_length == key_length && memcmp(entry_key, key, key_length) == 0)
            continue; /* skip the removed key */

        /* Copy the value via emit+parse */
        {
            const vigil_allocator_t *allocator = json_runtime_allocator(vm);
            status = vigil_json_emit(entry_value, &emitted, &emitted_length, error);
            if (status != VIGIL_STATUS_OK)
            {
                vigil_json_free(&new_obj);
                vigil_vm_stack_pop_n(vm, arg_count);
                return status;
            }
            {
                vigil_error_t parse_err = {0};
                status = vigil_json_parse(allocator, emitted, emitted_length, &copy, &parse_err);
            }
            if (allocator != NULL && emitted != NULL)
                allocator->deallocate(allocator->user_data, emitted);
            if (status != VIGIL_STATUS_OK)
            {
                vigil_json_free(&new_obj);
                vigil_vm_stack_pop_n(vm, arg_count);
                return status;
            }
        }

        status = vigil_json_object_set(new_obj, entry_key, entry_key_length, copy, error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_json_free(&copy);
            vigil_json_free(&new_obj);
            vigil_vm_stack_pop_n(vm, arg_count);
            return status;
        }
    }

    /* Replace the tree in the handle registry */
    handle = json_get_handle_field(self);
    if (handle >= 0 && handle < MAX_JSON_HANDLES)
    {
        if (json_handles[handle].owned)
            vigil_json_free(&json_handles[handle].tree);
        json_handles[handle].tree = new_obj;
        json_handles[handle].owned = 1;
    }
    else
    {
        vigil_json_free(&new_obj);
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_err_kind(vm, "remove: invalid handle", JSON_ERR_TYPE, error);
    }

    status = json_sync_raw(vm, self, new_obj, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (status != VIGIL_STATUS_OK)
        return status;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t json_value_push(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = json_get_self(vm, base);
    vigil_json_value_t *tree = json_get_tree(self);
    vigil_json_value_t *val_tree = NULL;
    vigil_json_value_t *copy = NULL;
    vigil_status_t status;

    if (tree == NULL || vigil_json_type(tree) != VIGIL_JSON_ARRAY)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_err_kind(vm, "push: value is not an array", JSON_ERR_TYPE, error);
    }

    {
        vigil_value_t arg_val = vigil_vm_stack_get(vm, base + 1U);
        vigil_object_t *arg_obj = vigil_value_as_object(&arg_val);
        val_tree = json_get_tree(arg_obj);
    }
    if (val_tree == NULL)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_err_kind(vm, "push: invalid value argument", JSON_ERR_TYPE, error);
    }

    /* Copy via emit+parse */
    {
        const vigil_allocator_t *allocator = json_runtime_allocator(vm);
        char *emitted = NULL;
        size_t emitted_length = 0U;
        status = vigil_json_emit(val_tree, &emitted, &emitted_length, error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_vm_stack_pop_n(vm, arg_count);
            return status;
        }
        {
            vigil_error_t parse_err = {0};
            status = vigil_json_parse(allocator, emitted, emitted_length, &copy, &parse_err);
        }
        if (allocator != NULL && emitted != NULL)
            allocator->deallocate(allocator->user_data, emitted);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_vm_stack_pop_n(vm, arg_count);
            return status;
        }
    }

    status = vigil_json_array_push(tree, copy, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_json_free(&copy);
        vigil_vm_stack_pop_n(vm, arg_count);
        return status;
    }

    status = json_sync_raw(vm, self, tree, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (status != VIGIL_STATUS_OK)
        return status;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

/* ── Pretty print ────────────────────────────────────────────────── */

static vigil_status_t json_pretty_emit(vigil_vm_t *vm, const vigil_json_value_t *v, int indent, int depth, char **buf,
                                       size_t *len, size_t *cap, vigil_error_t *error);

static void json_pretty_append(char **buf, size_t *len, size_t *cap, const char *s, size_t n,
                               const vigil_allocator_t *allocator)
{
    if (n == 0)
        return;
    while (*len + n >= *cap)
    {
        size_t nc = *cap < 64 ? 64 : *cap * 2;
        char *nb = (char *)allocator->reallocate(allocator->user_data, *buf, nc);
        if (!nb)
            return;
        *buf = nb;
        *cap = nc;
    }
    memcpy(*buf + *len, s, n);
    *len += n;
}

static void json_pretty_indent(char **buf, size_t *len, size_t *cap, int indent, int depth,
                               const vigil_allocator_t *allocator)
{
    int spaces = indent * depth;
    int i;
    for (i = 0; i < spaces; i++)
        json_pretty_append(buf, len, cap, " ", 1, allocator);
}

static vigil_status_t json_pretty_emit(vigil_vm_t *vm, const vigil_json_value_t *v, int indent, int depth, char **buf,
                                       size_t *len, size_t *cap, vigil_error_t *error)
{
    const vigil_allocator_t *allocator = json_runtime_allocator(vm);
    if (v == NULL)
    {
        json_pretty_append(buf, len, cap, "null", 4, allocator);
        return VIGIL_STATUS_OK;
    }

    switch (vigil_json_type(v))
    {
    case VIGIL_JSON_NULL:
        json_pretty_append(buf, len, cap, "null", 4, allocator);
        break;
    case VIGIL_JSON_BOOL:
        if (vigil_json_bool_value(v))
            json_pretty_append(buf, len, cap, "true", 4, allocator);
        else
            json_pretty_append(buf, len, cap, "false", 5, allocator);
        break;
    case VIGIL_JSON_NUMBER: {
        char tmp[64];
        int n;
        double val = vigil_json_number_value(v);
        if (val == (double)(int64_t)val && val >= -1e15 && val <= 1e15)
            n = snprintf(tmp, sizeof(tmp), "%lld", (long long)(int64_t)val);
        else
            n = snprintf(tmp, sizeof(tmp), "%.17g", val);
        if (n > 0)
            json_pretty_append(buf, len, cap, tmp, (size_t)n, allocator);
        break;
    }
    case VIGIL_JSON_STRING: {
        /* Emit escaped string */
        const char *s = vigil_json_string_value(v);
        size_t slen = vigil_json_string_length(v);
        char *emitted = NULL;
        size_t emitted_len = 0U;
        vigil_status_t status = vigil_json_emit(v, &emitted, &emitted_len, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        json_pretty_append(buf, len, cap, emitted, emitted_len, allocator);
        if (allocator)
            allocator->deallocate(allocator->user_data, emitted);
        (void)s;
        (void)slen;
        break;
    }
    case VIGIL_JSON_ARRAY: {
        size_t count = vigil_json_array_count(v);
        size_t i;
        if (count == 0)
        {
            json_pretty_append(buf, len, cap, "[]", 2, allocator);
            break;
        }
        json_pretty_append(buf, len, cap, "[\n", 2, allocator);
        for (i = 0; i < count; i++)
        {
            json_pretty_indent(buf, len, cap, indent, depth + 1, allocator);
            json_pretty_emit(vm, vigil_json_array_get(v, i), indent, depth + 1, buf, len, cap, error);
            if (i + 1 < count)
                json_pretty_append(buf, len, cap, ",", 1, allocator);
            json_pretty_append(buf, len, cap, "\n", 1, allocator);
        }
        json_pretty_indent(buf, len, cap, indent, depth, allocator);
        json_pretty_append(buf, len, cap, "]", 1, allocator);
        break;
    }
    case VIGIL_JSON_OBJECT: {
        size_t count = vigil_json_object_count(v);
        size_t i;
        if (count == 0)
        {
            json_pretty_append(buf, len, cap, "{}", 2, allocator);
            break;
        }
        json_pretty_append(buf, len, cap, "{\n", 2, allocator);
        for (i = 0; i < count; i++)
        {
            const char *key = NULL;
            size_t key_len = 0U;
            const vigil_json_value_t *child = NULL;
            vigil_json_object_entry(v, i, &key, &key_len, &child);
            json_pretty_indent(buf, len, cap, indent, depth + 1, allocator);
            json_pretty_append(buf, len, cap, "\"", 1, allocator);
            json_pretty_append(buf, len, cap, key, key_len, allocator);
            json_pretty_append(buf, len, cap, "\": ", 3, allocator);
            json_pretty_emit(vm, child, indent, depth + 1, buf, len, cap, error);
            if (i + 1 < count)
                json_pretty_append(buf, len, cap, ",", 1, allocator);
            json_pretty_append(buf, len, cap, "\n", 1, allocator);
        }
        json_pretty_indent(buf, len, cap, indent, depth, allocator);
        json_pretty_append(buf, len, cap, "}", 1, allocator);
        break;
    }
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t json_value_stringify_pretty(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_json_value_t *tree = json_get_tree(json_get_self(vm, base));
    vigil_value_t indent_val = vigil_vm_stack_get(vm, base + 1U);
    int indent = (int)vigil_value_as_int(&indent_val);
    const vigil_allocator_t *allocator = json_runtime_allocator(vm);
    char *buf = NULL;
    size_t len = 0, cap = 0;
    vigil_status_t status;

    if (tree == NULL)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_string(vm, "", 0U, error);
    }
    if (indent < 0)
        indent = 0;
    if (indent > 16)
        indent = 16;

    status = json_pretty_emit(vm, tree, indent, 0, &buf, &len, &cap, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (status != VIGIL_STATUS_OK)
    {
        if (allocator && buf)
            allocator->deallocate(allocator->user_data, buf);
        return status;
    }
    status = json_push_string(vm, buf ? buf : "", len, error);
    if (allocator && buf)
        allocator->deallocate(allocator->user_data, buf);
    return status;
}

/* ── Merge ───────────────────────────────────────────────────────── */

static vigil_status_t json_value_merge(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = json_get_self(vm, base);
    vigil_json_value_t *tree = json_get_tree(self);
    vigil_json_value_t *src_tree = NULL;
    vigil_status_t status;
    size_t i, count;

    if (tree == NULL || vigil_json_type(tree) != VIGIL_JSON_OBJECT)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_err_kind(vm, "merge: target is not an object", JSON_ERR_TYPE, error);
    }

    {
        vigil_value_t arg_val = vigil_vm_stack_get(vm, base + 1U);
        vigil_object_t *arg_obj = vigil_value_as_object(&arg_val);
        src_tree = json_get_tree(arg_obj);
    }
    if (src_tree == NULL || vigil_json_type(src_tree) != VIGIL_JSON_OBJECT)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_err_kind(vm, "merge: source is not an object", JSON_ERR_TYPE, error);
    }

    count = vigil_json_object_count(src_tree);
    for (i = 0; i < count; i++)
    {
        const char *key = NULL;
        size_t key_len = 0U;
        const vigil_json_value_t *child = NULL;
        vigil_json_value_t *copy = NULL;
        char *emitted = NULL;
        size_t emitted_len = 0U;
        const vigil_allocator_t *allocator = json_runtime_allocator(vm);

        status = vigil_json_object_entry(src_tree, i, &key, &key_len, &child);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_vm_stack_pop_n(vm, arg_count);
            return status;
        }

        status = vigil_json_emit(child, &emitted, &emitted_len, error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_vm_stack_pop_n(vm, arg_count);
            return status;
        }
        {
            vigil_error_t parse_err = {0};
            status = vigil_json_parse(allocator, emitted, emitted_len, &copy, &parse_err);
        }
        if (allocator && emitted)
            allocator->deallocate(allocator->user_data, emitted);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_vm_stack_pop_n(vm, arg_count);
            return status;
        }

        status = vigil_json_object_set(tree, key, key_len, copy, error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_json_free(&copy);
            vigil_vm_stack_pop_n(vm, arg_count);
            return status;
        }
    }

    status = json_sync_raw(vm, self, tree, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (status != VIGIL_STATUS_OK)
        return status;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

/* ── Module-level encode / decode ────────────────────────────────── */

static vigil_status_t json_encode_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const vigil_object_t *function = json_current_function(vm);
    vigil_value_t value = vigil_vm_stack_get(vm, base);
    vigil_json_value_t *json = NULL;
    char *text = NULL;
    size_t text_length = 0U;
    const vigil_allocator_t *allocator = json_runtime_allocator(vm);
    vigil_status_t status;

    if (function == NULL)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_empty_string_and_err(vm, "encode: missing execution context", JSON_ERR_TYPE, error);
    }

    status = json_encode_value(vm, function, &value, &json, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_empty_string_and_err(vm, vigil_error_message(error), JSON_ERR_TYPE, error);
    }

    status = vigil_json_emit(json, &text, &text_length, error);
    vigil_json_free(&json);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (status != VIGIL_STATUS_OK)
        return status;

    status = json_push_string_and_ok(vm, text, text_length, error);
    if (allocator != NULL && text != NULL)
        allocator->deallocate(allocator->user_data, text);
    return status;
}

static vigil_status_t json_decode_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const vigil_object_t *function = json_current_function(vm);
    const char *text = NULL;
    size_t text_length = 0U;
    vigil_value_t prototype = vigil_vm_stack_get(vm, base + 1U);
    vigil_object_t *prototype_object = vigil_value_as_object(&prototype);
    vigil_json_value_t *json = NULL;
    vigil_value_t decoded;
    vigil_status_t status;
    const char *message = "decode: expected string json text";

    vigil_value_init_nil(&decoded);
    if (function == NULL)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_nil_and_err(vm, "decode: missing execution context", JSON_ERR_TYPE, error);
    }
    if (!json_get_string_arg(vm, base, &text, &text_length))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_nil_and_err(vm, message, JSON_ERR_PARSE, error);
    }
    if (prototype_object == NULL || vigil_value_kind(&prototype) != VIGIL_VALUE_OBJECT ||
        vigil_object_type(prototype_object) != VIGIL_OBJECT_INSTANCE)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_nil_and_err(vm, "decode: prototype must be an instance", JSON_ERR_TYPE, error);
    }

    status = json_parse_text(vm, text, text_length, &json, &message);
    if (status != VIGIL_STATUS_OK)
        return status;
    if (json == NULL)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return json_push_nil_and_err(vm, message, JSON_ERR_PARSE, error);
    }

    status =
        json_decode_class(vm, function, json, vigil_instance_object_class_index(prototype_object), &decoded, error);
    vigil_json_free(&json);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_value_release(&decoded);
        return json_push_nil_and_err(vm, vigil_error_message(error), JSON_ERR_TYPE, error);
    }

    status = vigil_vm_stack_push(vm, &decoded, error);
    vigil_value_release(&decoded);
    if (status != VIGIL_STATUS_OK)
        return status;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

/* ── Registration tables ─────────────────────────────────────────── */

static const int str_param[] = {VIGIL_TYPE_STRING};
static const int i32_param[] = {VIGIL_TYPE_I32};
static const int f64_param[] = {VIGIL_TYPE_F64};
static const int bool_param[] = {VIGIL_TYPE_BOOL};
static const int obj_param[] = {VIGIL_TYPE_OBJECT};
static const int str_obj_param[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_OBJECT};
static const int obj_err_returns[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_ERR};
static const int bool_err_returns[] = {VIGIL_TYPE_BOOL, VIGIL_TYPE_ERR};
static const int f64_err_returns[] = {VIGIL_TYPE_F64, VIGIL_TYPE_ERR};
static const int str_err_returns[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_ERR};
static const int i32_err_returns[] = {VIGIL_TYPE_I32, VIGIL_TYPE_ERR};

static const char *const json_value_param_names[] = {"value"};
static const char *const json_decode_param_names[] = {"text", "prototype"};
static const char *const json_decode_param_types[] = {"string", "T"};
static const char *const json_text_param_names[] = {"text"};
static const char *const json_path_param_names[] = {"path"};
static const char *const json_index_param_names[] = {"index"};
static const char *const json_key_param_names[] = {"key"};
static const char *const json_key_val_param_names[] = {"key", "val"};
static const char *const json_key_val_param_types[] = {"string", "json.Value"};
static const char *const json_val_param_names[] = {"val"};
static const char *const json_val_param_types[] = {"json.Value"};
static const char *const json_indent_param_names[] = {"indent"};
static const char *const json_source_param_names[] = {"source"};
static const char *const json_source_param_types[] = {"json.Value"};
static const char *const json_s_param_names[] = {"s"};
static const char *const json_n_param_names[] = {"n"};
static const char *const json_b_param_names[] = {"b"};

/* ── Doc strings ─────────────────────────────────────────────────── */

static const vigil_native_symbol_doc_t vigil_json_module_doc = {
    "JSON parsing, traversal, and mutation.",
    "Parse arbitrary JSON, inspect and mutate it dynamically through json.Value, and encode or decode structured "
    "values.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_encode_doc = {
    "Encode a value as JSON.",
    "Serializes supported Vigil values into a JSON string.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_decode_doc = {
    "Decode JSON into a prototype shape.",
    "Decodes JSON text using the supplied prototype value to determine the target shape. Missing fields get defaults; "
    "extra fields are ignored.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_doc = {
    "Dynamic JSON value wrapper.",
    "Stores a cached JSON tree with handle registry for efficient access. Supports traversal, mutation, and "
    "serialization.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_raw_doc = {
    "Canonical JSON text.",
    "Stores the underlying JSON representation used by the wrapper.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_handle_doc = {
    "Internal handle index.",
    "Index into the handle registry for the cached JSON tree.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_parse_doc = {
    "Parse JSON text.",
    "Parses arbitrary JSON text and returns a json.Value wrapper.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_read_doc = {
    "Read and parse JSON from a file.",
    "Loads a file from disk, parses it as JSON, and returns a json.Value wrapper.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_kind_doc = {
    "Get the JSON kind name.",
    "Returns one of null, bool, number, string, array, object, or invalid.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_is_null_doc = {"Check for null.",
                                                                       "Returns true when the value is null.", NULL};
static const vigil_native_symbol_doc_t vigil_json_value_is_bool_doc = {
    "Check for bool.", "Returns true when the value is a JSON boolean.", NULL};
static const vigil_native_symbol_doc_t vigil_json_value_is_number_doc = {
    "Check for number.", "Returns true when the value is a JSON number.", NULL};
static const vigil_native_symbol_doc_t vigil_json_value_is_string_doc = {
    "Check for string.", "Returns true when the value is a JSON string.", NULL};
static const vigil_native_symbol_doc_t vigil_json_value_is_array_doc = {
    "Check for array.", "Returns true when the value is a JSON array.", NULL};
static const vigil_native_symbol_doc_t vigil_json_value_is_object_doc = {
    "Check for object.", "Returns true when the value is a JSON object.", NULL};
static const vigil_native_symbol_doc_t vigil_json_value_len_doc = {
    "Get object or array length.",
    "Returns the number of entries in an array or object value.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_as_bool_doc = {
    "Read a boolean value.",
    "Returns the underlying boolean when the value is a JSON boolean.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_as_number_doc = {
    "Read a numeric value.",
    "Returns the underlying number when the value is a JSON number.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_as_string_doc = {
    "Read a string value.",
    "Returns the underlying string when the value is a JSON string.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_at_doc = {
    "Read an array element.",
    "Returns the value at the given array index.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_get_doc = {
    "Read an object member.",
    "Returns the value stored at the given object key.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_has_doc = {
    "Check for an object member.",
    "Returns true when the key exists on the value and the value is an object.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_keys_doc = {
    "List object keys.",
    "Returns the object's keys in their stored order.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_stringify_doc = {
    "Serialize the value to text.",
    "Returns the canonical JSON text currently stored by the wrapper.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_write_doc = {
    "Write JSON to a file.",
    "Writes the stored JSON text to the target path.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_object_doc = {
    "Create an empty JSON object.",
    "Returns a new json.Value wrapping an empty object.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_array_doc = {
    "Create an empty JSON array.",
    "Returns a new json.Value wrapping an empty array.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_from_string_doc = {
    "Create a JSON string value.",
    "Wraps a Vigil string as a JSON string value.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_from_number_doc = {
    "Create a JSON number value.",
    "Wraps an f64 as a JSON number value.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_from_int_doc = {
    "Create a JSON integer value.",
    "Wraps an i32 as a JSON number value.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_from_bool_doc = {
    "Create a JSON boolean value.",
    "Wraps a bool as a JSON boolean value.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_null_doc = {
    "Create a JSON null value.",
    "Returns a new json.Value wrapping null.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_set_doc = {
    "Set an object key.",
    "Sets or updates a key on a JSON object. Regenerates the raw text.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_remove_doc = {
    "Remove an object key.",
    "Removes a key from a JSON object. Regenerates the raw text.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_push_doc = {
    "Append to an array.",
    "Appends a value to a JSON array. Regenerates the raw text.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_stringify_pretty_doc = {
    "Pretty-print JSON.",
    "Returns indented JSON text with the given indent width.",
    NULL,
};
static const vigil_native_symbol_doc_t vigil_json_value_merge_doc = {
    "Shallow merge objects.",
    "Copies all keys from source into this object. Source wins on conflicts.",
    NULL,
};

/* ── Field and method tables ─────────────────────────────────────── */

static const vigil_native_class_field_t json_value_fields[] = {
    {"raw", 3U, VIGIL_TYPE_STRING, VIGIL_NATIVE_FIELD_PRIMITIVE, NULL, 0U, 0, NULL, &vigil_json_value_raw_doc},
    {"handle", 6U, VIGIL_TYPE_I64, VIGIL_NATIVE_FIELD_PRIMITIVE, NULL, 0U, 0, NULL, &vigil_json_value_handle_doc},
};

static const vigil_native_module_function_t json_functions[] = {
    {"encode", 6U, json_encode_fn, 1U, obj_param, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U,
     json_value_param_names, NULL, NULL, &vigil_json_encode_doc},
    {"decode", 6U, json_decode_fn, 2U, str_obj_param, VIGIL_TYPE_OBJECT, 2U, obj_err_returns, 0, NULL, NULL, 2U,
     json_decode_param_names, json_decode_param_types, "T", &vigil_json_decode_doc},
};

/* clang-format off */
static const vigil_native_class_method_t json_value_methods[] = {
    /* Static constructors */
    {"parse", 5U, json_value_parse, 1U, str_param, VIGIL_TYPE_OBJECT, 2U, obj_err_returns, 1, "Value", 5U, 0,
     json_text_param_names, NULL, "json.Value", &vigil_json_value_parse_doc},
    {"read", 4U, json_value_read, 1U, str_param, VIGIL_TYPE_OBJECT, 2U, obj_err_returns, 1, "Value", 5U, 0,
     json_path_param_names, NULL, "json.Value", &vigil_json_value_read_doc},
    {"object", 6U, json_value_static_object, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL, 1, "Value", 5U, 0,
     NULL, NULL, "json.Value", &vigil_json_value_object_doc},
    {"array", 5U, json_value_static_array, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL, 1, "Value", 5U, 0,
     NULL, NULL, "json.Value", &vigil_json_value_array_doc},
    {"from_string", 11U, json_value_static_from_string, 1U, str_param, VIGIL_TYPE_OBJECT, 1U, NULL, 1, "Value", 5U, 0,
     json_s_param_names, NULL, "json.Value", &vigil_json_value_from_string_doc},
    {"from_number", 11U, json_value_static_from_number, 1U, f64_param, VIGIL_TYPE_OBJECT, 1U, NULL, 1, "Value", 5U, 0,
     json_n_param_names, NULL, "json.Value", &vigil_json_value_from_number_doc},
    {"from_int", 8U, json_value_static_from_int, 1U, i32_param, VIGIL_TYPE_OBJECT, 1U, NULL, 1, "Value", 5U, 0,
     json_n_param_names, NULL, "json.Value", &vigil_json_value_from_int_doc},
    {"from_bool", 9U, json_value_static_from_bool, 1U, bool_param, VIGIL_TYPE_OBJECT, 1U, NULL, 1, "Value", 5U, 0,
     json_b_param_names, NULL, "json.Value", &vigil_json_value_from_bool_doc},
    {"null", 4U, json_value_static_null, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL, 1, "Value", 5U, 0,
     NULL, NULL, "json.Value", &vigil_json_value_null_doc},
    /* Instance accessors */
    {"kind", 4U, json_value_kind, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &vigil_json_value_kind_doc},
    {"is_null", 7U, json_value_is_null, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &vigil_json_value_is_null_doc},
    {"is_bool", 7U, json_value_is_bool, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &vigil_json_value_is_bool_doc},
    {"is_number", 9U, json_value_is_number, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &vigil_json_value_is_number_doc},
    {"is_string", 9U, json_value_is_string, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &vigil_json_value_is_string_doc},
    {"is_array", 8U, json_value_is_array, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &vigil_json_value_is_array_doc},
    {"is_object", 9U, json_value_is_object, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &vigil_json_value_is_object_doc},
    {"len", 3U, json_value_len, 0U, NULL, VIGIL_TYPE_I32, 2U, i32_err_returns, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &vigil_json_value_len_doc},
    {"as_bool", 7U, json_value_as_bool, 0U, NULL, VIGIL_TYPE_BOOL, 2U, bool_err_returns, 0, NULL, 0U, 0, NULL, NULL,
     NULL, &vigil_json_value_as_bool_doc},
    {"as_number", 9U, json_value_as_number, 0U, NULL, VIGIL_TYPE_F64, 2U, f64_err_returns, 0, NULL, 0U, 0, NULL, NULL,
     NULL, &vigil_json_value_as_number_doc},
    {"as_string", 9U, json_value_as_string, 0U, NULL, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, 0U, 0, NULL,
     NULL, NULL, &vigil_json_value_as_string_doc},
    {"at", 2U, json_value_at, 1U, i32_param, VIGIL_TYPE_OBJECT, 2U, obj_err_returns, 0, NULL, 0U, 0,
     json_index_param_names, NULL, "json.Value", &vigil_json_value_at_doc},
    {"get", 3U, json_value_get, 1U, str_param, VIGIL_TYPE_OBJECT, 2U, obj_err_returns, 0, NULL, 0U, 0,
     json_key_param_names, NULL, "json.Value", &vigil_json_value_get_doc},
    {"has", 3U, json_value_has, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, 0U, 0, json_key_param_names, NULL,
     NULL, &vigil_json_value_has_doc},
    {"keys", 4U, json_value_keys, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL, 0, NULL, 0U, VIGIL_TYPE_STRING, NULL, NULL,
     NULL, &vigil_json_value_keys_doc},
    {"stringify", 9U, json_value_stringify, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &vigil_json_value_stringify_doc},
    {"write", 5U, json_value_write, 1U, str_param, VIGIL_TYPE_ERR, 1U, NULL, 0, NULL, 0U, 0, json_path_param_names,
     NULL, NULL, &vigil_json_value_write_doc},
    /* Mutation methods */
    {"set", 3U, json_value_set, 2U, str_obj_param, VIGIL_TYPE_ERR, 1U, NULL, 0, NULL, 0U, 0,
     json_key_val_param_names, json_key_val_param_types, NULL, &vigil_json_value_set_doc},
    {"remove", 6U, json_value_remove, 1U, str_param, VIGIL_TYPE_ERR, 1U, NULL, 0, NULL, 0U, 0,
     json_key_param_names, NULL, NULL, &vigil_json_value_remove_doc},
    {"push", 4U, json_value_push, 1U, obj_param, VIGIL_TYPE_ERR, 1U, NULL, 0, NULL, 0U, 0,
     json_val_param_names, json_val_param_types, NULL, &vigil_json_value_push_doc},
    {"stringify_pretty", 16U, json_value_stringify_pretty, 1U, i32_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, 0U, 0,
     json_indent_param_names, NULL, NULL, &vigil_json_value_stringify_pretty_doc},
    {"merge", 5U, json_value_merge, 1U, obj_param, VIGIL_TYPE_ERR, 1U, NULL, 0, NULL, 0U, 0,
     json_source_param_names, json_source_param_types, NULL, &vigil_json_value_merge_doc},
};
/* clang-format on */

static const vigil_native_class_t json_classes[] = {
    {"Value", 5U, json_value_fields, JSON_VALUE_FIELD_COUNT, json_value_methods,
     sizeof(json_value_methods) / sizeof(json_value_methods[0]), NULL, &vigil_json_value_doc},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_json = {"json",
                                                           4U,
                                                           json_functions,
                                                           sizeof(json_functions) / sizeof(json_functions[0]),
                                                           json_classes,
                                                           sizeof(json_classes) / sizeof(json_classes[0]),
                                                           &vigil_json_module_doc,
                                                           NULL,
                                                           0U};
