/* VIGIL standard library: yaml module.
 *
 * YAML 1.2 subset parsing with JSON interop, query helpers, stringify,
 * and file I/O. All functions use (result, err) multi-return.
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

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

/* ── Stack helpers ────────────────────────────────────────────────── */

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

static vigil_status_t push_str_and_ok(vigil_vm_t *vm, const char *str, size_t len, vigil_error_t *error)
{
    vigil_status_t s = push_string(vm, str, len, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t push_str_and_err(vigil_vm_t *vm, const char *msg, vigil_error_t *error)
{
    vigil_status_t s = push_string(vm, "", 0, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_object_t *err_obj = NULL;
    s = vigil_error_object_new_cstr(vigil_vm_runtime(vm), msg, 1, &err_obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t v;
    vigil_value_init_object(&v, &err_obj);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

static vigil_status_t push_obj_and_ok(vigil_vm_t *vm, vigil_object_t **obj, vigil_error_t *error)
{
    vigil_value_t v;
    vigil_value_init_object(&v, obj);
    vigil_status_t s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t push_obj_and_err(vigil_vm_t *vm, vigil_object_t **empty, const char *msg, vigil_error_t *error)
{
    vigil_value_t v;
    vigil_value_init_object(&v, empty);
    vigil_status_t s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_object_t *err_obj = NULL;
    s = vigil_error_object_new_cstr(vigil_vm_runtime(vm), msg, 1, &err_obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_init_object(&v, &err_obj);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

static vigil_status_t push_i32_and_ok(vigil_vm_t *vm, int32_t val, vigil_error_t *error)
{
    vigil_value_t v;
    vigil_value_init_int(&v, (int64_t)val);
    vigil_status_t s = vigil_vm_stack_push(vm, &v, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t push_i32_and_err(vigil_vm_t *vm, const char *msg, vigil_error_t *error)
{
    vigil_value_t v;
    vigil_value_init_int(&v, 0);
    vigil_status_t s = vigil_vm_stack_push(vm, &v, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_object_t *err_obj = NULL;
    s = vigil_error_object_new_cstr(vigil_vm_runtime(vm), msg, 1, &err_obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_init_object(&v, &err_obj);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

static vigil_status_t push_bool(vigil_vm_t *vm, bool b, vigil_error_t *error)
{
    vigil_value_t v;
    vigil_value_init_bool(&v, b);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t push_ok_only(vigil_vm_t *vm, vigil_error_t *error)
{
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t push_err_only(vigil_vm_t *vm, const char *msg, vigil_error_t *error)
{
    vigil_object_t *err_obj = NULL;
    vigil_status_t s = vigil_error_object_new_cstr(vigil_vm_runtime(vm), msg, 1, &err_obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t v;
    vigil_value_init_object(&v, &err_obj);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

/* ── Path navigation (shared) ────────────────────────────────────── */

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

/* ── Value-to-string helper ──────────────────────────────────────── */

static vigil_status_t yaml_value_to_string(vigil_vm_t *vm, const vigil_json_value_t *val, vigil_error_t *error)
{
    switch (vigil_json_type(val))
    {
    case VIGIL_JSON_STRING:
        return push_str_and_ok(vm, vigil_json_string_value(val), vigil_json_string_length(val), error);
    case VIGIL_JSON_NUMBER: {
        char buf[64];
        int n = snprintf(buf, sizeof(buf), "%g", vigil_json_number_value(val));
        return push_str_and_ok(vm, buf, (size_t)n, error);
    }
    case VIGIL_JSON_BOOL:
        return push_str_and_ok(vm, vigil_json_bool_value(val) ? "true" : "false",
                               vigil_json_bool_value(val) ? 4 : 5, error);
    case VIGIL_JSON_NULL:
        return push_str_and_ok(vm, "null", 4, error);
    default: {
        char *result_str = NULL;
        size_t result_len = 0;
        vigil_status_t s = vigil_json_emit(val, &result_str, &result_len, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_allocator_t alloc = get_alloc(vm);
        s = push_str_and_ok(vm, result_str, result_len, error);
        alloc.deallocate(alloc.user_data, result_str);
        return s;
    }
    }
}

/* ── Parse + navigate helper ─────────────────────────────────────── */

static vigil_status_t yaml_parse_and_navigate(vigil_vm_t *vm, const char *yaml_str, size_t yaml_len,
                                              const char *path_str, size_t path_len,
                                              vigil_json_value_t **out_json, const vigil_json_value_t **out_node,
                                              vigil_error_t *error)
{
    vigil_allocator_t alloc = get_alloc(vm);
    vigil_status_t s = vigil_yaml_parse(yaml_str, yaml_len, &alloc, out_json, error);
    if (s != VIGIL_STATUS_OK)
        return s;

    if (path_len == 0)
    {
        *out_node = *out_json;
        return VIGIL_STATUS_OK;
    }

    char *path_copy = (char *)alloc.allocate(alloc.user_data, path_len + 1);
    if (!path_copy)
    {
        vigil_json_free(out_json);
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    memcpy(path_copy, path_str, path_len);
    path_copy[path_len] = '\0';

    *out_node = yaml_navigate_path(*out_json, path_copy);
    alloc.deallocate(alloc.user_data, path_copy);
    return VIGIL_STATUS_OK;
}

/* ══════════════════════════════════════════════════════════════════
 *  Public API functions
 * ══════════════════════════════════════════════════════════════════ */

/* ── yaml.parse(yaml) -> (string, err) ───────────────────────────── */

static vigil_status_t vigil_yaml_parse_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *yaml_str;
    size_t yaml_len;

    if (!get_string_arg(vm, base, 0, &yaml_str, &yaml_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "parse: expected string argument", error);
    }

    vigil_allocator_t alloc = get_alloc(vm);
    vigil_json_value_t *json = NULL;
    vigil_status_t s = vigil_yaml_parse(yaml_str, yaml_len, &alloc, &json, error);
    if (s != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "parse: invalid YAML", error);
    }

    char *json_str = NULL;
    size_t json_len = 0;
    s = vigil_json_emit(json, &json_str, &json_len, error);
    vigil_json_free(&json);
    if (s != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "parse: JSON emit failed", error);
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    s = push_str_and_ok(vm, json_str, json_len, error);
    alloc.deallocate(alloc.user_data, json_str);
    return s;
}

/* ── yaml.get(yaml, path) -> (string, err) ───────────────────────── */

static vigil_status_t vigil_yaml_get_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *yaml_str, *path_str;
    size_t yaml_len, path_len;

    if (!get_string_arg(vm, base, 0, &yaml_str, &yaml_len) || !get_string_arg(vm, base, 1, &path_str, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "get: invalid arguments", error);
    }

    vigil_json_value_t *json = NULL;
    const vigil_json_value_t *node = NULL;
    vigil_status_t s = yaml_parse_and_navigate(vm, yaml_str, yaml_len, path_str, path_len, &json, &node, error);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (s != VIGIL_STATUS_OK)
        return push_str_and_err(vm, "get: invalid YAML", error);
    if (!node)
    {
        vigil_json_free(&json);
        return push_str_and_err(vm, "get: path not found", error);
    }

    s = yaml_value_to_string(vm, node, error);
    vigil_json_free(&json);
    return s;
}

/* ── yaml.has(yaml, path) -> bool ────────────────────────────────── */

static vigil_status_t vigil_yaml_has_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *yaml_str, *path_str;
    size_t yaml_len, path_len;

    if (!get_string_arg(vm, base, 0, &yaml_str, &yaml_len) || !get_string_arg(vm, base, 1, &path_str, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, false, error);
    }

    vigil_json_value_t *json = NULL;
    const vigil_json_value_t *node = NULL;
    vigil_status_t s = yaml_parse_and_navigate(vm, yaml_str, yaml_len, path_str, path_len, &json, &node, error);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (s != VIGIL_STATUS_OK)
    {
        vigil_json_free(&json);
        return push_bool(vm, false, error);
    }
    bool found = (node != NULL);
    vigil_json_free(&json);
    return push_bool(vm, found, error);
}

/* ── yaml.type(yaml, path) -> string ─────────────────────────────── */

static vigil_status_t vigil_yaml_type_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *yaml_str, *path_str;
    size_t yaml_len, path_len;

    if (!get_string_arg(vm, base, 0, &yaml_str, &yaml_len) || !get_string_arg(vm, base, 1, &path_str, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "null", 4, error);
    }

    vigil_json_value_t *json = NULL;
    const vigil_json_value_t *node = NULL;
    vigil_status_t s = yaml_parse_and_navigate(vm, yaml_str, yaml_len, path_str, path_len, &json, &node, error);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (s != VIGIL_STATUS_OK || !node)
    {
        vigil_json_free(&json);
        return push_string(vm, "null", 4, error);
    }

    const char *type_str;
    switch (vigil_json_type(node))
    {
    case VIGIL_JSON_STRING:
        type_str = "string";
        break;
    case VIGIL_JSON_NUMBER:
        type_str = "number";
        break;
    case VIGIL_JSON_BOOL:
        type_str = "bool";
        break;
    case VIGIL_JSON_NULL:
        type_str = "null";
        break;
    case VIGIL_JSON_ARRAY:
        type_str = "array";
        break;
    case VIGIL_JSON_OBJECT:
        type_str = "object";
        break;
    default:
        type_str = "null";
        break;
    }
    vigil_json_free(&json);
    return push_string(vm, type_str, strlen(type_str), error);
}

/* ── yaml.keys(yaml, path) -> (array<string>, err) ───────────────── */

static vigil_status_t vigil_yaml_keys_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *yaml_str, *path_str;
    size_t yaml_len, path_len;

    if (!get_string_arg(vm, base, 0, &yaml_str, &yaml_len) || !get_string_arg(vm, base, 1, &path_str, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_object_t *empty = NULL;
        vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &empty, error);
        if (s != VIGIL_STATUS_OK) return s;
        return push_obj_and_err(vm, &empty, "keys: invalid arguments", error);
    }

    vigil_json_value_t *json = NULL;
    const vigil_json_value_t *node = NULL;
    vigil_status_t s = yaml_parse_and_navigate(vm, yaml_str, yaml_len, path_str, path_len, &json, &node, error);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (s != VIGIL_STATUS_OK || !node || vigil_json_type(node) != VIGIL_JSON_OBJECT)
    {
        vigil_json_free(&json);
        vigil_object_t *empty = NULL;
        s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &empty, error);
        if (s != VIGIL_STATUS_OK) return s;
        return push_obj_and_err(vm, &empty, "keys: path is not an object", error);
    }

    size_t count = vigil_json_object_count(node);
    vigil_object_t *arr = NULL;
    s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
    if (s != VIGIL_STATUS_OK)
    {
        vigil_json_free(&json);
        return s;
    }

    for (size_t i = 0; i < count; i++)
    {
        const char *key = NULL;
        size_t key_len = 0;
        const vigil_json_value_t *val = NULL;
        vigil_json_object_entry(node, i, &key, &key_len, &val);

        vigil_object_t *str_obj = NULL;
        s = vigil_string_object_new(vigil_vm_runtime(vm), key, key_len, &str_obj, error);
        if (s != VIGIL_STATUS_OK)
        {
            vigil_json_free(&json);
            return s;
        }
        vigil_value_t str_val;
        vigil_value_init_object(&str_val, &str_obj);
        s = vigil_array_object_append(arr, &str_val, error);
        vigil_value_release(&str_val);
        if (s != VIGIL_STATUS_OK)
        {
            vigil_json_free(&json);
            return s;
        }
    }

    vigil_json_free(&json);
    return push_obj_and_ok(vm, &arr, error);
}

/* ── yaml.len(yaml, path) -> (i32, err) ──────────────────────────── */

static vigil_status_t vigil_yaml_len_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *yaml_str, *path_str;
    size_t yaml_len, path_len;

    if (!get_string_arg(vm, base, 0, &yaml_str, &yaml_len) || !get_string_arg(vm, base, 1, &path_str, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i32_and_err(vm, "len: invalid arguments", error);
    }

    vigil_json_value_t *json = NULL;
    const vigil_json_value_t *node = NULL;
    vigil_status_t s = yaml_parse_and_navigate(vm, yaml_str, yaml_len, path_str, path_len, &json, &node, error);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (s != VIGIL_STATUS_OK || !node)
    {
        vigil_json_free(&json);
        return push_i32_and_err(vm, "len: path not found", error);
    }

    int32_t result = 0;
    switch (vigil_json_type(node))
    {
    case VIGIL_JSON_ARRAY:
        result = (int32_t)vigil_json_array_count(node);
        break;
    case VIGIL_JSON_OBJECT:
        result = (int32_t)vigil_json_object_count(node);
        break;
    case VIGIL_JSON_STRING:
        result = (int32_t)vigil_json_string_length(node);
        break;
    default:
        vigil_json_free(&json);
        return push_i32_and_err(vm, "len: value has no length", error);
    }

    vigil_json_free(&json);
    return push_i32_and_ok(vm, result, error);
}

/* ── YAML stringify (JSON -> YAML) ───────────────────────────────── */

static void yaml_emit_indent(char **buf, size_t *len, size_t *cap, const vigil_allocator_t *alloc, size_t indent)
{
    for (size_t i = 0; i < indent; i++)
    {
        if (*len + 1 >= *cap)
        {
            *cap = *cap ? *cap * 2 : 256;
            *buf = (char *)alloc->reallocate(alloc->user_data, *buf, *cap);
        }
        (*buf)[(*len)++] = ' ';
    }
}

static void yaml_emit_str(char **buf, size_t *len, size_t *cap, const vigil_allocator_t *alloc, const char *s,
                           size_t slen)
{
    while (*len + slen + 1 >= *cap)
    {
        *cap = *cap ? *cap * 2 : 256;
        *buf = (char *)alloc->reallocate(alloc->user_data, *buf, *cap);
    }
    memcpy(*buf + *len, s, slen);
    *len += slen;
}

static int yaml_string_needs_quoting(const char *s, size_t len)
{
    if (len == 0)
        return 1;
    if (len == 4 && (strncmp(s, "true", 4) == 0 || strncmp(s, "null", 4) == 0))
        return 1;
    if (len == 5 && strncmp(s, "false", 5) == 0)
        return 1;
    if (len == 1 && s[0] == '~')
        return 1;
    /* Check if it looks numeric */
    char *endptr;
    strtod(s, &endptr);
    if (endptr == s + len)
        return 1;
    for (size_t i = 0; i < len; i++)
    {
        if (s[i] == ':' || s[i] == '#' || s[i] == '[' || s[i] == ']' || s[i] == '{' || s[i] == '}' || s[i] == ',' ||
            s[i] == '\n' || s[i] == '"' || s[i] == '\'' || s[i] == '&' || s[i] == '*' || s[i] == '!' || s[i] == '|' ||
            s[i] == '>' || s[i] == '%' || s[i] == '@' || s[i] == '`')
            return 1;
    }
    if (s[0] == ' ' || s[len - 1] == ' ' || s[0] == '-')
        return 1;
    return 0;
}

static void yaml_emit_value(const vigil_json_value_t *val, char **buf, size_t *len, size_t *cap,
                             const vigil_allocator_t *alloc, size_t indent, int is_root);

static void yaml_emit_quoted(char **buf, size_t *len, size_t *cap, const vigil_allocator_t *alloc, const char *s,
                              size_t slen)
{
    yaml_emit_str(buf, len, cap, alloc, "\"", 1);
    for (size_t i = 0; i < slen; i++)
    {
        if (s[i] == '"')
            yaml_emit_str(buf, len, cap, alloc, "\\\"", 2);
        else if (s[i] == '\\')
            yaml_emit_str(buf, len, cap, alloc, "\\\\", 2);
        else if (s[i] == '\n')
            yaml_emit_str(buf, len, cap, alloc, "\\n", 2);
        else if (s[i] == '\t')
            yaml_emit_str(buf, len, cap, alloc, "\\t", 2);
        else if (s[i] == '\r')
            yaml_emit_str(buf, len, cap, alloc, "\\r", 2);
        else
        {
            if (*len + 1 >= *cap)
            {
                *cap = *cap ? *cap * 2 : 256;
                *buf = (char *)alloc->reallocate(alloc->user_data, *buf, *cap);
            }
            (*buf)[(*len)++] = s[i];
        }
    }
    yaml_emit_str(buf, len, cap, alloc, "\"", 1);
}

static void yaml_emit_value(const vigil_json_value_t *val, char **buf, size_t *len, size_t *cap,
                             const vigil_allocator_t *alloc, size_t indent, int is_root)
{
    switch (vigil_json_type(val))
    {
    case VIGIL_JSON_NULL:
        yaml_emit_str(buf, len, cap, alloc, "null", 4);
        break;
    case VIGIL_JSON_BOOL:
        if (vigil_json_bool_value(val))
            yaml_emit_str(buf, len, cap, alloc, "true", 4);
        else
            yaml_emit_str(buf, len, cap, alloc, "false", 5);
        break;
    case VIGIL_JSON_NUMBER: {
        char nbuf[64];
        int n = snprintf(nbuf, sizeof(nbuf), "%g", vigil_json_number_value(val));
        yaml_emit_str(buf, len, cap, alloc, nbuf, (size_t)n);
        break;
    }
    case VIGIL_JSON_STRING: {
        const char *s = vigil_json_string_value(val);
        size_t slen = vigil_json_string_length(val);
        if (yaml_string_needs_quoting(s, slen))
            yaml_emit_quoted(buf, len, cap, alloc, s, slen);
        else
            yaml_emit_str(buf, len, cap, alloc, s, slen);
        break;
    }
    case VIGIL_JSON_OBJECT: {
        size_t count = vigil_json_object_count(val);
        if (count == 0)
        {
            yaml_emit_str(buf, len, cap, alloc, "{}", 2);
            break;
        }
        for (size_t i = 0; i < count; i++)
        {
            const char *key = NULL;
            size_t key_len = 0;
            const vigil_json_value_t *child = NULL;
            vigil_json_object_entry(val, i, &key, &key_len, &child);

            if (i > 0 || !is_root)
            {
                yaml_emit_str(buf, len, cap, alloc, "\n", 1);
                yaml_emit_indent(buf, len, cap, alloc, indent);
            }
            yaml_emit_str(buf, len, cap, alloc, key, key_len);
            yaml_emit_str(buf, len, cap, alloc, ":", 1);

            vigil_json_type_t ct = vigil_json_type(child);
            if (ct == VIGIL_JSON_OBJECT || ct == VIGIL_JSON_ARRAY)
            {
                yaml_emit_value(child, buf, len, cap, alloc, indent + 2, 0);
            }
            else
            {
                yaml_emit_str(buf, len, cap, alloc, " ", 1);
                yaml_emit_value(child, buf, len, cap, alloc, indent + 2, 0);
            }
        }
        break;
    }
    case VIGIL_JSON_ARRAY: {
        size_t count = vigil_json_array_count(val);
        if (count == 0)
        {
            yaml_emit_str(buf, len, cap, alloc, "[]", 2);
            break;
        }
        for (size_t i = 0; i < count; i++)
        {
            const vigil_json_value_t *child = vigil_json_array_get(val, i);
            yaml_emit_str(buf, len, cap, alloc, "\n", 1);
            yaml_emit_indent(buf, len, cap, alloc, indent);
            yaml_emit_str(buf, len, cap, alloc, "- ", 2);

            vigil_json_type_t ct = vigil_json_type(child);
            if (ct == VIGIL_JSON_OBJECT)
            {
                /* First key on same line as dash */
                size_t oc = vigil_json_object_count(child);
                for (size_t j = 0; j < oc; j++)
                {
                    const char *k = NULL;
                    size_t kl = 0;
                    const vigil_json_value_t *cv = NULL;
                    vigil_json_object_entry(child, j, &k, &kl, &cv);
                    if (j > 0)
                    {
                        yaml_emit_str(buf, len, cap, alloc, "\n", 1);
                        yaml_emit_indent(buf, len, cap, alloc, indent + 2);
                    }
                    yaml_emit_str(buf, len, cap, alloc, k, kl);
                    yaml_emit_str(buf, len, cap, alloc, ":", 1);
                    vigil_json_type_t cvt = vigil_json_type(cv);
                    if (cvt == VIGIL_JSON_OBJECT || cvt == VIGIL_JSON_ARRAY)
                        yaml_emit_value(cv, buf, len, cap, alloc, indent + 4, 0);
                    else
                    {
                        yaml_emit_str(buf, len, cap, alloc, " ", 1);
                        yaml_emit_value(cv, buf, len, cap, alloc, indent + 4, 0);
                    }
                }
            }
            else
            {
                yaml_emit_value(child, buf, len, cap, alloc, indent + 2, 0);
            }
        }
        break;
    }
    }
}

/* ── yaml.stringify(json) -> (string, err) ───────────────────────── */

static vigil_status_t vigil_yaml_stringify_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *json_str;
    size_t json_len;

    if (!get_string_arg(vm, base, 0, &json_str, &json_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "stringify: expected string argument", error);
    }

    vigil_allocator_t alloc = get_alloc(vm);
    vigil_json_value_t *json = NULL;
    vigil_status_t s = vigil_json_parse(&alloc, json_str, json_len, &json, error);
    if (s != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "stringify: invalid JSON", error);
    }

    char *buf = NULL;
    size_t cap = 0, len = 0;
    yaml_emit_value(json, &buf, &len, &cap, &alloc, 0, 1);
    vigil_json_free(&json);

    if (buf)
    {
        /* Add trailing newline */
        yaml_emit_str(&buf, &len, &cap, &alloc, "\n", 1);
        buf[len] = '\0';
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    s = push_str_and_ok(vm, buf ? buf : "\n", buf ? len : 1, error);
    alloc.deallocate(alloc.user_data, buf);
    return s;
}

/* ── yaml.set(yaml, path, value) -> (string, err) ────────────────── */

static vigil_status_t vigil_yaml_set_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *yaml_str, *path_str, *val_str;
    size_t yaml_len, path_len, val_len;

    if (!get_string_arg(vm, base, 0, &yaml_str, &yaml_len) || !get_string_arg(vm, base, 1, &path_str, &path_len) ||
        !get_string_arg(vm, base, 2, &val_str, &val_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "set: invalid arguments", error);
    }

    vigil_allocator_t alloc = get_alloc(vm);
    vigil_json_value_t *json = NULL;
    vigil_status_t s = vigil_yaml_parse(yaml_str, yaml_len, &alloc, &json, error);
    if (s != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "set: invalid YAML", error);
    }

    /* Navigate to parent, then set the final key */
    char *path_copy = (char *)alloc.allocate(alloc.user_data, path_len + 1);
    if (!path_copy)
    {
        vigil_json_free(&json);
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "set: out of memory", error);
    }
    memcpy(path_copy, path_str, path_len);
    path_copy[path_len] = '\0';

    /* Find the last segment */
    char *last_dot = NULL;
    char *last_bracket = NULL;
    for (char *c = path_copy; *c; c++)
    {
        if (*c == '.')
            last_dot = c;
        if (*c == '[')
            last_bracket = c;
    }

    vigil_json_value_t *parent = json;
    const char *final_key = path_copy;
    size_t final_key_len = path_len;

    if (last_dot && (!last_bracket || last_dot > last_bracket))
    {
        *last_dot = '\0';
        parent = (vigil_json_value_t *)yaml_navigate_path(json, path_copy);
        final_key = last_dot + 1;
        final_key_len = strlen(final_key);
    }
    else if (last_bracket && (!last_dot || last_bracket > last_dot))
    {
        *last_bracket = '\0';
        if (path_copy[0])
            parent = (vigil_json_value_t *)yaml_navigate_path(json, path_copy);
        final_key = last_bracket + 1;
        final_key_len = strlen(final_key);
    }

    if (!parent)
    {
        alloc.deallocate(alloc.user_data, path_copy);
        vigil_json_free(&json);
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "set: parent path not found", error);
    }

    /* Parse the value string as a YAML scalar */
    vigil_json_value_t *new_val = NULL;
    s = vigil_yaml_parse(val_str, val_len, &alloc, &new_val, error);
    if (s != VIGIL_STATUS_OK)
    {
        alloc.deallocate(alloc.user_data, path_copy);
        vigil_json_free(&json);
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "set: invalid value", error);
    }

    if (vigil_json_type(parent) == VIGIL_JSON_OBJECT)
    {
        s = vigil_json_object_set(parent, final_key, final_key_len, new_val, error);
    }
    else
    {
        vigil_json_free(&new_val);
        s = VIGIL_STATUS_INVALID_ARGUMENT;
    }

    alloc.deallocate(alloc.user_data, path_copy);

    if (s != VIGIL_STATUS_OK)
    {
        vigil_json_free(&json);
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "set: could not set value", error);
    }

    /* Emit updated JSON */
    char *json_out = NULL;
    size_t json_out_len = 0;
    s = vigil_json_emit(json, &json_out, &json_out_len, error);
    vigil_json_free(&json);

    vigil_vm_stack_pop_n(vm, arg_count);
    if (s != VIGIL_STATUS_OK)
        return push_str_and_err(vm, "set: emit failed", error);

    s = push_str_and_ok(vm, json_out, json_out_len, error);
    alloc.deallocate(alloc.user_data, json_out);
    return s;
}

/* ── File I/O helpers ────────────────────────────────────────────── */

static vigil_status_t yaml_read_file(const vigil_allocator_t *alloc, const char *path, char **out_data,
                                     size_t *out_len, const char **out_msg, vigil_error_t *error)
{
    FILE *file = NULL;
    *out_data = NULL;
    *out_len = 0;
    *out_msg = NULL;

#ifdef _WIN32
    {
        errno_t open_status = fopen_s(&file, path, "rb");
        if (open_status != 0)
            file = NULL;
    }
#else
    file = fopen(path, "rb");
#endif
    if (!file)
    {
        *out_msg = "read_file: could not open file";
        return VIGIL_STATUS_OK;
    }
    if (fseek(file, 0L, SEEK_END) != 0)
    {
        *out_msg = "read_file: could not seek file";
        fclose(file);
        return VIGIL_STATUS_OK;
    }
    long sz = ftell(file);
    if (sz < 0)
    {
        *out_msg = "read_file: could not size file";
        fclose(file);
        return VIGIL_STATUS_OK;
    }
    if (fseek(file, 0L, SEEK_SET) != 0)
    {
        *out_msg = "read_file: could not rewind file";
        fclose(file);
        return VIGIL_STATUS_OK;
    }
    size_t size = (size_t)sz;
    char *data = (char *)alloc->allocate(alloc->user_data, size + 1U);
    if (!data)
    {
        fclose(file);
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "yaml read_file: allocation failed");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    size_t nread = fread(data, 1U, size, file);
    int ferr = ferror(file);
    fclose(file);
    if (nread != size && ferr)
    {
        alloc->deallocate(alloc->user_data, data);
        *out_msg = "read_file: could not read file";
        return VIGIL_STATUS_OK;
    }
    data[nread] = '\0';
    *out_data = data;
    *out_len = nread;
    return VIGIL_STATUS_OK;
}

/* ── yaml.read_file(path) -> (string, err) ───────────────────────── */

static vigil_status_t vigil_yaml_read_file_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "read_file: expected path string", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_allocator_t alloc = get_alloc(vm);
    char *data = NULL;
    size_t data_len = 0;
    const char *msg = NULL;
    vigil_status_t s = yaml_read_file(&alloc, path, &data, &data_len, &msg, error);
    if (s != VIGIL_STATUS_OK)
        return push_str_and_err(vm, "read_file: read failed", error);
    if (msg)
        return push_str_and_err(vm, msg, error);

    /* Parse YAML to JSON */
    vigil_json_value_t *json = NULL;
    s = vigil_yaml_parse(data, data_len, &alloc, &json, error);
    alloc.deallocate(alloc.user_data, data);
    if (s != VIGIL_STATUS_OK)
        return push_str_and_err(vm, "read_file: invalid YAML", error);

    char *json_str = NULL;
    size_t json_len = 0;
    s = vigil_json_emit(json, &json_str, &json_len, error);
    vigil_json_free(&json);
    if (s != VIGIL_STATUS_OK)
        return push_str_and_err(vm, "read_file: emit failed", error);

    s = push_str_and_ok(vm, json_str, json_len, error);
    alloc.deallocate(alloc.user_data, json_str);
    return s;
}

/* ── yaml.write_file(path, json) -> err ──────────────────────────── */

static vigil_status_t vigil_yaml_write_file_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path, *json_str;
    size_t path_len, json_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len) || !get_string_arg(vm, base, 1, &json_str, &json_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_err_only(vm, "write_file: invalid arguments", error);
    }

    vigil_allocator_t alloc = get_alloc(vm);
    vigil_json_value_t *json = NULL;
    vigil_status_t s = vigil_json_parse(&alloc, json_str, json_len, &json, error);
    if (s != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_err_only(vm, "write_file: invalid JSON", error);
    }

    char *buf = NULL;
    size_t cap = 0, len = 0;
    yaml_emit_value(json, &buf, &len, &cap, &alloc, 0, 1);
    vigil_json_free(&json);

    if (buf)
    {
        yaml_emit_str(&buf, &len, &cap, &alloc, "\n", 1);
        buf[len] = '\0';
    }

    vigil_vm_stack_pop_n(vm, arg_count);

    FILE *file = NULL;
#ifdef _WIN32
    {
        errno_t open_status = fopen_s(&file, path, "wb");
        if (open_status != 0)
            file = NULL;
    }
#else
    file = fopen(path, "wb");
#endif
    if (!file)
    {
        alloc.deallocate(alloc.user_data, buf);
        return push_err_only(vm, "write_file: could not open file", error);
    }
    size_t nw = fwrite(buf ? buf : "\n", 1U, buf ? len : 1, file);
    alloc.deallocate(alloc.user_data, buf);
    if (nw != (buf ? len : 1))
    {
        fclose(file);
        return push_err_only(vm, "write_file: could not write file", error);
    }
    if (fclose(file) != 0)
        return push_err_only(vm, "write_file: could not close file", error);

    return push_ok_only(vm, error);
}

/* ══════════════════════════════════════════════════════════════════
 *  Module registration
 * ══════════════════════════════════════════════════════════════════ */

static const int str_param[] = {VIGIL_TYPE_STRING};
static const int str_str_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int str_str_str_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};

static const int str_err_returns[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_ERR};
static const int obj_err_returns[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_ERR};
static const int i32_err_returns[] = {VIGIL_TYPE_I32, VIGIL_TYPE_ERR};

static const vigil_native_type_t array_string_ret = VIGIL_NATIVE_TYPE_ARRAY(VIGIL_TYPE_STRING);

static const char *const pn_yaml[] = {"yaml"};
static const char *const pn_yaml_path[] = {"yaml", "path"};
static const char *const pn_json[] = {"json"};
static const char *const pn_path[] = {"path"};
static const char *const pn_path_json[] = {"path", "json"};
static const char *const pn_yaml_path_value[] = {"yaml", "path", "value"};

static const vigil_native_symbol_doc_t vigil_yaml_module_doc = {
    "YAML parsing and generation.",
    "The yaml module parses YAML 1.2 (block and flow styles) to JSON, with query helpers, stringify, and file I/O.",
    NULL,
};

static const vigil_native_symbol_doc_t doc_parse = {
    "Parse YAML to JSON string.",
    "Parses a YAML document and returns it as a JSON string.",
    "string json, err e = yaml.parse(\"name: test\\ncount: 42\")",
};

static const vigil_native_symbol_doc_t doc_get = {
    "Get value at path from YAML.",
    "Parses YAML and returns the value at the given path. Use dot notation for objects and [n] for arrays.",
    "string val, err e = yaml.get(yaml_str, \"server.host\")",
};

static const vigil_native_symbol_doc_t doc_has = {
    "Check if path exists in YAML.",
    "Returns true if the path exists in the YAML document.",
    "bool exists = yaml.has(yaml_str, \"server.port\")",
};

static const vigil_native_symbol_doc_t doc_type = {
    "Get type at path.",
    "Returns the type name: string, number, bool, null, array, or object.",
    "string t = yaml.type(yaml_str, \"server.port\")",
};

static const vigil_native_symbol_doc_t doc_keys = {
    "Get keys at path.",
    "Returns the keys of the object at the given path.",
    "array<string> k, err e = yaml.keys(yaml_str, \"server\")",
};

static const vigil_native_symbol_doc_t doc_len = {
    "Get length at path.",
    "Returns the length of the array, object, or string at the given path.",
    "i32 n, err e = yaml.len(yaml_str, \"items\")",
};

static const vigil_native_symbol_doc_t doc_stringify = {
    "Convert JSON to YAML.",
    "Converts a JSON string to YAML block style.",
    "string yaml, err e = yaml.stringify(json_str)",
};

static const vigil_native_symbol_doc_t doc_set = {
    "Set value at path.",
    "Sets a value at the given path and returns the updated JSON string.",
    "string updated, err e = yaml.set(yaml_str, \"server.port\", \"9090\")",
};

static const vigil_native_symbol_doc_t doc_read_file = {
    "Read and parse YAML file.",
    "Reads a file, parses YAML, and returns the JSON string.",
    "string json, err e = yaml.read_file(\"config.yaml\")",
};

static const vigil_native_symbol_doc_t doc_write_file = {
    "Write JSON as YAML file.",
    "Converts JSON to YAML and writes to a file.",
    "err e = yaml.write_file(\"config.yaml\", json_str)",
};

static const vigil_native_module_function_t vigil_yaml_functions[] = {
    /* parse(yaml) -> (string, err) */
    {"parse", 5U, vigil_yaml_parse_fn, 1U, str_param, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U,
     pn_yaml, NULL, NULL, &doc_parse},

    /* get(yaml, path) -> (string, err) */
    {"get", 3U, vigil_yaml_get_fn, 2U, str_str_params, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U,
     pn_yaml_path, NULL, NULL, &doc_get},

    /* has(yaml, path) -> bool */
    {"has", 3U, vigil_yaml_has_fn, 2U, str_str_params, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, pn_yaml_path,
     NULL, NULL, &doc_has},

    /* type(yaml, path) -> string */
    {"type", 4U, vigil_yaml_type_fn, 2U, str_str_params, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, pn_yaml_path,
     NULL, NULL, &doc_type},

    /* keys(yaml, path) -> (array<string>, err) */
    {"keys", 4U, vigil_yaml_keys_fn, 2U, str_str_params, VIGIL_TYPE_OBJECT, 2U, obj_err_returns, VIGIL_TYPE_STRING,
     NULL, &array_string_ret, 0U, pn_yaml_path, NULL, "array<string>", &doc_keys},

    /* len(yaml, path) -> (i32, err) */
    {"len", 3U, vigil_yaml_len_fn, 2U, str_str_params, VIGIL_TYPE_I32, 2U, i32_err_returns, 0, NULL, NULL, 0U,
     pn_yaml_path, NULL, NULL, &doc_len},

    /* stringify(json) -> (string, err) */
    {"stringify", 9U, vigil_yaml_stringify_fn, 1U, str_param, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U,
     pn_json, NULL, NULL, &doc_stringify},

    /* set(yaml, path, value) -> (string, err) */
    {"set", 3U, vigil_yaml_set_fn, 3U, str_str_str_params, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U,
     pn_yaml_path_value, NULL, NULL, &doc_set},

    /* read_file(path) -> (string, err) */
    {"read_file", 9U, vigil_yaml_read_file_fn, 1U, str_param, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL,
     0U, pn_path, NULL, NULL, &doc_read_file},

    /* write_file(path, json) -> err */
    {"write_file", 10U, vigil_yaml_write_file_fn, 2U, str_str_params, VIGIL_TYPE_ERR, 1U, NULL, 0, NULL, NULL, 0U,
     pn_path_json, NULL, NULL, &doc_write_file},
};

#define YAML_FUNCTION_COUNT (sizeof(vigil_yaml_functions) / sizeof(vigil_yaml_functions[0]))

VIGIL_API const vigil_native_module_t vigil_stdlib_yaml = {
    "yaml", 4U, vigil_yaml_functions, YAML_FUNCTION_COUNT, NULL, 0U, &vigil_yaml_module_doc, NULL, 0U};
