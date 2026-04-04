/*
 * vm_ops_collection.c — Extracted collection opcode handlers.
 */

#include "vm_ops_collection.h"

/* ── Helper: extract object pointer and validate type ──────────── */

static vigil_object_t *get_object(const vigil_value_t *v)
{
    if (!vigil_nanbox_is_object(*v))
        return NULL;
    return (vigil_object_t *)vigil_nanbox_decode_ptr(*v);
}

static vigil_object_t *require_array(const vigil_value_t *v)
{
    vigil_object_t *o = get_object(v);
    return (o && vigil_object_type(o) == VIGIL_OBJECT_ARRAY) ? o : NULL;
}

static vigil_object_t *require_map(const vigil_value_t *v)
{
    vigil_object_t *o = get_object(v);
    return (o && vigil_object_type(o) == VIGIL_OBJECT_MAP) ? o : NULL;
}

/* ── GET_INDEX ─────────────────────────────────────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_get_index(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;

    frame->ip += 1U;
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    vigil_object_t *obj = get_object(&left);
    if (obj == NULL)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "index access requires an array or map", error);
    }

    if (vigil_object_type(obj) == VIGIL_OBJECT_ARRAY)
    {
        if (!vigil_nanbox_is_int(right) || vigil_value_as_int(&right) < 0)
        {
            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&right);
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "array index must be a non-negative i32",
                                       error);
        }
        if (!vigil_array_object_get(obj, (size_t)vigil_value_as_int(&right), &value))
        {
            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&right);
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "array index is out of range", error);
        }
    }
    else if (vigil_object_type(obj) == VIGIL_OBJECT_MAP)
    {
        if (!vigil_vm_value_is_supported_map_key(&right))
        {
            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&right);
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map index must be i32, bool, or string",
                                       error);
        }
        if (!vigil_map_object_get(obj, &right, &value))
        {
            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&right);
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map key is not present", error);
        }
    }
    else
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "index access requires an array or map", error);
    }

    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&right);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── GET_COLLECTION_SIZE ───────────────────────────────────────── */

vigil_status_t vigil_vm_op_get_collection_size(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, value;

    frame->ip += 1U;
    left = vigil_vm_pop_or_nil(vm);

    vigil_object_t *obj = get_object(&left);
    if (obj == NULL)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "collection size requires an array or map",
                                   error);
    }

    if (vigil_object_type(obj) == VIGIL_OBJECT_ARRAY)
        vigil_value_init_int(&value, (int64_t)vigil_array_object_length(obj));
    else if (vigil_object_type(obj) == VIGIL_OBJECT_MAP)
        vigil_value_init_int(&value, (int64_t)vigil_map_object_count(obj));
    else
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "collection size requires an array or map",
                                   error);
    }

    VIGIL_VM_VALUE_RELEASE(&left);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── ARRAY_PUSH ────────────────────────────────────────────────── */

vigil_status_t vigil_vm_op_array_push(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_value_t left, value;

    frame->ip += 1U;
    value = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    vigil_object_t *arr = require_array(&left);
    if (arr == NULL)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "array push() requires an array receiver", error);
    }
    vigil_status_t status = vigil_array_object_append(arr, &value, error);
    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── ARRAY_POP ─────────────────────────────────────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_array_pop(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;

    frame->ip += 1U;
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    vigil_object_t *arr = require_array(&left);
    if (arr == NULL)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "array pop() requires an array receiver", error);
    }
    if (vigil_array_object_pop(arr, &value))
    {
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_INIT_NIL(&right);
        status = vigil_vm_make_ok_error_value(vm, &right, error);
    }
    else
    {
        status = vigil_vm_make_bounds_error_value(vm, "array is empty", &value, error);
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    if (status != VIGIL_STATUS_OK)
    {
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_RELEASE(&value);
        return status;
    }
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    if (status == VIGIL_STATUS_OK)
        status = vigil_vm_push(vm, &right, error);
    VIGIL_VM_VALUE_RELEASE(&right);
    return status;
}

