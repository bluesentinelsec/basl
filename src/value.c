#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"
#include "platform/platform.h" /* For atomic operations */
#include "vigil/chunk.h"
#include "vigil/map.h"
#include "vigil/string.h"
#include "vigil/value.h"

struct vigil_object
{
    vigil_runtime_t *runtime;
    vigil_object_type_t type;
    int64_t ref_count;
};

typedef struct vigil_string_object
{
    vigil_object_t base;
    vigil_string_t value;
} vigil_string_object_t;

typedef struct vigil_error_object
{
    vigil_object_t base;
    vigil_string_t message;
    int64_t kind;
} vigil_error_object_t;

typedef struct vigil_function_object
{
    vigil_object_t base;
    vigil_string_t name;
    size_t arity;
    size_t return_count;
    vigil_chunk_t chunk;
    vigil_object_t **functions;
    size_t function_count;
    size_t function_index;
    int owns_function_table;
    vigil_value_t *globals;
    size_t global_count;
    int owns_global_table;
    struct vigil_runtime_class *classes;
    size_t class_count;
    int owns_class_table;
} vigil_function_object_t;

typedef struct vigil_closure_object
{
    vigil_object_t base;
    vigil_object_t *function;
    vigil_value_t *captures;
    size_t capture_count;
} vigil_closure_object_t;

typedef struct vigil_runtime_interface_impl
{
    size_t interface_index;
    size_t *function_indices;
    size_t function_count;
} vigil_runtime_interface_impl_t;

typedef struct vigil_runtime_class
{
    vigil_runtime_interface_impl_t *interface_impls;
    size_t interface_impl_count;
} vigil_runtime_class_t;

typedef struct vigil_instance_object
{
    vigil_object_t base;
    size_t class_index;
    vigil_value_t *fields;
    size_t field_count;
} vigil_instance_object_t;

typedef struct vigil_array_object
{
    vigil_object_t base;
    vigil_value_t *items;
    size_t item_count;
    size_t item_capacity;
} vigil_array_object_t;

typedef struct vigil_map_object
{
    vigil_object_t base;
    vigil_map_t entries;
} vigil_map_object_t;

/* Bigint object — heap-boxed 64-bit integer for values outside
   the 48-bit inline range. */
#define VIGIL_BIGINT_MAGIC UINT32_C(0xB161B161)
typedef struct vigil_bigint_object
{
    vigil_object_t base;
    uint32_t magic;
    int is_unsigned;
    union {
        int64_t signed_value;
        uint64_t unsigned_value;
    } as;
} vigil_bigint_object_t;

typedef struct vigil_native_function_object
{
    vigil_object_t base;
    vigil_string_t name;
    size_t arity;
    vigil_native_fn_t function;
} vigil_native_function_object_t;

static const vigil_string_object_t *vigil_string_object_cast(const vigil_object_t *object)
{
    if (object == NULL || object->type != VIGIL_OBJECT_STRING)
    {
        return NULL;
    }

    return (const vigil_string_object_t *)object;
}

static const vigil_function_object_t *vigil_function_object_cast(const vigil_object_t *object)
{
    if (object == NULL || object->type != VIGIL_OBJECT_FUNCTION)
    {
        return NULL;
    }

    return (const vigil_function_object_t *)object;
}

static const vigil_closure_object_t *vigil_closure_object_cast(const vigil_object_t *object)
{
    if (object == NULL || object->type != VIGIL_OBJECT_CLOSURE)
    {
        return NULL;
    }

    return (const vigil_closure_object_t *)object;
}

static const vigil_error_object_t *vigil_error_object_cast(const vigil_object_t *object)
{
    if (object == NULL || object->type != VIGIL_OBJECT_ERROR)
    {
        return NULL;
    }

    return (const vigil_error_object_t *)object;
}

static const vigil_instance_object_t *vigil_instance_object_cast(const vigil_object_t *object)
{
    if (object == NULL || object->type != VIGIL_OBJECT_INSTANCE)
    {
        return NULL;
    }

    return (const vigil_instance_object_t *)object;
}

static const vigil_array_object_t *vigil_array_object_cast(const vigil_object_t *object)
{
    if (object == NULL || object->type != VIGIL_OBJECT_ARRAY)
    {
        return NULL;
    }

    return (const vigil_array_object_t *)object;
}

static const vigil_map_object_t *vigil_map_object_cast(const vigil_object_t *object)
{
    if (object == NULL || object->type != VIGIL_OBJECT_MAP)
    {
        return NULL;
    }

    return (const vigil_map_object_t *)object;
}

static void free_class_table(vigil_runtime_t *runtime, vigil_runtime_class_t *classes, size_t class_count)
{
    size_t class_index;
    void *memory;

    for (class_index = 0U; class_index < class_count; ++class_index)
    {
        size_t impl_index;

        for (impl_index = 0U; impl_index < classes[class_index].interface_impl_count; ++impl_index)
        {
            memory = classes[class_index].interface_impls[impl_index].function_indices;
            vigil_runtime_free(runtime, &memory);
        }

        memory = classes[class_index].interface_impls;
        vigil_runtime_free(runtime, &memory);
    }

    memory = classes;
    vigil_runtime_free(runtime, &memory);
}

static void destroy_function_object(vigil_function_object_t *obj)
{
    vigil_runtime_t *runtime = obj->base.runtime;
    void *memory;

    vigil_string_free(&obj->name);
    vigil_chunk_free(&obj->chunk);

    if (obj->owns_class_table && obj->classes != NULL)
    {
        free_class_table(runtime, obj->classes, obj->class_count);
    }
    if (obj->owns_function_table && obj->functions != NULL)
    {
        size_t i;

        for (i = 0U; i < obj->function_count; ++i)
        {
            if (i != obj->function_index)
            {
                vigil_object_release(&obj->functions[i]);
            }
        }

        memory = obj->functions;
        vigil_runtime_free(runtime, &memory);
    }
    if (obj->owns_global_table && obj->globals != NULL)
    {
        size_t i;

        for (i = 0U; i < obj->global_count; ++i)
        {
            vigil_value_release(&obj->globals[i]);
        }

        memory = obj->globals;
        vigil_runtime_free(runtime, &memory);
    }
}

static void destroy_closure_object(vigil_closure_object_t *obj)
{
    vigil_runtime_t *runtime = obj->base.runtime;
    void *memory;

    if (obj->function != NULL)
    {
        vigil_object_release(&obj->function);
    }
    if (obj->captures != NULL)
    {
        size_t i;

        for (i = 0U; i < obj->capture_count; ++i)
        {
            vigil_value_release(&obj->captures[i]);
        }

        memory = obj->captures;
        vigil_runtime_free(runtime, &memory);
    }
}

