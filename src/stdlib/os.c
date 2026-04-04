/* os.c — os module: environment variables, process info, platform detection. */

#include <stdlib.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/status.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_nanbox.h"

static bool get_string_arg(vigil_vm_t *vm, size_t base, size_t idx, const char **str, size_t *len)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    if (!vigil_nanbox_is_object(v))
        return false;
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(v);
    if (!obj || vigil_object_type(obj) != VIGIL_OBJECT_STRING)
        return false;
    *str = vigil_string_object_c_str(obj);
    *len = vigil_string_object_length(obj);
    return true;
}

static vigil_status_t push_string(vigil_vm_t *vm, const char *text, size_t len, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_object_t *obj = NULL;
    vigil_value_t val;
    status = vigil_string_object_new(vigil_vm_runtime(vm), text, len, &obj, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    vigil_value_init_object(&val, &obj);
    status = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return status;
}

static vigil_status_t push_cstr(vigil_vm_t *vm, const char *text, vigil_error_t *error)
{
    return push_string(vm, text, strlen(text), error);
}

/* os.getenv(key: string) -> string */
static vigil_status_t os_getenv(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *key_text;
    size_t key_len;

    if (!get_string_arg(vm, base, 0, &key_text, &key_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_cstr(vm, "", error);
    }

    char key_buf[256];
    if (key_len >= sizeof(key_buf))
        key_len = sizeof(key_buf) - 1U;
    memcpy(key_buf, key_text, key_len);
    key_buf[key_len] = '\0';

    vigil_vm_stack_pop_n(vm, arg_count);

    const char *val = getenv(key_buf);
    return push_cstr(vm, val != NULL ? val : "", error);
}

/* os.exit(code: i32) -> void */
static vigil_status_t os_exit_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t code_val = vigil_vm_stack_get(vm, base);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    int code = (int)vigil_value_as_int(&code_val);
    vigil_value_release(&code_val);
    exit(code);
    return VIGIL_STATUS_OK;
}

/* os.platform() -> string */
static vigil_status_t os_platform(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
#if defined(_WIN32)
    return push_cstr(vm, "windows", error);
#elif defined(__APPLE__)
    return push_cstr(vm, "macos", error);
#elif defined(__linux__)
    return push_cstr(vm, "linux", error);
#elif defined(__EMSCRIPTEN__)
    return push_cstr(vm, "wasm", error);
#else
    return push_cstr(vm, "unknown", error);
#endif
}

/* os.arch() -> string */
static vigil_status_t os_arch(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
#if defined(__aarch64__) || defined(_M_ARM64)
    return push_cstr(vm, "arm64", error);
#elif defined(__x86_64__) || defined(_M_X64)
    return push_cstr(vm, "x86_64", error);
#elif defined(__i386__) || defined(_M_IX86)
    return push_cstr(vm, "x86", error);
#elif defined(__arm__) || defined(_M_ARM)
    return push_cstr(vm, "arm", error);
#elif defined(__EMSCRIPTEN__)
    return push_cstr(vm, "wasm32", error);
#else
    return push_cstr(vm, "unknown", error);
#endif
}

/* ── Module descriptor ────────────────────────────────────────────── */

static const int p_str[] = {VIGIL_TYPE_STRING};
static const int p_i32[] = {VIGIL_TYPE_I32};

static const vigil_native_module_function_t os_functions[] = {
    {"getenv", 6U, os_getenv, 1U, p_str, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"exit", 4U, os_exit_fn, 1U, p_i32, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"platform", 8U, os_platform, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"arch", 4U, os_arch, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_os = {
    "os", 2U, os_functions, sizeof(os_functions) / sizeof(os_functions[0]), NULL, 0U, NULL, NULL, 0U};