/* ── ARRAY_GET_SAFE ────────────────────────────────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_array_get_safe(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;

    frame->ip += 1U;
    value = vigil_vm_pop_or_nil(vm);
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    vigil_object_t *arr = require_array(&left);
    if (arr == NULL)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "array get() requires an array receiver", error);
    }
    if (!vigil_nanbox_is_int(right) || vigil_value_as_int(&right) < 0)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "array get() index must be a non-negative i32",
                                   error);
    }
    {
        vigil_value_t item;
        int found;

        VIGIL_VM_VALUE_INIT_NIL(&item);
        found = vigil_array_object_get(arr, (size_t)vigil_value_as_int(&right), &item);
        if (found)
        {
            VIGIL_VM_VALUE_RELEASE(&value);
            value = item;
            status = vigil_vm_make_ok_error_value(vm, &right, error);
        }
        else
        {
            status = vigil_vm_make_bounds_error_value(vm, "array index is out of range", &right, error);
            vigil_value_release(&item);
        }
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    if (status != VIGIL_STATUS_OK)
    {
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_RELEASE(&value);
        return status;
    }
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    if (status == VIGIL_STATUS_OK)
        status = vigil_vm_push(vm, &right, error);
    VIGIL_VM_VALUE_RELEASE(&right);
    return status;
}

/* ── ARRAY_SET_SAFE ────────────────────────────────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_array_set_safe(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;

    frame->ip += 1U;
    value = vigil_vm_pop_or_nil(vm);
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    vigil_object_t *arr = require_array(&left);
    if (arr == NULL)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "array set() requires an array receiver", error);
    }
    if (!vigil_nanbox_is_int(right) || vigil_value_as_int(&right) < 0)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "array set() index must be a non-negative i32",
                                   error);
    }
    status = vigil_array_object_set(arr, (size_t)vigil_value_as_int(&right), &value, error);
    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&right);
    VIGIL_VM_VALUE_RELEASE(&value);
    if (status == VIGIL_STATUS_OK)
        status = vigil_vm_make_ok_error_value(vm, &value, error);
    else if (status == VIGIL_STATUS_INVALID_ARGUMENT)
        status = vigil_vm_make_bounds_error_value(vm, "array index is out of range", &value, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── ARRAY_SLICE ───────────────────────────────────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_array_slice(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;

    frame->ip += 1U;
    value = vigil_vm_pop_or_nil(vm);
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    vigil_object_t *arr = require_array(&left);
    if (arr == NULL)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "array slice() requires an array receiver",
                                   error);
    }
    if (!vigil_nanbox_is_int(right) || !vigil_nanbox_is_int(value) || vigil_value_as_int(&right) < 0 ||
        vigil_value_as_int(&value) < 0)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                   "array slice() start and end must be non-negative i32 values", error);
    }
    vigil_object_t *object = NULL;
    status = vigil_array_object_slice(arr, (size_t)vigil_value_as_int(&right), (size_t)vigil_value_as_int(&value),
                                      &object, error);
    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&right);
    VIGIL_VM_VALUE_RELEASE(&value);
    if (status != VIGIL_STATUS_OK)
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "array slice range is out of bounds", error);
    vigil_value_init_object(&value, &object);
    vigil_object_release(&object);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── ARRAY_CONTAINS ────────────────────────────────────────────── */

vigil_status_t vigil_vm_op_array_contains(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;

    frame->ip += 1U;
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    vigil_object_t *arr = require_array(&left);
    if (arr == NULL)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "array contains() requires an array receiver",
                                   error);
    }
    (value) = vigil_nanbox_from_bool(0);
    {
        size_t item_count = vigil_array_object_length(arr);
        size_t item_index;
        vigil_value_t item;

        for (item_index = 0U; item_index < item_count; item_index += 1U)
        {
            VIGIL_VM_VALUE_INIT_NIL(&item);
            if (!vigil_array_object_get(arr, item_index, &item))
                continue;
            if (vigil_vm_values_equal(&item, &right))
            {
                (value) = vigil_nanbox_from_bool(1);
                vigil_value_release(&item);
                break;
            }
            vigil_value_release(&item);
        }
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&right);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── MAP_GET_SAFE ──────────────────────────────────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_map_get_safe(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;

    frame->ip += 1U;
    value = vigil_vm_pop_or_nil(vm);
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    vigil_object_t *map = require_map(&left);
    if (map == NULL)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map get() requires a map receiver", error);
    }
    {
        vigil_value_t stored;
        int found;

        VIGIL_VM_VALUE_INIT_NIL(&stored);
        found = vigil_map_object_get(map, &right, &stored);
        VIGIL_VM_VALUE_RELEASE(&right);
        (right) = vigil_nanbox_from_bool(found != 0);
        if (found)
        {
            VIGIL_VM_VALUE_RELEASE(&value);
            value = stored;
        }
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    if (status == VIGIL_STATUS_OK)
        status = vigil_vm_push(vm, &right, error);
    VIGIL_VM_VALUE_RELEASE(&right);
    return status;
}

