/* VIGIL standard library: test module.
 *
 * Provides test.T class with assert() and fail() methods for use in
 * _test.vigil files.
 */
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_nanbox.h"

/* Thread-local buffer for test failure messages so the pointer survives
 * stack cleanup.  512 bytes is plenty for assertion messages. */
static char vigil_test_msg_buf[512];

static const char *vigil_test_capture_msg(vigil_vm_t *vm, size_t base, size_t arg_count, size_t msg_arg_index,
                                          const char *default_msg)
{
    if (arg_count > msg_arg_index)
    {
        vigil_value_t msg_val = vigil_vm_stack_get(vm, base + msg_arg_index);
        if (vigil_nanbox_is_object(msg_val))
        {
            const vigil_object_t *obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(msg_val);
            if (obj != NULL && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
            {
                const char *text = vigil_string_object_c_str(obj);
                size_t len = vigil_string_object_length(obj);
                if (text && len > 0)
                {
                    if (len >= sizeof(vigil_test_msg_buf))
                        len = sizeof(vigil_test_msg_buf) - 1;
                    memcpy(vigil_test_msg_buf, text, len);
                    vigil_test_msg_buf[len] = '\0';
                    return vigil_test_msg_buf;
                }
            }
        }
    }
    return default_msg;
}

/* ── T.assert(bool, string) ─────────────────────────────────────── */

static vigil_status_t vigil_test_assert(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t cond = vigil_vm_stack_get(vm, base + 1U);

    if (!vigil_nanbox_decode_bool(cond))
    {
        const char *msg = vigil_test_capture_msg(vm, base, arg_count, 2U, "assertion failed");
        vigil_vm_stack_pop_n(vm, arg_count);
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = msg;
            error->length = strlen(msg);
        }
        return VIGIL_STATUS_INTERNAL;
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    return VIGIL_STATUS_OK;
}

/* ── T.fail(string) ─────────────────────────────────────────────── */

static vigil_status_t vigil_test_fail(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *msg = vigil_test_capture_msg(vm, base, arg_count, 1U, "test failed");
    vigil_vm_stack_pop_n(vm, arg_count);
    if (error)
    {
        error->type = VIGIL_STATUS_INTERNAL;
        error->value = msg;
        error->length = strlen(msg);
    }
    return VIGIL_STATUS_INTERNAL;
}

/* ── module descriptor ───────────────────────────────────────────── */

static const int vigil_test_assert_params[] = {VIGIL_TYPE_BOOL, VIGIL_TYPE_STRING};
static const int vigil_test_fail_params[] = {VIGIL_TYPE_STRING};
static const char *const vigil_test_assert_param_names[] = {"condition", "message"};
static const char *const vigil_test_fail_param_names[] = {"message"};

static const vigil_native_symbol_doc_t vigil_test_module_doc = {
    "Testing utilities.",
    "The test module provides the test.T assertion helper used by VIGIL test files.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_test_t_doc = {
    "Test assertion context.",
    "Supplies assertion helpers to VIGIL test functions.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_test_assert_doc = {
    "Assert that a condition is true.",
    "Fails the current test with the provided message when the condition is false.",
    "t.assert(1 + 1 == 2, \"math still works\")",
};

static const vigil_native_symbol_doc_t vigil_test_fail_doc = {
    "Fail the current test.",
    "Unconditionally fails the current test with the provided message.",
    "t.fail(\"expected connection to close\")",
};

static const vigil_native_class_method_t vigil_test_t_methods[] = {
    {"assert", 6U, vigil_test_assert, 2U, vigil_test_assert_params, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, 0U, 0,
     vigil_test_assert_param_names, NULL, NULL, &vigil_test_assert_doc},
    {"fail", 4U, vigil_test_fail, 1U, vigil_test_fail_params, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, 0U, 0,
     vigil_test_fail_param_names, NULL, NULL, &vigil_test_fail_doc},
};

static const vigil_native_class_t vigil_test_classes[] = {
    {"T", 1U, NULL, 0U, vigil_test_t_methods, 2U, NULL, &vigil_test_t_doc},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_test = {
    "test", 4U, NULL, 0U, vigil_test_classes, 1U, &vigil_test_module_doc, NULL, 0U};
