/* reflect.c — reflection module for runtime type introspection. */

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

/* ── Helpers ─────────────────────────────────────────────────── */

static const vigil_object_t *reflect_current_function(vigil_vm_t *vm)
{
    size_t depth = vigil_vm_frame_depth(vm);
    if (depth == 0U)
        return NULL;
    return vigil_vm_frame_function(vm, depth - 1U);
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

static vigil_status_t push_str_and_ok(vigil_vm_t *vm, const char *text, size_t len, vigil_error_t *error)
{
    vigil_status_t s = push_str(vm, text, len, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t push_str_and_err(vigil_vm_t *vm, const char *msg, vigil_error_t *error)
{
    vigil_status_t s = push_str(vm, "", 0, error);
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

static vigil_status_t push_ok_err(vigil_vm_t *vm, vigil_error_t *error)
{
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
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

/* Get a string argument from the stack at base+idx. */
static const char *get_str_arg(vigil_vm_t *vm, size_t base, size_t idx, size_t *out_len)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    if (!vigil_nanbox_is_object(v))
        return NULL;
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(v);
    if (!obj || vigil_object_type(obj) != VIGIL_OBJECT_STRING)
        return NULL;
    if (out_len)
        *out_len = vigil_string_object_length(obj);
    return vigil_string_object_c_str(obj);
}

/* Convert a value to a string representation into a caller-provided buffer.
   Returns the string pointer (may be buf or a static/object string). */
static const char *value_to_str(vigil_value_t v, char *buf, size_t bufsz, size_t *out_len)
{
    if (vigil_nanbox_is_nil(v))
    {
        *out_len = 3;
        return "nil";
    }
    if (vigil_nanbox_is_bool(v))
    {
        if (vigil_nanbox_decode_bool(v))
        {
            *out_len = 4;
            return "true";
        }
        *out_len = 5;
        return "false";
    }
    if (vigil_nanbox_is_int(v))
    {
        int64_t i = vigil_nanbox_is_int_inline(v) ? vigil_nanbox_decode_int(v) : vigil_value_as_int(&v);
        int n = snprintf(buf, bufsz, "%lld", (long long)i);
        *out_len = (size_t)(n > 0 ? n : 0);
        return buf;
    }
    if (vigil_nanbox_is_uint(v))
    {
        uint64_t u = vigil_nanbox_is_uint_inline(v) ? vigil_nanbox_decode_uint(v) : vigil_value_as_uint(&v);
        int n = snprintf(buf, bufsz, "%llu", (unsigned long long)u);
        *out_len = (size_t)(n > 0 ? n : 0);
        return buf;
    }
    if (vigil_nanbox_is_double(v))
    {
        double d = vigil_nanbox_decode_double(v);
        int n = snprintf(buf, bufsz, "%g", d);
        *out_len = (size_t)(n > 0 ? n : 0);
        return buf;
    }
    if (vigil_nanbox_is_object(v))
    {
        vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(v);
        if (obj && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
        {
            *out_len = vigil_string_object_length(obj);
            return vigil_string_object_c_str(obj);
        }
        *out_len = 6;
        return "object";
    }
    *out_len = 7;
    return "unknown";
}

/* ── Phase 1: Type queries ───────────────────────────────────── */

static vigil_status_t reflect_type_name_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (vigil_nanbox_is_nil(v))
        return push_str(vm, "nil", 3, error);
    if (vigil_nanbox_is_bool(v))
        return push_str(vm, "bool", 4, error);
    if (vigil_nanbox_is_int(v))
        return push_str(vm, "i32", 3, error);
    if (vigil_nanbox_is_uint(v))
        return push_str(vm, "u64", 3, error);
    if (vigil_nanbox_is_double(v))
        return push_str(vm, "f64", 3, error);
    if (vigil_nanbox_is_object(v))
    {
        vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(v);
        if (obj)
        {
            switch (vigil_object_type(obj))
            {
            case VIGIL_OBJECT_STRING:
                return push_str(vm, "string", 6, error);
            case VIGIL_OBJECT_INSTANCE:
            {
                size_t ci = vigil_instance_object_class_index(obj);
                const vigil_object_t *fn = reflect_current_function(vm);
                size_t name_len = 0;
                const char *name = vigil_runtime_class_name(fn, ci, &name_len);
                if (name && name_len > 0)
                    return push_str(vm, name, name_len, error);
                return push_str(vm, "class", 5, error);
            }
            case VIGIL_OBJECT_ARRAY:
                return push_str(vm, "array", 5, error);
            case VIGIL_OBJECT_MAP:
                return push_str(vm, "map", 3, error);
            case VIGIL_OBJECT_FUNCTION:
                return push_str(vm, "function", 8, error);
            case VIGIL_OBJECT_CLOSURE:
                return push_str(vm, "function", 8, error);
            case VIGIL_OBJECT_ERROR:
                return push_str(vm, "error", 5, error);
            default:
                break;
            }
        }
    }
    return push_str(vm, "unknown", 7, error);
}

static vigil_status_t reflect_type_kind_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (vigil_nanbox_is_nil(v))
        return push_str(vm, "nil", 3, error);
    if (vigil_nanbox_is_bool(v))
        return push_str(vm, "bool", 4, error);
    if (vigil_nanbox_is_int(v))
        return push_str(vm, "i32", 3, error);
    if (vigil_nanbox_is_uint(v))
        return push_str(vm, "u64", 3, error);
    if (vigil_nanbox_is_double(v))
        return push_str(vm, "f64", 3, error);
    if (vigil_nanbox_is_object(v))
    {
        vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(v);
        if (obj)
        {
            switch (vigil_object_type(obj))
            {
            case VIGIL_OBJECT_STRING:
                return push_str(vm, "string", 6, error);
            case VIGIL_OBJECT_INSTANCE:
                return push_str(vm, "class", 5, error);
            case VIGIL_OBJECT_ARRAY:
                return push_str(vm, "array", 5, error);
            case VIGIL_OBJECT_MAP:
                return push_str(vm, "map", 3, error);
            case VIGIL_OBJECT_FUNCTION:
            case VIGIL_OBJECT_CLOSURE:
            case VIGIL_OBJECT_NATIVE_FUNCTION:
                return push_str(vm, "function", 8, error);
            default:
                break;
            }
        }
    }
    return push_str(vm, "nil", 3, error);
}

static vigil_status_t reflect_is_nil_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_value_t result;
    vigil_value_init_bool(&result, vigil_nanbox_is_nil(v));
    return vigil_vm_stack_push(vm, &result, error);
}

static vigil_status_t reflect_is_numeric_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    int numeric = vigil_nanbox_is_int(v) || vigil_nanbox_is_uint(v) || vigil_nanbox_is_double(v);
    vigil_value_t result;
    vigil_value_init_bool(&result, numeric);
    return vigil_vm_stack_push(vm, &result, error);
}

/* ── Phase 2: Field inspection ───────────────────────────────── */

/* Helper: check if value is an instance and return its object pointer. */
static vigil_object_t *as_instance(vigil_value_t v)
{
    if (!vigil_nanbox_is_object(v))
        return NULL;
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(v);
    if (!obj || vigil_object_type(obj) != VIGIL_OBJECT_INSTANCE)
        return NULL;
    return obj;
}

static vigil_status_t reflect_fields_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_object_t *inst = as_instance(v);
    if (!inst)
    {
        /* Return empty array for non-instance values. */
        vigil_object_t *arr = NULL;
        vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_value_t val;
        vigil_value_init_object(&val, &arr);
        s = vigil_vm_stack_push(vm, &val, error);
        vigil_value_release(&val);
        return s;
    }

    size_t ci = vigil_instance_object_class_index(inst);
    const vigil_object_t *fn = reflect_current_function(vm);
    size_t fc = vigil_function_object_class_field_count(fn, ci);

    vigil_object_t *arr = NULL;
    vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
    if (s != VIGIL_STATUS_OK)
        return s;

    for (size_t i = 0; i < fc; i++)
    {
        const char *name = NULL;
        size_t name_len = 0;
        if (vigil_function_object_get_class_field(fn, ci, i, &name, &name_len, NULL, NULL) && name)
        {
            vigil_object_t *str_obj = NULL;
            s = vigil_string_object_new(vigil_vm_runtime(vm), name, name_len, &str_obj, error);
            if (s != VIGIL_STATUS_OK)
            {
                vigil_object_release(&arr);
                return s;
            }
            vigil_value_t sv;
            vigil_value_init_object(&sv, &str_obj);
            s = vigil_array_object_append(arr, &sv, error);
            vigil_value_release(&sv);
            if (s != VIGIL_STATUS_OK)
            {
                vigil_object_release(&arr);
                return s;
            }
        }
    }

    vigil_value_t val;
    vigil_value_init_object(&val, &arr);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return s;
}

