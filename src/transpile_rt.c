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
#include "internal/vigil_vm_internal.h"
#include "vigil/value.h"

vigil_status_t vigil_tc_to_string(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *src,
                                  vigil_error_t *error)
{
    return vigil_vm_stringify_value(tc->vm, src, dst, error);
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
            vm->stack[arg_base + i] = regs[arg_base + i];
        vm->stack_count = needed;
    }

    status = native_fn(vm, (size_t)arg_count, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    /* Copy return values back to registers. */
    {
        size_t ri;
        for (ri = (size_t)arg_base; ri < vm->stack_count; ri++)
            regs[ri] = vm->stack[ri];
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
            vigil_value_init_int(&dst[0], (int64_t)val);
            dst[1] = vigil_runtime_ok_error_value(tc->runtime);
            return VIGIL_STATUS_OK;
        }
    }

    /* Parse failed — return error. */
    vigil_value_init_int(&dst[0], 0);
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
