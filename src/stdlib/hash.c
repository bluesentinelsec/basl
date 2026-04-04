/* hash.c — hash module: non-cryptographic hash functions. */

#include <stdint.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_nanbox.h"

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

/* hash.fnv1a(data: string) -> i64 */
static vigil_status_t hash_fnv1a(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t len;
    if (!get_str(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_value_t val = vigil_nanbox_encode_i32(0);
        return vigil_vm_stack_push(vm, &val, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < len; i++)
    {
        hash ^= (uint64_t)(unsigned char)data[i];
        hash *= UINT64_C(1099511628211);
    }

    vigil_value_t val = vigil_nanbox_encode_int((int64_t)hash);
    return vigil_vm_stack_push(vm, &val, error);
}

/* hash.djb2(data: string) -> i64 */
static vigil_status_t hash_djb2(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t len;
    if (!get_str(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_value_t val = vigil_nanbox_encode_i32(0);
        return vigil_vm_stack_push(vm, &val, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    uint64_t hash = 5381;
    for (size_t i = 0; i < len; i++)
        hash = ((hash << 5) + hash) + (uint64_t)(unsigned char)data[i];

    vigil_value_t val = vigil_nanbox_encode_int((int64_t)hash);
    return vigil_vm_stack_push(vm, &val, error);
}

/* ── Module descriptor ────────────────────────────────────────────── */

static const int p_str[] = {VIGIL_TYPE_STRING};

static const vigil_native_module_function_t hash_functions[] = {
    {"fnv1a", 5U, hash_fnv1a, 1U, p_str, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"djb2", 4U, hash_djb2, 1U, p_str, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_hash = {
    "hash", 4U, hash_functions, sizeof(hash_functions) / sizeof(hash_functions[0]), NULL, 0U, NULL, NULL, 0U};
