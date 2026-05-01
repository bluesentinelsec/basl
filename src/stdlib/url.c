/* VIGIL standard library: url module.
 *
 * Provides URL parsing and manipulation per RFC 3986.
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

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"

/* ── Stack helpers ───────────────────────────────────────────────── */

static bool get_string_arg(vigil_vm_t *vm, size_t base, size_t idx, const char **out, size_t *out_len)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    if (!vigil_nanbox_is_object(v))
        return false;
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(v);
    if (!obj || vigil_object_type(obj) != VIGIL_OBJECT_STRING)
        return false;
    *out = vigil_string_object_c_str(obj);
    *out_len = vigil_string_object_length(obj);
    return true;
}

static vigil_status_t push_string(vigil_vm_t *vm, const char *str, size_t len, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_status_t s = vigil_string_object_new(vigil_vm_runtime(vm), str, len, &obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t v;
    vigil_value_init_object(&v, &obj);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

/* ── Multi-return helpers ────────────────────────────────────────── */

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

/* ── Component accessor helper ───────────────────────────────────── */

typedef const char *(*url_field_fn)(const vigil_url_t *url);

static const char *field_scheme(const vigil_url_t *u)
{
    return u->scheme;
}
static const char *field_host(const vigil_url_t *u)
{
    return u->host;
}
static const char *field_port(const vigil_url_t *u)
{
    return u->port;
}
static const char *field_path(const vigil_url_t *u)
{
    return u->path;
}
static const char *field_query(const vigil_url_t *u)
{
    return u->raw_query;
}
static const char *field_fragment(const vigil_url_t *u)
{
    return u->fragment;
}

static vigil_status_t url_component_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error, url_field_fn field,
                                       const char *name)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *url_str;
    size_t url_len;

    if (!get_string_arg(vm, base, 0, &url_str, &url_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "url: argument must be a string", error);
    }

    vigil_url_t url;
    if (vigil_url_parse(vigil_runtime_allocator(vigil_vm_runtime(vm)), url_str, url_len, &url, error) !=
        VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        char msg[128];
        snprintf(msg, sizeof(msg), "url.%s: invalid URL", name);
        return push_str_and_err(vm, msg, error);
    }

    const char *val = field(&url);
    const char *safe = val ? val : "";
    size_t safe_len = val ? strlen(val) : 0;

    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_status_t s = push_str_and_ok(vm, safe, safe_len, error);
    vigil_url_free(&url);
    return s;
}

/* ── url.scheme(url: string) -> (string, err) ────────────────────── */

static vigil_status_t vigil_url_scheme_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return url_component_fn(vm, arg_count, error, field_scheme, "scheme");
}

/* ── url.host(url: string) -> (string, err) ──────────────────────── */

static vigil_status_t vigil_url_host_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return url_component_fn(vm, arg_count, error, field_host, "host");
}

/* ── url.port(url: string) -> (string, err) ──────────────────────── */

static vigil_status_t vigil_url_port_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return url_component_fn(vm, arg_count, error, field_port, "port");
}

/* ── url.path(url: string) -> (string, err) ──────────────────────── */

static vigil_status_t vigil_url_path_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return url_component_fn(vm, arg_count, error, field_path, "path");
}

/* ── url.query(url: string) -> (string, err) ─────────────────────── */

static vigil_status_t vigil_url_query_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return url_component_fn(vm, arg_count, error, field_query, "query");
}

/* ── url.fragment(url: string) -> (string, err) ──────────────────── */

static vigil_status_t vigil_url_fragment_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return url_component_fn(vm, arg_count, error, field_fragment, "fragment");
}

/* ── url.encode(s: string) -> (string, err) ──────────────────────── */

static vigil_status_t vigil_url_encode_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *input;
    size_t input_len;

    if (!get_string_arg(vm, base, 0, &input, &input_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "url.encode: argument must be a string", error);
    }

    const vigil_allocator_t *a = vigil_runtime_allocator(vigil_vm_runtime(vm));
    char *encoded;
    size_t encoded_len;
    if (vigil_url_query_escape(a, input, input_len, &encoded, &encoded_len, error) != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "url.encode: encoding failed", error);
    }

    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_status_t s = push_str_and_ok(vm, encoded, encoded_len, error);
    a->deallocate(a->user_data, encoded);
    return s;
}

/* ── url.decode(s: string) -> (string, err) ──────────────────────── */

