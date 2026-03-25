/*
 * vigil_vm_internal.h — Shared VM internals for dispatch-loop helpers.
 *
 * This header exposes the VM struct layout, frame types, and inline
 * macros so that opcode handler files (vm_ops_*.c) can operate on the
 * VM without duplicating definitions.  Nothing here is part of the
 * public API.
 */

#ifndef VIGIL_VM_INTERNAL_H
#define VIGIL_VM_INTERNAL_H

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../value_internal.h"
#include "vigil/string.h"
#include "vigil/vm.h"
#include "vigil_internal.h"
#include "vigil_nanbox.h"

/* ── Branch-prediction hints (no-op on MSVC) ───────────────────── */
#if defined(__GNUC__) || defined(__clang__)
#define VIGIL_LIKELY(x) __builtin_expect(!!(x), 1)
#define VIGIL_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define VIGIL_LIKELY(x) (x)
#define VIGIL_UNLIKELY(x) (x)
#endif

/* ── Frame and defer types ─────────────────────────────────────── */

typedef struct vigil_vm_frame
{
    const vigil_object_t *callable;
    const vigil_object_t *function;
    const vigil_chunk_t *chunk;
    size_t ip;
    size_t base_slot;
    struct vigil_vm_defer_action *defers;
    size_t defer_count;
    size_t defer_capacity;
    vigil_value_t *pending_returns;
    size_t pending_return_count;
    size_t pending_return_capacity;
    int draining_defers;
} vigil_vm_frame_t;

typedef enum vigil_vm_defer_kind
{
    VIGIL_VM_DEFER_CALL = 0,
    VIGIL_VM_DEFER_CALL_VALUE = 1,
    VIGIL_VM_DEFER_NEW_INSTANCE = 2,
    VIGIL_VM_DEFER_CALL_INTERFACE = 3,
    VIGIL_VM_DEFER_CALL_NATIVE = 4
} vigil_vm_defer_kind_t;

typedef struct vigil_vm_defer_action
{
    vigil_vm_defer_kind_t kind;
    uint32_t operand_a;
    uint32_t operand_b;
    uint32_t arg_count;
    vigil_value_t *values;
    size_t value_count;
} vigil_vm_defer_action_t;

struct vigil_vm
{
    vigil_runtime_t *runtime;
    vigil_value_t *stack;
    size_t stack_count;
    size_t stack_capacity;
    vigil_vm_frame_t *frames;
    size_t frame_count;
    size_t frame_capacity;
    int (*debug_hook)(vigil_vm_t *vm, void *userdata);
    void *debug_hook_userdata;
    const char *const *argv;
    size_t argc;
    /* Platform thread ID of the OS thread running this VM. Set when the VM is
     * opened so the debugger can identify which thread hit a breakpoint. */
    uint64_t thread_id;
    int in_regvm_call; /* set by vigil_vm_execute_call to skip cleanup */
};

/* ── Inline value helpers ──────────────────────────────────────── */

#define VIGIL_VM_VALUE_INIT_NIL(v)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        *(v) = VIGIL_NANBOX_NIL;                                                                                       \
    } while (0)

#define VIGIL_VM_VALUE_COPY(dst, src)                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        *(dst) = *(src);                                                                                               \
        if (vigil_nanbox_has_object(*(dst)))                                                                           \
            vigil_object_retain((vigil_object_t *)vigil_nanbox_decode_ptr(*(dst)));                                    \
    } while (0)

#define VIGIL_VM_VALUE_RELEASE(v)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        if (vigil_nanbox_has_object(*(v)))                                                                             \
        {                                                                                                              \
            vigil_object_t *_obj = (vigil_object_t *)vigil_nanbox_decode_ptr(*(v));                                    \
            vigil_object_release(&_obj);                                                                               \
        }                                                                                                              \
        *(v) = VIGIL_NANBOX_NIL;                                                                                       \
    } while (0)

/* ── Chunk field access ────────────────────────────────────────── */

#define VIGIL_VM_CHUNK_CODE(chunk) ((chunk)->code.data)
#define VIGIL_VM_CHUNK_CODE_SIZE(chunk) ((chunk)->code.length)
#define VIGIL_VM_CHUNK_CONSTANT(chunk, idx) ((idx) < (chunk)->constant_count ? &(chunk)->constants[(idx)] : NULL)

/* ── Shared VM helpers (defined in vm.c) ───────────────────────── */

