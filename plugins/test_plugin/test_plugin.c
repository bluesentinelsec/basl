/* Vigil plugin: test_plugin
 *
 * A minimal plugin used by the test suite to validate the plugin system.
 *
 * Functions:
 *   test_plugin.add(i32 a, i32 b) -> i32     returns a + b
 *   test_plugin.greet() -> string             returns "hello from plugin"
 *   test_plugin.negate(i32 a) -> i32          returns -a
 */
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_nanbox.h"

/* ── helpers ─────────────────────────────────────────────────────── */

static int32_t tp_pop_i32(vigil_vm_t *vm, size_t base, size_t idx)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    return (int32_t)vigil_nanbox_decode_int(v);
}

static vigil_status_t tp_push_i32(vigil_vm_t *vm, int32_t val, vigil_error_t *error)
{
    vigil_value_t v;
    vigil_value_init_int(&v, (int64_t)val);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t tp_push_string(vigil_vm_t *vm, const char *s, vigil_error_t *error)
{
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_object_t *obj = NULL;
    vigil_status_t st = vigil_string_object_new_cstr(rt, s, &obj, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_t val;
    vigil_value_init_object(&val, &obj);
    vigil_object_release(&obj);
    st = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return st;
}

/* ── native functions ────────────────────────────────────────────── */

static vigil_status_t tp_add(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t a = tp_pop_i32(vm, base, 0);
    int32_t b = tp_pop_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tp_push_i32(vm, a + b, error);
}

static vigil_status_t tp_greet(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    return tp_push_string(vm, "hello from plugin", error);
}

static vigil_status_t tp_negate(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t a = tp_pop_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tp_push_i32(vm, -a, error);
}

/* ── parameter type arrays ───────────────────────────────────────── */

static const int tp_i32_params[] = {VIGIL_TYPE_I32};
static const int tp_i32i32_params[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32};

/* ── function table ──────────────────────────────────────────────── */

static const vigil_native_module_function_t tp_functions[] = {
    {"add", 3U, tp_add, 2U, tp_i32i32_params, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"greet", 5U, tp_greet, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"negate", 6U, tp_negate, 1U, tp_i32_params, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
};

#define TP_FUNCTION_COUNT (sizeof(tp_functions) / sizeof(tp_functions[0]))

/* ── module export ───────────────────────────────────────────────── */

VIGIL_API const vigil_native_module_t vigil_plugin_test_plugin = {"test_plugin",     11U,  tp_functions,
                                                                  TP_FUNCTION_COUNT, NULL, 0U, NULL};