static vigil_status_t reflect_field_count_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_object_t *inst = as_instance(v);
    vigil_value_t result;
    if (!inst)
    {
        vigil_value_init_int(&result, 0);
    }
    else
    {
        vigil_value_init_int(&result, (int64_t)vigil_instance_object_field_count(inst));
    }
    return vigil_vm_stack_push(vm, &result, error);
}

/* Find field index by name. Returns -1 if not found. */
static int find_field_index(const vigil_object_t *fn, size_t ci, const char *name, size_t name_len)
{
    size_t fc = vigil_function_object_class_field_count(fn, ci);
    for (size_t i = 0; i < fc; i++)
    {
        const char *fn_name = NULL;
        size_t fn_len = 0;
        if (vigil_function_object_get_class_field(fn, ci, i, &fn_name, &fn_len, NULL, NULL))
        {
            if (fn_len == name_len && memcmp(fn_name, name, name_len) == 0)
                return (int)i;
        }
    }
    return -1;
}

static vigil_status_t reflect_get_field_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    size_t name_len = 0;
    const char *name = get_str_arg(vm, base, 1, &name_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_object_t *inst = as_instance(v);
    if (!inst || !name)
        return push_str_and_err(vm, "not an instance or invalid name", error);

    size_t ci = vigil_instance_object_class_index(inst);
    const vigil_object_t *fn = reflect_current_function(vm);
    int fi = find_field_index(fn, ci, name, name_len);
    if (fi < 0)
        return push_str_and_err(vm, "field not found", error);

    vigil_value_t fv;
    if (!vigil_instance_object_get_field(inst, (size_t)fi, &fv))
        return push_str_and_err(vm, "cannot read field", error);

    char buf[64];
    size_t slen = 0;
    const char *s = value_to_str(fv, buf, sizeof(buf), &slen);
    vigil_value_release(&fv);
    return push_str_and_ok(vm, s, slen, error);
}

