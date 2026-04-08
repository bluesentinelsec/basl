/*
 * vm.c — VIGIL bytecode virtual machine
 *
 * ═══════════════════════════════════════════════════════════════════
 *  REGISTER-BASED VM
 * ═══════════════════════════════════════════════════════════════════
 *
 * All bytecode execution goes through the register VM (regvm.c).
 * Stack bytecode emitted by the compiler is translated to fixed-width
 * register instructions on first call, cached on the chunk, and
 * executed via the register VM dispatch loop with computed-goto
 * dispatch (GCC/Clang) or a switch fallback (MSVC).
 *
 * This file contains:
 *   - VM lifecycle (open, close, stack/frame management)
 *   - Shared runtime helpers (checked arithmetic, string ops, etc.)
 *   - The entry points vigil_vm_execute / vigil_vm_execute_function
 *     which translate and dispatch to the register VM
 *   - vigil_vm_execute_call for sub-calls from within the register VM
 *
 * ═══════════════════════════════════════════════════════════════════
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_aot.h"
#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"
#include "internal/vigil_regvm.h"
#include "internal/vigil_vm_internal.h"
#include "platform/platform.h"
#include "value_internal.h"
#include "vigil/string.h"
#include "vigil/vm.h"
#include "vm_ops_collection.h"
#include "vm_ops_convert.h"
#include "vm_ops_string.h"

/* i32 overflow macros now defined in vigil_vm_internal.h */

#define VIGIL_VM_INITIAL_STACK_CAPACITY 256U
#define VIGIL_VM_INITIAL_FRAME_CAPACITY 64U

vigil_status_t vigil_vm_fail_at_ip(vigil_vm_t *vm, vigil_status_t status, const char *message, vigil_error_t *error);
vigil_value_t vigil_vm_pop_or_nil(vigil_vm_t *vm);
static void vigil_vm_defer_action_clear(vigil_runtime_t *runtime, vigil_vm_defer_action_t *action);

static vigil_status_t vigil_vm_validate(const vigil_vm_t *vm, vigil_error_t *error)
{
    vigil_error_clear(error);

    if (vm == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "vm must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (vm->runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "vm runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return VIGIL_STATUS_OK;
}

#if defined(VIGIL_ENABLE_AOT)
static int vigil_vm_env_disables_aot(void)
{
    const char *value = getenv("VIGIL_NO_AOT");

    return value != NULL && value[0] != '\0' && !(value[0] == '0' && value[1] == '\0');
}
#endif

static int vigil_vm_default_aot_enabled(void)
{
#if defined(VIGIL_ENABLE_AOT)
    return !vigil_vm_env_disables_aot();
#else
    return 0;
#endif
}

static void vigil_vm_release_stack(vigil_vm_t *vm)
{
    size_t i;

    if (vm == NULL)
    {
        return;
    }

    for (i = 0U; i < vm->stack_capacity; ++i)
    {
        vigil_value_release(&vm->stack[i]);
    }

    vm->stack_count = 0U;
}

static void vigil_vm_release_value_range(vigil_value_t *values, size_t count)
{
    size_t i;

    if (values == NULL)
    {
        return;
    }

    for (i = 0U; i < count; ++i)
    {
        vigil_value_release(&values[i]);
    }
}

static void vigil_vm_defer_action_clear(vigil_runtime_t *runtime, vigil_vm_defer_action_t *action)
{
    size_t i;
    void *memory;

    if (action == NULL)
    {
        return;
    }

    for (i = 0U; i < action->value_count; i += 1U)
    {
        vigil_value_release(&action->values[i]);
    }
    memory = action->values;
    if (runtime != NULL)
    {
        vigil_runtime_free(runtime, &memory);
    }
    memset(action, 0, sizeof(*action));
}

vigil_status_t vigil_vm_grow_value_array(vigil_runtime_t *runtime, vigil_value_t **values, size_t *capacity,
                                         size_t minimum_capacity, vigil_error_t *error)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= *capacity)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    old_capacity = *capacity;
    next_capacity = old_capacity == 0U ? 2U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > SIZE_MAX / 2U)
        {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }
    if (next_capacity > SIZE_MAX / sizeof(**values))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "vm value array overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = *values;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(runtime, next_capacity * sizeof(**values), &memory, error);
    }
    else
    {
        status = vigil_runtime_realloc(runtime, &memory, next_capacity * sizeof(**values), error);
    }
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    *values = (vigil_value_t *)memory;
    *capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static void vigil_vm_frame_clear(vigil_runtime_t *runtime, vigil_vm_frame_t *frame)
{
    size_t i;
    void *memory;

    if (frame == NULL)
    {
        return;
    }

    for (i = 0U; i < frame->defer_count; i += 1U)
    {
        vigil_vm_defer_action_clear(runtime, &frame->defers[i]);
    }
    memory = frame->defers;
    if (runtime != NULL)
    {
        vigil_runtime_free(runtime, &memory);
    }
    for (i = 0U; i < frame->pending_return_count; i += 1U)
    {
        vigil_value_release(&frame->pending_returns[i]);
    }
    memory = frame->pending_returns;
    if (runtime != NULL)
    {
        vigil_runtime_free(runtime, &memory);
    }
    memset(frame, 0, sizeof(*frame));
}

static void vigil_vm_clear_frames(vigil_vm_t *vm)
{
    size_t i;

    if (vm == NULL)
    {
        return;
    }

    for (i = 0U; i < vm->frame_count; i += 1U)
    {
        vigil_vm_frame_clear(vm->runtime, &vm->frames[i]);
    }
    vm->frame_count = 0U;
}

static void vigil_vm_pop_frame(vigil_vm_t *vm)
{
    vigil_vm_frame_clear(vm->runtime, &vm->frames[vm->frame_count - 1U]);
    vm->frame_count -= 1U;
}