static void free_value_array(vigil_runtime_t *runtime, vigil_value_t *values, size_t count)
{
    size_t i;
    void *memory;

    for (i = 0U; i < count; ++i)
    {
        vigil_value_release(&values[i]);
    }

    memory = values;
    vigil_runtime_free(runtime, &memory);
}

static void vigil_object_destroy(vigil_object_t *object)
{
    vigil_runtime_t *runtime;
    void *memory;

    if (object == NULL)
    {
        return;
    }

    runtime = object->runtime;
    switch (object->type)
    {
    case VIGIL_OBJECT_STRING:
        vigil_string_free(&((vigil_string_object_t *)object)->value);
        break;
    case VIGIL_OBJECT_ERROR:
        vigil_string_free(&((vigil_error_object_t *)object)->message);
        break;
    case VIGIL_OBJECT_FUNCTION:
        destroy_function_object((vigil_function_object_t *)object);
        break;
    case VIGIL_OBJECT_CLOSURE:
        destroy_closure_object((vigil_closure_object_t *)object);
        break;
    case VIGIL_OBJECT_INSTANCE: {
        vigil_instance_object_t *inst = (vigil_instance_object_t *)object;
        if (inst->fields != NULL)
        {
            free_value_array(runtime, inst->fields, inst->field_count);
        }
        break;
    }
    case VIGIL_OBJECT_ARRAY: {
        vigil_array_object_t *arr = (vigil_array_object_t *)object;
        if (arr->items != NULL)
        {
            free_value_array(runtime, arr->items, arr->item_count);
        }
        break;
    }
    case VIGIL_OBJECT_MAP:
        vigil_map_free(&((vigil_map_object_t *)object)->entries);
        break;
    case VIGIL_OBJECT_BIGINT:
        break;
    case VIGIL_OBJECT_NATIVE_FUNCTION:
        vigil_string_free(&((vigil_native_function_object_t *)object)->name);
        break;
    case VIGIL_OBJECT_INVALID:
    default:
        break;
    }

    memory = object;
    if (runtime != NULL)
    {
        vigil_runtime_free(runtime, &memory);
    }
    else
    {
        free(memory);
    }
}

static void vigil_object_init(vigil_object_t *object, vigil_runtime_t *runtime, vigil_object_type_t type)
{
    if (object == NULL)
    {
        return;
    }

    object->runtime = runtime;
    object->type = type;
    object->ref_count = 1U;
}

void vigil_value_init_nil(vigil_value_t *value)
{
    if (value == NULL)
    {
        return;
    }

    *value = VIGIL_NANBOX_NIL;
}

void vigil_value_init_bool(vigil_value_t *value, bool boolean)
{
    if (value == NULL)
    {
        return;
    }

    *value = boolean ? VIGIL_NANBOX_TRUE : VIGIL_NANBOX_FALSE;
}

void vigil_value_init_int(vigil_value_t *value, int64_t integer)
{
    if (value == NULL)
    {
        return;
    }

    if (vigil_nanbox_int_fits_inline(integer))
    {
        *value = vigil_nanbox_encode_int(integer);
    }
    else
    {
        /* Box using malloc (no runtime available). */
        vigil_bigint_object_t *obj = (vigil_bigint_object_t *)malloc(sizeof(vigil_bigint_object_t));
        if (obj == NULL)
        {
            *value = VIGIL_NANBOX_NIL;
            return;
        }
        vigil_object_init(&obj->base, NULL, VIGIL_OBJECT_BIGINT);
        obj->magic = VIGIL_BIGINT_MAGIC;
        obj->is_unsigned = 0;
        obj->as.signed_value = integer;
        *value = vigil_nanbox_encode_bigint(&obj->base);
    }
}

void vigil_value_init_uint(vigil_value_t *value, uint64_t integer)
{
    if (value == NULL)
    {
        return;
    }

    if (vigil_nanbox_uint_fits_inline(integer))
    {
        *value = vigil_nanbox_encode_uint(integer);
    }
    else
    {
        vigil_bigint_object_t *obj = (vigil_bigint_object_t *)malloc(sizeof(vigil_bigint_object_t));
        if (obj == NULL)
        {
            *value = VIGIL_NANBOX_NIL;
            return;
        }
        vigil_object_init(&obj->base, NULL, VIGIL_OBJECT_BIGINT);
        obj->magic = VIGIL_BIGINT_MAGIC;
        obj->is_unsigned = 1;
        obj->as.unsigned_value = integer;
        *value = vigil_nanbox_encode_biguint(&obj->base);
    }
}

void vigil_value_init_float(vigil_value_t *value, double number)
{
    if (value == NULL)
    {
        return;
    }

    *value = vigil_nanbox_encode_double(number);
}

void vigil_value_init_object(vigil_value_t *value, vigil_object_t **object)
{
    vigil_object_t *resolved_object;

    if (value == NULL)
    {
        return;
    }

    *value = VIGIL_NANBOX_NIL;

    if (object == NULL || *object == NULL)
    {
        return;
    }

    resolved_object = *object;
    *object = NULL;
    *value = vigil_nanbox_encode_object(resolved_object);
}

vigil_value_t vigil_value_copy(const vigil_value_t *value)
{
    vigil_value_t copy;

    if (value == NULL)
    {
        return VIGIL_NANBOX_NIL;
    }

    copy = *value;
    if (vigil_nanbox_has_object(copy))
    {
        vigil_object_retain((vigil_object_t *)vigil_nanbox_decode_ptr(copy));
    }

    return copy;
}

void vigil_value_release(vigil_value_t *value)
{
    vigil_object_t *object;

    if (value == NULL)
    {
        return;
    }

    if (vigil_nanbox_has_object(*value))
    {
        object = (vigil_object_t *)vigil_nanbox_decode_ptr(*value);
        vigil_object_release(&object);
    }

    *value = VIGIL_NANBOX_NIL;
}