static vigil_status_t reflect_set_field_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    size_t name_len = 0;
    const char *name = get_str_arg(vm, base, 1, &name_len);
    size_t val_len = 0;
    const char *val_str = get_str_arg(vm, base, 2, &val_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_object_t *inst = as_instance(v);
    if (!inst || !name || !val_str)
        return push_err_only(vm, "not an instance or invalid args", error);

    size_t ci = vigil_instance_object_class_index(inst);
    const vigil_object_t *fn = reflect_current_function(vm);
    int fi = find_field_index(fn, ci, name, name_len);
    if (fi < 0)
        return push_err_only(vm, "field not found", error);

    vigil_runtime_resolved_type_t ftype;
    vigil_function_object_get_class_field(fn, ci, (size_t)fi, NULL, NULL, &ftype, NULL);

    vigil_value_t new_val;
    switch (ftype.kind)
    {
    case VIGIL_TYPE_I32:
    case VIGIL_TYPE_I64:
    {
        char *end = NULL;
        long long parsed = strtoll(val_str, &end, 10);
        if (end == val_str || *end != '\0')
            return push_err_only(vm, "invalid integer", error);
        vigil_value_init_int(&new_val, (int64_t)parsed);
        break;
    }
    case VIGIL_TYPE_F64:
    {
        char *end = NULL;
        double parsed = strtod(val_str, &end);
        if (end == val_str || *end != '\0')
            return push_err_only(vm, "invalid float", error);
        vigil_value_init_float(&new_val, parsed);
        break;
    }
    case VIGIL_TYPE_BOOL:
        vigil_value_init_bool(&new_val, strcmp(val_str, "true") == 0);
        break;
    case VIGIL_TYPE_STRING:
    {
        vigil_object_t *str_obj = NULL;
        vigil_status_t s = vigil_string_object_new(vigil_vm_runtime(vm), val_str, val_len, &str_obj, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_value_init_object(&new_val, &str_obj);
        break;
    }
    default:
        return push_err_only(vm, "unsupported field type for set_field", error);
    }

    vigil_status_t s = vigil_instance_object_set_field(inst, (size_t)fi, &new_val, error);
    vigil_value_release(&new_val);
    if (s != VIGIL_STATUS_OK)
        return s;
    return push_ok_err(vm, error);
}

static vigil_status_t reflect_field_type_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    size_t name_len = 0;
    const char *name = get_str_arg(vm, base, 1, &name_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_object_t *inst = as_instance(v);
    if (!inst || !name)
        return push_str(vm, "unknown", 7, error);

    size_t ci = vigil_instance_object_class_index(inst);
    const vigil_object_t *fn = reflect_current_function(vm);
    int fi = find_field_index(fn, ci, name, name_len);
    if (fi < 0)
        return push_str(vm, "unknown", 7, error);

    vigil_runtime_resolved_type_t ftype;
    vigil_function_object_get_class_field(fn, ci, (size_t)fi, NULL, NULL, &ftype, NULL);

    switch (ftype.kind)
    {
    case VIGIL_TYPE_I32:
        return push_str(vm, "i32", 3, error);
    case VIGIL_TYPE_I64:
        return push_str(vm, "i64", 3, error);
    case VIGIL_TYPE_U8:
        return push_str(vm, "u8", 2, error);
    case VIGIL_TYPE_U32:
        return push_str(vm, "u32", 3, error);
    case VIGIL_TYPE_U64:
        return push_str(vm, "u64", 3, error);
    case VIGIL_TYPE_F64:
        return push_str(vm, "f64", 3, error);
    case VIGIL_TYPE_BOOL:
        return push_str(vm, "bool", 4, error);
    case VIGIL_TYPE_STRING:
        return push_str(vm, "string", 6, error);
    case VIGIL_TYPE_OBJECT:
        return push_str(vm, "object", 6, error);
    default:
        return push_str(vm, "unknown", 7, error);
    }
}

static vigil_status_t reflect_has_field_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    size_t name_len = 0;
    const char *name = get_str_arg(vm, base, 1, &name_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_object_t *inst = as_instance(v);
    vigil_value_t result;
    if (!inst || !name)
    {
        vigil_value_init_bool(&result, 0);
        return vigil_vm_stack_push(vm, &result, error);
    }

    size_t ci = vigil_instance_object_class_index(inst);
    const vigil_object_t *fn = reflect_current_function(vm);
    int fi = find_field_index(fn, ci, name, name_len);
    vigil_value_init_bool(&result, fi >= 0);
    return vigil_vm_stack_push(vm, &result, error);
}

/* ── Phase 3: Method inspection ──────────────────────────────── */

static vigil_status_t reflect_methods_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_object_t *inst = as_instance(v);
    if (!inst)
    {
        vigil_object_t *arr = NULL;
        vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_value_t val;
        vigil_value_init_object(&val, &arr);
        s = vigil_vm_stack_push(vm, &val, error);
        vigil_value_release(&val);
        return s;
    }

    size_t ci = vigil_instance_object_class_index(inst);
    const vigil_object_t *fn = reflect_current_function(vm);
    size_t mc = vigil_function_object_class_method_count(fn, ci);

    vigil_object_t *arr = NULL;
    vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
    if (s != VIGIL_STATUS_OK)
        return s;

    for (size_t i = 0; i < mc; i++)
    {
        const char *name = NULL;
        size_t name_len = 0;
        if (vigil_function_object_get_class_method(fn, ci, i, &name, &name_len, NULL, NULL) && name)
        {
            vigil_object_t *str_obj = NULL;
            s = vigil_string_object_new(vigil_vm_runtime(vm), name, name_len, &str_obj, error);
            if (s != VIGIL_STATUS_OK)
            {
                vigil_object_release(&arr);
                return s;
            }
            vigil_value_t sv;
            vigil_value_init_object(&sv, &str_obj);
            s = vigil_array_object_append(arr, &sv, error);
            vigil_value_release(&sv);
            if (s != VIGIL_STATUS_OK)
            {
                vigil_object_release(&arr);
                return s;
            }
        }
    }

    vigil_value_t val;
    vigil_value_init_object(&val, &arr);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return s;
}