vigil_status_t vigil_vm_grow_stack(vigil_vm_t *vm, size_t minimum_capacity, vigil_error_t *error)
{
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    vigil_status_t status;

    if (minimum_capacity <= vm->stack_capacity)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    old_capacity = vm->stack_capacity;
    next_capacity = old_capacity == 0U ? 16U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > (SIZE_MAX / 2U))
        {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > (SIZE_MAX / sizeof(*vm->stack)))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "vm stack allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    if (vm->stack == NULL)
    {
        memory = NULL;
        status = vigil_runtime_alloc(vm->runtime, next_capacity * sizeof(*vm->stack), &memory, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        memset(memory, 0, next_capacity * sizeof(*vm->stack));
    }
    else
    {
        memory = vm->stack;
        status = vigil_runtime_realloc(vm->runtime, &memory, next_capacity * sizeof(*vm->stack), error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        memset((vigil_value_t *)memory + old_capacity, 0, (next_capacity - old_capacity) * sizeof(*vm->stack));
    }

    vm->stack = (vigil_value_t *)memory;
    vm->stack_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_grow_frames(vigil_vm_t *vm, size_t minimum_capacity, vigil_error_t *error)
{
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    vigil_status_t status;

    if (minimum_capacity <= vm->frame_capacity)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    old_capacity = vm->frame_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > (SIZE_MAX / 2U))
        {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > (SIZE_MAX / sizeof(*vm->frames)))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "vm frame allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    if (vm->frames == NULL)
    {
        memory = NULL;
        status = vigil_runtime_alloc(vm->runtime, next_capacity * sizeof(*vm->frames), &memory, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }
    else
    {
        memory = vm->frames;
        status = vigil_runtime_realloc(vm->runtime, &memory, next_capacity * sizeof(*vm->frames), error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        memset((vigil_vm_frame_t *)memory + old_capacity, 0, (next_capacity - old_capacity) * sizeof(*vm->frames));
    }

    vm->frames = (vigil_vm_frame_t *)memory;
    vm->frame_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static vigil_vm_frame_t *vigil_vm_current_frame(vigil_vm_t *vm)
{
    if (vm == NULL || vm->frame_count == 0U)
    {
        return NULL;
    }

    return &vm->frames[vm->frame_count - 1U];
}

static vigil_status_t vigil_vm_push_frame(vigil_vm_t *vm, const vigil_object_t *callable,
                                          const vigil_object_t *function, const vigil_chunk_t *chunk, size_t base_slot,
                                          vigil_error_t *error)
{
    vigil_status_t status;
    vigil_vm_frame_t *frame;

    if (vm->frame_count == SIZE_MAX)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "vm frame stack overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    status = vigil_vm_grow_frames(vm, vm->frame_count + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    frame = &vm->frames[vm->frame_count];
    memset(frame, 0, sizeof(*frame));
    frame->callable = callable;
    frame->function = function;
    frame->chunk = chunk;
    frame->ip = 0U;
    frame->base_slot = base_slot;
    vm->frame_count += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_push(vigil_vm_t *vm, const vigil_value_t *value, vigil_error_t *error)
{
    vigil_status_t status;

    if (vm->stack_count == SIZE_MAX)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "vm stack overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    status = vigil_vm_grow_stack(vm, vm->stack_count + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vigil_value_release(&vm->stack[vm->stack_count]);
    vm->stack[vm->stack_count] = vigil_value_copy(value);
    vm->stack_count += 1U;
    return VIGIL_STATUS_OK;
}

vigil_value_t vigil_vm_pop_or_nil(vigil_vm_t *vm)
{
    vigil_value_t value;

    VIGIL_VM_VALUE_INIT_NIL(&value);
    if (vm == NULL || vm->stack_count == 0U)
    {
        return value;
    }

    value = vm->stack[vm->stack_count - 1U];
    VIGIL_VM_VALUE_INIT_NIL(&vm->stack[vm->stack_count - 1U]);
    vm->stack_count -= 1U;
    return value;
}

vigil_status_t vigil_vm_checked_add(int64_t left, int64_t right, int64_t *out_result)
{
    if ((right > 0 && left > INT64_MAX - right) || (right < 0 && left < INT64_MIN - right))
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left + right;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_checked_uadd(uint64_t left, uint64_t right, uint64_t *out_result)
{
    if (left > UINT64_MAX - right)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left + right;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_checked_subtract(int64_t left, int64_t right, int64_t *out_result)
{
    if ((right > 0 && left < INT64_MIN + right) || (right < 0 && left > INT64_MAX + right))
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left - right;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_checked_usubtract(uint64_t left, uint64_t right, uint64_t *out_result)
{
    if (left < right)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left - right;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_checked_multiply(int64_t left, int64_t right, int64_t *out_result)
{
    if (left == 0 || right == 0)
    {
        *out_result = 0;
        return VIGIL_STATUS_OK;
    }

    if ((left == -1 && right == INT64_MIN) || (right == -1 && left == INT64_MIN))
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (left > 0)
    {
        if (right > 0)
        {
            if (left > INT64_MAX / right)
            {
                return VIGIL_STATUS_INVALID_ARGUMENT;
            }
        }
        else if (right < INT64_MIN / left)
        {
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
    }
    else if (right > 0)
    {
        if (left < INT64_MIN / right)
        {
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
    }
    else if (left != 0 && right < INT64_MAX / left)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left * right;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_checked_umultiply(uint64_t left, uint64_t right, uint64_t *out_result)
{
    if (left == 0U || right == 0U)
    {
        *out_result = 0U;
        return VIGIL_STATUS_OK;
    }
    if (left > UINT64_MAX / right)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left * right;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_checked_divide(int64_t left, int64_t right, int64_t *out_result)
{
    if (right == 0)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (left == INT64_MIN && right == -1)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left / right;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_checked_udivide(uint64_t left, uint64_t right, uint64_t *out_result)
{
    if (right == 0U)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left / right;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_checked_modulo(int64_t left, int64_t right, int64_t *out_result)
{
    if (right == 0)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (left == INT64_MIN && right == -1)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left % right;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_checked_umodulo(uint64_t left, uint64_t right, uint64_t *out_result)
{
    if (right == 0U)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left % right;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_checked_negate(int64_t value, int64_t *out_result)
{
    if (value == INT64_MIN)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = -value;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_checked_shift_left(int64_t left, int64_t right, int64_t *out_result)
{
    if (right < 0 || right >= 64)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = (int64_t)(((uint64_t)left) << (uint32_t)right);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_checked_shift_right(int64_t left, int64_t right, int64_t *out_result)
{
    uint64_t shifted;

    if (right < 0 || right >= 64)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (right == 0)
    {
        *out_result = left;
        return VIGIL_STATUS_OK;
    }

    shifted = ((uint64_t)left) >> (uint32_t)right;
    if (left < 0)
    {
        shifted |= UINT64_MAX << (64U - (uint32_t)right);
    }

    *out_result = (int64_t)shifted;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_checked_ushift_left(uint64_t left, uint64_t right, uint64_t *out_result)
{
    if (right >= 64U)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left << (uint32_t)right;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_checked_ushift_right(uint64_t left, uint64_t right, uint64_t *out_result)
{
    if (right >= 64U)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left >> (uint32_t)right;
    return VIGIL_STATUS_OK;
}

int vigil_vm_value_is_integer(const vigil_value_t *value)
{
    return value != NULL && (vigil_nanbox_is_int(*value) || vigil_nanbox_is_uint(*value));
}

static int vigil_vm_string_objects_equal(const vigil_object_t *left_object, const vigil_object_t *right_object)
{
    size_t left_length;
    size_t right_length;
    const char *left_text;
    const char *right_text;

    left_length = vigil_string_object_length(left_object);
    right_length = vigil_string_object_length(right_object);
    left_text = vigil_string_object_c_str(left_object);
    right_text = vigil_string_object_c_str(right_object);
    return left_length == right_length && left_text != NULL && right_text != NULL &&
           memcmp(left_text, right_text, left_length) == 0;
}

static int vigil_vm_error_objects_equal(const vigil_object_t *left_object, const vigil_object_t *right_object)
{
    size_t left_length;
    size_t right_length;
    const char *left_text;
    const char *right_text;

    left_length = vigil_error_object_message_length(left_object);
    right_length = vigil_error_object_message_length(right_object);
    left_text = vigil_error_object_message(left_object);
    right_text = vigil_error_object_message(right_object);
    return vigil_error_object_kind(left_object) == vigil_error_object_kind(right_object) &&
           left_length == right_length && left_text != NULL && right_text != NULL &&
           memcmp(left_text, right_text, left_length) == 0;
}

static int vigil_vm_object_values_equal(const vigil_value_t *left, const vigil_value_t *right)
{
    const vigil_object_t *left_object;
    const vigil_object_t *right_object;

    left_object = ((vigil_object_t *)vigil_nanbox_decode_ptr(*left));
    right_object = ((vigil_object_t *)vigil_nanbox_decode_ptr(*right));
    if (left_object == right_object)
    {
        return 1;
    }
    if (left_object == NULL || right_object == NULL)
    {
        return 0;
    }
    if (vigil_object_type(left_object) == VIGIL_OBJECT_STRING && vigil_object_type(right_object) == VIGIL_OBJECT_STRING)
    {
        return vigil_vm_string_objects_equal(left_object, right_object);
    }
    if (vigil_object_type(left_object) == VIGIL_OBJECT_ERROR && vigil_object_type(right_object) == VIGIL_OBJECT_ERROR)
    {
        return vigil_vm_error_objects_equal(left_object, right_object);
    }
    return 0;
}

static int vigil_vm_scalar_values_equal(const vigil_value_t *left, const vigil_value_t *right)
{
    switch (vigil_value_kind(left))
    {
    case VIGIL_VALUE_NIL:
        return 1;
    case VIGIL_VALUE_BOOL:
        return vigil_nanbox_decode_bool(*left) == vigil_nanbox_decode_bool(*right);
    case VIGIL_VALUE_INT:
        return vigil_value_as_int(left) == vigil_value_as_int(right);
    case VIGIL_VALUE_UINT:
        return vigil_value_as_uint(left) == vigil_value_as_uint(right);
    case VIGIL_VALUE_FLOAT:
        return vigil_nanbox_decode_double(*left) == vigil_nanbox_decode_double(*right);
    default:
        return 0;
    }
}

int vigil_vm_values_equal(const vigil_value_t *left, const vigil_value_t *right)
{
    if (left == NULL || right == NULL)
    {
        return 0;
    }

    if (vigil_value_kind(left) != vigil_value_kind(right))
    {
        return 0;
    }

    if (vigil_value_kind(left) == VIGIL_VALUE_OBJECT)
    {
        return vigil_vm_object_values_equal(left, right);
    }

    return vigil_vm_scalar_values_equal(left, right);
}

int vigil_vm_value_is_supported_map_key(const vigil_value_t *value)
{
    const vigil_object_t *object;

    if (value == NULL)
    {
        return 0;
    }

    switch (vigil_value_kind(value))
    {
    case VIGIL_VALUE_BOOL:
    case VIGIL_VALUE_INT:
    case VIGIL_VALUE_UINT:
        return 1;
    case VIGIL_VALUE_OBJECT:
        object = ((vigil_object_t *)vigil_nanbox_decode_ptr(*value));
        return object != NULL && vigil_object_type(object) == VIGIL_OBJECT_STRING;
    default:
        return 0;
    }
}

vigil_status_t vigil_vm_concat_strings(vigil_vm_t *vm, const vigil_value_t *left, const vigil_value_t *right,
                                       vigil_value_t *out_value, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_string_t text;
    const vigil_object_t *left_object;
    const vigil_object_t *right_object;
    vigil_object_t *object;

    if (vm == NULL || left == NULL || right == NULL || out_value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "string operands are required");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    left_object = (vigil_object_t *)vigil_nanbox_decode_ptr(*left);
    right_object = (vigil_object_t *)vigil_nanbox_decode_ptr(*right);
    if (left_object == NULL || right_object == NULL || vigil_object_type(left_object) != VIGIL_OBJECT_STRING ||
        vigil_object_type(right_object) != VIGIL_OBJECT_STRING)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "string operands are required");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    object = NULL;
    vigil_string_init(&text, vm->runtime);
    status = vigil_string_append(&text, vigil_string_object_c_str(left_object), vigil_string_object_length(left_object),
                                 error);
    if (status == VIGIL_STATUS_OK)
    {
        status = vigil_string_append(&text, vigil_string_object_c_str(right_object),
                                     vigil_string_object_length(right_object), error);
    }
    if (status == VIGIL_STATUS_OK)
    {
        status =
            vigil_string_object_new(vm->runtime, vigil_string_c_str(&text), vigil_string_length(&text), &object, error);
    }
    if (status == VIGIL_STATUS_OK)
    {
        vigil_value_init_object(out_value, &object);
    }
    vigil_object_release(&object);
    vigil_string_free(&text);
    return status;
}

static vigil_status_t vigil_vm_stringify_object_value(const vigil_value_t *value, vigil_value_t *out_value,
                                                      vigil_error_t *error)
{
    if (((vigil_object_t *)vigil_nanbox_decode_ptr(*value)) == NULL ||
        vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(*value))) != VIGIL_OBJECT_STRING)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "string conversion requires a primitive or string operand");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    *out_value = vigil_value_copy(value);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_stringify_bool_value(vigil_vm_t *vm, const vigil_value_t *value,
                                                    vigil_value_t *out_value, vigil_error_t *error)
{
    const char *text;
    vigil_object_t *object;
    vigil_status_t status;

    object = NULL;
    text = vigil_nanbox_decode_bool(*value) ? "true" : "false";
    status = vigil_string_object_new_cstr(vm->runtime, text, &object, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_value_init_object(out_value, &object);
    vigil_object_release(&object);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_stringify_formatted_value(vigil_vm_t *vm, const char *buffer, size_t length,
                                                         vigil_value_t *out_value, vigil_error_t *error)
{
    vigil_object_t *object;
    vigil_status_t status;

    object = NULL;
    status = vigil_string_object_new(vm->runtime, buffer, length, &object, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_value_init_object(out_value, &object);
    vigil_object_release(&object);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_stringify_int_value(vigil_vm_t *vm, const vigil_value_t *value, vigil_value_t *out_value,
                                                   vigil_error_t *error)
{
    char buffer[128];
    int written;

    written = snprintf(buffer, sizeof(buffer), "%lld", (long long)vigil_value_as_int(value));
    if (written < 0 || (size_t)written >= sizeof(buffer))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format integer string conversion");
        return VIGIL_STATUS_INTERNAL;
    }

    return vigil_vm_stringify_formatted_value(vm, buffer, (size_t)written, out_value, error);
}

static vigil_status_t vigil_vm_stringify_uint_value(vigil_vm_t *vm, const vigil_value_t *value,
                                                    vigil_value_t *out_value, vigil_error_t *error)
{
    char buffer[128];
    int written;

    written = snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)vigil_value_as_uint(value));
    if (written < 0 || (size_t)written >= sizeof(buffer))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format integer string conversion");
        return VIGIL_STATUS_INTERNAL;
    }

    return vigil_vm_stringify_formatted_value(vm, buffer, (size_t)written, out_value, error);
}

static vigil_status_t vigil_vm_stringify_float_value(vigil_vm_t *vm, const vigil_value_t *value,
                                                     vigil_value_t *out_value, vigil_error_t *error)
{
    char buffer[128];
    int written;

    written = snprintf(buffer, sizeof(buffer), "%.17g", vigil_nanbox_decode_double(*value));
    if (written < 0 || (size_t)written >= sizeof(buffer))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format float string conversion");
        return VIGIL_STATUS_INTERNAL;
    }

    return vigil_vm_stringify_formatted_value(vm, buffer, (size_t)written, out_value, error);
}

vigil_status_t vigil_vm_stringify_value(vigil_vm_t *vm, const vigil_value_t *value, vigil_value_t *out_value,
                                        vigil_error_t *error)
{
    if (vm == NULL || value == NULL || out_value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "vm string conversion arguments must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    switch (vigil_value_kind(value))
    {
    case VIGIL_VALUE_BOOL:
        return vigil_vm_stringify_bool_value(vm, value, out_value, error);
    case VIGIL_VALUE_INT:
        return vigil_vm_stringify_int_value(vm, value, out_value, error);
    case VIGIL_VALUE_UINT:
        return vigil_vm_stringify_uint_value(vm, value, out_value, error);
    case VIGIL_VALUE_FLOAT:
        return vigil_vm_stringify_float_value(vm, value, out_value, error);
    case VIGIL_VALUE_OBJECT:
        return vigil_vm_stringify_object_value(value, out_value, error);
    default:
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "string conversion requires a primitive or string operand");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
}

/* ── FORMAT_SPEC helpers ─────────────────────────────────────────── */

/* Encoding for word1:
   bits  0-7:  fill character (ASCII, 0 = space default)
   bits  8-9:  alignment  0=default 1=left 2=right 3=center
   bits 10-13: format type 0=str 1=dec 2=hex 3=HEX 4=bin 5=oct 6=float_f
   bit  14:    grouping (thousands separator)
*/
#define FSPEC_FILL(w) ((char)((w) & 0xFFU))
#define FSPEC_ALIGN(w) (((w) >> 8U) & 0x3U)
#define FSPEC_TYPE(w) (((w) >> 10U) & 0xFU)
#define FSPEC_GROUP(w) (((w) >> 14U) & 0x1U)
#define FSPEC_WIDTH(w) ((w) & 0xFFFFU)
#define FSPEC_PREC(w) (((w) >> 16U) & 0xFFFFU)

typedef struct
{
    char fill;
    unsigned int align;
    unsigned int width;
    vigil_value_t *out_value;
} vigil_vm_format_spec_layout_t;

typedef struct
{
    unsigned int fmt_type;
    unsigned int grouping;
    char *buf;
    size_t buf_size;
    int *out_len;
    vigil_error_t *error;
} vigil_vm_format_spec_int_args_t;

static uint64_t vigil_vm_format_spec_abs_i64(int64_t value)
{
    if (value < 0)
    {
        return (uint64_t)(-(value + 1)) + 1U;
    }
    return (uint64_t)value;
}

static vigil_status_t vigil_vm_format_spec_write_decimal(const vigil_value_t *val, char *buf, size_t buf_size,
                                                         int *out_len, vigil_error_t *error)
{
    int len;

    if (!vigil_nanbox_is_int(*val))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "integer format specifier requires an integer value");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    len = snprintf(buf, buf_size, "%lld", (long long)vigil_nanbox_decode_int(*val));
    if (len < 0 || (size_t)len >= buf_size)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format decimal value");
        return VIGIL_STATUS_INTERNAL;
    }

    *out_len = len;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_format_spec_write_hex(const vigil_value_t *val, char *buf, size_t buf_size,
                                                     int uppercase, int *out_len, vigil_error_t *error)
{
    int64_t ival;
    uint64_t uval;
    int len;

    if (!vigil_nanbox_is_int(*val))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "integer format specifier requires an integer value");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    ival = vigil_nanbox_decode_int(*val);
    uval = vigil_vm_format_spec_abs_i64(ival);
    if (ival < 0)
    {
        len = snprintf(buf, buf_size, uppercase ? "-%llX" : "-%llx", (unsigned long long)uval);
    }
    else
    {
        len = snprintf(buf, buf_size, uppercase ? "%llX" : "%llx", (unsigned long long)uval);
    }
    if (len < 0 || (size_t)len >= buf_size)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format hexadecimal value");
        return VIGIL_STATUS_INTERNAL;
    }

    *out_len = len;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_format_spec_write_binary_digits(uint64_t uval, char *buf, size_t buf_size, int pos,
                                                               int *out_len, vigil_error_t *error)
{
    char tmp[65];
    int ti;

    if (uval == 0U)
    {
        if ((size_t)(pos + 1) >= buf_size)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format binary value");
            return VIGIL_STATUS_INTERNAL;
        }
        buf[pos++] = '0';
    }
    else
    {
        ti = 0;
        while (uval > 0U && ti < (int)sizeof(tmp))
        {
            tmp[ti++] = (char)('0' + (char)(uval & 1U));
            uval >>= 1U;
        }
        while (ti > 0)
        {
            if ((size_t)(pos + 1) >= buf_size)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format binary value");
                return VIGIL_STATUS_INTERNAL;
            }
            buf[pos++] = tmp[--ti];
        }
    }

    if ((size_t)pos >= buf_size)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format binary value");
        return VIGIL_STATUS_INTERNAL;
    }
    buf[pos] = '\0';
    *out_len = pos;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_format_spec_write_binary(const vigil_value_t *val, char *buf, size_t buf_size,
                                                        int *out_len, vigil_error_t *error)
{
    int64_t ival;
    uint64_t uval;
    int pos;
    vigil_status_t status;

    if (!vigil_nanbox_is_int(*val))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "integer format specifier requires an integer value");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    ival = vigil_nanbox_decode_int(*val);
    pos = 0;
    if (ival < 0)
    {
        if (buf_size < 2U)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format binary value");
            return VIGIL_STATUS_INTERNAL;
        }
        buf[pos++] = '-';
    }
    uval = vigil_vm_format_spec_abs_i64(ival);
    status = vigil_vm_format_spec_write_binary_digits(uval, buf, buf_size, pos, out_len, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    return VIGIL_STATUS_OK;
}

static int vigil_vm_format_spec_string_object(const vigil_value_t *val, const char **out_text, size_t *out_length)
{
    const vigil_object_t *obj;

    if (!vigil_nanbox_is_object(*val))
    {
        return 0;
    }

    obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(*val);
    if (obj == NULL || vigil_object_type(obj) != VIGIL_OBJECT_STRING)
    {
        return 0;
    }

    *out_text = vigil_string_object_c_str(obj);
    *out_length = vigil_string_object_length(obj);
    return 1;
}

static vigil_status_t vigil_vm_format_spec_write_octal(const vigil_value_t *val, char *buf, size_t buf_size,
                                                       int *out_len, vigil_error_t *error)
{
    int64_t ival;
    uint64_t uval;
    int len;

    if (!vigil_nanbox_is_int(*val))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "integer format specifier requires an integer value");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    ival = vigil_nanbox_decode_int(*val);
    uval = vigil_vm_format_spec_abs_i64(ival);
    if (ival < 0)
    {
        len = snprintf(buf, buf_size, "-%llo", (unsigned long long)uval);
    }
    else
    {
        len = snprintf(buf, buf_size, "%llo", (unsigned long long)uval);
    }
    if (len < 0 || (size_t)len >= buf_size)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format octal value");
        return VIGIL_STATUS_INTERNAL;
    }

    *out_len = len;
    return VIGIL_STATUS_OK;
}

static int vigil_vm_format_spec_apply_grouping(char *buf, int len)
{
    char tmp[256];
    int src;
    int dst;
    int start;

    if (len <= 0 || (size_t)len >= sizeof(tmp))
    {
        return len;
    }

    src = 0;
    dst = 0;
    if (buf[0] == '-')
    {
        tmp[dst++] = '-';
        src = 1;
    }
    start = src;
    while (src < len)
    {
        int remaining;

        remaining = len - src;
        if (remaining > 0 && remaining % 3 == 0 && src > start)
        {
            tmp[dst++] = ',';
        }
        tmp[dst++] = buf[src++];
    }

    memcpy(buf, tmp, (size_t)dst);
    buf[dst] = '\0';
    return dst;
}

static vigil_status_t vigil_vm_format_spec_integer_value(const vigil_value_t *val,
                                                         const vigil_vm_format_spec_int_args_t *args)
{
    vigil_status_t status;
    int len;

    if (args->fmt_type == 1U)
    {
        status = vigil_vm_format_spec_write_decimal(val, args->buf, args->buf_size, &len, args->error);
    }
    else if (args->fmt_type == 2U)
    {
        status = vigil_vm_format_spec_write_hex(val, args->buf, args->buf_size, 0, &len, args->error);
    }
    else if (args->fmt_type == 3U)
    {
        status = vigil_vm_format_spec_write_hex(val, args->buf, args->buf_size, 1, &len, args->error);
    }
    else if (args->fmt_type == 4U)
    {
        status = vigil_vm_format_spec_write_binary(val, args->buf, args->buf_size, &len, args->error);
    }
    else
    {
        status = vigil_vm_format_spec_write_octal(val, args->buf, args->buf_size, &len, args->error);
    }
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (args->grouping != 0U && args->fmt_type == 1U)
    {
        len = vigil_vm_format_spec_apply_grouping(args->buf, len);
    }
    *args->out_len = len;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_format_spec_float_value(const vigil_value_t *val, unsigned int precision, char *buf,
                                                       size_t buf_size, int *out_len, vigil_error_t *error)
{
    char fmt[32];
    int len;

    if (!vigil_nanbox_is_double(*val))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "float format specifier requires f64 value");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    len = snprintf(fmt, sizeof(fmt), "%%.%uf", precision);
    if (len < 0 || (size_t)len >= sizeof(fmt))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to build float format specifier");
        return VIGIL_STATUS_INTERNAL;
    }

    len = snprintf(buf, buf_size, fmt, vigil_nanbox_decode_double(*val));
    if (len < 0 || (size_t)len >= buf_size)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format float value");
        return VIGIL_STATUS_INTERNAL;
    }

    *out_len = len;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_format_spec_string_value(const vigil_value_t *val, const char **out_text,
                                                        size_t *out_length)
{
    if (vigil_vm_format_spec_string_object(val, out_text, out_length))
    {
        return VIGIL_STATUS_OK;
    }

    *out_text = "";
    *out_length = 0U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_format_spec_emit_text(vigil_vm_t *vm, const char *text, size_t text_len,
                                                     const vigil_vm_format_spec_layout_t *layout, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_object_t *object;
    void *memory;
    size_t pad;
    size_t total;
    size_t lpad;
    size_t rpad;
    char *out;

    if (layout->width > 0U && text_len < layout->width)
    {
        pad = layout->width - text_len;
        total = layout->width;
        status = vigil_runtime_alloc(vm->runtime, total + 1U, &memory, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        out = (char *)memory;
        lpad = 0U;
        rpad = 0U;
        if (layout->align == 1U)
        {
            rpad = pad;
        }
        else if (layout->align == 3U)
        {
            lpad = pad / 2U;
            rpad = pad - lpad;
        }
        else
        {
            lpad = pad;
        }
        memset(out, layout->fill, lpad);
        if (text_len > 0U)
        {
            memcpy(out + lpad, text, text_len);
        }
        memset(out + lpad + text_len, layout->fill, rpad);
        object = NULL;
        status = vigil_string_object_new(vm->runtime, out, total, &object, error);
        vigil_runtime_free(vm->runtime, &memory);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        vigil_value_init_object(layout->out_value, &object);
        vigil_object_release(&object);
        return VIGIL_STATUS_OK;
    }

    object = NULL;
    status = vigil_string_object_new(vm->runtime, text == NULL ? "" : text, text_len, &object, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_value_init_object(layout->out_value, &object);
    vigil_object_release(&object);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_format_spec_value(vigil_vm_t *vm, const vigil_value_t *val, uint32_t word1, uint32_t word2,
                                          vigil_value_t *out_value, vigil_error_t *error)
{
    char fill;
    unsigned int align;
    unsigned int fmt_type;
    unsigned int grouping;
    unsigned int width;
    unsigned int precision;
    char buf[256];
    int len;
    vigil_status_t status;
    const char *text;
    size_t text_len;

    fill = FSPEC_FILL(word1);
    if (fill == 0)
        fill = ' ';
    align = FSPEC_ALIGN(word1);
    fmt_type = FSPEC_TYPE(word1);
    grouping = FSPEC_GROUP(word1);
    width = FSPEC_WIDTH(word2);
    precision = FSPEC_PREC(word2);

    /* Step 1: format the value into buf[] based on fmt_type. */
    if (fmt_type == 6U)
    {
        status = vigil_vm_format_spec_float_value(val, precision, buf, sizeof(buf), &len, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }
    else if (fmt_type >= 1U && fmt_type <= 5U)
    {
        vigil_vm_format_spec_int_args_t int_args;

        int_args.fmt_type = fmt_type;
        int_args.grouping = grouping;
        int_args.buf = buf;
        int_args.buf_size = sizeof(buf);
        int_args.out_len = &len;
        int_args.error = error;
        status = vigil_vm_format_spec_integer_value(val, &int_args);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }
    else
    {
        status = vigil_vm_format_spec_string_value(val, &text, &text_len);
        vigil_vm_format_spec_layout_t layout;

        layout.fill = fill;
        layout.align = align;
        layout.width = width;
        layout.out_value = out_value;
        return vigil_vm_format_spec_emit_text(vm, text, text_len, &layout, error);
    }

    vigil_vm_format_spec_layout_t layout;

    layout.fill = fill;
    layout.align = align;
    layout.width = width;
    layout.out_value = out_value;
    return vigil_vm_format_spec_emit_text(vm, buf, (size_t)len, &layout, error);
}

vigil_status_t vigil_vm_format_f64_value(vigil_vm_t *vm, const vigil_value_t *value, uint32_t precision,
                                         vigil_value_t *out_value, vigil_error_t *error)
{
    vigil_status_t status;
    char format[32];
    int written;
    int length;
    void *memory;
    char *buffer;
    vigil_object_t *object;

    if (vm == NULL || value == NULL || out_value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "f64 format arguments must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (!vigil_nanbox_is_double(*value))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "f64 formatting requires an f64 operand");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    written = snprintf(format, sizeof(format), "%%.%uf", (unsigned int)precision);
    if (written < 0 || (size_t)written >= sizeof(format))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to build float format specifier");
        return VIGIL_STATUS_INTERNAL;
    }

    length = snprintf(NULL, 0, format, vigil_nanbox_decode_double(*value));
    if (length < 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to measure formatted float output");
        return VIGIL_STATUS_INTERNAL;
    }

    object = NULL;
    memory = NULL;
    status = vigil_runtime_alloc(vm->runtime, (size_t)length + 1U, &memory, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    buffer = (char *)memory;
    written = snprintf(buffer, (size_t)length + 1U, format, vigil_nanbox_decode_double(*value));
    if (written != length)
    {
        vigil_runtime_free(vm->runtime, &memory);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to write formatted float output");
        return VIGIL_STATUS_INTERNAL;
    }

    status = vigil_string_object_new(vm->runtime, buffer, (size_t)length, &object, error);
    vigil_runtime_free(vm->runtime, &memory);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vigil_value_init_object(out_value, &object);
    vigil_object_release(&object);
    return VIGIL_STATUS_OK;
}

int vigil_vm_get_string_parts(const vigil_value_t *value, const char **out_text, size_t *out_length)
{
    const vigil_object_t *object;

    if (out_text != NULL)
    {
        *out_text = NULL;
    }
    if (out_length != NULL)
    {
        *out_length = 0U;
    }
    if (value == NULL || !vigil_nanbox_is_object(*value))
    {
        return 0;
    }
    object = ((vigil_object_t *)vigil_nanbox_decode_ptr(*value));
    if (object == NULL || vigil_object_type(object) != VIGIL_OBJECT_STRING)
    {
        return 0;
    }
    if (out_text != NULL)
    {
        *out_text = vigil_string_object_c_str(object);
    }
    if (out_length != NULL)
    {
        *out_length = vigil_string_object_length(object);
    }
    return 1;
}

vigil_status_t vigil_vm_new_string_value(vigil_vm_t *vm, const char *text, size_t length, vigil_value_t *out_value,
                                         vigil_error_t *error)
{
    vigil_status_t status;
    vigil_object_t *object;

    if (vm == NULL || out_value == NULL || (length != 0U && text == NULL))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "string creation arguments are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    object = NULL;
    status = vigil_string_object_new(vm->runtime, text == NULL ? "" : text, length, &object, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_value_init_object(out_value, &object);
    vigil_object_release(&object);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_make_error_value(vigil_vm_t *vm, int64_t kind, const char *message, size_t message_length,
                                         vigil_value_t *out_value, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_object_t *object;

    object = NULL;
    status = vigil_error_object_new(vm->runtime, message, message_length, kind, &object, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_value_init_object(out_value, &object);
    vigil_object_release(&object);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_make_ok_error_value(vigil_vm_t *vm, vigil_value_t *out_value, vigil_error_t *error)
{
    return vigil_vm_make_error_value(vm, 0, "", 0U, out_value, error);
}

vigil_status_t vigil_vm_make_bounds_error_value(vigil_vm_t *vm, const char *message, vigil_value_t *out_value,
                                                vigil_error_t *error)
{
    size_t length;

    length = message == NULL ? 0U : strlen(message);
    return vigil_vm_make_error_value(vm, 7, message == NULL ? "" : message, length, out_value, error);
}

int vigil_vm_find_substring(const char *text, size_t text_length, const char *needle, size_t needle_length,
                            size_t *out_index)
{
    size_t index;

    if (out_index != NULL)
    {
        *out_index = 0U;
    }
    if (text == NULL || needle == NULL)
    {
        return 0;
    }
    if (needle_length == 0U)
    {
        return 1;
    }
    if (needle_length > text_length)
    {
        return 0;
    }

    for (index = 0U; index + needle_length <= text_length; index += 1U)
    {
        if (memcmp(text + index, needle, needle_length) == 0)
        {
            if (out_index != NULL)
            {
                *out_index = index;
            }
            return 1;
        }
    }

    return 0;
}

static vigil_status_t vigil_vm_push_checked_signed_integer(vigil_vm_t *vm, int64_t integer_value, int64_t minimum_value,
                                                           int64_t maximum_value, const char *error_message,
                                                           vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value;

    if (integer_value < minimum_value || integer_value > maximum_value)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, error_message);
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    do
    {
        vigil_value_init_int(&(value), integer_value);
    } while (0);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

static vigil_status_t vigil_vm_push_checked_unsigned_integer(vigil_vm_t *vm, uint64_t integer_value,
                                                             uint64_t maximum_value, const char *error_message,
                                                             vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value;

    if (integer_value > maximum_value)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, error_message);
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    do
    {
        vigil_value_init_uint(&(value), integer_value);
    } while (0);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

vigil_status_t vigil_vm_convert_to_signed_integer_type(vigil_vm_t *vm, const vigil_value_t *value,
                                                       int64_t minimum_value, int64_t maximum_value,
                                                       const char *operand_error, const char *range_error,
                                                       vigil_error_t *error)
{
    int64_t integer_value;
    double float_value;

    if (vm == NULL || value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "integer conversion arguments must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (vigil_nanbox_is_int(*value))
    {
        return vigil_vm_push_checked_signed_integer(vm, vigil_value_as_int(value), minimum_value, maximum_value,
                                                    range_error, error);
    }
    if (vigil_nanbox_is_uint(*value))
    {
        if (vigil_value_as_uint(value) > (uint64_t)maximum_value)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, range_error);
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        return vigil_vm_push_checked_signed_integer(vm, (int64_t)vigil_value_as_uint(value), minimum_value,
                                                    maximum_value, range_error, error);
    }
    if (!vigil_nanbox_is_double(*value))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, operand_error);
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    float_value = vigil_nanbox_decode_double(*value);
    if (!isfinite(float_value) || float_value > (double)INT64_MAX || float_value < (double)INT64_MIN)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, range_error);
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    integer_value = (int64_t)float_value;
    return vigil_vm_push_checked_signed_integer(vm, integer_value, minimum_value, maximum_value, range_error, error);
}

vigil_status_t vigil_vm_convert_to_unsigned_integer_type(vigil_vm_t *vm, const vigil_value_t *value,
                                                         uint64_t maximum_value, const char *operand_error,
                                                         const char *range_error, vigil_error_t *error)
{
    uint64_t integer_value;
    double float_value;

    if (vm == NULL || value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "integer conversion arguments must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (vigil_nanbox_is_uint(*value))
    {
        return vigil_vm_push_checked_unsigned_integer(vm, vigil_value_as_uint(value), maximum_value, range_error,
                                                      error);
    }
    if (vigil_nanbox_is_int(*value))
    {
        if (vigil_value_as_int(value) < 0)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, range_error);
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        return vigil_vm_push_checked_unsigned_integer(vm, (uint64_t)vigil_value_as_int(value), maximum_value,
                                                      range_error, error);
    }
    if (!vigil_nanbox_is_double(*value))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, operand_error);
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    float_value = vigil_nanbox_decode_double(*value);
    if (!isfinite(float_value) || float_value < 0.0 || float_value > (double)UINT64_MAX)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, range_error);
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    integer_value = (uint64_t)float_value;
    return vigil_vm_push_checked_unsigned_integer(vm, integer_value, maximum_value, range_error, error);
}

vigil_status_t vigil_vm_fail_at_ip(vigil_vm_t *vm, vigil_status_t status, const char *message, vigil_error_t *error)
{
    vigil_source_span_t span;
    vigil_vm_frame_t *frame;

    vigil_error_set_literal(error, status, message);
    frame = vigil_vm_current_frame(vm);
    if (error != NULL && frame != NULL)
    {
        span = vigil_chunk_span_at(frame->chunk, frame->ip);
        error->location.source_id = span.source_id;
        error->location.offset = span.start_offset;
    }

    return status;
}

vigil_status_t vigil_vm_read_u32(vigil_vm_t *vm, uint32_t *out_value, vigil_error_t *error)
{
    vigil_vm_frame_t *frame;
    const uint8_t *code;
    size_t code_size;
    size_t ip;

    frame = vigil_vm_current_frame(vm);
    if (frame == NULL)
    {
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "vm frame is missing", error);
    }

    code = VIGIL_VM_CHUNK_CODE(frame->chunk);
    code_size = VIGIL_VM_CHUNK_CODE_SIZE(frame->chunk);
    ip = frame->ip;
    if (code == NULL || ip + 4U >= code_size)
    {
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "truncated operand in chunk", error);
    }

    *out_value = (uint32_t)code[ip + 1U];
    *out_value |= (uint32_t)code[ip + 2U] << 8U;
    *out_value |= (uint32_t)code[ip + 3U] << 16U;
    *out_value |= (uint32_t)code[ip + 4U] << 24U;
    frame->ip += 5U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_read_raw_u32(vigil_vm_t *vm, uint32_t *out_value, vigil_error_t *error)
{
    vigil_vm_frame_t *frame;
    const uint8_t *code;
    size_t code_size;
    size_t ip;

    frame = vigil_vm_current_frame(vm);
    if (frame == NULL)
    {
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "vm frame is missing", error);
    }

    code = VIGIL_VM_CHUNK_CODE(frame->chunk);
    code_size = VIGIL_VM_CHUNK_CODE_SIZE(frame->chunk);
    ip = frame->ip;
    if (code == NULL || ip + 3U >= code_size)
    {
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "truncated operand in chunk", error);
    }

    *out_value = (uint32_t)code[ip];
    *out_value |= (uint32_t)code[ip + 1U] << 8U;
    *out_value |= (uint32_t)code[ip + 2U] << 16U;
    *out_value |= (uint32_t)code[ip + 3U] << 24U;
    frame->ip += 4U;
    return VIGIL_STATUS_OK;
}

void vigil_vm_options_init(vigil_vm_options_t *options)
{
    if (options == NULL)
    {
        return;
    }

    memset(options, 0, sizeof(*options));
}

vigil_status_t vigil_vm_open(vigil_vm_t **out_vm, vigil_runtime_t *runtime, const vigil_vm_options_t *options,
                             vigil_error_t *error)
{
    vigil_vm_t *vm;
    void *memory;
    vigil_status_t status;
    size_t initial_stack_capacity;

    vigil_error_clear(error);
    if (out_vm == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_vm must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_vm = NULL;
    memory = NULL;
    status = vigil_runtime_alloc(runtime, sizeof(*vm), &memory, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vm = (vigil_vm_t *)memory;
    vm->runtime = runtime;
    initial_stack_capacity = options == NULL ? 0U : options->initial_stack_capacity;
    if (initial_stack_capacity < VIGIL_VM_INITIAL_STACK_CAPACITY)
    {
        initial_stack_capacity = VIGIL_VM_INITIAL_STACK_CAPACITY;
    }
    status = vigil_vm_grow_stack(vm, initial_stack_capacity, error);
    if (status != VIGIL_STATUS_OK)
    {
        memory = vm;
        vigil_runtime_free(runtime, &memory);
        return status;
    }
    status = vigil_vm_grow_frames(vm, VIGIL_VM_INITIAL_FRAME_CAPACITY, error);
    if (status != VIGIL_STATUS_OK)
    {
        memory = vm->stack;
        vigil_runtime_free(runtime, &memory);
        memory = vm;
        vigil_runtime_free(runtime, &memory);
        return status;
    }

    /* Inherit any debug hook that was active when this VM was created so that
     * thread-spawned VMs are immediately instrumented. */
    vm->debug_hook = runtime->debug_hook;
    vm->debug_hook_userdata = runtime->debug_hook_userdata;

    /* Record the calling thread's ID so the debugger can identify which
     * thread owns this VM when a breakpoint fires. */
    vm->thread_id = vigil_platform_thread_current_id();
    vm->aot_enabled = vigil_vm_default_aot_enabled();

    /* Register in the runtime's thread-aware VM registry. */
    status = vigil_runtime_register_vm(runtime, vm, error);
    if (status != VIGIL_STATUS_OK)
    {
        memory = vm->stack;
        vigil_runtime_free(runtime, &memory);
        memory = vm->frames;
        vigil_runtime_free(runtime, &memory);
        memory = vm;
        vigil_runtime_free(runtime, &memory);
        return status;
    }

    *out_vm = vm;
    return VIGIL_STATUS_OK;
}

void vigil_vm_close(vigil_vm_t **vm)
{
    vigil_vm_t *resolved_vm;
    vigil_runtime_t *runtime;
    void *memory;

    if (vm == NULL || *vm == NULL)
    {
        return;
    }

    resolved_vm = *vm;
    *vm = NULL;
    runtime = resolved_vm->runtime;

    /* Remove from the runtime's thread registry before freeing so the debugger
     * never holds a dangling VM pointer. */
    vigil_runtime_unregister_vm(runtime, resolved_vm);

    vigil_vm_release_stack(resolved_vm);
    vigil_vm_clear_frames(resolved_vm);
    memory = resolved_vm->stack;
    if (runtime != NULL)
    {
        vigil_runtime_free(runtime, &memory);
    }

    memory = resolved_vm->frames;
    if (runtime != NULL)
    {
        vigil_runtime_free(runtime, &memory);
    }

    memory = resolved_vm;
    if (runtime != NULL)
    {
        vigil_runtime_free(runtime, &memory);
    }
}

void vigil_vm_set_aot_enabled(vigil_vm_t *vm, int enabled)
{
    if (vm == NULL)
    {
        return;
    }

    vm->aot_enabled = enabled != 0;
}

int vigil_vm_aot_enabled(const vigil_vm_t *vm)
{
    if (vm == NULL)
    {
        return 0;
    }

    return vm->aot_enabled;
}

vigil_runtime_t *vigil_vm_runtime(const vigil_vm_t *vm)
{
    if (vm == NULL)
    {
        return NULL;
    }

    return vm->runtime;
}

size_t vigil_vm_stack_depth(const vigil_vm_t *vm)
{
    if (vm == NULL)
    {
        return 0U;
    }

    return vm->stack_count;
}

size_t vigil_vm_frame_depth(const vigil_vm_t *vm)
{
    if (vm == NULL)
    {
        return 0U;
    }

    return vm->frame_count;
}

vigil_value_t vigil_vm_stack_get(const vigil_vm_t *vm, size_t index)
{
    if (vm == NULL || index >= vm->stack_count)
    {
        vigil_value_t nil;
        vigil_value_init_nil(&nil);
        return nil;
    }
    return vm->stack[index];
}

vigil_status_t vigil_vm_stack_push(vigil_vm_t *vm, const vigil_value_t *value, vigil_error_t *error)
{
    if (vm == NULL || value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "vm and value must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    return vigil_vm_push(vm, value, error);
}

void vigil_vm_stack_pop_n(vigil_vm_t *vm, size_t count)
{
    size_t i;

    if (vm == NULL || count == 0U)
    {
        return;
    }
    if (count > vm->stack_count)
    {
        count = vm->stack_count;
    }
    for (i = 0U; i < count; i++)
    {
        vm->stack_count -= 1U;
        VIGIL_VM_VALUE_RELEASE(&vm->stack[vm->stack_count]);
    }
}

void vigil_vm_set_debug_hook(vigil_vm_t *vm, int (*hook)(vigil_vm_t *vm, void *userdata), void *userdata)
{
    if (vm == NULL)
        return;
    vm->debug_hook = hook;
    vm->debug_hook_userdata = userdata;
}

uint64_t vigil_vm_thread_id(const vigil_vm_t *vm)
{
    return vm != NULL ? vm->thread_id : 0U;
}

void vigil_vm_set_args(vigil_vm_t *vm, const char *const *argv, size_t argc)
{
    if (vm == NULL)
        return;
    vm->argv = argv;
    vm->argc = argc;
}

void vigil_vm_get_args(const vigil_vm_t *vm, const char *const **out_argv, size_t *out_argc)
{
    if (out_argv != NULL)
        *out_argv = vm != NULL ? vm->argv : NULL;
    if (out_argc != NULL)
        *out_argc = vm != NULL ? vm->argc : 0;
}

const vigil_chunk_t *vigil_vm_frame_chunk(const vigil_vm_t *vm, size_t frame_index)
{
    if (vm == NULL || frame_index >= vm->frame_count)
        return NULL;
    return vm->frames[frame_index].chunk;
}

size_t vigil_vm_frame_ip(const vigil_vm_t *vm, size_t frame_index)
{
    if (vm == NULL || frame_index >= vm->frame_count)
        return 0U;
    return vm->frames[frame_index].ip;
}

size_t vigil_vm_frame_base_slot(const vigil_vm_t *vm, size_t frame_index)
{
    if (vm == NULL || frame_index >= vm->frame_count)
        return 0U;
    return vm->frames[frame_index].base_slot;
}

const vigil_object_t *vigil_vm_frame_function(const vigil_vm_t *vm, size_t frame_index)
{
    if (vm == NULL || frame_index >= vm->frame_count)
        return NULL;
    return vm->frames[frame_index].function;
}

vigil_status_t vigil_vm_execute(vigil_vm_t *vm, const vigil_chunk_t *chunk, vigil_value_t *out_value,
                                vigil_error_t *error)
{
    vigil_status_t status;

    status = vigil_vm_validate(vm, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (chunk == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "chunk must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (out_value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_value must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_vm_release_stack(vm);
    vigil_vm_clear_frames(vm);
    status = vigil_vm_push_frame(vm, NULL, NULL, chunk, 0U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    return vigil_vm_execute_function(vm, NULL, out_value, error);
}

/* ── CALL_EXTERN runtime handler ─────────────────────────────────── */

/*
 * vigil_extern_call is implemented in ffi.c when the ffi stdlib module
 * is enabled. Reduced builds keep a stub here so the core VM does not
 * depend on that optional module at link time.
 */
#ifdef VIGIL_HAS_STDLIB_FFI
extern vigil_status_t vigil_extern_call(vigil_vm_t *vm, const char *desc, size_t desc_len, size_t arg_count,
                                        vigil_error_t *error);
#else
static vigil_status_t vigil_extern_call(vigil_vm_t *vm, const char *desc, size_t desc_len, size_t arg_count,
                                        vigil_error_t *error)
{
    (void)vm;
    (void)desc;
    (void)desc_len;
    (void)arg_count;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED,
                            "extern calls require a build with the ffi stdlib module enabled");
    return VIGIL_STATUS_UNSUPPORTED;
}
#endif

vigil_status_t vigil_vm_call_extern_fn(vigil_vm_t *vm, const char *desc, size_t desc_len, size_t arg_count,
                                       vigil_error_t *error)
{
    return vigil_extern_call(vm, desc, desc_len, arg_count, error);
}

/* ── Parse intrinsic helpers ─────────────────────────────────────────
   These implement parse.i32/f64/bool inline in the VM, avoiding the
   CALL_NATIVE overhead (constant lookup, function pointer dereference,
   vigil_vm_stack_push retain/release per push, and the ok_error
   retain/release cycle).  Each pops a string argument and pushes
   (value, err) — two stack slots. */

/* Push a parse-error pair: (default_value, error_object). */
static void vigil_vm_ensure_stack(vigil_vm_t *vm, size_t need)
{
    vigil_error_t err = {0};

    if (vm->stack_count + need > vm->stack_capacity)
        vigil_vm_grow_stack(vm, vm->stack_count + need, &err);
}

static void vigil_vm_push_parse_error(vigil_vm_t *vm, vigil_value_t default_val, const char *msg)
{
    vigil_object_t *err_obj = NULL;
    vigil_error_t err = {0};

    vigil_vm_ensure_stack(vm, 2U);
    if (vigil_error_object_new_cstr(vm->runtime, msg, 8, &err_obj, &err) != VIGIL_STATUS_OK)
        err_obj = NULL;
    vigil_value_release(&vm->stack[vm->stack_count]);
    vm->stack[vm->stack_count] = default_val;
    vm->stack_count += 1U;
    vigil_value_release(&vm->stack[vm->stack_count]);
    vm->stack[vm->stack_count] =
        err_obj != NULL ? vigil_nanbox_encode_object(err_obj) : vigil_runtime_ok_error_value(vm->runtime);
    vm->stack_count += 1U;
}

/* Push a parse-success pair: (value, ok_error). */
static void vigil_vm_push_parse_ok(vigil_vm_t *vm, vigil_value_t val)
{
    vigil_value_t ok = vigil_runtime_ok_error_value(vm->runtime);

    vigil_vm_ensure_stack(vm, 2U);
    /* The ok sentinel has a saturated refcount (immortal), so we skip
       the atomic retain.  POP's release will decrement harmlessly. */
    vigil_value_release(&vm->stack[vm->stack_count]);
    vm->stack[vm->stack_count] = val;
    vm->stack_count += 1U;
    vigil_value_release(&vm->stack[vm->stack_count]);
    vm->stack[vm->stack_count] = ok;
    vm->stack_count += 1U;
}

/* Parse intrinsic: pop string, push (i32, err). */
void vigil_vm_parse_i32(vigil_vm_t *vm)
{
    vigil_value_t input;
    vigil_object_t *obj;
    const char *s;
    char *end;
    long val;

    vm->stack_count -= 1U;
    input = vm->stack[vm->stack_count];
    vm->stack[vm->stack_count] = VIGIL_NANBOX_NIL;
    obj = (vigil_object_t *)vigil_nanbox_decode_ptr(input);
    s = vigil_string_object_c_str(obj);
    if (s == NULL || *s == '\0')
    {
        vigil_value_release(&input);
        vigil_vm_push_parse_error(vm, vigil_nanbox_encode_int(0), "empty string");
        return;
    }
    errno = 0;
    val = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || val < INT32_MIN || val > INT32_MAX)
    {
        vigil_value_release(&input);
        vigil_vm_push_parse_error(vm, vigil_nanbox_encode_int(0), "invalid integer");
        return;
    }
    vigil_value_release(&input);
    vigil_vm_push_parse_ok(vm, vigil_nanbox_encode_int((int64_t)val));
}

/* Parse intrinsic: pop string, push (f64, err). */
void vigil_vm_parse_f64(vigil_vm_t *vm)
{
    vigil_value_t input;
    vigil_object_t *obj;
    const char *s;
    char *end;
    double val;

    vm->stack_count -= 1U;
    input = vm->stack[vm->stack_count];
    vm->stack[vm->stack_count] = VIGIL_NANBOX_NIL;
    obj = (vigil_object_t *)vigil_nanbox_decode_ptr(input);
    s = vigil_string_object_c_str(obj);
    if (s == NULL || *s == '\0')
    {
        vigil_value_release(&input);
        vigil_vm_push_parse_error(vm, vigil_nanbox_encode_double(0.0), "empty string");
        return;
    }
    errno = 0;
    val = strtod(s, &end);
    if (errno != 0 || end == s || *end != '\0')
    {
        vigil_value_release(&input);
        vigil_vm_push_parse_error(vm, vigil_nanbox_encode_double(0.0), "invalid float");
        return;
    }
    vigil_value_release(&input);
    vigil_vm_push_parse_ok(vm, vigil_nanbox_encode_double(val));
}

/* Parse intrinsic: pop string, push (bool, err). */
void vigil_vm_parse_bool(vigil_vm_t *vm)
{
    vigil_value_t input;
    vigil_object_t *obj;
    const char *s;

    vm->stack_count -= 1U;
    input = vm->stack[vm->stack_count];
    vm->stack[vm->stack_count] = VIGIL_NANBOX_NIL;
    obj = (vigil_object_t *)vigil_nanbox_decode_ptr(input);
    s = vigil_string_object_c_str(obj);
    if (s != NULL && (strcmp(s, "true") == 0 || strcmp(s, "1") == 0))
    {
        vigil_value_release(&input);
        vigil_vm_push_parse_ok(vm, vigil_nanbox_from_bool(1));
        return;
    }
    if (s != NULL && (strcmp(s, "false") == 0 || strcmp(s, "0") == 0))
    {
        vigil_value_release(&input);
        vigil_vm_push_parse_ok(vm, vigil_nanbox_from_bool(0));
        return;
    }
    vigil_value_release(&input);
    vigil_vm_push_parse_error(vm, vigil_nanbox_from_bool(0), "invalid boolean");
}

/* Execute a sub-call from the register VM.  Pushes a frame for the
   callee, translates to register bytecode (with caching), and executes
   via the register VM.  Return values are left on the stack. */
vigil_status_t vigil_vm_execute_call(vigil_vm_t *vm, const vigil_object_t *callee, size_t arg_count,
                                     vigil_error_t *error)
{
    const vigil_reg_chunk_t *callee_rc;
    vigil_status_t status;

    /* Handle native functions directly — they have no chunk to translate. */
    if (callee && vigil_object_type(callee) == VIGIL_OBJECT_NATIVE_FUNCTION)
    {
        vigil_native_fn_t nfn = vigil_native_function_get((vigil_object_t *)callee);
        if (nfn)
            return nfn(vm, arg_count, error);
        return VIGIL_STATUS_INTERNAL;
    }

    size_t arg_base = vm->stack_count - arg_count;
    size_t base_slot = vm->stack_count; /* separate window */
    vigil_chunk_t *callee_chunk = (vigil_chunk_t *)vigil_callable_object_chunk(callee);
    if (!callee_chunk)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "execute_call: callee has no chunk");
        return VIGIL_STATUS_INTERNAL;
    }

    /* Push callee frame. */
    /* clang-format off */
    if (vm->frame_count >= vm->frame_capacity)
    {
        vigil_status_t s = vigil_vm_push_frame(vm, callee, vigil_callable_object_function(callee), callee_chunk, base_slot, error);
        if (s != VIGIL_STATUS_OK)
            return s;
    }
    else
    {
        vigil_vm_frame_t *nf = &vm->frames[vm->frame_count];
        nf->callable = callee;
        nf->function = vigil_callable_object_function(callee);
        nf->chunk = callee_chunk;
        nf->ip = 0U;
        nf->base_slot = base_slot;
        nf->defers = NULL;
        nf->defer_count = 0;
        nf->defer_capacity = 0;
        nf->pending_returns = NULL;
        nf->pending_return_count = 0;
        nf->pending_return_capacity = 0;
        nf->draining_defers = 0;
        vm->frame_count += 1U;
    }

    /* Translate on first use and publish the cache exactly once. */
    status = vigil_chunk_ensure_reg_cache(callee_chunk,
                                          (uint8_t)vigil_function_object_arity(vigil_callable_object_function(callee)),
                                          &callee_rc, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    /* Copy args to callee's separate window (retain objects). */
    {
        size_t arity = callee_rc->arity;
        size_t n = arity < arg_count ? arity : arg_count;
        size_t need = base_slot + (size_t)callee_rc->max_registers;
        if (vm->stack_capacity < need)
        {
            vigil_status_t gs = vigil_vm_grow_stack(vm, need + 16, error);
            if (gs != VIGIL_STATUS_OK) return gs;
        }
        /* clang-format on */
        vigil_vm_release_value_range(&vm->stack[base_slot], (size_t)callee_rc->max_registers);
        if (vm->stack_count < need)
            vm->stack_count = need;
        for (size_t a = 0; a < n; a++)
        {
            vm->stack[base_slot + a] = vm->stack[arg_base + a];
            if (vigil_nanbox_has_object(vm->stack[base_slot + a]))
                vigil_object_retain((vigil_object_t *)vigil_nanbox_decode_ptr(vm->stack[base_slot + a]));
        }
        for (size_t z = n; z < callee_rc->max_registers; z++)
            vm->stack[base_slot + z] = VIGIL_NANBOX_NIL;
    }

    vigil_value_t dummy = {0};
    status = vigil_reg_execute_cached(vm, callee_rc, &dummy, error);

    /* Pop the callee frame. */
    vigil_vm_pop_frame(vm);

    /* Copy ALL return values from callee's window to caller's expected position.
       The RETURN handler set stack_count = base + base_r + count. The translator
       moves return values to R[0..count-1], so they start at base_slot. */
    {
        size_t ret_count = (vm->stack_count > base_slot) ? (vm->stack_count - base_slot) : 0U;
        size_t callee_regs = (size_t)callee_rc->max_registers;

        if (ret_count > callee_regs)
        {
            ret_count = callee_regs;
        }

        if (arg_base == base_slot)
        {
            for (size_t r = ret_count; r < callee_regs; r++)
                vigil_value_release(&vm->stack[base_slot + r]);
            vm->stack_count = base_slot + ret_count;
            return status;
        }

        vigil_vm_release_value_range(&vm->stack[arg_base], arg_count);

        for (size_t r = ret_count; r < callee_regs; r++)
        {
            vigil_value_release(&vm->stack[base_slot + r]);
        }

        for (size_t r = 0U; r < ret_count; r++)
        {
            vm->stack[arg_base + r] = vm->stack[base_slot + r];
            vm->stack[base_slot + r] = VIGIL_NANBOX_NIL;
        }
        vm->stack_count = arg_base + ret_count;
    }

    return status;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) origin/main
vigil_status_t vigil_vm_execute_function(vigil_vm_t *vm, const vigil_object_t *function, vigil_value_t *out_value,
                                         vigil_error_t *error)
{
    vigil_status_t status;
    const vigil_reg_chunk_t *fn_rc;

    status = vigil_vm_validate(vm, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    if (out_value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_value must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (function != NULL)
    {
        if (vigil_object_type(function) != VIGIL_OBJECT_FUNCTION && vigil_object_type(function) != VIGIL_OBJECT_CLOSURE)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                    "function must be a function or closure object");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        const vigil_object_t *inner_fn = vigil_callable_object_function(function);
        size_t arity = vigil_function_object_arity(inner_fn);
        vigil_chunk_t *fn_chunk = (vigil_chunk_t *)vigil_callable_object_chunk(function);
        if (vm->stack_count < arity)
        {
            if (arity == 0U)
            {
                vigil_vm_release_stack(vm);
                vigil_vm_clear_frames(vm);
                status = vigil_vm_push_frame(vm, function, inner_fn, fn_chunk, 0U, error);
            }
            else
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                        "not enough arguments on stack for function arity");
                return VIGIL_STATUS_INVALID_ARGUMENT;
            }
        }
        else
        {
            size_t base = vm->stack_count - arity;
            vigil_vm_clear_frames(vm);
            status = vigil_vm_push_frame(vm, function, inner_fn, fn_chunk, base, error);
        }
        if (status != VIGIL_STATUS_OK)
            return status;

        status = vigil_chunk_ensure_reg_cache(fn_chunk, (uint8_t)arity, &fn_rc, error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_vm_release_stack(vm);
            vigil_vm_clear_frames(vm);
            return status;
        }

        status = vigil_reg_execute_cached(vm, fn_rc, out_value, error);
        {
            size_t nregs = (size_t)fn_rc->max_registers;
            if (status != VIGIL_STATUS_OK)
                *out_value = VIGIL_NANBOX_NIL;
            if (status == VIGIL_STATUS_OK && vigil_nanbox_has_object(*out_value))
                vigil_object_retain((vigil_object_t *)vigil_nanbox_decode_ptr(*out_value));
            vigil_vm_release_value_range(vm->stack, nregs < vm->stack_capacity ? nregs : vm->stack_capacity);
        }
        vm->stack_count = 0;
        vigil_vm_clear_frames(vm);
        return status;
    }

    /* function == NULL: frame already pushed (e.g. vigil_vm_execute).
       Translate the frame's chunk and execute. */
    {
        vigil_vm_frame_t *frame = &vm->frames[vm->frame_count - 1U];
        if (frame->chunk == NULL)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "vm frame chunk must not be null");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        vigil_chunk_t *chunk = (vigil_chunk_t *)frame->chunk;
        status = vigil_chunk_ensure_reg_cache(chunk, 0U, &fn_rc, error);
        if (status != VIGIL_STATUS_OK)
            return status;

        status = vigil_reg_execute_cached(vm, fn_rc, out_value, error);
        if (vm->in_regvm_call)
            return status;
        {
            size_t nregs = (size_t)fn_rc->max_registers;
            if (status != VIGIL_STATUS_OK)
                *out_value = VIGIL_NANBOX_NIL;
            if (status == VIGIL_STATUS_OK && vigil_nanbox_has_object(*out_value))
                vigil_object_retain((vigil_object_t *)vigil_nanbox_decode_ptr(*out_value));
            vigil_vm_release_value_range(vm->stack, nregs < vm->stack_capacity ? nregs : vm->stack_capacity);
        }
        vm->stack_count = 0;
        vigil_vm_clear_frames(vm);
        return status;
    }
}
