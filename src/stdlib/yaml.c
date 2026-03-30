/* VIGIL standard library: yaml module.
 *
 * Provides YAML parsing with output as JSON strings.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vigil/json.h"
#include "vigil/native_module.h"
#include "vigil/runtime.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"
#include "vigil/yaml.h"

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"

/* ── Allocator helpers ────────────────────────────────────────────── */

static vigil_allocator_t get_alloc(vigil_vm_t *vm)
{
    const vigil_allocator_t *a = vigil_runtime_allocator(vigil_vm_runtime(vm));
    if (a != NULL)
        return *a;
    return vigil_default_allocator();
}

/* ── Helpers ─────────────────────────────────────────────────────── */

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

static vigil_status_t push_string(vigil_vm_t *vm, const char *str, size_t len, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_status_t s = vigil_string_object_new(vigil_vm_runtime(vm), str, len, &obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t val;
    vigil_value_init_object(&val, &obj);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return s;
}

/* ── yaml.parse(yaml: string) -> string ──────────────────────────── */

static vigil_status_t vigil_yaml_parse_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *yaml_str;
    size_t yaml_len;
    vigil_json_value_t *json = NULL;
    char *json_str = NULL;
    size_t json_len = 0;
    vigil_status_t s;

    if (!get_string_arg(vm, base, 0, &yaml_str, &yaml_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    vigil_allocator_t alloc = get_alloc(vm);

    s = vigil_yaml_parse(yaml_str, yaml_len, &alloc, &json, error);
    if (s != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    s = vigil_json_emit(json, &json_str, &json_len, error);
    vigil_json_free(&json);

    if (s != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    s = push_string(vm, json_str, json_len, error);
    alloc.deallocate(alloc.user_data, json_str);
    return s;
}

/* ── yaml.get(yaml: string, path: string) -> string ──────────────── */

static const vigil_json_value_t *yaml_navigate_path(const vigil_json_value_t *root, char *path)
{
    const vigil_json_value_t *current = root;
    char *tok = path;
    while (*tok && current)
    {
        while (*tok == '.')
            tok++;
        if (!*tok)
            break;
        if (*tok == '[')
        {
            tok++;
            size_t idx = (size_t)strtoul(tok, &tok, 10);
            if (*tok == ']')
                tok++;
            current = (vigil_json_type(current) == VIGIL_JSON_ARRAY) ? vigil_json_array_get(current, idx) : NULL;
        }
        else
        {
            char *end = tok;
            while (*end && *end != '.' && *end != '[')
                end++;
            char saved = *end;
            *end = '\0';
            current = (vigil_json_type(current) == VIGIL_JSON_OBJECT) ? vigil_json_object_get(current, tok) : NULL;
            *end = saved;
            tok = end;
        }
    }
    return current;
}

static vigil_status_t yaml_value_to_string(vigil_vm_t *vm, const vigil_json_value_t *val, vigil_error_t *error)
{
    switch (vigil_json_type(val))
    {
    case VIGIL_JSON_STRING:
        return push_string(vm, vigil_json_string_value(val), vigil_json_string_length(val), error);
    case VIGIL_JSON_NUMBER: {
        char buf[64];
        int n = snprintf(buf, sizeof(buf), "%g", vigil_json_number_value(val));
        return push_string(vm, buf, (size_t)n, error);
    }
    case VIGIL_JSON_BOOL:
        return push_string(vm, vigil_json_bool_value(val) ? "true" : "false", vigil_json_bool_value(val) ? 4 : 5,
                           error);
    case VIGIL_JSON_NULL:
        return push_string(vm, "null", 4, error);
    default: {
        char *result_str = NULL;
        size_t result_len = 0;
        vigil_status_t s = vigil_json_emit(val, &result_str, &result_len, error);
        if (s == VIGIL_STATUS_OK)
        {
            vigil_allocator_t alloc = get_alloc(vm);
            s = push_string(vm, result_str, result_len, error);
            alloc.deallocate(alloc.user_data, result_str);
        }
        return s;
    }
    }
}

static vigil_status_t vigil_yaml_get_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *yaml_str, *path_str;
    size_t yaml_len, path_len;
    vigil_json_value_t *json = NULL;
    vigil_status_t s;

    if (!get_string_arg(vm, base, 0, &yaml_str, &yaml_len) || !get_string_arg(vm, base, 1, &path_str, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    vigil_allocator_t alloc = get_alloc(vm);
    s = vigil_yaml_parse(yaml_str, yaml_len, &alloc, &json, error);
    if (s != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    char *path_copy = (char *)alloc.allocate(alloc.user_data, path_len + 1);
    if (!path_copy)
    {
        vigil_json_free(&json);
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    memcpy(path_copy, path_str, path_len);
    path_copy[path_len] = '\0';

    const vigil_json_value_t *current = yaml_navigate_path(json, path_copy);
    alloc.deallocate(alloc.user_data, path_copy);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (!current)
    {
        vigil_json_free(&json);
        return push_string(vm, "", 0, error);
    }

    s = yaml_value_to_string(vm, current, error);
    vigil_json_free(&json);
    return s;
}

/* ── Module definition ───────────────────────────────────────────── */

static const int str_param[] = {VIGIL_TYPE_STRING};
static const int str_str_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};

static const vigil_native_module_function_t vigil_yaml_functions[] = {
    {"parse", 5U, vigil_yaml_parse_fn, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U},
    {"get", 3U, vigil_yaml_get_fn, 2U, str_str_params, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U},
};

#define YAML_FUNCTION_COUNT (sizeof(vigil_yaml_functions) / sizeof(vigil_yaml_functions[0]))

VIGIL_API const vigil_native_module_t vigil_stdlib_yaml = {"yaml", 4U, vigil_yaml_functions, YAML_FUNCTION_COUNT,
                                                           NULL,   0U};