static vigil_status_t reflect_method_count_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_object_t *inst = as_instance(v);
    vigil_value_t result;
    if (!inst)
    {
        vigil_value_init_int(&result, 0);
    }
    else
    {
        size_t ci = vigil_instance_object_class_index(inst);
        const vigil_object_t *fn = reflect_current_function(vm);
        vigil_value_init_int(&result, (int64_t)vigil_function_object_class_method_count(fn, ci));
    }
    return vigil_vm_stack_push(vm, &result, error);
}

static vigil_status_t reflect_has_method_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    size_t name_len = 0;
    const char *name = get_str_arg(vm, base, 1, &name_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_object_t *inst = as_instance(v);
    vigil_value_t result;
    if (!inst || !name)
    {
        vigil_value_init_bool(&result, 0);
        return vigil_vm_stack_push(vm, &result, error);
    }

    size_t ci = vigil_instance_object_class_index(inst);
    const vigil_object_t *fn = reflect_current_function(vm);
    size_t mc = vigil_function_object_class_method_count(fn, ci);
    int found = 0;
    for (size_t i = 0; i < mc; i++)
    {
        const char *mname = NULL;
        size_t mlen = 0;
        if (vigil_function_object_get_class_method(fn, ci, i, &mname, &mlen, NULL, NULL))
        {
            if (mlen == name_len && memcmp(mname, name, name_len) == 0)
            {
                found = 1;
                break;
            }
        }
    }
    vigil_value_init_bool(&result, found);
    return vigil_vm_stack_push(vm, &result, error);
}