vigil_status_t vigil_vm_fail_at_ip(vigil_vm_t *vm, vigil_status_t status, const char *message, vigil_error_t *error);
vigil_value_t vigil_vm_pop_or_nil(vigil_vm_t *vm);
vigil_status_t vigil_vm_push(vigil_vm_t *vm, const vigil_value_t *value, vigil_error_t *error);
vigil_status_t vigil_vm_grow_stack(vigil_vm_t *vm, size_t minimum_capacity, vigil_error_t *error);
vigil_status_t vigil_vm_grow_value_array(vigil_runtime_t *runtime, vigil_value_t **values, size_t *capacity,
                                         size_t minimum_capacity, vigil_error_t *error);
int vigil_vm_get_string_parts(const vigil_value_t *value, const char **out_text, size_t *out_length);
vigil_status_t vigil_vm_invoke_value_call(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error);
vigil_status_t vigil_vm_invoke_interface_call(vigil_vm_t *vm, vigil_vm_frame_t *frame, size_t interface_index,
                                              size_t method_index, size_t arg_count, vigil_error_t *error);
vigil_status_t vigil_vm_call_extern_fn(vigil_vm_t *vm, const char *desc, size_t desc_len, size_t arg_count,
                                       vigil_error_t *error);
void vigil_vm_parse_i32(vigil_vm_t *vm);
void vigil_vm_parse_f64(vigil_vm_t *vm);
void vigil_vm_parse_bool(vigil_vm_t *vm);
vigil_status_t vigil_vm_new_string_value(vigil_vm_t *vm, const char *text, size_t length, vigil_value_t *out_value,
                                         vigil_error_t *error);
vigil_status_t vigil_vm_make_error_value(vigil_vm_t *vm, int64_t kind, const char *message, size_t message_length,
                                         vigil_value_t *out_value, vigil_error_t *error);
vigil_status_t vigil_vm_make_ok_error_value(vigil_vm_t *vm, vigil_value_t *out_value, vigil_error_t *error);
int vigil_vm_find_substring(const char *text, size_t text_length, const char *needle, size_t needle_length,
                            size_t *out_index);
vigil_status_t vigil_vm_make_bounds_error_value(vigil_vm_t *vm, const char *message, vigil_value_t *out_value,
                                                vigil_error_t *error);
int vigil_vm_values_equal(const vigil_value_t *left, const vigil_value_t *right);
int vigil_vm_value_is_supported_map_key(const vigil_value_t *value);

/* ── Fast bytecode read macros ──────────────────────────────────── */

#define VIGIL_VM_FAST_READ_U32(code, ip, out)                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        (out) = (uint32_t)(code)[(ip) + 1U];                                                                           \
        (out) |= (uint32_t)(code)[(ip) + 2U] << 8U;                                                                    \
        (out) |= (uint32_t)(code)[(ip) + 3U] << 16U;                                                                   \
        (out) |= (uint32_t)(code)[(ip) + 4U] << 24U;                                                                   \
        (ip) += 5U;                                                                                                    \
    } while (0)

#define VIGIL_VM_FAST_READ_RAW_U32(code, ip, out)                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        (out) = (uint32_t)(code)[(ip)];                                                                                \
        (out) |= (uint32_t)(code)[(ip) + 1U] << 8U;                                                                    \
        (out) |= (uint32_t)(code)[(ip) + 2U] << 16U;                                                                   \
        (out) |= (uint32_t)(code)[(ip) + 3U] << 24U;                                                                   \
        (ip) += 4U;                                                                                                    \
    } while (0)

/* ── i32 overflow check macros ─────────────────────────────────── */

#if defined(__GNUC__) || defined(__clang__)
#define VIGIL_I32_ADD_OVERFLOW(a, b, r) __builtin_add_overflow(a, b, r)
#define VIGIL_I32_SUB_OVERFLOW(a, b, r) __builtin_sub_overflow(a, b, r)
#define VIGIL_I32_MUL_OVERFLOW(a, b, r) __builtin_mul_overflow(a, b, r)
#else
#define VIGIL_I32_ADD_OVERFLOW(a, b, r)                                                                                \
    (*(r) = (int32_t)((int64_t)(a) + (int64_t)(b)),                                                                    \
     (int64_t)(a) + (int64_t)(b) < (int64_t)INT32_MIN || (int64_t)(a) + (int64_t)(b) > (int64_t)INT32_MAX)
#define VIGIL_I32_SUB_OVERFLOW(a, b, r)                                                                                \
    (*(r) = (int32_t)((int64_t)(a) - (int64_t)(b)),                                                                    \
     (int64_t)(a) - (int64_t)(b) < (int64_t)INT32_MIN || (int64_t)(a) - (int64_t)(b) > (int64_t)INT32_MAX)
