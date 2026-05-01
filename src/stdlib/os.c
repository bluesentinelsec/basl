/* os.c — os module: environment variables, process control, platform detection. */

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

/* ── SIGINT / interrupted support ────────────────────────────────── */

/* Signal handling and PID are delegated to the platform layer. */

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

/* os.getenv(key: string) -> (string, bool) */
static vigil_status_t os_getenv(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *key_text;
    size_t key_len;

    if (!get_string_arg(vm, base, 0, &key_text, &key_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_status_t s = push_cstr(vm, "", error);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_value_t bv;
        vigil_value_init_bool(&bv, 0);
        return vigil_vm_stack_push(vm, &bv, error);
    }

    char key_buf[256];
    if (key_len >= sizeof(key_buf))
        key_len = sizeof(key_buf) - 1U;
    memcpy(key_buf, key_text, key_len);
    key_buf[key_len] = '\0';

    const char *val = getenv(key_buf);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_status_t s = push_cstr(vm, val != NULL ? val : "", error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t bv;
    vigil_value_init_bool(&bv, val != NULL);
    return vigil_vm_stack_push(vm, &bv, error);
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

/* os.exec(program: string, args: array<string>) -> (string, string, i32, err) */
static vigil_status_t os_exec(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *prog;
    size_t prog_len;
    vigil_status_t s;

    if (!get_string_arg(vm, base, 0, &prog, &prog_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        s = push_cstr(vm, "", error);
        if (s != VIGIL_STATUS_OK)
            return s;
        s = push_cstr(vm, "", error);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_value_t iv;
        vigil_value_init_int(&iv, -1);
        s = vigil_vm_stack_push(vm, &iv, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        return push_err(vm, "exec: invalid program argument", error);
    }

    /* Read the args array */
    vigil_value_t arr_val = vigil_vm_stack_get(vm, base + 1);
    const vigil_object_t *arr_obj = NULL;
    size_t arr_len = 0;
    if (vigil_nanbox_is_object(arr_val))
    {
        arr_obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(arr_val);
        if (arr_obj && vigil_object_type(arr_obj) == VIGIL_OBJECT_ARRAY)
            arr_len = vigil_array_object_length(arr_obj);
        else
            arr_obj = NULL;
    }

    /* Build argv: [program, args..., NULL] */
    const vigil_allocator_t *a = vigil_runtime_allocator(vigil_vm_runtime(vm));
    size_t argc = 1 + arr_len;
    const char **argv = a->allocate(a->user_data, (argc + 1) * sizeof(char *));
    if (!argv)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        s = push_cstr(vm, "", error);
        if (s != VIGIL_STATUS_OK)
            return s;
        s = push_cstr(vm, "", error);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_value_t iv;
        vigil_value_init_int(&iv, -1);
        s = vigil_vm_stack_push(vm, &iv, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        return push_err(vm, "exec: allocation failed", error);
    }

    /* Copy program name into a NUL-terminated buffer */
    char prog_buf[4096];
    if (prog_len >= sizeof(prog_buf))
        prog_len = sizeof(prog_buf) - 1;
    memcpy(prog_buf, prog, prog_len);
    prog_buf[prog_len] = '\0';
    argv[0] = prog_buf;

    /* Temporary buffers for each arg string (stack-allocated for small counts) */
    char arg_bufs[64][4096];
    for (size_t i = 0; i < arr_len && i < 64; i++)
    {
        vigil_value_t elem;
        if (vigil_array_object_get(arr_obj, i, &elem) && vigil_nanbox_is_object(elem))
        {
            vigil_object_t *so = (vigil_object_t *)vigil_nanbox_decode_ptr(elem);
            if (so && vigil_object_type(so) == VIGIL_OBJECT_STRING)
            {
                const char *es = vigil_string_object_c_str(so);
                size_t el = vigil_string_object_length(so);
                if (el >= sizeof(arg_bufs[0]))
                    el = sizeof(arg_bufs[0]) - 1;
                memcpy(arg_bufs[i], es, el);
                arg_bufs[i][el] = '\0';
                argv[1 + i] = arg_bufs[i];
            }
            else
            {
                argv[1 + i] = "";
            }
            vigil_value_release(&elem);
        }
        else
        {
            argv[1 + i] = "";
        }
    }
    argv[argc] = NULL;

    vigil_vm_stack_pop_n(vm, arg_count);

    char *out_stdout = NULL, *out_stderr = NULL;
    int exit_code = -1;
    s = vigil_platform_exec(a, (const char *const *)argv, &out_stdout, &out_stderr, &exit_code, error);
    a->deallocate(a->user_data, (void *)argv);

    vigil_status_t ps;
    ps = push_cstr(vm, out_stdout ? out_stdout : "", error);
    if (out_stdout)
        a->deallocate(a->user_data, out_stdout);
    if (ps != VIGIL_STATUS_OK)
    {
        if (out_stderr)
            a->deallocate(a->user_data, out_stderr);
        return ps;
    }

    ps = push_cstr(vm, out_stderr ? out_stderr : "", error);
    if (out_stderr)
        a->deallocate(a->user_data, out_stderr);
    if (ps != VIGIL_STATUS_OK)
        return ps;

    vigil_value_t iv;
    vigil_value_init_int(&iv, (int64_t)exit_code);
    ps = vigil_vm_stack_push(vm, &iv, error);
    if (ps != VIGIL_STATUS_OK)
        return ps;

    if (s != VIGIL_STATUS_OK)
        return push_err(vm, "exec: command failed", error);
    return push_ok(vm, error);
}

/* os.exec_streaming(program: string, args: array<string>) -> (i32, err) */
static vigil_status_t os_exec_streaming(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *prog;
    size_t prog_len;
    vigil_status_t s;

    if (!get_string_arg(vm, base, 0, &prog, &prog_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_value_t iv;
        vigil_value_init_int(&iv, -1);
        s = vigil_vm_stack_push(vm, &iv, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        return push_err(vm, "exec_streaming: invalid program argument", error);
    }

    vigil_value_t arr_val = vigil_vm_stack_get(vm, base + 1);
    const vigil_object_t *arr_obj = NULL;
    size_t arr_len = 0;
    if (vigil_nanbox_is_object(arr_val))
    {
        arr_obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(arr_val);
        if (arr_obj && vigil_object_type(arr_obj) == VIGIL_OBJECT_ARRAY)
            arr_len = vigil_array_object_length(arr_obj);
        else
            arr_obj = NULL;
    }

    const vigil_allocator_t *a = vigil_runtime_allocator(vigil_vm_runtime(vm));
    size_t argc = 1 + arr_len;
    const char **argv = a->allocate(a->user_data, (argc + 1) * sizeof(char *));
    if (!argv)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_value_t iv;
        vigil_value_init_int(&iv, -1);
        s = vigil_vm_stack_push(vm, &iv, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        return push_err(vm, "exec_streaming: allocation failed", error);
    }

    char prog_buf[4096];
    if (prog_len >= sizeof(prog_buf))
        prog_len = sizeof(prog_buf) - 1;
    memcpy(prog_buf, prog, prog_len);
    prog_buf[prog_len] = '\0';
    argv[0] = prog_buf;

    char arg_bufs[64][4096];
    for (size_t i = 0; i < arr_len && i < 64; i++)
    {
        vigil_value_t elem;
        if (vigil_array_object_get(arr_obj, i, &elem) && vigil_nanbox_is_object(elem))
        {
            vigil_object_t *so = (vigil_object_t *)vigil_nanbox_decode_ptr(elem);
            if (so && vigil_object_type(so) == VIGIL_OBJECT_STRING)
            {
                const char *es = vigil_string_object_c_str(so);
                size_t el = vigil_string_object_length(so);
                if (el >= sizeof(arg_bufs[0]))
                    el = sizeof(arg_bufs[0]) - 1;
                memcpy(arg_bufs[i], es, el);
                arg_bufs[i][el] = '\0';
                argv[1 + i] = arg_bufs[i];
            }
            else
            {
                argv[1 + i] = "";
            }
            vigil_value_release(&elem);
        }
        else
        {
            argv[1 + i] = "";
        }
    }
    argv[argc] = NULL;

    vigil_vm_stack_pop_n(vm, arg_count);

    int exit_code = -1;
    s = vigil_platform_exec_streaming((const char *const *)argv, &exit_code, error);
    a->deallocate(a->user_data, (void *)argv);

    vigil_value_t iv;
    vigil_value_init_int(&iv, (int64_t)exit_code);
    vigil_status_t ps = vigil_vm_stack_push(vm, &iv, error);
    if (ps != VIGIL_STATUS_OK)
        return ps;

    if (s != VIGIL_STATUS_OK)
        return push_err(vm, "exec_streaming: command failed", error);
    return push_ok(vm, error);
}

/* os.hostname() -> string */
static vigil_status_t os_hostname(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    const vigil_allocator_t *a = vigil_runtime_allocator(vigil_vm_runtime(vm));
    char *name = NULL;
    vigil_status_t s = vigil_platform_hostname(a, &name, error);
    if (s != VIGIL_STATUS_OK)
        return push_cstr(vm, "", error);
    s = push_cstr(vm, name, error);
    a->deallocate(a->user_data, name);
    return s;
}

/* os.pid() -> i32 */
static vigil_status_t os_pid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    int pid = vigil_platform_getpid();
    vigil_value_t v;
    vigil_value_init_int(&v, (int64_t)pid);
    return vigil_vm_stack_push(vm, &v, error);
}

/* os.interrupted() -> bool */
static vigil_status_t os_interrupted(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_platform_install_interrupt_handler();
    vigil_value_t v;
    vigil_value_init_bool(&v, vigil_platform_interrupted());
    return vigil_vm_stack_push(vm, &v, error);
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
        free(env);        /* alloc-check: exempt - platform-allocated */
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
static const int p_str_obj[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_OBJECT};

/* Return type arrays */
static const int str_bool_returns[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_BOOL};
static const int str_str_i32_err_returns[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_I32, VIGIL_TYPE_ERR};
static const int i32_err_returns[] = {VIGIL_TYPE_I32, VIGIL_TYPE_ERR};

/* Extended type info for exec params: (string, array<string>) */
static const vigil_native_type_t exec_params_ext[] = {VIGIL_NATIVE_TYPE_PRIMITIVE(VIGIL_TYPE_STRING),
                                                      VIGIL_NATIVE_TYPE_ARRAY(VIGIL_TYPE_STRING)};

/* Param name arrays */
static const char *const getenv_param_names[] = {"key"};
static const char *const setenv_param_names[] = {"key", "value"};
static const char *const unsetenv_param_names[] = {"key"};
static const char *const exit_param_names[] = {"code"};
static const char *const exec_param_names[] = {"program", "args"};

/* ── Doc strings ─────────────────────────────────────────────────── */

static const vigil_native_symbol_doc_t os_module_doc = {
    "Operating system interface.",
    "Environment variables, process control, platform detection, and process execution.",
    NULL,
};

static const vigil_native_symbol_doc_t os_getenv_doc = {
    "Get an environment variable.",
    "Returns the value and a boolean indicating whether the variable exists.",
    "val, ok := os.getenv(\"HOME\")",
};
static const vigil_native_symbol_doc_t os_setenv_doc = {
    "Set an environment variable.",
    "Sets the environment variable to the given value. Returns err.",
    "err := os.setenv(\"MY_VAR\", \"hello\")",
};
static const vigil_native_symbol_doc_t os_unsetenv_doc = {
    "Remove an environment variable.",
    "Removes the named environment variable. Returns err.",
    "err := os.unsetenv(\"MY_VAR\")",
};
static const vigil_native_symbol_doc_t os_environ_doc = {
    "Get all environment variables.",
    "Returns a map of all environment variable key-value pairs.",
    "env := os.environ()",
};
static const vigil_native_symbol_doc_t os_exit_doc = {
    "Terminate the process.",
    "Terminates the process immediately with the given exit code. Note: defer blocks are not executed.",
    "os.exit(1)",
};
static const vigil_native_symbol_doc_t os_platform_doc = {
    "Get the operating system name.",
    "Returns one of: \"linux\", \"macos\", \"windows\", \"ios\", \"android\", \"wasm\", \"unknown\".",
    "p := os.platform()",
};
static const vigil_native_symbol_doc_t os_arch_doc = {
    "Get the CPU architecture.",
    "Returns one of: \"x86_64\", \"arm64\", \"x86\", \"arm\", \"wasm32\", \"unknown\".",
    "a := os.arch()",
};
static const vigil_native_symbol_doc_t os_exec_doc = {
    "Execute a command and capture output.",
    "Runs a program with the given arguments and returns (stdout, stderr, exit_code, err).",
    "out, err_out, code, err := os.exec(\"ls\", [\"-la\"])",
};
static const vigil_native_symbol_doc_t os_exec_streaming_doc = {
    "Execute a command with inherited stdio.",
    "Runs a program with stdout/stderr flowing to the terminal. Returns (exit_code, err).",
    "code, err := os.exec_streaming(\"make\", [\"build\"])",
};
static const vigil_native_symbol_doc_t os_hostname_doc = {
    "Get the machine hostname.",
    "Returns the hostname of the current machine.",
    "name := os.hostname()",
};
static const vigil_native_symbol_doc_t os_pid_doc = {
    "Get the current process ID.",
    "Returns the PID of the running process.",
    "id := os.pid()",
};
static const vigil_native_symbol_doc_t os_interrupted_doc = {
    "Check if SIGINT was received.",
    "Returns true if the process has received an interrupt signal (Ctrl+C). Installs the signal handler on first call.",
    "if os.interrupted() { os.exit(1) }",
};

static const vigil_native_module_function_t os_functions[] = {
    {"getenv", 6U, os_getenv, 1U, p_str, VIGIL_TYPE_STRING, 2U, str_bool_returns, 0, NULL, NULL, 0U, getenv_param_names,
     NULL, NULL, &os_getenv_doc},
    {"setenv", 6U, os_setenv, 2U, p_str_str, VIGIL_TYPE_ERR, 1U, NULL, 0, NULL, NULL, 0U, setenv_param_names, NULL,
     NULL, &os_setenv_doc},
    {"unsetenv", 8U, os_unsetenv, 1U, p_str, VIGIL_TYPE_ERR, 1U, NULL, 0, NULL, NULL, 0U, unsetenv_param_names, NULL,
     NULL, &os_unsetenv_doc},
    {"environ", 7U, os_environ, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL, 0, NULL,
     &(const vigil_native_type_t)VIGIL_NATIVE_TYPE_MAP(VIGIL_TYPE_STRING, VIGIL_TYPE_STRING), 0U, NULL, NULL, NULL,
     &os_environ_doc},
    {"exit", 4U, os_exit_fn, 1U, p_i32, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U, exit_param_names, NULL, NULL,
     &os_exit_doc},
    {"platform", 8U, os_platform, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     &os_platform_doc},
    {"arch", 4U, os_arch, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, &os_arch_doc},
    {"exec", 4U, os_exec, 2U, p_str_obj, VIGIL_TYPE_STRING, 4U, str_str_i32_err_returns, 0, exec_params_ext, NULL, 0U,
     exec_param_names, NULL, NULL, &os_exec_doc},
    {"exec_streaming", 14U, os_exec_streaming, 2U, p_str_obj, VIGIL_TYPE_I32, 2U, i32_err_returns, 0, exec_params_ext,
     NULL, 0U, exec_param_names, NULL, NULL, &os_exec_streaming_doc},
    {"hostname", 8U, os_hostname, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     &os_hostname_doc},
    {"pid", 3U, os_pid, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, &os_pid_doc},
    {"interrupted", 11U, os_interrupted, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     &os_interrupted_doc},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_os = {
    "os", 2U, os_functions, sizeof(os_functions) / sizeof(os_functions[0]), NULL, 0U, &os_module_doc, NULL, 0U};
