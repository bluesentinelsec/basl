/* VIGIL standard library: url module.
 *
 * Provides URL parsing and manipulation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/runtime.h"
#include "vigil/type.h"
#include "vigil/url.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_nanbox.h"

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

/* ── url.parse(url: string) -> string ────────────────────────────── */

static vigil_status_t vigil_url_parse_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *url_str;
    size_t url_len;
    vigil_url_t url;
    char result[2048];
    size_t len;

    if (!get_string_arg(vm, base, 0, &url_str, &url_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    if (vigil_url_parse(vigil_runtime_allocator(vigil_vm_runtime(vm)), url_str, url_len, &url, error) !=
        VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    len = (size_t)snprintf(result, sizeof(result), "%s|%s|%s|%s|%s|%s|%s|%s", url.scheme ? url.scheme : "",
                           url.username ? url.username : "", url.password ? url.password : "", url.host ? url.host : "",
                           url.port ? url.port : "", url.path ? url.path : "", url.raw_query ? url.raw_query : "",
                           url.fragment ? url.fragment : "");

    vigil_url_free(&url);
    return push_string(vm, result, len, error);
}

/* ── url.scheme(url: string) -> string ───────────────────────────── */

static vigil_status_t vigil_url_scheme_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *url_str;
    size_t url_len;
    vigil_url_t url;
    vigil_status_t s;

    if (!get_string_arg(vm, base, 0, &url_str, &url_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    if (vigil_url_parse(vigil_runtime_allocator(vigil_vm_runtime(vm)), url_str, url_len, &url, error) !=
        VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    s = push_string(vm, url.scheme ? url.scheme : "", url.scheme ? strlen(url.scheme) : 0, error);
    vigil_url_free(&url);
    return s;
}

/* ── url.host(url: string) -> string ─────────────────────────────── */

static vigil_status_t vigil_url_host_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *url_str;
    size_t url_len;
    vigil_url_t url;
    vigil_status_t s;

    if (!get_string_arg(vm, base, 0, &url_str, &url_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    if (vigil_url_parse(vigil_runtime_allocator(vigil_vm_runtime(vm)), url_str, url_len, &url, error) !=
        VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    s = push_string(vm, url.host ? url.host : "", url.host ? strlen(url.host) : 0, error);
    vigil_url_free(&url);
    return s;
}

/* ── url.port(url: string) -> string ─────────────────────────────── */

static vigil_status_t vigil_url_port_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *url_str;
    size_t url_len;
    vigil_url_t url;
    vigil_status_t s;

    if (!get_string_arg(vm, base, 0, &url_str, &url_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    if (vigil_url_parse(vigil_runtime_allocator(vigil_vm_runtime(vm)), url_str, url_len, &url, error) !=
        VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    s = push_string(vm, url.port ? url.port : "", url.port ? strlen(url.port) : 0, error);
    vigil_url_free(&url);
    return s;
}

/* ── url.path(url: string) -> string ─────────────────────────────── */

static vigil_status_t vigil_url_path_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *url_str;
    size_t url_len;
    vigil_url_t url;
    vigil_status_t s;

    if (!get_string_arg(vm, base, 0, &url_str, &url_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    if (vigil_url_parse(vigil_runtime_allocator(vigil_vm_runtime(vm)), url_str, url_len, &url, error) !=
        VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    s = push_string(vm, url.path ? url.path : "", url.path ? strlen(url.path) : 0, error);
    vigil_url_free(&url);
    return s;
}

/* ── url.query(url: string) -> string ────────────────────────────── */

static vigil_status_t vigil_url_query_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *url_str;
    size_t url_len;
    vigil_url_t url;
    vigil_status_t s;

    if (!get_string_arg(vm, base, 0, &url_str, &url_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    if (vigil_url_parse(vigil_runtime_allocator(vigil_vm_runtime(vm)), url_str, url_len, &url, error) !=
        VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    s = push_string(vm, url.raw_query ? url.raw_query : "", url.raw_query ? strlen(url.raw_query) : 0, error);
    vigil_url_free(&url);
    return s;
}

/* ── url.fragment(url: string) -> string ─────────────────────────── */

static vigil_status_t vigil_url_fragment_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *url_str;
    size_t url_len;
    vigil_url_t url;
    vigil_status_t s;

    if (!get_string_arg(vm, base, 0, &url_str, &url_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    if (vigil_url_parse(vigil_runtime_allocator(vigil_vm_runtime(vm)), url_str, url_len, &url, error) !=
        VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    s = push_string(vm, url.fragment ? url.fragment : "", url.fragment ? strlen(url.fragment) : 0, error);
    vigil_url_free(&url);
    return s;
}

/* ── url.encode(s: string) -> string ─────────────────────────────── */

static vigil_status_t vigil_url_encode_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *input;
    size_t input_len;
    char *encoded;
    size_t encoded_len;
    vigil_status_t s;

    if (!get_string_arg(vm, base, 0, &input, &input_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    const vigil_allocator_t *a = vigil_runtime_allocator(vigil_vm_runtime(vm));
    if (vigil_url_query_escape(a, input, input_len, &encoded, &encoded_len, error) != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    s = push_string(vm, encoded, encoded_len, error);
    a->deallocate(a->user_data, encoded);
    return s;
}

/* ── url.decode(s: string) -> string ─────────────────────────────── */

static vigil_status_t vigil_url_decode_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *input;
    size_t input_len;
    char *decoded;
    size_t decoded_len;
    vigil_status_t s;

    if (!get_string_arg(vm, base, 0, &input, &input_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    const vigil_allocator_t *a = vigil_runtime_allocator(vigil_vm_runtime(vm));
    if (vigil_url_unescape(a, input, input_len, &decoded, &decoded_len, error) != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    s = push_string(vm, decoded, decoded_len, error);
    a->deallocate(a->user_data, decoded);
    return s;
}

/* ── Module definition ───────────────────────────────────────────── */

static const int str_param[] = {VIGIL_TYPE_STRING};

static const vigil_native_module_function_t vigil_url_functions[] = {
    {"parse", 5U, vigil_url_parse_fn, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    {"scheme", 6U, vigil_url_scheme_fn, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    {"host", 4U, vigil_url_host_fn, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    {"port", 4U, vigil_url_port_fn, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    {"path", 4U, vigil_url_path_fn, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    {"query", 5U, vigil_url_query_fn, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    {"fragment", 8U, vigil_url_fragment_fn, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
    {"encode", 6U, vigil_url_encode_fn, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    {"decode", 6U, vigil_url_decode_fn, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
};

#define URL_FUNCTION_COUNT (sizeof(vigil_url_functions) / sizeof(vigil_url_functions[0]))

VIGIL_API const vigil_native_module_t vigil_stdlib_url = {"url", 3U,  vigil_url_functions, URL_FUNCTION_COUNT, NULL,
                                                          0U,    NULL};