#define VIGIL_I32_MUL_OVERFLOW(a, b, r)                                                                                \
    (*(r) = (int32_t)((int64_t)(a) * (int64_t)(b)),                                                                    \
     (int64_t)(a) * (int64_t)(b) < (int64_t)INT32_MIN || (int64_t)(a) * (int64_t)(b) > (int64_t)INT32_MAX)
#endif

/* ── Checked arithmetic helpers (defined in vm.c) ──────────────── */

vigil_status_t vigil_vm_checked_add(int64_t left, int64_t right, int64_t *out_result);
vigil_status_t vigil_vm_checked_uadd(uint64_t left, uint64_t right, uint64_t *out_result);
vigil_status_t vigil_vm_checked_subtract(int64_t left, int64_t right, int64_t *out_result);
vigil_status_t vigil_vm_checked_usubtract(uint64_t left, uint64_t right, uint64_t *out_result);
vigil_status_t vigil_vm_checked_multiply(int64_t left, int64_t right, int64_t *out_result);
vigil_status_t vigil_vm_checked_umultiply(uint64_t left, uint64_t right, uint64_t *out_result);
vigil_status_t vigil_vm_checked_divide(int64_t left, int64_t right, int64_t *out_result);
vigil_status_t vigil_vm_checked_udivide(uint64_t left, uint64_t right, uint64_t *out_result);
vigil_status_t vigil_vm_checked_modulo(int64_t left, int64_t right, int64_t *out_result);
vigil_status_t vigil_vm_checked_umodulo(uint64_t left, uint64_t right, uint64_t *out_result);
vigil_status_t vigil_vm_checked_shift_left(int64_t left, int64_t right, int64_t *out_result);
vigil_status_t vigil_vm_checked_shift_right(int64_t left, int64_t right, int64_t *out_result);
vigil_status_t vigil_vm_checked_ushift_left(uint64_t left, uint64_t right, uint64_t *out_result);
vigil_status_t vigil_vm_checked_ushift_right(uint64_t left, uint64_t right, uint64_t *out_result);

/* Helpers for conversion / unary / binary ops (defined in vm.c). */
vigil_status_t vigil_vm_checked_negate(int64_t value, int64_t *out_result);
int vigil_vm_value_is_integer(const vigil_value_t *value);
vigil_status_t vigil_vm_concat_strings(vigil_vm_t *vm, const vigil_value_t *left, const vigil_value_t *right,
                                       vigil_value_t *out_value, vigil_error_t *error);
vigil_status_t vigil_vm_stringify_value(vigil_vm_t *vm, const vigil_value_t *value, vigil_value_t *out_value,
                                        vigil_error_t *error);
vigil_status_t vigil_vm_format_f64_value(vigil_vm_t *vm, const vigil_value_t *value, uint32_t precision,
                                         vigil_value_t *out_value, vigil_error_t *error);
vigil_status_t vigil_vm_format_spec_value(vigil_vm_t *vm, const vigil_value_t *val, uint32_t word1, uint32_t word2,
                                          vigil_value_t *out_value, vigil_error_t *error);
vigil_status_t vigil_vm_convert_to_signed_integer_type(vigil_vm_t *vm, const vigil_value_t *value, int64_t min_value,
                                                       int64_t max_value, const char *type_error_message,
                                                       const char *range_error_message, vigil_error_t *error);
vigil_status_t vigil_vm_convert_to_unsigned_integer_type(vigil_vm_t *vm, const vigil_value_t *value, uint64_t max_value,
                                                         const char *type_error_message,
                                                         const char *range_error_message, vigil_error_t *error);
vigil_status_t vigil_vm_read_u32(vigil_vm_t *vm, uint32_t *out_value, vigil_error_t *error);
vigil_status_t vigil_vm_read_raw_u32(vigil_vm_t *vm, uint32_t *out_value, vigil_error_t *error);

/* Execute a function call: push a frame for the callee and run the
   stack VM dispatch loop.  When the callee returns, its return values
   are left on the stack at base_slot.  The caller's frame remains
   intact.  Used by the register VM's CALL handler. */
vigil_status_t vigil_vm_execute_call(vigil_vm_t *vm, const vigil_object_t *callee, size_t arg_count,
                                     vigil_error_t *error);

#endif /* VIGIL_VM_INTERNAL_H */