vigil_value_kind_t vigil_value_kind(const vigil_value_t *value)
{
    uint64_t v;

    if (value == NULL)
    {
        return VIGIL_VALUE_NIL;
    }

    v = *value;
    if (vigil_nanbox_is_double(v))
    {
        return VIGIL_VALUE_FLOAT;
    }
    if (vigil_nanbox_is_nil(v))
    {
        return VIGIL_VALUE_NIL;
    }
    if (vigil_nanbox_is_bool(v))
    {
        return VIGIL_VALUE_BOOL;
    }
    if (vigil_nanbox_is_int(v))
    {
        return VIGIL_VALUE_INT;
    }
    if (vigil_nanbox_is_uint(v))
    {
        return VIGIL_VALUE_UINT;
    }
    /* object, bigint ptr, biguint ptr all have sign bit set */
    if (vigil_nanbox_is_object(v))
    {
        return VIGIL_VALUE_OBJECT;
    }
    return VIGIL_VALUE_NIL;
}

bool vigil_value_as_bool(const vigil_value_t *value)
{
    if (value == NULL)
    {
        return false;
    }

    return vigil_nanbox_decode_bool(*value);
}

int64_t vigil_value_as_int(const vigil_value_t *value)
{
    uint64_t v;

    if (value == NULL)
    {
        return 0;
    }

    v = *value;
    if (vigil_nanbox_is_int_inline(v))
    {
        return vigil_nanbox_decode_int(v);
    }
    if (vigil_nanbox_is_bigint(v))
    {
        const vigil_bigint_object_t *bi = (const vigil_bigint_object_t *)vigil_nanbox_decode_ptr(v);
        return bi->as.signed_value;
    }
    return 0;
}

uint64_t vigil_value_as_uint(const vigil_value_t *value)
{
    uint64_t v;

    if (value == NULL)
    {
        return 0U;
    }

    v = *value;
    if (vigil_nanbox_is_uint_inline(v))
    {
        return vigil_nanbox_decode_uint(v);
    }
    if (vigil_nanbox_is_biguint(v))
    {
        const vigil_bigint_object_t *bi = (const vigil_bigint_object_t *)vigil_nanbox_decode_ptr(v);
        return bi->as.unsigned_value;
    }
    return 0U;
}

double vigil_value_as_float(const vigil_value_t *value)
{
    if (value == NULL)
    {
        return 0.0;
    }

    if (vigil_nanbox_is_double(*value))
    {
        return vigil_nanbox_decode_double(*value);
    }
    return 0.0;
}

vigil_object_t *vigil_value_as_object(const vigil_value_t *value)
{
    if (value == NULL)
    {
        return NULL;
    }

    if (vigil_nanbox_is_object(*value))
    {
        return (vigil_object_t *)vigil_nanbox_decode_ptr(*value);
    }
    return NULL;
}

vigil_object_type_t vigil_object_type(const vigil_object_t *object)
{
    if (object == NULL)
    {
        return VIGIL_OBJECT_INVALID;
    }

    return object->type;
}

size_t vigil_object_ref_count(const vigil_object_t *object)
{
    if (object == NULL)
    {
        return 0U;
    }

    return (size_t)object->ref_count;
}

void vigil_object_retain(vigil_object_t *object)
{
    if (object == NULL)
    {
        return;
    }

    object->ref_count++;
}

void vigil_object_make_immortal(vigil_object_t *object)
{
    if (object != NULL)
        object->ref_count = INT64_MAX / 2;
}

void vigil_object_force_destroy(vigil_object_t **object)
{
    if (object == NULL || *object == NULL)
        return;
    (*object)->ref_count = 1;
    vigil_object_release(object);
}

void vigil_object_release(vigil_object_t **object)
{
    vigil_object_t *resolved_object;

    if (object == NULL || *object == NULL)
    {
        return;
    }

    resolved_object = *object;
    *object = NULL;

    if (--resolved_object->ref_count > 0)
    {
        return;
    }

    vigil_object_destroy(resolved_object);
}

/* ── Bigint object creation (internal) ───────────────────────────── */