/* ── MAP_SET_SAFE ──────────────────────────────────────────────── */

vigil_status_t vigil_vm_op_map_set_safe(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;

    frame->ip += 1U;
    value = vigil_vm_pop_or_nil(vm);
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    vigil_object_t *map = require_map(&left);
    if (map == NULL)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map set() requires a map receiver", error);
    }
    status = vigil_map_object_set(map, &right, &value, error);
    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&right);
    VIGIL_VM_VALUE_RELEASE(&value);
    if (status != VIGIL_STATUS_OK)
        return status;
    status = vigil_vm_make_ok_error_value(vm, &value, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── MAP_REMOVE_SAFE ───────────────────────────────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_map_remove_safe(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;

    frame->ip += 1U;
    value = vigil_vm_pop_or_nil(vm);
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    vigil_object_t *map = require_map(&left);
    if (map == NULL)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map remove() requires a map receiver", error);
    }
    {
        vigil_value_t removed_value;
        int removed;

        VIGIL_VM_VALUE_INIT_NIL(&removed_value);
        removed = vigil_map_object_remove(map, &right, &removed_value, error);
        VIGIL_VM_VALUE_RELEASE(&right);
        (right) = vigil_nanbox_from_bool(removed != 0);
        if (removed)
        {
            VIGIL_VM_VALUE_RELEASE(&value);
            value = removed_value;
        }
        else
        {
            vigil_value_release(&removed_value);
        }
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    if (status == VIGIL_STATUS_OK)
        status = vigil_vm_push(vm, &right, error);
    VIGIL_VM_VALUE_RELEASE(&right);
    return status;
}

/* ── MAP_HAS ───────────────────────────────────────────────────── */

vigil_status_t vigil_vm_op_map_has(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;

    frame->ip += 1U;
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    vigil_object_t *map = require_map(&left);
    if (map == NULL)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map has() requires a map receiver", error);
    }
    {
        vigil_value_t stored;
        int found;

        VIGIL_VM_VALUE_INIT_NIL(&stored);
        found = vigil_map_object_get(map, &right, &stored);
        vigil_value_release(&stored);
        VIGIL_VM_VALUE_RELEASE(&right);
        (value) = vigil_nanbox_from_bool(found != 0);
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── MAP_KEYS / MAP_VALUES ─────────────────────────────────────── */

vigil_status_t vigil_vm_op_map_keys_values(vigil_vm_t *vm, vigil_vm_frame_t *frame, const uint8_t *code,
                                           vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, value;
    bool want_keys;

    frame->ip += 1U;
    want_keys = ((vigil_opcode_t)code[frame->ip - 1U] == VIGIL_OPCODE_MAP_KEYS);
    left = vigil_vm_pop_or_nil(vm);

    vigil_object_t *map = require_map(&left);
    if (map == NULL)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map method requires a map receiver", error);
    }
    {
        size_t item_count = vigil_map_object_count(map);
        vigil_value_t *items = NULL;
        size_t item_capacity = 0U;
        size_t item_index;
        vigil_object_t *object = NULL;

        status = vigil_vm_grow_value_array(vm->runtime, &items, &item_capacity, item_count, error);
        for (item_index = 0U; status == VIGIL_STATUS_OK && item_index < item_count; item_index += 1U)
        {
            VIGIL_VM_VALUE_INIT_NIL(&items[item_index]);
            bool ok = want_keys ? vigil_map_object_key_at(map, item_index, &items[item_index])
                                : vigil_map_object_value_at(map, item_index, &items[item_index]);
            if (!ok)
                status = VIGIL_STATUS_INTERNAL;
        }
        if (status == VIGIL_STATUS_OK)
            status = vigil_array_object_new(vm->runtime, items, item_count, &object, error);
        for (item_index = 0U; item_index < item_count; item_index += 1U)
            vigil_value_release(&items[item_index]);
        vigil_runtime_free(vm->runtime, (void **)&items);
        VIGIL_VM_VALUE_RELEASE(&left);
        if (status != VIGIL_STATUS_OK)
        {
            if (status == VIGIL_STATUS_INTERNAL)
                return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "failed to enumerate map entries", error);
            return status;
        }
        vigil_value_init_object(&value, &object);
        vigil_object_release(&object);
    }
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── GET_MAP_KEY_AT ────────────────────────────────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_get_map_key_at(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;

    frame->ip += 1U;
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);
    VIGIL_VM_VALUE_INIT_NIL(&value);

    vigil_object_t *map = require_map(&left);
    if (map == NULL)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map iteration requires a map object", error);
    }
    if (!vigil_nanbox_is_int(right) || vigil_value_as_int(&right) < 0)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map iteration index must be a non-negative i32",
                                   error);
    }
    if (!vigil_map_object_key_at(map, (size_t)vigil_value_as_int(&right), &value))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map iteration index is out of range", error);
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&right);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── GET_MAP_VALUE_AT ──────────────────────────────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_get_map_value_at(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;

    frame->ip += 1U;
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);
    VIGIL_VM_VALUE_INIT_NIL(&value);

    vigil_object_t *map = require_map(&left);
    if (map == NULL)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map iteration requires a map object", error);
    }
    if (!vigil_nanbox_is_int(right) || vigil_value_as_int(&right) < 0)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map iteration index must be a non-negative i32",
                                   error);
    }
    if (!vigil_map_object_value_at(map, (size_t)vigil_value_as_int(&right), &value))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map iteration index is out of range", error);
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&right);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── SET_INDEX ─────────────────────────────────────────────────── */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_vm_op_set_index(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;

    frame->ip += 1U;
    value = vigil_vm_pop_or_nil(vm);
    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);

    vigil_object_t *obj = get_object(&left);
    if (obj == NULL)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "indexed assignment requires an array or map",
                                   error);
    }

    if (vigil_object_type(obj) == VIGIL_OBJECT_ARRAY)
    {
        if (!vigil_nanbox_is_int(right) || vigil_value_as_int(&right) < 0)
        {
            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&right);
            VIGIL_VM_VALUE_RELEASE(&value);
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "array index must be a non-negative i32",
                                       error);
        }
        status = vigil_array_object_set(obj, (size_t)vigil_value_as_int(&right), &value, error);
    }
    else if (vigil_object_type(obj) == VIGIL_OBJECT_MAP)
    {
        if (!vigil_vm_value_is_supported_map_key(&right))
        {
            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&right);
            VIGIL_VM_VALUE_RELEASE(&value);
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map index must be i32, bool, or string",
                                       error);
        }
        status = vigil_map_object_set(obj, &right, &value, error);
    }
    else
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "indexed assignment requires an array or map",
                                   error);
    }

    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&right);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

