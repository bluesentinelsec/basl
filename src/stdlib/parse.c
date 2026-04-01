/* VIGIL standard library: parse module.
 *
 * Provides string-to-value parsing functions.
 *   parse.i32(string) -> (i32, err)
 *   parse.i64(string) -> (i64, err)
 *   parse.f64(string) -> (f64, err)
 *   parse.bool(string) -> (bool, err)
 */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/runtime.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_internal.h"

/* ── Helpers ─────────────────────────────────────────────────── */

static const char *get_string_arg(vigil_vm_t *vm, size_t base)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    vigil_object_t *obj = vigil_value_as_object(&v);
    if (obj == NULL || vigil_object_type(obj) != VIGIL_OBJECT_STRING)
        return NULL;
    return vigil_string_object_c_str(obj);
}

static vigil_status_t push_parse_err(vigil_vm_t *vm, const char *msg, vigil_error_t *error)
{
    /* err.parse = kind 8 */
    vigil_object_t *obj = NULL;
    vigil_status_t s = vigil_error_object_new_cstr(vigil_vm_runtime(vm), msg, 8, &obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t v;
    vigil_value_init_object(&v, &obj);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

/* ── Functions ───────────────────────────────────────────────── */

static vigil_status_t parse_i32_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *s = get_string_arg(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (s == NULL || *s == '\0')
    {
        vigil_value_t v;
        vigil_value_init_int(&v, 0);
        vigil_status_t st = vigil_vm_stack_push(vm, &v, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return push_parse_err(vm, "empty string", error);
    }
    char *end = NULL;
    errno = 0;
    long val = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || val < INT32_MIN || val > INT32_MAX)
    {
        vigil_value_t v;
        vigil_value_init_int(&v, 0);
        vigil_status_t st = vigil_vm_stack_push(vm, &v, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return push_parse_err(vm, "invalid integer", error);
    }
    vigil_value_t v;
    vigil_value_init_int(&v, (int64_t)val);
    vigil_status_t st = vigil_vm_stack_push(vm, &v, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t parse_i64_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *s = get_string_arg(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (s == NULL || *s == '\0')
    {
        vigil_value_t v;
        vigil_value_init_int(&v, 0);
        vigil_status_t st = vigil_vm_stack_push(vm, &v, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return push_parse_err(vm, "empty string", error);
    }
    char *end = NULL;
    errno = 0;
    long long val = strtoll(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0')
    {
        vigil_value_t v;
        vigil_value_init_int(&v, 0);
        vigil_status_t st = vigil_vm_stack_push(vm, &v, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return push_parse_err(vm, "invalid integer", error);
    }
    vigil_value_t v;
    vigil_value_init_int(&v, (int64_t)val);
    vigil_status_t st = vigil_vm_stack_push(vm, &v, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t parse_f64_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *s = get_string_arg(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (s == NULL || *s == '\0')
    {
        vigil_value_t v;
        vigil_value_init_float(&v, 0.0);
        vigil_status_t st = vigil_vm_stack_push(vm, &v, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return push_parse_err(vm, "empty string", error);
    }
    char *end = NULL;
    errno = 0;
    double val = strtod(s, &end);
    if (errno != 0 || end == s || *end != '\0')
    {
        vigil_value_t v;
        vigil_value_init_float(&v, 0.0);
        vigil_status_t st = vigil_vm_stack_push(vm, &v, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return push_parse_err(vm, "invalid float", error);
    }
    vigil_value_t v;
    vigil_value_init_float(&v, val);
    vigil_status_t st = vigil_vm_stack_push(vm, &v, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t parse_bool_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *s = get_string_arg(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (s == NULL)
    {
        vigil_value_t v;
        vigil_value_init_bool(&v, 0);
        vigil_status_t st = vigil_vm_stack_push(vm, &v, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return push_parse_err(vm, "invalid boolean", error);
    }
    if (strcmp(s, "true") == 0 || strcmp(s, "1") == 0)
    {
        vigil_value_t v;
        vigil_value_init_bool(&v, 1);
        vigil_status_t st = vigil_vm_stack_push(vm, &v, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
    }
    if (strcmp(s, "false") == 0 || strcmp(s, "0") == 0)
    {
        vigil_value_t v;
        vigil_value_init_bool(&v, 0);
        vigil_status_t st = vigil_vm_stack_push(vm, &v, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
    }
    vigil_value_t v;
    vigil_value_init_bool(&v, 0);
    vigil_status_t st = vigil_vm_stack_push(vm, &v, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return push_parse_err(vm, "invalid boolean", error);
}

/* ── Module definition ───────────────────────────────────────── */

static const int string_param[] = {VIGIL_TYPE_STRING};

static const int i32_err_returns[] = {VIGIL_TYPE_I32, VIGIL_TYPE_ERR};
static const int i64_err_returns[] = {VIGIL_TYPE_I64, VIGIL_TYPE_ERR};
static const int f64_err_returns[] = {VIGIL_TYPE_F64, VIGIL_TYPE_ERR};
static const int bool_err_returns[] = {VIGIL_TYPE_BOOL, VIGIL_TYPE_ERR};
static const char *const parse_string_param_names[] = {"s"};

static const vigil_native_symbol_doc_t vigil_parse_module_doc = {
    "String-to-value parsing.",
    "Parse strings into typed values with error handling.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_parse_i32_doc = {
    "Parse string to i32.",
    "Returns (value, error). Error is ok on success.",
    "i32 v, error err = parse.i32(\"42\")",
};

static const vigil_native_symbol_doc_t vigil_parse_i64_doc = {
    "Parse string to i64.",
    "Returns (value, error). Error is ok on success.",
    "i64 v, error err = parse.i64(\"123456789\")",
};

static const vigil_native_symbol_doc_t vigil_parse_f64_doc = {
    "Parse string to f64.",
    "Returns (value, error). Error is ok on success.",
    "f64 v, error err = parse.f64(\"3.14\")",
};

static const vigil_native_symbol_doc_t vigil_parse_bool_doc = {
    "Parse string to bool.",
    "Accepts \"true\" and \"false\". Returns (value, error).",
    "bool v, error err = parse.bool(\"true\")",
};

static const vigil_native_module_function_t parse_funcs[] = {
    {"i32", 3U, parse_i32_fn, 1U, string_param, VIGIL_TYPE_I32, 2U, i32_err_returns, 0, NULL, NULL, 0U,
     parse_string_param_names, NULL, NULL, &vigil_parse_i32_doc},
    {"i64", 3U, parse_i64_fn, 1U, string_param, VIGIL_TYPE_I64, 2U, i64_err_returns, 0, NULL, NULL, 0U,
     parse_string_param_names, NULL, NULL, &vigil_parse_i64_doc},
    {"f64", 3U, parse_f64_fn, 1U, string_param, VIGIL_TYPE_F64, 2U, f64_err_returns, 0, NULL, NULL, 0U,
     parse_string_param_names, NULL, NULL, &vigil_parse_f64_doc},
    {"bool", 4U, parse_bool_fn, 1U, string_param, VIGIL_TYPE_BOOL, 2U, bool_err_returns, 0, NULL, NULL, 0U,
     parse_string_param_names, NULL, NULL, &vigil_parse_bool_doc},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_parse = {
    "parse", 5U, parse_funcs, sizeof(parse_funcs) / sizeof(parse_funcs[0]), NULL, 0U, &vigil_parse_module_doc};