/* ── Phase 4: Enum reflection ────────────────────────────────── */

static vigil_status_t reflect_enum_members_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t name_len = 0;
    const char *name = get_str_arg(vm, base, 0, &name_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    const vigil_object_t *fn = reflect_current_function(vm);
    if (!fn || !name)
    {
        /* Return empty array + error. */
        vigil_object_t *arr = NULL;
        vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_value_t val;
        vigil_value_init_object(&val, &arr);
        s = vigil_vm_stack_push(vm, &val, error);
        vigil_value_release(&val);
        if (s != VIGIL_STATUS_OK)
            return s;
        return push_err_only(vm, "enum not found", error);
    }

    size_t ec = vigil_function_object_enum_count(fn);
    for (size_t i = 0; i < ec; i++)
    {
        const char *ename = NULL;
        size_t elen = 0;
        size_t mcount = 0;
        if (vigil_function_object_get_enum(fn, i, &ename, &elen, &mcount))
        {
            if (elen == name_len && memcmp(ename, name, name_len) == 0)
            {
                /* Found the enum — build array of member names. */
                vigil_object_t *arr = NULL;
                vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
                if (s != VIGIL_STATUS_OK)
                    return s;
                for (size_t mi = 0; mi < mcount; mi++)
                {
                    const char *mname = NULL;
                    size_t mlen = 0;
                    if (vigil_function_object_get_enum_member(fn, i, mi, &mname, &mlen, NULL) && mname)
                    {
                        vigil_object_t *str_obj = NULL;
                        s = vigil_string_object_new(vigil_vm_runtime(vm), mname, mlen, &str_obj, error);
                        if (s != VIGIL_STATUS_OK)
                        {
                            vigil_object_release(&arr);
                            return s;
                        }
                        vigil_value_t sv;
                        vigil_value_init_object(&sv, &str_obj);
                        s = vigil_array_object_append(arr, &sv, error);
                        vigil_value_release(&sv);
                        if (s != VIGIL_STATUS_OK)
                        {
                            vigil_object_release(&arr);
                            return s;
                        }
                    }
                }
                vigil_value_t val;
                vigil_value_init_object(&val, &arr);
                s = vigil_vm_stack_push(vm, &val, error);
                vigil_value_release(&val);
                if (s != VIGIL_STATUS_OK)
                    return s;
                return push_ok_err(vm, error);
            }
        }
    }

    /* Enum not found. */
    vigil_object_t *arr = NULL;
    vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t val;
    vigil_value_init_object(&val, &arr);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    if (s != VIGIL_STATUS_OK)
        return s;
    return push_err_only(vm, "enum not found", error);
}

