/*
 * transpile_rt.c — Runtime helpers for transpiled C code.
 */

#include "vigil/transpile_rt.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"
#include "internal/vigil_regvm.h"
#include "internal/vigil_vm_internal.h"
#include "vigil/value.h"
#include "vm_ops_collection.h"
#include "vm_ops_string.h"

static inline uint64_t tc_to_nanbox(uint64_t v);

vigil_status_t vigil_tc_to_string(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *src,
                                  vigil_error_t *error)
{
    vigil_value_t encoded = tc_to_nanbox(*src);
    return vigil_vm_stringify_value(tc->vm, &encoded, dst, error);
}

vigil_status_t vigil_tc_call_native(vigil_tc_t *tc, vigil_value_t *regs, uint8_t arg_base,
                                    uint8_t arg_count, uint32_t const_idx, vigil_error_t *error)
{
    vigil_vm_t *vm = tc->vm;
    const vigil_value_t *native_val;
    vigil_native_fn_t native_fn;
    vigil_status_t status;
    uint8_t i;

    if (const_idx >= tc->constant_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "transpile_rt: native constant index out of range");
        return VIGIL_STATUS_INTERNAL;
    }

    native_val = &tc->constants[const_idx];
    if (!vigil_nanbox_has_object(*native_val))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "transpile_rt: native constant is not an object");
        return VIGIL_STATUS_INTERNAL;
    }

    native_fn = vigil_native_function_get((vigil_object_t *)vigil_nanbox_decode_ptr(*native_val));
    if (native_fn == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "transpile_rt: not a native function");
        return VIGIL_STATUS_UNSUPPORTED;
    }

    /* Ensure the VM has a frame. Push a dummy frame if needed. */
    if (vm->frame_count == 0U)
    {
        status = vigil_vm_push_frame(vm, (vigil_object_t *)tc->function, (vigil_object_t *)tc->function, NULL, 0U,
                                     error);
        if (status != VIGIL_STATUS_OK)
            return status;
    }

    /* Ensure stack capacity and copy args. */
    {
        size_t needed = (size_t)arg_base + (size_t)arg_count;
        if (vm->stack_capacity < needed)
        {
            status = vigil_vm_grow_stack(vm, needed, error);
            if (status != VIGIL_STATUS_OK)
                return status;
        }
        for (i = 0; i < arg_count; i++)
        {
            vigil_value_t v = regs[arg_base + i];
            if (vigil_nanbox_is_object(v))
            {
                vigil_object_retain((vigil_object_t *)vigil_nanbox_decode_ptr(v));
                vm->stack[arg_base + i] = v;
            }
            else if (vigil_nanbox_is_int(v) || vigil_nanbox_is_bool(v) || v == VIGIL_NANBOX_NIL)
                vm->stack[arg_base + i] = v;
            else
                vm->stack[arg_base + i] = vigil_nanbox_encode_int((int64_t)v);
        }
        vm->stack_count = needed;
    }

    status = native_fn(vm, (size_t)arg_count, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    /* Copy return values back to registers and clear stack slots
       to prevent double-release during vigil_vm_close. */
    {
        size_t ri;
        for (ri = (size_t)arg_base; ri < vm->stack_count; ri++)
        {
            regs[ri] = vm->stack[ri];
            vm->stack[ri] = 0;
        }
    }

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_tc_new_error(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *msg,
                                  const vigil_value_t *kind, vigil_error_t *error)
{
    vigil_object_t *msg_obj;
    vigil_object_t *err_obj = NULL;
    const char *msg_str;
    size_t msg_len;
    int64_t kind_val;

    msg_obj = vigil_value_as_object(msg);
    msg_str = vigil_string_object_c_str(msg_obj);
    msg_len = vigil_string_object_length(msg_obj);
    kind_val = vigil_value_as_int(kind);

    vigil_status_t st = vigil_error_object_new(tc->runtime, msg_str, msg_len, kind_val, &err_obj, error);
    if (st != VIGIL_STATUS_OK)
        return st;

    vigil_value_init_object(dst, &err_obj);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_tc_get_error_kind(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *err_val,
                                       vigil_error_t *error)
{
    vigil_object_t *obj = vigil_value_as_object(err_val);
    (void)tc;
    (void)error;
    vigil_value_init_int(dst, vigil_error_object_kind(obj));
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_tc_get_error_msg(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *err_val,
                                      vigil_error_t *error)
{
    vigil_object_t *err_obj = vigil_value_as_object(err_val);
    const char *msg = vigil_error_object_message(err_obj);
    size_t len = vigil_error_object_message_length(err_obj);
    vigil_object_t *str_obj = NULL;
    vigil_status_t st;

    (void)tc;
    st = vigil_string_object_new(tc->runtime, msg, len, &str_obj, error);
    if (st != VIGIL_STATUS_OK)
        return st;

    vigil_value_init_object(dst, &str_obj);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_tc_format_f64(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *val,
                                   uint8_t precision, vigil_error_t *error)
{
    return vigil_vm_format_f64_value(tc->vm, val, (uint32_t)precision, dst, error);
}

vigil_status_t vigil_tc_parse_i32(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *src,
                                  vigil_error_t *error)
{
    vigil_object_t *obj = vigil_value_as_object(src);
    const char *s = vigil_string_object_c_str(obj);

    (void)error;
    if (s != NULL && *s != '\0')
    {
        char *end;
        errno = 0;
        long val = strtol(s, &end, 10);
        if (errno == 0 && end != s && *end == '\0' && val >= INT32_MIN && val <= INT32_MAX)
        {
            dst[0] = (uint64_t)(int64_t)val;
            dst[1] = vigil_runtime_ok_error_value(tc->runtime);
            return VIGIL_STATUS_OK;
        }
    }

    /* Parse failed — return error. */
    dst[0] = 0;
    {
        vigil_object_t *err_obj = NULL;
        vigil_status_t st = vigil_error_object_new_cstr(tc->runtime, "parse_i32: invalid input", 1, &err_obj, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        vigil_value_init_object(&dst[1], &err_obj);
    }
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_tc_parse_f64(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *src,
                                  vigil_error_t *error)
{
    vigil_object_t *obj = vigil_value_as_object(src);
    const char *s = vigil_string_object_c_str(obj);

    (void)error;
    if (s != NULL && *s != '\0')
    {
        char *end;
        errno = 0;
        double val = strtod(s, &end);
        if (errno == 0 && end != s && *end == '\0')
        {
            vigil_value_init_float(&dst[0], val);
            dst[1] = vigil_runtime_ok_error_value(tc->runtime);
            return VIGIL_STATUS_OK;
        }
    }

    vigil_value_init_float(&dst[0], 0.0);
    {
        vigil_object_t *err_obj = NULL;
        vigil_status_t st = vigil_error_object_new_cstr(tc->runtime, "parse_f64: invalid input", 1, &err_obj, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        vigil_value_init_object(&dst[1], &err_obj);
    }
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_tc_parse_bool(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *src,
                                   vigil_error_t *error)
{
    vigil_object_t *obj = vigil_value_as_object(src);
    const char *s = vigil_string_object_c_str(obj);
    size_t len = s ? vigil_string_object_length(obj) : 0;

    (void)error;
    if (len == 4 && memcmp(s, "true", 4) == 0)
    {
        vigil_value_init_bool(&dst[0], 1);
        dst[1] = vigil_runtime_ok_error_value(tc->runtime);
        return VIGIL_STATUS_OK;
    }
    if (len == 5 && memcmp(s, "false", 5) == 0)
    {
        vigil_value_init_bool(&dst[0], 0);
        dst[1] = vigil_runtime_ok_error_value(tc->runtime);
        return VIGIL_STATUS_OK;
    }

    vigil_value_init_bool(&dst[0], 0);
    {
        vigil_object_t *err_obj = NULL;
        vigil_status_t st =
            vigil_error_object_new_cstr(tc->runtime, "parse_bool: invalid input", 1, &err_obj, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        vigil_value_init_object(&dst[1], &err_obj);
    }
    return VIGIL_STATUS_OK;
}

/* ── Generic VM stack-op bridge ──────────────────────────────────── */

typedef vigil_status_t (*vm_op_fn)(vigil_vm_t *, vigil_vm_frame_t *, vigil_error_t *);
typedef vigil_status_t (*vm_op_code_fn)(vigil_vm_t *, vigil_vm_frame_t *, const uint8_t *, vigil_error_t *);

static vigil_status_t tc_ensure_frame(vigil_tc_t *tc, vigil_error_t *error)
{
    if (tc->vm->frame_count == 0U)
        return vigil_vm_push_frame(tc->vm, (vigil_object_t *)tc->function, (vigil_object_t *)tc->function, NULL, 0U,
                                   error);
    return VIGIL_STATUS_OK;
}

/* Convert a raw register value to a proper nanbox value for the VM stack.
   Returns the value unchanged if it's already a valid nanbox encoding,
   otherwise nanbox-encodes it as an integer. */
static inline uint64_t tc_to_nanbox(uint64_t v)
{
    if (vigil_nanbox_is_object(v) || vigil_nanbox_is_int(v) || vigil_nanbox_is_bool(v) || v == VIGIL_NANBOX_NIL)
        return v;
    return vigil_nanbox_encode_int((int64_t)v);
}

static vigil_status_t tc_sync_and_call(vigil_tc_t *tc, vigil_value_t *regs, uint8_t top_reg,
                                        uint8_t pop_count, uint8_t dst_reg, uint8_t ret_count,
                                        vm_op_fn op, vigil_error_t *error)
{
    vigil_vm_t *vm = tc->vm;
    vigil_status_t status;
    size_t needed = (size_t)top_reg + 1;

    status = tc_ensure_frame(tc, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    if (vm->stack_capacity < needed)
    {
        status = vigil_vm_grow_stack(vm, needed, error);
        if (status != VIGIL_STATUS_OK)
            return status;
    }

    /* Sync registers to VM stack, nanbox-encoding raw integers.
       Registers contain a mix of raw int64_t (from Phase 1 arithmetic)
       and nanboxed values (objects, tagged ints, bools).  We use strict
       nanbox type checks to identify already-encoded values; everything
       else is nanbox-encoded as int.  Objects are retained so the VM op
       can safely release its stack copy without freeing the register's
       reference. */
    for (size_t i = 0; i <= (size_t)top_reg; i++)
    {
        uint64_t v = regs[i];
        if (vigil_nanbox_is_object(v))
        {
            vigil_object_retain((vigil_object_t *)vigil_nanbox_decode_ptr(v));
            vm->stack[i] = v;
        }
        else
            vm->stack[i] = tc_to_nanbox(v);
    }
    vm->stack_count = needed;

    vigil_vm_frame_t *frame = &vm->frames[vm->frame_count - 1U];
    frame->ip = 0;
    status = op(vm, frame, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    {
        size_t result_base = (size_t)top_reg + 1U - (size_t)pop_count;
        for (uint8_t i = 0; i < ret_count; i++)
        {
            if (result_base + i < vm->stack_count)
            {
                regs[dst_reg + i] = vm->stack[result_base + i];
                vm->stack[result_base + i] = 0;
            }
        }
    }

    /* Clear remaining stack slots to prevent stale references. */
    {
        size_t si;
        for (si = 0; si <= (size_t)top_reg && si < vm->stack_count; si++)
            vm->stack[si] = 0;
    }

    return VIGIL_STATUS_OK;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
vigil_status_t vigil_tc_vm_op(vigil_tc_t *tc, vigil_value_t *regs, uint8_t opcode,
                              uint8_t a, uint8_t b, uint8_t c, vigil_error_t *error)
{
    vigil_vm_t *vm = tc->vm;
    vigil_status_t status;
    uint8_t top;

    status = tc_ensure_frame(tc, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    switch (opcode)
    {
    /* ── Core collection ops ─────────────────────────────────── */
    case VREG_NEW_ARRAY:
    {
        uint16_t count = (uint16_t)((uint16_t)b << 8 | (uint16_t)c);
        vigil_object_t *arr = NULL;
        /* The register file may contain raw int64_t values from Phase 1
           arithmetic.  Convert them to nanboxed values for the array API. */
        vigil_value_t items_buf[256];
        vigil_value_t *items = NULL;
        if (count > 0 && count <= 256)
        {
            for (uint16_t idx = 0; idx < count; idx++)
            {
                uint64_t v = regs[a + idx];
                items_buf[idx] = tc_to_nanbox(v);
            }
            items = items_buf;
        }
        status = vigil_array_object_new(vm->runtime, items, (size_t)count, &arr, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        vigil_value_release(&regs[a]);
        vigil_value_init_object(&regs[a], &arr);
        return VIGIL_STATUS_OK;
    }
    case VREG_NEW_MAP:
    {
        uint16_t pair_count = (uint16_t)((uint16_t)b << 8 | (uint16_t)c);
        vigil_object_t *map = NULL;
        status = vigil_map_object_new(vm->runtime, &map, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        for (uint16_t idx = 0; idx < pair_count; idx++)
        {
            vigil_value_t key = regs[a + idx * 2];
            vigil_value_t val = regs[a + idx * 2 + 1];
            /* Nanbox-encode raw integers if needed. */
            key = tc_to_nanbox(key);
            val = tc_to_nanbox(val);
            status = vigil_map_object_set(map, &key, &val, error);
            if (status != VIGIL_STATUS_OK)
            {
                vigil_object_release(&map);
                return status;
            }
        }
        vigil_value_release(&regs[a]);
        vigil_value_init_object(&regs[a], &map);
        return VIGIL_STATUS_OK;
    }
    case VREG_GET_INDEX:
        top = b > c ? b : c;
        return tc_sync_and_call(tc, regs, top, 2, a, 1, vigil_vm_op_get_index, error);
    case VREG_SET_INDEX:
        top = a;
        if (b > top) top = b;
        if (c > top) top = c;
        return tc_sync_and_call(tc, regs, top, 3, a, 0, vigil_vm_op_set_index, error);
    case VREG_COLLECTION_SIZE:
    {
        if (c == 1)
            status = tc_sync_and_call(tc, regs, b, 1, a, 1, vigil_vm_op_get_string_size, error);
        else
            status = tc_sync_and_call(tc, regs, b, 1, a, 1, vigil_vm_op_get_collection_size, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        /* Decode nanboxed integer result to raw int64_t for arithmetic. */
        if (vigil_nanbox_is_int(regs[a]))
            regs[a] = (uint64_t)vigil_nanbox_decode_int(regs[a]);
        return VIGIL_STATUS_OK;
    }

    /* ── Array methods ───────────────────────────────────────── */
    case VREG_ARRAY_PUSH:
        top = b > c ? b : c;
        return tc_sync_and_call(tc, regs, top, 2, a, 0, vigil_vm_op_array_push, error);
    case VREG_ARRAY_POP:
        return tc_sync_and_call(tc, regs, c, 2, a, 2, vigil_vm_op_array_pop, error);
    case VREG_ARRAY_GET_SAFE:
        return tc_sync_and_call(tc, regs, c, 3, a, 2, vigil_vm_op_array_get_safe, error);
    case VREG_ARRAY_SET_SAFE:
        return tc_sync_and_call(tc, regs, c, 3, a, 1, vigil_vm_op_array_set_safe, error);
    case VREG_ARRAY_SLICE:
        top = c;
        if (b > top) top = b;
        return tc_sync_and_call(tc, regs, top, 3, a, 1, vigil_vm_op_array_slice, error);
    case VREG_ARRAY_CONTAINS:
        top = b > c ? b : c;
        return tc_sync_and_call(tc, regs, top, 2, a, 1, vigil_vm_op_array_contains, error);
    case VREG_ARRAY_SORT:
        return tc_sync_and_call(tc, regs, a, 1, a, 0, vigil_vm_op_array_sort, error);
    case VREG_ARRAY_SORT_DESC:
        return tc_sync_and_call(tc, regs, a, 1, a, 0, vigil_vm_op_array_sort_desc, error);
    case VREG_ARRAY_REVERSE:
        return tc_sync_and_call(tc, regs, a, 1, a, 0, vigil_vm_op_array_reverse, error);
    case VREG_ARRAY_INDEX_OF:
        return tc_sync_and_call(tc, regs, c, 2, a, 1, vigil_vm_op_array_index_of, error);
    case VREG_ARRAY_REMOVE_AT:
        return tc_sync_and_call(tc, regs, c, 2, a, 2, vigil_vm_op_array_remove_at, error);
    case VREG_ARRAY_INSERT_AT:
        return tc_sync_and_call(tc, regs, c, 3, a, 1, vigil_vm_op_array_insert_at, error);
    case VREG_ARRAY_CLEAR:
        return tc_sync_and_call(tc, regs, a, 1, a, 0, vigil_vm_op_array_clear, error);

    /* ── Map methods ─────────────────────────────────────────── */
    case VREG_MAP_GET_SAFE:
        return tc_sync_and_call(tc, regs, c, 3, a, 2, vigil_vm_op_map_get_safe, error);
    case VREG_MAP_SET_SAFE:
        return tc_sync_and_call(tc, regs, c, 3, a, 1, vigil_vm_op_map_set_safe, error);
    case VREG_MAP_REMOVE_SAFE:
        return tc_sync_and_call(tc, regs, c, 3, a, 2, vigil_vm_op_map_remove_safe, error);
    case VREG_MAP_HAS:
        return tc_sync_and_call(tc, regs, c, 2, a, 1, vigil_vm_op_map_has, error);
    case VREG_MAP_KEYS:
    {
        uint8_t keys_op = VIGIL_OPCODE_MAP_KEYS;
        size_t needed = (size_t)b + 1;
        status = tc_ensure_frame(tc, error);
        if (status != VIGIL_STATUS_OK) return status;
        if (vm->stack_capacity < needed)
        { status = vigil_vm_grow_stack(vm, needed, error); if (status != VIGIL_STATUS_OK) return status; }
        for (size_t i = 0; i <= (size_t)b; i++)
        {
            uint64_t v = regs[i];
            if (v == 0 || vigil_nanbox_is_int(v) || vigil_nanbox_is_bool(v))
                vm->stack[i] = v;
            else if (vigil_nanbox_is_object(v))
            { vigil_object_retain((vigil_object_t *)vigil_nanbox_decode_ptr(v)); vm->stack[i] = v; }
            else
                vm->stack[i] = vigil_nanbox_encode_int((int64_t)v);
        }
        vm->stack_count = needed;
        { vigil_vm_frame_t *frame = &vm->frames[vm->frame_count - 1U];
          frame->ip = 0;
          status = vigil_vm_op_map_keys_values(vm, frame, &keys_op, error); }
        if (status != VIGIL_STATUS_OK) return status;
        { size_t rb = (size_t)b; if (rb < vm->stack_count) { regs[a] = vm->stack[rb]; vm->stack[rb] = 0; } }
        return VIGIL_STATUS_OK;
    }
    case VREG_MAP_VALUES:
    {
        uint8_t vals_op = VIGIL_OPCODE_MAP_VALUES;
        size_t needed = (size_t)b + 1;
        status = tc_ensure_frame(tc, error);
        if (status != VIGIL_STATUS_OK) return status;
        if (vm->stack_capacity < needed)
        { status = vigil_vm_grow_stack(vm, needed, error); if (status != VIGIL_STATUS_OK) return status; }
        for (size_t i = 0; i <= (size_t)b; i++)
        {
            uint64_t v = regs[i];
            if (v == 0 || vigil_nanbox_is_int(v) || vigil_nanbox_is_bool(v))
                vm->stack[i] = v;
            else if (vigil_nanbox_is_object(v))
            { vigil_object_retain((vigil_object_t *)vigil_nanbox_decode_ptr(v)); vm->stack[i] = v; }
            else
                vm->stack[i] = vigil_nanbox_encode_int((int64_t)v);
        }
        vm->stack_count = needed;
        { vigil_vm_frame_t *frame = &vm->frames[vm->frame_count - 1U];
          frame->ip = 0;
          status = vigil_vm_op_map_keys_values(vm, frame, &vals_op, error); }
        if (status != VIGIL_STATUS_OK) return status;
        { size_t rb = (size_t)b; if (rb < vm->stack_count) { regs[a] = vm->stack[rb]; vm->stack[rb] = 0; } }
        return VIGIL_STATUS_OK;
    }
    case VREG_MAP_KEY_AT:
        return tc_sync_and_call(tc, regs, c, 2, a, 1, vigil_vm_op_get_map_key_at, error);
    case VREG_MAP_VALUE_AT:
        return tc_sync_and_call(tc, regs, c, 2, a, 1, vigil_vm_op_get_map_value_at, error);
    case VREG_MAP_CLEAR:
        return tc_sync_and_call(tc, regs, a, 1, a, 0, vigil_vm_op_map_clear, error);

    /* ── Reference management ────────────────────────────────── */
    case VREG_DUP:
        regs[a] = vigil_value_copy(&regs[b]);
        return VIGIL_STATUS_OK;
    case VREG_RELEASE:
        vigil_value_release(&regs[a]);
        return VIGIL_STATUS_OK;

    /* ── Phase 4: Classes, interfaces ────────────────────────── */
    case VREG_NEW_INSTANCE:
        /* Handled specially by the transpiler (two-word instruction). */
        vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "transpile_rt: NEW_INSTANCE via vm_op not supported");
        return VIGIL_STATUS_UNSUPPORTED;
    case VREG_GET_FIELD:
    {
        vigil_object_t *obj;
        vigil_value_t fv = 0;
        if (!vigil_nanbox_is_object(regs[b]))
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "field access requires instance");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        obj = (vigil_object_t *)vigil_nanbox_decode_ptr(regs[b]);
        if (!vigil_instance_object_get_field(obj, (size_t)c, &fv))
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "invalid field index");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        vigil_value_release(&regs[a]);
        /* Decode nanboxed integers to raw int64_t for arithmetic compatibility. */
        if (vigil_nanbox_is_int(fv))
            regs[a] = (uint64_t)vigil_nanbox_decode_int(fv);
        else
            regs[a] = fv;
        return VIGIL_STATUS_OK;
    }
    case VREG_SET_FIELD:
    {
        vigil_object_t *obj;
        vigil_value_t val = regs[c];
        if (!vigil_nanbox_is_object(regs[a]))
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "field assignment requires instance");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        /* Nanbox-encode raw integer if needed. */
        val = tc_to_nanbox(val);
        obj = (vigil_object_t *)vigil_nanbox_decode_ptr(regs[a]);
        return vigil_instance_object_set_field(obj, (size_t)b, &val, error);
    }
    case VREG_CHAR_FROM_INT:
        return tc_sync_and_call(tc, regs, b, 1, a, 1, vigil_vm_op_char_from_int, error);

    /* ── Phase 5: Globals, captures, closures ────────────────── */
    case VREG_GET_GLOBAL:
    {
        uint16_t gidx = (uint16_t)((uint16_t)b << 8 | (uint16_t)c);
        vigil_value_t gval = 0;
        if (!vigil_function_object_get_global(tc->function, (size_t)gidx, &gval))
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "invalid global index");
            return VIGIL_STATUS_INTERNAL;
        }
        vigil_value_release(&regs[a]);
        regs[a] = vigil_nanbox_is_int(gval) ? (uint64_t)vigil_nanbox_decode_int(gval) : gval;
        return VIGIL_STATUS_OK;
    }
    case VREG_SET_GLOBAL:
    {
        uint16_t gidx = (uint16_t)((uint16_t)b << 8 | (uint16_t)c);
        vigil_value_t val = regs[a];
        val = tc_to_nanbox(val);
        return vigil_function_object_set_global(tc->function, (size_t)gidx, &val, error);
    }
    case VREG_GET_FUNCTION:
    {
        uint16_t fidx = (uint16_t)((uint16_t)b << 8 | (uint16_t)c);
        const vigil_object_t *fn = vigil_function_object_sibling(tc->function, (size_t)fidx);
        if (!fn)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "invalid function index");
            return VIGIL_STATUS_INTERNAL;
        }
        vigil_object_retain((vigil_object_t *)fn);
        vigil_value_release(&regs[a]);
        vigil_value_init_object(&regs[a], (vigil_object_t **)&fn);
        return VIGIL_STATUS_OK;
    }
    case VREG_NEW_CLOSURE:
    {
        const vigil_object_t *fn = vigil_function_object_sibling(tc->function, (size_t)b);
        vigil_object_t *closure = NULL;
        vigil_value_t caps[256];
        uint8_t ci;
        if (!fn)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "invalid function index for closure");
            return VIGIL_STATUS_INTERNAL;
        }
        for (ci = 0; ci < c; ci++)
        {
            uint64_t v = regs[a + ci];
            caps[ci] = tc_to_nanbox(v);
        }
        status = vigil_closure_object_new(vm->runtime, (vigil_object_t *)fn, caps, (size_t)c, &closure, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        vigil_value_release(&regs[a]);
        vigil_value_init_object(&regs[a], &closure);
        return VIGIL_STATUS_OK;
    }
    case VREG_GET_CAPTURE:
    case VREG_SET_CAPTURE:
        /* Captures require a closure callable -- stub for now. */
        return VIGIL_STATUS_OK;

    default:
        vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "transpile_rt: unsupported vm op");
        return VIGIL_STATUS_UNSUPPORTED;
    }
}

vigil_status_t vigil_tc_new_instance(vigil_tc_t *tc, vigil_value_t *regs, uint8_t dest,
                                     uint16_t class_idx, uint8_t fields_base, uint8_t field_count,
                                     vigil_error_t *error)
{
    vigil_object_t *inst = NULL;
    vigil_value_t fields_buf[256];
    vigil_value_t *fields = NULL;
    vigil_status_t status;

    if (field_count > 0)
    {
        for (uint8_t i = 0; i < field_count; i++)
        {
            uint64_t v = regs[fields_base + i];
            fields_buf[i] = tc_to_nanbox(v);
        }
        fields = fields_buf;
    }

    status = vigil_instance_object_new(tc->runtime, (size_t)class_idx, fields, (size_t)field_count, &inst, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    vigil_value_release(&regs[dest]);
    vigil_value_init_object(&regs[dest], &inst);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_tc_call_value(vigil_tc_t *tc, vigil_value_t *regs, uint8_t ret,
                                   uint16_t arg_count, uint8_t arg_base, vigil_error_t *error)
{
    vigil_vm_t *vm = tc->vm;
    vigil_status_t status;
    size_t total = (size_t)arg_count + 1U;
    size_t i;

    status = tc_ensure_frame(tc, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    if (vm->stack_capacity < (size_t)arg_base + total)
    {
        status = vigil_vm_grow_stack(vm, (size_t)arg_base + total, error);
        if (status != VIGIL_STATUS_OK)
            return status;
    }

    /* Sync registers: callee at arg_base, then args. */
    for (i = 0; i < total; i++)
    {
        uint64_t v = regs[arg_base + i];
        if (vigil_nanbox_is_object(v))
        {
            vigil_object_retain((vigil_object_t *)vigil_nanbox_decode_ptr(v));
            vm->stack[arg_base + i] = v;
        }
        else if (v == 0 || vigil_nanbox_is_int(v) || vigil_nanbox_is_bool(v))
            vm->stack[arg_base + i] = v;
        else
            vm->stack[arg_base + i] = vigil_nanbox_encode_int((int64_t)v);
    }
    vm->stack_count = (size_t)arg_base + total;

    /* Extract callee, shift args down. */
    vigil_value_t callee_val = vm->stack[arg_base];
    if (!vigil_nanbox_is_object(callee_val))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "call_value: not a callable");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    vigil_object_t *callee = (vigil_object_t *)vigil_nanbox_decode_ptr(callee_val);
    vigil_value_release(&vm->stack[arg_base]);
    if (arg_count > 0)
        memmove(&vm->stack[arg_base], &vm->stack[arg_base + 1], (size_t)arg_count * sizeof(vigil_value_t));
    vm->stack_count -= 1U;

    status = vigil_vm_execute_call(vm, callee, (size_t)arg_count, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    /* Copy results back and clear stack to prevent double-release. */
    for (i = (size_t)arg_base; i < vm->stack_count && i < (size_t)arg_base + total; i++)
    {
        vigil_value_t rv = vm->stack[i];
        regs[ret + (i - (size_t)arg_base)] = vigil_nanbox_is_int(rv) ? (uint64_t)vigil_nanbox_decode_int(rv) : rv;
        vm->stack[i] = 0;
    }
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_tc_call_extern(vigil_tc_t *tc, vigil_value_t *regs, uint8_t ret,
                                    uint8_t const_idx, uint8_t arg_count, uint8_t arg_base,
                                    vigil_error_t *error)
{
    vigil_vm_t *vm = tc->vm;
    vigil_status_t status;
    size_t i;

    if ((size_t)const_idx >= tc->constant_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "call_extern: invalid constant index");
        return VIGIL_STATUS_INTERNAL;
    }

    status = tc_ensure_frame(tc, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    if (vm->stack_capacity < (size_t)arg_base + (size_t)arg_count)
    {
        status = vigil_vm_grow_stack(vm, (size_t)arg_base + (size_t)arg_count, error);
        if (status != VIGIL_STATUS_OK)
            return status;
    }

    for (i = 0; i < (size_t)arg_count; i++)
    {
        uint64_t v = regs[arg_base + i];
        if (vigil_nanbox_is_object(v))
        {
            vigil_object_retain((vigil_object_t *)vigil_nanbox_decode_ptr(v));
            vm->stack[arg_base + i] = v;
        }
        else if (v == 0 || vigil_nanbox_is_int(v) || vigil_nanbox_is_bool(v))
            vm->stack[arg_base + i] = v;
        else
            vm->stack[arg_base + i] = vigil_nanbox_encode_int((int64_t)v);
    }
    vm->stack_count = (size_t)arg_base + (size_t)arg_count;

    const vigil_value_t *desc_val = &tc->constants[const_idx];
    if (!vigil_nanbox_has_object(*desc_val))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "call_extern: not an object constant");
        return VIGIL_STATUS_UNSUPPORTED;
    }
    vigil_object_t *desc_obj = (vigil_object_t *)vigil_nanbox_decode_ptr(*desc_val);
    const char *desc = vigil_string_object_c_str(desc_obj);
    size_t desc_len = vigil_string_object_length(desc_obj);

    status = vigil_vm_call_extern_fn(vm, desc, desc_len, (size_t)arg_count, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    for (i = (size_t)arg_base; i < vm->stack_count; i++)
    {
        vigil_value_t rv = vm->stack[i];
        regs[ret + (i - (size_t)arg_base)] = vigil_nanbox_is_int(rv) ? (uint64_t)vigil_nanbox_decode_int(rv) : rv;
        vm->stack[i] = 0;
    }
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_tc_call_interface(vigil_tc_t *tc, vigil_value_t *regs, uint8_t ret,
                                       uint8_t iface_idx, uint8_t arg_count,
                                       uint32_t method_idx, uint8_t arg_base,
                                       vigil_error_t *error)
{
    vigil_vm_t *vm = tc->vm;
    vigil_status_t status;
    size_t total = (size_t)arg_count + 1U;
    size_t i;

    status = tc_ensure_frame(tc, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    if (vm->stack_capacity < (size_t)arg_base + total)
    {
        status = vigil_vm_grow_stack(vm, (size_t)arg_base + total, error);
        if (status != VIGIL_STATUS_OK)
            return status;
    }

    for (i = 0; i < total; i++)
    {
        uint64_t v = regs[arg_base + i];
        if (vigil_nanbox_is_object(v))
        {
            vigil_object_retain((vigil_object_t *)vigil_nanbox_decode_ptr(v));
            vm->stack[arg_base + i] = v;
        }
        else if (v == 0 || vigil_nanbox_is_int(v) || vigil_nanbox_is_bool(v))
            vm->stack[arg_base + i] = v;
        else
            vm->stack[arg_base + i] = vigil_nanbox_encode_int((int64_t)v);
    }
    vm->stack_count = (size_t)arg_base + total;

    /* Resolve the interface method. */
    vigil_value_t receiver = vm->stack[arg_base];
    if (!vigil_nanbox_is_object(receiver))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "interface call requires instance");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    size_t class_index = vigil_instance_object_class_index(
        (vigil_object_t *)vigil_nanbox_decode_ptr(receiver));
    const vigil_object_t *callee = vigil_function_object_resolve_interface_method(
        tc->function, class_index, (size_t)iface_idx, (size_t)method_idx);
    if (!callee)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "interface method not found");
        return VIGIL_STATUS_INTERNAL;
    }

    status = vigil_vm_execute_call(vm, callee, total, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    for (i = (size_t)arg_base; i < vm->stack_count && i < (size_t)arg_base + total; i++)
    {
        vigil_value_t rv = vm->stack[i];
        regs[ret + (i - (size_t)arg_base)] = vigil_nanbox_is_int(rv) ? (uint64_t)vigil_nanbox_decode_int(rv) : rv;
        vm->stack[i] = 0;
    }
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_tc_format_spec(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *val,
                                    uint32_t word1, uint32_t word2, vigil_error_t *error)
{
    vigil_value_t nanboxed = *val;
    vigil_value_t result = 0;
    vigil_status_t status;

    /* Nanbox-encode raw integer if needed. */
    nanboxed = tc_to_nanbox(nanboxed);

    status = vigil_vm_format_spec_value(tc->vm, &nanboxed, word1, word2, &result, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    vigil_value_release(dst);
    *dst = result;
    return VIGIL_STATUS_OK;
}

int vigil_tc_values_equal(const vigil_value_t *regs, uint8_t b, uint8_t c)
{
    uint64_t lhs = regs[b], rhs = regs[c];
    /* Fast path: identical bits (covers same-pointer objects and equal ints). */
    if (lhs == rhs)
        return 1;
    /* If both are objects, use value equality. */
    if (vigil_nanbox_has_object(lhs) && vigil_nanbox_has_object(rhs))
        return vigil_vm_values_equal(&lhs, &rhs);
    /* Raw int comparison (Phase 1 arithmetic stores raw int64_t). */
    return 0;
}

vigil_status_t vigil_tc_generic_add(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *lhs,
                                     const vigil_value_t *rhs, vigil_error_t *error)
{
    /* String concatenation when both are objects. */
    if (vigil_nanbox_is_object(*lhs) && vigil_nanbox_is_object(*rhs))
    {
        vigil_value_t result;
        vigil_status_t st = vigil_vm_concat_strings(tc->vm, lhs, rhs, &result, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        *dst = result;
        return VIGIL_STATUS_OK;
    }
    /* Integer addition. */
    *dst = (uint64_t)((int64_t)*lhs + (int64_t)*rhs);
    return VIGIL_STATUS_OK;
}