static vigil_status_t vigil_bigint_object_new_signed(vigil_runtime_t *runtime, int64_t value,
                                                     vigil_object_t **out_object, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_bigint_object_t *object;
    void *memory;

    vigil_error_clear(error);
    status = vigil_runtime_alloc(runtime, sizeof(*object), &memory, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    object = (vigil_bigint_object_t *)memory;
    vigil_object_init(&object->base, runtime, VIGIL_OBJECT_BIGINT);
    object->magic = VIGIL_BIGINT_MAGIC;
    object->is_unsigned = 0;
    object->as.signed_value = value;
    *out_object = &object->base;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_bigint_object_new_unsigned(vigil_runtime_t *runtime, uint64_t value,
                                                       vigil_object_t **out_object, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_bigint_object_t *object;
    void *memory;

    vigil_error_clear(error);
    status = vigil_runtime_alloc(runtime, sizeof(*object), &memory, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    object = (vigil_bigint_object_t *)memory;
    vigil_object_init(&object->base, runtime, VIGIL_OBJECT_BIGINT);
    object->magic = VIGIL_BIGINT_MAGIC;
    object->is_unsigned = 1;
    object->as.unsigned_value = value;
    *out_object = &object->base;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_value_init_int_rt(vigil_value_t *value, int64_t integer, vigil_runtime_t *runtime,
                                       vigil_error_t *error)
{
    vigil_object_t *obj;
    vigil_status_t status;

    if (value == NULL)
    {
        return VIGIL_STATUS_OK;
    }

    if (vigil_nanbox_int_fits_inline(integer))
    {
        *value = vigil_nanbox_encode_int(integer);
        return VIGIL_STATUS_OK;
    }

    status = vigil_bigint_object_new_signed(runtime, integer, &obj, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    *value = vigil_nanbox_encode_bigint(obj);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_value_init_uint_rt(vigil_value_t *value, uint64_t integer, vigil_runtime_t *runtime,
                                        vigil_error_t *error)
{
    vigil_object_t *obj;
    vigil_status_t status;

    if (value == NULL)
    {
        return VIGIL_STATUS_OK;
    }

    if (vigil_nanbox_uint_fits_inline(integer))
    {
        *value = vigil_nanbox_encode_uint(integer);
        return VIGIL_STATUS_OK;
    }

    status = vigil_bigint_object_new_unsigned(runtime, integer, &obj, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    *value = vigil_nanbox_encode_biguint(obj);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_string_object_new(vigil_runtime_t *runtime, const char *value, size_t length,
                                       vigil_object_t **out_object, vigil_error_t *error)
{
    vigil_string_object_t *object;
    void *memory;
    vigil_status_t status;

    vigil_error_clear(error);

    if (runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "string object value must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (out_object == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_object must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    /* Allocate object struct and string bytes in one block to halve allocation count. */
    *out_object = NULL;
    memory = NULL;
    status = vigil_runtime_alloc(runtime, sizeof(*object) + length + 1U, &memory, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    object = (vigil_string_object_t *)memory;
    vigil_object_init(&object->base, runtime, VIGIL_OBJECT_STRING);
    /* Point bytes.data at the inline region immediately after the struct.
     * Set bytes.runtime = NULL so vigil_byte_buffer_free skips the free;
     * the whole block is freed when the object itself is freed. */
    object->value.bytes.runtime = NULL;
    object->value.bytes.data = (uint8_t *)(object + 1);
    object->value.bytes.capacity = length + 1U;
    object->value.bytes.length = length + 1U;
    if (length > 0U)
    {
        memcpy(object->value.bytes.data, value, length);
    }
    object->value.bytes.data[length] = '\0';

    *out_object = &object->base;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_error_object_new(vigil_runtime_t *runtime, const char *message, size_t length, int64_t kind,
                                      vigil_object_t **out_object, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_error_object_t *object;
    void *memory;

    vigil_error_clear(error);

    if (runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (message == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "error object message must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (out_object == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_object must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_object = NULL;
    memory = NULL;
    status = vigil_runtime_alloc(runtime, sizeof(*object), &memory, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    object = (vigil_error_object_t *)memory;
    memset(object, 0, sizeof(*object));
    vigil_object_init(&object->base, runtime, VIGIL_OBJECT_ERROR);
    vigil_string_init(&object->message, runtime);
    status = vigil_string_assign(&object->message, message, length, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_object_t *base = &object->base;

        vigil_object_release(&base);
        return status;
    }
    object->kind = kind;
    *out_object = &object->base;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_error_object_new_cstr(vigil_runtime_t *runtime, const char *message, int64_t kind,
                                           vigil_object_t **out_object, vigil_error_t *error)
{
    if (message == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "error object message must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return vigil_error_object_new(runtime, message, strlen(message), kind, out_object, error);
}

vigil_status_t vigil_string_object_new_cstr(vigil_runtime_t *runtime, const char *value, vigil_object_t **out_object,
                                            vigil_error_t *error)
{
    if (value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "string object value must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return vigil_string_object_new(runtime, value, strlen(value), out_object, error);
}

const char *vigil_string_object_c_str(const vigil_object_t *object)
{
    const vigil_string_object_t *string_object;

    string_object = vigil_string_object_cast(object);
    if (string_object == NULL)
    {
        return "";
    }

    return vigil_string_c_str(&string_object->value);
}

size_t vigil_string_object_length(const vigil_object_t *object)
{
    const vigil_string_object_t *string_object;

    string_object = vigil_string_object_cast(object);
    if (string_object == NULL)
    {
        return 0U;
    }

    return vigil_string_length(&string_object->value);
}

const char *vigil_error_object_message(const vigil_object_t *object)
{
    const vigil_error_object_t *error_object = vigil_error_object_cast(object);

    if (error_object == NULL)
    {
        return NULL;
    }

    return vigil_string_c_str(&error_object->message);
}

size_t vigil_error_object_message_length(const vigil_object_t *object)
{
    const vigil_error_object_t *error_object = vigil_error_object_cast(object);

    if (error_object == NULL)
    {
        return 0U;
    }

    return vigil_string_length(&error_object->message);
}

int64_t vigil_error_object_kind(const vigil_object_t *object)
{
    const vigil_error_object_t *error_object = vigil_error_object_cast(object);

    if (error_object == NULL)
    {
        return 0;
    }

    return error_object->kind;
}

vigil_status_t vigil_function_object_new(vigil_runtime_t *runtime, const char *name, size_t name_length, size_t arity,
                                         size_t return_count, vigil_chunk_t *chunk, vigil_object_t **out_object,
                                         vigil_error_t *error)
{
    vigil_status_t status;
    vigil_function_object_t *object;
    void *memory;

    vigil_error_clear(error);

    if (runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (name == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "function object name must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (chunk == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "function object chunk must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (chunk->runtime != runtime)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "function object chunk runtime must match runtime");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (out_object == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_object must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_object = NULL;
    memory = NULL;
    status = vigil_runtime_alloc(runtime, sizeof(*object), &memory, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    object = (vigil_function_object_t *)memory;
    vigil_object_init(&object->base, runtime, VIGIL_OBJECT_FUNCTION);
    vigil_string_init(&object->name, runtime);
    object->arity = arity;
    object->return_count = return_count;
    object->functions = NULL;
    object->function_count = 0U;
    object->function_index = 0U;
    object->owns_function_table = 0;
    object->globals = NULL;
    object->global_count = 0U;
    object->owns_global_table = 0;
    object->classes = NULL;
    object->class_count = 0U;
    object->owns_class_table = 0;
    status = vigil_string_assign(&object->name, name, name_length, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_object_destroy(&object->base);
        return status;
    }

    object->chunk = *chunk;
    memset(chunk, 0, sizeof(*chunk));
    *out_object = &object->base;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_function_object_new_cstr(vigil_runtime_t *runtime, const char *name, size_t arity,
                                              size_t return_count, vigil_chunk_t *chunk, vigil_object_t **out_object,
                                              vigil_error_t *error)
{
    if (name == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "function object name must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return vigil_function_object_new(runtime, name, strlen(name), arity, return_count, chunk, out_object, error);
}

const char *vigil_function_object_name(const vigil_object_t *object)
{
    const vigil_function_object_t *function_object;

    function_object = vigil_function_object_cast(object);
    if (function_object == NULL)
    {
        return "";
    }

    return vigil_string_c_str(&function_object->name);
}

size_t vigil_function_object_arity(const vigil_object_t *object)
{
    const vigil_function_object_t *function_object;

    function_object = vigil_function_object_cast(object);
    if (function_object == NULL)
    {
        return 0U;
    }

    return function_object->arity;
}

size_t vigil_function_object_return_count(const vigil_object_t *object)
{
    const vigil_function_object_t *function_object;

    function_object = vigil_function_object_cast(object);
    if (function_object == NULL)
    {
        return 0U;
    }

    return function_object->return_count;
}

const vigil_chunk_t *vigil_function_object_chunk(const vigil_object_t *object)
{
    const vigil_function_object_t *function_object;

    function_object = vigil_function_object_cast(object);
    if (function_object == NULL)
    {
        return NULL;
    }

    return &function_object->chunk;
}

static vigil_status_t closure_alloc_captures(vigil_runtime_t *runtime, vigil_closure_object_t *object,
                                             const vigil_value_t *captures, size_t capture_count, vigil_error_t *error)
{
    vigil_status_t status;
    void *memory = NULL;
    size_t i;

    status = vigil_runtime_alloc(runtime, capture_count * sizeof(*object->captures), &memory, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_object_t *base = &object->base;
        vigil_object_release(&base);
        return status;
    }

    object->captures = (vigil_value_t *)memory;
    for (i = 0U; i < capture_count; ++i)
    {
        object->captures[i] = vigil_value_copy(&captures[i]);
    }
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_closure_object_new(vigil_runtime_t *runtime, vigil_object_t *function,
                                        const vigil_value_t *captures, size_t capture_count,
                                        vigil_object_t **out_object, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_closure_object_t *object;
    void *memory;

    vigil_error_clear(error);
    if (runtime == NULL || function == NULL || out_object == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "closure object arguments are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (vigil_object_type(function) != VIGIL_OBJECT_FUNCTION)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "closure function must be a function object");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (capture_count != 0U && captures == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "closure captures must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_object = NULL;
    memory = NULL;
    status = vigil_runtime_alloc(runtime, sizeof(*object), &memory, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    object = (vigil_closure_object_t *)memory;
    memset(object, 0, sizeof(*object));
    vigil_object_init(&object->base, runtime, VIGIL_OBJECT_CLOSURE);
    vigil_object_retain(function);
    object->function = function;
    object->capture_count = capture_count;

    if (capture_count != 0U)
    {
        status = closure_alloc_captures(runtime, object, captures, capture_count, error);
        if (status != VIGIL_STATUS_OK)
            return status;
    }

    *out_object = &object->base;
    return VIGIL_STATUS_OK;
}

const vigil_object_t *vigil_closure_object_function(const vigil_object_t *object)
{
    const vigil_closure_object_t *closure_object = vigil_closure_object_cast(object);

    if (closure_object == NULL)
    {
        return NULL;
    }

    return closure_object->function;
}

size_t vigil_closure_object_capture_count(const vigil_object_t *object)
{
    const vigil_closure_object_t *closure_object = vigil_closure_object_cast(object);

    if (closure_object == NULL)
    {
        return 0U;
    }

    return closure_object->capture_count;
}

int vigil_closure_object_get_capture(const vigil_object_t *object, size_t index, vigil_value_t *out_value)
{
    const vigil_closure_object_t *closure_object = vigil_closure_object_cast(object);

    if (closure_object == NULL || out_value == NULL || index >= closure_object->capture_count)
    {
        return 0;
    }

    *out_value = vigil_value_copy(&closure_object->captures[index]);
    return 1;
}

vigil_status_t vigil_closure_object_set_capture(vigil_object_t *object, size_t index, const vigil_value_t *value,
                                                vigil_error_t *error)
{
    vigil_closure_object_t *closure_object = (vigil_closure_object_t *)object;
    vigil_value_t copy;

    vigil_error_clear(error);
    if (closure_object == NULL || closure_object->base.type != VIGIL_OBJECT_CLOSURE || value == NULL ||
        index >= closure_object->capture_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "closure capture arguments are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    copy = vigil_value_copy(value);
    vigil_value_release(&closure_object->captures[index]);
    closure_object->captures[index] = copy;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_instance_object_new(vigil_runtime_t *runtime, size_t class_index, const vigil_value_t *fields,
                                         size_t field_count, vigil_object_t **out_object, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_instance_object_t *object;
    void *memory;
    size_t i;

    vigil_error_clear(error);

    if (runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (field_count != 0U && fields == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "instance object fields must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (out_object == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_object must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_object = NULL;
    memory = NULL;
    status = vigil_runtime_alloc(runtime, sizeof(*object), &memory, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    object = (vigil_instance_object_t *)memory;
    vigil_object_init(&object->base, runtime, VIGIL_OBJECT_INSTANCE);
    object->class_index = class_index;
    object->fields = NULL;
    object->field_count = field_count;
    if (field_count != 0U)
    {
        memory = NULL;
        status = vigil_runtime_alloc(runtime, field_count * sizeof(*object->fields), &memory, error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_object_destroy(&object->base);
            return status;
        }

        object->fields = (vigil_value_t *)memory;
        for (i = 0U; i < field_count; ++i)
        {
            object->fields[i] = vigil_value_copy(&fields[i]);
        }
    }

    *out_object = &object->base;
    return VIGIL_STATUS_OK;
}

size_t vigil_instance_object_class_index(const vigil_object_t *object)
{
    const vigil_instance_object_t *instance_object;

    instance_object = vigil_instance_object_cast(object);
    if (instance_object == NULL)
    {
        return 0U;
    }

    return instance_object->class_index;
}

size_t vigil_instance_object_field_count(const vigil_object_t *object)
{
    const vigil_instance_object_t *instance_object;

    instance_object = vigil_instance_object_cast(object);
    if (instance_object == NULL)
    {
        return 0U;
    }

    return instance_object->field_count;
}

int vigil_instance_object_get_field(const vigil_object_t *object, size_t index, vigil_value_t *out_value)
{
    const vigil_instance_object_t *instance_object;

    instance_object = vigil_instance_object_cast(object);
    if (instance_object == NULL || out_value == NULL || index >= instance_object->field_count)
    {
        return 0;
    }

    *out_value = vigil_value_copy(&instance_object->fields[index]);
    return 1;
}

vigil_status_t vigil_instance_object_set_field(vigil_object_t *object, size_t index, const vigil_value_t *value,
                                               vigil_error_t *error)
{
    const vigil_instance_object_t *instance_object;
    vigil_value_t copy;

    vigil_error_clear(error);
    instance_object = vigil_instance_object_cast(object);
    if (instance_object == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "object must be an instance object");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "field value must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (index >= instance_object->field_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "field index is out of range");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    copy = vigil_value_copy(value);
    vigil_value_release(&((vigil_instance_object_t *)object)->fields[index]);
    ((vigil_instance_object_t *)object)->fields[index] = copy;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_array_object_new(vigil_runtime_t *runtime, const vigil_value_t *items, size_t item_count,
                                      vigil_object_t **out_object, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_array_object_t *object;
    void *memory;
    size_t index;

    vigil_error_clear(error);
    if (runtime == NULL || out_object == NULL || (item_count != 0U && items == NULL))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "array object arguments are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_object = NULL;
    memory = NULL;
    status = vigil_runtime_alloc(runtime, sizeof(*object), &memory, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    object = (vigil_array_object_t *)memory;
    memset(object, 0, sizeof(*object));
    vigil_object_init(&object->base, runtime, VIGIL_OBJECT_ARRAY);
    if (item_count != 0U)
    {
        memory = NULL;
        status = vigil_runtime_alloc(runtime, item_count * sizeof(*object->items), &memory, error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_object_destroy(&object->base);
            return status;
        }

        object->items = (vigil_value_t *)memory;
        object->item_count = item_count;
        object->item_capacity = item_count;
        for (index = 0U; index < item_count; ++index)
        {
            object->items[index] = vigil_value_copy(&items[index]);
        }
    }

    *out_object = &object->base;
    return VIGIL_STATUS_OK;
}

size_t vigil_array_object_length(const vigil_object_t *object)
{
    const vigil_array_object_t *array_object;

    array_object = vigil_array_object_cast(object);
    if (array_object == NULL)
    {
        return 0U;
    }

    return array_object->item_count;
}

int vigil_array_object_get(const vigil_object_t *object, size_t index, vigil_value_t *out_value)
{
    const vigil_array_object_t *array_object;

    array_object = vigil_array_object_cast(object);
    if (array_object == NULL || out_value == NULL || index >= array_object->item_count)
    {
        return 0;
    }

    *out_value = vigil_value_copy(&array_object->items[index]);
    return 1;
}

vigil_status_t vigil_array_object_append(vigil_object_t *object, const vigil_value_t *value, vigil_error_t *error)
{
    vigil_array_object_t *array_object;
    vigil_value_t copy;
    void *memory;
    vigil_status_t status;
    size_t new_capacity;

    vigil_error_clear(error);
    array_object = (vigil_array_object_t *)object;
    if (array_object == NULL || array_object->base.type != VIGIL_OBJECT_ARRAY || value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "array object append arguments are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (array_object->item_count == array_object->item_capacity)
    {
        memory = array_object->items;
        new_capacity = array_object->item_capacity == 0U ? 4U : array_object->item_capacity * 2U;
        if (array_object->item_capacity == 0U)
        {
            status = vigil_runtime_alloc(array_object->base.runtime, new_capacity * sizeof(*array_object->items),
                                         &memory, error);
        }
        else
        {
            status = vigil_runtime_realloc(array_object->base.runtime, &memory,
                                           new_capacity * sizeof(*array_object->items), error);
        }
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        array_object->items = (vigil_value_t *)memory;
        array_object->item_capacity = new_capacity;
    }

    copy = vigil_value_copy(value);
    array_object->items[array_object->item_count] = copy;
    array_object->item_count += 1U;
    return VIGIL_STATUS_OK;
}

int vigil_array_object_pop(vigil_object_t *object, vigil_value_t *out_value)
{
    vigil_array_object_t *array_object;
    size_t index;

    if (out_value == NULL)
    {
        return 0;
    }
    vigil_value_init_nil(out_value);
    array_object = (vigil_array_object_t *)object;
    if (array_object == NULL || array_object->base.type != VIGIL_OBJECT_ARRAY || array_object->item_count == 0U)
    {
        return 0;
    }

    index = array_object->item_count - 1U;
    *out_value = array_object->items[index];
    vigil_value_init_nil(&array_object->items[index]);
    array_object->item_count = index;
    if (index == 0U)
    {
        /* Keep the allocation for reuse; repeated push/pop traffic is common. */
        return 1;
    }
    return 1;
}

vigil_status_t vigil_array_object_set(vigil_object_t *object, size_t index, const vigil_value_t *value,
                                      vigil_error_t *error)
{
    vigil_array_object_t *array_object;
    vigil_value_t copy;

    vigil_error_clear(error);
    array_object = (vigil_array_object_t *)object;
    if (array_object == NULL || array_object->base.type != VIGIL_OBJECT_ARRAY || value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "array object set arguments are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (index >= array_object->item_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "array index is out of range");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    copy = vigil_value_copy(value);
    vigil_value_release(&array_object->items[index]);
    array_object->items[index] = copy;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_array_object_slice(const vigil_object_t *object, size_t start, size_t end,
                                        vigil_object_t **out_object, vigil_error_t *error)
{
    const vigil_array_object_t *array_object;

    vigil_error_clear(error);
    array_object = vigil_array_object_cast(object);
    if (array_object == NULL || out_object == NULL || start > end || end > array_object->item_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "array slice arguments are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return vigil_array_object_new(array_object->base.runtime, array_object->items + start, end - start, out_object,
                                  error);
}

vigil_status_t vigil_map_object_new(vigil_runtime_t *runtime, vigil_object_t **out_object, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_map_object_t *object;
    void *memory;

    vigil_error_clear(error);
    if (runtime == NULL || out_object == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "map object arguments are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_object = NULL;
    memory = NULL;
    status = vigil_runtime_alloc(runtime, sizeof(*object), &memory, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    object = (vigil_map_object_t *)memory;
    memset(object, 0, sizeof(*object));
    vigil_object_init(&object->base, runtime, VIGIL_OBJECT_MAP);
    vigil_map_init(&object->entries, runtime);
    *out_object = &object->base;
    return VIGIL_STATUS_OK;
}

size_t vigil_map_object_count(const vigil_object_t *object)
{
    const vigil_map_object_t *map_object;

    map_object = vigil_map_object_cast(object);
    if (map_object == NULL)
    {
        return 0U;
    }

    return vigil_map_count(&map_object->entries);
}

int vigil_map_object_get(const vigil_object_t *object, const vigil_value_t *key, vigil_value_t *out_value)
{
    const vigil_map_object_t *map_object;
    const vigil_value_t *stored;

    map_object = vigil_map_object_cast(object);
    if (map_object == NULL || key == NULL || out_value == NULL)
    {
        return 0;
    }

    stored = vigil_map_get_value(&map_object->entries, key);
    if (stored == NULL)
    {
        return 0;
    }

    *out_value = vigil_value_copy(stored);
    return 1;
}

int vigil_map_object_key_at(const vigil_object_t *object, size_t index, vigil_value_t *out_key)
{
    const vigil_map_object_t *map_object;
    const vigil_value_t *stored_key;
    const vigil_value_t *stored_value;

    if (out_key == NULL)
    {
        return 0;
    }
    vigil_value_init_nil(out_key);
    map_object = vigil_map_object_cast(object);
    if (map_object == NULL)
    {
        return 0;
    }

    stored_key = NULL;
    stored_value = NULL;
    if (!vigil_map_entry_value_at(&map_object->entries, index, &stored_key, &stored_value))
    {
        return 0;
    }

    *out_key = vigil_value_copy(stored_key);
    return 1;
}

int vigil_map_object_value_at(const vigil_object_t *object, size_t index, vigil_value_t *out_value)
{
    const vigil_map_object_t *map_object;
    const vigil_value_t *stored_key;
    const vigil_value_t *stored_value;

    map_object = vigil_map_object_cast(object);
    if (map_object == NULL || out_value == NULL)
    {
        return 0;
    }

    stored_key = NULL;
    stored_value = NULL;
    if (!vigil_map_entry_value_at(&map_object->entries, index, &stored_key, &stored_value))
    {
        return 0;
    }

    *out_value = vigil_value_copy(stored_value);
    return 1;
}

vigil_status_t vigil_map_object_set(vigil_object_t *object, const vigil_value_t *key, const vigil_value_t *value,
                                    vigil_error_t *error)
{
    vigil_map_object_t *map_object;

    vigil_error_clear(error);
    map_object = (vigil_map_object_t *)object;
    if (map_object == NULL || map_object->base.type != VIGIL_OBJECT_MAP || key == NULL || value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "map object set arguments are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return vigil_map_set_value(&map_object->entries, key, value, error);
}

int vigil_map_object_remove(vigil_object_t *object, const vigil_value_t *key, vigil_value_t *out_value,
                            vigil_error_t *error)
{
    vigil_map_object_t *map_object;
    const vigil_value_t *stored;
    int removed;
    vigil_status_t status;

    vigil_error_clear(error);
    if (out_value != NULL)
    {
        vigil_value_init_nil(out_value);
    }
    map_object = (vigil_map_object_t *)object;
    if (map_object == NULL || map_object->base.type != VIGIL_OBJECT_MAP || key == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "map object remove arguments are invalid");
        return 0;
    }

    stored = vigil_map_get_value(&map_object->entries, key);
    if (stored == NULL)
    {
        return 0;
    }
    if (out_value != NULL)
    {
        *out_value = vigil_value_copy(stored);
    }

    removed = 0;
    status = vigil_map_remove_value(&map_object->entries, key, &removed, error);
    if (status != VIGIL_STATUS_OK || !removed)
    {
        if (out_value != NULL)
        {
            vigil_value_release(out_value);
        }
        return 0;
    }
    return 1;
}

static vigil_status_t alloc_class_table(vigil_runtime_t *runtime, const vigil_runtime_class_init_t *classes_init,
                                        size_t class_count, vigil_runtime_class_t **out_classes, vigil_error_t *error)
{
    vigil_runtime_class_t *classes;
    void *memory;
    size_t class_index;

    memory = NULL;
    if (vigil_runtime_alloc(runtime, class_count * sizeof(*classes), &memory, error) != VIGIL_STATUS_OK)
    {
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    classes = (vigil_runtime_class_t *)memory;
    memset(classes, 0, class_count * sizeof(*classes));

    for (class_index = 0U; class_index < class_count; ++class_index)
    {
        size_t interface_count = classes_init[class_index].interface_impl_count;
        size_t i;

        classes[class_index].interface_impl_count = interface_count;
        if (interface_count == 0U)
        {
            continue;
        }

        memory = NULL;
        if (vigil_runtime_alloc(runtime, interface_count * sizeof(*classes[class_index].interface_impls), &memory,
                                error) != VIGIL_STATUS_OK)
        {
            free_class_table(runtime, classes, class_count);
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        classes[class_index].interface_impls = (vigil_runtime_interface_impl_t *)memory;
        memset(classes[class_index].interface_impls, 0,
               interface_count * sizeof(*classes[class_index].interface_impls));

        for (i = 0U; i < interface_count; ++i)
        {
            size_t method_count = classes_init[class_index].interface_impls[i].function_count;

            classes[class_index].interface_impls[i].interface_index =
                classes_init[class_index].interface_impls[i].interface_index;
            classes[class_index].interface_impls[i].function_count = method_count;
            if (method_count == 0U)
            {
                continue;
            }

            memory = NULL;
            if (vigil_runtime_alloc(runtime,
                                    method_count * sizeof(*classes[class_index].interface_impls[i].function_indices),
                                    &memory, error) != VIGIL_STATUS_OK)
            {
                free_class_table(runtime, classes, class_count);
                return VIGIL_STATUS_OUT_OF_MEMORY;
            }
            classes[class_index].interface_impls[i].function_indices = (size_t *)memory;
            memcpy(classes[class_index].interface_impls[i].function_indices,
                   classes_init[class_index].interface_impls[i].function_indices,
                   method_count * sizeof(*classes[class_index].interface_impls[i].function_indices));
        }
    }

    *out_classes = classes;
    return VIGIL_STATUS_OK;
}

static vigil_status_t alloc_globals_table(vigil_runtime_t *runtime, const vigil_value_t *initial_globals,
                                          size_t global_count, vigil_value_t **out_globals, vigil_error_t *error)
{
    vigil_value_t *globals;
    void *memory;
    size_t i;

    memory = NULL;
    if (vigil_runtime_alloc(runtime, global_count * sizeof(*globals), &memory, error) != VIGIL_STATUS_OK)
    {
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    globals = (vigil_value_t *)memory;
    for (i = 0U; i < global_count; ++i)
    {
        if (initial_globals != NULL)
        {
            globals[i] = vigil_value_copy(&initial_globals[i]);
        }
        else
        {
            vigil_value_init_nil(&globals[i]);
        }
    }
    *out_globals = globals;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_function_object_attach_siblings(vigil_object_t *owner_function, vigil_object_t **functions,
                                                     size_t function_count, size_t owner_index,
                                                     const vigil_value_t *initial_globals, size_t global_count,
                                                     const vigil_runtime_class_init_t *classes_init, size_t class_count,
                                                     vigil_error_t *error)
{
    size_t i;
    vigil_function_object_t *owner;
    vigil_function_object_t *function_object;
    vigil_runtime_t *runtime;
    vigil_runtime_class_t *classes;
    vigil_value_t *globals;
    vigil_status_t status;

    vigil_error_clear(error);
    owner = (vigil_function_object_t *)owner_function;
    if (owner == NULL || owner->base.type != VIGIL_OBJECT_FUNCTION)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "owner_function must be a function object");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (functions == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "function table must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (function_count == 0U || owner_index >= function_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "function table bounds are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    runtime = owner->base.runtime;
    classes = NULL;
    globals = NULL;
    if (class_count != 0U && classes_init == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "class metadata must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (global_count != 0U)
    {
        status = alloc_globals_table(runtime, initial_globals, global_count, &globals, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }

    if (class_count != 0U)
    {
        status = alloc_class_table(runtime, classes_init, class_count, &classes, error);
        if (status != VIGIL_STATUS_OK)
        {
            goto cleanup;
        }
    }

    for (i = 0U; i < function_count; ++i)
    {
        function_object = (vigil_function_object_t *)functions[i];
        if (function_object == NULL || function_object->base.type != VIGIL_OBJECT_FUNCTION)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                    "function table entries must all be function objects");
            status = VIGIL_STATUS_INVALID_ARGUMENT;
            goto cleanup;
        }

        function_object->functions = functions;
        function_object->function_count = function_count;
        function_object->function_index = i;
        function_object->owns_function_table = 0;
        function_object->globals = globals;
        function_object->global_count = global_count;
        function_object->owns_global_table = 0;
        function_object->classes = classes;
        function_object->class_count = class_count;
        function_object->owns_class_table = 0;
    }

    owner->owns_function_table = 1;
    owner->owns_global_table = 1;
    owner->owns_class_table = 1;
    return VIGIL_STATUS_OK;

cleanup:
    if (globals != NULL)
    {
        free_value_array(runtime, globals, global_count);
    }
    if (classes != NULL)
    {
        free_class_table(runtime, classes, class_count);
    }
    return status;
}

const vigil_object_t *vigil_function_object_sibling(const vigil_object_t *function, size_t index)
{
    const vigil_function_object_t *function_object;

    function_object = vigil_function_object_cast(function);
    if (function_object == NULL || function_object->functions == NULL)
    {
        return NULL;
    }
    if (index >= function_object->function_count)
    {
        return NULL;
    }

    return function_object->functions[index];
}

const vigil_object_t *vigil_function_object_resolve_interface_method(const vigil_object_t *function, size_t class_index,
                                                                     size_t interface_index, size_t method_index)
{
    const vigil_function_object_t *function_object;
    const vigil_runtime_class_t *class_metadata;
    size_t impl_index;
    size_t function_index;

    function_object = vigil_function_object_cast(function);
    if (function_object == NULL || function_object->classes == NULL)
    {
        return NULL;
    }
    if (class_index >= function_object->class_count)
    {
        return NULL;
    }

    class_metadata = &function_object->classes[class_index];
    for (impl_index = 0U; impl_index < class_metadata->interface_impl_count; ++impl_index)
    {
        if (class_metadata->interface_impls[impl_index].interface_index != interface_index)
        {
            continue;
        }
        if (method_index >= class_metadata->interface_impls[impl_index].function_count)
        {
            return NULL;
        }

        function_index = class_metadata->interface_impls[impl_index].function_indices[method_index];
        return vigil_function_object_sibling(function, function_index);
    }

    return NULL;
}

int vigil_function_object_get_global(const vigil_object_t *function, size_t index, vigil_value_t *out_value)
{
    const vigil_function_object_t *function_object;

    function_object = vigil_function_object_cast(function);
    if (function_object == NULL || out_value == NULL || function_object->globals == NULL ||
        index >= function_object->global_count)
    {
        return 0;
    }

    *out_value = vigil_value_copy(&function_object->globals[index]);
    return 1;
}

vigil_status_t vigil_function_object_set_global(const vigil_object_t *function, size_t index,
                                                const vigil_value_t *value, vigil_error_t *error)
{
    const vigil_function_object_t *function_object;
    vigil_value_t copy;

    vigil_error_clear(error);
    function_object = vigil_function_object_cast(function);
    if (function_object == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "function must be a function object");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "global value must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (function_object->globals == NULL || index >= function_object->global_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "global index is out of range");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    copy = vigil_value_copy(value);
    vigil_value_release(&((vigil_function_object_t *)function)->globals[index]);
    ((vigil_function_object_t *)function)->globals[index] = copy;
    return VIGIL_STATUS_OK;
}

const vigil_object_t *vigil_callable_object_function(const vigil_object_t *callable)
{
    if (callable == NULL)
    {
        return NULL;
    }
    if (vigil_object_type(callable) == VIGIL_OBJECT_FUNCTION)
    {
        return callable;
    }
    if (vigil_object_type(callable) == VIGIL_OBJECT_CLOSURE)
    {
        return vigil_closure_object_function(callable);
    }

    return NULL;
}

size_t vigil_callable_object_arity(const vigil_object_t *callable)
{
    const vigil_object_t *function = vigil_callable_object_function(callable);

    return vigil_function_object_arity(function);
}

size_t vigil_callable_object_return_count(const vigil_object_t *callable)
{
    const vigil_object_t *function = vigil_callable_object_function(callable);

    return vigil_function_object_return_count(function);
}

const vigil_chunk_t *vigil_callable_object_chunk(const vigil_object_t *callable)
{
    const vigil_object_t *function = vigil_callable_object_function(callable);

    return vigil_function_object_chunk(function);
}

vigil_status_t vigil_native_function_object_create(vigil_runtime_t *runtime, const char *name, size_t name_length,
                                                   size_t arity, vigil_native_fn_t function,
                                                   vigil_object_t **out_object, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_native_function_object_t *obj;
    void *memory;

    if (out_object == NULL || function == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "native function arguments must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    *out_object = NULL;
    memory = NULL;
    status = vigil_runtime_alloc(runtime, sizeof(*obj), &memory, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    obj = (vigil_native_function_object_t *)memory;
    memset(obj, 0, sizeof(*obj));
    obj->base.runtime = runtime;
    obj->base.type = VIGIL_OBJECT_NATIVE_FUNCTION;
    obj->base.ref_count = 1U;
    vigil_string_init(&obj->name, runtime);
    if (name != NULL && name_length > 0U)
    {
        vigil_string_assign(&obj->name, name, name_length, error);
    }
    obj->arity = arity;
    obj->function = function;
    *out_object = &obj->base;
    return VIGIL_STATUS_OK;
}

vigil_native_fn_t vigil_native_function_get(const vigil_object_t *object)
{
    const vigil_native_function_object_t *native;

    if (object == NULL || object->type != VIGIL_OBJECT_NATIVE_FUNCTION)
    {
        return NULL;
    }
    native = (const vigil_native_function_object_t *)object;
    return native->function;
}