/* ── ARRAY_SORT ────────────────────────────────────────────────── */

vigil_status_t vigil_vm_op_array_sort(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_value_t left;
    frame->ip += 1U;
    left = vigil_vm_pop_or_nil(vm);
    vigil_object_t *arr = get_object(&left);
    if (arr == NULL || vigil_object_type(arr) != VIGIL_OBJECT_ARRAY)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "sort() requires an array receiver", error);
    }
    vigil_array_object_sort(arr, 0);
    VIGIL_VM_VALUE_RELEASE(&left);
    return VIGIL_STATUS_OK;
}

/* ── ARRAY_SORT_DESC ───────────────────────────────────────────── */

vigil_status_t vigil_vm_op_array_sort_desc(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_value_t left;
    frame->ip += 1U;
    left = vigil_vm_pop_or_nil(vm);
    vigil_object_t *arr = get_object(&left);
    if (arr == NULL || vigil_object_type(arr) != VIGIL_OBJECT_ARRAY)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "sort_desc() requires an array receiver", error);
    }
    vigil_array_object_sort(arr, 1);
    VIGIL_VM_VALUE_RELEASE(&left);
    return VIGIL_STATUS_OK;
}

/* ── ARRAY_REVERSE ─────────────────────────────────────────────── */

vigil_status_t vigil_vm_op_array_reverse(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_value_t left;
    frame->ip += 1U;
    left = vigil_vm_pop_or_nil(vm);
    vigil_object_t *arr = get_object(&left);
    if (arr == NULL || vigil_object_type(arr) != VIGIL_OBJECT_ARRAY)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "reverse() requires an array receiver", error);
    }
    vigil_array_object_reverse(arr);
    VIGIL_VM_VALUE_RELEASE(&left);
    return VIGIL_STATUS_OK;
}

/* ── ARRAY_INDEX_OF ────────────────────────────────────────────── */

vigil_status_t vigil_vm_op_array_index_of(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, needle, result;
    frame->ip += 1U;
    needle = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);
    vigil_object_t *arr = get_object(&left);
    if (arr == NULL || vigil_object_type(arr) != VIGIL_OBJECT_ARRAY)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&needle);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "index_of() requires an array receiver", error);
    }
    int idx = vigil_array_object_index_of(arr, &needle);
    vigil_value_init_int(&result, (int64_t)idx);
    VIGIL_VM_VALUE_RELEASE(&left);
    VIGIL_VM_VALUE_RELEASE(&needle);
    status = vigil_vm_push(vm, &result, error);
    VIGIL_VM_VALUE_RELEASE(&result);
    return status;
}

