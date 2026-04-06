/* os.c — os module: environment variables, process info, platform detection. */

#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include "vigil/native_module.h"
#include "vigil/status.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"
#include "platform/platform.h"

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
}

/* os.platform() -> string */
static vigil_status_t os_platform(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
#if defined(__EMSCRIPTEN__)
    return push_cstr(vm, "wasm", error);
#elif defined(_WIN32)
    return push_cstr(vm, "windows", error);
#elif defined(__ANDROID__)
    return push_cstr(vm, "android", error);
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    return push_cstr(vm, "ios", error);
#elif defined(__APPLE__)
    return push_cstr(vm, "macos", error);
#elif defined(__linux__)
    return push_cstr(vm, "linux", error);
#else
    return push_cstr(vm, "unknown", error);
#endif
}

/* os.arch() -> string */
static vigil_status_t os_arch(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
#if defined(__EMSCRIPTEN__)
    return push_cstr(vm, "wasm32", error);
#elif defined(__aarch64__) || defined(_M_ARM64)
    return push_cstr(vm, "arm64", error);
#elif defined(__x86_64__) || defined(_M_X64)
    return push_cstr(vm, "x86_64", error);
#elif defined(__i386__) || defined(_M_IX86)
    return push_cstr(vm, "x86", error);
#elif defined(__arm__) || defined(_M_ARM)
    return push_cstr(vm, "arm", error);
#else
    return push_cstr(vm, "unknown", error);
#endif
}

/* ── Error helpers ───────────────────────────────────────────────── */

static vigil_status_t push_err(vigil_vm_t *vm, const char *msg, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_status_t s = vigil_error_object_new_cstr(vigil_vm_runtime(vm), msg, 0, &obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t v;
    vigil_value_init_object(&v, &obj);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

static vigil_status_t push_ok(vigil_vm_t *vm, vigil_error_t *error)
{
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

/* ── setenv / unsetenv / environ ─────────────────────────────────── */

/* os.setenv(key: string, value: string) -> error */
static vigil_status_t os_setenv(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *key, *val;
    size_t key_len, val_len;

    if (!get_string_arg(vm, base, 0, &key, &key_len) || !get_string_arg(vm, base, 1, &val, &val_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_err(vm, "setenv: invalid argument", error);
    }

    char key_buf[256], val_buf[4096];
    if (key_len >= sizeof(key_buf))
        key_len = sizeof(key_buf) - 1;
    if (val_len >= sizeof(val_buf))
        val_len = sizeof(val_buf) - 1;
    memcpy(key_buf, key, key_len);
    key_buf[key_len] = '\0';
    memcpy(val_buf, val, val_len);
    val_buf[val_len] = '\0';

    vigil_vm_stack_pop_n(vm, arg_count);

    if (vigil_platform_setenv(key_buf, val_buf, error) != VIGIL_STATUS_OK)
        return push_err(vm, "setenv failed", error);
    return push_ok(vm, error);
}

/* os.unsetenv(key: string) -> error */
static vigil_status_t os_unsetenv(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *key;
    size_t key_len;

    if (!get_string_arg(vm, base, 0, &key, &key_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_err(vm, "unsetenv: invalid argument", error);
    }

    char key_buf[256];
    if (key_len >= sizeof(key_buf))
        key_len = sizeof(key_buf) - 1;
    memcpy(key_buf, key, key_len);
    key_buf[key_len] = '\0';

    vigil_vm_stack_pop_n(vm, arg_count);

    if (vigil_platform_unsetenv(key_buf, error) != VIGIL_STATUS_OK)
        return push_err(vm, "unsetenv failed", error);
    return push_ok(vm, error);
}

/* os.environ() -> map[string]string */
static vigil_status_t os_environ(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_object_t *map = NULL;
    vigil_status_t s = vigil_map_object_new(rt, &map, error);
    if (s != VIGIL_STATUS_OK)
        return s;

    char **env = NULL;
    size_t env_count = 0;
    s = vigil_platform_environ(&env, &env_count, error);
    if (s != VIGIL_STATUS_OK)
    {
        vigil_object_release(&map);
        return s;
    }

    for (size_t i = 0; i < env_count; i++)
    {
        const char *eq = strchr(env[i], '=');
        if (!eq)
        {
            free(env[i]); /* alloc-check: exempt - platform-allocated */
            continue;
        }
        vigil_object_t *ko = NULL, *vo = NULL;
        vigil_value_t kv, vv;
        s = vigil_string_object_new(rt, env[i], (size_t)(eq - env[i]), &ko, error);
        if (s != VIGIL_STATUS_OK)
        {
            free(env[i]); /* alloc-check: exempt - platform-allocated */
            break;
        }
        s = vigil_string_object_new(rt, eq + 1, strlen(eq + 1), &vo, error);
        if (s != VIGIL_STATUS_OK)
        {
            vigil_object_release(&ko);
            free(env[i]); /* alloc-check: exempt - platform-allocated */
            break;
        }
        vigil_value_init_object(&kv, &ko);
        vigil_value_init_object(&vv, &vo);
        s = vigil_map_object_set(map, &kv, &vv, error);
        vigil_value_release(&kv);
        vigil_value_release(&vv);
        free(env[i]); /* alloc-check: exempt - platform-allocated */
        if (s != VIGIL_STATUS_OK)
            break;
    }

    /* Free remaining entries on error */
    if (s != VIGIL_STATUS_OK)
    {
        for (size_t j = 0; j < env_count; j++)
            free(env[j]); /* alloc-check: exempt - platform-allocated */
        free(env); /* alloc-check: exempt - platform-allocated */
        vigil_object_release(&map);
        return s;
    }
    free(env); /* alloc-check: exempt - platform-allocated */

    vigil_value_t mv;
    vigil_value_init_object(&mv, &map);
    s = vigil_vm_stack_push(vm, &mv, error);
    vigil_value_release(&mv);
    return s;
}

/* ── Module descriptor ────────────────────────────────────────────── */

static const int p_str[] = {VIGIL_TYPE_STRING};
static const int p_str_str[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int p_i32[] = {VIGIL_TYPE_I32};

static const vigil_native_module_function_t os_functions[] = {
    {"getenv", 6U, os_getenv, 1U, p_str, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"setenv", 6U, os_setenv, 2U, p_str_str, VIGIL_TYPE_ERR, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"unsetenv", 8U, os_unsetenv, 1U, p_str, VIGIL_TYPE_ERR, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"environ", 7U, os_environ, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"exit", 4U, os_exit_fn, 1U, p_i32, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"platform", 8U, os_platform, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"arch", 4U, os_arch, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_os = {
    "os", 2U, os_functions, sizeof(os_functions) / sizeof(os_functions[0]), NULL, 0U, NULL, NULL, 0U};