static vigil_status_t vigil_url_decode_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *input;
    size_t input_len;

    if (!get_string_arg(vm, base, 0, &input, &input_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "url.decode: argument must be a string", error);
    }

    const vigil_allocator_t *a = vigil_runtime_allocator(vigil_vm_runtime(vm));
    char *decoded;
    size_t decoded_len;
    if (vigil_url_unescape(a, input, input_len, &decoded, &decoded_len, error) != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "url.decode: decoding failed", error);
    }

    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_status_t s = push_str_and_ok(vm, decoded, decoded_len, error);
    a->deallocate(a->user_data, decoded);
    return s;
}

/* ── Module definition ───────────────────────────────────────────── */

static const int str_param[] = {VIGIL_TYPE_STRING};
static const int str_err_returns[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_ERR};
static const char *const url_value_param_names[] = {"url"};
static const char *const url_string_param_names[] = {"s"};

static const vigil_native_symbol_doc_t vigil_url_module_doc = {
    "URL parsing and manipulation.",
    "The url module provides functions for parsing and manipulating URLs according to RFC 3986.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_url_scheme_doc = {
    "Get the scheme (protocol) from a URL.",
    "Returns the scheme component such as \"https\" or \"http\". Returns an error for invalid URLs.",
    "url.scheme(\"https://example.com\")",
};

static const vigil_native_symbol_doc_t vigil_url_host_doc = {
    "Get the hostname from a URL.",
    "Returns the host component without the port. Returns an error for invalid URLs.",
    "url.host(\"https://example.com:8080/path\")",
};

static const vigil_native_symbol_doc_t vigil_url_port_doc = {
    "Get the port from a URL.",
    "Returns the port as a string, or an empty string if not specified. Returns an error for invalid URLs.",
    "url.port(\"https://example.com:8080\")",
};

static const vigil_native_symbol_doc_t vigil_url_path_doc = {
    "Get the path from a URL.",
    "Returns the decoded path component. Returns an error for invalid URLs.",
    "url.path(\"https://example.com/foo/bar\")",
};

static const vigil_native_symbol_doc_t vigil_url_query_doc = {
    "Get the query string from a URL.",
    "Returns the raw query string without the leading '?'. Returns an error for invalid URLs.",
    "url.query(\"https://example.com?a=1&b=2\")",
};

static const vigil_native_symbol_doc_t vigil_url_fragment_doc = {
    "Get the fragment from a URL.",
    "Returns the decoded fragment without the leading '#'. Returns an error for invalid URLs.",
    "url.fragment(\"https://example.com#section\")",
};

static const vigil_native_symbol_doc_t vigil_url_encode_doc = {
    "Percent-encode a string for use in URLs.",
    "Encodes special characters as %XX sequences. Returns an error if encoding fails.",
    "url.encode(\"hello world\")",
};

static const vigil_native_symbol_doc_t vigil_url_decode_doc = {
    "Decode a percent-encoded string.",
    "Decodes %XX sequences and '+' to space. Returns an error if decoding fails.",
    "url.decode(\"hello%20world\")",
};

static const vigil_native_module_function_t vigil_url_functions[] = {
    {"scheme", 6U, vigil_url_scheme_fn, 1U, str_param, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U,
     url_value_param_names, NULL, NULL, &vigil_url_scheme_doc},
    {"host", 4U, vigil_url_host_fn, 1U, str_param, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U,
     url_value_param_names, NULL, NULL, &vigil_url_host_doc},
    {"port", 4U, vigil_url_port_fn, 1U, str_param, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U,
     url_value_param_names, NULL, NULL, &vigil_url_port_doc},
    {"path", 4U, vigil_url_path_fn, 1U, str_param, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U,
     url_value_param_names, NULL, NULL, &vigil_url_path_doc},
    {"query", 5U, vigil_url_query_fn, 1U, str_param, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U,
     url_value_param_names, NULL, NULL, &vigil_url_query_doc},
    {"fragment", 8U, vigil_url_fragment_fn, 1U, str_param, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U,
     url_value_param_names, NULL, NULL, &vigil_url_fragment_doc},
    {"encode", 6U, vigil_url_encode_fn, 1U, str_param, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U,
     url_string_param_names, NULL, NULL, &vigil_url_encode_doc},
    {"decode", 6U, vigil_url_decode_fn, 1U, str_param, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U,
     url_string_param_names, NULL, NULL, &vigil_url_decode_doc},
};

#define URL_FUNCTION_COUNT (sizeof(vigil_url_functions) / sizeof(vigil_url_functions[0]))

VIGIL_API const vigil_native_module_t vigil_stdlib_url = {
    "url", 3U, vigil_url_functions, URL_FUNCTION_COUNT, NULL, 0U, &vigil_url_module_doc, NULL, 0U};