/* ── ARRAY_REMOVE_AT ───────────────────────────────────────────── */

vigil_status_t vigil_vm_op_array_remove_at(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, idx_val, removed, err_val;
    frame->ip += 1U;
    idx_val = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);
    vigil_object_t *arr = get_object(&left);
    if (arr == NULL || vigil_object_type(arr) != VIGIL_OBJECT_ARRAY)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&idx_val);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "remove() requires an array receiver", error);
    }
    int64_t index = vigil_value_as_int(&idx_val);
    VIGIL_VM_VALUE_RELEASE(&idx_val);
    if (index < 0 || (size_t)index >= vigil_array_object_length(arr))
    {
        status = vigil_vm_make_bounds_error_value(vm, "array remove index out of bounds", &removed, error);
    }
    else
    {
        status = vigil_array_object_remove_at(arr, (size_t)index, &removed, error);
        if (status == VIGIL_STATUS_OK)
        {
            VIGIL_VM_VALUE_INIT_NIL(&err_val);
            status = vigil_vm_make_ok_error_value(vm, &err_val, error);
        }
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    if (status != VIGIL_STATUS_OK)
    {
        VIGIL_VM_VALUE_RELEASE(&removed);
        return status;
    }
    status = vigil_vm_push(vm, &removed, error);
    VIGIL_VM_VALUE_RELEASE(&removed);
    if (status == VIGIL_STATUS_OK)
        status = vigil_vm_push(vm, &err_val, error);
    VIGIL_VM_VALUE_RELEASE(&err_val);
    return status;
}

/* ── ARRAY_INSERT_AT ───────────────────────────────────────────── */

vigil_status_t vigil_vm_op_array_insert_at(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, idx_val, elem, err_val;
    frame->ip += 1U;
    elem = vigil_vm_pop_or_nil(vm);
    idx_val = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);
    vigil_object_t *arr = get_object(&left);
    if (arr == NULL || vigil_object_type(arr) != VIGIL_OBJECT_ARRAY)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&idx_val);
        VIGIL_VM_VALUE_RELEASE(&elem);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "insert() requires an array receiver", error);
    }
    int64_t index = vigil_value_as_int(&idx_val);
    VIGIL_VM_VALUE_RELEASE(&idx_val);
    if (index < 0 || (size_t)index > vigil_array_object_length(arr))
    {
        VIGIL_VM_VALUE_RELEASE(&elem);
        status = vigil_vm_make_bounds_error_value(vm, "array insert index out of bounds", &err_val, error);
    }
    else
    {
        status = vigil_array_object_insert_at(arr, (size_t)index, &elem, error);
        VIGIL_VM_VALUE_RELEASE(&elem);
        if (status == VIGIL_STATUS_OK)
            status = vigil_vm_make_ok_error_value(vm, &err_val, error);
    }
    VIGIL_VM_VALUE_RELEASE(&left);
    if (status != VIGIL_STATUS_OK)
    {
        VIGIL_VM_VALUE_RELEASE(&err_val);
        return status;
    }
    status = vigil_vm_push(vm, &err_val, error);
    VIGIL_VM_VALUE_RELEASE(&err_val);
    return status;
}

/* ── ARRAY_CLEAR ───────────────────────────────────────────────── */

vigil_status_t vigil_vm_op_array_clear(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_value_t left;
    frame->ip += 1U;
    left = vigil_vm_pop_or_nil(vm);
    vigil_object_t *arr = get_object(&left);
    if (arr == NULL || vigil_object_type(arr) != VIGIL_OBJECT_ARRAY)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "clear() requires an array receiver", error);
    }
    vigil_array_object_clear(arr);
    VIGIL_VM_VALUE_RELEASE(&left);
    return VIGIL_STATUS_OK;
}

/* ── MAP_CLEAR ─────────────────────────────────────────────────── */

vigil_status_t vigil_vm_op_map_clear(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_value_t left;
    frame->ip += 1U;
    left = vigil_vm_pop_or_nil(vm);
    vigil_object_t *obj = get_object(&left);
    if (obj == NULL || vigil_object_type(obj) != VIGIL_OBJECT_MAP)
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "clear() requires a map receiver", error);
    }
    vigil_map_object_clear(obj);
    VIGIL_VM_VALUE_RELEASE(&left);
    return VIGIL_STATUS_OK;
}