static vigil_status_t reflect_enum_value_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t enum_name_len = 0;
    const char *enum_name = get_str_arg(vm, base, 0, &enum_name_len);
    size_t member_name_len = 0;
    const char *member_name = get_str_arg(vm, base, 1, &member_name_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    const vigil_object_t *fn = reflect_current_function(vm);
    if (!fn || !enum_name || !member_name)
    {
        vigil_value_t zero;
        vigil_value_init_int(&zero, 0);
        vigil_status_t s = vigil_vm_stack_push(vm, &zero, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        return push_err_only(vm, "enum or member not found", error);
    }

    size_t ec = vigil_function_object_enum_count(fn);
    for (size_t i = 0; i < ec; i++)
    {
        const char *ename = NULL;
        size_t elen = 0;
        size_t mcount = 0;
        if (vigil_function_object_get_enum(fn, i, &ename, &elen, &mcount))
        {
            if (elen == enum_name_len && memcmp(ename, enum_name, enum_name_len) == 0)
            {
                for (size_t mi = 0; mi < mcount; mi++)
                {
                    const char *mname = NULL;
                    size_t mlen = 0;
                    int64_t mval = 0;
                    if (vigil_function_object_get_enum_member(fn, i, mi, &mname, &mlen, &mval))
                    {
                        if (mlen == member_name_len && memcmp(mname, member_name, member_name_len) == 0)
                        {
                            vigil_value_t result;
                            vigil_value_init_int(&result, mval);
                            vigil_status_t s = vigil_vm_stack_push(vm, &result, error);
                            if (s != VIGIL_STATUS_OK)
                                return s;
                            return push_ok_err(vm, error);
                        }
                    }
                }
                /* Member not found in this enum. */
                break;
            }
        }
    }

    vigil_value_t zero;
    vigil_value_init_int(&zero, 0);
    vigil_status_t s = vigil_vm_stack_push(vm, &zero, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return push_err_only(vm, "enum or member not found", error);
}

/* ── Module definition ───────────────────────────────────────── */

static const int obj_param[] = {VIGIL_TYPE_OBJECT};
static const int obj_str_params[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_STRING};
static const int obj_str_str_params[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int str_param[] = {VIGIL_TYPE_STRING};
static const int str_str_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};

static const int str_err_returns[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_ERR};
static const int err_returns[] = {VIGIL_TYPE_ERR};
static const int i64_err_returns[] = {VIGIL_TYPE_I64, VIGIL_TYPE_ERR};
static const int arr_err_returns[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_ERR};

static const vigil_native_type_t array_string_ret = VIGIL_NATIVE_TYPE_ARRAY(VIGIL_TYPE_STRING);

static const char *const val_param_names[] = {"value"};
static const char *const val_name_param_names[] = {"value", "name"};
static const char *const val_name_val_param_names[] = {"value", "name", "new_value"};
static const char *const enum_name_param_names[] = {"enum_name"};
static const char *const enum_member_param_names[] = {"enum_name", "member_name"};

static const vigil_native_symbol_doc_t reflect_module_doc = {
    "Runtime reflection and type introspection.",
    "Query types, inspect fields, and modify instance values at runtime.",
    NULL,
};

static const vigil_native_symbol_doc_t doc_type_name = {
    "Get the type name of a value.",
    "Returns the type name: \"i32\", \"f64\", \"bool\", \"string\", or the class/struct name for instances.",
    "string name = reflect.type_name(value)",
};
static const vigil_native_symbol_doc_t doc_type_kind = {
    "Get the type kind of a value.",
    "Returns the kind: \"i32\", \"i64\", \"f64\", \"bool\", \"string\", \"nil\", \"array\", \"map\", \"class\", or \"function\".",
    "string kind = reflect.type_kind(value)",
};
static const vigil_native_symbol_doc_t doc_is_nil = {
    "Check if a value is nil.",
    "Returns true if the value is nil.",
    "bool n = reflect.is_nil(value)",
};
static const vigil_native_symbol_doc_t doc_is_numeric = {
    "Check if a value is numeric.",
    "Returns true if the value is i32, i64, or f64.",
    "bool n = reflect.is_numeric(value)",
};
static const vigil_native_symbol_doc_t doc_fields = {
    "Get field names of an instance.",
    "Returns an array of field name strings for a struct or class instance.",
    "array<string> names = reflect.fields(instance)",
};
static const vigil_native_symbol_doc_t doc_field_count = {
    "Get the number of fields.",
    "Returns the field count for a struct or class instance, or 0 for non-instances.",
    "i32 count = reflect.field_count(instance)",
};
static const vigil_native_symbol_doc_t doc_get_field = {
    "Read a field value by name.",
    "Returns the field value as a string representation. Errors if the field does not exist.",
    "string val, err e = reflect.get_field(instance, \"name\")",
};
static const vigil_native_symbol_doc_t doc_set_field = {
    "Set a field value by name.",
    "Parses the string value according to the field's declared type and sets it.",
    "err e = reflect.set_field(instance, \"age\", \"30\")",
};
static const vigil_native_symbol_doc_t doc_field_type = {
    "Get the declared type of a field.",
    "Returns the type name of the field: \"string\", \"i32\", \"f64\", \"bool\", etc.",
    "string t = reflect.field_type(instance, \"name\")",
};
static const vigil_native_symbol_doc_t doc_has_field = {
    "Check if a field exists.",
    "Returns true if the instance has a field with the given name.",
    "bool has = reflect.has_field(instance, \"name\")",
};
static const vigil_native_symbol_doc_t doc_methods = {
    "Get method names of an instance.",
    "Returns an array of method name strings for a class instance.",
    "array<string> names = reflect.methods(instance)",
};
static const vigil_native_symbol_doc_t doc_method_count = {
    "Get the number of methods.",
    "Returns the method count for a class instance, or 0 for non-instances.",
    "i32 count = reflect.method_count(instance)",
};
static const vigil_native_symbol_doc_t doc_has_method = {
    "Check if a method exists.",
    "Returns true if the instance has a method with the given name.",
    "bool has = reflect.has_method(instance, \"name\")",
};
static const vigil_native_symbol_doc_t doc_enum_members = {
    "Get member names of an enum.",
    "Returns an array of member name strings for the named enum type.",
    "array<string> members, err e = reflect.enum_members(\"Color\")",
};
static const vigil_native_symbol_doc_t doc_enum_value = {
    "Get the value of an enum member.",
    "Returns the integer value of the named member in the named enum.",
    "i64 val, err e = reflect.enum_value(\"Color\", \"Red\")",
};

static const vigil_native_module_function_t reflect_funcs[] = {
    /* Phase 1: type queries */
    {"type_name", 9U, reflect_type_name_fn, 1U, obj_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U,
     val_param_names, NULL, NULL, &doc_type_name},
    {"type_kind", 9U, reflect_type_kind_fn, 1U, obj_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U,
     val_param_names, NULL, NULL, &doc_type_kind},
    {"is_nil", 6U, reflect_is_nil_fn, 1U, obj_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U,
     val_param_names, NULL, NULL, &doc_is_nil},
    {"is_numeric", 10U, reflect_is_numeric_fn, 1U, obj_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U,
     val_param_names, NULL, NULL, &doc_is_numeric},
    /* Phase 2: field inspection */
    {"fields", 6U, reflect_fields_fn, 1U, obj_param, VIGIL_TYPE_OBJECT, 1U, NULL, VIGIL_TYPE_STRING, NULL,
     &array_string_ret, 0U, val_param_names, NULL, NULL, &doc_fields},
    {"field_count", 11U, reflect_field_count_fn, 1U, obj_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     val_param_names, NULL, NULL, &doc_field_count},
    {"get_field", 9U, reflect_get_field_fn, 2U, obj_str_params, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL,
     0U, val_name_param_names, NULL, NULL, &doc_get_field},
    {"set_field", 9U, reflect_set_field_fn, 3U, obj_str_str_params, VIGIL_TYPE_ERR, 1U, err_returns, 0, NULL, NULL,
     0U, val_name_val_param_names, NULL, NULL, &doc_set_field},
    {"field_type", 10U, reflect_field_type_fn, 2U, obj_str_params, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U,
     val_name_param_names, NULL, NULL, &doc_field_type},
    {"has_field", 9U, reflect_has_field_fn, 2U, obj_str_params, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U,
     val_name_param_names, NULL, NULL, &doc_has_field},
    /* Phase 3: method inspection */
    {"methods", 7U, reflect_methods_fn, 1U, obj_param, VIGIL_TYPE_OBJECT, 1U, NULL, VIGIL_TYPE_STRING, NULL,
     &array_string_ret, 0U, val_param_names, NULL, NULL, &doc_methods},
    {"method_count", 12U, reflect_method_count_fn, 1U, obj_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     val_param_names, NULL, NULL, &doc_method_count},
    {"has_method", 10U, reflect_has_method_fn, 2U, obj_str_params, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U,
     val_name_param_names, NULL, NULL, &doc_has_method},
    /* Phase 4: enum reflection */
    {"enum_members", 12U, reflect_enum_members_fn, 1U, str_param, VIGIL_TYPE_OBJECT, 2U, arr_err_returns,
     VIGIL_TYPE_STRING, NULL, &array_string_ret, 0U, enum_name_param_names, NULL, NULL, &doc_enum_members},
    {"enum_value", 10U, reflect_enum_value_fn, 2U, str_str_params, VIGIL_TYPE_I64, 2U, i64_err_returns, 0, NULL, NULL,
     0U, enum_member_param_names, NULL, NULL, &doc_enum_value},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_reflect = {
    "reflect", 7U, reflect_funcs, sizeof(reflect_funcs) / sizeof(reflect_funcs[0]), NULL, 0U, &reflect_module_doc,
    NULL, 0U};
